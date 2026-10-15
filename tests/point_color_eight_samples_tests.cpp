// Draft only. Eight-sample Point Color live-window integration coverage.
// No colour-science maths, no numerical reference, no profiling gate.
#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>
#include <QFileInfo>
#include <algorithm>

#include "../src/app/window.h"
#include "inspector_navigation_support.h"
#include <QScrollArea>
#include "../src/app/canvas.h"
#include "../src/app/edit_loader.h"
#include "../src/app/point_color_panel.h"
#include "../src/core/document.h"

namespace {
constexpr int kLongEdge = 3840;
constexpr int kSampleCount = 8;
constexpr int kSteps = 20;

QString writeEightPointSheet(const QString &directory)
{
    const int width = 4000, height = 1280;
    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) return {};
    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int band = (x * kSampleCount) / width;
            const int hue = (band * 360) / kSampleCount;
            line[x] = QColor::fromHsv(hue, 220, 220).rgb();
        }
    }
    const QString path = directory + QStringLiteral("/point-eight-sheet.jpg");
    QImageWriter writer(path, "jpeg");
    writer.setQuality(92);
    if (!writer.write(image)) return {};
    return path;
}

QSlider *sliderByName(QWidget *root, const QString &name)
{
    return root->findChild<QSlider *>(name);
}

QVector<PointColorAdjustment> eightSamples()
{
    QVector<PointColorAdjustment> points;
    points.reserve(kSampleCount);
    for (int i = 0; i < kSampleCount; ++i) {
        PointColorAdjustment p;
        p.hue = i * 45.0;
        p.saturation = .5;
        p.luminance = .5;
        p.hueWidth = 60.0;
        p.saturationWidth = .4;
        p.luminanceWidth = .4;
        p.feather = .5;
        p.hueShift = .1;
        p.saturationShift = .15;
        p.luminanceShift = .05;
        points.append(p);
    }
    return points;
}
} // namespace

class PointColorEightSamplesTest : public QObject {
    Q_OBJECT

private slots:
    void eightSampleLiveEditAndHistory();
};

void PointColorEightSamplesTest::eightSampleLiveEditAndHistory()
{
    const QString external = qEnvironmentVariable("LUMINA_EIGHT_POINT_PHOTO");
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "temporary directory");

    QString source = external.isEmpty()?QString():QFileInfo(external).canonicalFilePath();
    if (!external.isEmpty()) QVERIFY(!source.isEmpty());
    QString externalDigest;
    bool externalUsed = false;
    if (!source.isEmpty() && QFile::exists(source)) {
        externalUsed = true;
        QFile file(source);
        QVERIFY2(file.open(QIODevice::ReadOnly), "external source readable");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&file);
        externalDigest = QString::fromLatin1(hash.result().toHex());
    } else {
        source = writeEightPointSheet(dir.path());
    }
    QVERIFY2(!source.isEmpty() && QFile::exists(source), "source image present");
    QFile sourceFile(source);QVERIFY(sourceFile.open(QIODevice::ReadOnly));QCryptographicHash sourceHash(QCryptographicHash::Sha256);sourceHash.addData(&sourceFile);const auto originalHash=sourceHash.result();sourceFile.close();

    Window window(nullptr, QStringLiteral(""));
    window.show();
    QVERIFY(openAllInspectorSections(window));
    QVERIFY2(window.findChild<QWidget *>(QStringLiteral("point-color-panel")),
             "point-color panel exists");

    QSignalSpy startup(window.findChild<EditLoader *>(), &EditLoader::ready);
    QVERIFY(startup.isValid());
    window.addFiles(QStringList{source});
    QTRY_COMPARE_WITH_TIMEOUT(window.currentIndex(), 0, 90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(), 90000);

    Recipe recipe = window.currentRecipe();
    recipe.pointColors = eightSamples();
    QVERIFY2(validRecipe(recipe), "eight-sample recipe validates");

    window.applyRecipe(recipe, QStringLiteral("eight samples"));
    QTRY_VERIFY_WITH_TIMEOUT(window.currentRecipe().pointColors.size() == kSampleCount, 90000);
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(), 90000);

    const Recipe applied = window.currentRecipe();
    QCOMPARE(applied.pointColors.size(), kSampleCount);

    auto *panel = window.findChild<PointColorPanel *>(QStringLiteral("point-color-panel"));
    QVERIFY2(panel, "point-color panel resolves");
    panel->setPoints(applied.pointColors, 0);
    QCOMPARE(panel->selectedIndex(), 0);

    auto *hueSlider = sliderByName(&window, QStringLiteral("slider-point-color-hue-shift"));
    QVERIFY2(hueSlider && hueSlider->isEnabled(),
             "hue-shift slider present and enabled for sample 0");
    auto *scroll=window.findChild<QScrollArea *>();QVERIFY(scroll);scroll->ensureWidgetVisible(hueSlider);QTest::qWait(50);QVERIFY(hueSlider->visibleRegion().contains(hueSlider->rect()));

    QSignalSpy previews(&window, &Window::previewChanged);
    QSignalSpy paintings(window.findChild<Canvas *>(), &Canvas::framePainted);
    QSignalSpy editorReady(window.findChild<EditLoader *>(), &EditLoader::ready);
    QVERIFY(previews.isValid());
    QVERIFY(paintings.isValid());
    QVERIFY(editorReady.isValid());

    const int previewsBefore = previews.count();
    const int paintingsBefore = paintings.count();

    // Observed slider scale is 100 for hue shift; drag 10 -> 30 over ~2s.
    hueSlider->setSliderDown(true);
    hueSlider->setValue(10);
    QTest::qWait(100);
    double lastRenderMs = -1;
    QSize lastImageSize;
    for (int step = 1; step <= kSteps; ++step) {
        const int value = 10 + (20 * step) / kSteps;
        hueSlider->setValue(value);
        QTest::qWait(100);
        if (!editorReady.isEmpty()) {
            const QVariant v = editorReady.last().at(0);
            const EditResult result = qvariant_cast<EditResult>(v);
            if (!result.image.isNull()) {
                lastImageSize = result.image.size();
                lastRenderMs = result.elapsedMs;
            }
        }
    }
    hueSlider->setSliderDown(false);

    const int previewsDuring = previews.count() - previewsBefore;
    const int paintingsDuring = paintings.count() - paintingsBefore;
    qInfo() << "eight-sample drag previews:" << previewsDuring
            << "paints:" << paintingsDuring
            << "last image size:" << lastImageSize
            << "renderMs:" << lastRenderMs;

    QVERIFY2(previewsDuring >= 2,
             qPrintable(QStringLiteral("interim previews %1").arg(previewsDuring)));
    QVERIFY2(paintingsDuring >= 2,
             qPrintable(QStringLiteral("interim paints %1").arg(paintingsDuring)));
    QVERIFY(lastImageSize.isValid());
    QCOMPARE(std::max(lastImageSize.width(), lastImageSize.height()), kLongEdge);

    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(), 90000);
    const Recipe edited = window.currentRecipe();
    auto expected=applied;expected.pointColors[0].hueShift=.3;QCOMPARE(edited,expected);
    QCOMPARE(edited.pointColors.size(), kSampleCount);
    QCOMPARE(edited.pointColors[0].hueShift, .3);
    for (int i = 1; i < kSampleCount; ++i) {
        QCOMPARE(edited.pointColors[i], applied.pointColors[i]);
    }
    QCOMPARE(edited.pointColors[0].hue, applied.pointColors[0].hue);
    QCOMPARE(edited.pointColors[0].saturation, applied.pointColors[0].saturation);
    QCOMPARE(edited.pointColors[0].luminance, applied.pointColors[0].luminance);
    QCOMPARE(edited.pointColors[0].hueWidth, applied.pointColors[0].hueWidth);
    QCOMPARE(edited.pointColors[0].saturationWidth, applied.pointColors[0].saturationWidth);
    QCOMPARE(edited.pointColors[0].luminanceWidth, applied.pointColors[0].luminanceWidth);
    QCOMPARE(edited.pointColors[0].feather, applied.pointColors[0].feather);
    QCOMPARE(edited.pointColors[0].saturationShift, applied.pointColors[0].saturationShift);
    QCOMPARE(edited.pointColors[0].luminanceShift, applied.pointColors[0].luminanceShift);

    window.undoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(), 90000);
    QCOMPARE(window.currentRecipe(), applied);

    window.redoEdit();
    QTRY_VERIFY_WITH_TIMEOUT(window.neutralPickerAvailable(), 90000);
    QCOMPARE(window.currentRecipe(), edited);

    if (externalUsed) {
        QFile file(source);
        QVERIFY2(file.open(QIODevice::ReadOnly), "external source still readable after test");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&file);
        QCOMPARE(QString::fromLatin1(hash.result().toHex()), externalDigest);
    } else {
        // Generated fixture: assert the file is unchanged from what we wrote.
        QFile file(source);QVERIFY(file.open(QIODevice::ReadOnly));QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(&file);QCOMPARE(hash.result(),originalHash);
    }

}

QTEST_MAIN(PointColorEightSamplesTest)
#include "point_color_eight_samples_tests.moc"
