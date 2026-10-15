#include "app/curve_editor.h"
#include <QSignalSpy>
#include <QTest>

class CurveKeyboardTests:public QObject {
    Q_OBJECT
private slots:
    void insertNudgeSelectionAndDelete(){
        CurveEditor editor;editor.show();editor.setFocus();QSignalSpy edits(&editor,&CurveEditor::curveEdited),finished(&editor,&CurveEditor::curveFinished);
        QTest::keyClick(&editor,Qt::Key_Home);QTest::keyClick(&editor,Qt::Key_Right,Qt::ControlModifier);QCOMPARE(edits.size(),0);
        QTest::keyClick(&editor,Qt::Key_Insert);QCOMPARE(edits.size(),1);QCOMPARE(finished.size(),1);auto curve=qvariant_cast<Curve>(edits.last().at(0));QCOMPARE(curve.size(),3);QCOMPARE(curve[1],QPointF(.5,.5));
        QTest::keyClick(&editor,Qt::Key_Up);curve=qvariant_cast<Curve>(edits.last().at(0));QVERIFY(qAbs(curve[1].y()-.505)<1e-12);
        QTest::keyClick(&editor,Qt::Key_Right,Qt::ShiftModifier);curve=qvariant_cast<Curve>(edits.last().at(0));QVERIFY(qAbs(curve[1].x()-.55)<1e-12);
        QTest::keyClick(&editor,Qt::Key_Delete);QCOMPARE(qvariant_cast<Curve>(edits.last().at(0)).size(),2);QCOMPARE(edits.size(),finished.size());QVERIFY(!editor.accessibleDescription().isEmpty());
    }
    void endpointsReadOnlyLimitsAndNoOps(){
        CurveEditor editor;editor.show();QSignalSpy edits(&editor,&CurveEditor::curveEdited);
        QTest::keyClick(&editor,Qt::Key_Home);QTest::keyClick(&editor,Qt::Key_Left);QTest::keyClick(&editor,Qt::Key_Down);QTest::keyClick(&editor,Qt::Key_Delete);QCOMPARE(edits.size(),0);
        QTest::keyClick(&editor,Qt::Key_End);QTest::keyClick(&editor,Qt::Key_Right);QTest::keyClick(&editor,Qt::Key_Up);QCOMPARE(edits.size(),0);
        editor.setReadOnly(true);QTest::keyClick(&editor,Qt::Key_Insert);QTest::keyClick(&editor,Qt::Key_Down);QCOMPARE(edits.size(),0);
        editor.setReadOnly(false);Curve dense;for(int i=0;i<maximumCurvePoints;++i)dense.append({i/double(maximumCurvePoints-1),i/double(maximumCurvePoints-1)});editor.setCurve(dense);QTest::keyClick(&editor,Qt::Key_Insert);QCOMPARE(edits.size(),0);
    }
};
QTEST_MAIN(CurveKeyboardTests)
#include "curve_keyboard_tests.moc"
