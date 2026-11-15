/**
 * @file      test_cursorwindowslider.cpp
 * @brief     Tests for cursorwindowslider.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/widgets/cursorwindowslider.h"

#include "accessible_checks.h"

#include <QAccessible>
#include <QImage>
#include <QLineEdit>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

using openswmmvis::ui::CursorWindowSlider;

class TestCursorWindowSlider : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Pin initial cursor and window normalized fractions to zero.
     *
     */
    void defaults();
    /**
     *
     * @brief Require cursor inputs below/above [0,1] to clamp and a valid 0.42 fraction to remain
     * exact.
     *
     */
    void setCursorClamps();
    /**
     *
     * @brief Require window inputs below/above [0,1] to clamp and a valid 0.25 fraction to remain
     * exact.
     *
     */
    void setWindowClamps();
    /**
     *
     * @brief Require cursor change payload 0.3, no emission for repeating it and another emission
     * for 0.6.
     *
     */
    void cursorChangedFiresOnlyOnChange();
    /**
     *
     * @brief Require one window change signal and no additional signal for repeated 0.2.
     *
     */
    void windowChangedFiresOnlyOnChange();
    /**
     *
     * @brief Require right-arrow increase, at least three change signals across
     * right/left/shift-right and a shift increment over 0.04.
     * The left-step magnitude and exact ordinary increment are not separately asserted.
     *
     */
    void arrowKeysNudgeCursor();
    /**
     *
     * @brief Require Home/End key events to place the normalized cursor exactly at zero/one.
     *
     */
    void homeEndJumpToEnds();
    /**
     *
     * @brief Require cursor changes not to alter/notify window fraction and window changes not to
     * alter/notify cursor fraction.
     *
     */
    void cursorAndWindowAreIndependent();
    /**
     * @brief A-01: a named Slider whose spoken value is the host's text for the cursor.
     */
    void screenReaderReadsTheTime();
    /**
     * @brief A-01: key moves and the end of a drag are announced; playback updates
     *        (setCursorNorm) are not.
     */
    void userMovesAreAnnouncedPlaybackIsNot();
    /**
     * @brief A-01: a screen-reader value change is a complete gesture, like a key press.
     */
    void screenReaderSetValueSeeks();
    /**
     * @brief A-01: the thumb is outlined while the slider has focus.
     */
    void focusIsDrawn();
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

    s.setCursorNorm(0.3); // no-op
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

    s.setWindowNorm(0.2); // no-op
    QCOMPARE(spy.count(), 1);
}

void TestCursorWindowSlider::arrowKeysNudgeCursor()
{
    CursorWindowSlider s;
    s.setCursorNorm(0.5);
    QSignalSpy spy(&s, &CursorWindowSlider::cursorChanged);

    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &right);
    QVERIFY(s.cursorNorm() > 0.5); // moved right ~1%

    QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &left);

    const qreal pre = s.cursorNorm();
    QKeyEvent shiftRight(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
    QCoreApplication::sendEvent(&s, &shiftRight);
    QVERIFY(s.cursorNorm() - pre > 0.04); // Shift nudges ~5%

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
    QCOMPARE(s.windowNorm(), 0.2); // window untouched by scrubbing
    QCOMPARE(winSpy.count(), 0);   // and emitted no window change

    QSignalSpy curSpy(&s, &CursorWindowSlider::cursorChanged);
    s.setWindowNorm(0.5);
    QCOMPARE(s.cursorNorm(), 0.9); // cursor untouched by a window change
    QCOMPARE(curSpy.count(), 0);
}

void TestCursorWindowSlider::screenReaderReadsTheTime()
{
    CursorWindowSlider s;
    s.setAccessibleName(QStringLiteral("Animation timeline"));
    s.setCursorNorm(0.25);

    QAccessibleInterface *iface = a11ytest::accessibleFor(&s);
    QVERIFY(iface);
    QCOMPARE(iface->role(), QAccessible::Slider);
    QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("Animation timeline"));
    QCOMPARE(iface->text(QAccessible::Value), QStringLiteral("25%"));
    QVERIFY(iface->text(QAccessible::Description).contains(QStringLiteral("Home")));

    s.setValueFormatter([](qreal v) { return QStringLiteral("hour %1").arg(qRound(v * 24)); });
    QCOMPARE(iface->text(QAccessible::Value), QStringLiteral("hour 6"));
    QVERIFY(iface->valueInterface());
    QCOMPARE(iface->valueInterface()->currentValue().toDouble(), 0.25);
}

void TestCursorWindowSlider::userMovesAreAnnouncedPlaybackIsNot()
{
    CursorWindowSlider s;
    s.resize(300, 24);
    s.setValueFormatter([](qreal v) { return QString::number(v, 'f', 2); });
    s.setCursorNorm(0.5);
    s.show();
    QVERIFY(QTest::qWaitForWindowExposed(&s));
    a11ytest::AccessibleEventRecorder events;

    for (int i = 0; i < 5; ++i) // playback moves the thumb every frame
        s.setCursorNorm(0.5 + 0.01 * (i + 1));
    QCOMPARE(events.valuesFor(&s), QStringList());

    QTest::keyClick(&s, Qt::Key_Right);
    QTest::keyClick(&s, Qt::Key_End);
    QTest::keyClick(&s, Qt::Key_End); // already at the end: nothing new to say
    QCOMPARE(events.valuesFor(&s), (QStringList{QStringLiteral("0.56"), QStringLiteral("1.00")}));

    events.clear();
    QTest::mousePress(&s, Qt::LeftButton, {}, QPoint(150, 12));
    QTest::mouseMove(&s, QPoint(100, 12));
    QCOMPARE(events.valuesFor(&s), QStringList()); // not every pixel of a drag
    QTest::mouseRelease(&s, Qt::LeftButton, {}, QPoint(100, 12));
    QCOMPARE(events.valuesFor(&s).size(), 1);
}

void TestCursorWindowSlider::screenReaderSetValueSeeks()
{
    CursorWindowSlider s;
    QSignalSpy released(&s, &CursorWindowSlider::cursorReleased);
    a11ytest::accessibleFor(&s)->valueInterface()->setCurrentValue(0.4);
    QCOMPARE(s.cursorNorm(), 0.4);
    QCOMPARE(released.count(), 1);
    QCOMPARE(released.first().first().toDouble(), 0.4);
}

void TestCursorWindowSlider::focusIsDrawn()
{
    QWidget host;
    auto *other = new QLineEdit(&host);
    auto *slider = new CursorWindowSlider(&host);
    auto *layout = new QVBoxLayout(&host);
    layout->addWidget(other);
    layout->addWidget(slider);
    slider->setCursorNorm(0.5);
    host.resize(300, 80);
    host.show();
    QVERIFY(QTest::qWaitForWindowActive(&host));

    other->setFocus();
    const QImage unfocused = slider->grab().toImage();
    slider->setFocus();
    QVERIFY(slider->grab().toImage() != unfocused);
    other->setFocus();
    QCOMPARE(slider->grab().toImage(), unfocused);
}

QTEST_MAIN(TestCursorWindowSlider)
#include "test_cursorwindowslider.moc"
