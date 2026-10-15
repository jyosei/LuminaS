#include <QtTest>
#include <QtGui/qtestsupport_gui.h>
#include <QColorSpace>
#include <QSignalSpy>
#include <QThread>
#include <atomic>
#include <lcms2.h>
#include "app/canvas.h"
#include "app/display_presenter.h"
// Bounded presentation-plumbing tests. Every image is synthetic (1x1 / 64x32 RGB)
// and every profile is generated in-process, so no photo, screenshot or system
// colour data is involved. The tests drive epochs, queue invalidation and the
// manual override; they never assert a colour formula (that stays in the colour
// transform tests).
namespace {
QByteArray srgbIccBytes()
{
    cmsHPROFILE profile = cmsCreate_sRGBProfile();
    if (!profile) return {};
    cmsUInt32Number size = 0;
    cmsSaveProfileToMem(profile, nullptr, &size);
    QByteArray bytes(static_cast<int>(size), '\0');
    cmsSaveProfileToMem(profile, bytes.data(), &size);
    cmsCloseProfile(profile);
    bytes.resize(static_cast<int>(size));
    return bytes;
}
QByteArray generatedRgbIcc()
{
    // Generated from parameters only: a simple gamma-2.2 RGB profile. No monitor
    // file is read and no OS query is issued anywhere in this test.
    cmsToneCurve *curve = cmsBuildGamma(nullptr, 2.2);
    if (!curve) return {};
    cmsToneCurve *curves[3] = {curve, curve, curve};
    cmsCIExyY white{0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE primaries{{0.64, 0.33, 1.0}, {0.30, 0.60, 1.0}, {0.15, 0.06, 1.0}};
    cmsHPROFILE profile = cmsCreateRGBProfile(&white, &primaries, curves);
    cmsFreeToneCurve(curve);
    if (!profile) return {};
    cmsUInt32Number size = 0;
    cmsSaveProfileToMem(profile, nullptr, &size);
    QByteArray bytes(static_cast<int>(size), '\0');
    cmsSaveProfileToMem(profile, bytes.data(), &size);
    cmsCloseProfile(profile);
    bytes.resize(static_cast<int>(size));
    return bytes;
}
QImage syntheticImage(const QSize &size = QSize(64, 32))
{
    QImage image(size, QImage::Format_RGB888);
    image.fill(QColor(96, 128, 160));
    return image;
}
// Identity conversion so the plumbing is observable without guessing colour maths.
DisplayTransformResult echo(const QImage &source, const QByteArray &, const std::shared_ptr<std::atomic<bool>> &)
{
    DisplayTransformResult result;
    result.image = source;
    return result;
}
DisplayTransformResult alwaysFail(const QImage &, const QByteArray &, const std::shared_ptr<std::atomic<bool>> &)
{
    DisplayTransformResult result;
    result.error = QStringLiteral("synthetic transform failure");
    return result;
}
bool waitReady(QSignalSpy &spy, int count = 1, int timeoutMs = 15000)
{
    if (spy.size() >= count) return true;
    return spy.wait(timeoutMs) && spy.size() >= count;
}
QImage readyImage(const QSignalSpy &spy, int index)
{
    return spy.at(index).at(0).value<QImage>();
}
} // namespace

class DisplayPresenterTests : public QObject {
    Q_OBJECT
private slots:
    void canonicalIdentityDoesNotRewriteBytes();
    void newContextInvalidatesObsoleteFrame();
    void sameContextKeepsLatestRequest();
    void clearDropsCanonicalAndEpoch();
    void manualOverrideChangesProfileAndEmitsFreshFrame();
    void manualOverrideLeavesCanonicalUntouched();
    void manualOverrideDoesNotTouchRecipe();
    void switchingContextMakesOldFrameObsolete();
    void taggedInputReportsFailureInsteadOfFakingConversion();
    void plainSrgbFallsBackToCanonicalOnFailure();
    void canvasUsesPresentedImageForPainting();
    void canvasMessageClearsPresentation();
    void canvasPaintsOnlyWithAvailableFrame();
    void canvasPaintsActualMonitorPixels();
};

void DisplayPresenterTests::canonicalIdentityDoesNotRewriteBytes()
{
    DisplayPresenter presenter(echo, nullptr);
    QSignalSpy ready(&presenter, &DisplayPresenter::imageReady);
    const QImage source = syntheticImage();
    const QColor before = source.pixelColor(0, 0);
    presenter.setImage(source, QStringLiteral("ctx-a"));
    QVERIFY(waitReady(ready));
    QCOMPARE(presenter.canonicalImage().pixelColor(0, 0), before);
    QCOMPARE(presenter.canonicalImage().size(), source.size());
    QCOMPARE(readyImage(ready, 0).size(), source.size());
}

void DisplayPresenterTests::newContextInvalidatesObsoleteFrame()
{
    DisplayPresenter presenter(alwaysFail, nullptr);
    QSignalSpy ready(&presenter, &DisplayPresenter::imageReady);
    QSignalSpy cleared(&presenter, &DisplayPresenter::presentationCleared);
    presenter.setImage(syntheticImage(), QStringLiteral("ctx-a"));
    const quint64 firstEpoch = presenter.epoch();
    const int beforeChange=cleared.count();
    presenter.setImage(syntheticImage(), QStringLiteral("ctx-b"));
    QCOMPARE(cleared.count(),beforeChange+1);
    QVERIFY(presenter.epoch() > firstEpoch);
    QVERIFY(waitReady(ready));                          // failure of a plain sRGB canonical
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.at(0).at(0).value<QImage>().size(), QSize(64, 32));
    QVERIFY(ready.at(0).at(1).toString().contains(QStringLiteral("synthetic transform failure")));
    QVERIFY(ready.at(0).at(1).toString().contains(QStringLiteral("sRGB")));
}

void DisplayPresenterTests::sameContextKeepsLatestRequest()
{
    DisplayPresenter presenter(echo, nullptr);
    QSignalSpy ready(&presenter, &DisplayPresenter::imageReady);
    QSignalSpy cleared(&presenter, &DisplayPresenter::presentationCleared);
    presenter.setImage(syntheticImage(), QStringLiteral("ctx-live"));
    const quint64 epoch = presenter.epoch();
    const int beforeUpdate=cleared.count();
    presenter.setImage(syntheticImage(),QStringLiteral("ctx-live"));
    QCOMPARE(cleared.count(),beforeUpdate);
    QCOMPARE(presenter.epoch(), epoch);
    QVERIFY(waitReady(ready));
    QCOMPARE(readyImage(ready, 0).size(), QSize(64, 32));   // the first frame is the earlier request
    QVERIFY(ready.count() <= 2);
}

void DisplayPresenterTests::clearDropsCanonicalAndEpoch()
{
    DisplayPresenter presenter(echo, nullptr);
    QSignalSpy cleared(&presenter, &DisplayPresenter::presentationCleared);
    presenter.setImage(syntheticImage(), QStringLiteral("ctx-a"));
    const quint64 epoch = presenter.epoch();
    const int beforeClear=cleared.count();
    presenter.clear();
    QVERIFY(presenter.canonicalImage().isNull());
    QVERIFY(presenter.contentContext().isEmpty());
    QVERIFY(presenter.epoch() > epoch);
    QCOMPARE(cleared.count(),beforeClear+1);
}

void DisplayPresenterTests::manualOverrideChangesProfileAndEmitsFreshFrame()
{
    const QByteArray icc = generatedRgbIcc();
    QVERIFY(!icc.isEmpty());
    DisplayPresenter presenter(echo, nullptr);
    QSignalSpy ready(&presenter, &DisplayPresenter::imageReady);
    presenter.setImage(syntheticImage(), QStringLiteral("ctx-a"));
    QVERIFY(waitReady(ready));
    const quint64 epoch = presenter.epoch();
    presenter.setMonitorProfile(icc, QStringLiteral("QA 手动覆盖"));
    QVERIFY(presenter.manualProfileActive());
    QCOMPARE(presenter.monitorIcc(), icc);
    QCOMPARE(presenter.profileNote(), QStringLiteral("QA 手动覆盖"));
    QVERIFY(presenter.epoch() > epoch);                 // a fresh epoch, so the old frame is stale
    QVERIFY(waitReady(ready, 2));
    QCOMPARE(presenter.canonicalImage().size(), QSize(64, 32));
    QVERIFY(presenter.displaySnapshot().contains(QStringLiteral("手动")));
}

void DisplayPresenterTests::manualOverrideLeavesCanonicalUntouched()
{
    const QByteArray icc = generatedRgbIcc();
    QVERIFY(!icc.isEmpty());
    DisplayPresenter presenter(echo, nullptr);
    QSignalSpy ready(&presenter, &DisplayPresenter::imageReady);
    const QImage source = syntheticImage();
    const QColor expected = source.pixelColor(3, 4);
    presenter.setImage(source, QStringLiteral("ctx-a"));
    QVERIFY(waitReady(ready));
    const QImage canonicalBefore = presenter.canonicalImage().copy();
    presenter.setMonitorProfile(icc);
    presenter.useSystemMonitorProfile();
    QVERIFY(!presenter.manualProfileActive());
    const QImage canonicalAfter = presenter.canonicalImage();
    QCOMPARE(canonicalAfter.size(), canonicalBefore.size());
    QCOMPARE(canonicalAfter.pixelColor(3, 4), expected);
    QCOMPARE(canonicalAfter.colorSpace().isValid(), canonicalBefore.colorSpace().isValid());
}

void DisplayPresenterTests::manualOverrideDoesNotTouchRecipe()
{
    // The presenter exposes no Recipe/export surface at all: the override only
    // changes presentation bytes, and the canonical image keeps its own tag.
    const QByteArray icc = generatedRgbIcc();
    QVERIFY(!icc.isEmpty());
    DisplayPresenter presenter(echo, nullptr);
    QSignalSpy ready(&presenter, &DisplayPresenter::imageReady);
    QImage source = syntheticImage();
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    presenter.setImage(source, QStringLiteral("ctx-a"));
    QVERIFY(waitReady(ready));
    presenter.setMonitorProfile(icc);
    QCOMPARE(presenter.canonicalImage().colorSpace(), QColorSpace(QColorSpace::SRgb));
}

void DisplayPresenterTests::switchingContextMakesOldFrameObsolete()
{
    DisplayPresenter presenter(echo, nullptr);
    QSignalSpy ready(&presenter, &DisplayPresenter::imageReady);
    presenter.setImage(syntheticImage(), QStringLiteral("photo-1"));
    presenter.setImage(syntheticImage(QSize(200, 100)), QStringLiteral("photo-2"));
    QVERIFY(waitReady(ready));
    QCOMPARE(presenter.contentContext(), QStringLiteral("photo-2"));
    QCOMPARE(presenter.canonicalImage().size(), QSize(200, 100));
    for (int i = 0; i < ready.count(); ++i)
        QCOMPARE(readyImage(ready, i).size(), QSize(200, 100)); // no frame of the old context
}

void DisplayPresenterTests::taggedInputReportsFailureInsteadOfFakingConversion()
{
    DisplayPresenter presenter(alwaysFail, nullptr);
    QSignalSpy ready(&presenter, &DisplayPresenter::imageReady);
    QImage tagged = syntheticImage();
    const QByteArray icc = srgbIccBytes();
    QVERIFY(!icc.isEmpty());
    tagged.setColorSpace(QColorSpace::AdobeRgb);
    QVERIFY(tagged.colorSpace().isValid());
    presenter.setImage(tagged, QStringLiteral("tagged"));
    QVERIFY(waitReady(ready));
    // Conversion of tagged input failed, so no converted frame and no sRGB fake.
    QVERIFY(readyImage(ready, 0).isNull());
    QCOMPARE(ready.at(0).at(1).toString(), QStringLiteral("synthetic transform failure"));
    QVERIFY(presenter.canonicalImage().colorSpace().isValid());
}

void DisplayPresenterTests::plainSrgbFallsBackToCanonicalOnFailure()
{
    DisplayPresenter presenter(alwaysFail, nullptr);
    QSignalSpy ready(&presenter, &DisplayPresenter::imageReady);
    QImage plain = syntheticImage();
    QVERIFY(!plain.colorSpace().isValid());
    const QColor expected = plain.pixelColor(0, 0);
    presenter.setImage(plain, QStringLiteral("plain"));
    QVERIFY(waitReady(ready));
    const QImage fallback = readyImage(ready, 0);
    QCOMPARE(fallback.size(), plain.size());
    QCOMPARE(fallback.pixelColor(0, 0), expected);
    QCOMPARE(fallback.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(ready.at(0).at(1).toString().contains(QStringLiteral("sRGB")));
}

void DisplayPresenterTests::canvasUsesPresentedImageForPainting()
{
    Canvas canvas;
    canvas.resize(400, 300);
    QImage canonical = syntheticImage();
    canonical.fill(QColor(10, 20, 30));
    canvas.setImage(canonical, QStringLiteral("ctx"));
    QVERIFY(canvas.presenter());
    canvas.presenter()->setMonitorProfile(srgbIccBytes(), QStringLiteral("QA"));
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    QSignalSpy painted(&canvas, &Canvas::framePainted);
    QVERIFY(waitReady(painted));
    const QImage grab = canvas.grab().toImage();
    QVERIFY(!grab.isNull());
    QVERIFY(grab.size().isValid());
}

void DisplayPresenterTests::canvasMessageClearsPresentation()
{
    Canvas canvas;
    canvas.resize(400, 300);
    canvas.setImage(syntheticImage(), QStringLiteral("ctx"));
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    canvas.setMessage(QStringLiteral("正在读取照片…"));
    QVERIFY(canvas.presenter()->canonicalImage().isNull());
    QVERIFY(!canvas.displaySnapshot().isEmpty());
}

void DisplayPresenterTests::canvasPaintsOnlyWithAvailableFrame()
{
    // Accepting a request is not a painted frame; only the asynchronous result is.
    Canvas canvas;
    canvas.resize(400, 300);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    QSignalSpy painted(&canvas, &Canvas::framePainted);
    QImage canonical = syntheticImage();
    canvas.presenter()->setMonitorProfile(QByteArray(), QStringLiteral("empty override"));
    canvas.setImage(canonical,QStringLiteral("fresh"));
    QImage captured(canvas.size(),QImage::Format_ARGB32);canvas.render(&captured);
    QCOMPARE(painted.count(), 0);
    QTRY_VERIFY(painted.count()>0);
}

void DisplayPresenterTests::canvasPaintsActualMonitorPixels(){
    Canvas canvas;canvas.resize(400,300);const auto profile=generatedRgbIcc();
    canvas.setMonitorProfile(profile,"synthetic SDR");canvas.show();QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    QImage source(64,32,QImage::Format_RGBA8888);source.fill(QColor(140,95,60));source.setColorSpace(QColorSpace::SRgb);
    const auto before=source;const auto expected=transformDisplay(source,profile);QVERIFY(expected.error.isEmpty());
    QSignalSpy painted(&canvas,&Canvas::framePainted);canvas.setImage(source,"monitor-pixels");QTRY_VERIFY(painted.count()>0);
    auto centre=[&]{const auto image=canvas.grab().toImage();return image.pixelColor(image.width()/2,image.height()/2);};
    const auto actual=centre(),reference=expected.image.pixelColor(0,0);
    QVERIFY(std::abs(actual.red()-reference.red())<=1);QVERIFY(std::abs(actual.green()-reference.green())<=1);QVERIFY(std::abs(actual.blue()-reference.blue())<=1);
    const int count=painted.count();canvas.setMonitorProfile({},"sRGB fallback");QTRY_VERIFY(painted.count()>count);
    QCOMPARE(centre(),source.pixelColor(0,0));QCOMPARE(source,before);
}
QTEST_MAIN(DisplayPresenterTests)
#include "display_presenter_tests.moc"
