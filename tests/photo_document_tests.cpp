#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "core/photo_document.h"

using namespace LocalEdit;

// S3 A2 public data-contract suite. Every assertion observes the declared public
// API (validDocument/documentJson/documentFromJson/documentFromLegacyRecipe) and
// the canonical JSON schema; nothing here re-implements a renderer or a writer.
namespace {
using namespace LocalEdit;

QJsonObject encodedDocument(const PhotoDocument &document){
    QString error;const auto result=LocalEdit::documentJson(document,error);
    if(!error.isEmpty())QTest::qFail(qPrintable(error),__FILE__,__LINE__);
    return result;
}
AssetIdentity readyAsset(){
    AssetIdentity asset;
    asset.id=QStringLiteral("asset-1");
    asset.sourcePath=QStringLiteral("C:/photos/one.cr3");
    asset.sourceHash=QString(64,QChar('a'));
    asset.normalizedSize=QSize(8000,5625);
    asset.raw=true;
    asset.sourceFacts=QJsonObject{{QStringLiteral("sourceKind"),QStringLiteral("raw")},
        {QStringLiteral("orientation"),1}};
    return asset;
}

AssetIdentity pendingAsset(){
    AssetIdentity asset=readyAsset();
    asset.sourceHash.clear();
    asset.normalizedSize=QSize(0,0);
    return asset;
}

MaskComponent brushComponent(const QString &id,double hardness=.6){
    MaskComponent component;
    component.id=id;
    component.operation=MaskOperation::Add;
    component.strength=.75;
    BrushGeometry brush;
    brush.points={{10.5,-20.25},{30,40}};
    brush.radius=250;
    brush.hardness=hardness;
    component.geometry=brush;
    return component;
}

MaskComponent linearComponent(const QString &id){
    MaskComponent component;
    component.id=id;
    component.operation=MaskOperation::Subtract;
    component.strength=.5;
    component.geometry=LinearGeometry{QPointF(-5,7),QPointF(120.5,900)};
    return component;
}

MaskComponent radialComponent(const QString &id){
    MaskComponent component;
    component.id=id;
    component.operation=MaskOperation::Add;
    component.strength=1;
    component.geometry=RadialGeometry{QPointF(3.5,-4.25),250.5,180.25,-33.75};
    return component;
}

Recipe richGlobal(){
    Recipe recipe;
    recipe.exposure=.35;recipe.contrast=-.2;recipe.saturation=1.4;recipe.warmth=.1;recipe.tint=-.05;
    recipe.highlights=-.4;recipe.shadows=.25;recipe.whites=.05;recipe.blacks=-.1;recipe.vibrance=.3;
    recipe.texture=.2;recipe.clarity=-.15;recipe.dehaze=.1;
    recipe.sharpenAmount=.4;recipe.sharpenRadius=1.5;recipe.sharpenThreshold=.02;
    recipe.curves[0]={{0,.02},{.5,.55},{1,.98}};
    recipe.curveTangents[0]={.1,.2,.3,.4,.5};
    recipe.luminanceCurve={{0,.01},{.4,.45},{1,.99}};
    recipe.luminanceTangents={.05,.15,.25,.35,.45};
    recipe.hsl[0]={.1,.2,-.3};recipe.hsl[5]={-.2,.05,.1};
    recipe.parametricCurve.regions={.1,-.2,.3,-.05};
    recipe.parametricCurve.splits={.2,.5,.8};
    recipe.grading[0]={12,.3,.1};recipe.grading[2]={340,.2,.05};
    recipe.gradingBlend=.4;recipe.gradingBalance=-.1;
    recipe.pointColors.append(PointColorAdjustment{120,.6,.4,25,.3,.25,.6,.1,-.2,.05});
    recipe.pointColors.append(PointColorAdjustment{30,.5,.5});
    recipe.monochrome=false;
    recipe.monochromeMix={.1,.2,.3,.4,.5,.6,.7,.8};
    recipe.primaries[0]={.05,-.02};
    recipe.grainAmount=.2;recipe.grainSize=4;recipe.grainRoughness=.3;
    recipe.defringe.purpleAmount=.4;
    recipe.crop={.05,.05,.9,.9};
    recipe.angle=1.5;
    return recipe;
}

PhotoDocument sampleDocument(){
    PhotoDocument document;
    document.asset=readyAsset();
    document.asset.sourceFacts.insert(QStringLiteral("privateExtension"),
        QJsonObject{{QStringLiteral("a"),1},{QStringLiteral("b"),QStringLiteral("x")}});
    document.global=richGlobal();
    AdjustmentLayer bottom;
    bottom.id=QStringLiteral("layer-bottom");
    bottom.name=QStringLiteral("Bottom");
    bottom.enabled=false;
    bottom.opacity=.65;
    bottom.parameters=LocalParameters{.5,-.25,.1,-.1,.05,-.05,.2,-.2,1.75};
    bottom.mask.components={brushComponent(QStringLiteral("component-brush")),
        linearComponent(QStringLiteral("component-linear")),
        radialComponent(QStringLiteral("component-radial"))};
    bottom.mask.feather=12.5;
    bottom.mask.invert=true;
    bottom.mask.version=7;
    bottom.version=3;
    AdjustmentLayer top;
    top.id=QStringLiteral("layer-top");
    top.name=QStringLiteral("Top");
    top.mask.components={radialComponent(QStringLiteral("component-radial-top"))};
    document.layers={bottom,top};
    document.revision=18446744073709551614ull;
    document.localIdentity=9007199254740993ull;
    return document;
}

QJsonObject minimalDocumentJson(){
    // Minimal canonical document, mutated per rejection case.
    PhotoDocument document;
    document.asset=readyAsset();
    AdjustmentLayer layer;
    layer.id=QStringLiteral("layer-1");
    layer.name=QStringLiteral("Layer 1");
    layer.mask.components={brushComponent(QStringLiteral("component-1"))};
    document.layers={layer};
    return encodedDocument(document);
}
}

class PhotoDocumentTests:public QObject {
    Q_OBJECT
private slots:
    void richRoundTripAllGeometryKinds(){
        const PhotoDocument document=sampleDocument();
        QString error=QStringLiteral("stale");
        QVERIFY2(validDocument(document,error,true),qPrintable(error));
        QVERIFY(error.isEmpty());
        const QJsonObject json=encodedDocument(document);
        QVERIFY2(!json.isEmpty(),qPrintable(error));
        QCOMPARE(json.value(QStringLiteral("format")).toString(),QStringLiteral("LuminaS.photo-document"));
        QCOMPARE(json.value(QStringLiteral("formatVersion")).toInt(),1);
        QCOMPARE(json.value(QStringLiteral("algorithmVersion")).toInt(),1);
        QCOMPARE(json.value(QStringLiteral("revision")).toString(),QStringLiteral("18446744073709551614"));
        QCOMPARE(json.value(QStringLiteral("localIdentity")).toString(),QStringLiteral("9007199254740993"));
        const QJsonObject asset=json.value(QStringLiteral("asset")).toObject();
        QCOMPARE(asset.keys().size(),6);
        QCOMPARE(asset.value(QStringLiteral("normalizedSize")).toArray().size(),2);
        const QJsonArray layers=json.value(QStringLiteral("layers")).toArray();
        QCOMPARE(layers.size(),2);
        const QJsonObject bottom=layers.at(0).toObject();
        QCOMPARE(bottom.keys().size(),7);
        QCOMPARE(bottom.value(QStringLiteral("parameters")).toObject().keys().size(),9);
        const QJsonObject mask=bottom.value(QStringLiteral("mask")).toObject();
        QCOMPARE(mask.value(QStringLiteral("components")).toArray().size(),3);
        QCOMPARE(mask.value(QStringLiteral("version")).toString(),QStringLiteral("7"));
        const QJsonArray components=mask.value(QStringLiteral("components")).toArray();
        const QStringList kinds{QStringLiteral("brush"),QStringLiteral("linear"),QStringLiteral("radial")};
        for(int index=0;index<3;++index){
            const QJsonObject component=components.at(index).toObject();
            QCOMPARE(component.keys().size(),4);
            QCOMPARE(component.value(QStringLiteral("operation")).toString(),
                index==1?QStringLiteral("subtract"):QStringLiteral("add"));
            const QJsonObject geometry=component.value(QStringLiteral("geometry")).toObject();
            QCOMPARE(geometry.value(QStringLiteral("kind")).toString(),kinds.at(index));
        }
        PhotoDocument parsed;
        error=QStringLiteral("stale");
        QVERIFY2(documentFromJson(json,parsed,error,true),qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(parsed,document);
        QCOMPARE(parsed.revision,quint64(18446744073709551614ull));
        QCOMPARE(encodedDocument(parsed).value(QStringLiteral("global")).toObject(),
            json.value(QStringLiteral("global")).toObject());
    }

    void hugeUnsignedIdentityPreserved(){
        PhotoDocument document;
        document.asset=readyAsset();
        document.revision=18446744073709551615ull;
        document.localIdentity=9007199254740995ull;
        const QJsonObject json=encodedDocument(document);
        QCOMPARE(json.value(QStringLiteral("revision")).toString(),QStringLiteral("18446744073709551615"));
        QCOMPARE(json.value(QStringLiteral("localIdentity")).toString(),QStringLiteral("9007199254740995"));
        PhotoDocument parsed;
        QString error;
        QVERIFY2(documentFromJson(json,parsed,error,true),qPrintable(error));
        QCOMPARE(parsed.revision,quint64(18446744073709551615ull));
        QCOMPARE(parsed.localIdentity,quint64(9007199254740995ull));
        QJsonObject huge=json;
        huge.insert(QStringLiteral("revision"),QStringLiteral("18446744073709551616"));
        PhotoDocument rejected=document;
        error.clear();
        QVERIFY2(!documentFromJson(huge,rejected,error,true),"revision overflow accepted");
        QVERIFY(!error.isEmpty());
        QCOMPARE(rejected,document);
    }

    void pendingAssetEditingValidPersistenceRejected(){
        PhotoDocument document;
        document.asset=pendingAsset();
        QString error;
        QVERIFY2(validDocument(document,error,false),qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY2(!validDocument(document,error,true),QStringLiteral("pending asset passed persistence").toUtf8().constData());
        QVERIFY(!error.isEmpty());
        // The data contract requires an editing document to round trip through the serializer.
        const QJsonObject json=encodedDocument(document);
        QVERIFY(!json.isEmpty());
        QCOMPARE(json.value(QStringLiteral("asset")).toObject().value(QStringLiteral("sourceHash")).toString(),QString());
        QCOMPARE(json.value(QStringLiteral("asset")).toObject().value(QStringLiteral("normalizedSize")).toArray().size(),2);
        PhotoDocument parsed;
        QVERIFY2(documentFromJson(json,parsed,error,false),qPrintable(error));
        QCOMPARE(parsed,document);
    }

    void sourceIdentityBoundary45MP(){
        PhotoDocument document;
        document.asset=readyAsset();
        document.asset.normalizedSize=QSize(9000,5000);
        QString error;
        QVERIFY2(validDocument(document,error,true),qPrintable(error));
        PhotoDocument oversized=document;
        oversized.asset.normalizedSize=QSize(9000,5001);
        QVERIFY2(!validDocument(oversized,error,true),QStringLiteral("over 45MP accepted").toUtf8().constData());
        QVERIFY(!error.isEmpty());
        PhotoDocument mixed=document;
        mixed.asset.normalizedSize=QSize(4000,0);
        QVERIFY2(!validDocument(mixed,error,false),QStringLiteral("mixed dimensions accepted").toUtf8().constData());
        PhotoDocument negative=document;
        negative.asset.normalizedSize=QSize(-4000,-4000);
        QVERIFY2(!validDocument(negative,error,false),QStringLiteral("negative dimensions accepted").toUtf8().constData());
        QVERIFY(!error.isEmpty());
    }

    void unknownAndWrongTypedInputsRejectedWithoutMutation(){
        const QJsonObject base=minimalDocumentJson();
        const QByteArray bytes=QJsonDocument(base).toJson(QJsonDocument::Compact);
        QJsonObject root=base;
        root.insert(QStringLiteral("extraRoot"),1);
        QJsonObject layer=base;
        QJsonArray layers=layer.value(QStringLiteral("layers")).toArray();
        QJsonObject first=layers.at(0).toObject();
        first.insert(QStringLiteral("extraLayer"),true);
        layers.replace(0,first);
        layer.insert(QStringLiteral("layers"),layers);
        QJsonObject mask=base;
        layers=mask.value(QStringLiteral("layers")).toArray();
        first=layers.at(0).toObject();
        QJsonObject maskObject=first.value(QStringLiteral("mask")).toObject();
        maskObject.insert(QStringLiteral("extraMask"),QStringLiteral("x"));
        first.insert(QStringLiteral("mask"),maskObject);
        layers.replace(0,first);
        mask.insert(QStringLiteral("layers"),layers);
        QJsonObject geometry=base;
        layers=geometry.value(QStringLiteral("layers")).toArray();
        first=layers.at(0).toObject();
        maskObject=first.value(QStringLiteral("mask")).toObject();
        QJsonArray components=maskObject.value(QStringLiteral("components")).toArray();
        QJsonObject component=components.at(0).toObject();
        QJsonObject geometryObject=component.value(QStringLiteral("geometry")).toObject();
        geometryObject.insert(QStringLiteral("extraGeometry"),0);
        component.insert(QStringLiteral("geometry"),geometryObject);
        components.replace(0,component);
        maskObject.insert(QStringLiteral("components"),components);
        first.insert(QStringLiteral("mask"),maskObject);
        layers.replace(0,first);
        geometry.insert(QStringLiteral("layers"),layers);
        QJsonObject global=base;
        QJsonObject globalObject=global.value(QStringLiteral("global")).toObject();
        globalObject.insert(QStringLiteral("extraGlobal"),1);
        global.insert(QStringLiteral("global"),globalObject);
        QJsonObject nestedGlobal=base;
        globalObject=nestedGlobal.value(QStringLiteral("global")).toObject();
        QJsonObject parametric=globalObject.value(QStringLiteral("parametricCurve")).toObject();
        parametric.insert(QStringLiteral("extraNested"),2);
        globalObject.insert(QStringLiteral("parametricCurve"),parametric);
        nestedGlobal.insert(QStringLiteral("global"),globalObject);
        QJsonObject future=base;
        future.insert(QStringLiteral("formatVersion"),2);
        QJsonObject algorithm=base;
        algorithm.insert(QStringLiteral("algorithmVersion"),2);
        QJsonObject kind=geometry;
        layers=kind.value(QStringLiteral("layers")).toArray();
        first=layers.at(0).toObject();
        maskObject=first.value(QStringLiteral("mask")).toObject();
        components=maskObject.value(QStringLiteral("components")).toArray();
        component=components.at(0).toObject();
        geometryObject=component.value(QStringLiteral("geometry")).toObject();
        geometryObject.insert(QStringLiteral("kind"),QStringLiteral("polygon"));
        component.insert(QStringLiteral("geometry"),geometryObject);
        components.replace(0,component);
        maskObject.insert(QStringLiteral("components"),components);
        first.insert(QStringLiteral("mask"),maskObject);
        layers.replace(0,first);
        kind.insert(QStringLiteral("layers"),layers);
        const QList<QPair<QString,QJsonObject>> cases{
            {QStringLiteral("unknownRoot"),root},{QStringLiteral("unknownLayer"),layer},
            {QStringLiteral("unknownMask"),mask},{QStringLiteral("unknownGeometry"),geometry},
            {QStringLiteral("unknownGlobal"),global},{QStringLiteral("unknownNestedGlobal"),nestedGlobal},
            {QStringLiteral("futureFormat"),future},{QStringLiteral("futureAlgorithm"),algorithm},
            {QStringLiteral("unknownKind"),kind},
        };
        for(const auto &entry:cases){
            PhotoDocument sentinel=sampleDocument();
            const PhotoDocument original=sentinel;
            QString error=QStringLiteral("stale");
            const bool accepted=documentFromJson(entry.second,sentinel,error,true);
            QVERIFY2(!accepted,qPrintable(entry.first));
            QVERIFY2(!error.isEmpty(),qPrintable(entry.first));
            QVERIFY2(sentinel==original,qPrintable(QStringLiteral("output mutated by %1").arg(entry.first)));
        }
        // A first parse leaves the output usable and later failures never change it again.
        PhotoDocument output;
        QString error;
        QVERIFY2(documentFromJson(base,output,error,true),qPrintable(error));
        const PhotoDocument parsed=output;
        QVERIFY(!documentFromJson(root,output,error,true));
        QCOMPARE(output,parsed);
        QVERIFY(QJsonDocument(base).toJson(QJsonDocument::Compact)==bytes);
        QJsonObject wrongType=base;
        layers=wrongType.value(QStringLiteral("layers")).toArray();
        first=layers.at(0).toObject();
        first.insert(QStringLiteral("enabled"),1);
        layers.replace(0,first);
        wrongType.insert(QStringLiteral("layers"),layers);
        QVERIFY2(!documentFromJson(wrongType,output,error,true),qPrintable(error));
        QCOMPARE(output,parsed);
        QJsonObject numberForBool=base;
        layers=numberForBool.value(QStringLiteral("layers")).toArray();
        first=layers.at(0).toObject();
        maskObject=first.value(QStringLiteral("mask")).toObject();
        maskObject.insert(QStringLiteral("invert"),1);
        first.insert(QStringLiteral("mask"),maskObject);
        layers.replace(0,first);
        numberForBool.insert(QStringLiteral("layers"),layers);
        QVERIFY2(!documentFromJson(numberForBool,output,error,true),qPrintable(error));
        QCOMPARE(output,parsed);
    }

    void globalRecipeDataRoundTripsExactly(){
        const PhotoDocument document=sampleDocument();
        const QJsonObject global=encodedDocument(document).value(QStringLiteral("global")).toObject();
        QCOMPARE(global,recipeJson(document.global));
        QCOMPARE(global.value(QStringLiteral("curves")).toArray().at(0).toArray().size(),3);
        QCOMPARE(global.value(QStringLiteral("curveTangents")).toArray().at(0).toArray().size(),5);
        QCOMPARE(global.value(QStringLiteral("hsl")).toArray().size(),8);
        QCOMPARE(global.value(QStringLiteral("pointColors")).toArray().at(0).toObject().keys().size(),10);
        QCOMPARE(global.value(QStringLiteral("parametricCurve")).toObject().value(QStringLiteral("splits")).toArray().size(),3);
        PhotoDocument parsed;
        QString error;
        QVERIFY2(documentFromJson(encodedDocument(document),parsed,error,true),qPrintable(error));
        QCOMPARE(parsed.global,document.global);
        QCOMPARE(recipeJson(parsed.global),global);
    }

    void identityAndNumericBounds(){
        PhotoDocument document;
        document.asset=readyAsset();
        QString error;
        QVERIFY(validDocument(document,error,false));
        PhotoDocument longId=document;
        longId.asset.id=QString(129,QChar('z'));
        QVERIFY(!validDocument(longId,error,false));
        QVERIFY(!error.isEmpty());
        PhotoDocument emptyId=document;
        emptyId.layers.append(AdjustmentLayer{});
        QVERIFY(!validDocument(emptyId,error,false));
        PhotoDocument duplicate=document;
        AdjustmentLayer layer;
        layer.id=QStringLiteral("same-id");
        layer.mask.components={brushComponent(QStringLiteral("same-id"))};
        AdjustmentLayer second;
        second.id=QStringLiteral("same-id");
        duplicate.layers={layer,second};
        QVERIFY2(!validDocument(duplicate,error,false),QStringLiteral("duplicate layer id accepted").toUtf8().constData());
        QVERIFY(!error.isEmpty());
        PhotoDocument duplicateComponent=document;
        layer.mask.components={brushComponent(QStringLiteral("component-x")),radialComponent(QStringLiteral("component-x"))};
        layer.id=QStringLiteral("layer-x");
        duplicateComponent.layers={layer};
        QVERIFY2(!validDocument(duplicateComponent,error,false),QStringLiteral("duplicate component id accepted").toUtf8().constData());
        PhotoDocument nonFinite=document;
        AdjustmentLayer numbers;
        numbers.id=QStringLiteral("layer-numbers");
        numbers.parameters.exposure=std::numeric_limits<double>::quiet_NaN();
        nonFinite.layers={numbers};
        QVERIFY(!validDocument(nonFinite,error,false));
        QVERIFY(!error.isEmpty());
        nonFinite.layers.first().parameters=LocalParameters{5.5,0,0,0,0,0,0,0,1};
        QVERIFY2(!validDocument(nonFinite,error,false),QStringLiteral("out of range exposure accepted").toUtf8().constData());
        nonFinite.layers.first().parameters=LocalParameters{0,0,0,0,0,0,0,0,0};
        QVERIFY2(validDocument(nonFinite,error,false),qPrintable(error));
        nonFinite.layers.first().parameters=LocalParameters{0,0,0,0,0,0,0,0,2.5};
        QVERIFY2(!validDocument(nonFinite,error,false),QStringLiteral("saturation 2.5 accepted").toUtf8().constData());
        PhotoDocument badGeometry=document;
        AdjustmentLayer geometry;
        geometry.id=QStringLiteral("layer-geometry");
        MaskComponent emptyBrush=brushComponent(QStringLiteral("component-empty"));
        std::get<BrushGeometry>(emptyBrush.geometry).points.clear();
        MaskComponent coincident=linearComponent(QStringLiteral("component-coincident"));
        std::get<LinearGeometry>(coincident.geometry).end=std::get<LinearGeometry>(coincident.geometry).start;
        MaskComponent hugeRadius=radialComponent(QStringLiteral("component-huge"));
        std::get<RadialGeometry>(hugeRadius.geometry).radiusX=1e9;
        MaskComponent badFeather;
        badFeather.id=QStringLiteral("component-feather");
        badFeather.geometry=brushComponent(QStringLiteral("component-feather")).geometry;
        geometry.mask.components={emptyBrush};
        badGeometry.layers={geometry};
        QVERIFY2(!validDocument(badGeometry,error,false),QStringLiteral("empty brush accepted").toUtf8().constData());
        geometry.mask.components={coincident};
        QVERIFY2(!validDocument(badGeometry,error,false),QStringLiteral("coincident linear endpoints accepted").toUtf8().constData());
        geometry.mask.components={hugeRadius};
        QVERIFY2(!validDocument(badGeometry,error,false),QStringLiteral("radius above 1e8 accepted").toUtf8().constData());
        geometry.mask.components={badFeather};
        geometry.mask.feather=16385;
        QVERIFY2(!validDocument(badGeometry,error,false),QStringLiteral("feather above 16384 accepted").toUtf8().constData());
    }

    void countLimitsEnforced(){
        PhotoDocument document;
        document.asset=readyAsset();
        AdjustmentLayer layer;
        layer.id=QStringLiteral("layer-limit");
        QString error;
        MaskComponent stroke=brushComponent(QStringLiteral("component-limit"));
        std::get<BrushGeometry>(stroke.geometry).points=QVector<QPointF>(262144,QPointF(1,2));
        layer.mask.components={stroke};
        document.layers={layer};
        QVERIFY2(validDocument(document,error,false),qPrintable(error));
        std::get<BrushGeometry>(document.layers.first().mask.components.first().geometry).points.append(QPointF(3,4));
        QVERIFY2(!validDocument(document,error,false),QStringLiteral("262145 point stroke accepted").toUtf8().constData());
        QVERIFY(!error.isEmpty());
        PhotoDocument tooManyLayers;
        tooManyLayers.asset=readyAsset();
        for(int index=0;index<65;++index){
            AdjustmentLayer entry;
            entry.id=QStringLiteral("layer-%1").arg(index);
            tooManyLayers.layers.append(entry);
        }
        QVERIFY2(!validDocument(tooManyLayers,error,false),QStringLiteral("65 layers accepted").toUtf8().constData());
        PhotoDocument tooManyComponents;
        tooManyComponents.asset=readyAsset();
        AdjustmentLayer dense;
        dense.id=QStringLiteral("layer-dense");
        for(int index=0;index<4097;++index){
            MaskComponent entry;
            entry.id=QStringLiteral("component-%1").arg(index);
            entry.geometry=RadialGeometry{};
            dense.mask.components.append(entry);
        }
        tooManyComponents.layers={dense};
        QVERIFY2(!validDocument(tooManyComponents,error,false),QStringLiteral("4097 components accepted").toUtf8().constData());
        dense.mask.components.removeLast();
        tooManyComponents.layers={dense};
        QVERIFY2(validDocument(tooManyComponents,error,false),qPrintable(error));
    }

    void legacyRecipeWrapAndRejection(){
        const AssetIdentity asset=pendingAsset();
        PhotoDocument document;
        QString error=QStringLiteral("stale");
        const QJsonObject legacy{{QStringLiteral("exposure"),.5},{QStringLiteral("contrast"),-.25}};
        QVERIFY2(documentFromLegacyRecipe(asset,legacy,document,error),qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(document.layers.isEmpty());
        QCOMPARE(document.revision,quint64(1));
        QCOMPARE(document.localIdentity,quint64(0));
        QCOMPARE(document.algorithmVersion,1);
        QCOMPARE(document.asset,asset);
        QCOMPARE(document.global.exposure,.5);
        QCOMPARE(document.global.contrast,-.25);
        QCOMPARE(document.global.saturation,1.0);
        QCOMPARE(document.global.sharpenRadius,1.0);
        QVERIFY2(validDocument(document,error,false),qPrintable(error));
        QCOMPARE(encodedDocument(document).value(QStringLiteral("layers")).toArray().size(),0);
        PhotoDocument untouched=document;
        QJsonObject unknown=legacy;
        unknown.insert(QStringLiteral("notARecipeField"),1);
        QVERIFY2(!documentFromLegacyRecipe(asset,unknown,untouched,error),
            QStringLiteral("unknown legacy field accepted").toUtf8().constData());
        QVERIFY(!error.isEmpty());
        QCOMPARE(untouched,document);
        // A full serialized document is not a legacy recipe.
        PhotoDocument disguised=sampleDocument();
        QVERIFY2(!documentFromLegacyRecipe(asset,encodedDocument(disguised),untouched,error),
            QStringLiteral("full document accepted as a preset").toUtf8().constData());
        QCOMPARE(untouched,document);
        PhotoDocument badAsset;
        AssetIdentity nameless=asset;
        nameless.sourcePath.clear();
        QVERIFY2(!documentFromLegacyRecipe(nameless,legacy,badAsset,error),
            QStringLiteral("empty sourcePath accepted").toUtf8().constData());
        QVERIFY(!error.isEmpty());
    }

    void schemaKeysAreCanonicalAndStable(){
        const QJsonObject json=minimalDocumentJson();
        QStringList rootKeys=json.keys();
        rootKeys.sort();
        const QStringList expectedRoot{QStringLiteral("algorithmVersion"),QStringLiteral("asset"),
            QStringLiteral("format"),QStringLiteral("formatVersion"),QStringLiteral("global"),
            QStringLiteral("layers"),QStringLiteral("localIdentity"),QStringLiteral("revision")};
        QCOMPARE(rootKeys,expectedRoot);
        QStringList assetKeys=json.value(QStringLiteral("asset")).toObject().keys();
        assetKeys.sort();
        const QStringList expectedAsset{QStringLiteral("id"),QStringLiteral("normalizedSize"),
            QStringLiteral("raw"),QStringLiteral("sourceFacts"),QStringLiteral("sourceHash"),
            QStringLiteral("sourcePath")};
        QCOMPARE(assetKeys,expectedAsset);
        const QJsonArray layers=json.value(QStringLiteral("layers")).toArray();
        QStringList layerKeys=layers.at(0).toObject().keys();
        layerKeys.sort();
        const QStringList expectedLayer{QStringLiteral("enabled"),QStringLiteral("id"),QStringLiteral("mask"),
            QStringLiteral("name"),QStringLiteral("opacity"),QStringLiteral("parameters"),QStringLiteral("version")};
        QCOMPARE(layerKeys,expectedLayer);
        QStringList maskKeys=layers.at(0).toObject().value(QStringLiteral("mask")).toObject().keys();
        maskKeys.sort();
        const QStringList expectedMask{QStringLiteral("components"),QStringLiteral("feather"),
            QStringLiteral("invert"),QStringLiteral("version")};
        QCOMPARE(maskKeys,expectedMask);
        const QJsonObject component=layers.at(0).toObject().value(QStringLiteral("mask")).toObject()
            .value(QStringLiteral("components")).toArray().at(0).toObject();
        QStringList componentKeys=component.keys();
        componentKeys.sort();
        const QStringList expectedComponent{QStringLiteral("geometry"),QStringLiteral("id"),
            QStringLiteral("operation"),QStringLiteral("strength")};
        QCOMPARE(componentKeys,expectedComponent);
        QStringList brushKeys=component.value(QStringLiteral("geometry")).toObject().keys();
        brushKeys.sort();
        const QStringList expectedBrush{QStringLiteral("hardness"),QStringLiteral("kind"),
            QStringLiteral("points"),QStringLiteral("radius")};
        QCOMPARE(brushKeys,expectedBrush);
        QStringList parameterKeys=layers.at(0).toObject().value(QStringLiteral("parameters")).toObject().keys();
        parameterKeys.sort();
        const QStringList expectedParameters{QStringLiteral("blacks"),QStringLiteral("contrast"),
            QStringLiteral("exposure"),QStringLiteral("highlights"),QStringLiteral("saturation"),
            QStringLiteral("shadows"),QStringLiteral("tint"),QStringLiteral("warmth"),QStringLiteral("whites")};
        QCOMPARE(parameterKeys,expectedParameters);
        QCOMPARE(parameterKeys.size(),9);
    }

    void opaqueSourceFactsPreserved(){
        PhotoDocument document=sampleDocument();
        document.asset.sourceFacts=QJsonObject{{QStringLiteral("vendorBlock"),
            QJsonObject{{QStringLiteral("unknownA"),QJsonArray{1,2,3}},
                {QStringLiteral("unknownB"),QStringLiteral("keep")}}},
            {QStringLiteral("orientation"),6}};
        PhotoDocument parsed;
        QString error;
        QVERIFY2(documentFromJson(encodedDocument(document),parsed,error,true),qPrintable(error));
        QCOMPARE(parsed.asset.sourceFacts,document.asset.sourceFacts);
        QCOMPARE(parsed.asset,document.asset);
    }

    void snapshotCopyIsIndependent(){
        const DocumentSnapshot snapshot=std::make_shared<const PhotoDocument>(sampleDocument());
        const quint64 revision=snapshot->revision;
        PhotoDocument copy=*snapshot;
        QCOMPARE(copy,*snapshot);
        std::get<BrushGeometry>(copy.layers.first().mask.components.first().geometry).points[0]=QPointF(999,-999);
        QVERIFY(copy!=*snapshot);
        QCOMPARE(snapshot->revision,revision);
        QCOMPARE(snapshot->layers.first().mask.components.first(),
            sampleDocument().layers.first().mask.components.first());
        QCOMPARE(std::get<BrushGeometry>(snapshot->layers.first().mask.components.first().geometry).points.at(0),
            QPointF(10.5,-20.25));
    }
};

QTEST_APPLESS_MAIN(PhotoDocumentTests)

#include "photo_document_tests.moc"
