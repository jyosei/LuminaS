#pragma once
#include "core/document.h"
#include <QByteArrayView>
#include <QJsonObject>

namespace RawWorker {
constexpr int protocolVersion=1,algorithmVersion=1;
constexpr qsizetype maximumLineBytes=1024*1024;
enum class Operation {DecodeFull,Prepare};
struct Request {
    QString id,sourcePath,sourceHash,token;
    Operation operation=Operation::DecodeFull;
    double warmth=0,tint=0;
    Recipe geometry; // Only the existing geometry prefix, never global grading.
    int longEdge=0; // DecodeFull:0; Prepare:3840 (small sources remain native).
};
bool validUuid(const QString &text);
bool validHash(const QString &text);
bool validRequest(const Request &request,QString &error);
QJsonObject requestJson(const Request &request,QString &error);
bool requestFromJson(const QJsonObject &json,Request &request,QString &error);
QString requestHash(const Request &request,QString &error);
QJsonObject hello();
bool validHello(const QJsonObject &json,QString &error);
QByteArray encodeLine(const QJsonObject &json,QString &error); // Includes LF.
bool decodeLine(QByteArrayView line,QJsonObject &json,QString &error); // Excludes LF; canonical compact JSON.
QJsonObject cancelJson(const Request &request);
bool validCancel(const QJsonObject &json,const Request &request,QString &error);
}
