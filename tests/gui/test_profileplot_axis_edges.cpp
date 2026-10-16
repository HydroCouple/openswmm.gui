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
    void liveFitIgnoresMissingAndHiddenOutputs();
    void lidLayersStayVisibleAndSelectable();
    void lidBottomPortsAndCompactWeirs();
    void lidWaterStatesAnimateWithoutMovingAxes();
    void hydraulicLinesAndFloodGlyphs_data();
    void hydraulicLinesAndFloodGlyphs();
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

namespace {
ProfileBuilder::PathStatic shallowProfile()
{
    ProfileBuilder::PathStatic path;
    ProfileBuilder::NodeStatic n; n.invertElev=100; n.maxDepth=2;
    path.nodes={n,n,n}; path.chainage={0,500,1000};
    for (int i=0;i<3;++i) path.nodes[i].name=QStringLiteral("N%1").arg(i);
    ProfileBuilder::LinkStatic l; l.length=500; l.maxDepth=0.3;
    path.links={l,l};
    return path;
}
}

void TestProfilePlotAxisEdges::liveFitIgnoresMissingAndHiddenOutputs()
{
    ProfilePlotWidget plot; plot.resize(800,480);
    const auto path=shallowProfile(); plot.setPath(path);
    ProfileBuilder::SourceSeries source; source.reportStepSec=60;
    source.nodeHead.resize(3); source.nodeDepth.resize(3); source.linkVelocity.resize(2);
    auto derived=std::make_shared<ProfileBuilder::SourceDerived>(
        ProfileBuilder::compute(path,source,ProfileBuilder::kGravityFps2));
    ProfilePlotWidget::SeriesBinding hgl; hgl.derived=derived;
    plot.setSeries({hgl});
    const auto geometryFit=plot.visibleDataRange();
    QVERIFY(geometryFit.top()>99); QVERIFY(geometryFit.bottom()<103);
    QVERIFY(plot.verticalExaggeration()>100); // independently fitted axes by default
    QVERIFY(geometryFit.left()<0 && geometryFit.right()>1000);

    source.periodCount=1;
    source.nodeHead={{100.5f},{100.75f},{100.25f}};
    source.nodeDepth={{0.5f},{0.75f},{0.25f}};
    source.linkVelocity={{0},{0}};
    QVERIFY(ProfileBuilder::appendPeriods(path,source,ProfileBuilder::kGravityFps2,*derived));
    // EGL and water-surface share storage with HGL, but are not enabled.
    derived->maxEgl.fill(5000,3); derived->minWaterSurface.fill(-5000,3);
    ProfilePlotWidget::SeriesBinding egl=hgl;
    egl.kind=ProfileBuilder::OutputKind::EGL; egl.visible=false;
    plot.setSeries({hgl,egl}); plot.setCurrentPeriod(0);
    QCOMPARE(plot.visibleDataRange(),geometryFit);
    egl.visible=true; plot.setSeries({hgl,egl});
    QVERIFY(plot.visibleDataRange().bottom()>5000);
    egl.visible=false; plot.setSeries({hgl,egl});
    QCOMPARE(plot.visibleDataRange(),geometryFit);

    plot.zoomBy(0.5); const auto manual=plot.visibleDataRange();
    derived->maxHgl.fill(104,3); plot.setSeries(plot.series());
    QCOMPARE(plot.visibleDataRange(),manual); // live appends respect manual navigation
    plot.fitToExtent();
    QVERIFY(plot.visibleDataRange().bottom()>104);
    QVERIFY(plot.visibleDataRange().top()>99);
    QVERIFY(plot.verticalExaggeration()>50);
    auto flat=path;
    for(auto &node:flat.nodes) node.maxDepth=0;
    for(auto &link:flat.links) link.maxDepth=0;
    plot.setSeries({}); plot.setPath(flat); plot.fitToExtent();
    QVERIFY(plot.visibleDataRange().top()>99);
    QVERIFY(plot.visibleDataRange().bottom()<101);
}

void TestProfilePlotAxisEdges::lidLayersStayVisibleAndSelectable()
{
    ProfilePlotOptions options;
    options.setLegendVisible(false); options.setShowTimeLabel(false);
    auto path=shallowProfile();
    path.nodes[0].kind=ProfileBuilder::NodeKind::Storage; // ordinary storage stays narrow
    auto &lid=path.nodes[1]; lid.kind=ProfileBuilder::NodeKind::Storage;
    using Layer=ProfileBuilder::NodeLidLayer;
    lid.lidLayers={{Layer::Surface,102,101.5},{Layer::Media,101.5,100.5},
                   {Layer::Aggregate,100.5,100}};
    ProfilePlotWidget plot; plot.resize(800,480); plot.setOptions(&options); plot.setPath(path);
    auto values=std::make_shared<ProfileBuilder::SourceDerived>();
    values->hglByPeriod={{101.9,101.9,101.9}};
    values->minHgl=values->maxHgl={101.9,101.9,101.9};
    ProfilePlotWidget::SeriesBinding hgl; hgl.derived=values;
    hgl.brush=QBrush(QColor(0,100,255,210)); hgl.pen=QPen(Qt::blue,2);
    auto maxHgl=hgl; maxHgl.kind=ProfileBuilder::OutputKind::MaxHGL;
    maxHgl.brush=QBrush(QColor(0,100,255,210));
    plot.setSeries({hgl,maxHgl}); plot.setCurrentPeriod(0);
    auto pixel=[&](double x,double y) {
        const auto range=plot.visibleDataRange();
        return QPoint(qRound(64+(x-range.left())/range.width()*(plot.width()-80)),
                      qRound(16+(range.bottom()-y)/range.height()*(plot.height()-56)));
    };
    QImage image(plot.size(),QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white); plot.render(&image);
    auto media=pixel(500,101.1); media.rx()+=16;
    auto gravel=pixel(500,100.2); gravel.rx()+=16;
    auto pond=pixel(500,101.7); pond.rx()+=16;
    const auto mc=image.pixelColor(media), gc=image.pixelColor(gravel), pc=image.pixelColor(pond);
    QVERIFY(mc.red()>mc.blue()+25); // brown material remains visible under HGL
    QVERIFY(std::abs(gc.red()-gc.blue())<25); // distinct aggregate texture
    QVERIFY(pc.blue()>pc.red()+40); // open surface space still shows ponding
    QCOMPARE(plot.nodeIndexAt(media),1); // full media column participates in selection
    QCOMPARE(plot.nodeIndexAt(pixel(0,101)+QPoint(16,0)),-1); // ordinary storage unchanged
    plot.setSelectedElementNames({lid.name});
    plot.render(&image);
    const auto selection=image.pixelColor(pixel(500,101)+QPoint(24,0));
    QVERIFY(selection.red()>200 && selection.green()<160);
    plot.show(); QCoreApplication::processEvents();
    QTest::mouseMove(&plot,media);
    QVERIFY(plot.toolTip().contains("Media")); QVERIFY(plot.toolTip().contains("Aggregate"));
    const QString dir=qEnvironmentVariable("SWMMVIS_PROFILE_FIT_OUTPUT");
    if(!dir.isEmpty()) { QDir().mkpath(dir); QVERIFY(image.save(dir+"/lid-profile.png")); }

    // Endpoint columns fit, including in a narrow window. Resize while in
    // automatic mode recomputes the schematic-width margin, not manual zoom.
    path.nodes[0].lidLayers=lid.lidLayers; plot.setPath(path); plot.resize(400,300);
    plot.fitToExtent();
    QVERIFY(pixel(0,101).x()-24>=64);
    const auto fit=plot.visibleDataRange(); plot.zoomBy(0.8);
    const auto manual=plot.visibleDataRange(); plot.resize(700,400);
    QCOMPARE(plot.visibleDataRange(),manual); QVERIFY(manual!=fit);
}

void TestProfilePlotAxisEdges::lidWaterStatesAnimateWithoutMovingAxes()
{
    ProfilePlotOptions options; options.setLegendVisible(false); options.setShowTimeLabel(false);
    auto path=shallowProfile(); auto &node=path.nodes[1]; node.kind=ProfileBuilder::NodeKind::Storage;
    using L=ProfileBuilder::NodeLidLayer;
    node.lidLayers={{L::Surface,102,101.5},{L::Media,101.5,100.5},{L::Aggregate,100.5,100}};
    ProfileBuilder::BranchLink outlet; outlet.name="Drain"; outlet.onPath=true;
    outlet.invertElev=100.5; outlet.lidLayer=3; node.branches={outlet};
    ProfilePlotWidget plot; plot.resize(900,520); plot.setOptions(&options); plot.setPath(path);
    auto pixel=[&](double y, int offset=14) {
        const auto r=plot.visibleDataRange();
        return QPoint(qRound(64+(500-r.left())/r.width()*(plot.width()-80))+offset,
                      qRound(16+(r.bottom()-y)/r.height()*(plot.height()-56)));
    };
    openswmmvis::io::LidStateFrame frame; frame.time=QDateTime::currentDateTimeUtc();
    frame.status="LID test sample";
    openswmmvis::io::LidNodeState state; state.name=node.name; state.richards=true;
    state.cells={{2,101,101.5,.4,.1,-.1,101.15},{2,100.5,101,.4,.4,.1,100.85},
                 {3,100,100.5,.3,.3,.2,100.45}};
    frame.nodes={state};
    const auto range=plot.visibleDataRange();
    plot.setLidWaterState(frame,true,true);
    QImage wet(plot.size(),QImage::Format_ARGB32_Premultiplied); wet.fill(Qt::white); plot.render(&wet);
    const auto blue=wet.pixelColor(pixel(100.7)); QVERIFY(blue.blue()>blue.red()+20);
    const auto boundary=wet.pixelColor(pixel(101,0)); QVERIFY(boundary.blue()>boundary.red()+50);
    const auto head=wet.pixelColor(pixel(100.45,3)); QVERIFY(head.red()>head.green()+30);
    plot.setLidWaterState(frame,false,false);
    QImage hidden(plot.size(),QImage::Format_ARGB32_Premultiplied); hidden.fill(Qt::white); plot.render(&hidden);
    const auto brown=hidden.pixelColor(pixel(100.7)); QVERIFY(brown.red()>brown.blue()+20);
    plot.setLidWaterState(frame,true,true); plot.show(); QCoreApplication::processEvents();
    QTest::mouseMove(&plot,pixel(100.4));
    QVERIFY(plot.toolTip().contains("Drain, layer 3")); // anchor selects gravel at the interface
    QVERIFY(plot.toolTip().contains("pore saturation"));
    auto dry=frame; for(auto &c:dry.nodes[0].cells) {c.moisture=.03;c.pressure=-1;}
    plot.setLidWaterState(dry,true,true); QCOMPARE(plot.visibleDataRange(),range);
    plot.setLidWaterState(frame,true,true); QCOMPARE(plot.visibleDataRange(),range);
    const auto dir=qEnvironmentVariable("SWMMVIS_PROFILE_FIT_OUTPUT");
    if(!dir.isEmpty()) { QDir().mkpath(dir); QVERIFY(wet.save(dir+"/lid-water-states.png")); }
    // Missing recordings retain material instead of implying a dry or saturated bed.
    plot.setLidWaterState({},true,true); plot.render(&hidden);
    const auto missing=hidden.pixelColor(pixel(100.7)); QVERIFY(missing.red()>missing.blue()+20);
}

QTEST_MAIN(TestProfilePlotAxisEdges)
#include "test_profileplot_axis_edges.moc"

void TestProfilePlotAxisEdges::hydraulicLinesAndFloodGlyphs_data()
{
    QTest::addColumn<bool>("lid");
    QTest::newRow("ordinary-conduit") << false;
    QTest::newRow("lid-bottom-outlet") << true;
}

void TestProfilePlotAxisEdges::hydraulicLinesAndFloodGlyphs()
{
    QFETCH(bool,lid);
    using namespace ProfileBuilder;
    ProfilePlotOptions options;
    options.setLegendVisible(false); options.setShowNodeRoses(false);
    options.setShowBranchStubs(false); options.setShowNodeLabels(false);
    options.setShowLinkLabels(false); options.setCurrentHglFill(false);
    const QColor hglColor(12,40,230), eglColor(230,10,190), floodColor(235,17,30);
    options.setFloodColor(floodColor);
    NodeStatic node; node.name="S"; node.invertElev=100; node.maxDepth=2.5;
    node.kind=lid?NodeKind::Storage:NodeKind::Junction;
    NodeStatic outfall=node; outfall.name="O"; outfall.kind=NodeKind::Outfall;
    LinkStatic link; link.name="D"; link.length=100; link.maxDepth=.25;
    link.kind=lid?LinkKind::Orifice:LinkKind::Conduit;
    if(lid) {
        node.lidLayers={{NodeLidLayer::Surface,102.5,102},
                        {NodeLidLayer::Media,102,101},{NodeLidLayer::Aggregate,101,100}};
        BranchLink drain; drain.name=link.name; drain.kind=link.kind;
        drain.lidLayer=3; drain.onPath=true; node.branches={drain};
    }
    PathStatic path; path.nodes={node,outfall}; path.links={link}; path.chainage={0,100};
    SourceSeries source; source.periodCount=2; source.reportStepSec=60;
    source.nodeHead={{101.2f,102.7f},{101.2f,102.7f}};
    source.linkVelocity={{5.f,5.f}};
    auto derived=std::make_shared<SourceDerived>(compute(path,source,kGravityFps2));
    ProfilePlotWidget::SeriesBinding hgl,egl;
    hgl.kind=OutputKind::HGL; hgl.derived=derived; hgl.pen=QPen(hglColor,3);
    egl.kind=OutputKind::EGL; egl.derived=derived; egl.pen=QPen(eglColor,3,Qt::DashLine);
    ProfilePlotWidget plot; plot.resize(900,520); plot.setOptions(&options);
    plot.setPath(path); plot.setSeries({hgl,egl}); plot.fitToExtent();
    auto countColor=[](const QImage &image,const QColor &target,QRect area) {
        int count=0; area=area.intersected(image.rect());
        for(int y=area.top();y<=area.bottom();++y) for(int x=area.left();x<=area.right();++x) {
            const auto c=image.pixelColor(x,y);
            if(std::abs(c.red()-target.red())<5 && std::abs(c.green()-target.green())<5
                && std::abs(c.blue()-target.blue())<5) ++count;
        }
        return count;
    };
    for(int period:{0,1,0}) {
        plot.setCurrentPeriod(period);
        QImage image(plot.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);plot.render(&image);
        QVERIFY(countColor(image,hglColor,image.rect())>200);
        QVERIFY(countColor(image,eglColor,image.rect())>200);
        const auto range=plot.visibleDataRange();
        const int x=qRound(64+(0-range.left())/range.width()*(plot.width()-80));
        const int y=qRound(16+(range.bottom()-102.5)/range.height()*(plot.height()-56));
        const auto glyph=QRect(x-20,y-20,40,21);
        if(period==1) QVERIFY(countColor(image,floodColor,glyph)>20);
        else QCOMPARE(countColor(image,floodColor,glyph),0);
        saveInundation(image,QString("hydraulics-%1-%2.png").arg(lid?"lid":"ordinary").arg(period));
    }
    // At the rim the data extent alone gives only a few pixels of padding.
    // Fit must reserve the whole configured glyph, including endpoint nodes.
    source.nodeHead={{102.5f,102.5f},{102.5f,102.5f}};
    source.linkVelocity={{0.f,0.f}};
    *derived=compute(path,source,kGravityFps2);
    options.setFloodRadiusPx(24); plot.resize(420,240); plot.setSeries({hgl,egl});
    plot.fitToExtent(); plot.setCurrentPeriod(1);
    const auto compactRange=plot.visibleDataRange();
    const double rimX=64+(0-compactRange.left())/compactRange.width()*(plot.width()-80);
    const double rimY=16+(compactRange.bottom()-102.5)/compactRange.height()*(plot.height()-56);
    QVERIFY(rimX-24>=65);
    QVERIFY(rimY-24>=17);
    QVERIFY(rimY-24>16+20+QFontMetricsF(options.timeLabelFont()).height());
    // Surface indicators are independent of line visibility, and toggling
    // them restores the normal node cap without disturbing manual zoom.
    hgl.visible=false; egl.visible=false; plot.setSeries({hgl,egl});
    plot.zoomBy(.9); const auto manualRange=plot.visibleDataRange();
    QImage glyphImage(plot.size(),QImage::Format_ARGB32_Premultiplied);
    glyphImage.fill(Qt::white); plot.render(&glyphImage);
    QVERIFY(countColor(glyphImage,floodColor,glyphImage.rect())>20);
    options.setShowFloodingGlyphs(false); plot.render(&glyphImage);
    QCOMPARE(countColor(glyphImage,floodColor,glyphImage.rect()),0);
    QCOMPARE(plot.visibleDataRange(),manualRange);
    options.setShowFloodingGlyphs(true); plot.render(&glyphImage);
    QVERIFY(countColor(glyphImage,floodColor,glyphImage.rect())>20);
    QCOMPARE(plot.visibleDataRange(),manualRange);
    plot.fitToExtent(); plot.render(&glyphImage);
    saveInundation(glyphImage,QString("hydraulics-%1-compact-glyph.png").arg(lid?"lid":"ordinary"));
}

void TestProfilePlotAxisEdges::lidBottomPortsAndCompactWeirs()
{
    using namespace ProfileBuilder;
    ProfilePlotOptions options; options.setLegendVisible(false);
    options.setShowNodeRoses(false); options.setShowBranchStubs(false);
    NodeStatic lid; lid.name="B"; lid.kind=NodeKind::Storage; lid.maxDepth=2.5;
    lid.lidLayers={{NodeLidLayer::Surface,2.5,2},{NodeLidLayer::Media,2,1},
                   {NodeLidLayer::Aggregate,1,0}};
    BranchLink drain; drain.name="V_BR"; drain.kind=LinkKind::Orifice;
    drain.maxDepth=.25; drain.lidLayer=3; drain.onPath=true;
    lid.branches={drain};
    NodeStatic outfall; outfall.name="R"; outfall.kind=NodeKind::Outfall;
    LinkStatic link; link.name=drain.name; link.kind=drain.kind; link.length=100; link.maxDepth=.25;
    ProfilePlotWidget plot; plot.resize(900,520); plot.setOptions(&options);
    auto pixel=[&](double x,double y) {
        const auto r=plot.visibleDataRange();
        return QPoint(qRound(64+(x-r.left())/r.width()*(plot.width()-80)),
                      qRound(16+(r.bottom()-y)/r.height()*(plot.height()-56)));
    };
    for(bool reversed:{false,true}) for(int layer:{2,3}) {
        lid.branches[0].lidLayer=layer; lid.branches[0].lidAnchorTop=false;
        link.reversed=reversed;
        PathStatic path; path.nodes=reversed?QVector<NodeStatic>{outfall,lid}:QVector<NodeStatic>{lid,outfall};
        path.links={link}; path.chainage={0,100}; plot.setPath(path); plot.fitToExtent();
        const double x=reversed?1:0, level=lid.lidLayers[layer-1].bottomElev;
        const QPoint port=pixel(x,level);
        QCOMPARE(plot.linkIndexAt(port+QPoint(0,7)),0); // vertical drain through layer bottom
        // Fit can shorten the schematic drop near the bottom edge. The
        // horizontal connector must still run below the physical layer port.
        bool belowPort = false;
        for (int dy=8; dy<=18; ++dy)
            belowPort |= plot.linkIndexAt(pixel(.5,level)+QPoint(0,dy))==0;
        QVERIFY(belowPort);
        if(layer==3) QCOMPARE(plot.linkIndexAt(pixel(.5,.125)),-1); // no fictitious long side barrel
        plot.setSelectedElementNames({link.name});
        QImage image(plot.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);plot.render(&image);
        const QColor selected=image.pixelColor(port+QPoint(0,7));
        QVERIFY(selected.red()>220 && selected.green()<150 && selected.blue()<50);
        saveInundation(image,QString("lid-bottom-%1-%2.png").arg(reversed).arg(layer));
        plot.setSelectedElementNames({});
        path.nodes[reversed?1:0].branches[0].lidAnchorTop=true; plot.setPath(path);plot.fitToExtent();
        QCOMPARE(plot.linkIndexAt(pixel(x,level)+QPoint(0,7)),-1); // TOP retains its side-elevation connection
    }
    lid.branches[0].lidLayer=3;lid.branches[0].lidAnchorTop=false;
    LinkStatic weir;weir.name="W_B";weir.kind=LinkKind::Weir;weir.length=100;weir.crestHeight=2.4;weir.reversed=true;
    weir.offset1=2.4; // real reversed W_B has the overflow crest at this endpoint
    BranchLink overflow;overflow.name="W_B";overflow.kind=LinkKind::Weir;overflow.onPath=true;
    lid.branches.append(overflow);
    PathStatic path; path.nodes={outfall,lid,outfall};path.links={weir,link};path.chainage={0,100,200};
    plot.setPath(path);plot.fitToExtent();
    QCOMPARE(plot.linkIndexAt(pixel(.5,1)), -1); // broad brown wall is gone
    QCOMPARE(plot.linkIndexAt(pixel(1,1)+QPoint(-29,0)),0); // compact weir remains selectable
    QImage image(plot.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);plot.render(&image);
    const auto openSpace=image.pixelColor(pixel(.5,1));
    QVERIFY(openSpace.red()>240 && openSpace.green()>240 && openSpace.blue()>240);
    saveInundation(image,"lid-overflow-bottom-drain.png");
}
