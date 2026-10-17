// tests/sha256_stream_tests.cpp
#include "io/sha256_stream.h"

#include <QCryptographicHash>
#include <QTest>
#include <QElapsedTimer>

#include <utility>

namespace {

QByteArray ref(QByteArrayView v)
{
    return QCryptographicHash::hash(v, QCryptographicHash::Sha256);
}

// Deterministic pseudo-random payload of `n` bytes (xorshift32).
QByteArray makeDeterministic(qsizetype n, quint32 seed = 0x9E3779B9u)
{
    QByteArray out(n, Qt::Uninitialized);
    quint32 x = seed ? seed : 1u;
    for (qsizetype i = 0; i < n; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        out[static_cast<int>(i)] = static_cast<char>(x & 0xFF);
    }
    return out;
}

// Feeds `data` in the given chunk sizes (cycled); a size of 0 is an empty
// chunk and must be accepted.
bool feedInChunks(Sha256Stream &s, QByteArrayView data,
                  const QList<qsizetype> &sizes)
{
    if (sizes.isEmpty())
        return false;
    qsizetype pos = 0;
    qsizetype idx = 0;
    while (pos < data.size()) {
        qsizetype want = sizes[idx % sizes.size()];
        idx++;
        if (want < 0)
            return false;
        const qsizetype n = qMin(want, data.size() - pos);
        if (!s.addData(data.sliced(pos, n)))
            return false;
        pos += n;
    }
    return true;
}

} // namespace

class Sha256StreamTests : public QObject
{
    Q_OBJECT

private slots:
    void emptyInput_canonical();
    void nulBytes_reference();
    void oneMiB_unevenChunks_withEmpty();
    void repeatedFinalization_cached();
    void addAfterFinalize_rejected();
    void moveCtor_transfersState();
    void moveAssign_transfersState();
    void qtForced_matchesReference();
    void nativeIfAvailable_matchesReference();
    void largeSingleChunk(){
        const auto bytes=makeDeterministic(16*1024*1024+17);
        for(bool native:{false,true}){Sha256Stream stream(native);QVERIFY(stream.addData(bytes));QCOMPARE(stream.result(),ref(bytes));}
    }
    void standardAbcVector(){
        const auto expected=QByteArray::fromHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        for(bool native:{false,true}){Sha256Stream stream(native);QVERIFY(stream.addData(QByteArrayView("a",1)));QVERIFY(stream.addData(QByteArrayView("bc",2)));QCOMPARE(stream.result(),expected);}
    }
    void throughputOptIn(){
        if(qEnvironmentVariableIntValue("LUMINA_STREAM_HASH_BENCHMARK")!=1)QSKIP("Explicit synthetic memory benchmark; not image loading time");
        const auto bytes=makeDeterministic(64*1024*1024);const auto expected=ref(bytes);
        for(int round=0;round<4;++round)for(int order=0;order<2;++order){
            const bool requestedNative=bool((round+order)%2);QElapsedTimer timer;timer.start();Sha256Stream stream(requestedNative);
            const auto view=QByteArrayView(bytes);for(qsizetype offset=0;offset<view.size();offset+=1024*1024)QVERIFY(stream.addData(view.sliced(offset,qMin<qsizetype>(1024*1024,view.size()-offset))));
            const auto digest=stream.result();const auto ns=timer.nsecsElapsed();QCOMPARE(digest,expected);
            qInfo()<<"streamHashBenchmark round"<<round<<"bytes"<<bytes.size()<<"requestedNative"<<requestedNative<<"usedNative"<<stream.native()<<"elapsedNs"<<ns;
        }
    }
};

void Sha256StreamTests::emptyInput_canonical()
{
    const QByteArray expected = ref(QByteArrayView());
    QCOMPARE(expected.size(), 32);

    for (bool preferNative : {false, true}) {
        Sha256Stream s(preferNative);
        QVERIFY(s.ok());
        const QByteArray got = s.result();
        QCOMPARE(got, expected);
    }
}

void Sha256StreamTests::nulBytes_reference()
{
    QByteArray data;
    data.append('\0');
    data.append("abc");
    data.append('\0');
    data.append('\0');
    data.append('z');
    const QByteArray expected = ref(QByteArrayView(data));

    for (bool preferNative : {false, true}) {
        Sha256Stream s(preferNative);
        QVERIFY(s.addData(QByteArrayView(data)));
        QCOMPARE(s.result(), expected);
    }
}

void Sha256StreamTests::oneMiB_unevenChunks_withEmpty()
{
    const QByteArray data = makeDeterministic(1024 * 1024);
    const QByteArray expected = ref(QByteArrayView(data));

    // Uneven sizes cycling through, including an empty chunk.
    const QList<qsizetype> sizes = {0, 1, 7, 4096, 13, 65536, 3, 0, 99991};

    for (bool preferNative : {false, true}) {
        Sha256Stream s(preferNative);
        QVERIFY(s.ok());
        QVERIFY(feedInChunks(s, QByteArrayView(data), sizes));
        QCOMPARE(s.result(), expected);
    }
}

void Sha256StreamTests::repeatedFinalization_cached()
{
    const QByteArray data = makeDeterministic(4096);
    for (bool preferNative : {false, true}) {
        Sha256Stream s(preferNative);
        QVERIFY(s.addData(QByteArrayView(data)));
        const QByteArray a = s.result();
        const QByteArray b = s.result();
        QCOMPARE(a, ref(QByteArrayView(data)));
        QCOMPARE(b, a);
        QVERIFY(s.ok());
    }
}

void Sha256StreamTests::addAfterFinalize_rejected()
{
    const QByteArray data = makeDeterministic(1000);
    for (bool preferNative : {false, true}) {
        Sha256Stream s(preferNative);
        QVERIFY(s.addData(QByteArrayView(data)));
        const QByteArray first = s.result();
        QVERIFY(!s.addData(QByteArrayView("x", 1)));
        QCOMPARE(s.result(), first);
        QCOMPARE(first, ref(QByteArrayView(data)));
    }
}

void Sha256StreamTests::moveCtor_transfersState()
{
    const QByteArray data = makeDeterministic(2048);
    const QByteArray expected = ref(QByteArrayView(data));

    Sha256Stream a(true);
    QVERIFY(a.addData(QByteArrayView(data.left(1024))));

    Sha256Stream b(std::move(a));
    // Moved-from: safe but inert.
    QVERIFY(!a.ok());
    QVERIFY(!a.native());
    QVERIFY(!a.addData(QByteArrayView("y", 1)));
    QCOMPARE(a.result(), QByteArray());

    QVERIFY(b.ok());
    QVERIFY(b.addData(QByteArrayView(data.mid(1024))));
    QCOMPARE(b.result(), expected);
}

void Sha256StreamTests::moveAssign_transfersState()
{
    const QByteArray data = makeDeterministic(3000);
    const QByteArray expected = ref(QByteArrayView(data));

    Sha256Stream target(true);
    QVERIFY(target.addData(QByteArrayView(data.left(1000))));

    Sha256Stream source(true);
    QVERIFY(source.addData(QByteArrayView(data.left(1000))));

    // Assign over a partially fed target; prior target state is replaced.
    target = std::move(source);
    QVERIFY(!source.ok());
    QCOMPARE(source.result(), QByteArray());
    QVERIFY(!source.addData(QByteArrayView("y", 1)));

    QVERIFY(target.ok());
    QVERIFY(target.addData(QByteArrayView(data.mid(1000))));
    QCOMPARE(target.result(), expected);
}

void Sha256StreamTests::qtForced_matchesReference()
{
    const QByteArray data = makeDeterministic(200000);
    Sha256Stream s(false);
    QVERIFY(s.ok());
    QVERIFY(!s.native());
    QVERIFY(s.addData(QByteArrayView(data)));
    QCOMPARE(s.result(), ref(QByteArrayView(data)));
}

void Sha256StreamTests::nativeIfAvailable_matchesReference()
{
    const QByteArray data = makeDeterministic(123457);
    Sha256Stream s(true);
    QVERIFY(s.ok()); // native-or-Qt; either way a live backend
    QVERIFY(s.addData(QByteArrayView(data)));
    // Digest must match Qt reference regardless of which backend ran;
    // `native()` only reports which one was used.
    QCOMPARE(s.result(), ref(QByteArrayView(data)));
}

QTEST_GUILESS_MAIN(Sha256StreamTests)
#include "sha256_stream_tests.moc"
