/*!
 * \file   test_cursorwindowslider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Issue 1 — pins CursorWindowSlider's public API + invariants (the single-thumb
 * scrubber that replaced the two-thumb RangeSliderWidget on the animation
 * toolbar):
 *   - default cursor 0, window 0
 *   - setCursorNorm / setWindowNorm clamp to [0..1]
 *   - cursorChanged / windowNormChanged fire on change, not on no-ops
 *   - arrow keys nudge the cursor; Home/End jump to the timeline ends
 *   - the cursor moves independently of the window (no two-handle coupling —
 *     the regression that caused the old lag)
 *
 * Self-contained: only links cursorwindowslider.cpp + Qt6 Widgets. Writes no
 * temp files.
 */
#include "ui/widgets/cursorwindowslider.h"

#include <QObject>
#include <QSignalSpy>
#include <QTest>

using openswmmvis::ui::CursorWindowSlider;

class TestCursorWindowSlider : public QObject
{
    Q_OBJECT
private slots:
    void defaults();
    void setCursorClamps();
    void setWindowClamps();
    void cursorChangedFiresOnlyOnChange();
    void windowChangedFiresOnlyOnChange();
    void arrowKeysNudgeCursor();
    void homeEndJumpToEnds();
    void cursorAndWindowAreIndependent();
};

void TestCursorWindowSlider::defaults()
{
    CursorWindowSlider s;
    QCOMPARE(s.cursorNorm(), 0.0);
    QCOMPARE(s.windowNorm(), 0.0);
}

void TestCursorWindowSlider::setCursorClamps()
{
    CursorWindowSlider s;
    s.setCursorNorm(-0.5);
    QCOMPARE(s.cursorNorm(), 0.0);
    s.setCursorNorm(1.5);
    QCOMPARE(s.cursorNorm(), 1.0);
    s.setCursorNorm(0.42);
    QCOMPARE(s.cursorNorm(), 0.42);
}

void TestCursorWindowSlider::setWindowClamps()
{
    CursorWindowSlider s;
    s.setWindowNorm(-0.5);
    QCOMPARE(s.windowNorm(), 0.0);
    s.setWindowNorm(2.0);
    QCOMPARE(s.windowNorm(), 1.0);
    s.setWindowNorm(0.25);
    QCOMPARE(s.windowNorm(), 0.25);
}

void TestCursorWindowSlider::cursorChangedFiresOnlyOnChange()
{
    CursorWindowSlider s;
    QSignalSpy spy(&s, &CursorWindowSlider::cursorChanged);

    s.setCursorNorm(0.3);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeLast().at(0).toDouble(), 0.3);

    s.setCursorNorm(0.3);            // no-op
    QCOMPARE(spy.count(), 0);

    s.setCursorNorm(0.6);
    QCOMPARE(spy.count(), 1);
}

void TestCursorWindowSlider::windowChangedFiresOnlyOnChange()
{
    CursorWindowSlider s;
    QSignalSpy spy(&s, &CursorWindowSlider::windowNormChanged);

    s.setWindowNorm(0.2);
    QCOMPARE(spy.count(), 1);

    s.setWindowNorm(0.2);            // no-op
    QCOMPARE(spy.count(), 1);
}

void TestCursorWindowSlider::arrowKeysNudgeCursor()
{
    CursorWindowSlider s;
    s.setCursorNorm(0.5);
    QSignalSpy spy(&s, &CursorWindowSlider::cursorChanged);

    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &right);
    QVERIFY(s.cursorNorm() > 0.5);                 // moved right ~1%

    QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &left);

    const qreal pre = s.cursorNorm();
    QKeyEvent shiftRight(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
    QCoreApplication::sendEvent(&s, &shiftRight);
    QVERIFY(s.cursorNorm() - pre > 0.04);          // Shift nudges ~5%

    QVERIFY(spy.count() >= 3);
}

void TestCursorWindowSlider::homeEndJumpToEnds()
{
    CursorWindowSlider s;
    s.setCursorNorm(0.4);

    QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &home);
    QCOMPARE(s.cursorNorm(), 0.0);

    QKeyEvent end(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &end);
    QCOMPARE(s.cursorNorm(), 1.0);
}

void TestCursorWindowSlider::cursorAndWindowAreIndependent()
{
    // The regression we are guarding against: moving the cursor must NOT change
    // the window (and vice-versa). In the old two-thumb slider the handles were
    // coupled through the controller, which is what made dragging laggy.
    CursorWindowSlider s;
    s.setWindowNorm(0.2);
    QSignalSpy winSpy(&s, &CursorWindowSlider::windowNormChanged);

    s.setCursorNorm(0.1);
    s.setCursorNorm(0.9);
    QCOMPARE(s.windowNorm(), 0.2);   // window untouched by scrubbing
    QCOMPARE(winSpy.count(), 0);     // and emitted no window change

    QSignalSpy curSpy(&s, &CursorWindowSlider::cursorChanged);
    s.setWindowNorm(0.5);
    QCOMPARE(s.cursorNorm(), 0.9);   // cursor untouched by a window change
    QCOMPARE(curSpy.count(), 0);
}

QTEST_MAIN(TestCursorWindowSlider)
#include "test_cursorwindowslider.moc"
