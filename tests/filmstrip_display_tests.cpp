#include "app/ui_primitives.h"
#include "color/display_transform.h"
#include <QtTest>
#include <QColorSpace>
#include <QImage>
#include <QListWidget>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QWidget>
#include <lcms2.h>
#include <algorithm>

namespace {

// Synthetic stand-in for the filmstrip: one QListWidget with the same delegate
// installation path Window uses. No files, images, screenshots or model data.
struct Strip {
    QListWidget *view=nullptr;
    QListWidgetItem *item=nullptr;
    QIcon source;
};
Strip buildStrip(const QIcon &source,int count=1){
    Strip strip;strip.view=new QListWidget;
    strip.view->setObjectName(QStringLiteral("filmstrip"));
    strip.view->setViewMode(QListView::IconMode);
    strip.view->setIconSize({64,48});strip.view->setGridSize({80,72});
    strip.view->resize(400,120);
    Ui::installFilmstripDelegate(strip.view);
    for(int i=0;i<count;++i){auto *item=new QListWidgetItem(source,QString(),strip.view);item->setSizeHint({80,72});if(!i)strip.item=item;}
    strip.source=source;strip.view->show();QTest::qWait(10);return strip;
}
QByteArray syntheticRgbProfile(){
    cmsCIExyY white{.3127,.3290,1};cmsCIExyYTRIPLE primaries{{.64,.33,1},{.21,.71,1},{.15,.06,1}};
    auto *gamma=cmsBuildGamma(nullptr,2.2);cmsToneCurve *curves[]{gamma,gamma,gamma};
    auto profile=cmsCreateRGBProfile(&white,&primaries,curves);cmsFreeToneCurve(gamma);
    cmsUInt32Number length=0;cmsSaveProfileToMem(profile,nullptr,&length);QByteArray bytes(length,Qt::Uninitialized);
    cmsSaveProfileToMem(profile,bytes.data(),&length);cmsCloseProfile(profile);return bytes;
}
QImage solid(int width,int height,int r,int g,int b){
    QImage image(width,height,QImage::Format_RGBA8888);image.fill(QColor(r,g,b,255));image.setColorSpace(QColorSpace::SRgb);return image;
}
QIcon solidIcon(int r,int g,int b){return QIcon(QPixmap::fromImage(solid(64,48,r,g,b)));}
QImage renderCard(QListWidget *view,QListWidgetItem *item){
    QImage target(80,72,QImage::Format_RGBA8888);target.fill(Qt::black);
    QPainter painter(&target);QStyleOptionViewItem option;option.initFrom(view);option.rect=QRect(0,0,80,72);
    view->itemDelegate()->paint(&painter,option,view->indexFromItem(item));painter.end();return target;
}
int packed(const QColor &color){return (color.red()<<16)|(color.green()<<8)|color.blue();}
int centerPixel(const QImage &image){return packed(image.pixelColor(40,20));}
// Independent expected value: transformDisplay over the same small pixels the
// delegate feeds it, never a re-implementation of the colour maths.
int expectedCenter(const QImage &source,const QSize &size,const QByteArray &profile){
    const auto pixels=source.scaled(size,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    const auto converted=transformDisplay(pixels,profile);
    const auto result=converted.error.isEmpty()&&!converted.image.isNull()?converted.image:pixels;
    return packed(result.pixelColor(result.width()/2,result.height()/2));
}

}
class FilmstripDisplayTests:public QObject {
    Q_OBJECT
private slots:
    void helperConvertsThroughDelegateAndMatchesTransformDisplay();
    void profileChangeInvalidatesCachedThumbnail();
    void originalIconAndStoredRolesAreUnchanged();
    void invalidProfileFallsBackWithoutManagedClaim();
    void cacheStaysBounded();
};
void FilmstripDisplayTests::helperConvertsThroughDelegateAndMatchesTransformDisplay(){
    const auto profile=syntheticRgbProfile();const auto source=solidIcon(230,90,40);
    auto strip=buildStrip(source);
    const QSize size(64,48);
    QCOMPARE(Ui::filmstripDisplayImage(strip.view,source,size).cacheKey(),source.cacheKey());
    Ui::setFilmstripDisplayProfile(strip.view,profile);
    const auto actual=Ui::filmstripDisplayImage(strip.view,source,size);
    QVERIFY(!actual.isNull());QVERIFY(actual.cacheKey()!=source.cacheKey());
    const auto reference=transformDisplay(solid(64,48,230,90,40),profile);
    QVERIFY2(reference.error.isEmpty(),qPrintable(reference.error));
    const auto pixels=actual.pixmap(size).toImage();
    QCOMPARE(pixels.size(),size);
    QVERIFY(pixels!=solid(64,48,230,90,40));
    for(int x=0;x<size.width();++x){
        const auto a=pixels.pixelColor(x,size.height()/2),b=reference.image.pixelColor(x,size.height()/2);
        QCOMPARE(a.red(),b.red());QCOMPARE(a.green(),b.green());QCOMPARE(a.blue(),b.blue());
    }
    // The delegate also reaches the same conversion path used by Window's paint.
    const auto painted=renderCard(strip.view,strip.item);
    QVERIFY(packed(painted.pixelColor(40,20))!=packed(QColor(230,90,40)));
    QCOMPARE(centerPixel(painted),centerPixel(reference.image));
    delete strip.view;
}
void FilmstripDisplayTests::profileChangeInvalidatesCachedThumbnail(){
    const auto first=syntheticRgbProfile();auto second=first;second.append(' ');
    const auto source=solidIcon(200,120,60);auto strip=buildStrip(source);
    const QSize size(64,48);
    Ui::setFilmstripDisplayProfile(strip.view,first);
    const auto initial=Ui::filmstripDisplayImage(strip.view,source,size);
    QVERIFY(!initial.isNull());
    QCOMPARE(Ui::filmstripDisplayImage(strip.view,source,size).cacheKey(),initial.cacheKey());
    Ui::setFilmstripDisplayProfile(strip.view,second);
    const auto reconverted=Ui::filmstripDisplayImage(strip.view,source,size);
    QCOMPARE(reconverted.pixmap(size).toImage(),initial.pixmap(size).toImage());
    QVERIFY(reconverted.cacheKey()!=initial.cacheKey());
    Ui::setFilmstripDisplayProfile(strip.view,QByteArray());
    QCOMPARE(Ui::filmstripDisplayImage(strip.view,source,size).cacheKey(),source.cacheKey());
    delete strip.view;
}
void FilmstripDisplayTests::originalIconAndStoredRolesAreUnchanged(){
    const auto profile=syntheticRgbProfile();const auto source=solidIcon(180,140,100);auto strip=buildStrip(source);
    auto *item=strip.item;const auto before=item->icon();
    item->setData(Qt::UserRole+1,true);item->setData(Qt::UserRole+2,1);item->setData(Qt::UserRole+3,2);
    item->setData(Qt::UserRole+4,QStringLiteral("synthetic.png"));
    const QList<int> keys={Qt::DecorationRole,Qt::UserRole+1,Qt::UserRole+2,Qt::UserRole+3,Qt::UserRole+4};
    QList<QVariant> values;for(auto key:keys)values.append(item->data(key));
    Ui::setFilmstripDisplayProfile(strip.view,profile);
    Ui::filmstripDisplayImage(strip.view,source,{64,48});
    renderCard(strip.view,item);
    int i=0;for(auto key:keys){const auto expected=values.at(i++);if(key==Qt::DecorationRole)QCOMPARE(item->data(key).value<QIcon>().cacheKey(),expected.value<QIcon>().cacheKey());else QCOMPARE(item->data(key),expected);}
    QCOMPARE(item->icon().cacheKey(),before.cacheKey());
    QCOMPARE(item->data(Qt::DecorationRole).value<QIcon>().pixmap(64,48).toImage().convertToFormat(QImage::Format_RGBA8888),solid(64,48,180,140,100));
    delete strip.view;
}
void FilmstripDisplayTests::invalidProfileFallsBackWithoutManagedClaim(){
    const auto source=solidIcon(120,90,60);auto strip=buildStrip(source);
    Ui::setFilmstripDisplayProfile(strip.view,QByteArray("invalid ICC"));
    const auto fallback=Ui::filmstripDisplayImage(strip.view,source,{64,48});
    QCOMPARE(fallback.pixmap(64,48).toImage().convertToFormat(QImage::Format_RGBA8888),solid(64,48,120,90,60));
    QCOMPARE(centerPixel(renderCard(strip.view,strip.item)),centerPixel(solid(64,48,120,90,60)));
    delete strip.view;
}
void FilmstripDisplayTests::cacheStaysBounded(){
    const auto profile=syntheticRgbProfile();const auto source=solidIcon(160,110,70);auto strip=buildStrip(source,80);
    Ui::setFilmstripDisplayProfile(strip.view,profile);
    for(int i=0;i<80;++i)Ui::filmstripDisplayImage(strip.view,strip.view->item(i)->icon(),{64,48});
    for(int i=0;i<80;++i){
        const auto expected=expectedCenter(strip.view->item(i)->icon().pixmap({64,48}).toImage(),{64,48},profile);
        const auto converted=Ui::filmstripDisplayImage(strip.view,strip.view->item(i)->icon(),{64,48});
        const auto color=converted.pixmap({64,48}).toImage().pixelColor(32,24);
        QCOMPARE((color.red()<<16)|(color.green()<<8)|color.blue(),expected);
    }
    delete strip.view;
}
QTEST_MAIN(FilmstripDisplayTests)
#include "filmstrip_display_tests.moc"
