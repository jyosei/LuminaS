#include "display_transform.h"
#include <QColorSpace>
#include <lcms2.h>
#include <algorithm>
namespace {
constexpr qsizetype maximumIcc=4*1024*1024;
struct Profile {
    cmsHPROFILE handle=nullptr;
    explicit Profile(cmsHPROFILE value):handle(value){}
    ~Profile(){if(handle)cmsCloseProfile(handle);}
};
bool rgb(Profile &profile){return profile.handle&&cmsGetColorSpace(profile.handle)==cmsSigRgbData;}
}
bool validDisplayProfile(const QByteArray &icc,QString &error){
    error.clear();if(icc.isEmpty())return true;
    if(icc.size()>maximumIcc){error="Display ICC exceeds limit";return false;}
    Profile profile(cmsOpenProfileFromMem(icc.constData(),cmsUInt32Number(icc.size())));
    if(!rgb(profile)){error="Unsupported or invalid RGB display ICC";return false;}
    const auto type=cmsGetDeviceClass(profile.handle);const auto version=cmsGetEncodedICCversion(profile.handle)>>24;
    if((type!=cmsSigDisplayClass&&type!=cmsSigColorSpaceClass)||(version!=2&&version!=4)||
       !cmsIsIntentSupported(profile.handle,INTENT_RELATIVE_COLORIMETRIC,LCMS_USED_AS_OUTPUT)){
        error="Unsupported display ICC class/version/intent";return false;
    }
    return true;
}
DisplayTransformResult transformDisplay(const QImage &input,const QByteArray &monitorIcc,
                                       const std::shared_ptr<std::atomic<bool>> &cancel){
    DisplayTransformResult result;
    if(input.isNull()){result.error="Empty display frame";return result;}
    if(cancel&&cancel->load()){result.error="Display conversion cancelled";return result;}
    const auto inputIcc=input.colorSpace().isValid()?input.colorSpace().iccProfile():QByteArray();
    if(inputIcc.size()>maximumIcc||monitorIcc.size()>maximumIcc){result.error="Display ICC exceeds limit";return result;}
    const QColorSpace target=monitorIcc.isEmpty()?QColorSpace(QColorSpace::SRgb):QColorSpace::fromIccProfile(monitorIcc);
    if(monitorIcc.isEmpty()&&(!input.colorSpace().isValid()||input.colorSpace()==QColorSpace(QColorSpace::SRgb))){result.image=input;return result;}
    Profile source(inputIcc.isEmpty()?cmsCreate_sRGBProfile():cmsOpenProfileFromMem(inputIcc.constData(),cmsUInt32Number(inputIcc.size())));
    Profile destination(monitorIcc.isEmpty()?cmsCreate_sRGBProfile():cmsOpenProfileFromMem(monitorIcc.constData(),cmsUInt32Number(monitorIcc.size())));
    if(!rgb(source)||!rgb(destination)){result.error="Unsupported or invalid RGB display ICC";return result;}
    const auto type=cmsGetDeviceClass(destination.handle);
    if(type!=cmsSigDisplayClass&&type!=cmsSigColorSpaceClass){result.error="ICC is not a display RGB profile";return result;}
    const auto version=cmsGetEncodedICCversion(destination.handle)>>24;
    if(version!=2&&version!=4){result.error="Unsupported display ICC version";return result;}
    if(target.isValid()&&target==input.colorSpace()){
        result.image=input;if(!monitorIcc.isEmpty())result.image.setColorSpace(QColorSpace());return result;
    }
    auto transform=cmsCreateTransform(source.handle,TYPE_RGBA_8,destination.handle,TYPE_RGBA_8,
                                     INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_COPY_ALPHA|cmsFLAGS_NOCACHE);
    if(!transform){result.error="Display ICC transform failed";return result;}
    const auto encoded=input.convertToFormat(QImage::Format_RGBA8888);
    QImage output(input.size(),QImage::Format_RGBA8888);
    if(output.isNull()){cmsDeleteTransform(transform);result.error="Display allocation failed";return result;}
    for(int y=0;y<output.height();++y){
        if((y%64)==0&&cancel&&cancel->load()){cmsDeleteTransform(transform);result.error="Display conversion cancelled";return result;}
        cmsDoTransform(transform,encoded.constScanLine(y),output.scanLine(y),cmsUInt32Number(output.width()));
    }
    cmsDeleteTransform(transform);
    // Pixels are now device codes; avoid treating them as another sRGB input.
    output.setColorSpace(monitorIcc.isEmpty()?QColorSpace(QColorSpace::SRgb):QColorSpace());
    result.image=std::move(output);return result;
}
