#include "source_reader.h"
#include "preview_size.h"
#include "color/color_pipeline.h"
#include <libraw/libraw.h>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QMap>
#include <cmath>
#include <vector>
#include <algorithm>
#include <array>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrentRun>
static QByteArray hashFile(const QString &path) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256); if(!hash.addData(&file))return {};
    return hash.result().toHex();
}
static QByteArray jpegIcc(const QString &path,QString &error) {
    QFile file(path); if(!file.open(QIODevice::ReadOnly)||file.read(2)!=QByteArray::fromHex("ffd8")){error="JPEG header";return {};}
    QMap<int,QByteArray> segments; int total=0,bytes=0;
    while(!file.atEnd()) {
        char lead=0,marker=0; if(!file.getChar(&lead)||quint8(lead)!=255){error="JPEG marker";return {};}
        do{if(!file.getChar(&marker)){error="JPEG EOF";return {};}}while(quint8(marker)==255);
        if(quint8(marker)==0xDA||quint8(marker)==0xD9)break;
        const auto lengthBytes=file.read(2);if(lengthBytes.size()!=2){error="JPEG length";return {};}
        const int length=(quint8(lengthBytes[0])<<8)+quint8(lengthBytes[1])-2;
        if(length<0){error="JPEG length";return {};}
        const auto data=file.read(length);if(data.size()!=length){error="JPEG segment EOF";return {};}
        if(quint8(marker)==0xE2 && data.startsWith(QByteArray("ICC_PROFILE\0",12))) {
            if(data.size()<14){error="ICC segment";return {};}
            const int index=quint8(data[12]),count=quint8(data[13]);
            if(count<1||index<1||index>count||segments.contains(index)||(total && total!=count)){error="ICC sequence";return {};}
            total=count;bytes+=data.size()-14;if(bytes>4*1024*1024){error="ICC exceeds 4MiB";return {};}
            segments[index]=data.mid(14);
        }
    }
    if(total!=segments.size()){error="Incomplete ICC";return {};}
    QByteArray result;for(int i=1;i<=total;++i)result+=segments[i];return result;
}
FrameResult readSource(const QString &path,double warmth,double tint) {
    QElapsedTimer phase;phase.start();QJsonObject stages;
    const auto phaseDone=[&](const char *name){stages.insert(name,double(phase.elapsed()));phase.restart();};
    auto frame=std::make_shared<SourceFrame>(); frame->path=path;
    const auto before=hashFile(path); if(before.isEmpty())return {{},QStringLiteral("无法读取源文件")};
    phaseDone("hashBeforeMs");
    frame->sha256=QString::fromLatin1(before);
    QElapsedTimer timer;timer.start(); QString error;
    frame->raw=QFileInfo(path).suffix().compare("cr3",Qt::CaseInsensitive)==0;
    if(!frame->raw) {
        QImageReader reader(path);reader.setDecideFormatFromContent(true);reader.setAutoTransform(true);
        if(reader.format()!="jpeg"||!acceptedImageSize(reader.size()))return {{},QStringLiteral("不是有效的 ≤45MP JPEG")};
        const auto icc=jpegIcc(path,error);if(!error.isEmpty())return {{},error};
        const auto encoded=reader.read();if(encoded.isNull())return {{},reader.errorString()};
        phaseDone("jpegHeaderDecodeMs");
        frame->linear=inputToWork(encoded,icc,error); if(!error.isEmpty())return {{},error};
        phaseDone("jpegWorkConvertMs");
        frame->colorNote=icc.isEmpty()?QStringLiteral("无嵌入ICC，按sRGB → 线性Rec.2020") : QStringLiteral("嵌入ICC → 线性Rec.2020");
        frame->facts={{"embeddedIccBytes",icc.size()},{"iccSha256",QString::fromLatin1(QCryptographicHash::hash(icc,QCryptographicHash::Sha256).toHex())},{"sourceKind","rendered JPEG"}};
    } else {
        if(!std::isfinite(warmth)||!std::isfinite(tint)||std::abs(warmth)>1||std::abs(tint)>1)return {{},QStringLiteral("无效 RAW 白平衡")};
        LibRaw raw; if(LibRaw::versionNumber()!=LIBRAW_MAKE_VERSION(0,22,2))return {{},"LibRaw ABI/version"};
        auto &p=raw.imgdata.params;
        raw.imgdata.rawparams.max_raw_memory_mb=1024;raw.imgdata.rawparams.use_rawspeed=0;
        p.output_color=0;p.output_bps=16;p.gamm[0]=p.gamm[1]=1;p.no_auto_bright=1;p.no_auto_scale=0;
        p.use_camera_wb=0;p.use_auto_wb=0;p.use_camera_matrix=1;p.user_qual=3;p.half_size=0;
        p.highlight=1;p.adjust_maximum_thr=0;p.bright=1;p.user_flip=-1;
#ifdef Q_OS_WIN
        int code=raw.open_file(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
        int code=raw.open_file(QFile::encodeName(path).constData());
#endif
        auto failure=[&](int status){return FrameResult{{},QStringLiteral("RAW 输入失败：")+LibRaw::strerror(status)};};
        if(code!=LIBRAW_SUCCESS)return failure(code);
        phaseDone("rawOpenMs");
        if(raw.imgdata.idata.colors!=3 || raw.imgdata.idata.filters<=1000)return {{},QStringLiteral("S2 当前仅验证三通道 Bayer RAW")};
        auto &s=raw.imgdata.sizes; if(s.width<1||s.height<1)return {{},"Invalid RAW dimensions"};
        raw.adjust_to_raw_inset_crop(1);if(!acceptedImageSize({s.width,s.height}))return {{},"RAW crop >45MP"};
        double multipliers[4];
        for(int c=0;c<4;++c){ multipliers[c]=raw.imgdata.color.cam_mul[c];if(c==3 && multipliers[c]<=0)multipliers[c]=multipliers[1];if(!std::isfinite(multipliers[c])||multipliers[c]<=0)return {{},QStringLiteral("RAW 缺有效相机WB")}; }
        multipliers[0]*=std::exp2(warmth);multipliers[2]*=std::exp2(-warmth);
        multipliers[1]*=std::exp2(tint);multipliers[3]*=std::exp2(tint);
        const double normalization=*std::max_element(multipliers,multipliers+4)/ *std::min_element(multipliers,multipliers+4);
        for(int c=0;c<4;++c)p.user_mul[c]=float(multipliers[c]);
        if((code=raw.unpack())!=LIBRAW_SUCCESS)return failure(code);
        phaseDone("rawUnpackMs");
        if((code=raw.dcraw_process())!=LIBRAW_SUCCESS)return failure(code);
        phaseDone("rawDevelopMs");
        float matrix[3][3];for(int i=0;i<3;++i)for(int j=0;j<3;++j)matrix[i][j]=raw.imgdata.color.rgb_cam[i][j];
        int width,height,colors,bits;raw.get_mem_image_format(&width,&height,&colors,&bits);
        if(colors!=3||bits!=16||!acceptedImageSize({width,height}))return {{},"RAW RGB16 format"};
        QImage camera(width,height,QImage::Format_RGBX64);if(camera.isNull())return {{},"RAW allocation"};
        if((code=raw.copy_mem_image(camera.bits(),int(camera.bytesPerLine()),0))!=LIBRAW_SUCCESS)return failure(code);
        raw.recycle();
        phaseDone("rawRgb16CopyMs");
        frame->linear=QImage(width,height,QImage::Format_RGBA32FPx4);if(frame->linear.isNull())return {{},"float allocation"};
        auto input=linearSrgbProfile();auto work=workProfile();
        auto transform=cmsCreateTransform(input,TYPE_RGBA_FLT,work,TYPE_RGBA_FLT,INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_COPY_ALPHA|cmsFLAGS_NOOPTIMIZE|cmsFLAGS_NOCACHE);
        const int workers=std::clamp(QThread::idealThreadCount(),1,std::min(6,height));
        std::array<cmsHTRANSFORM,6> transforms{};transforms[0]=transform;
        if(transform)for(int i=1;i<workers;++i){transforms[i]=cmsCreateTransform(input,TYPE_RGBA_FLT,work,TYPE_RGBA_FLT,INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_COPY_ALPHA|cmsFLAGS_NOOPTIMIZE|cmsFLAGS_NOCACHE);if(!transforms[i]){for(auto t:transforms)if(t)cmsDeleteTransform(t);cmsCloseProfile(input);cmsCloseProfile(work);return {{},"RAW worker transform allocation"};}}
        cmsCloseProfile(input);cmsCloseProfile(work);if(!transform)return {{},"RAW float matrix/ICC"};
        const float basis[]{1,0,0,1,0,1,0,1,0,0,1,1};float workBasis[12];cmsDoTransform(transform,basis,workBasis,3);
        for(int c=0;c<3;++c)for(int j=0;j<3;++j)for(int k=0;k<3;++k)frame->cameraToWork[c*3+j]+=workBasis[k*4+c]*matrix[k][j];
        for(int c=0;c<4;++c)frame->cameraWB[c]=multipliers[c];
        auto *destination=frame->linear.bits();const auto stride=frame->linear.bytesPerLine();std::array<double,6> minima{},maxima{};
        const auto rows=[&](int part){std::vector<float> row(size_t(width)*4);double minimum=0,maximum=0;
        for(int y=height*part/workers;y<height*(part+1)/workers;++y){
            const auto *packed=reinterpret_cast<const quint16 *>(camera.constScanLine(y));
            for(int x=0;x<width;++x){
                for(int c=0;c<3;++c){double v=0;for(int j=0;j<3;++j)v+=matrix[c][j]*packed[x*3+j];row[size_t(x)*4+c]=float(v/65535*normalization);}
                row[size_t(x)*4+3]=1;
            }
            cmsDoTransform(transforms[part],row.data(),destination+y*stride,cmsUInt32Number(width));
            const auto *values=reinterpret_cast<const float *>(destination+y*stride);
            for(int x=0;x<width;++x)for(int c=0;c<3;++c){minimum=std::min(minimum,double(values[x*4+c]));maximum=std::max(maximum,double(values[x*4+c]));}
        }minima[part]=minimum;maxima[part]=maximum;};
        static QThreadPool pool;pool.setMaxThreadCount(6);QFuture<void> jobs[6];for(int i=0;i<workers;++i)jobs[i]=QtConcurrent::run(&pool,[&,i]{rows(i);});for(int i=0;i<workers;++i)jobs[i].waitForFinished();
        const double minimum=*std::min_element(minima.begin(),minima.begin()+workers),maximum=*std::max_element(maxima.begin(),maxima.begin()+workers);
        for(auto t:transforms)if(t)cmsDeleteTransform(t);
        phaseDone("rawWorkConvertMs");
        frame->rawWarmth=warmth;frame->rawTint=tint;
        frame->colorNote=QStringLiteral("RAW相机通道 → 浮点矩阵 → 线性Rec.2020；高光/肤色待验证");
        frame->facts={{"highlight",1},{"outputColor",0},{"normalization",normalization},{"workMin",minimum},{"workMax",maximum},
            {"contract","Bayer AHD camera RGB16 with max-WB scaling; float matrix preserves negative/superwhite; sensor/black-floor clipping not recovered"}};
    }
    if(frame->linear.isNull())return {{},QStringLiteral("源图读取失败")};
    if(hashFile(path)!=before)return {{},QStringLiteral("源文件处理期间变化")};
    phaseDone("hashAfterMs");frame->facts.insert("readStages",stages);
    frame->facts.insert("elapsedMs",double(timer.elapsed()));
    frame->facts.insert("width",frame->linear.width());frame->facts.insert("height",frame->linear.height());
    frame->originalSize=frame->linear.size();return {frame,{}};
}
