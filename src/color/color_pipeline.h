#pragma once
#include <lcms2.h>
#include <QByteArray>
#include <QImage>
#include <array>
class ToneOperator;
class PostCropVignette;
class ProceduralGrain;
cmsHPROFILE workProfile();
cmsHPROFILE linearSrgbProfile();
QImage inputToWork(const QImage &encodedRgba,const QByteArray &icc,QString &error);
QImage workToSrgb(const QImage &linear,QString &error);
QImage workToSrgbReference(const QImage &linear,QString &error);
QImage workToSrgbAffine(const QImage &linear,const std::array<float,9> &matrix,const std::array<float,3> &offset,QString &error,const ToneOperator *tone=nullptr,const PostCropVignette *vignette=nullptr,bool flipHorizontal=false,bool flipVertical=false,const ProceduralGrain *grain=nullptr);
