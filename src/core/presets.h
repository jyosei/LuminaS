#pragma once
#include "document.h"
struct AdjustmentPreset { QString name;Recipe adjustments; };
QVector<AdjustmentPreset> builtinPresets();
Recipe applyPreset(const Recipe &current,const AdjustmentPreset &preset);
bool blendPreset(const Recipe &base,const AdjustmentPreset &preset,double strength,Recipe &result,QString &error);
bool savePresetFile(const QString &path,const AdjustmentPreset &preset,QString &error);
bool loadPresetFile(const QString &path,AdjustmentPreset &preset,QString &error);
// Managed custom preset library (nonrecursive scan, at most 64 *.luminas-preset.json).
QVector<AdjustmentPreset> loadPresetLibrary(const QString &directory,QStringList &warnings);
bool storePresetLibrary(const QString &directory,const AdjustmentPreset &preset,QString &error);
bool renamePresetLibrary(const QString &directory,const QString &oldName,const QString &newName,QString &error);
bool removePresetLibrary(const QString &directory,const QString &name,QString &error);
