#include "recovery_store.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace LocalEdit {
namespace {

constexpr int maxPathChars = 32767;
constexpr int maxPendingErrorChars = 4096;

QString canonicalPath(const QString &path) {
    QString p = QDir::fromNativeSeparators(path);
    p = QDir::cleanPath(p);
#ifdef Q_OS_WIN
    p = p.toCaseFolded();
#endif
    return p;
}

bool validatePath(const QString &path, int maxChars) {
    if (path.isEmpty()) return false;
    if (path.size() > maxChars) return false;
    if (path.contains(QChar(u'\0'))) return false;
    if (!QDir::isAbsolutePath(path)) return false;
    return true;
}

bool parseUint64Canonical(const QJsonValue &value, quint64 &out) {
    if (!value.isString()) return false;
    const QString s = value.toString();
    if (s.isEmpty() || s.size() > 20) return false;
    if (s.size() > 1 && s.at(0) == QChar(u'0')) return false;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c < QChar(u'0') || c > QChar(u'9')) return false;
    }
    bool ok = false;
    const quint64 parsed = s.toULongLong(&ok);
    if (!ok) return false;
    out = parsed;
    return true;
}

bool isLowercaseHex64(const QString &s) {
    if (s.size() != 64) return false;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (!((c >= QChar(u'0') && c <= QChar(u'9')) ||
              (c >= QChar(u'a') && c <= QChar(u'f')))) {
            return false;
        }
    }
    return true;
}

bool hasOnlyKeys(const QJsonObject &object, const QVector<QString> &keys) {
    if (object.size() != keys.size()) return false;
    for (const QString &key : keys) {
        if (!object.contains(key)) return false;
    }
    return true;
}

bool decodePhotoMark(const QJsonValue &value, PhotoMark &mark) {
    if (!value.isDouble()) return false;
    const double d = value.toDouble();
    if (!(d >= 0 && d <= 2) || d != std::floor(d)) return false;
    const qint64 i = static_cast<qint64>(d);
    if (i < 0 || i > 2) return false;
    mark = static_cast<PhotoMark>(i);
    return true;
}

bool encodePhotoMark(PhotoMark mark, QJsonValue &value) {
    switch (mark) {
    case PhotoMark::Unmarked: value = 0; return true;
    case PhotoMark::Keep: value = 1; return true;
    case PhotoMark::Reject: value = 2; return true;
    }
    return false;
}

bool checkDocumentBytes(const QJsonObject &documentJson, qint64 &totalOut) {
    QJsonDocument doc(documentJson);
    const QByteArray compact = doc.toJson(QJsonDocument::Compact);
    const qint64 bytes = static_cast<qint64>(compact.size());
    if (bytes > maximumDocumentBytes) return false;
    totalOut += bytes;
    if (totalOut > maximumProjectBytes) return false;
    return true;
}

} // namespace

static bool validateRecoveryState(const RecoveryState &state, QString &error) {
    error.clear();

    if (!state.project) {
        error = QStringLiteral("Recovery has no project.");
        return false;
    }
    if (!validProject(*state.project, error)) {
        return false;
    }

    const ProjectState &project = *state.project;

    if (state.order.size() > maximumProjectPhotos || state.pending.size() > maximumProjectPhotos ||
        project.photos.size()+state.pending.size() > maximumProjectPhotos) {
        error = QStringLiteral("Recovery order exceeds maximum photo count.");
        return false;
    }

    QVector<QString> canonicalOrder;
    canonicalOrder.reserve(state.order.size());
    for (const QString &path : state.order) {
        if (!validatePath(path, maxPathChars)) {
            error = QStringLiteral("Recovery order contains an invalid path.");
            return false;
        }
        canonicalOrder.append(canonicalPath(path));
    }

    for (const PendingImport &pending : state.pending) {
        if (pending.mark!=PhotoMark::Unmarked&&pending.mark!=PhotoMark::Keep&&pending.mark!=PhotoMark::Reject) {
            error=QStringLiteral("Pending import mark is invalid.");return false;
        }
        if (!validatePath(pending.path, maxPathChars)) {
            error = QStringLiteral("Pending import contains an invalid path.");
            return false;
        }
        if (pending.error.size() > maxPendingErrorChars) {
            error = QStringLiteral("Pending import error text too long.");
            return false;
        }
        if (pending.error.contains(QChar(u'\0'))) {
            error = QStringLiteral("Pending import error text contains NUL.");
            return false;
        }
    }

    QVector<QString> knownCanonical;
    knownCanonical.reserve(project.photos.size());
    QVector<QString> knownIds;
    knownIds.reserve(project.photos.size());
    for (int i = 0; i < project.photos.size(); ++i) {
        const ProjectPhoto &photo = project.photos.at(i);
        if (!photo.document) {
            error = QStringLiteral("Recovery project photo has no document.");
            return false;
        }
        const QString &sourcePath = photo.document->asset.sourcePath;
        if (!validatePath(sourcePath, maxPathChars)) {
            error = QStringLiteral("Project photo has an invalid source path.");
            return false;
        }
        knownCanonical.append(canonicalPath(sourcePath));
        knownIds.append(photo.document->asset.id);
    }

    QVector<QString> pendingCanonical;
    pendingCanonical.reserve(state.pending.size());
    for (const PendingImport &pending : state.pending) {
        pendingCanonical.append(canonicalPath(pending.path));
    }

    // Known and pending source paths must be disjoint.
    for (const QString &kp : knownCanonical) {
        for (const QString &pp : pendingCanonical) {
            if (kp == pp) {
                error = QStringLiteral("Recovery source path is both known and pending.");
                return false;
            }
        }
    }

    // Order must be unique and exactly the union of known and pending paths.
    {
        QVector<QString> sortedOrder = canonicalOrder;
        std::sort(sortedOrder.begin(), sortedOrder.end());
        for (int i = 1; i < sortedOrder.size(); ++i) {
            if (sortedOrder.at(i) == sortedOrder.at(i - 1)) {
                error = QStringLiteral("Recovery order contains duplicate paths.");
                return false;
            }
        }
        QVector<QString> sortedUnion = knownCanonical;
        sortedUnion += pendingCanonical;
        std::sort(sortedUnion.begin(), sortedUnion.end());
        if (sortedOrder != sortedUnion) {
            error = QStringLiteral("Recovery order does not match known and pending sources.");
            return false;
        }
    }

    // Known project photos must appear in order as a subsequence.
    {
        int cursor = 0;
        for (const QString &kc : knownCanonical) {
            bool found = false;
            while (cursor < canonicalOrder.size()) {
                if (canonicalOrder.at(cursor) == kc) {
                    found = true;
                    ++cursor;
                    break;
                }
                ++cursor;
            }
            if (!found) {
                error = QStringLiteral("Recovery order is not a supersequence of project photos.");
                return false;
            }
        }
    }

    if (state.activePath.isEmpty()) {
        if (!project.activeAssetId.isEmpty()) {
            error = QStringLiteral("Recovery has an active asset but no active path.");
            return false;
        }
    } else {
        if (!validatePath(state.activePath, maxPathChars)) {
            error = QStringLiteral("Recovery active path is invalid.");
            return false;
        }
        const QString activeCanonical = canonicalPath(state.activePath);
        bool matchedKnown = false;
        bool matchedPending = false;
        for (int i = 0; i < knownCanonical.size(); ++i) {
            if (knownCanonical.at(i) == activeCanonical) {
                matchedKnown = true;
                if (knownIds.at(i) != project.activeAssetId) {
                    error = QStringLiteral("Recovery active path does not match active asset.");
                    return false;
                }
            }
        }
        for (const QString &pc : pendingCanonical) {
            if (pc == activeCanonical) matchedPending = true;
        }
        if (!matchedKnown && !matchedPending) {
            error = QStringLiteral("Recovery active path is not a known or pending source.");
            return false;
        }
        if (matchedPending && !project.activeAssetId.isEmpty()) {
            error = QStringLiteral("Recovery pending active path conflicts with active asset.");
            return false;
        }
    }

    // Explicit path/hash pair.
    if (state.explicitPath.isEmpty() != state.explicitHash.isEmpty()) {
        error = QStringLiteral("Recovery explicit path and hash must be both set or both empty.");
        return false;
    }
    if (!state.explicitPath.isEmpty()) {
        if (!validatePath(state.explicitPath, maxPathChars)) {
            error = QStringLiteral("Recovery explicit path is invalid.");
            return false;
        }
        if (!isLowercaseHex64(state.explicitHash)) {
            error = QStringLiteral("Recovery explicit hash must be 64 lowercase hex characters.");
            return false;
        }
        const QString explicitCanonical = canonicalPath(state.explicitPath);
        for (const QString &kp : knownCanonical) {
            if (kp == explicitCanonical) {
                error = QStringLiteral("Recovery explicit path aliases a known source.");
                return false;
            }
        }
        for (const QString &pp : pendingCanonical) {
            if (pp == explicitCanonical) {
                error = QStringLiteral("Recovery explicit path aliases a pending source.");
                return false;
            }
        }
    }

    return true;
}

bool validRecovery(const RecoveryState &state, QString &error) {
    recoveryJson(state,error);
    return error.isEmpty();
}

RecoverySnapshot freezeRecovery(const RecoveryState &state, QString &error) {
    error.clear();

    // Take our own top-state copy so caller mutations do not alias our snapshot.
    auto frozen = std::make_shared<RecoveryState>();
    frozen->order = state.order;
    frozen->pending = state.pending;
    frozen->activePath = state.activePath;
    frozen->explicitPath = state.explicitPath;
    frozen->explicitHash = state.explicitHash;

    if (state.project) {
        QString projectError;
        ProjectSnapshot projectSnapshot = freezeProject(*state.project, projectError);
        if (!projectSnapshot) {
            error = projectError.isEmpty()
                ? QStringLiteral("Recovery project failed to freeze.")
                : projectError;
            return {};
        }
        frozen->project = projectSnapshot;
    } else {
        frozen->project = ProjectSnapshot();
    }

    QString validError;
    if (!validRecovery(*frozen, validError)) {
        error = validError;
        return {};
    }

    return frozen;
}

QJsonObject recoveryJson(const RecoveryState &state, QString &error) {
    error.clear();

    QString validError;
    if (!validateRecoveryState(state, validError)) {
        error = validError;
        return {};
    }

    QJsonObject top;
    top.insert(QStringLiteral("format"), QStringLiteral("LuminaS.recovery"));
    top.insert(QStringLiteral("version"), static_cast<int>(recoveryFormatVersion));

    QJsonObject projectObj;
    if (state.project) {
        const ProjectState &project = *state.project;
        projectObj.insert(QStringLiteral("id"), project.id);
        projectObj.insert(QStringLiteral("name"), project.name);
        projectObj.insert(QStringLiteral("revision"),
            QString::number(project.revision));
        projectObj.insert(QStringLiteral("activeAssetId"), project.activeAssetId);

        QJsonArray photosArray;
        for (const ProjectPhoto &photo : project.photos) {
            QJsonObject photoObj;
            QJsonValue markValue;
            if (!encodePhotoMark(photo.mark, markValue)) {
                error = QStringLiteral("Unknown photo mark.");
                return {};
            }
            photoObj.insert(QStringLiteral("mark"), markValue);

            QString documentError;
            const QJsonObject documentOut =
                documentJson(*photo.document, documentError);
            if (!documentError.isEmpty()) {
                error = documentError;
                return {};
            }
            photoObj.insert(QStringLiteral("document"), documentOut);
            photosArray.append(photoObj);
        }
        projectObj.insert(QStringLiteral("photos"), photosArray);
    }
    top.insert(QStringLiteral("project"), projectObj);

    QJsonArray orderArray;
    for (const QString &path : state.order) {
        orderArray.append(path);
    }
    top.insert(QStringLiteral("order"), orderArray);

    QJsonArray pendingArray;
    for (const PendingImport &pending : state.pending) {
        QJsonObject pendingObj;
        pendingObj.insert(QStringLiteral("path"), pending.path);
        pendingObj.insert(QStringLiteral("error"), pending.error);
        pendingObj.insert(QStringLiteral("mark"), int(pending.mark));
        pendingArray.append(pendingObj);
    }
    top.insert(QStringLiteral("pending"), pendingArray);

    top.insert(QStringLiteral("activePath"), state.activePath);
    top.insert(QStringLiteral("explicitPath"), state.explicitPath);
    top.insert(QStringLiteral("explicitHash"), state.explicitHash);

    if(QJsonDocument(top).toJson(QJsonDocument::Compact).size()>maximumProjectBytes){
        error=QStringLiteral("Recovery envelope exceeds size limit.");return {};
    }
    return top;
}

bool recoveryFromJson(const QJsonObject &json, RecoveryState &state, QString &error) {
    error.clear();

    const QVector<QString> topKeys = {
        QStringLiteral("format"),
        QStringLiteral("version"),
        QStringLiteral("project"),
        QStringLiteral("order"),
        QStringLiteral("pending"),
        QStringLiteral("activePath"),
        QStringLiteral("explicitPath"),
        QStringLiteral("explicitHash"),
    };
    if (!hasOnlyKeys(json, topKeys)) {
        error = QStringLiteral("Recovery JSON has unexpected or missing top-level keys.");
        return false;
    }

    {
        const QJsonValue formatValue = json.value(QStringLiteral("format"));
        if (!formatValue.isString() ||
            formatValue.toString() != QStringLiteral("LuminaS.recovery")) {
            error = QStringLiteral("Recovery JSON format marker invalid.");
            return false;
        }
    }

    {
        const QJsonValue versionValue = json.value(QStringLiteral("version"));
        if (!versionValue.isDouble()) {
            error = QStringLiteral("Recovery JSON version is not a number.");
            return false;
        }
        const double d = versionValue.toDouble();
        if (d != recoveryFormatVersion) {
            error = QStringLiteral("Recovery JSON version unsupported.");
            return false;
        }
    }

    QJsonObject projectObj;
    {
        const QJsonValue projectValue = json.value(QStringLiteral("project"));
        if (!projectValue.isObject()) {
            error = QStringLiteral("Recovery JSON project is not an object.");
            return false;
        }
        projectObj = projectValue.toObject();
        const QVector<QString> projectKeys = {
            QStringLiteral("id"),
            QStringLiteral("name"),
            QStringLiteral("revision"),
            QStringLiteral("activeAssetId"),
            QStringLiteral("photos"),
        };
        if (!hasOnlyKeys(projectObj, projectKeys)) {
            error = QStringLiteral("Recovery JSON project has unexpected or missing keys.");
            return false;
        }
    }

    ProjectState projectTemp;
    {
        const QJsonValue idValue = projectObj.value(QStringLiteral("id"));
        const QJsonValue nameValue = projectObj.value(QStringLiteral("name"));
        const QJsonValue activeValue = projectObj.value(QStringLiteral("activeAssetId"));
        if (!idValue.isString() || !nameValue.isString() || !activeValue.isString()) {
            error = QStringLiteral("Recovery JSON project string fields invalid.");
            return false;
        }
        projectTemp.id = idValue.toString();
        projectTemp.name = nameValue.toString();
        projectTemp.activeAssetId = activeValue.toString();

        quint64 revision = 0;
        if (!parseUint64Canonical(projectObj.value(QStringLiteral("revision")), revision)) {
            error = QStringLiteral("Recovery JSON project revision invalid.");
            return false;
        }
        if (revision == 0) {
            error = QStringLiteral("Recovery JSON project revision must be nonzero.");
            return false;
        }
        projectTemp.revision = revision;
    }

    qint64 totalBytes = 0;
    {
        const QJsonValue photosValue = projectObj.value(QStringLiteral("photos"));
        if (!photosValue.isArray()) {
            error = QStringLiteral("Recovery JSON photos is not an array.");
            return false;
        }
        const QJsonArray photosArray = photosValue.toArray();
        if (photosArray.size() > maximumProjectPhotos) {
            error = QStringLiteral("Recovery JSON photos exceed maximum count.");
            return false;
        }

        QVector<ProjectPhoto> photos;
        photos.reserve(photosArray.size());
        for (int i = 0; i < photosArray.size(); ++i) {
            const QJsonValue photoValue = photosArray.at(i);
            if (!photoValue.isObject()) {
                error = QStringLiteral("Recovery JSON photo entry is not an object.");
                return false;
            }
            const QJsonObject photoObj = photoValue.toObject();
            const QVector<QString> photoKeys = {
                QStringLiteral("mark"),
                QStringLiteral("document"),
            };
            if (!hasOnlyKeys(photoObj, photoKeys)) {
                error = QStringLiteral("Recovery JSON photo entry has unexpected keys.");
                return false;
            }

            ProjectPhoto photo;
            if (!decodePhotoMark(photoObj.value(QStringLiteral("mark")), photo.mark)) {
                error = QStringLiteral("Recovery JSON photo mark invalid.");
                return false;
            }

            const QJsonValue documentValue = photoObj.value(QStringLiteral("document"));
            if (!documentValue.isObject()) {
                error = QStringLiteral("Recovery JSON photo document is not an object.");
                return false;
            }
            const QJsonObject documentObj = documentValue.toObject();

            if (!checkDocumentBytes(documentObj, totalBytes)) {
                error = QStringLiteral("Recovery JSON document size limit exceeded.");
                return false;
            }

            PhotoDocument document;
            QString documentError;
            if (!documentFromJson(documentObj, document, documentError, true)) {
                error = documentError.isEmpty()
                    ? QStringLiteral("Recovery JSON photo document invalid.")
                    : documentError;
                return false;
            }
            photo.document = std::make_shared<const PhotoDocument>(std::move(document));
            photos.append(photo);
        }
        projectTemp.photos = photos;
    }

    RecoveryState temp;
    temp.project = std::make_shared<const ProjectState>(std::move(projectTemp));

    {
        const QJsonValue orderValue = json.value(QStringLiteral("order"));
        if (!orderValue.isArray()) {
            error = QStringLiteral("Recovery JSON order is not an array.");
            return false;
        }
        const QJsonArray orderArray = orderValue.toArray();
        if (orderArray.size() > maximumProjectPhotos) {
            error = QStringLiteral("Recovery JSON order exceeds maximum count.");
            return false;
        }
        QStringList order;
        order.reserve(orderArray.size());
        for (int i = 0; i < orderArray.size(); ++i) {
            const QJsonValue pathValue = orderArray.at(i);
            if (!pathValue.isString()) {
                error = QStringLiteral("Recovery JSON order path is not a string.");
                return false;
            }
            order.append(pathValue.toString());
        }
        temp.order = order;
    }

    {
        const QJsonValue pendingValue = json.value(QStringLiteral("pending"));
        if (!pendingValue.isArray()) {
            error = QStringLiteral("Recovery JSON pending is not an array.");
            return false;
        }
        const QJsonArray pendingArray = pendingValue.toArray();
        if (pendingArray.size() > maximumProjectPhotos) {
            error = QStringLiteral("Recovery JSON pending exceeds maximum count.");
            return false;
        }
        QVector<PendingImport> pending;
        pending.reserve(pendingArray.size());
        for (int i = 0; i < pendingArray.size(); ++i) {
            const QJsonValue pendingValueEntry = pendingArray.at(i);
            if (!pendingValueEntry.isObject()) {
                error = QStringLiteral("Recovery JSON pending entry is not an object.");
                return false;
            }
            const QJsonObject pendingObj = pendingValueEntry.toObject();
            QVector<QString> pendingKeys = {
                QStringLiteral("path"),
                QStringLiteral("error"),
            };
            if(pendingObj.contains(QStringLiteral("mark")))pendingKeys.append(QStringLiteral("mark"));
            if (!hasOnlyKeys(pendingObj, pendingKeys)) {
                error = QStringLiteral("Recovery JSON pending entry has unexpected keys.");
                return false;
            }
            const QJsonValue pathValue = pendingObj.value(QStringLiteral("path"));
            const QJsonValue errorValue = pendingObj.value(QStringLiteral("error"));
            if (!pathValue.isString() || !errorValue.isString()) {
                error = QStringLiteral("Recovery JSON pending entry fields invalid.");
                return false;
            }
            PendingImport pendingImport;
            const auto markValue=pendingObj.value(QStringLiteral("mark"));
            if(!markValue.isUndefined()){
                if(!decodePhotoMark(markValue,pendingImport.mark)){
                    error=QStringLiteral("Recovery pending mark is invalid.");return false;
                }
            }
            pendingImport.path = pathValue.toString();
            pendingImport.error = errorValue.toString();
            pending.append(pendingImport);
        }
        temp.pending = pending;
    }

    {
        const QJsonValue activeValue = json.value(QStringLiteral("activePath"));
        const QJsonValue explicitPathValue = json.value(QStringLiteral("explicitPath"));
        const QJsonValue explicitHashValue = json.value(QStringLiteral("explicitHash"));
        if (!activeValue.isString() || !explicitPathValue.isString() ||
            !explicitHashValue.isString()) {
            error = QStringLiteral("Recovery JSON path fields invalid.");
            return false;
        }
        temp.activePath = activeValue.toString();
        temp.explicitPath = explicitPathValue.toString();
        temp.explicitHash = explicitHashValue.toString();
    }

    // Freeze the mutable project into an immutable snapshot, and validate fully.
    QString freezeError;
    RecoverySnapshot frozen = freezeRecovery(temp, freezeError);
    if (!frozen) {
        error = freezeError.isEmpty()
            ? QStringLiteral("Recovery JSON failed freezing.")
            : freezeError;
        return false;
    }

    // Only on success assign the caller's state.
    state = *frozen;
    return true;
}

} // namespace LocalEdit
