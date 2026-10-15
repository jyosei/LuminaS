#include "app/adjustment_navigation.h"
#include "app/ui_primitives.h"
#include "app/window.h"
#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// Synthetic stand-in for Window's adjustment panel: the same Ui::card structure,
// a few value controls, and one plain non-card child before the cards.
struct Panel {
    QDockWidget *dock=nullptr;
    QScrollArea *scroll=nullptr;
    QWidget *content=nullptr;
    QList<QFrame *> cards;
    QList<QToolButton *> headers;
    QList<QDoubleSpinBox *> spins;
    QList<QComboBox *> combos;
};

Panel buildPanel(int cardCount,int viewportHeight=140){
    Panel panel;
    panel.dock=new QDockWidget(QStringLiteral("调整"));
    panel.dock->setObjectName(QStringLiteral("adjustment-dock"));
    panel.content=new QWidget;
    panel.content->setObjectName(QStringLiteral("adjustment-panel"));
    panel.content->setMinimumWidth(300);
    auto *sections=new QVBoxLayout(panel.content);
    sections->setContentsMargins(8,4,8,8);
    sections->setSpacing(9);
    auto *plain=new QLabel(QStringLiteral("头部提示"));plain->setObjectName("panel-hint");sections->addWidget(plain);
    QFormLayout *form=nullptr;
    for(int i=0;i<cardCount;++i){
        auto *card=Ui::card(QStringLiteral("分类%1").arg(i+1),QString(),Ui::Glyph::Light,form);
        sections->addWidget(card);
        panel.cards.append(card);
        panel.headers.append(card->findChild<QToolButton *>(QStringLiteral("card-header")));
        auto *spin=new QDoubleSpinBox;spin->setObjectName(QStringLiteral("value-%1").arg(i+1));spin->setRange(-1,1);spin->setDecimals(2);spin->setValue(.25);form->addRow(QStringLiteral("值 %1").arg(i+1),spin);
        auto *combo=new QComboBox;combo->setObjectName(QStringLiteral("choice-%1").arg(i+1));combo->addItems({QStringLiteral("甲"),QStringLiteral("乙")});form->addRow(QStringLiteral("选择 %1").arg(i+1),combo);
        panel.spins.append(spin);panel.combos.append(combo);
    }
    sections->addStretch();
    panel.scroll=new QScrollArea;panel.scroll->setWidgetResizable(true);panel.scroll->setWidget(panel.content);
    panel.dock->setWidget(panel.scroll);
    panel.dock->setMinimumSize(340,viewportHeight);
    panel.dock->resize(350,viewportHeight);
    Ui::installAdjustmentNavigation(panel.dock,panel.scroll);
    panel.dock->show();
    QTest::qWait(10);
    return panel;
}

QToolButton *header(QFrame *card){return card->findChild<QToolButton *>(QStringLiteral("card-header"));}
QComboBox *navigationChoice(QDockWidget *dock){return dock->findChild<QComboBox *>(QStringLiteral("adjustment-section-choice"));}
QWidget *navigationBar(QDockWidget *dock){return dock->findChild<QWidget *>(QStringLiteral("adjustment-navigation"));}

} // namespace

class AdjustmentNavigationTests:public QObject {
    Q_OBJECT
private slots:
    void installsWrapperAboveScrollAndKeepsContent();
    void comboEntriesFollowCardOrderAndTitles();
    void selectingCollapsedTargetExpandsAndScrollsToHeader();
    void selectingCardAlignsTargetNearViewportTop();
    void selectionLeavesOtherCollapseStatesAlone();
    void expandAllAndCollapseAllOnlyTouchHeaders();
    void keyboardSelectionActivatesCard();
    void navigationWorksWhileContentDisabled();
    void navigationNeverChangesControlValues();
    void destroyingWidgetsIsSafe();
    void nativeWindowNavigation();
};

void AdjustmentNavigationTests::installsWrapperAboveScrollAndKeepsContent(){
    Panel panel=buildPanel(3);
    QWidget *bar=navigationBar(panel.dock);
    QVERIFY(bar);
    QCOMPARE(panel.dock->widget(),bar);
    QCOMPARE(panel.scroll->parentWidget(),bar);
    // wrapper identity, ordering and non-destructive reuse of the original scroll area
    QCOMPARE(panel.content->objectName(),QStringLiteral("adjustment-panel"));
    QCOMPARE(panel.scroll->widget(),panel.content);
    QCOMPARE(bar->findChildren<QScrollArea *>().size(),1);
    QVERIFY(panel.dock->isAncestorOf(panel.content));
    for(QFrame *card:panel.cards)QVERIFY(panel.content->isAncestorOf(card));
    QVERIFY(navigationChoice(panel.dock));
    QVERIFY(navigationChoice(panel.dock)->mapTo(bar,QPoint()).y()<panel.scroll->mapTo(bar,QPoint()).y());
    QVERIFY(panel.dock->findChild<QPushButton *>(QStringLiteral("adjustment-expand-all")));
    QVERIFY(panel.dock->findChild<QPushButton *>(QStringLiteral("adjustment-collapse-all")));
    QCOMPARE(navigationChoice(panel.dock)->accessibleName(),QStringLiteral("调整分类"));
    QCOMPARE(panel.headers.size(),3);
    for(QToolButton *button:panel.headers)QVERIFY(button->isChecked());
    panel.dock->deleteLater();
}

void AdjustmentNavigationTests::comboEntriesFollowCardOrderAndTitles(){
    Panel panel=buildPanel(4);
    auto *choice=navigationChoice(panel.dock);
    QCOMPARE(choice->count(),4);
    QStringList expected;
    for(int i=0;i<panel.cards.size();++i)expected<<header(panel.cards[i])->text();
    for(int i=0;i<choice->count();++i)QCOMPARE(choice->itemText(i),expected[i]);
    QCOMPARE(expected,QStringList({QStringLiteral("分类1"),QStringLiteral("分类2"),QStringLiteral("分类3"),QStringLiteral("分类4")}));
    panel.dock->deleteLater();
}

void AdjustmentNavigationTests::selectingCollapsedTargetExpandsAndScrollsToHeader(){
    Panel panel=buildPanel(14,120);
    auto *choice=navigationChoice(panel.dock);
    for(auto *h:panel.headers)h->setChecked(false);
    QVERIFY(!panel.headers[11]->isChecked());
    choice->setCurrentIndex(11);
    QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,11));
    QVERIFY(panel.headers[11]->isChecked());
    QVERIFY(panel.headers[11]->isVisible());
    QScrollBar *bar=panel.scroll->verticalScrollBar();
    QVERIFY(bar->maximum()>0);
    QTRY_VERIFY(bar->value()>0);
    const int y=panel.headers[11]->mapTo(panel.scroll->viewport(),QPoint()).y();
    QVERIFY(y>=0&&y<panel.scroll->viewport()->height());
    panel.dock->deleteLater();
}

void AdjustmentNavigationTests::selectingCardAlignsTargetNearViewportTop(){
    // Viewport tall enough for one full expanded card, and enough cards below the
    // target that top alignment is actually reachable (no natural clamp).
    Panel panel=buildPanel(12,360);
    auto *choice=navigationChoice(panel.dock);
    auto *bar=panel.scroll->verticalScrollBar();
    auto *viewport=panel.scroll->viewport();
    QVERIFY(viewport->height()>0);
    for(QToolButton *button:panel.headers)button->setChecked(false);

    const int index=5;
    QFrame *card=panel.cards[index];
    QToolButton *button=panel.headers[index];
    QVERIFY(card->height()<=viewport->height()); // a full card fits, so the assert is meaningful

    auto assertAligned=[&](const char *from){
        QVERIFY(button->isChecked());
        const int top=card->mapTo(viewport,QPoint(0,0)).y();
        const int bottom=card->mapTo(viewport,QPoint(0,card->height())).y();
        QVERIFY2(top>=0&&top<=12,from);
        QVERIFY2(bottom<=viewport->height(),from);
        // The whole card, not just its header, must be on screen. The old
        // ensureWidgetVisible(header) behaviour left rows below the fold, which
        // also left the header pinned near the bottom edge instead of the top.
        const int headerY=button->mapTo(viewport,QPoint(0,0)).y();
        // The header has its own inset inside the card; top alignment refers
        // to the card boundary, not to removing the card's internal padding.
        QVERIFY2(headerY<=12+button->mapTo(card,QPoint()).y(),from);
    };

    // From the top of the list.
    bar->setValue(bar->minimum());
    QTest::qWait(1);
    choice->setCurrentIndex(index);
    QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,index));
    QTRY_VERIFY(bar->value()>0);
    QTest::qWait(1);
    assertAligned("from top");

    // From the bottom of the list: the jump must still land the card at the top.
    bar->setValue(bar->maximum());
    QTest::qWait(1);
    choice->setCurrentIndex(0);
    QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,0));
    QTest::qWait(1);
    choice->setCurrentIndex(index);
    QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,index));
    QTest::qWait(1);
    assertAligned("from bottom");

    panel.dock->deleteLater();
}
void AdjustmentNavigationTests::selectionLeavesOtherCollapseStatesAlone(){
    Panel panel=buildPanel(6);
    auto *choice=navigationChoice(panel.dock);
    panel.headers[0]->setChecked(false);
    panel.headers[1]->setChecked(false);
    panel.headers[5]->setChecked(false);
    choice->setCurrentIndex(3);
    QTest::keyClick(choice,Qt::Key_Return);
    QVERIFY(panel.headers[3]->isChecked());
    for(int index:{0,1,5})QVERIFY(!panel.headers[index]->isChecked());
    for(int index:{2,4})QVERIFY(panel.headers[index]->isChecked());
    panel.dock->deleteLater();
}

void AdjustmentNavigationTests::expandAllAndCollapseAllOnlyTouchHeaders(){
    Panel panel=buildPanel(5);
    auto *expand=panel.dock->findChild<QPushButton *>(QStringLiteral("adjustment-expand-all"));
    auto *collapse=panel.dock->findChild<QPushButton *>(QStringLiteral("adjustment-collapse-all"));
    QSignalSpy spinSpy(panel.spins[0],&QDoubleSpinBox::valueChanged);
    QSignalSpy comboSpy(panel.combos[0],&QComboBox::currentIndexChanged);
    collapse->click();
    for(QToolButton *button:panel.headers)QVERIFY(!button->isChecked());
    expand->click();
    for(QToolButton *button:panel.headers)QVERIFY(button->isChecked());
    QCOMPARE(spinSpy.count(),0);
    QCOMPARE(comboSpy.count(),0);
    panel.dock->deleteLater();
}

void AdjustmentNavigationTests::keyboardSelectionActivatesCard(){
    Panel panel=buildPanel(5);
    auto *choice=navigationChoice(panel.dock);
    for(QToolButton *button:panel.headers)button->setChecked(false);
    choice->setFocus();
    choice->setCurrentIndex(0);
    QTest::keyClick(choice,Qt::Key_Down);
    QCOMPARE(choice->currentIndex(),1);
    QVERIFY(panel.headers[1]->isChecked());
    QVERIFY(panel.headers[1]->isVisible());
    QTest::keyClick(choice,Qt::Key_Down);
    QCOMPARE(choice->currentIndex(),2);
    QVERIFY(panel.headers[2]->isChecked());
    panel.dock->deleteLater();
}

void AdjustmentNavigationTests::navigationWorksWhileContentDisabled(){
    Panel panel=buildPanel(4);
    panel.content->setEnabled(false);
    QVERIFY(!panel.content->isEnabled());
    auto *choice=navigationChoice(panel.dock);
    QVERIFY(choice->isEnabled());
    QWidget *bar=navigationBar(panel.dock);
    QVERIFY(bar->isEnabled());
    QSignalSpy spinSpy(panel.spins[1],&QDoubleSpinBox::valueChanged);
    choice->setCurrentIndex(1);
    QTest::keyClick(choice,Qt::Key_Return);
    QVERIFY(panel.headers[1]->isChecked());
    QCOMPARE(panel.spins[1]->value(),.25);
    QCOMPARE(spinSpy.count(),0);
    panel.dock->deleteLater();
}

void AdjustmentNavigationTests::navigationNeverChangesControlValues(){
    Panel panel=buildPanel(5);
    QList<QSignalSpy *> spies;
    for(QDoubleSpinBox *spin:panel.spins)spies.append(new QSignalSpy(spin,&QDoubleSpinBox::valueChanged));
    QList<double> before;
    for(QDoubleSpinBox *spin:panel.spins)before<<spin->value();
    QList<int> combosBefore;
    for(QComboBox *combo:panel.combos)combosBefore<<combo->currentIndex();
    auto *choice=navigationChoice(panel.dock);
    for(int index=0;index<choice->count();++index){choice->setCurrentIndex(index);QTest::keyClick(choice,Qt::Key_Return);}
    panel.dock->findChild<QPushButton *>(QStringLiteral("adjustment-collapse-all"))->click();
    panel.dock->findChild<QPushButton *>(QStringLiteral("adjustment-expand-all"))->click();
    for(int i=0;i<panel.spins.size();++i){QCOMPARE(panel.spins[i]->value(),before[i]);QCOMPARE(spies[i]->count(),0);}
    for(int i=0;i<panel.combos.size();++i)QCOMPARE(panel.combos[i]->currentIndex(),combosBefore[i]);
    for(auto *spy:spies)delete spy;
    panel.dock->deleteLater();
}

void AdjustmentNavigationTests::destroyingWidgetsIsSafe(){
    Panel panel=buildPanel(3);
    auto *choice=navigationChoice(panel.dock);
    choice->setCurrentIndex(1);
    delete panel.dock; // queued navigation must be cancelled with its context
    QCoreApplication::processEvents();
    Panel second=buildPanel(2);
    QVERIFY(navigationBar(second.dock));
    second.dock->deleteLater();
    QCoreApplication::processEvents();
}

void AdjustmentNavigationTests::nativeWindowNavigation(){
    Window window(nullptr,QStringLiteral(""));
    window.show();
    auto *dock=window.findChild<QDockWidget *>("adjustment-dock");
    auto *choice=navigationChoice(dock);
    QVERIFY(choice);QVERIFY(choice->count()>10);
    auto *scroll=dock->findChild<QScrollArea *>();QVERIFY(scroll);
    auto *panel=scroll->widget();QVERIFY(!panel->isEnabled());
    QVERIFY(choice->isEnabled());
    window.findChild<QPushButton *>("adjustment-collapse-all")->click();
    choice->setCurrentIndex(choice->count()-1);
    QTRY_VERIFY(scroll->verticalScrollBar()->value()>0);
    QTest::qWait(50);
    const auto snapshot=qEnvironmentVariable("LUMINA_NAVIGATION_SCREENSHOT");
    if(!snapshot.isEmpty())QVERIFY(window.grab().save(snapshot));
}

QTEST_MAIN(AdjustmentNavigationTests)
#include "adjustment_navigation_tests.moc"
