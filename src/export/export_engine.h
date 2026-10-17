#pragma once
#include "export_types.h"
namespace Export {
QByteArray builtinProfile(const QString &name,QString &error);
bool validProfile(const QByteArray &icc,Mode mode,QString &error);
QList<Profile> installedProfiles();
// All pixels originate in straight float32 linear Rec.2020, never display RGB.
struct Pixels { QSize size; int channels=3,bits=8; QByteArray data,icc; };
Pixels convert(const QImage &linear,const Settings &settings,QString &error,
               const std::atomic_bool *cancel=nullptr);
QImage preview(const QImage &linear,const Settings &settings,QString &error,const std::atomic_bool *cancel=nullptr);
Result execute(const Job &job,const std::atomic_bool *cancel=nullptr,const Progress &progress={});
QImage readOutput(const QString &path,QString &error);
}
