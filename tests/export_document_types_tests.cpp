#include "export/export_engine.h"
#include "export/export_types.h"
#include "export/export_queue.h"
#include "core/photo_document.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace Export;

namespace {
QByteArray digest(const QString &path){
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return {};return hash.result().toHex();
}

QString sample(const QTemporaryDir &dir){
    QImage image(64,48,QImage::Format_RGB32);
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)image.setPixelColor(x,y,QColor(x*4,y*5,90));
    image.setColorSpace(QColorSpace::SRgb);
    const auto path=dir.filePath(QStringLiteral("source.jpg"));
    return image.save(path,"JPEG",95)?path:QString();
}

Settings srgbSettings(){
    Settings settings;QString error;
    settings.icc=builtinProfile(QStringLiteral("sRGB"),error);
    return settings;
}

LocalEdit::BrushGeometry brush(){
    LocalEdit::BrushGeometry geometry;
    geometry.points=QVector<QPointF>{QPointF(10,10),QPointF(20,25)};
    geometry.radius=40;geometry.hardness=.5;
    return geometry;
}

LocalEdit::PhotoDocument makeDocument(const QString &sourcePath,const QString &hash,const Recipe &recipe,quint64 revision){
    LocalEdit::PhotoDocument document;
    document.asset.id=QStringLiteral("asset-1");
    document.asset.sourcePath=sourcePath;
    document.asset.sourceHash=hash;
    document.asset.normalizedSize=QSize(64,48);
    document.asset.raw=sourcePath.endsWith(QStringLiteral(".cr3"),Qt::CaseInsensitive);
    document.global=recipe;
    document.revision=revision;
    LocalEdit::AdjustmentLayer layer;
    layer.id=QStringLiteral("layer-1");layer.name=QStringLiteral("brush");
    layer.opacity=.8;
    LocalEdit::MaskComponent component;
    component.id=QStringLiteral("component-1");
    component.geometry=brush();
    layer.mask.components.append(component);
    document.layers.append(layer);
    return document;
}

LocalEdit::DocumentSnapshot snapshot(const LocalEdit::PhotoDocument &document){
    return std::make_shared<const LocalEdit::PhotoDocument>(document);
}

Recipe sampleRecipe(){Recipe recipe;recipe.exposure=.25;recipe.contrast=.12;return recipe;}

Job makeJob(const QString &source,const QString &hash,const Recipe &recipe,quint64 revision){
    Job job;
    job.id=QStringLiteral("0f0a5b3c-3c62-4a57-9b5d-1a2b3c4d5e6f");
    job.source=source;job.hash=hash;job.path=source+QStringLiteral(".out.jpg");
    job.revision=revision;job.recipe=recipe;job.settings=srgbSettings();
    job.document=snapshot(makeDocument(source,hash,recipe,revision));
    return job;
}

bool decode(const Job &job,Job &out,QString &error){
    error.clear();
    return jobFromJson(jobJson(job),out,error);
}
} // namespace

namespace QTest{template<> char *toString(const LocalEdit::DocumentSnapshot &){return qstrdup("DocumentSnapshot");}}

class ExportDocumentTypesTests:public QObject {
    Q_OBJECT
private slots:
    void jobDocumentRoundTrip(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto source=sample(dir);QVERIFY(!source.isEmpty());
        const auto hash=QString::fromLatin1(digest(source));QVERIFY(hash.size()==64);
        const auto recipe=sampleRecipe();
        Job job=makeJob(source,hash,recipe,7);
        QVERIFY(job.document);
        const auto copy=*job.document;
        QCOMPARE(copy.asset.sourcePath,source);
        QCOMPARE(copy.global.exposure,recipe.exposure);
        QCOMPARE(copy.layers.size(),1);
        QCOMPARE(copy.layers.first().mask.components.size(),1);
        QVERIFY(std::holds_alternative<LocalEdit::BrushGeometry>(copy.layers.first().mask.components.first().geometry));

        const auto json=jobJson(job);
        QVERIFY(!json.isEmpty());
        QCOMPARE(json.value(QStringLiteral("version")).toInt(),2);
        QVERIFY(json.value(QStringLiteral("document")).isObject());
        Job restored;QString error;
        QVERIFY2(jobFromJson(json,restored,error),qPrintable(error));
        QVERIFY(restored.document);
        QVERIFY(*restored.document==copy);
        QCOMPARE(restored.source,source);
        QCOMPARE(restored.revision,quint64(7));
        QCOMPARE(json.value(QStringLiteral("revision")).toString(),QStringLiteral("7"));
        QVERIFY(restored.recipe==recipe);
        QCOMPARE(QJsonDocument(jobJson(restored)).toJson(),QJsonDocument(json).toJson());

        Settings legacy;QVERIFY(settingsFromJson(settingsJson(job.settings),legacy,error));
        const auto settingsObject=settingsJson(job.settings);
        QCOMPARE(settingsObject.value(QStringLiteral("version")).toInt(),1);

        Job plain=job;plain.document.reset();
        const auto nullJson=jobJson(plain);
        QCOMPARE(nullJson.value(QStringLiteral("version")).toInt(),2);
        QVERIFY(nullJson.contains(QStringLiteral("document")));
        QVERIFY(nullJson.value(QStringLiteral("document")).isNull());
        Job nullRestored;QVERIFY2(jobFromJson(nullJson,nullRestored,error),qPrintable(error));
        QVERIFY(!nullRestored.document);

        auto v1=json;v1.insert(QStringLiteral("version"),1);v1.remove(QStringLiteral("document"));
        Job v1Restored;QVERIFY2(jobFromJson(v1,v1Restored,error),qPrintable(error));
        QVERIFY(!v1Restored.document);
        QCOMPARE(v1Restored.revision,job.revision);
        QVERIFY(v1Restored.recipe==job.recipe);
        v1.insert(QStringLiteral("document"),QJsonValue::Null);
        QVERIFY2(jobFromJson(v1,v1Restored,error),qPrintable(error));

        auto v1Document=json;v1Document.insert(QStringLiteral("version"),1);
        QVERIFY(!jobFromJson(v1Document,v1Restored,error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!v1Restored.document);
        QCOMPARE(v1Restored.id,job.id);

        auto missing=nullJson;missing.remove(QStringLiteral("document"));
        Job untouched=plain;untouched.id=QStringLiteral("sentinel");
        QVERIFY(!jobFromJson(missing,untouched,error));
        QCOMPARE(untouched.id,QStringLiteral("sentinel"));
        auto boolean=nullJson;boolean.insert(QStringLiteral("document"),true);
        QVERIFY(!jobFromJson(boolean,untouched,error));
        QCOMPARE(untouched.id,QStringLiteral("sentinel"));
        Job absent=job;absent.document.reset();
        auto absentJson=jobJson(absent);absentJson.remove(QStringLiteral("document"));
        QVERIFY(!jobFromJson(absentJson,untouched,error));

        auto badVersion=json;
        auto badDocument=badVersion.value(QStringLiteral("document")).toObject();
        badDocument.insert(QStringLiteral("algorithmVersion"),2);
        badVersion.insert(QStringLiteral("document"),badDocument);
        QVERIFY(!jobFromJson(badVersion,untouched,error));
        QCOMPARE(untouched.id,QStringLiteral("sentinel"));
    }
    void jobValidationRejectsMismatch(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto source=sample(dir);QVERIFY(!source.isEmpty());
        const auto hash=QString::fromLatin1(digest(source));
        const auto recipe=sampleRecipe();
        Job job=makeJob(source,hash,recipe,3);
        QString error;
        QVERIFY2(validateJobDocument(job,error),qPrintable(error));

        const auto check=[&](const Job &candidate){
            QString reason;const bool ok=validateJobDocument(candidate,reason);
            return ok?QString():reason;
        };
        QVERIFY(check(job).isEmpty());

        auto wrongHash=job;wrongHash.hash=QString(64,'a');
        if(wrongHash.hash==hash)wrongHash.hash=QString(64,'b');
        QVERIFY(!check(wrongHash).isEmpty());

        auto wrongPath=job;wrongPath.document=snapshot(makeDocument(source+QStringLiteral("x"),hash,recipe,3));
        QVERIFY(!check(wrongPath).isEmpty());

        auto relative=job;relative.document=snapshot(makeDocument(QStringLiteral("relative/source.jpg"),hash,recipe,3));
        QVERIFY(!check(relative).isEmpty());

        Recipe other;other.exposure=.5;
        auto wrongGlobal=job;wrongGlobal.document=snapshot(makeDocument(source,hash,other,3));
        QVERIFY(!check(wrongGlobal).isEmpty());

        auto wrongRecipe=job;wrongRecipe.recipe=other;
        QVERIFY(!check(wrongRecipe).isEmpty());

        auto wrongRevision=job;wrongRevision.document=snapshot(makeDocument(source,hash,recipe,4));
        QVERIFY(!check(wrongRevision).isEmpty());

        auto wrongJobRevision=job;wrongJobRevision.revision=4;
        QVERIFY(!check(wrongJobRevision).isEmpty());

        auto wrongRaw=job;auto rawDocument=makeDocument(source,hash,recipe,3);rawDocument.asset.raw=true;wrongRaw.document=snapshot(rawDocument);
        QVERIFY(!check(wrongRaw).isEmpty());

        auto unknownAlgorithm=job;auto algorithmDocument=makeDocument(source,hash,recipe,3);algorithmDocument.algorithmVersion=9;unknownAlgorithm.document=snapshot(algorithmDocument);
        QVERIFY(!check(unknownAlgorithm).isEmpty());

        auto zeroSize=job;auto sizeDocument=makeDocument(source,hash,recipe,3);sizeDocument.asset.normalizedSize=QSize(0,0);zeroSize.document=snapshot(sizeDocument);
        QVERIFY(!check(zeroSize).isEmpty());

        auto missingFile=job;missingFile.source=dir.filePath(QStringLiteral("not-created.jpg"));
        missingFile.document=snapshot(makeDocument(missingFile.source,hash,recipe,3));
        QVERIFY2(validateJobDocument(missingFile,error),qPrintable(error)); // no filesystem IO

        Job legacy=job;legacy.document.reset();
        QVERIFY(validateJobDocument(legacy,error));
    }
    void freezeCopiesSharedDocument(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto source=sample(dir);QVERIFY(!source.isEmpty());
        const auto hash=QString::fromLatin1(digest(source));
        const auto recipe=sampleRecipe();
        auto original=std::make_shared<LocalEdit::PhotoDocument>(makeDocument(source,hash,recipe,2));
        Settings settings=srgbSettings();
        QString error;
        const QList<Asset> assets{Asset{source,hash,recipe,2,false,false,original}};
        const auto jobs=freeze(assets,settings,error);
        QVERIFY2(jobs.size()==1,qPrintable(error));
        QVERIFY(jobs.first().document);
        QVERIFY(jobs.first().document!=original);
        const auto frozen=jobs.first().document;

        original->layers[0].opacity=.1;
        original->layers[0].name=QStringLiteral("mutated");
        original->global.exposure=-1;
        original->revision=99;
        QCOMPARE(frozen->layers[0].opacity,.8);
        QCOMPARE(frozen->layers[0].name,QStringLiteral("brush"));
        QCOMPARE(frozen->global.exposure,recipe.exposure);
        QCOMPARE(frozen->revision,quint64(2));
        QVERIFY(validateJobDocument(jobs.first(),error));

        const QList<Asset> stableAssets{Asset{source,hash,recipe,2,false,false,snapshot(makeDocument(source,hash,recipe,2))}};
        auto wrongHashAsset=stableAssets;wrongHashAsset[0].hash=QString(64,'c');
        QVERIFY(freeze(wrongHashAsset,settings,error).isEmpty());QVERIFY(!error.isEmpty());
        auto wrongRecipeAsset=stableAssets;wrongRecipeAsset[0].recipe=Recipe{};
        QVERIFY(freeze(wrongRecipeAsset,settings,error).isEmpty());
        auto wrongRevisionAsset=stableAssets;wrongRevisionAsset[0].revision=5;
        QVERIFY(freeze(wrongRevisionAsset,settings,error).isEmpty());
        auto wrongLeaf=stableAssets;wrongLeaf[0].path=QFileInfo(source).absolutePath()+QStringLiteral("/./")+QFileInfo(source).fileName();
        const auto bound=freeze(wrongLeaf,settings,error);
        QVERIFY2(bound.size()==1,qPrintable(error));
        QCOMPARE(bound.first().document->asset.sourcePath,bound.first().source);
        QVERIFY(QDir().mkpath(QFileInfo(bound.first().path).absolutePath()));
        QFile existing(bound.first().path);QVERIFY(existing.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(existing.write("existing"),qint64(8));existing.close();
        settings.collision=Collision::Skip;const auto skipped=freeze(stableAssets,settings,error);QCOMPARE(skipped.size(),1);QCOMPARE(skipped.first().status,Status::Skipped);QVERIFY(skipped.first().document);QVERIFY(*skipped.first().document==*stableAssets.first().document);
    }
    void queueStoresDecodedCopies(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto source=sample(dir);QVERIFY(!source.isEmpty());
        const auto hash=QString::fromLatin1(digest(source));
        const auto recipe=sampleRecipe();
        const auto record=dir.filePath(QStringLiteral("records/queue.json"));
        QString error;
        {
            Queue queue(record);
            auto alias=std::make_shared<LocalEdit::PhotoDocument>(makeDocument(source,hash,recipe,2));
            Job job=makeJob(source,hash,recipe,2);
            job.document=alias;
            QVERIFY2(queue.append({job},error),qPrintable(error));
            alias->layers[0].name=QStringLiteral("mutated");
            alias->global.exposure=-1;
            QCOMPARE(queue.jobs().size(),1);
            QVERIFY(queue.jobs()[0].document);
            QCOMPARE(queue.jobs()[0].document->layers[0].name,QStringLiteral("brush"));
            QCOMPARE(queue.jobs()[0].document->global.exposure,recipe.exposure);
        }
        Queue loaded(record);
        QVERIFY2(loaded.load(error),qPrintable(error));
        QVERIFY2(queueStateHeld(record),"queued document changed with caller alias");
    }

    void queueRejectsUnserializableJob(){
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto source=sample(dir);QVERIFY(!source.isEmpty());
        const auto hash=QString::fromLatin1(digest(source));
        const auto recipe=sampleRecipe();
        Job job=makeJob(source,hash,recipe,2);
        auto broken=std::make_shared<LocalEdit::PhotoDocument>(*job.document);
        broken->asset.normalizedSize=QSize(0,0);
        job.document=broken;
        QVERIFY(jobJson(job).isEmpty());
        QString error;
        Queue queue(dir.filePath(QStringLiteral("records/queue.json")));
        QVERIFY(!queue.append({job},error));
        QVERIFY(!error.isEmpty());
        Job untouched=makeJob(source,hash,recipe,2);
        untouched.id=QStringLiteral("sentinel");
        QVERIFY(!jobFromJson(jobJson(job),untouched,error));
        QCOMPARE(untouched.id,QStringLiteral("sentinel"));
    }

private:
    bool queueStateHeld(const QString &record){
        QFile file(record);if(!file.open(QIODevice::ReadOnly))return false;
        const auto root=QJsonDocument::fromJson(file.readAll()).object();
        const auto jobs=root.value(QStringLiteral("jobs")).toArray();
        if(jobs.size()!=1)return false;
        const auto document=jobs[0].toObject().value(QStringLiteral("document")).toObject();
        const auto object=document.value(QStringLiteral("layers")).toArray();
        if(object.size()!=1)return false;
        const auto layer=object[0].toObject();
        if(layer.value(QStringLiteral("name")).toString()!=QStringLiteral("brush"))return false;
        return std::abs(document.value(QStringLiteral("global")).toObject().value(QStringLiteral("exposure")).toDouble()-sampleRecipe().exposure)<1e-9;
    }
};

QTEST_GUILESS_MAIN(ExportDocumentTypesTests)
#include "export_document_types_tests.moc"
