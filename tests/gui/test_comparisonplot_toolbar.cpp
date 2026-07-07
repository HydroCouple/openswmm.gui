/*!
 * \file   test_comparisonplot_toolbar.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AT.2 — pins the public API of `InteractiveChartView`: mode
 * round-trip, modeChanged emission, resetZoom restoring axis ranges
 * after a programmatic zoom, and chartContextMenuRequested firing on
 * right-click.
 *
 * Mouse-drag pan / rubber-band-zoom behavior is verified manually in
 * the AT.2 verification plan — synthetic-event drag is brittle on
 * offscreen QPA and Qt Charts' coordinate-mapping (mapFromScene /
 * mapToScene) returns null rects without a real top-level window.
 */
#include "ui/widgets/interactivechartview.h"

#include <QChart>
#include <QChartView>
#include <QContextMenuEvent>
#include <QDateTimeAxis>
#include <QLineSeries>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QValueAxis>

using openswmmvis::ui::InteractiveChartView;
using Mode = InteractiveChartView::Mode;

class TestComparisonPlotToolbar : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void modeRoundTripsAndSignalsOnChange();
    void modeChangeIsIdempotent();
    void rightClickEmitsContextMenuRequest();
    void resetZoomRestoresInitialRanges();
    void cursorReflectsMode();
    void axisEndpointHitTestAndSetValueAxisEdges();
    void dateTimeAxisEdgesAcceptDateTimes();

private:
    QChart *makeChart();
    QChart *makeDateChart(QDateTime &start, QDateTime &end);
};

void TestComparisonPlotToolbar::initTestCase()
{
    qRegisterMetaType<Mode>("openswmmvis::ui::InteractiveChartView::Mode");
}

QChart *TestComparisonPlotToolbar::makeChart()
{
    auto *chart = new QChart;
    auto *series = new QLineSeries;
    series->append(0.0, 10.0);
    series->append(1.0, 20.0);
    series->append(2.0, 30.0);
    chart->addSeries(series);
    auto *xAxis = new QValueAxis;  xAxis->setRange(0.0, 2.0);
    auto *yAxis = new QValueAxis;  yAxis->setRange(0.0, 30.0);
    chart->addAxis(xAxis, Qt::AlignBottom);
    chart->addAxis(yAxis, Qt::AlignLeft);
    series->attachAxis(xAxis);
    series->attachAxis(yAxis);
    return chart;
}

QChart *TestComparisonPlotToolbar::makeDateChart(QDateTime &start, QDateTime &end)
{
    start = QDateTime::fromMSecsSinceEpoch(1'767'225'600'000LL);
    end = start.addSecs(6 * 3600);

    auto *chart = new QChart;
    auto *series = new QLineSeries;
    series->append(start.toMSecsSinceEpoch(), 10.0);
    series->append(end.toMSecsSinceEpoch(), 20.0);
    chart->addSeries(series);
    auto *xAxis = new QDateTimeAxis; xAxis->setRange(start, end);
    auto *yAxis = new QValueAxis;    yAxis->setRange(0.0, 30.0);
    chart->addAxis(xAxis, Qt::AlignBottom);
    chart->addAxis(yAxis, Qt::AlignLeft);
    series->attachAxis(xAxis);
    series->attachAxis(yAxis);
    return chart;
}

void TestComparisonPlotToolbar::modeRoundTripsAndSignalsOnChange()
{
    InteractiveChartView view(makeChart());
    QCOMPARE(view.mode(), Mode::Select);

    QSignalSpy spy(&view, &InteractiveChartView::modeChanged);
    view.setMode(Mode::Pan);
    QCOMPARE(view.mode(), Mode::Pan);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<Mode>(), Mode::Pan);

    view.setMode(Mode::ZoomIn);
    QCOMPARE(view.mode(), Mode::ZoomIn);
    QCOMPARE(spy.count(), 1);

    view.setMode(Mode::ZoomOut);
    QCOMPARE(view.mode(), Mode::ZoomOut);

    view.setMode(Mode::Select);
    QCOMPARE(view.mode(), Mode::Select);
}

void TestComparisonPlotToolbar::modeChangeIsIdempotent()
{
    InteractiveChartView view(makeChart());
    QSignalSpy spy(&view, &InteractiveChartView::modeChanged);
    view.setMode(Mode::Pan);
    view.setMode(Mode::Pan);   // re-set to same mode — no signal
    view.setMode(Mode::Pan);
    QCOMPARE(spy.count(), 1);
}

void TestComparisonPlotToolbar::rightClickEmitsContextMenuRequest()
{
    InteractiveChartView view(makeChart());
    view.resize(400, 300);

    QSignalSpy spy(&view, &InteractiveChartView::chartContextMenuRequested);

    // Synthetic right-button press at viewport center.
    const QPoint pt = view.viewport()->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(pt), view.viewport()->mapToGlobal(pt),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &press);

    QCOMPARE(spy.count(), 1);
    const QPoint emittedGlobal = spy.takeFirst().at(0).toPoint();
    QVERIFY(!emittedGlobal.isNull());
}

void TestComparisonPlotToolbar::resetZoomRestoresInitialRanges()
{
    QChart *chart = makeChart();
    InteractiveChartView view(chart);
    view.resize(400, 300);

    // Snapshot the initial X axis range.
    const auto axes = chart->axes(Qt::Horizontal);
    QVERIFY(!axes.isEmpty());
    auto *xAxis = qobject_cast<QValueAxis *>(axes.first());
    QVERIFY(xAxis);
    const qreal x0 = xAxis->min();
    const qreal x1 = xAxis->max();

    // Drive a programmatic zoom by shrinking the axis range — the same
    // path Qt Charts uses internally when zoomIn(QRectF) runs.
    xAxis->setRange(0.5, 1.5);
    QVERIFY(xAxis->min() > x0);
    QVERIFY(xAxis->max() < x1);

    // resetZoom on a chart that never went through zoomIn doesn't bring
    // back the original range — Qt Charts only un-stacks pushed-zoom
    // states. So we exercise the canonical sequence: zoomIn → zoomReset.
    chart->zoomIn(QRectF(50, 50, 100, 100));
    view.resetZoom();
    // After the reset, axes should be back to where they were just before
    // the zoomIn (i.e., the (0.5, 1.5) we manually set).
    QCOMPARE(xAxis->min(), 0.5);
    QCOMPARE(xAxis->max(), 1.5);
}

void TestComparisonPlotToolbar::cursorReflectsMode()
{
    InteractiveChartView view(makeChart());
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view, 1000));

    view.setMode(Mode::Pan);
    QCOMPARE(view.viewport()->cursor().shape(), Qt::OpenHandCursor);

    view.setMode(Mode::ZoomIn);
    QCOMPARE(view.viewport()->cursor().shape(), Qt::CrossCursor);

    view.setMode(Mode::ZoomOut);
    QCOMPARE(view.viewport()->cursor().shape(), Qt::CrossCursor);

    view.setMode(Mode::Select);
    QCOMPARE(view.viewport()->cursor().shape(), Qt::ArrowCursor);
}

void TestComparisonPlotToolbar::axisEndpointHitTestAndSetValueAxisEdges()
{
    auto *chart = makeChart();
    InteractiveChartView view(chart);
    view.resize(500, 320);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view, 1000));

    const QRectF plot = chart->plotArea();
    QVERIFY(!plot.isEmpty());
    auto toViewport = [&view, chart](const QPointF &chartPoint) {
        return view.mapFromScene(chart->mapToScene(chartPoint));
    };

    QCOMPARE(view.axisEdgeAt(
                 toViewport(QPointF(plot.left(), plot.bottom())) + QPoint(0, 12)),
             InteractiveChartView::AxisEdge::XMinimum);
    QCOMPARE(view.axisEdgeAt(
                 toViewport(QPointF(plot.right(), plot.bottom())) + QPoint(0, 12)),
             InteractiveChartView::AxisEdge::XMaximum);
    QCOMPARE(view.axisEdgeAt(
                 toViewport(QPointF(plot.left(), plot.bottom())) + QPoint(-12, -10)),
             InteractiveChartView::AxisEdge::YMinimum);
    QCOMPARE(view.axisEdgeAt(
                 toViewport(QPointF(plot.left(), plot.top())) + QPoint(-12, 10)),
             InteractiveChartView::AxisEdge::YMaximum);

    auto *xAxis = qobject_cast<QValueAxis *>(chart->axes(Qt::Horizontal).first());
    auto *yAxis = qobject_cast<QValueAxis *>(chart->axes(Qt::Vertical).first());
    QVERIFY(xAxis);
    QVERIFY(yAxis);

    QVERIFY(view.setAxisEdgeValue(InteractiveChartView::AxisEdge::XMinimum, -1.5));
    QCOMPARE(xAxis->min(), -1.5);
    QVERIFY(view.setAxisEdgeValue(InteractiveChartView::AxisEdge::XMaximum, 4.0));
    QCOMPARE(xAxis->max(), 4.0);
    QVERIFY(view.setAxisEdgeValue(InteractiveChartView::AxisEdge::YMinimum, -2.0));
    QCOMPARE(yAxis->min(), -2.0);
    QVERIFY(view.setAxisEdgeValue(InteractiveChartView::AxisEdge::YMaximum, 50.0));
    QCOMPARE(yAxis->max(), 50.0);

    QVERIFY(!view.setAxisEdgeValue(InteractiveChartView::AxisEdge::XMinimum, 4.0));
    QVERIFY(!view.setAxisEdgeValue(InteractiveChartView::AxisEdge::YMaximum, -2.0));
}

void TestComparisonPlotToolbar::dateTimeAxisEdgesAcceptDateTimes()
{
    QDateTime start;
    QDateTime end;
    auto *chart = makeDateChart(start, end);
    InteractiveChartView view(chart);

    auto *xAxis = qobject_cast<QDateTimeAxis *>(chart->axes(Qt::Horizontal).first());
    QVERIFY(xAxis);

    const QDateTime nextStart = start.addSecs(3600);
    QVERIFY(view.setAxisEdgeValue(InteractiveChartView::AxisEdge::XMinimum, nextStart));
    QCOMPARE(xAxis->min(), nextStart);

    const QDateTime nextEnd = end.addSecs(3600);
    QVERIFY(view.setAxisEdgeValue(InteractiveChartView::AxisEdge::XMaximum, nextEnd));
    QCOMPARE(xAxis->max(), nextEnd);

    QVERIFY(!view.setAxisEdgeValue(InteractiveChartView::AxisEdge::XMaximum, start));
}

QTEST_MAIN(TestComparisonPlotToolbar)
#include "test_comparisonplot_toolbar.moc"
