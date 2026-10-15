#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <limits>
#include "core/defringe_json.h"
#include "core/document.h"

class DefringeJsonTests:public QObject {
    Q_OBJECT
private slots:
    void legacyAndDefaults();
    void roundTrip();
    void strictRejection();
    void atomicOutputUnchanged();
    void equalityAndRecipeValid();
};

void DefringeJsonTests::legacyAndDefaults(){
    QCOMPARE(Recipe{}.defringe,DefringeSettings());
    const auto legacy=recipeFromJson({});
    QCOMPARE(legacy.defringe,DefringeSettings());
    QVERIFY(validRecipe(legacy));
    // An absent key keeps the legacy default rather than failing.
    const auto withOther=recipeFromJson(QJsonObject{{"exposure",.5}});
    QCOMPARE(withOther.defringe,DefringeSettings());
    QVERIFY(validRecipe(withOther));
}

void DefringeJsonTests::roundTrip(){
    const DefringeSettings samples[]{
        DefringeSettings(),
        DefringeSettings{1,0,5,1,360,90,20},
        DefringeSettings{.37,123.456,42.5,.88,270.25,12.75,7.125}};
    for(const auto &s:samples){
        QVERIFY(validDefringe(s));
        QCOMPARE(recipeFromJson(recipeJson(Recipe{.defringe=s})).defringe,s);
        DefringeSettings out;
        QVERIFY(defringeFromJson(defringeJson(s),out));
        QCOMPARE(out,s);
        QCOMPARE(recipeFromJson(QJsonObject{{"defringe",defringeJson(s)}}).defringe,s);
    }
    const auto json=defringeJson(DefringeSettings());
    QCOMPARE(json.size(),7);
    for(const auto &key:{"purpleAmount","purpleHue","purpleWidth","greenAmount","greenHue","greenWidth","radius"})
        QVERIFY(json.contains(key));
}

void DefringeJsonTests::strictRejection(){
    const QJsonValue malformed[]{
        QJsonValue::Null,QJsonValue(true),QJsonValue(3.5),QJsonValue("defringe"),
        QJsonArray{},
        QJsonObject{},
        QJsonObject{{"purpleAmount",0}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",3},{"extra",0}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",true}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius","3"}},
        QJsonObject{{"purpleAmount",1.5},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",3}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",361},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",3}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",4.9},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",3}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",90.1},{"radius",3}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",.9}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",20.1}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",std::numeric_limits<double>::quiet_NaN()}},
        QJsonObject{{"purpleAmount",std::numeric_limits<double>::infinity()},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",3}}};
    for(const auto &v:malformed){
        DefringeSettings out{1,2,3,4,5,6,7};
        QVERIFY(!defringeFromJson(v,out));
        QCOMPARE(out,(DefringeSettings{1,2,3,4,5,6,7}));
        auto j=recipeJson({});j["defringe"]=v;
        QVERIFY(!validRecipe(recipeFromJson(j)));
    }
}

void DefringeJsonTests::atomicOutputUnchanged(){
    DefringeSettings out{1,2,3,4,5,6,7};
    const QJsonValue malformed[]{
        QJsonValue::Null, QJsonObject{},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",999}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45},{"radius",3},{"extra",0}},
        QJsonObject{{"purpleAmount",0},{"purpleHue",300},{"purpleWidth",50},{"greenAmount",0},{"greenHue",115},{"greenWidth",45}}};
    for(const auto &v:malformed){
        QVERIFY(!defringeFromJson(v,out));
        QCOMPARE(out,(DefringeSettings{1,2,3,4,5,6,7}));
    }
    QVERIFY(defringeFromJson(defringeJson(DefringeSettings{.5,10,10,.5,20,20,5}),out));
    QCOMPARE(out,(DefringeSettings{.5,10,10,.5,20,20,5}));
}

void DefringeJsonTests::equalityAndRecipeValid(){
    const DefringeSettings base{1,20,30,.5,40,50,10};
    QCOMPARE(base,(DefringeSettings{1,20,30,.5,40,50,10}));
    QVERIFY((base!=DefringeSettings{1,20,30,.5,40,50,11}));
    QVERIFY((base!=DefringeSettings{1,20,30,.5,40,51,10}));
    Recipe r;QVERIFY(validRecipe(r));
    r.defringe=base;QVERIFY(validRecipe(r));
    QCOMPARE(recipeJson(r)["defringe"],defringeJson(base));
    for(const auto &bad:std::initializer_list<DefringeSettings>{
        DefringeSettings{-1},DefringeSettings{1.0001},DefringeSettings{0,361},DefringeSettings{0,0,4},
        DefringeSettings{0,0,91},DefringeSettings{0,0,50,1.5},DefringeSettings{0,0,50,0,361},
        DefringeSettings{0,0,50,0,0,4},DefringeSettings{0,0,50,0,0,91},DefringeSettings{0,0,50,0,0,45,0}}){
        QVERIFY(!validDefringe(bad));
        Recipe invalid=r;invalid.defringe=bad;QVERIFY(!validRecipe(invalid));
    }
}

QTEST_MAIN(DefringeJsonTests)
#include "defringe_json_tests.moc"
