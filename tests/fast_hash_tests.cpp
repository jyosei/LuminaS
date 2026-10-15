// tests/fast_hash_tests.cpp
#include <QtTest/QtTest>
#include <QByteArray>
#include <QCryptographicHash>
#include <QElapsedTimer>

#include <future>
#include <vector>

#include "../src/io/fast_hash.h"

class FastHashTests : public QObject
{
    Q_OBJECT

private slots:
    void emptyInputMatchesQt();
    void abcMatchesQt();
    void embeddedNulMatchesQt();
    void largePatternedMatchesQt();
    void sourceBufferUnchanged();
    void concurrentCallsMatchQt();
};

namespace {

QByteArray expectedSha256(QByteArrayView data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

} // namespace

void FastHashTests::emptyInputMatchesQt()
{
    const QByteArray empty;
    const QByteArray got = fastSha256(QByteArrayView(empty));
    QCOMPARE(got, expectedSha256(QByteArrayView(empty)));
    QCOMPARE(got.size(), 32);
}

void FastHashTests::abcMatchesQt()
{
    const QByteArray input = QByteArrayLiteral("abc");
    const QByteArray got = fastSha256(QByteArrayView(input));
    QCOMPARE(got, expectedSha256(QByteArrayView(input)));
}

void FastHashTests::embeddedNulMatchesQt()
{
    const QByteArray input("a\0b\0c", 5);
    const QByteArray got = fastSha256(QByteArrayView(input));
    QCOMPARE(got, expectedSha256(QByteArrayView(input)));
}

void FastHashTests::largePatternedMatchesQt()
{
    // 37 MiB > 16 MiB chunk limit, exercising the chunked loop.
    constexpr qsizetype kSize = 37 * 1024 * 1024;
    QByteArray input;
    input.resize(kSize);
    for (qsizetype i = 0; i < kSize; ++i)
        input[i] = static_cast<char>((i * 31 + 7) & 0xFF);

    const QByteArray unchanged(input.constData(),input.size());
    bool usedNative = false;QElapsedTimer nativeTimer;nativeTimer.start();
    const QByteArray got = fastSha256(QByteArrayView(input), &usedNative);
    const auto nativeNs=nativeTimer.nsecsElapsed();
    const QByteArray expected = expectedSha256(QByteArrayView(input));

#ifdef Q_OS_WIN
    QVERIFY(usedNative);
#endif

    QCOMPARE(got, expected);
    QCOMPARE(input,unchanged);

    if (input.size() >= 4 * 1024 * 1024) {
        QElapsedTimer timer;
        timer.start();
        const QByteArray ref = expectedSha256(QByteArrayView(input));
        const qint64 qtNs = timer.nsecsElapsed();
        Q_UNUSED(qtNs);
        Q_UNUSED(ref);
        qInfo() << "large fixture native vs Qt digest computed;"
                << "usedNative =" << usedNative<<"nativeMs"<<nativeNs/1e6<<"QtMs"<<qtNs/1e6;
    }
}

void FastHashTests::sourceBufferUnchanged()
{
    const QByteArray original = QByteArrayLiteral("immutable-input-\x01\x02\x03");
    QByteArray input = original;
    (void)fastSha256(QByteArrayView(input));
    QCOMPARE(input, original);
}

void FastHashTests::concurrentCallsMatchQt()
{
    std::vector<QByteArray> inputs;
    inputs.reserve(4);
    inputs.push_back(QByteArrayLiteral("alpha"));
    inputs.push_back(QByteArray("beta\0beta", 9));
    inputs.push_back(QByteArray(4096, 'x'));
    inputs.push_back(QByteArrayLiteral("delta-42"));

    std::vector<std::future<QByteArray>> futures;
    futures.reserve(inputs.size());
    for (const QByteArray &in : inputs) {
        futures.push_back(std::async(std::launch::async,
                                     [in]() {
                                         return fastSha256(QByteArrayView(in));
                                     }));
    }

    for (size_t i = 0; i < futures.size(); ++i) {
        const QByteArray got = futures[i].get();
        const QByteArray expected = expectedSha256(QByteArrayView(inputs[i]));
        QCOMPARE(got, expected);
    }
}

QTEST_MAIN(FastHashTests)
#include "fast_hash_tests.moc"
