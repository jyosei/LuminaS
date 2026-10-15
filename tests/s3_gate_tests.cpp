#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImage>
#include <QColor>
#include <QScopeGuard>
#include "app/edit_loader.h"

// S3 A1 gate suite. These tests observe the *public* generation helpers and the real
// EditLoader delivery path; they never assert pixel content against an invented
// algorithm, only identity, path and readiness of what was actually delivered.
namespace {
constexpr int kWaitMs=90000;

QImage syntheticFrame(int width,int height,double tint){
    QImage image(width,height,QImage::Format_RGB888);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        image.setPixelColor(x,y,QColor(int(40+x*150/width),int(45+y*140/height),int(60+tint*80)));
    return image;
}

// One synthetic photo per test: small 256x192 JPEG, never a real library file.
QString writeSynthetic(const QTemporaryDir &directory,const QString &name,int width=256,int height=192,double tint=0){
    const auto path=directory.filePath(name);
    if(!syntheticFrame(width,height,tint).save(path,"JPEG",95))return {};
    return path;
}

EditTag baseTag(quint64 session){
    EditTag tag;
    tag.session=session;tag.revision=1;tag.draft=1;tag.job=1;tag.geometry=1;
    return tag;
}

QString describe(const EditTag &tag){
    return QStringLiteral("s%1 r%2 d%3 j%4 g%5 i%6 v%7 %8")
        .arg(tag.session).arg(tag.revision).arg(tag.draft).arg(tag.job).arg(tag.geometry)
        .arg(tag.localIdentity).arg(tag.viewEpoch).arg(tag.interactive?QStringLiteral("live"):QStringLiteral("still"));
}
}

class S3GateTests:public QObject {
    Q_OBJECT
private slots:
    // 1. Same-generation identity rejects each identity field the design names.
    void differentIdentityRejected(){
        auto reference=baseTag(7);reference.interactive=true;
        QVERIFY(sameEditGeneration(reference,reference));
        QVERIFY(acceptsEditTag(reference,reference));
        const QList<QPair<QString,EditTag>> variants{
            {QStringLiteral("session"),[](EditTag t){t.session+=1;return t;}(reference)},
            {QStringLiteral("revision"),[](EditTag t){t.revision+=1;return t;}(reference)},
            {QStringLiteral("geometry"),[](EditTag t){t.geometry+=1;return t;}(reference)},
            {QStringLiteral("localIdentity"),[](EditTag t){t.localIdentity+=1;return t;}(reference)},
            {QStringLiteral("viewEpoch"),[](EditTag t){t.viewEpoch+=1;return t;}(reference)},
        };
        for(const auto &variant:variants){
            const auto label=variant.first.toUtf8();
            QVERIFY2(!sameEditGeneration(variant.second,reference),label.constData());
            QVERIFY2(!acceptsEditTag(variant.second,reference),label.constData());
            QVERIFY2(!acceptsEditTag(reference,variant.second),label.constData());
        }
        // draft and job are delivery ordering, not identity.
        auto newerDraft=reference;newerDraft.draft+=5;newerDraft.job+=9;
        QVERIFY(sameEditGeneration(newerDraft,reference));
        QVERIFY(acceptsEditTag(newerDraft,reference));
    }

    // 2. Same-generation interactive feedback is accepted even when draft/job differ;
    //    an interactive/non-interactive pair is never accepted in either direction.
    void compatibleOldInteractiveAccepted(){
        auto expected=baseTag(3);
        auto olderDraft=expected;olderDraft.draft=1;olderDraft.job=1;
        expected.draft=9;expected.job=9;
        expected.interactive=true;olderDraft.interactive=true;
        QVERIFY(acceptsEditTag(olderDraft,expected));
        QVERIFY(acceptsEditTag(expected,olderDraft));

        auto stillInteractive=olderDraft;stillInteractive.interactive=false;
        QVERIFY(!acceptsEditTag(stillInteractive,expected));
        auto stillExpected=expected;stillExpected.interactive=false;
        QVERIFY(!acceptsEditTag(olderDraft,stillExpected));
        QVERIFY(!acceptsEditTag(stillInteractive,stillExpected));

        // A different path or RenderView is checked by the caller; identity alone is not enough.
        RenderView mask;mask.sharpenMask=true;
        const RenderView plain;
        QVERIFY(!(mask==plain));
    }

    // 3. Exact non-interactive matches are accepted; an older non-interactive result is not.
    void exactNonInteractiveAcceptedButOlderRejected(){
        auto expected=baseTag(11);
        QVERIFY(acceptsEditTag(expected,expected));
        QVERIFY(!expected.interactive);
        auto older=expected;--older.job;--older.draft;
        QVERIFY(!acceptsEditTag(older,expected));
        auto olderGeneration=expected;++olderGeneration.revision;
        QVERIFY(!acceptsEditTag(olderGeneration,expected));
        auto interactiveOlder=older;interactiveOlder.interactive=true;
        QVERIFY2(!acceptsEditTag(interactiveOlder,expected),describe(interactiveOlder).toUtf8().constData());
    }

    // 4. A queued old request followed by a newer local/view request before the event loop
    //    runs delivers only the latest one, and only the latest tag is sample-ready.
    void loaderDeliversOnlyLatestLocalAndViewGeneration(){
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto first=writeSynthetic(directory,QStringLiteral("gate-first.jpg"),256,192,0);
        const auto second=writeSynthetic(directory,QStringLiteral("gate-second.jpg"),256,192,1);
        QVERIFY(!first.isEmpty());QVERIFY(!second.isEmpty());
        EditLoader loader;
        QSignalSpy ready(&loader,&EditLoader::ready);
        QVERIFY(ready.isValid());

        Recipe firstRecipe;firstRecipe.exposure=.25;
        Recipe secondRecipe;secondRecipe.exposure=-.35;
        auto stale=baseTag(21);stale.localIdentity=1;stale.viewEpoch=1;
        auto latest=baseTag(21);latest.localIdentity=7;latest.viewEpoch=4;

        loader.request(first,firstRecipe,stale);
        loader.request(second,secondRecipe,latest);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,kWaitMs);
        QTest::qWait(250); // let any superseded delivery arrive before asserting

        QCOMPARE(ready.count(),1);
        const auto delivered=ready.last().at(0).value<EditResult>();
        QCOMPARE(delivered.path,second);
        QCOMPARE(delivered.tag.localIdentity,quint64(7));
        QCOMPARE(delivered.tag.viewEpoch,quint64(4));
        QVERIFY2(acceptsEditTag(delivered.tag,latest),describe(delivered.tag).toUtf8().constData());
        QVERIFY2(!sameEditGeneration(delivered.tag,stale),describe(delivered.tag).toUtf8().constData());

        QTRY_VERIFY_WITH_TIMEOUT(loader.canPickNeutral(second,secondRecipe,latest),kWaitMs);
        QVERIFY(!loader.canPickNeutral(first,firstRecipe,stale));
        QVERIFY(!loader.canPickNeutral(first,firstRecipe,latest));
        QVERIFY(!loader.canPickNeutral(second,secondRecipe,stale));
        QVERIFY(!loader.canPickNeutral(second,firstRecipe,latest));
    }

    // 5. Explicit invalidate while a request is in flight: no ready, no sample capability,
    //    and the next valid request still succeeds.
    void invalidateWhileInflightBlocksReadyAndNeutral(){
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto path=writeSynthetic(directory,QStringLiteral("gate-invalidate.jpg"),256,192,.5);
        QVERIFY(!path.isEmpty());
        EditLoader loader;
        QSignalSpy ready(&loader,&EditLoader::ready);
        QSignalSpy progress(&loader,&EditLoader::prefetchProgress);
        QVERIFY(ready.isValid()&&progress.isValid());

        Recipe recipe;recipe.exposure=.2;recipe.contrast=.1;
        const auto tag=baseTag(31);
        loader.request(path,recipe,tag);
        loader.invalidate();
        QCOMPARE(ready.count(),0);
        QTest::qWait(500);
        QCOMPARE(ready.count(),0);
        QVERIFY(!loader.canPickNeutral(path,recipe,tag));
        auto next=tag;++next.revision;++next.draft;++next.job;
        QVERIFY(!loader.canPickNeutral(path,recipe,next));

        // Invalidating must not poison the lane: a new generation still completes.
        loader.request(path,recipe,next);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,kWaitMs);
        const auto delivered=ready.last().at(0).value<EditResult>();
        QCOMPARE(delivered.path,path);
        QCOMPARE(delivered.tag.revision,quint64(2));
        QVERIFY2(acceptsEditTag(delivered.tag,next),describe(delivered.tag).toUtf8().constData());
        QTRY_VERIFY_WITH_TIMEOUT(loader.canPickNeutral(path,recipe,next),kWaitMs);
    }

    // 6. View generation A -> B -> A: the first A epoch is never the delivered identity.
    void viewEpochAbaNeverAcceptsFirstEpoch(){
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto path=writeSynthetic(directory,QStringLiteral("gate-aba.jpg"),256,192,-.4);
        QVERIFY(!path.isEmpty());
        EditLoader loader;
        QSignalSpy ready(&loader,&EditLoader::ready);
        QVERIFY(ready.isValid());

        Recipe recipe;recipe.exposure=.1;
        auto epoch1=baseTag(41);epoch1.viewEpoch=1;
        auto epoch2=epoch1;++epoch2.viewEpoch;
        auto epoch3=epoch1;epoch3.viewEpoch+=2;
        auto epoch4=epoch1;epoch4.viewEpoch+=3;
        RenderView viewA;RenderView viewB;viewB.pointRange=0;

        loader.request(path,recipe,epoch1,viewA);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,kWaitMs);
        const auto first=ready.last().at(0).value<EditResult>();
        QVERIFY2(acceptsEditTag(first.tag,epoch1),describe(first.tag).toUtf8().constData());
        QCOMPARE(first.view,viewA);
        const int afterFirst=ready.count();

        // Leave A, run B, then come back to A with a fresh epoch.
        loader.request(path,recipe,epoch2,viewB);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count()>afterFirst,kWaitMs);
        const auto second=ready.last().at(0).value<EditResult>();
        QVERIFY2(acceptsEditTag(second.tag,epoch2),describe(second.tag).toUtf8().constData());
        QCOMPARE(second.view,viewB);
        const int afterSecond=ready.count();

        loader.request(path,recipe,epoch3,viewA);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count()>afterSecond,kWaitMs);
        const auto reopened=ready.last().at(0).value<EditResult>();
        QVERIFY2(acceptsEditTag(reopened.tag,epoch3),describe(reopened.tag).toUtf8().constData());
        QCOMPARE(reopened.view,viewA);
        // The stale first-A identity must not be what the reopened A view delivers.
        QVERIFY2(!acceptsEditTag(reopened.tag,epoch1),describe(reopened.tag).toUtf8().constData());
        QVERIFY(!sameEditGeneration(reopened.tag,epoch1));

        // A fourth epoch keeps the same invariant: only the newest epoch matches.
        const int afterThird=ready.count();
        loader.request(path,recipe,epoch4,viewA);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count()>afterThird,kWaitMs);
        const auto newest=ready.last().at(0).value<EditResult>();
        QVERIFY2(acceptsEditTag(newest.tag,epoch4),describe(newest.tag).toUtf8().constData());
        QVERIFY(!acceptsEditTag(newest.tag,epoch3));
    }

    // 7. A receiver outside the loader lifetime must observe no late delivery.
    void destroyedLoaderWithQueuedRequestIsSafe(){
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto first=writeSynthetic(directory,QStringLiteral("gate-drop-a.jpg"),256,192,.2);
        const auto second=writeSynthetic(directory,QStringLiteral("gate-drop-b.jpg"),256,192,-.2);
        QVERIFY(!first.isEmpty()&&!second.isEmpty());
        QObject observer;int delivered=0;
        {
            EditLoader loader;
            QSignalSpy live(&loader,&EditLoader::ready);
            connect(&loader,&EditLoader::ready,&observer,[&]{++delivered;});
            QVERIFY(live.isValid());
            Recipe recipe;recipe.exposure=.15;
            auto stale=baseTag(51);stale.localIdentity=2;stale.viewEpoch=2;
            auto latest=baseTag(51);latest.localIdentity=3;latest.viewEpoch=5;
            loader.request(first,recipe,stale);
            loader.request(second,recipe,latest);
            loader.invalidate();
            // Queued work is dropped before it can deliver anything.
            QCOMPARE(live.count(),0);
        }
        QTest::qWait(300); // any stray delivery would be a use-after-free, not a silent pass.
        QCOMPARE(delivered,0);
    }
};

QTEST_MAIN(S3GateTests)
#include "s3_gate_tests.moc"
