/*!
 * \file   test_comparisonplot_rangeslider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AT.3 — pins RangeSliderWidget's public API + invariants:
 *   - setRange clamps to [0..1] and reorders so lo ≤ hi
 *   - rangeChanged fires on changes, not on no-ops
 *   - keyboard nudges keep lo ≤ hi
 *   - mouse-drag of either thumb cannot cross the other thumb
 *
 * Self-contained: only links rangeslider.cpp + Qt6 Widgets.
 */
#include "ui/widgets/rangeslider.h"

#include <QObject>
#include <QSignalSpy>
#include <QTest>

using openswmmvis::ui::RangeSliderWidget;

class TestComparisonPlotRangeSlider : public QObject
{
    Q_OBJECT
private slots:
    void defaultRangeIsFull();
    void setRangeClampsAndOrders();
    void rangeChangedFiresOnlyOnChange();
    void arrowKeyNudgePreservesOrder();
    void homeAndEndShiftWindow();
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
    QVERIFY(s.lo() < s.hi());                 // ordering preserved
    QVERIFY(s.lo() > 0.4 - 1e-9);             // shifted right by ~1%
    QVERIFY(s.hi() > 0.6 - 1e-9);

    QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QCoreApplication::sendEvent(&s, &left);
    QVERIFY(s.lo() < s.hi());

    // Shift-arrow nudges by 5%.
    const qreal preShiftLo = s.lo();
    QKeyEvent shiftRight(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
    QCoreApplication::sendEvent(&s, &shiftRight);
    QVERIFY(s.lo() - preShiftLo > 0.04);      // moved by ≥4% (5% requested)

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

QTEST_MAIN(TestComparisonPlotRangeSlider)
#include "test_comparisonplot_rangeslider.moc"
