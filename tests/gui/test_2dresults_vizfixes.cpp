/*!
 * \file   test_2dresults_vizfixes.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Pins the SWMM2DResultsLayer behaviour changed by the 2D visualization
 * refinement (PLAN_2D_VIZ_REFINEMENT.md):
 *   - Issue 3: rebuildSceneGeometry_ deduplicates mesh edges (each undirected
 *     edge stored once → m_sceneEdges.size() = unique-edge count, not 3·nTri).
 *   - Issue 2: scrubbing a LIVE source via setCurrentSimTime clears follow-live
 *     so refreshTimeRange() no longer snaps the cursor back to the newest frame;
 *     seeking to the last frame re-arms follow-live.
 *   - Issue 4: at a cell's peak frame the animated interpolated depth
 *     (depthAtSceneInterp) equals the max-depth envelope sample
 *     (maxDepthAtSceneInterp) — they share one reduction.
 *   - Issue 5: velocityAtScene returns false with no flux data and off-mesh.
 *
 * Issue #155 adds the coordinate-scale cases: a declared /crs factor scales
 * the source's SI metres to model units, a metric declaration is a no-op, an
 * undeclared source takes the caller's fallback, and no declaration + no
 * fallback leaves the coordinates alone.
 *
 * Registered in tests/gui/CMakeLists.txt with the full-app link pattern
 * (swmmvis_link_full_app_deps) — it drives the real SWMM2DResultsLayer, whose
 * link closure is most of the app. Pure assertions — writes no temp files.
 */
#include "layers/swmm2dresultslayer.h"
#include "plot/meshprofilesampler.h"
#include "plot/meshprofileplotwidget.h"
#include "plot/meshprofileplotoptions.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/scalarfillsublayer.h"
#include <QSGGeometryNode>
#include <QGraphicsScene>
#include <QPainter>

#include <QDateTime>
#include <QObject>
#include <QTest>
#include <QSignalSpy>

#include <array>
#include <memory>
#include <vector>

namespace {

// Minimal synthetic source: a unit square split into two triangles sharing the
// 0–2 diagonal. Flat bed (z = 0) so the free-surface reconstruction reduces to
// η = depth and per-vertex blends are exact. Depth is spatially uniform per
// frame and rises each frame, so every cell/vertex peaks at the LAST frame.
//
//   v3(0,1) ---- v2(1,1)
//     |  \  tri1   |
//     |    \       |
//   v0(0,0) ---- v1(1,0)
//   tri0 = {0,1,2}   tri1 = {0,2,3}
class FakeSource : public IMesh2DSource
{
public:
    explicit FakeSource(bool live, std::vector<float> perFrameDepth)
        : m_live(live), m_frameDepth(std::move(perFrameDepth)) {}

    int vertexCount()   const override { return 4; }
    int triangleCount() const override { return 2; }
    int timeCount()     const override { return int(m_frameDepth.size()); }
    bool isLive()       const override { return m_live; }

    bool readMeshGeometry(std::vector<double>& vx, std::vector<double>& vy,
                          std::vector<double>& vz,
                          std::vector<std::array<int, 3>>& tris) override
    {
        vx = {0.0, 1.0, 1.0, 0.0};
        vy = {0.0, 0.0, 1.0, 1.0};
        vz = {0.0, 0.0, 0.0, 0.0};
        tris = {{0, 1, 2}, {0, 2, 3}};
        return true;
    }

    bool readDepthsAt(int t, std::vector<float>& depths) override
    {
        if (t < 0 || t >= timeCount()) return false;
        depths.assign(2, m_frameDepth[size_t(t)]);   // uniform over both cells
        return true;
    }

    QDateTime simTimeAt(int t) const override
    {
        return m_t0.addSecs(qint64(t) * 60);          // 1-minute frames
    }

    // Issue #155 — lets a case declare the metres-per-model-unit factor the
    // engine's /crs variable would carry. Default: undeclared (pre-6.0 file).
    void declareCoordinateReference(const QString& crs, double metresPerModelUnit)
    {
        m_ref.crs                = crs;
        m_ref.metresPerModelUnit = metresPerModelUnit;
        m_ref.storedUnits        = QStringLiteral("m");
        m_ref.declared           = true;
    }

    openswmmvis::io::CoordinateReference coordinateReference() const override
    {
        return m_ref;
    }

private:
    bool               m_live;
    std::vector<float> m_frameDepth;
    QDateTime          m_t0 = QDateTime(QDate(2026, 1, 1), QTime(0, 0));
    openswmmvis::io::CoordinateReference m_ref;
};

// Mixed triangle/quad source (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md):
// a unit-square QUAD (cell 0) sharing its right edge v1–v2 with a triangle
// (cell 1). Flat bed. Cell depths are constant in time.
//
//   v3(0,1) ---- v2(1,1)
//     |            |  \
//     |   quad 0   |   tri 1  v4(2,0.5)
//     |            |  /
//   v0(0,0) ---- v1(1,0)
//   cells = {0,1,2,3}, {1,4,2,-1}
class FakeMixedSource : public IMesh2DSource
{
public:
    FakeMixedSource(float quadDepth, float triDepth)
        : m_quadDepth(quadDepth), m_triDepth(triDepth) {}

    int vertexCount()   const override { return 5; }
    int triangleCount() const override { return 2; }     // CELLS
    int timeCount()     const override { return 1; }

    bool readCells(std::vector<double>& vx, std::vector<double>& vy,
                   std::vector<double>& vz,
                   std::vector<std::array<int, 4>>& cells) override
    {
        vx = {0.0, 1.0, 1.0, 0.0, 2.0};
        vy = {0.0, 0.0, 1.0, 1.0, 0.5};
        vz = {0.0, 0.0, 0.0, 0.0, 0.0};
        cells = {{0, 1, 2, 3}, {1, 4, 2, -1}};
        return true;
    }

    // Display fan (not consulted by the layer, which calls readCells and
    // splits with mesh::cellGeom itself).
    bool readMeshGeometry(std::vector<double>& vx, std::vector<double>& vy,
                          std::vector<double>& vz,
                          std::vector<std::array<int, 3>>& tris) override
    {
        std::vector<std::array<int, 4>> cells;
        readCells(vx, vy, vz, cells);
        tris = {{0, 1, 3}, {1, 2, 3}, {1, 4, 2}};
        return true;
    }

    bool readDepthsAt(int t, std::vector<float>& depths) override
    {
        if (t != 0) return false;
        depths = {m_quadDepth, m_triDepth};
        return true;
    }

    QDateTime simTimeAt(int t) const override
    {
        return QDateTime(QDate(2026, 1, 1), QTime(0, 0)).addSecs(qint64(t) * 60);
    }

private:
    float m_quadDepth, m_triDepth;
};

class VfrSource : public IMesh2DSource {
public:
    std::vector<double> x{0,1,0,1}, y{0,0,1,1}, z{0,0,4,4};
    std::vector<std::vector<float>> frames{{0.4921875f,0.0f}};
    int vertexCount() const override { return int(x.size()); }
    int triangleCount() const override { return 2; }
    int timeCount() const override { return int(frames.size()); }
    bool readMeshGeometry(std::vector<double>& ox,std::vector<double>& oy,
        std::vector<double>& oz,std::vector<std::array<int,3>>& tris) override {
        ox=x;oy=y;oz=z;tris={{0,1,2},{1,3,2}};return true;
    }
    bool readDepthsAt(int t,std::vector<float>& d) override {
        if (t<0 || t>=timeCount()) return false;
        d=frames[size_t(t)];return true;
    }
    bool readVertexDepthsAt(int,std::vector<float>& d) override {
        d.assign(x.size(),100.0f);return true; // deliberately incompatible legacy smoothing
    }
    QDateTime simTimeAt(int t) const override {
        return QDateTime(QDate(2026,1,1),QTime(0,0)).addSecs(t);
    }
};

// Same sloping fixture with a spatially uniform RT0 flow. Reversing speed
// exercises both dense and grid-sampled velocity snapshots during handoff.
class FlowVfrSource : public VfrSource {
public:
    float speed = 1.0f;
    bool readEdgeGeometry(std::vector<float>& lengths,
        std::vector<float>& nx, std::vector<float>& ny) override {
        const float diagonal = std::sqrt(2.0f), n = 1.0f/diagonal;
        lengths={1,diagonal,1,0, 1,1,diagonal,0};
        nx={0,n,-1,0, 1,0,-n,0}; ny={-1,n,0,0, 0,1,-n,0};
        return true;
    }
    bool readEdgeFluxAt(int, std::vector<float>& flux) override {
        std::vector<float> length,nx,ny; readEdgeGeometry(length,nx,ny);
        flux.resize(length.size());
        for (size_t i=0;i<flux.size();++i) flux[i]=length[i]*nx[i]*speed;
        return true;
    }
};

} // namespace

class Test2DResultsVizFixes : public QObject
{
    Q_OBJECT
private slots:
    void cellVfrKeepsDryNeighborsDry();
    void smoothProfilesAndContoursShareOneSurface();
    void smoothMaximumContainsEveryFrame();
    void mapFillsStopAtExactShoreline();
    void contourAnimationPublishesCompleteFrames();
    void depthClassificationRemainsStableDuringPlayback();
    void cpuContoursStopAtExactShoreline();
    void exactProfileIgnoresStationSpacing();
    void boundaryProfileKeepsWetSideAcrossFrames();
    void contourRangesSaturateCpuAndQsg();
    void latestFrameReplacementCanReduceEnvelope();
    void edgesAreDeduplicated();
    void liveScrubHoldsFrame();
    void liveSeekToLastReArmsFollow();
    void maxDepthMatchesAnimatedAtPeak();
    void velocityFalseWithoutFlux();
    void declaredMetreFactorScalesToModelUnits();
    void metricModelIsUnscaled();
    void undeclaredSourceUsesCallerFallback();
    void undeclaredSourceDefaultsToNoScaling();
    void mixedMeshQuadRendersAsFanOfItsCell();
};

void Test2DResultsVizFixes::smoothProfilesAndContoursShareOneSurface()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<FakeMixedSource>(0.4f,0.2f));
    const auto profile=MeshProfileSampler::buildMeshProfile(nullptr,&layer,{{0,-0.5},{2,-0.5}},100);
    QVERIFY(profile.samples.size()>=4);
    bool slopes=false,sharedEdge=false;
    for (int i=0;i<profile.samples.size();++i) {
        const auto& s=profile.samples[i];
        QVERIFY(std::abs(s.signedDepthNow-layer.signedDepthAtDisplayTriangle(s.displayTriIdx,s.scenePt))<1e-12);
        QVERIFY(std::abs(s.signedMaxDepth-s.signedDepthNow)<1e-12);
        if (i==0) continue;
        const auto& before=profile.samples[i-1];
        if (s.breakBefore && std::abs(before.chainage-s.chainage)<1e-12) {
            QVERIFY(std::abs(before.signedDepthNow-s.signedDepthNow)<1e-12);
            sharedEdge=true;
        } else if (!s.breakBefore && std::abs(s.signedDepthNow-before.signedDepthNow)>0.01) slopes=true;
    }
    QVERIFY2(sharedEdge,"The test must cross a cell boundary");
    QVERIFY2(slopes,"The water surface is still a series of flat cell plateaus");
    const double left=layer.depthAtCellInterp(0,{1-1e-7,-0.5});
    const double right=layer.depthAtCellInterp(1,{1+1e-7,-0.5});
    QVERIFY2(std::abs(left-right)<1e-6,"Water surface jumps at the shared wet edge");
    MeshProfilePlotOptions options;
    options.setShowMaxEnvelopeFill(false);options.setShowMaxEnvelopeLine(false);
    options.setShowTimeLabel(false);options.setLegendVisible(false);
    MeshProfilePlotWidget widget;
    widget.setOptions(&options);widget.resize(800,400);widget.setProfile(profile);
    const QString dir=qEnvironmentVariable("VFR_TEST_ARTIFACTS");
    if (!dir.isEmpty()) {
        QImage image(widget.size(),QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);widget.render(&image);
        QVERIFY(image.save(dir+"/profile-continuous-surface.png"));
    }
}

void Test2DResultsVizFixes::smoothMaximumContainsEveryFrame()
{
    SWMM2DResultsLayer layer;
    auto source=std::make_unique<VfrSource>();auto* raw=source.get();
    source->z.assign(4,0);
    source->frames={{1.0f,0.0f},{0.0f,0.2f}};
    layer.setSource(std::move(source));
    const auto maximum=layer.maxSurfaceDepths();
    for (int frame=0;frame<2;++frame) {
        layer.setCurrentTimeIndex(frame);
        for (int tri=0;tri<2;++tri) {
            const QPointF p=layer.m_sceneTris[tri].centroid;
            const double now=layer.signedDepthAtDisplayTriangle(tri,p);
            const double peak=layer.signedDepthAtDisplayTriangle(tri,p,maximum);
            if (std::isfinite(now)) QVERIFY(peak>=now-1e-12);
        }
    }
    raw->frames[1][1]=0.05f;
    const auto reduced=layer.maxSurfaceDepths();
    QVERIFY(std::abs(reduced[1][0]-0.05)<1e-7);
    QVERIFY(std::abs(reduced[0][0]-1.0)<1e-12);
}

void Test2DResultsVizFixes::mapFillsStopAtExactShoreline()
{
    class Renderer : public SWMM2DResultsQSGRenderer {
    public:
        QSGNode* sync(QSGNode* old=nullptr) { return updatePaintNode(old,nullptr); }
    };
    SWMM2DResultsLayer layer;
    auto source=std::make_unique<VfrSource>(); auto* raw=source.get();
    layer.setSource(std::move(source));
    layer.setVisible(true);
    for (int pass=0;pass<4;++pass) {
        raw->frames[0][0]=0.4921875f;
        layer.refreshCurrentFrame();
        for (auto* sub : layer.sublayers()) sub->setVisible(false);
        if (pass<2) {
            layer.contourBandSublayer()->setVisible(true);
            layer.contourBandSublayer()->bandStyle()->setSmoothBands(pass==0);
        } else if (pass==2) layer.cellDepthFillSublayer()->setVisible(true);
        else layer.smoothDepthFillSublayer()->setVisible(true);
        Renderer renderer;
        renderer.setWidth(400); renderer.setHeight(400);
        renderer.setMapExtent(MapExtent(0,0,1,1)); renderer.setLayer(&layer);
        QSignalSpy ready(&renderer,&SWMM2DResultsQSGRenderer::contentReady);
        std::unique_ptr<QSGNode> root(renderer.sync());
        QVERIFY(root);
        auto measure=[&]() {
            double area=0,minY=1;
            for (QSGNode* child=root->firstChild();child;child=child->nextSibling()) {
                if (child->type()!=QSGNode::GeometryNodeType) continue;
                const auto* g=static_cast<QSGGeometryNode*>(child)->geometry();
                if (!g || g->vertexCount()==0 || g->attributeCount()!=2) continue;
                const auto* v=g->vertexDataAsColoredPoint2D();
                for (int i=0;i<g->vertexCount();i+=3) {
                    for (int k=0;k<3;++k) minY=std::min(minY,double(v[i+k].y));
                    area+=0.5*std::abs(double(v[i+1].x-v[i].x)*(v[i+2].y-v[i].y)
                        - double(v[i+1].y-v[i].y)*(v[i+2].x-v[i].x));
                }
            }
            return std::make_pair(area,minY);
        };
        const double shore=(1.5-layer.dryDepth())/4.0;
        auto [area,minY]=measure();
        QVERIFY2(std::abs(minY-(0.5-shore))<1e-6,"Map pass extended uphill past its wet boundary");
        QVERIFY2(std::abs(area-(shore-0.5*shore*shore))<1e-6,"Map pass filled dry terrain or drew overlapping base water");
        // Same timestamp, lower stored volume: the renderer must discard
        // cached contours even though its time index and ramp are unchanged.
        raw->frames[0][0]=0.1f;
        layer.refreshCurrentFrame();
        root.reset(renderer.sync(root.release()));
        if (pass==0 && qEnvironmentVariableIntValue("OPENSWMM_QSG_ASYNC_CONTOURS")==1) {
            // Keep the complete previous shoreline until the replacement is ready.
            QCOMPARE(measure(),std::make_pair(area,minY));
            QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,5000);
            root.reset(renderer.sync(root.release()));
        }
        const auto smaller=measure();
        QVERIFY(smaller.first<area);
        QVERIFY(smaller.second>minY);
    }
}

void Test2DResultsVizFixes::contourAnimationPublishesCompleteFrames()
{
    if (qEnvironmentVariableIntValue("OPENSWMM_QSG_ASYNC_CONTOURS")!=1)
        QSKIP("Run with OPENSWMM_QSG_ASYNC_CONTOURS=1 to exercise worker handoff");
    class Renderer : public SWMM2DResultsQSGRenderer {
    public:
        QSGNode* sync(QSGNode* old=nullptr) { return updatePaintNode(old,nullptr); }
    };
    SWMM2DResultsLayer layer;
    auto source=std::make_unique<FlowVfrSource>(); auto* raw=source.get();
    layer.setSource(std::move(source));
    layer.setMaxDepth(2.0); // fixed scale also permits dry/re-wet frames below
    layer.setVisible(true);
    for (auto* sub : layer.sublayers()) sub->setVisible(false);
    layer.contourBandSublayer()->setVisible(true);
    layer.isolineSublayer()->setVisible(true);
    layer.isolineSublayer()->isolineStyle()->setLabels(false);
    layer.velocityVectorSublayer()->setVisible(true);
    layer.smoothDepthFillSublayer()->setVisible(true); // must use the same frame
    Renderer renderer;
    renderer.setWidth(400); renderer.setHeight(400);
    renderer.setMapExtent(MapExtent(0,0,1,1)); renderer.setLayer(&layer);
    QSignalSpy ready(&renderer,&SWMM2DResultsQSGRenderer::contentReady);
    std::unique_ptr<QSGNode> root(renderer.sync());
    auto signature=[&](bool colored) {
        QByteArray bytes;
        for (auto* child=root->firstChild(); child; child=child->nextSibling()) {
            if (child->type()!=QSGNode::GeometryNodeType) continue;
            const auto* g=static_cast<QSGGeometryNode*>(child)->geometry();
            if (!g || (g->attributeCount()==2)!=colored) continue;
            bytes.append(reinterpret_cast<const char*>(g->vertexData()),
                         g->vertexCount()*g->sizeOfVertex());
        }
        return bytes;
    };
    const auto initialFill=signature(true), initialLines=signature(false);
    QVERIFY(!initialFill.isEmpty()); QVERIFY(!initialLines.isEmpty());
    QCOMPARE(renderer.displayedFrameRevision(),layer.frameRevision());
    raw->speed=-1.0f;
    raw->frames[0][0]=0.1f; layer.refreshCurrentFrame();
    const auto firstReplacement=layer.frameRevision();
    root.reset(renderer.sync(root.release()));
    QCOMPARE(signature(true),initialFill); QCOMPARE(signature(false),initialLines);
    renderer.setMapExtent(MapExtent(0.2,0,1.2,1));
    root.reset(renderer.sync(root.release()));
    QCOMPARE(signature(true),initialFill); QCOMPARE(signature(false),initialLines);
    renderer.setMapExtent(MapExtent(0,0,1,1));
    // A faster producer must neither queue every snapshot nor starve the map.
    raw->frames[0][0]=0.25f; layer.refreshCurrentFrame();
    root.reset(renderer.sync(root.release()));
    raw->frames[0][0]=0.35f; layer.refreshCurrentFrame();
    root.reset(renderer.sync(root.release()));
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,5000);
    root.reset(renderer.sync(root.release()));
    QCOMPARE(renderer.displayedFrameRevision(),firstReplacement);
    QVERIFY(signature(true)!=initialFill); QVERIFY(signature(false)!=initialLines);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>1,5000);
    root.reset(renderer.sync(root.release()));
    QCOMPARE(renderer.displayedFrameRevision(),layer.frameRevision());
    // Completed empty geometry is a real dry frame, never a cache miss.
    raw->frames[0][0]=0; layer.refreshCurrentFrame();
    root.reset(renderer.sync(root.release()));
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>2,5000);
    root.reset(renderer.sync(root.release()));
    QVERIFY(signature(true).isEmpty()); QVERIFY(signature(false).isEmpty());
    QCOMPARE(renderer.displayedFrameRevision(),layer.frameRevision());
    // Style edits bootstrap the requested frame; a pending older result cannot undo it.
    raw->frames[0][0]=0.2f; layer.refreshCurrentFrame();
    root.reset(renderer.sync(root.release()));
    raw->frames[0][0]=0.4f; layer.refreshCurrentFrame();
    layer.contourBandSublayer()->bandStyle()->setBandCount(5);
    root.reset(renderer.sync(root.release()));
    const auto editedFill=signature(true), editedLines=signature(false);
    QCOMPARE(renderer.displayedFrameRevision(),layer.frameRevision());
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>3,5000);
    root.reset(renderer.sync(root.release()));
    QCOMPARE(signature(true),editedFill); QCOMPARE(signature(false),editedLines);
    // A layer switch while work is pending must reject the old mesh/source.
    raw->frames[0][0]=0.3f; layer.refreshCurrentFrame();
    root.reset(renderer.sync(root.release()));
    SWMM2DResultsLayer other;
    other.setSource(std::make_unique<VfrSource>()); other.setVisible(true);
    renderer.setLayer(&other);
    root.reset(renderer.sync(root.release()));
    const auto otherFill=signature(true), otherLines=signature(false);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>4,5000);
    root.reset(renderer.sync(root.release()));
    QCOMPARE(signature(true),otherFill); QCOMPARE(signature(false),otherLines);
}

void Test2DResultsVizFixes::depthClassificationRemainsStableDuringPlayback()
{
    using namespace OpenSWMM::Render;
    auto source=std::make_unique<VfrSource>();
    source->frames={{0.8f,0.0f},{0.1f,0.15f}};
    SWMM2DResultsLayer layer;
    layer.setSource(std::move(source));
    const double maximum=layer.maxDepth();
    QVERIFY(maximum>0.8); // surface depth, not the cell mean
    ClassificationScheme scheme;
    scheme.setMethod(BinMethod::Quantile);
    scheme.setClassCount(3);
    const auto samples=layer.depthClassificationSamples(scheme);
    const auto edges=scheme.levelEdges(layer.dryDepth(),maximum,samples);
    for (int t : {0,1,0,1}) {
        layer.setCurrentTimeIndex(t);
        QCOMPARE(layer.maxDepth(),maximum);
        QCOMPARE(layer.depthClassificationSamples(scheme),samples);
        QCOMPARE(scheme.levelEdges(layer.dryDepth(),maximum,
            layer.depthClassificationSamples(scheme)),edges);
        for (const auto& tri : layer.m_sceneTris)
            for (float d : {tri.dv0,tri.dv1,tri.dv2})
                if (std::isfinite(d)) QVERIFY(d<=maximum);
    }
    scheme.setRangeMode(RangeMode::PerFrameAutoStretch);
    const auto perFrame=layer.depthClassificationSamples(scheme);
    layer.setCurrentTimeIndex(0);
    QVERIFY(layer.depthClassificationSamples(scheme)!=perFrame);
    layer.setMaxDepth(7.0); layer.setCurrentTimeIndex(1);
    QCOMPARE(layer.maxDepth(),7.0); // explicit user range is preserved
}

void Test2DResultsVizFixes::cpuContoursStopAtExactShoreline()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<VfrSource>());
    layer.setVisible(true);
    for (auto* sub : layer.sublayers()) sub->setVisible(false);
    layer.contourBandSublayer()->setVisible(true);
    QGraphicsScene scene;
    layer.populateScene(&scene,MapExtent(0,0,1,1),nullptr);
    for (bool smooth : {false,true}) {
        layer.contourBandSublayer()->bandStyle()->setSmoothBands(smooth);
        QImage image(400,400,QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        scene.render(&painter,QRectF(0,0,400,400),QRectF(0,-1,1,1));
        painter.end();
        QVERIFY(image.pixelColor(100,320).alpha()>0); // y=.2, wet
        QCOMPARE(image.pixelColor(100,240).alpha(),0); // y=.4, uphill/dry
        QCOMPARE(image.pixelColor(360,320).alpha(),0); // dry adjacent cell
        const QString dir=qEnvironmentVariable("VFR_TEST_ARTIFACTS");
        if (!dir.isEmpty()) QVERIFY(image.save(dir+(smooth ? "/map-smooth-bands.png" : "/map-flat-bands.png")));
    }
    layer.depopulateScene(&scene);
}

void Test2DResultsVizFixes::boundaryProfileKeepsWetSideAcrossFrames()
{
    auto source=std::make_unique<VfrSource>();
    source->frames={{0,0.0703125f},{0.4921875f,0},{0,0},{0.4921875f,0.0703125f}};
    SWMM2DResultsLayer layer; layer.setSource(std::move(source));
    for (const QVector<QPointF> path : {QVector<QPointF>{{1,0},{0,-1}},
                                       QVector<QPointF>{{0,-1},{1,0}}}) {
        layer.setCurrentTimeIndex(0);
        const auto profile=MeshProfileSampler::buildMeshProfile(nullptr,&layer,path);
        QCOMPARE(profile.samples.size(),2);
        for (const auto &s : profile.samples) QVERIFY(s.boundaryTriIdx>=0);
        for (int t : {0,1,2,3}) {
            layer.setCurrentTimeIndex(t);
            for (const auto &s : profile.samples) {
                const double d=MeshProfileSampler::signedWaterDepth(
                    &layer,s.displayTriIdx,s.boundaryTriIdx,s.scenePt);
                if (t==2) { QVERIFY(std::isnan(d)); continue; }
                // Exact lake-at-rest stage, even with a dry cell on one side
                // and independently when both cells have matching volumes.
                QVERIFY(std::abs(s.ground+d-1.5)<1e-6);
                QVERIFY(s.signedMaxDepth>=d-1e-6);
            }
        }
        layer.setCurrentTimeIndex(0);
        QCOMPARE(layer.depthAtCellInterp(0,{0.1,-0.1}),0.0f);
    }
}

void Test2DResultsVizFixes::contourRangesSaturateCpuAndQsg()
{
    class Renderer : public SWMM2DResultsQSGRenderer {
    public:
        QSGNode* sync(QSGNode* old=nullptr) { return updatePaintNode(old,nullptr); }
    };
    SWMM2DResultsLayer layer; layer.setSource(std::make_unique<VfrSource>());
    layer.setVisible(true);
    for (auto *sub:layer.sublayers()) sub->setVisible(false);
    auto *bands=layer.contourBandSublayer(); bands->setVisible(true);
    auto *style=bands->bandStyle(); style->setUseCustomRange(true);
    QGraphicsScene scene; layer.populateScene(&scene,MapExtent(0,0,1,1),nullptr);
    Renderer renderer;
    renderer.setWidth(400); renderer.setHeight(400);
    renderer.setMapExtent(MapExtent(0,0,1,1)); renderer.setLayer(&layer);
    std::unique_ptr<QSGNode> root;
    for (bool smooth : {false,true}) {
        style->setSmoothBands(smooth);
        // Above maximum, below minimum, and a color range wholly below dry cutoff.
        for (const auto range : {QPointF(0.1,0.2),QPointF(2,3),QPointF(0,0.00001)}) {
            style->setRangeMin(range.x()); style->setRangeMax(range.y());
            QImage img(400,400,QImage::Format_ARGB32_Premultiplied); img.fill(Qt::transparent);
            QPainter p(&img); scene.render(&p,QRectF(0,0,400,400),QRectF(0,-1,1,1)); p.end();
            const QColor actual=img.pixelColor(100,320); // depth 0.7, above max or below min
            QVERIFY(actual.alpha()>0);
            const int count=style->bandCount();
            const QColor expected=style->colorForBand(range.x()>1.5 ? 0 : count-1,count);
            QVERIFY(std::abs(actual.red()-expected.red())<=2);
            QVERIFY(std::abs(actual.green()-expected.green())<=2);
            QVERIFY(std::abs(actual.blue()-expected.blue())<=2);
            QVERIFY(img.pixelColor(100,260).alpha()>0); // shallow wet bank
            QCOMPARE(img.pixelColor(100,240).alpha(),0); // physical dry area still hidden
            root.reset(renderer.sync(root.release()));
            double area=0;
            for(auto *n=root->firstChild();n;n=n->nextSibling()) {
                if(n->type()!=QSGNode::GeometryNodeType) continue;
                auto *g=static_cast<QSGGeometryNode*>(n)->geometry();
                if(!g || g->attributeCount()!=2) continue;
                const auto *v=g->vertexDataAsColoredPoint2D();
                for(int i=0;i+2<g->vertexCount();i+=3)
                    area+=std::abs((v[i+1].x-v[i].x)*(v[i+2].y-v[i].y)
                                -(v[i+1].y-v[i].y)*(v[i+2].x-v[i].x))*0.5;
            }
            const double y=(1.5-layer.dryDepth())/4.0;
            QVERIFY2(std::abs(area-(y-y*y/2))<1e-6,"Color limits changed the wet area");
            const QString dir=qEnvironmentVariable("SWMMVIS_SHORELINE_ARTIFACT_DIR");
            if (!dir.isEmpty() && range==QPointF(0.1,0.2))
                QVERIFY(img.save(dir+(smooth?"/contours-saturated-smooth.png":"/contours-saturated-flat.png")));
        }
    }
    layer.depopulateScene(&scene);
}

void Test2DResultsVizFixes::cellVfrKeepsDryNeighborsDry()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<VfrSource>());
    layer.setCurrentTimeIndex(0);
    const auto& t=layer.m_sceneTris[0];
    QVERIFY(std::abs(t.dv0-1.5) < 1e-6);
    QVERIFY(std::abs(t.dv1-1.5) < 1e-6);
    QVERIFY(std::abs(t.dv2+2.5) < 1e-6);
    QCOMPARE(layer.depthAtCellInterp(0,{0.3,-0.4}),0.0f);
    QVERIFY(std::abs(layer.depthAtCellInterp(0,{0.35,-0.3})-0.3) < 1e-6);
    QVERIFY(!layer.cellHasSurface(1));
    QVERIFY(std::isnan(layer.m_sceneTris[1].dv0));
    QCOMPARE(layer.depthAtCellInterp(1,{0.9,-0.2}),0.0f);
    // The source's incompatible node field (100 m) is deliberately ignored.
    QVERIFY(layer.maxDepth() < 2.0);
}

void Test2DResultsVizFixes::exactProfileIgnoresStationSpacing()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<VfrSource>());
    layer.setCurrentTimeIndex(0);
    const QVector<QPointF> path{{0.25,0},{0.25,-1}};
    const auto coarse=MeshProfileSampler::buildMeshProfile(nullptr,&layer,path,100);
    const auto fine=MeshProfileSampler::buildMeshProfile(nullptr,&layer,path,0.0001);
    QVERIFY(coarse.exactWaterGeometry);
    QCOMPARE(coarse.samples.size(),fine.samples.size());
    QVERIFY(coarse.samples.size() >= 4);
    bool crossed=false;
    for (int i=1;i<coarse.samples.size();++i) {
        const auto& a=coarse.samples[i-1];const auto& b=coarse.samples[i];
        QCOMPARE(a.chainage,fine.samples[i-1].chainage);
        if (b.breakBefore) continue;
        double lo,hi;
        if (CellWaterGeometry::wetInterval(a.signedDepthNow,b.signedDepthNow,0,lo,hi)) {
            const double shoreline=a.chainage+hi*(b.chainage-a.chainage);
            QVERIFY(std::abs(shoreline-0.375) < 1e-6);
            crossed=true;
        }
    }
    QVERIFY(crossed);
    const auto offMesh=MeshProfileSampler::buildMeshProfile(nullptr,&layer,{{0.25,0.25},{0.25,-1.25}},100);
    QCOMPARE(offMesh.samples.first().chainage,0.0);
    QCOMPARE(offMesh.samples.last().chainage,1.5);
    QVERIFY(std::isnan(offMesh.samples.first().ground));
    QVERIFY(std::isnan(offMesh.samples.last().ground));
    const auto edge=MeshProfileSampler::buildMeshProfile(nullptr,&layer,{{0,-1},{1,0}},100);
    const auto edgeReverse=MeshProfileSampler::buildMeshProfile(nullptr,&layer,{{1,0},{0,-1}},100);
    QCOMPARE(edge.samples.size(),2);
    QCOMPARE(edgeReverse.samples.size(),2);
    for (int i=0;i<2;++i) {
        QCOMPARE(edge.samples[i].triIdx,0);
        QCOMPARE(edgeReverse.samples[i].triIdx,0);
        QVERIFY(std::abs(edge.samples[i].signedDepthNow-edgeReverse.samples[1-i].signedDepthNow)<1e-12);
    }
    const auto reverse=MeshProfileSampler::buildMeshProfile(nullptr,&layer,{{0.25,-1},{0.25,0}},100);
    QCOMPARE(reverse.samples.size(),coarse.samples.size());
    for (int i=0;i<coarse.samples.size();++i)
        QVERIFY(std::abs(coarse.samples[i].ground-reverse.samples[reverse.samples.size()-1-i].ground) < 1e-10);
}

void Test2DResultsVizFixes::latestFrameReplacementCanReduceEnvelope()
{
    SWMM2DResultsLayer layer;
    auto source=std::make_unique<VfrSource>();auto* raw=source.get();
    raw->frames={{0.1f,0.0f},{0.8f,0.0f}};
    layer.setSource(std::move(source));
    QCOMPARE(layer.maxDepthPerCell()[0],0.8f);
    raw->frames[1][0]=0.2f;
    QCOMPARE(layer.maxDepthPerCell()[0],0.2f);
    const auto before=layer.frameRevision();
    layer.refreshCurrentFrame();
    QVERIFY(layer.frameRevision()>before);
    raw->frames.push_back({0.05f,0.1f});
    QCOMPARE(layer.maxDepthPerCell()[0],0.2f);
    QCOMPARE(layer.maxDepthPerCell()[1],0.1f);
}

// Tri-quad G1/G4 — a quad is displayed as its two VFR sub-triangles, both
// carrying the QUAD's value and both hit-testing back to the quad's cell
// index; the wireframe strokes the true quad boundary (no diagonal); and the
// vertex reconstruction weights the quad's vote by 3/4 of a triangle's
// (the plan's 1/nv rule, normalised so triangles are unchanged).
void Test2DResultsVizFixes::mixedMeshQuadRendersAsFanOfItsCell()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<FakeMixedSource>(/*quad=*/0.4f, /*tri=*/0.2f));
    layer.setCurrentTimeIndex(0);

    // Fan: 2 sub-triangles for the quad + 1 for the triangle; cells stay 2.
    QCOMPARE(layer.cellCount(), 2);
    QCOMPARE(layer.m_sceneTris.size(), 3);
    QCOMPARE(layer.triVertexIndices().size(), size_t(3));
    QCOMPARE(layer.triCellMap(), (std::vector<int>{0, 0, 1}));
    QCOMPARE(layer.cellTriRange(), (std::vector<int>{0, 2, 3}));

    // Both halves of the quad answer with cell 0 (scene y = -model y).
    const QPointF inLowerHalf(0.25, -0.25);
    const QPointF inUpperHalf(0.75, -0.75);
    QCOMPARE(layer.pickCellAt(inLowerHalf), 0);
    QCOMPARE(layer.pickCellAt(inUpperHalf), 0);
    QCOMPARE(layer.pickCellAt(QPointF(1.5, -0.5)), 1);

    // ...and both read the quad's cell value.
    QCOMPARE(layer.depthAtSceneNow(inLowerHalf), 0.4f);
    QCOMPARE(layer.depthAtSceneNow(inUpperHalf), 0.4f);
    QCOMPARE(layer.depthAtSceneNow(QPointF(1.5, -0.5)), 0.2f);
    QVERIFY(layer.cellHasSurface(0));
    QVERIFY(layer.cellHasSurface(1));

    // Wireframe: 4 quad edges + 3 triangle edges − 1 shared = 6, no diagonal.
    QCOMPARE(layer.m_sceneEdges.size(), 6);

    // The wet shared edge has one smooth stage from the two cell supports.
    // Stored cell means above remain unchanged.
    const float atShared   = layer.depthAtSceneInterp(QPointF(1.0, 0.0));
    const float atQuadOnly = layer.depthAtSceneInterp(QPointF(0.0, 0.0));
    QVERIFY(std::abs(atShared   - 0.32f) < 1e-5f);
    QVERIFY(std::abs(atQuadOnly - 0.40f) < 1e-5f);

    // Rect pick answers in cell indices, once per cell.
    const QVector<int> picked = layer.pickCellsInRect(QRectF(-1.0, -2.0, 4.0, 3.0));
    QCOMPARE(picked, (QVector<int>{0, 1}));
}

// Issue #155 — the source hands over SI metres; a foot-based model CRS needs
// them divided by metres_per_model_unit before they mean anything on a canvas
// in that CRS. The unit square (0..1 m) must land at 0..3.2808 ft, matching
// where the .2dm-backed SWMM2DMeshLayer draws the same terrain.
void Test2DResultsVizFixes::declaredMetreFactorScalesToModelUnits()
{
    auto src = std::make_unique<FakeSource>(false, std::vector<float>{0.5f});
    src->declareCoordinateReference(QStringLiteral("EPSG:2249"), 0.3048);

    SWMM2DResultsLayer layer;
    layer.setSource(std::move(src));

    const MapExtent e = layer.extent();
    QVERIFY(qAbs(e.width()  - 1.0 / 0.3048) < 1e-9);
    QVERIFY(qAbs(e.height() - 1.0 / 0.3048) < 1e-9);
}

// The same path must be a no-op for a metric model — a factor of 1.0 leaves
// the coordinates exactly as stored, with no round-trip drift.
void Test2DResultsVizFixes::metricModelIsUnscaled()
{
    auto src = std::make_unique<FakeSource>(false, std::vector<float>{0.5f});
    src->declareCoordinateReference(QStringLiteral("EPSG:26986"), 1.0);

    SWMM2DResultsLayer layer;
    layer.setSource(std::move(src));

    const MapExtent e = layer.extent();
    QCOMPARE(e.width(),  1.0);
    QCOMPARE(e.height(), 1.0);
}

// A pre-6.0 .2d.h5 and the live engine source declare nothing, so the caller
// supplies the factor (swmmvis.cpp derives it from FLOW_UNITS + the mesh's
// `;; UNITS:` header, the same rule the engine applies).
void Test2DResultsVizFixes::undeclaredSourceUsesCallerFallback()
{
    SWMM2DResultsLayer layer;
    layer.setFallbackCoordinateScale(0.3048);
    layer.setSource(std::make_unique<FakeSource>(false, std::vector<float>{0.5f}));

    const MapExtent e = layer.extent();
    QVERIFY(qAbs(e.width() - 1.0 / 0.3048) < 1e-9);
}

// With no declaration and no fallback the layer must leave the coordinates
// alone — the pre-#155 behaviour, which at least agrees with SWMM2DMeshLayer.
// Guessing here (e.g. from the layer CRS's linear unit) would invent a new
// offset on models whose mesh units and CRS units disagree.
void Test2DResultsVizFixes::undeclaredSourceDefaultsToNoScaling()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<FakeSource>(false, std::vector<float>{0.5f}));

    const MapExtent e = layer.extent();
    QCOMPARE(e.width(),  1.0);
    QCOMPARE(e.height(), 1.0);
}

// Issue 3 — a 2-triangle mesh sharing one diagonal has 5 unique edges
// (4 boundary + 1 diagonal), not 6 (2 tris × 3). The dedup is what stops the
// shared edge being stroked twice (the dark-edge artifact).
void Test2DResultsVizFixes::edgesAreDeduplicated()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<FakeSource>(false, std::vector<float>{0.5f}));
    QCOMPARE(layer.m_sceneEdges.size(), 5);
}

// Issue 2 — scrubbing a live source to an earlier frame clears follow-live, so
// a subsequent refreshTimeRange() (the per-tick live ingest) does NOT snap the
// cursor back to the newest frame.
void Test2DResultsVizFixes::liveScrubHoldsFrame()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<FakeSource>(
        /*live=*/true, std::vector<float>{0.1f, 0.5f, 1.0f}));

    // Seed at the newest frame (follow-live armed by default).
    layer.refreshTimeRange();
    QVERIFY(layer.followLive());

    // User scrubs back to frame 1 (not the last) → follow-live clears.
    layer.setCurrentSimTime(layer.source()->simTimeAt(1));
    QCOMPARE(layer.currentTimeIndex(), 1);
    QVERIFY(!layer.followLive());

    // The next live tick must NOT advance the held frame.
    layer.refreshTimeRange();
    QCOMPARE(layer.currentTimeIndex(), 1);
}

void Test2DResultsVizFixes::liveSeekToLastReArmsFollow()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<FakeSource>(
        /*live=*/true, std::vector<float>{0.1f, 0.5f, 1.0f}));

    layer.setCurrentSimTime(layer.source()->simTimeAt(0));
    QVERIFY(!layer.followLive());

    // Seeking to the latest frame re-subscribes to live.
    layer.setCurrentSimTime(layer.source()->simTimeAt(2));
    QCOMPARE(layer.currentTimeIndex(), 2);
    QVERIFY(layer.followLive());
}

// Issue 4 — at the peak frame the animated interpolated depth equals the
// max-depth envelope sample at the same point (both reduced through the shared
// reconstruction). All cells peak at the last frame in this fixture.
void Test2DResultsVizFixes::maxDepthMatchesAnimatedAtPeak()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<FakeSource>(
        /*live=*/false, std::vector<float>{0.1f, 0.5f, 1.0f}));

    const QVector<float> vertMax = layer.maxDepthPerVertex();
    QCOMPARE(vertMax.size(), 4);

    // Show the peak frame (index 2). A point inside tri0 (centroid ~ (0.67,0.33)
    // → scene y is flipped, so use a point we know lies in the mesh bbox).
    layer.setCurrentTimeIndex(2);
    const QPointF p(0.6, -0.3);   // scene space: y = -modelY (see rebuildSceneGeometry_)

    const float animated = layer.depthAtSceneInterp(p);
    const float envelope = layer.maxDepthAtSceneInterp(p, vertMax);
    QVERIFY(animated > 0.0f);                       // point is wet
    QVERIFY(std::abs(animated - envelope) < 1e-4f); // consistent at the peak
}

// Issue 5 — with no edge-flux data the velocity field is empty, so
// velocityAtScene reports "no flow" everywhere, and an off-mesh point is false.
void Test2DResultsVizFixes::velocityFalseWithoutFlux()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<FakeSource>(false, std::vector<float>{0.5f}));
    layer.setCurrentTimeIndex(0);

    float vx = 1.0f, vy = 1.0f;
    QVERIFY(!layer.velocityAtScene(QPointF(0.5, -0.5), vx, vy));  // in-mesh, no flux
    QCOMPARE(vx, 0.0f);
    QCOMPARE(vy, 0.0f);
    QVERIFY(!layer.velocityAtScene(QPointF(99.0, 99.0), vx, vy)); // off-mesh
}

QTEST_MAIN(Test2DResultsVizFixes)
#include "test_2dresults_vizfixes.moc"
