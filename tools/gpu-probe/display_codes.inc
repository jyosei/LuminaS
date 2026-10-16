// Experimental exact byte-domain display map. Canonical input is already SDR
// sRGB8, matching transformDisplay; this is not a full-gamut export transform.
QImage displayCodebook(const QByteArray &icc){
    QString error;require(validDisplayProfile(icc,error)&&!icc.isEmpty(),"Invalid display ICC");
    const auto inputIcc=QColorSpace(QColorSpace::SRgb).iccProfile();
    auto source=cmsOpenProfileFromMem(inputIcc.constData(),cmsUInt32Number(inputIcc.size()));
    auto target=cmsOpenProfileFromMem(icc.constData(),cmsUInt32Number(icc.size()));
    const auto profiles=qScopeGuard([&]{if(source)cmsCloseProfile(source);if(target)cmsCloseProfile(target);});
    require(source&&target,"Display map profiles failed");
    auto transform=cmsCreateTransform(source,TYPE_RGBA_8,target,TYPE_RGBA_8,INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_COPY_ALPHA|cmsFLAGS_NOCACHE);
    require(transform,"Display map transform failed");const auto close=qScopeGuard([&]{cmsDeleteTransform(transform);});
    QImage codes(4096,4096,QImage::Format_RGBA8888);require(!codes.isNull(),"Display map allocation failed");
    std::array<quint8,4096*4> row;
    for(int y=0;y<4096;++y){
        for(int x=0;x<4096;++x){row[x*4]=quint8(x%256);row[x*4+1]=quint8(x/256+(y/256)*16);row[x*4+2]=quint8(y%256);row[x*4+3]=255;}
        cmsDoTransform(transform,row.data(),codes.scanLine(y),4096);
    }
    return codes;
}
