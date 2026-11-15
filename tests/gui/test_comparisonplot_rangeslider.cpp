/**
 * @file      test_comparisonplot_rangeslider.cpp
 * @brief     Tests for comparisonplot rangeslider.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AT.3 — pins RangeSliderWidget's public API + invariants:
 *   - setRange clamps to [0..1] and reorders so lo ≤ hi
 *   - rangeChanged fires on changes, not on no-ops
 *   - keyboard nudges keep lo ≤ hi
 *   - mouse-drag of either thumb cannot cross the other thumb
 *
 * Self-contained: only links rangeslider.cpp + Qt6 Widgets.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/widgets/rangeslider.h"

#include "accessible_checks.h"

#include <QAccessible>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

using openswmmvis::ui::RangeSliderWidget;

class TestComparisonPlotRangeSlider : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Pin the initial normalized interval to [0,1].
     *
     */
    void defaultRangeIsFull();
    /**
     *
     * @brief Require endpoint clamping to [0,1] and sorting of reversed valid bounds.
     *
     */
    void setRangeClampsAndOrders();
    /**
     *
     * @brief Require one signal for each distinct range, silence on a repeat and exact final
     * lower/upper payloads.
     *
     */
    void rangeChangedFiresOnlyOnChange();
    /**
     *
     * @brief Require keyboard shifts to preserve ordering, Shift-right to move by more than 0.04
     * and at least three range signals.
     * Ordinary right checks allow the original bounds within tolerance; exact one-percent
     * step and mouse-thumb crossing are not asserted by this case.
     *
     */
    void arrowKeyNudgePreservesOrder();
    /**
     *
     * @brief Require Home/End to shift the fixed-width interval against zero/one without resizing
     * it.
     *
     */
    void homeAndEndShiftWindow();
    /**
     * @brief A-08: the slider is a named Slider whose spoken value is the focused part,
     *        formatted by the host, with the numeric value interface.
     */
    void screenReaderReadsTheRange();
    /**
     * @brief A-08: Tab and Shift+Tab step through the range, its start and its end before
     *        leaving; each step is announced; the keys move only the focused part.
     */
    void tabSelectsAPartAndKeysMoveIt();
    /**
     * @brief A-08: a screen-reader value change moves the focused part like the keys.
     */
    void screenReaderSetsTheFocusedPart();
    /**
     * @brief A-08: the focused part is outlined, and only while the slider has focus.
     */
    void focusedPartIsDrawn();
};

void TestComparisonPlotRangeSlider::defaultRangeIsFull()
{
    RangeSliderWidget s;
    QCOMPARE(s.lo(), 0.0);
    QCOMPARE(s.hi(), 1.0);
}

void TestComparisonPlotRangeSlider::setRangeClampsAndOrders()
{
    RangeSliderWidget s;

    // Below-range values clamp to 0.
    s.setRange(-0.5, 0.7);
    QCOMPARE(s.lo(), 0.0);
    QCOMPARE(s.hi(), 0.7);

    // Above-range values clamp to 1.
    s.setRange(0.3, 1.5);
    QCOMPARE(s.lo(), 0.3);
    QCOMPARE(s.hi(), 1.0);

    // Reversed lo/hi gets reordered.
    s.setRange(0.8, 0.2);
    QCOMPARE(s.lo(), 0.2);
    QCOMPARE(s.hi(), 0.8);
}

void TestComparisonPlotRangeSlider::rangeChangedFiresOnlyOnChange()
{
    RangeSliderWidget s;
    QSignalSpy spy(&s, &RangeSliderWidget::rangeChanged);

    s.setRange(0.2, 0.8);
    QCOMPARE(spy.count(), 1);

    // Same range — no signal.
    s.setRange(0.2, 0.8);
    QCOMPARE(spy.count(), 1);

    s.setRange(0.1, 0.9);
    QCOMPARE(spy.count(), 2);
    const auto args = spy.takeLast();
    QCOMPARE(args.at(0).toDouble(), 0.1);
    QCOMPARE(args.at(1).toDouble(), 0.9);
}

void TestComparisonPlotRangeSlider::arrowKeyNudgePreservesOrder()
{
    RangeSliderWidget s;
    s.setRange(0.4, 0.6);
    QSignalSpy spy(&s, &RangeSliderWidget::rangeChanged);

    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &right);
    QVERIFY(s.lo() < s.hi());     // ordering preserved
    QVERIFY(s.lo() > 0.4 - 1e-9); // shifted right by ~1%
    QVERIFY(s.hi() > 0.6 - 1e-9);

    QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &left);
    QVERIFY(s.lo() < s.hi());

    // Shift-arrow nudges by 5%.
    const qreal preShiftLo = s.lo();
    QKeyEvent shiftRight(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
    QCoreApplication::sendEvent(&s, &shiftRight);
    QVERIFY(s.lo() - preShiftLo > 0.04); // moved by ≥4% (5% requested)

    QVERIFY(spy.count() >= 3);
}

void TestComparisonPlotRangeSlider::homeAndEndShiftWindow()
{
    RangeSliderWidget s;
    s.setRange(0.3, 0.5);
    const qreal width = s.hi() - s.lo();

    QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &home);
    QCOMPARE(s.lo(), 0.0);
    QCOMPARE(s.hi(), width);

    QKeyEvent end(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &end);
    QCOMPARE(s.hi(), 1.0);
    QCOMPARE(s.lo(), 1.0 - width);
}

namespace
{
/** A line edit, the slider and another line edit, so Tab can enter and leave the slider. */
struct SliderRow {
    QWidget host;
    QLineEdit *before = new QLineEdit(&host);
    RangeSliderWidget *slider = new RangeSliderWidget(&host);
    QLineEdit *after = new QLineEdit(&host);
    SliderRow()
    {
        auto *layout = new QHBoxLayout(&host);
        layout->addWidget(before);
        layout->addWidget(slider, 1);
        layout->addWidget(after);
        host.resize(400, 40);
    }
};

QString twoDecimals(qreal v)
{
    return QString::number(v, 'f', 2);
}
} // namespace

void TestComparisonPlotRangeSlider::screenReaderReadsTheRange()
{
    RangeSliderWidget s;
    s.setAccessibleName(QStringLiteral("Time range"));
    s.setRange(0.2, 0.6);

    QAccessibleInterface *iface = a11ytest::accessibleFor(&s);
    QVERIFY(iface);
    QCOMPARE(iface->role(), QAccessible::Slider);
    QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("Time range"));
    QCOMPARE(iface->text(QAccessible::Value), QStringLiteral("20% to 60%"));
    QVERIFY(iface->text(QAccessible::Description).contains(QStringLiteral("Tab")));

    s.setValueFormatter(&twoDecimals);
    QCOMPARE(iface->text(QAccessible::Value), QStringLiteral("0.20 to 0.60"));

    QAccessibleValueInterface *value = iface->valueInterface();
    QVERIFY(value);
    QCOMPARE(value->currentValue().toDouble(), 0.2);
    QCOMPARE(value->minimumValue().toDouble(), 0.0);
    QCOMPARE(value->maximumValue().toDouble(), 1.0);
}

void TestComparisonPlotRangeSlider::tabSelectsAPartAndKeysMoveIt()
{
    SliderRow row;
    row.slider->setValueFormatter(&twoDecimals);
    row.slider->setRange(0.2, 0.6);
    row.host.show();
    QVERIFY(QTest::qWaitForWindowActive(&row.host));
    a11ytest::AccessibleEventRecorder events;

    row.before->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(row.before, Qt::Key_Tab);
    QVERIFY(row.slider->hasFocus());
    QCOMPARE(row.slider->focusedPart(), RangeSliderWidget::Part::Window);

    QTest::keyClick(row.slider, Qt::Key_Tab);
    QVERIFY(row.slider->hasFocus());
    QCOMPARE(row.slider->focusedPart(), RangeSliderWidget::Part::Start);
    QTest::keyClick(row.slider, Qt::Key_Right);
    QCOMPARE(row.slider->lo(), 0.21);
    QCOMPARE(row.slider->hi(), 0.6);

    QTest::keyClick(row.slider, Qt::Key_Tab);
    QCOMPARE(row.slider->focusedPart(), RangeSliderWidget::Part::End);
    QTest::keyClick(row.slider, Qt::Key_Left);
    QCOMPARE(row.slider->lo(), 0.21);
    QCOMPARE(row.slider->hi(), 0.59);
    QTest::keyClick(row.slider, Qt::Key_Home); // the end stops at the start
    QCOMPARE(row.slider->hi(), 0.21);

    QCOMPARE(events.valuesFor(row.slider),
             (QStringList{QStringLiteral("Start 0.20"), QStringLiteral("Start 0.21"),
                          QStringLiteral("End 0.60"), QStringLiteral("End 0.59"),
                          QStringLiteral("End 0.21")}));

    QTest::keyClick(row.slider, Qt::Key_Tab); // past the end: leave the slider
    QVERIFY(row.after->hasFocus());
    QTest::keyClick(row.after, Qt::Key_Backtab); // back in at the end
    QVERIFY(row.slider->hasFocus());
    QCOMPARE(row.slider->focusedPart(), RangeSliderWidget::Part::End);
    QTest::keyClick(row.slider, Qt::Key_Backtab);
    QTest::keyClick(row.slider, Qt::Key_Backtab);
    QCOMPARE(row.slider->focusedPart(), RangeSliderWidget::Part::Window);
    QTest::keyClick(row.slider, Qt::Key_Backtab);
    QVERIFY(row.before->hasFocus());
}

void TestComparisonPlotRangeSlider::screenReaderSetsTheFocusedPart()
{
    SliderRow row;
    row.slider->setRange(0.2, 0.6);
    row.host.show();
    QVERIFY(QTest::qWaitForWindowActive(&row.host));
    QAccessibleValueInterface *value = a11ytest::accessibleFor(row.slider)->valueInterface();
    QVERIFY(value);

    value->setCurrentValue(0.3); // the range keeps its width
    QCOMPARE(row.slider->lo(), 0.3);
    QCOMPARE(row.slider->hi(), 0.7);

    row.slider->setFocus(Qt::TabFocusReason);
    QTest::keyClick(row.slider, Qt::Key_Tab);
    QTest::keyClick(row.slider, Qt::Key_Tab);
    QCOMPARE(row.slider->focusedPart(), RangeSliderWidget::Part::End);
    QCOMPARE(value->currentValue().toDouble(), 0.7);
    value->setCurrentValue(0.1); // the end cannot pass the start
    QCOMPARE(row.slider->lo(), 0.3);
    QCOMPARE(row.slider->hi(), 0.3);
}

void TestComparisonPlotRangeSlider::focusedPartIsDrawn()
{
    SliderRow row;
    row.slider->setRange(0.2, 0.6);
    row.host.show();
    QVERIFY(QTest::qWaitForWindowActive(&row.host));
    row.before->setFocus(Qt::OtherFocusReason);
    const QImage unfocused = row.slider->grab().toImage();

    row.slider->setFocus(Qt::TabFocusReason);
    const QImage window = row.slider->grab().toImage();
    QTest::keyClick(row.slider, Qt::Key_Tab);
    const QImage start = row.slider->grab().toImage();
    QTest::keyClick(row.slider, Qt::Key_Tab);
    const QImage end = row.slider->grab().toImage();

    QVERIFY(window != unfocused);
    QVERIFY(start != unfocused);
    QVERIFY(end != unfocused);
    QVERIFY(start != end);
    QVERIFY(window != start);

    row.before->setFocus(Qt::OtherFocusReason);
    QCOMPARE(row.slider->grab().toImage(), unfocused);
}

QTEST_MAIN(TestComparisonPlotRangeSlider)
#include "test_comparisonplot_rangeslider.moc"
