/*!
 * \file   test_profileplot_axis_edges.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Pins profile-plot axis endpoint editing without opening modal dialogs:
 *   - endpoint-label gutter hit tests identify X/Y min/max labels
 *   - setting one edge mutates only that visible range boundary
 *   - invalid min/max crossings are rejected
 */
#include "plot/meshprofileplotwidget.h"
#include "plot/profileplotwidget.h"

#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QTest>

class TestProfilePlotAxisEdges : public QObject
{
    Q_OBJECT
private slots:
    void profilePlotHitTestsAndSetsEdges();
    void meshProfilePlotHitTestsAndSetsEdges();
};

void TestProfilePlotAxisEdges::profilePlotHitTestsAndSetsEdges()
{
    ProfilePlotWidget plot;
    plot.resize(500, 300);

    using Edge = ProfilePlotWidget::AxisEdge;
    QCOMPARE(plot.axisEdgeAt(QPoint(64, 272)), Edge::XMinimum);
    QCOMPARE(plot.axisEdgeAt(QPoint(484, 272)), Edge::XMaximum);
    QCOMPARE(plot.axisEdgeAt(QPoint(52, 250)), Edge::YMinimum);
    QCOMPARE(plot.axisEdgeAt(QPoint(52, 26)), Edge::YMaximum);
    QCOMPARE(plot.axisEdgeAt(QPoint(250, 150)), Edge::None);

    QVERIFY(plot.setAxisEdgeValue(Edge::XMinimum, -5.0));
    QVERIFY(plot.setAxisEdgeValue(Edge::XMaximum, 20.0));
    QVERIFY(plot.setAxisEdgeValue(Edge::YMinimum, -2.0));
    QVERIFY(plot.setAxisEdgeValue(Edge::YMaximum, 15.0));

    const QRectF range = plot.visibleDataRange();
    QCOMPARE(range.left(), -5.0);
    QCOMPARE(range.right(), 20.0);
    QCOMPARE(range.top(), -2.0);
    QCOMPARE(range.bottom(), 15.0);

    QVERIFY(!plot.setAxisEdgeValue(Edge::XMinimum, 20.0));
    QVERIFY(!plot.setAxisEdgeValue(Edge::YMaximum, -2.0));
}

void TestProfilePlotAxisEdges::meshProfilePlotHitTestsAndSetsEdges()
{
    MeshProfilePlotWidget plot;
    plot.resize(500, 300);

    using Edge = MeshProfilePlotWidget::AxisEdge;
    QCOMPARE(plot.axisEdgeAt(QPoint(64, 272)), Edge::XMinimum);
    QCOMPARE(plot.axisEdgeAt(QPoint(484, 272)), Edge::XMaximum);
    QCOMPARE(plot.axisEdgeAt(QPoint(52, 250)), Edge::YMinimum);
    QCOMPARE(plot.axisEdgeAt(QPoint(52, 26)), Edge::YMaximum);
    QCOMPARE(plot.axisEdgeAt(QPoint(250, 150)), Edge::None);

    QVERIFY(plot.setAxisEdgeValue(Edge::XMinimum, -10.0));
    QVERIFY(plot.setAxisEdgeValue(Edge::XMaximum, 25.0));
    QVERIFY(plot.setAxisEdgeValue(Edge::YMinimum, -3.0));
    QVERIFY(plot.setAxisEdgeValue(Edge::YMaximum, 18.0));

    const QRectF range = plot.visibleDataRange();
    QCOMPARE(range.left(), -10.0);
    QCOMPARE(range.right(), 25.0);
    QCOMPARE(range.top(), -3.0);
    QCOMPARE(range.bottom(), 18.0);

    QVERIFY(!plot.setAxisEdgeValue(Edge::XMaximum, -10.0));
    QVERIFY(!plot.setAxisEdgeValue(Edge::YMinimum, 18.0));
}

QTEST_MAIN(TestProfilePlotAxisEdges)
#include "test_profileplot_axis_edges.moc"
