#include "export_engine.h"
#include "export_metadata.h"
#include "render/local_renderer.h"
#include "export_writer.h"
#include "color/color_pipeline.h"
#include "io/source_reader.h"
#include "io/fast_hash.h"
#include "render/cpu_renderer.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryFile>
#include <QSet>
#include <QtEndian>
#include <tiffio.h>
#include <zlib.h>
#include <cmath>
#include <vector>
#include <memory>
#include <algorithm>
#include <limits>

namespace Export {
namespace {
constexpr qint64 maxPixels=45000000, maxIcc=4*1024*1024;
struct ProfileOwner { cmsHPROFILE p=nullptr; ~ProfileOwner(){if(p)cmsCloseProfile(p);} };
struct TransformOwner { cmsHTRANSFORM p=nullptr; ~TransformOwner(){if(p)cmsDeleteTransform(p);} };
struct TiffOwner { TIFF *p=nullptr; ~TiffOwner(){if(p)TIFFClose(p);} };
bool stopped(const std::atomic_bool *c){return c&&c->load();}
TIFF *openTiff(const QString &path,const char *mode){
#ifdef Q_OS_WIN
    auto native=QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if(!native.startsWith(QStringLiteral("\\\\?\\")))native=native.startsWith(QStringLiteral("\\\\"))?QStringLiteral("\\\\?\\UNC\\")+native.mid(2):QStringLiteral("\\\\?\\")+native;
    return TIFFOpenW(reinterpret_cast<const wchar_t *>(native.utf16()),mode);
#else
    return TIFFOpen(QFile::encodeName(path).constData(),mode);
#endif
}
QImage rgbImage(const Pixels &p){
    if(p.channels!=3)return {};
    QImage result(p.size,p.bits==16?QImage::Format_RGBA64:QImage::Format_RGBA8888);
    if(result.isNull())return {};
    for(int y=0;y<p.size.height();++y){
        if(p.bits==16){auto *d=reinterpret_cast<quint16 *>(result.scanLine(y));const auto *s=reinterpret_cast<const quint16 *>(p.data.constData())+qint64(y)*p.size.width()*3;for(int x=0;x<p.size.width();++x){for(int c=0;c<3;++c)d[x*4+c]=s[x*3+c];d[x*4+3]=65535;}}
        else{auto *d=result.scanLine(y);const auto *s=reinterpret_cast<const uchar *>(p.data.constData())+qint64(y)*p.size.width()*3;for(int x=0;x<p.size.width();++x){for(int c=0;c<3;++c)d[x*4+c]=s[x*3+c];d[x*4+3]=255;}}
    }
    result.setColorSpace(QColorSpace::fromIccProfile(p.icc));return result;
}
QImage toScreenRgb(const Pixels &p,QString &error,const std::atomic_bool *cancel=nullptr){
    ProfileOwner input{cmsOpenProfileFromMem(p.icc.constData(),cmsUInt32Number(p.icc.size()))},output{cmsCreate_sRGBProfile()};
    const auto type=p.channels==4?(p.bits==16?TYPE_CMYK_16:TYPE_CMYK_8):(p.bits==16?TYPE_RGB_16:TYPE_RGB_8);
    TransformOwner transform{input.p&&output.p?cmsCreateTransform(input.p,type,output.p,TYPE_RGBA_8,INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_NOCACHE|cmsFLAGS_NOOPTIMIZE):nullptr};
    if(!transform.p){error=QStringLiteral("成片ICC显示转换失败");return {};}
    QImage result(p.size,QImage::Format_RGBA8888);if(result.isNull()){error=QStringLiteral("成片显示分配失败");return {};}
    for(int y=0;y<p.size.height();++y){if(stopped(cancel)){error=QStringLiteral("已取消");return {};}cmsDoTransform(transform.p,p.data.constData()+qint64(y)*p.size.width()*p.channels*(p.bits/8),result.scanLine(y),p.size.width());auto *row=result.scanLine(y);for(int x=0;x<p.size.width();++x)row[x*4+3]=255;}
    result.setColorSpace(QColorSpace::SRgb);return result;
}
bool injectExif(const QString &path,Format format,const QByteArray &exif,QString &error){
    QFile f(path);if(!f.open(QIODevice::ReadOnly)){error=f.errorString();return false;}auto bytes=f.readAll();f.close();
    if(format==Format::Jpeg){
        if(bytes.size()<2||!bytes.startsWith(QByteArray::fromHex("ffd8"))||exif.size()+8>65535){error="EXIF/JPEG size";return false;}
        QByteArray segment=QByteArray::fromHex("ffe1");const auto n=quint16(exif.size()+8);segment.append(char(n>>8));segment.append(char(n));segment.append("Exif\0\0",6);segment+=exif;bytes.insert(2,segment);
    }else if(format==Format::Png){
        if(bytes.size()<33||!bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))){error="PNG header";return false;}
        QByteArray chunk(4,'\0');qToBigEndian<quint32>(quint32(exif.size()),chunk.data());chunk+="eXIf";chunk+=exif;const auto crc=crc32(0,reinterpret_cast<const Bytef *>(chunk.constData()+4),uInt(chunk.size()-4));const int end=chunk.size();chunk.resize(end+4);qToBigEndian<quint32>(crc,chunk.data()+end);bytes.insert(33,chunk);
    }
    if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)||f.write(bytes)!=bytes.size()||!f.flush()){error=f.errorString();return false;}return true;
}
bool tiffWrite(QIODevice &device,const Pixels &pixels,const Settings &s,const PhotoMetadata &m,const std::atomic_bool *cancel,QString &error){
    if(!device.isOpen()||!device.isWritable()||device.isSequential()){error=QStringLiteral("TIFF输出设备不可写或不可定位");return false;}
    const auto rd=[](thandle_t h,void *p,tmsize_t n)->tmsize_t{return static_cast<QIODevice *>(h)->read(static_cast<char *>(p),n);};
    const auto wr=[](thandle_t h,void *p,tmsize_t n)->tmsize_t{return static_cast<QIODevice *>(h)->write(static_cast<const char *>(p),n);};
    const auto seek=[](thandle_t h,toff_t off,int whence)->toff_t{auto *d=static_cast<QIODevice *>(h);const qint64 base=whence==SEEK_CUR?d->pos():whence==SEEK_END?d->size():0;if(off>quint64(std::numeric_limits<qint64>::max()-base)||!d->seek(base+qint64(off)))return toff_t(-1);return toff_t(d->pos());};
    const auto close=[](thandle_t)->int{return 0;};const auto size=[](thandle_t h)->toff_t{return toff_t(static_cast<QIODevice *>(h)->size());};
    TiffOwner t{TIFFClientOpen("LuminaS output","w",&device,rd,wr,seek,close,size,nullptr,nullptr)};if(!t.p){error=QStringLiteral("无法创建TIFF");return false;}
    bool ok=true;const auto set=[&](uint32_t tag,auto... args){if(!TIFFSetField(t.p,tag,args...))ok=false;};
    set(TIFFTAG_IMAGEWIDTH,uint32_t(pixels.size.width()));set(TIFFTAG_IMAGELENGTH,uint32_t(pixels.size.height()));
    set(TIFFTAG_SAMPLESPERPIXEL,pixels.channels);set(TIFFTAG_BITSPERSAMPLE,pixels.bits);
    set(TIFFTAG_PHOTOMETRIC,pixels.channels==4?PHOTOMETRIC_SEPARATED:PHOTOMETRIC_RGB);
    if(pixels.channels==4)set(TIFFTAG_INKSET,INKSET_CMYK);
    set(TIFFTAG_PLANARCONFIG,PLANARCONFIG_CONTIG);set(TIFFTAG_ORIENTATION,ORIENTATION_TOPLEFT);
    set(TIFFTAG_COMPRESSION,s.zip?COMPRESSION_ADOBE_DEFLATE:COMPRESSION_NONE);
    if(s.zip)set(TIFFTAG_PREDICTOR,PREDICTOR_HORIZONTAL);
    set(TIFFTAG_ROWSPERSTRIP,TIFFDefaultStripSize(t.p,0));set(TIFFTAG_RESOLUTIONUNIT,RESUNIT_INCH);
    set(TIFFTAG_XRESOLUTION,float(s.ppi));set(TIFFTAG_YRESOLUTION,float(s.ppi));
    set(TIFFTAG_ICCPROFILE,uint32_t(pixels.icc.size()),pixels.icc.constData());set(TIFFTAG_SOFTWARE,"LuminaS");
    const auto text=[&](uint32_t tag,const QString &value){if(!value.isEmpty())set(tag,value.toUtf8().constData());};
    text(TIFFTAG_MAKE,m.make);text(TIFFTAG_MODEL,m.model);text(TIFFTAG_ARTIST,m.artist);text(TIFFTAG_COPYRIGHT,m.copyright);text(TIFFTAG_DATETIME,m.date);
    if(!ok){error=QStringLiteral("TIFF标签写入失败");return false;}
    const qint64 stride=qint64(pixels.size.width())*pixels.channels*(pixels.bits/8);
    for(int y=0;y<pixels.size.height();++y){if(stopped(cancel)){error=QStringLiteral("已取消");return false;}if(TIFFWriteScanline(t.p,const_cast<char *>(pixels.data.constData()+y*stride),y)<0){error=QStringLiteral("TIFF像素写入失败");return false;}}
    if(!TIFFWriteDirectory(t.p)){error="TIFF directory";return false;}
    // Separate canonical EXIF/GPS directories; no source IFDs are copied.
    toff_t exifOffset=0,gpsOffset=0;
    if(s.metadata==Metadata::Camera){
        TIFFCreateEXIFDirectory(t.p);
        TIFFSetField(t.p,EXIFTAG_EXIFVERSION,"0232");
        TIFFSetField(t.p,EXIFTAG_PIXELXDIMENSION,uint32_t(pixels.size.width()));TIFFSetField(t.p,EXIFTAG_PIXELYDIMENSION,uint32_t(pixels.size.height()));
        if(m.exposure>0)TIFFSetField(t.p,EXIFTAG_EXPOSURETIME,float(m.exposure));if(m.aperture>0)TIFFSetField(t.p,EXIFTAG_FNUMBER,float(m.aperture));if(m.focalLength>0)TIFFSetField(t.p,EXIFTAG_FOCALLENGTH,float(m.focalLength));
        if(m.iso>0){uint16_t iso=uint16_t(std::min(m.iso,65535));TIFFSetField(t.p,EXIFTAG_ISOSPEEDRATINGS,1,&iso);}
        if(!m.date.isEmpty())TIFFSetField(t.p,EXIFTAG_DATETIMEORIGINAL,m.date.toUtf8().constData());
        if(!m.lens.isEmpty())TIFFSetField(t.p,EXIFTAG_LENSMODEL,m.lens.toUtf8().constData());
        if(!TIFFWriteCustomDirectory(t.p,&exifOffset)){error="EXIF directory";return false;}
    }
    if(s.gps&&m.hasGps){
        TIFFCreateGPSDirectory(t.p);const uint8_t version[]{2,3,0,0};TIFFSetField(t.p,GPSTAG_VERSIONID,version);
        const auto dms=[](double value,double *out){value=std::abs(value);out[0]=std::floor(value);value=(value-out[0])*60;out[1]=std::floor(value);out[2]=(value-out[1])*60;};
        double lat[3],lon[3];dms(m.latitude,lat);dms(m.longitude,lon);
        TIFFSetField(t.p,GPSTAG_LATITUDEREF,m.latitude<0?"S":"N");TIFFSetField(t.p,GPSTAG_LONGITUDEREF,m.longitude<0?"W":"E");
        TIFFSetField(t.p,GPSTAG_LATITUDE,lat);TIFFSetField(t.p,GPSTAG_LONGITUDE,lon);TIFFSetField(t.p,GPSTAG_ALTITUDEREF,m.altitude<0?1:0);TIFFSetField(t.p,GPSTAG_ALTITUDE,std::abs(m.altitude));
        if(!TIFFWriteCustomDirectory(t.p,&gpsOffset)){error="GPS directory";return false;}
    }
    if(exifOffset||gpsOffset){if(!TIFFSetDirectory(t.p,0)){error="TIFF primary directory";return false;}if(exifOffset)TIFFSetField(t.p,TIFFTAG_EXIFIFD,exifOffset);if(gpsOffset)TIFFSetField(t.p,TIFFTAG_GPSIFD,gpsOffset);if(!TIFFRewriteDirectory(t.p)){error="TIFF metadata commit";return false;}}
    if(TIFFFlush(t.p)!=1){error=QStringLiteral("TIFF写入/刷新失败，可能空间不足");return false;}return true;
}
}
bool writeTiff(QIODevice &device,const Pixels &pixels,const Settings &s,const PhotoMetadata &metadata,QString &error,const std::atomic_bool *cancel){return tiffWrite(device,pixels,s,metadata,cancel,error);}
QByteArray builtinProfile(const QString &name,QString &error){
    error.clear();QColorSpace space;
    if(name=="sRGB")space=QColorSpace(QColorSpace::SRgb);
    else if(name=="Display P3")space=QColorSpace(QColorSpace::DisplayP3);
    else if(name=="Adobe RGB")space=QColorSpace(QColorSpace::AdobeRgb);
    else if(name=="ProPhoto RGB")space=QColorSpace(QColorSpace::ProPhotoRgb);
    else{error=QStringLiteral("未知RGB色域");return {};}
    auto bytes=space.iccProfile();if(bytes.isEmpty())error=QStringLiteral("无法生成ICC");return bytes;
}
bool validProfile(const QByteArray &icc,Mode mode,QString &error){
    error.clear();if(icc.size()<128||icc.size()>maxIcc){error=QStringLiteral("ICC长度无效（上限4MiB）");return false;}
    ProfileOwner profile{cmsOpenProfileFromMem(icc.constData(),cmsUInt32Number(icc.size()))};
    if(!profile.p||cmsGetColorSpace(profile.p)!=(mode==Mode::Cmyk?cmsSigCmykData:cmsSigRgbData)){error=QStringLiteral("ICC损坏或色彩模式不匹配");return false;}
    const auto c=cmsGetDeviceClass(profile.p);
    if(c!=cmsSigDisplayClass&&c!=cmsSigOutputClass&&c!=cmsSigColorSpaceClass){error=QStringLiteral("不支持此类输出ICC");return false;}
    const int version=cmsGetEncodedICCversion(profile.p)>>24;
    if((version!=2&&version!=4)||!cmsIsIntentSupported(profile.p,INTENT_RELATIVE_COLORIMETRIC,LCMS_USED_AS_OUTPUT)){error=QStringLiteral("ICC不支持输出转换");return false;}
    return true;
}
QList<Profile> installedProfiles(){
    QList<Profile> result;QStringList dirs;
#ifdef Q_OS_WIN
    dirs<<qEnvironmentVariable("SystemRoot",QStringLiteral("C:/Windows"))+"/System32/spool/drivers/color";
#else
    dirs<<"/usr/share/color/icc"<<QDir::homePath()+"/.local/share/color/icc";
#endif
    for(const auto &dir:dirs){int count=0;for(const auto &fi:QDir(dir).entryInfoList(QDir::Files,QDir::Name)){
        if(++count>1024)break;if(fi.suffix().compare("icc",Qt::CaseInsensitive)&&fi.suffix().compare("icm",Qt::CaseInsensitive))continue;
        if(fi.size()>maxIcc)continue;QFile file(fi.absoluteFilePath());if(!file.open(QIODevice::ReadOnly))continue;const auto icc=file.read(maxIcc+1);
        for(auto mode:{Mode::Rgb,Mode::Cmyk}){QString error;if(validProfile(icc,mode,error)){ProfileOwner p{cmsOpenProfileFromMem(icc.constData(),cmsUInt32Number(icc.size()))};char name[256]{};cmsGetProfileInfoASCII(p.p,cmsInfoDescription,"en","US",name,256);result.append({QString::fromUtf8(name).isEmpty()?fi.baseName():QString::fromUtf8(name),fi.absoluteFilePath(),icc,mode});break;}}
    }}return result;
}
Pixels convert(const QImage &linear,const Settings &s,QString &error,const std::atomic_bool *cancel){
    error=validate(s);if(!error.isEmpty())return {};
    if(linear.isNull()||linear.format()!=QImage::Format_RGBA32FPx4||qint64(linear.width())*linear.height()>maxPixels){error=QStringLiteral("导出浮点图像无效");return {};}
    Pixels out;out.size=linear.size();out.bits=s.bits;out.channels=s.mode==Mode::Cmyk?4:3;out.icc=s.icc.isEmpty()?builtinProfile(s.profileName,error):s.icc;
    if(!validProfile(out.icc,s.mode,error))return {};
    ProfileOwner input{workProfile()},output{cmsOpenProfileFromMem(out.icc.constData(),cmsUInt32Number(out.icc.size()))};
    if(!cmsIsIntentSupported(output.p,s.intent,LCMS_USED_AS_OUTPUT)){error=QStringLiteral("所选ICC不支持此转换意图");return {};}
    const auto type=out.channels==4?(s.bits==16?TYPE_CMYK_16:TYPE_CMYK_8):(s.bits==16?TYPE_RGB_16:TYPE_RGB_8);
    TransformOwner transform{cmsCreateTransform(input.p,TYPE_RGBA_FLT,output.p,type,s.intent,cmsFLAGS_NOOPTIMIZE|cmsFLAGS_NOCACHE|(s.blackPoint?cmsFLAGS_BLACKPOINTCOMPENSATION:0))};
    if(!transform.p){error=QStringLiteral("目标ICC转换失败");return {};}
    const qint64 stride=qint64(out.size.width())*out.channels*(out.bits/8);out.data.resize(stride*out.size.height());
    if(out.data.isEmpty()){error=QStringLiteral("导出分配失败");return {};}
    std::vector<float> row(size_t(linear.width())*4);
    for(int y=0;y<linear.height();++y){
        if(stopped(cancel)){error=QStringLiteral("已取消");return {};}
        const auto *src=reinterpret_cast<const float *>(linear.constScanLine(y));
        for(int x=0;x<linear.width();++x){const float a=src[x*4+3];if(!std::isfinite(a)){error="Nonfinite alpha";return {};}for(int c=0;c<3;++c){const float v=src[x*4+c];if(!std::isfinite(v)){error="Nonfinite pixel";return {};}row[size_t(x)*4+c]=v*std::clamp(a,0.f,1.f)+(1-std::clamp(a,0.f,1.f));}row[size_t(x)*4+3]=1;}
        cmsDoTransform(transform.p,row.data(),out.data.data()+y*stride,out.size.width());
    }return out;
}
QImage preview(const QImage &linear,const Settings &s,QString &error,const std::atomic_bool *cancel){auto pixels=convert(linear,s,error,cancel);if(pixels.data.isEmpty())return {};return toScreenRgb(pixels,error,cancel);}
Result execute(const Job &job,const std::atomic_bool *cancel,const Progress &progress){
    Result result;result.path=job.path;auto stage=[&](const QString &text){if(progress)progress(text);};
    const auto cancelled=[&]{if(stopped(cancel)){result.cancelled=true;result.error=QStringLiteral("已取消");return true;}return false;};
    if(cancelled())return result;
    result.error=validate(job.settings);if(!result.error.isEmpty())return result;
    if(!validateJobDocument(job,result.error))return result;
    const auto source=QFileInfo(job.source).canonicalFilePath();
    if(source.isEmpty()||job.hash.size()!=64||!validRecipe(job.recipe)){result.error=QStringLiteral("源文件、哈希或配方无效");return result;}
    if(QFileInfo(job.path).absoluteFilePath().compare(source,Qt::CaseInsensitive)==0){result.error=QStringLiteral("禁止覆盖原图");return result;}
    if(QFileInfo::exists(job.path)){result.skipped=true;result.error=QStringLiteral("目标已存在，未覆盖");return result;}
    // The queue owns this atomic until synchronous execute returns; its
    // destructor joins the pool. The token never escapes this call.
    const CancellationToken sourceCancel=cancel?CancellationToken(cancel,[](const std::atomic_bool *){}):CancellationToken{};
    stage(QStringLiteral("读取原图"));auto frame=readSource(source,job.recipe.warmth,job.recipe.tint,sourceCancel,job.hash);
    if(!frame.source){if(cancelled())return result;result.error=frame.error;return result;}
    if(frame.source->sha256.compare(job.hash,Qt::CaseInsensitive)){result.error=QStringLiteral("源文件已变化，请重新创建导出任务");return result;}
    if(cancelled())return result;
    const SourceMetadataSnapshot metadataSnapshot{frame.source->path,frame.source->sha256,frame.source->deliveryMetadata};const bool hasMetadata=frame.source->hasDeliveryMetadata;
    stage(QStringLiteral("全尺寸渲染"));auto linear=job.document?
        LocalEdit::renderDocumentFullLinear(*frame.source,*job.document,result.error,sourceCancel):
        renderLinear(*frame.source,job.recipe,0,result.error,{},sourceCancel);
    frame.source.reset();if(linear.isNull()){cancelled();return result;}
    if(cancelled())return result;
    const auto target=outputSize(linear.size(),job.settings,result.error);if(!target.isValid())return result;
    if(target!=linear.size()){stage(QStringLiteral("调整输出尺寸"));linear=linear.scaled(target,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(linear.isNull()){result.error=QStringLiteral("缩放分配失败");return result;}}
    stage(QStringLiteral("转换目标色彩"));auto pixels=convert(linear,job.settings,result.error,cancel);linear={};if(pixels.data.isEmpty()){if(stopped(cancel))result.cancelled=true;return result;}
    if(cancelled())return result;
    auto metadata=readMetadata(source,job.settings.metadata,job.settings.gps,result.error,hasMetadata?&metadataSnapshot:nullptr);if(!result.error.isEmpty())return result;
    const auto parent=QFileInfo(job.path).absolutePath();if(!QDir().mkpath(parent)){result.error=QStringLiteral("无法创建导出目录");return result;}
    QTemporaryFile temporary(parent+"/.luminas-XXXXXX.tmp");if(!temporary.open()){result.error=temporary.errorString();return result;}const auto temp=temporary.fileName();temporary.close();
    stage(QStringLiteral("编码写入"));bool encoded=false;
    if(job.settings.format==Format::Tiff){QFile output(temp);if(!output.open(QIODevice::ReadWrite|QIODevice::Truncate)){result.error=output.errorString();return result;}encoded=writeTiff(output,pixels,job.settings,metadata,result.error,cancel);if(encoded&&!output.flush()){encoded=false;result.error=output.errorString();}}
    else{
        auto image=rgbImage(pixels);if(image.isNull()){result.error=QStringLiteral("RGB编码缓冲分配失败");return result;}
        image.setDotsPerMeterX(int(std::lround(job.settings.ppi/0.0254)));image.setDotsPerMeterY(int(std::lround(job.settings.ppi/0.0254)));
        QImageWriter writer(temp,job.settings.format==Format::Jpeg?"jpeg":"png");writer.setQuality(job.settings.quality);
        encoded=writer.write(image);if(!encoded)result.error=writer.errorString();
        if(encoded)encoded=injectExif(temp,job.settings.format,exifBlock(metadata,pixels.size,job.settings.ppi),result.error);
    }
    if(cancelled())return result;if(!encoded){if(result.error.isEmpty())result.error=QStringLiteral("文件编码失败");return result;}
    pixels.data.clear();stage(QStringLiteral("校验成片"));
    // Decode the real file, not an in-memory renderer buffer, before commit.
    const auto checked=readOutput(temp,result.error);if(checked.isNull()||checked.size()!=target){if(result.error.isEmpty())result.error=QStringLiteral("成片尺寸校验失败");return result;}
    if(cancelled())return result;
    QFile sourceCheck(source);QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!sourceCheck.open(QIODevice::ReadOnly)||!hash.addData(&sourceCheck)||QString::fromLatin1(hash.result().toHex()).compare(job.hash,Qt::CaseInsensitive)){result.error=QStringLiteral("导出期间原图变化，未提交成片");return result;}
    stage(QStringLiteral("提交文件"));
    // QTemporaryFile retains its native handle even after close() on Windows.
    // Its own rename closes that handle and performs a same-volume rename,
    // refusing an existing destination (no QFile copy fallback).
    if(!temporary.rename(job.path)){result.error=QStringLiteral("提交失败：目标已存在或目录不可写；未覆盖文件：")+temporary.errorString();return result;}
    temporary.setAutoRemove(false);
    result.ok=true;result.size=target;result.error.clear();return result;
}
QImage readOutput(const QString &path,QString &error){
    error.clear();QFile f(path);if(!f.open(QIODevice::ReadOnly)){error=f.errorString();return {};}const auto signature=f.read(4);f.close();
    if(signature==QByteArray("II*\0",4)||signature==QByteArray("MM\0*",4)){
        TiffOwner t{openTiff(path,"r")};if(!t.p){error="TIFF open";return {};}
        uint32_t w=0,h=0;uint16_t samples=0,bits=0,photo=0,planar=0,orientation=0;
        TIFFGetField(t.p,TIFFTAG_IMAGEWIDTH,&w);TIFFGetField(t.p,TIFFTAG_IMAGELENGTH,&h);TIFFGetField(t.p,TIFFTAG_SAMPLESPERPIXEL,&samples);TIFFGetField(t.p,TIFFTAG_BITSPERSAMPLE,&bits);TIFFGetField(t.p,TIFFTAG_PHOTOMETRIC,&photo);TIFFGetFieldDefaulted(t.p,TIFFTAG_PLANARCONFIG,&planar);TIFFGetFieldDefaulted(t.p,TIFFTAG_ORIENTATION,&orientation);
        if(!w||!h||qint64(w)*h>maxPixels||(bits!=8&&bits!=16)||planar!=PLANARCONFIG_CONTIG||orientation!=ORIENTATION_TOPLEFT||!((photo==PHOTOMETRIC_RGB&&samples==3)||(photo==PHOTOMETRIC_SEPARATED&&samples==4))){error=QStringLiteral("不支持的成片TIFF结构");return {};}
        Pixels p;p.size=QSize(w,h);p.bits=bits;p.channels=samples;uint32_t n=0;void *icc=nullptr;
        if(!TIFFGetField(t.p,TIFFTAG_ICCPROFILE,&n,&icc)||n>maxIcc){error="TIFF ICC missing/large";return {};}p.icc=QByteArray(static_cast<const char *>(icc),n);
        if(!validProfile(p.icc,samples==4?Mode::Cmyk:Mode::Rgb,error))return {};
        const qint64 stride=qint64(w)*samples*(bits/8);if(TIFFScanlineSize64(t.p)!=uint64_t(stride)){error="TIFF stride";return {};}
        p.data.resize(stride*h);for(uint32_t y=0;y<h;++y)if(TIFFReadScanline(t.p,p.data.data()+y*stride,y)<0){error="TIFF scanline";return {};}
        return toScreenRgb(p,error);
    }
    QImageReader reader(path);reader.setDecideFormatFromContent(true);reader.setAutoTransform(true);const auto size=reader.size();
    if(!size.isValid()||qint64(size.width())*size.height()>maxPixels){error=QStringLiteral("成片尺寸超过45MP或无效");return {};}
    auto image=reader.read();if(image.isNull()){error=reader.errorString();return {};}
    if(!image.colorSpace().isValid()){error=QStringLiteral("成片没有有效ICC");return {};}
    return image.convertedToColorSpace(QColorSpace::SRgb);
}
}
