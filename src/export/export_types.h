#pragma once
#include "core/document.h"
#include "core/photo_document.h"
#include <QJsonObject>
#include <QList>
#include <QString>
#include <atomic>
#include <functional>

namespace Export {
enum class Format { Jpeg, Png, Tiff };
enum class Mode { Rgb, Cmyk };
enum class SizeMode { Full, LongEdge, ShortEdge, Fit, Percent, Print };
enum class Metadata { None, Copyright, Camera };
enum class Collision { Number, Skip };
enum class Status { Pending, Running, Succeeded, Failed, Cancelled, Skipped };
struct Settings {
    Format format=Format::Jpeg; Mode mode=Mode::Rgb; int bits=8,quality=95;
    bool zip=true,blackPoint=true; int intent=1;
    QString profileName=QStringLiteral("sRGB"); QByteArray icc;
    SizeMode sizeMode=SizeMode::Full; double width=3840,height=2160,percent=100,ppi=300;
    bool dontEnlarge=true,printCm=false;
    bool besideSource=true; QString directory,subfolder=QStringLiteral("LuminaS-Export");
    bool sequence=false; QString prefix=QStringLiteral("Photo"),suffix; int start=1;
    Collision collision=Collision::Number; Metadata metadata=Metadata::Camera; bool gps=false;
    int after=0; // results, open folder, inspect
};
struct Asset { QString path,hash; Recipe recipe; quint64 revision=0; bool selected=false,kept=false;
    LocalEdit::DocumentSnapshot document; };
struct Job { QString id,source,hash,path; quint64 revision=0; Recipe recipe; Settings settings;
    Status status=Status::Pending; QString error,stage; LocalEdit::DocumentSnapshot document; };
struct Result { bool ok=false,cancelled=false,skipped=false; QString error,path; QSize size; };
struct Profile { QString name,path; QByteArray icc; Mode mode=Mode::Rgb; };
QJsonObject settingsJson(const Settings &s);
bool settingsFromJson(const QJsonObject &json,Settings &s,QString &error);
QJsonObject jobJson(const Job &job);
bool jobFromJson(const QJsonObject &json,Job &job,QString &error);
bool validateJobDocument(const Job &job,QString &error);
QString extension(Format format);
QString statusText(Status status);
QString validate(const Settings &settings);
QSize outputSize(QSize full,const Settings &settings,QString &error);
QList<Job> freeze(const QList<Asset> &assets,const Settings &settings,QString &error);
using Progress=std::function<void(const QString &)>;
}
