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
#include "plot/profileplotoptions.h"
#include <QImage>
#include <QDir>

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
    void inundationClipsToAnalyticShoreline();
    void inundationRespectsGroundKnotsAndDryGaps();
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

namespace {
QImage renderInundation(const QVector<ProfilePlotWidget::Surface2DSample> &samples,
                        const QVector<QPointF> &ground)
{
    ProfilePlotOptions opts;
    opts.setShow2DInundation(true); opts.setLegendVisible(false);
    opts.setInundation2DLinePen(QPen(Qt::NoPen));
    opts.setInundation2DFillBrush(QBrush(QColor(255,0,0,128)));
    ProfileBuilder::PathStatic path;
    ProfileBuilder::NodeStatic n; n.invertElev=-1; n.maxDepth=3;
    path.nodes={n,n}; path.chainage={0,12}; path.terrainSamples=ground;
    path.nodes[0].maxDepth=ground.first().y()+1;
    path.nodes[1].maxDepth=ground.last().y()+1;
    ProfileBuilder::LinkStatic l; l.length=12; l.maxDepth=0.1; path.links={l};
    ProfilePlotWidget w; w.resize(700,360); w.setOptions(&opts); w.setPath(path);
    auto toggles=w.layerToggles(); toggles.useTerrainGround=true; w.setLayerToggles(toggles);
    w.setSurface2DSamples(samples);
    using E=ProfilePlotWidget::AxisEdge;
    w.setAxisEdgeValue(E::XMinimum,0); w.setAxisEdgeValue(E::XMaximum,12);
    w.setAxisEdgeValue(E::YMinimum,-1); w.setAxisEdgeValue(E::YMaximum,2);
    QImage img(w.size(),QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white); w.render(&img); return img;
}
bool waterPixel(const QImage &img,int x,int y)
{
    const auto c=img.pixelColor(x,y);
    return c.red()>220 && c.green()<180 && c.blue()<180 && c.green()>80;
}
int lastWaterColumn(const QImage &img)
{
    for (int x=img.width()-1;x>=0;--x)
        for (int y=0;y<img.height();++y) if(waterPixel(img,x,y)) return x;
    return -1;
}
void saveInundation(const QImage &img,const QString &name)
{
    const QString dir=qEnvironmentVariable("SWMMVIS_SHORELINE_ARTIFACT_DIR");
    if (!dir.isEmpty()) { QDir().mkpath(dir); img.save(dir+"/"+name); }
}
}

void TestProfilePlotAxisEdges::inundationClipsToAnalyticShoreline()
{
    using S=ProfilePlotWidget::Surface2DSample;
    const QVector<QPointF> ground{{0,0},{12,1.2}};
    const auto coarse=renderInundation({S{0,0,0.95},S{12,1.2,0.95}},ground);
    QVector<S> dense;
    for(int i=0;i<=120;++i) dense.push_back({i*0.1,i*0.01,0.95});
    const auto fine=renderInundation(dense,ground);
    // Intersection x=9.5, pixel=64+620*9.5/12. Fill excludes the final
    // subpixel taper. A sample-run painter stops far short or paints nothing.
    QVERIFY(std::abs(lastWaterColumn(coarse)-555)<=3);
    QVERIFY(std::abs(lastWaterColumn(coarse)-lastWaterColumn(fine))<=1);
    for(int x=70;x<500;++x)
        QVERIFY(waterPixel(coarse,x,150)==waterPixel(fine,x,150));
    saveInundation(coarse,QStringLiteral("profile-analytic-shoreline.png"));
}

void TestProfilePlotAxisEdges::inundationRespectsGroundKnotsAndDryGaps()
{
    using S=ProfilePlotWidget::Surface2DSample;
    const auto kink=renderInundation({S{0,0,1},S{12,0,1}},{{0,0},{6,0},{12,2}});
    // Drawn ground crosses the level surface at x=9, not x=6.
    QVERIFY(std::abs(lastWaterColumn(kink)-529)<=3);
    const auto gap=renderInundation(
        {S{0,0,1},S{4,0,1},S{8,0,1,true},S{12,0,1}},{{0,0},{12,0}});
    for(int x=300;x<450;++x)
        for(int y=30;y<300;++y) QVERIFY(!waterPixel(gap,x,y));
    const auto dryBed=renderInundation({S{0,2,1},S{12,2,1}},{{0,0},{12,0}});
    QCOMPARE(lastWaterColumn(dryBed),-1); // a lower DEM cannot wet a dry result bed
    saveInundation(gap,QStringLiteral("profile-dry-gap.png"));
    saveInundation(kink,QStringLiteral("profile-ground-kink.png"));
}

QTEST_MAIN(TestProfilePlotAxisEdges)
#include "test_profileplot_axis_edges.moc"
