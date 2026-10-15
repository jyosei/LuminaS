#include "raw_decoder.h"
#include "packed_rgb16.h"
#include "io/preview_size.h"
#include <libraw/libraw.h>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QJsonArray>
#include <QtEndian>
#include <memory>
#include <cstring>
#include <array>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

static QJsonObject processPeak() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)))
        return {{"peakWorkingSetBytes",double(counters.PeakWorkingSetSize)},
                {"peakPrivateCommitBytes",double(counters.PeakPagefileUsage)},
                {"scope","Windows process-lifetime high-water marks, not per-stage/whole-machine/GPU measurements"}};
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return {{"peakRssPlatformUnits",double(usage.ru_maxrss)},{"scope","process lifetime; platform units"}};
#endif
    return {{"unavailable",true}};
}
static QByteArray fileHash(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}
RawResult developRaw(const QString &path, const RawOptions &options) {
    RawResult result;
    QElapsedTimer total, stage; total.start();
    const auto beforeHash = fileHash(path);
    const auto sourceHashBeforeMs = total.elapsed();
    if (beforeHash.isEmpty()) { result.error = QStringLiteral("无法读取 RAW 源文件"); return result; }
    LibRaw raw;
    result.report = {{"librawVersion",LibRaw::version()},{"runtimeCapabilities",int(LibRaw::capabilities())},
        {"file",QFileInfo(path).fileName()},{"sourceSha256",QString::fromLatin1(beforeHash)},
        {"pipeline","open_file -> vendor inset crop -> unpack -> dcraw_process -> RGB16 output; no embedded preview"},
        {"linearOutputContract","RGB16 linear sRGB primaries; WB/scaled/clipped by LibRaw; not unbounded scene-linear master"}};
    auto fail = [&](const QString &message) {
        result.error = message; result.report.insert("error",message);
        result.report.insert("elapsedMs",double(total.elapsed())); result.report.insert("memory",processPeak());
        return result;
    };
    static_assert(LIBRAW_VERSION == LIBRAW_MAKE_VERSION(0,22,2), "LibRaw 0.22.2 headers required");
    if (LibRaw::versionNumber() != LIBRAW_MAKE_VERSION(0,22,2)) return fail(QStringLiteral("LibRaw 版本必须为 0.22.2"));
    if ((options.quality != 3 && options.quality != 4) || options.longEdge < 1) return fail(QStringLiteral("无效 RAW 参数"));
    auto &p = raw.imgdata.params;
    raw.imgdata.rawparams.max_raw_memory_mb = 1024;
    raw.imgdata.rawparams.use_rawspeed = 0;
    p.use_camera_wb = 1; p.use_auto_wb = 0; p.use_camera_matrix = 1;
    p.half_size = 0; p.output_color = 1; p.output_bps = 16;
    p.gamm[0] = 1; p.gamm[1] = 1;
    p.no_auto_bright = 1; p.no_auto_scale = 0; p.bright = 1;
    p.highlight = 0; p.adjust_maximum_thr = 0;
    p.user_qual = options.quality; p.user_flip = -1;
    p.dcb_iterations = 2; p.dcb_enhance_fl = 0;
    p.med_passes = 0; p.fbdd_noiserd = 0; p.threshold = 0;
    result.report.insert("parameters",QJsonObject{{"algorithm",options.quality == 3 ? "AHD" : "DCB"},
        {"userQual",p.user_qual},{"cameraWb",true},{"autoWb",false},{"cameraMatrix",1},
        {"halfSize",false},{"outputColor",1},{"outputBits",16},{"gammaPower",1},{"gammaSlope",1},
        {"noAutoBright",true},{"noAutoScale",false},{"bright",1},{"highlight",0},{"adjustMaximumThreshold",0},
        {"dcbIterations",2},{"dcbEnhance",false},{"medianPasses",0},{"fbdd",0},{"denoiseThreshold",0},
        {"rawspeed",false},{"maxRawAllocationMiB",1024},{"previewLongEdge",options.longEdge}});
    stage.start();
#ifdef Q_OS_WIN
    int code = raw.open_file(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
    int code = raw.open_file(QFile::encodeName(path).constData());
#endif
    QJsonObject times{{"openMs",double(stage.elapsed())}};
    times.insert("sourceHashBeforeMs",double(sourceHashBeforeMs));
    if (code != LIBRAW_SUCCESS) return fail(QStringLiteral("RAW 打开失败：") + LibRaw::strerror(code));
    auto &s = raw.imgdata.sizes;
    result.report.insert("metadata",QJsonObject{{"make",raw.imgdata.idata.make},{"model",raw.imgdata.idata.model},
        {"rawWidth",s.raw_width},{"rawHeight",s.raw_height},{"visibleWidthBeforeCrop",s.width},{"visibleHeightBeforeCrop",s.height},
        {"flip",s.flip},{"iso",raw.imgdata.other.iso_speed},{"shutterSeconds",raw.imgdata.other.shutter},
        {"aperture",raw.imgdata.other.aperture},{"focalLength",raw.imgdata.other.focal_len},
        {"black",int(raw.imgdata.color.black)},{"maximum",int(raw.imgdata.color.maximum)},
        {"wbR",raw.imgdata.color.cam_mul[0]},{"wbG",raw.imgdata.color.cam_mul[1]},{"wbB",raw.imgdata.color.cam_mul[2]},
        {"rawCount",int(raw.imgdata.idata.raw_count)}});
    // Validate the identified visible frame before unpack, then vendor-defined crop.
    if (!acceptedImageSize(QSize(s.width,s.height))) return fail(QStringLiteral("RAW 有效尺寸无效或超过 45MP 上限"));
    raw.adjust_to_raw_inset_crop(1);
    if (!acceptedImageSize(QSize(s.width,s.height))) return fail(QStringLiteral("RAW 裁边尺寸无效或超过 45MP 上限"));
    result.report.insert("crop",QJsonObject{{"policy","LibRaw vendor inset crop mask=1"},
        {"width",s.width},{"height",s.height},{"leftMargin",s.left_margin},{"topMargin",s.top_margin}});
    stage.restart(); code = raw.unpack(); times.insert("unpackMs",double(stage.elapsed()));
    if (code != LIBRAW_SUCCESS) return fail(QStringLiteral("RAW 解码失败：") + LibRaw::strerror(code));
    stage.restart(); code = raw.dcraw_process(); times.insert("processMs",double(stage.elapsed()));
    if (code != LIBRAW_SUCCESS) return fail(QStringLiteral("RAW 显影失败：") + LibRaw::strerror(code));
    stage.restart();
    using ImagePtr = std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)>;
    const bool diagnostics = options.collectLinearDiagnostics || !options.linearOutput.isEmpty();
    const bool direct = options.directLinearPreview && !diagnostics;
    result.report.insert("directLinearPreview",direct);
    result.report.insert("linearDiagnosticsEnabled",diagnostics);
    QImage linear;
    if (direct) {
        int width=0,height=0,colors=0,bits=0;
        raw.get_mem_image_format(&width,&height,&colors,&bits);
        if (colors!=3 || bits!=16 || !acceptedImageSize(QSize(width,height))) return fail(QStringLiteral("RAW 输出不是有效 RGB16 图像"));
        result.originalSize=QSize(width,height);
        linear=QImage(width,height,QImage::Format_RGBX64);
        if (linear.isNull()) return fail(QStringLiteral("RAW 线性缓冲分配失败"));
        // LibRaw writes packed RGB16 into the start of each wider RGBX64 row.
        code=raw.copy_mem_image(linear.bits(),int(linear.bytesPerLine()),0);
        if (code!=LIBRAW_SUCCESS) return fail(QStringLiteral("RAW 直接输出失败：")+LibRaw::strerror(code));
        times.insert("makeImageMs",double(stage.elapsed()));
        times.insert("makeImageAndStatsMs",double(stage.elapsed()));
        times.insert("diagnosticsMs",0);
        stage.restart();
        for (int y=0;y<height;++y) {
            auto *row=reinterpret_cast<quint16 *>(linear.scanLine(y));
            // Expand backwards: all three source samples are saved before writes.
            // Reading forwards would overwrite later packed RGB samples.
            expandPackedRgb16(row,width);
        }
        raw.recycle();
    } else {
    ImagePtr image(raw.dcraw_make_mem_image(&code), &LibRaw::dcraw_clear_mem);
    if (!image || code != LIBRAW_SUCCESS) return fail(QStringLiteral("RAW 输出缓冲失败：") + LibRaw::strerror(code));
    if (image->type != LIBRAW_IMAGE_BITMAP || image->bits != 16 || image->colors != 3 ||
        !acceptedImageSize(QSize(image->width,image->height)) || quint64(image->data_size) != quint64(image->width)*image->height*6)
        return fail(QStringLiteral("RAW 输出不是有效 RGB16 图像"));
    result.originalSize = QSize(image->width,image->height);
    times.insert("makeImageMs",double(stage.elapsed()));
    const auto makeStartedMs = stage.elapsed();
    const auto *values = reinterpret_cast<const quint16 *>(image->data);
    if (diagnostics) {
    std::array<int,3> minValues{65535,65535,65535}, maxValues{}, zeroValues{}, clippedValues{};
    const quint64 pixels = quint64(image->width)*image->height;
    for (quint64 i=0;i<pixels;++i) for (int c=0;c<3;++c) {
        const int v=values[i*3+c]; minValues[c]=qMin(minValues[c],v); maxValues[c]=qMax(maxValues[c],v);
        if (v==0) ++zeroValues[c];
        if (v==65535) ++clippedValues[c];
    }
    QJsonArray minima, maxima, zeros, clipped;
    for (int c=0;c<3;++c) { minima.append(minValues[c]); maxima.append(maxValues[c]); zeros.append(zeroValues[c]); clipped.append(clippedValues[c]); }
    result.report.insert("linearRgb16",QJsonObject{{"width",image->width},{"height",image->height},{"min",minima},
        {"max",maxima},{"zeroCount",zeros},{"at65535Count",clipped},{"dataBytes",int(image->data_size)},
        {"sha256",QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image->data),image->data_size),QCryptographicHash::Sha256).toHex())}});
    }
    times.insert("diagnosticsMs",double(stage.elapsed()-makeStartedMs));
    times.insert("makeImageAndStatsMs",double(stage.elapsed()));
    if (!options.linearOutput.isEmpty()) {
        const QFileInfo target(options.linearOutput), source(path);
        if (target.absoluteFilePath() == source.absoluteFilePath() || target.exists()) return fail(QStringLiteral("实验输出已存在或指向原文件，拒绝覆盖"));
        QSaveFile output(options.linearOutput);
        if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
        const auto header = QByteArray("P6\n")+QByteArray::number(image->width)+" "+QByteArray::number(image->height)+"\n65535\n";
        if (output.write(header) != header.size()) return fail(output.errorString());
        QByteArray row(image->width*6,Qt::Uninitialized);
        for (int y=0;y<image->height;++y) {
            for (int x=0;x<image->width*3;++x) qToBigEndian<quint16>(values[quint64(y)*image->width*3+x],row.data()+x*2);
            if (output.write(row) != row.size()) return fail(output.errorString());
        }
        if (!output.commit()) return fail(output.errorString());
        result.report.insert("linearFile",QFileInfo(options.linearOutput).fileName());
        result.report.insert("linearFileSha256",QString::fromLatin1(fileHash(options.linearOutput)));
    }
    stage.restart();
    // Scale RGB16 linear samples before display encoding; never tag linear pixels as sRGB encoded.
    linear=QImage(image->width,image->height,QImage::Format_RGBX64);
    if (linear.isNull()) return fail(QStringLiteral("RAW 预览分配失败"));
    for (int y=0;y<image->height;++y) {
        auto *row=reinterpret_cast<QRgba64 *>(linear.scanLine(y));
        for (int x=0;x<image->width;++x) {
            const quint64 pos=(quint64(y)*image->width+x)*3;
            row[x]=QRgba64::fromRgba64(values[pos],values[pos+1],values[pos+2],65535);
        }
    }
    image.reset(); raw.recycle();
    }
    linear.setColorSpace(QColorSpace::SRgbLinear);
    auto proxy = linear.scaled(previewSize(result.originalSize,options.longEdge),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    linear = {};
    result.preview = proxy.convertedToColorSpace(QColorSpace::SRgb,QImage::Format_RGB32);
    if (result.preview.isNull()) return fail(QStringLiteral("RAW 显示变换失败"));
    times.insert("previewMs",double(stage.elapsed()));
    result.report.insert("preview",QJsonObject{{"width",result.preview.width()},{"height",result.preview.height()},
        {"encoding","Qt linear sRGB -> encoded sRGB; experimental SDR preview"}});
    stage.restart();
    result.report.insert("sourceUnchanged",fileHash(path)==beforeHash);
    times.insert("sourceVerifyMs",double(stage.elapsed()));
    result.report.insert("timings",times); result.report.insert("elapsedMs",double(total.elapsed()));
    result.report.insert("memory",processPeak());
    if (!result.report["sourceUnchanged"].toBool()) return fail(QStringLiteral("RAW 源文件在处理期间改变"));
    return result;
}
