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
#include "ui/dialogs/swmm2dresultsstylepanel.h"
#include "ui/dialogs/layerstyledialog.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QUndoStack>
#include "map/swmm2dresultsqsgrenderer.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/scalarfillsublayer.h"
#include "render/contourjob.h"
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QSGGeometryNode>
#include <QGraphicsScene>
#include <QPainter>
#include <QQuickWindow>
#include <QSurfaceFormat>

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

// Shallow sheet flowing down ten sloping triangles. Horizontal per-cell VFR
// leaves disconnected low-corner wedges despite flow across every cell edge.
class FlowingSlopeSource : public IMesh2DSource {
public:
    bool fluxEnabled=true;
    std::vector<float> frames{0.04f,0.08f,0.12f};
    int vertexCount() const override { return 12; }
    int triangleCount() const override { return 10; }
    int timeCount() const override { return int(frames.size()); }
    bool readMeshGeometry(std::vector<double>& x,std::vector<double>& y,
        std::vector<double>& z,std::vector<std::array<int,3>>& tris) override {
        x.clear();y.clear();z.clear();tris.clear();
        for(int j=0;j<=5;++j) for(int k=0;k<2;++k) {
            x.push_back(10*j);y.push_back(10*k);z.push_back(27-0.7*j);
        }
        for(int j=0;j<5;++j) {
            tris.push_back({2*j,2*j+2,2*j+3}); tris.push_back({2*j,2*j+3,2*j+1});
        }
        return true;
    }
    bool readDepthsAt(int t,std::vector<float>& d) override {
        if(t<0 || t>=timeCount())return false;
        d.assign(10,frames[size_t(t)]);return true;
    }
    bool readEdgeFluxAt(int t,std::vector<float>& q) override {
        if(!fluxEnabled || t<0 || t>=timeCount())return false;
        q.assign(40,0);
        for(int c=0;c<10;c+=2) {
            q[4*c]=1;q[4*c+1]=-1;
            q[4*(c+1)+1]=-1;q[4*(c+1)+2]=1;
        }
        return true;
    }
    QDateTime simTimeAt(int t) const override {
        return QDateTime(QDate(2026,1,1),QTime(0,0)).addSecs(t);
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
    void largeContoursNeverMarchDuringSync();
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
    void thinFilmPolicyPreservesRawWaterAndPartialPools();
    void thinFilmSettingsRoundTrip();
    void thinFilmVisibilityCpuQsgAndPendingFrames();
    void thinFilmSavedAndLiveParity();
    void flowingSlopeProfilesContoursAndHistory();
    void bellingeNeighborhoodSavedLiveAndRendering();
    void bellingePerfBaseline();
    void surfaceWorkerMatchesSynchronousFit();
    void surfaceWorkerPublishesInRequestOrder();
    void surfaceWorkerEnvelopeAndStations();
    void surfaceWorkerLimitsLiveFits();
    void bellingeWorkerEqualityAndLatency();
};

void Test2DResultsVizFixes::bellingeNeighborhoodSavedLiveAndRendering()
{
    const QString path=qEnvironmentVariable("VFR_TEST_BELLINGE");
    if(path.isEmpty())QSKIP("Set VFR_TEST_BELLINGE to the preserved partial output");
    const QString dir=qEnvironmentVariable("VFR_TEST_ARTIFACTS");
    auto file=std::make_unique<HDF5Mesh2DSource>();QVERIFY(file->open(path));
    std::vector<double> x,y,z;std::vector<std::array<int,4>> cells;
    QVERIFY(file->readCells(x,y,z,cells));QVERIFY(cells.size()>40000);
    const int last=file->timeCount()-1;QVERIFY(last>=11);
    std::vector<float> depths,flux;
    QVERIFY(file->readDepthsAt(last,depths));QVERIFY(file->readEdgeFluxAt(last,flux));
    auto live=std::make_unique<EngineMesh2DSource>(x,y,z,cells);
    live->pushDepths(depths,file->simTimeAt(last),0);live->pushFlux(flux,file->simTimeAt(last),0);
    SWMM2DResultsLayer saved,stream;
    saved.setSource(std::move(file));stream.setSource(std::move(live));
    for(auto* layer:{&saved,&stream})layer->setDryDepth(0.0001);
    saved.setCurrentTimeIndex(last);stream.setCurrentTimeIndex(0);
    const auto reference=saved.m_sceneTris;
    QCOMPARE(reference.size(),stream.m_sceneTris.size());
    for(int i=0;i<reference.size();++i) {
        const auto& a=reference[i];const auto& b=stream.m_sceneTris[i];
        for(auto pair:{std::pair{a.dv0,b.dv0},std::pair{a.dv1,b.dv1},std::pair{a.dv2,b.dv2}})
            QVERIFY((std::isnan(pair.first)&&std::isnan(pair.second)) || pair.first==pair.second);
    }
    // Return to the same frame after reverse seeking: no accumulated smoothing.
    for(int frame:{3,last,8,last})saved.setCurrentTimeIndex(frame);
    for(int i=0;i<reference.size();++i) {
        const auto& a=reference[i];const auto& b=saved.m_sceneTris[i];
        for(auto pair:{std::pair{a.dv0,b.dv0},std::pair{a.dv1,b.dv1},std::pair{a.dv2,b.dv2}})
            QVERIFY((std::isnan(pair.first)&&std::isnan(pair.second)) || pair.first==pair.second);
    }
    std::vector<float> after;QVERIFY(saved.source()->readDepthsAt(last,after));QCOMPARE(after,depths);
    // A triangle's scene depths are the same shared signed field used by queries
    // and profiles. Compare both traces of every visible interior edge.
    std::vector<VertexDepthReconstruct::CellSplit> splits(cells.size());
    for(size_t c=0;c<cells.size();++c)splits[c].v=cells[c];
    const auto topology=CellWaterGeometry::smoothTopology(splits);
    auto corner=[&](int c) {
        const auto& t=reference[c/4];return c%4==0?t.dv0:(c%4==1?t.dv1:t.dv2);
    };
    int shared=0;
    for(const auto& e:topology.edges) {
        if(!std::isfinite(corner(e.a0)) || !std::isfinite(corner(e.a1)))continue;
        QCOMPARE(corner(e.a0),corner(e.a1));QCOMPARE(corner(e.b0),corner(e.b1));++shared;
    }
    qInfo()<<"Matching visible Bellinge shared edges:"<<shared;
    QVERIFY(shared>1000);
    const MapExtent extent(583950,6132800,584550,6133150);
    const QRectF sourceRect(583950,-6133150,600,350);
    const QVector<QPointF> section{{583970,-6132980},{584530,-6132980}};
    auto profile=MeshProfileSampler::buildMeshProfile(nullptr,&saved,section,100);
    QVERIFY(profile.samples.size()>20);
    for(const auto& s:profile.samples) {
        if(s.displayTriIdx<0)continue; // off-mesh end markers have no surface
        double q=saved.signedDepthAtDisplayTriangle(s.displayTriIdx,s.scenePt);
        if(!std::isfinite(q))q=saved.signedDepthAtDisplayTriangle(s.boundaryTriIdx,s.scenePt);
        q*=saved.depthToMeshUnits();
        QVERIFY((std::isnan(q)&&std::isnan(s.signedDepthNow)) || std::abs(q-s.signedDepthNow)<1e-10);
    }
    if(dir.isEmpty())return;
    saved.setVisible(true);
    for(auto* sub:saved.sublayers())sub->setVisible(false);
    auto* band=saved.contourBandSublayer();band->setVisible(true);band->setOpacity(0.6);
    QGraphicsScene scene;saved.populateScene(&scene,extent,nullptr);
    MeshProfilePlotOptions options;options.setShowTimeLabel(false);
    MeshProfilePlotWidget plot;plot.setOptions(&options);plot.resize(1200,450);
    for(bool smooth:{true,false}) {
        band->bandStyle()->setSmoothBands(smooth);
        for(int frame:{3,8,last}) {
            saved.setCurrentTimeIndex(frame);
            QImage map(1200,700,QImage::Format_ARGB32_Premultiplied);map.fill(Qt::white);
            QPainter painter(&map);scene.render(&painter,QRectF(0,0,1200,700),sourceRect);painter.end();
            const QString stem=QString("/bellinge-%1-%2").arg(smooth?"smooth":"bands").arg(frame);
            QVERIFY(map.save(dir+stem+"-cpu.png"));
            if(smooth) {
                plot.setProfile(MeshProfileSampler::buildMeshProfile(nullptr,&saved,section,100));
                QImage image(plot.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);plot.render(&image);
                QVERIFY(image.save(dir+stem+"-profile.png"));
            }
            if(qEnvironmentVariableIntValue("VFR_TEST_GPU")==1) {
                QQuickWindow window;QSurfaceFormat format=window.format();format.setSamples(4);window.setFormat(format);
                window.setColor(Qt::white);window.resize(1200,700);
                auto* renderer=new SWMM2DResultsQSGRenderer(window.contentItem());
                renderer->setSize(QSizeF(1200,700));renderer->setLayer(&saved);renderer->setMapExtent(extent);
                window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
                QTRY_COMPARE_WITH_TIMEOUT(renderer->displayedFrameRevision(),saved.frameRevision(),10000);
                const auto image=window.grabWindow();QVERIFY(!image.isNull());
                QVERIFY(image.save(dir+stem+"-gpu.png"));
                if(smooth && frame==last) {
                    // The full mesh exercises multiple GPU geometry chunks;
                    // a small region cannot expose large-buffer raster defects.
                    const auto box=saved.m_sceneBBox;
                    QSignalSpy swapped(&window,&QQuickWindow::frameSwapped);
                    renderer->setMapExtent(MapExtent(box.left(),-box.bottom(),box.right(),-box.top()));
                    QTRY_VERIFY_WITH_TIMEOUT(swapped.count()>0,10000);
                    const auto whole=window.grabWindow();QVERIFY(!whole.isNull());
                    QVERIFY(whole.save(dir+"/bellinge-whole-gpu.png"));
                    QImage cpu(1200,700,QImage::Format_ARGB32_Premultiplied);cpu.fill(Qt::white);
                    QPainter full(&cpu);scene.render(&full,QRectF(0,0,1200,700),box,Qt::IgnoreAspectRatio);full.end();
                    QVERIFY(cpu.save(dir+"/bellinge-whole-cpu.png"));
                }
                window.close();
            }
        }
    }
}

// Layer-level timing on the preserved Bellinge output. Self-skips unless both
// VFR_TEST_BELLINGE (fixture) and VFR_PERF_OUT (a reviewable output directory)
// are set. See workplans/2D_VFR_PERF_RECOVERY_PLAN_2026-10-02.md (M2).
void Test2DResultsVizFixes::bellingePerfBaseline()
{
    const QString path=qEnvironmentVariable("VFR_TEST_BELLINGE"),out=qEnvironmentVariable("VFR_PERF_OUT");
    if(path.isEmpty() || out.isEmpty())QSKIP("Set VFR_TEST_BELLINGE and VFR_PERF_OUT");
    QFile csv(out+"/layer-perf.csv");QVERIFY(csv.open(QIODevice::WriteOnly|QIODevice::Text));
    QTextStream row(&csv);row<<"metric,value_ms,count\n";
    QElapsedTimer timer;
    auto ms=[&timer]{return double(timer.nsecsElapsed())/1e6;};
    {
        // What opening a profile pays on a fresh layer.
        auto file=std::make_unique<HDF5Mesh2DSource>();QVERIFY(file->open(path));
        SWMM2DResultsLayer layer;layer.setSource(std::move(file));layer.setDryDepth(0.0001);
        const int last=layer.source()->timeCount()-1;layer.setCurrentTimeIndex(last);
        timer.start();const auto cold=layer.maxSurfaceDepths();const double coldMs=ms();
        timer.start();const auto warm=layer.maxSurfaceDepths();const double warmMs=ms();
        QCOMPARE(cold.size(),warm.size());
        row<<"envelope_cold,"<<coldMs<<","<<last+1<<"\n"<<"envelope_warm,"<<warmMs<<",1\n";
        const auto box=layer.m_sceneBBox;const double cx=box.center().x(),cy=box.center().y();
        const QVector<QPointF> sections[2]={{{583970,-6132980},{584530,-6132980}},
            {{std::max(box.left(),cx-1000),cy},{std::min(box.right(),cx+1000),cy}}};
        const char* names[2]={"profile_560m","profile_2km"};
        for(int i=0;i<2;++i) {
            timer.start();const auto profile=MeshProfileSampler::buildMeshProfile(nullptr,&layer,sections[i],100);
            const double buildMs=ms();
            timer.start();double sum=0;
            for(const auto& s:profile.samples) {
                if(s.displayTriIdx<0)continue;
                const double q=MeshProfileSampler::signedWaterDepth(&layer,s.displayTriIdx,s.boundaryTriIdx,s.scenePt);
                if(std::isfinite(q))sum+=q;
            }
            const double refreshMs=ms();Q_UNUSED(sum)
            row<<names[i]<<"_build,"<<buildMs<<","<<profile.samples.size()<<"\n"
               <<names[i]<<"_station_refresh,"<<refreshMs<<","<<profile.samples.size()<<"\n";
        }
        // Whole-mesh contour march of the current frame, as the scene-graph
        // sync pays it for meshes below the background-marching threshold.
        const auto& tris=layer.m_sceneTris;
        auto positions=std::make_shared<std::vector<OpenSWMM::Render::ContourJobInput::TriPos>>(size_t(tris.size()));
        auto scalars=std::make_shared<std::vector<std::array<float,3>>>(size_t(tris.size()));
        for(int i=0;i<tris.size();++i) {
            const auto& t=tris[i];
            (*positions)[size_t(i)]={float(t.a.x()-box.left()),float(t.a.y()-box.top()),float(t.b.x()-box.left()),
                float(t.b.y()-box.top()),float(t.c.x()-box.left()),float(t.c.y()-box.top())};
            (*scalars)[size_t(i)]={t.dv0,t.dv1,t.dv2};
        }
        OpenSWMM::Render::ContourJobInput input;input.positions=positions;input.scalars=scalars;
        input.minimumVisibleValue=0.0;
        const double maxDepth=std::max(layer.maxDepth(),1e-9);
        const auto& scheme=layer.contourBandSublayer()->bandStyle()->scheme();
        const auto edges=scheme.levelEdges(0.0,maxDepth,layer.depthClassificationSamples(scheme));
        input.bandLevels.assign(edges.cbegin(),edges.cend());
        timer.start();const auto bands=OpenSWMM::Render::computeContourJob(input);const double bandMs=ms();
        input.bandLevels.clear();input.isoLevels=OpenSWMM::Contour::evenlySpacedLevels(0.0,maxDepth,8);
        timer.start();const auto lines=OpenSWMM::Render::computeContourJob(input);const double isoMs=ms();
        row<<"contour_bands,"<<bandMs<<","<<bands.bands.size()<<"\n"
           <<"contour_isolines,"<<isoMs<<","<<lines.segs.size()<<"\n";
    }
    // GUI-thread cost of one frame change, with the source reads it contains.
    auto file=std::make_unique<HDF5Mesh2DSource>();QVERIFY(file->open(path));
    SWMM2DResultsLayer layer;layer.setSource(std::move(file));layer.setDryDepth(0.0001);
    const int count=layer.source()->timeCount();
    std::vector<float> depths,flux;
    for(int f=0;f<count;++f) {
        if(layer.currentTimeIndex()==f)layer.setCurrentTimeIndex((f+1)%count);
        timer.start();QVERIFY(layer.source()->readDepthsAt(f,depths));const double depthMs=ms();
        timer.start();layer.source()->readEdgeFluxAt(f,flux);const double fluxMs=ms();
        timer.start();layer.setCurrentTimeIndex(f);const double frameMs=ms();
        QCOMPARE(layer.currentTimeIndex(),f);
        row<<"frame_"<<f<<"_read_depths,"<<depthMs<<",1\n"<<"frame_"<<f<<"_read_flux,"<<fluxMs<<",1\n"
           <<"frame_"<<f<<"_set_time_index,"<<frameMs<<",1\n";
    }
}

// ---------------------------------------------------------------------------
// Fitted-surface worker (workplans/2D_VFR_PERF_RECOVERY_PLAN_2026-10-02.md).
// The worker must show exactly what the synchronous path shows.
// ---------------------------------------------------------------------------
namespace {
bool sameFloat(float a,float b) { return (std::isnan(a) && std::isnan(b)) || a==b; }
bool sameSceneCorners(const SWMM2DResultsLayer& a,const SWMM2DResultsLayer& b)
{
    if(a.m_sceneTris.size()!=b.m_sceneTris.size())return false;
    for(int i=0;i<a.m_sceneTris.size();++i) {
        const auto &p=a.m_sceneTris[i],&q=b.m_sceneTris[i];
        if(!sameFloat(p.dv0,q.dv0) || !sameFloat(p.dv1,q.dv1) || !sameFloat(p.dv2,q.dv2)
           || !sameFloat(p.depth,q.depth))return false;
    }
    return true;
}
bool sameField(const std::vector<CellWaterGeometry::CornerDepths>& a,
               const std::vector<CellWaterGeometry::CornerDepths>& b)
{
    if(a.size()!=b.size())return false;
    for(size_t c=0;c<a.size();++c)for(int k=0;k<4;++k)
        if(!((std::isnan(a[c][k]) && std::isnan(b[c][k])) || a[c][k]==b[c][k]))return false;
    return true;
}
bool sameMaximum(const MeshProfileSampler::MeshProfile& a,const MeshProfileSampler::MeshProfile& b)
{
    if(a.samples.size()!=b.samples.size())return false;
    for(int i=0;i<a.samples.size();++i) {
        const auto &p=a.samples[i],&q=b.samples[i];
        const bool sameSigned=(std::isnan(p.signedMaxDepth) && std::isnan(q.signedMaxDepth))
            || p.signedMaxDepth==q.signedMaxDepth;
        if(!sameSigned || p.maxDepth!=q.maxDepth || p.chainage!=q.chainage
           || p.displayTriIdx!=q.displayTriIdx || p.boundaryTriIdx!=q.boundaryTriIdx)return false;
    }
    return true;
}
std::unique_ptr<SWMM2DResultsLayer> flowingLayer(bool async)
{
    auto layer=std::make_unique<SWMM2DResultsLayer>();
    auto source=std::make_unique<FlowingSlopeSource>();
    source->frames={0.04f,0.08f,0.12f,0.06f,0.10f};
    layer->setAsyncSurface(async);
    layer->setSource(std::move(source));layer->setDryDepth(0.001);
    return layer;
}
} // namespace

void Test2DResultsVizFixes::surfaceWorkerMatchesSynchronousFit()
{
    for(size_t cacheBytes:{size_t(64)<<20,size_t(0)}) {
        auto sync=flowingLayer(false),async=flowingLayer(true);
        async->setSurfaceCacheBytes(cacheBytes);
        for(int frame:{3,1,4,1,0,3}) {
            sync->setCurrentTimeIndex(frame);
            async->setCurrentTimeIndex(frame);
            QCOMPARE(async->requestedTimeIndex(),frame);
            QTRY_COMPARE_WITH_TIMEOUT(async->currentTimeIndex(),frame,10000);
            QVERIFY(sameSceneCorners(*sync,*async));
            for(int cell=0;cell<sync->cellCount();++cell)
                QCOMPARE(async->cellWaterDisplayState(cell),sync->cellWaterDisplayState(cell));
            const QPointF point(12,-4);
            const double expected=sync->depthAtSceneInterp(point),actual=async->depthAtSceneInterp(point);
            QVERIFY(sameFloat(float(expected),float(actual)));
        }
        QTRY_VERIFY_WITH_TIMEOUT(!async->surfaceBusy(),10000);
        if(cacheBytes==0)continue;
        // A fitted frame is shown again at once, without another fit.
        const int fits=async->surfaceFitCount();
        async->setCurrentTimeIndex(1);
        QCOMPARE(async->currentTimeIndex(),1);
        QCOMPARE(async->surfaceFitCount(),fits);
        sync->setCurrentTimeIndex(1);
        QVERIFY(sameSceneCorners(*sync,*async));
    }
}

void Test2DResultsVizFixes::surfaceWorkerPublishesInRequestOrder()
{
    auto layer=flowingLayer(true);
    layer->setCurrentTimeIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(!layer->surfaceBusy() && layer->currentTimeIndex()==0,10000);
    QSignalSpy shown(layer.get(),&SWMM2DResultsLayer::currentTimeChanged);
    const quint64 revision=layer->frameRevision();
    const int fits=layer->surfaceFitCount();
    for(int frame:{1,2,3})layer->setCurrentTimeIndex(frame);
    QCOMPARE(layer->requestedTimeIndex(),3);
    // The previous fitted frame stays on display until a new one is ready.
    QCOMPARE(layer->currentTimeIndex(),0);
    QCOMPARE(layer->frameRevision(),revision);
    QTRY_COMPARE_WITH_TIMEOUT(layer->currentTimeIndex(),3,10000);
    QTRY_VERIFY_WITH_TIMEOUT(!layer->surfaceBusy(),10000);
    int previous=0;
    for(const auto& arguments:shown) {
        const int frame=arguments.at(0).toInt();
        QVERIFY(frame>previous);previous=frame;
    }
    QCOMPARE(previous,3);
    // Only the latest waiting request is kept: frame 2 was never fitted.
    QCOMPARE(layer->surfaceFitCount()-fits,2);
    // Returning to the displayed frame withdraws a newer request in flight.
    // Frame 2 has no fitted surface yet, so asking for it starts a fit.
    shown.clear();
    layer->setCurrentTimeIndex(2);layer->setCurrentTimeIndex(3);
    QCOMPARE(layer->requestedTimeIndex(),3);
    QTRY_VERIFY_WITH_TIMEOUT(!layer->surfaceBusy(),10000);
    QCOMPARE(layer->currentTimeIndex(),3);
    QCOMPARE(shown.count(),0);
}

void Test2DResultsVizFixes::surfaceWorkerEnvelopeAndStations()
{
    auto sync=flowingLayer(false),async=flowingLayer(true);
    sync->setCurrentTimeIndex(2);async->setCurrentTimeIndex(2);
    QTRY_VERIFY_WITH_TIMEOUT(!async->surfaceBusy() && async->currentTimeIndex()==2,10000);
    const QVector<QPointF> path{{0,-5},{50,-5}};
    // Stations alone never reconstruct a historical frame.
    const int fits=async->surfaceFitCount();
    auto stations=MeshProfileSampler::buildMeshProfile(nullptr,async.get(),path,100,false);
    QCOMPARE(async->surfaceFitCount(),fits);
    QVERIFY(stations.samples.size()>=20);
    for(const auto& s:stations.samples)QVERIFY(std::isnan(s.signedMaxDepth));
    QVERIFY(!async->envelopeComplete());
    QSignalSpy ready(async.get(),&SWMM2DResultsLayer::envelopeReady);
    async->requestEnvelope();
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,20000);
    QVERIFY(async->envelopeComplete());
    QVERIFY(sameField(async->maxSurfaceDepths(),sync->maxSurfaceDepths()));
    MeshProfileSampler::applyMaximum(stations,async.get());
    QVERIFY(sameMaximum(stations,MeshProfileSampler::buildMeshProfile(nullptr,sync.get(),path,100)));
    // A complete envelope answers at once and fits nothing.
    const int complete=async->surfaceFitCount();
    ready.clear();async->requestEnvelope();
    QCOMPARE(ready.count(),1);
    QCOMPARE(async->surfaceFitCount(),complete);
    // A visibility change invalidates the history; it is rebuilt to the same result.
    sync->setThinFilmDepth(0.05);async->setThinFilmDepth(0.05);
    QTRY_VERIFY_WITH_TIMEOUT(!async->surfaceBusy(),10000);
    QVERIFY(sameSceneCorners(*sync,*async));
    ready.clear();async->requestEnvelope();
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,20000);
    QVERIFY(sameField(async->maxSurfaceDepths(),sync->maxSurfaceDepths()));
}

void Test2DResultsVizFixes::surfaceWorkerLimitsLiveFits()
{
    FlowingSlopeSource shape;
    std::vector<double> x,y,z;std::vector<std::array<int,3>> tris;
    QVERIFY(shape.readMeshGeometry(x,y,z,tris));
    std::vector<std::array<int,4>> cells;
    for(const auto& t:tris)cells.push_back({t[0],t[1],t[2],-1});
    std::vector<float> flux;QVERIFY(shape.readEdgeFluxAt(0,flux));
    auto live=std::make_unique<EngineMesh2DSource>(x,y,z,cells);auto* raw=live.get();
    const QDateTime start(QDate(2026,1,1),QTime(0,0));
    raw->pushDepths(std::vector<float>(10,0.04f),start,0);raw->pushFlux(flux,start,0);
    SWMM2DResultsLayer layer;
    layer.setAsyncSurface(true);layer.setLiveMinFitIntervalMs(400);
    layer.setSource(std::move(live));layer.setDryDepth(0.001);
    layer.refreshTimeRange();
    QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy() && layer.currentTimeIndex()==0,10000);
    const int fits=layer.surfaceFitCount();
    for(int tick=1;tick<=5;++tick) {
        raw->pushDepths(std::vector<float>(10,0.04f+0.01f*float(tick)),start.addSecs(tick),tick);
        raw->pushFlux(flux,start.addSecs(tick),tick);
        layer.refreshTimeRange();
        QTest::qWait(5);
    }
    QTRY_COMPARE_WITH_TIMEOUT(layer.currentTimeIndex(),5,10000);
    QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(),10000);
    // Five ticks inside one interval: the first one and the latest one.
    QVERIFY2(layer.surfaceFitCount()-fits<=3,qPrintable(QString::number(layer.surfaceFitCount()-fits)));
    for(const auto& tri:layer.m_sceneTris)for(float d:{tri.dv0,tri.dv1,tri.dv2})
        QVERIFY(std::abs(d-0.09f)<1e-6f);
}

// Worker equality and latency on the preserved Bellinge output. Self-skips
// without VFR_TEST_BELLINGE; writes worker-latency.csv when VFR_PERF_OUT is set.
void Test2DResultsVizFixes::bellingeWorkerEqualityAndLatency()
{
    const QString path=qEnvironmentVariable("VFR_TEST_BELLINGE"),out=qEnvironmentVariable("VFR_PERF_OUT");
    if(path.isEmpty())QSKIP("Set VFR_TEST_BELLINGE to the preserved partial output");
    SWMM2DResultsLayer sync,async;
    async.setAsyncSurface(true);
    for(auto* layer:{&sync,&async}) {
        auto file=std::make_unique<HDF5Mesh2DSource>();QVERIFY(file->open(path));
        layer->setSource(std::move(file));layer->setDryDepth(0.0001);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!async.surfaceBusy(),120000);
    const int count=sync.source()->timeCount(),last=count-1;
    QFile csv(out+"/worker-latency.csv");
    if(!out.isEmpty())QVERIFY(csv.open(QIODevice::WriteOnly|QIODevice::Text));
    QTextStream row(&csv);row<<"metric,value_ms,count\n";
    QElapsedTimer timer;
    auto ms=[&timer]{return double(timer.nsecsElapsed())/1e6;};
    for(int frame=0;frame<count;++frame) {
        sync.setCurrentTimeIndex(frame);
        timer.start();async.setCurrentTimeIndex(frame);const double callMs=ms();
        QTRY_COMPARE_WITH_TIMEOUT(async.currentTimeIndex(),frame,120000);const double shownMs=ms();
        QVERIFY(sameSceneCorners(sync,async));
        row<<"frame_"<<frame<<"_request_call,"<<callMs<<",1\n"<<"frame_"<<frame<<"_request_to_shown,"<<shownMs<<",1\n";
    }
    QTRY_VERIFY_WITH_TIMEOUT(!async.surfaceBusy(),120000);
    // Every frame is fitted now: seeking is immediate and identical.
    const int fits=async.surfaceFitCount();
    for(int frame:{3,last,8,last,0}) {
        timer.start();async.setCurrentTimeIndex(frame);const double seekMs=ms();
        QCOMPARE(async.currentTimeIndex(),frame);
        sync.setCurrentTimeIndex(frame);
        QVERIFY(sameSceneCorners(sync,async));
        row<<"seek_cached_frame_"<<frame<<","<<seekMs<<",1\n";
    }
    QCOMPARE(async.surfaceFitCount(),fits);
    // Envelope from the fitted frames equals the synchronous fold.
    QSignalSpy ready(&async,&SWMM2DResultsLayer::envelopeReady);
    timer.start();async.requestEnvelope();const double requestMs=ms();
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,240000);
    timer.start();const auto maximum=async.maxSurfaceDepths();const double readMs=ms();
    QVERIFY(sameField(maximum,sync.maxSurfaceDepths()));
    row<<"envelope_request_call,"<<requestMs<<","<<count<<"\n"<<"envelope_read,"<<readMs<<",1\n";
    // Stations without the maximum, then the maximum applied: same section.
    const QVector<QPointF> section{{583970,-6132980},{584530,-6132980}};
    timer.start();
    auto profile=MeshProfileSampler::buildMeshProfile(nullptr,&async,section,100,false);const double stationMs=ms();
    timer.start();MeshProfileSampler::applyMaximum(profile,&async);const double applyMs=ms();
    QVERIFY(sameMaximum(profile,MeshProfileSampler::buildMeshProfile(nullptr,&sync,section,100)));
    row<<"profile_560m_stations,"<<stationMs<<","<<profile.samples.size()<<"\n"
       <<"profile_560m_apply_maximum,"<<applyMs<<","<<profile.samples.size()<<"\n";
}

void Test2DResultsVizFixes::flowingSlopeProfilesContoursAndHistory()
{
    const QString dir=qEnvironmentVariable("VFR_TEST_ARTIFACTS");
    for(double scale:{1.0,0.3048}) {
        SWMM2DResultsLayer layer;
        layer.setFallbackCoordinateScale(scale);
        auto source=std::make_unique<FlowingSlopeSource>();auto *raw=source.get();
        const auto stored=raw->frames;
        layer.setSource(std::move(source));layer.setDryDepth(0.001);
        const double factor=layer.depthToMeshUnits();
        for(int frame:{1,0,2,1}) {
            layer.setCurrentTimeIndex(frame);
            const auto profile=MeshProfileSampler::buildMeshProfile(nullptr,&layer,
                {{0,-5*factor},{50*factor,-5*factor}},100);
            QVERIFY(profile.samples.size()>=20);
            for(const auto& s:profile.samples) {
                QVERIFY(std::abs(s.signedDepthNow-double(stored[size_t(frame)])*factor)<1e-6);
                QVERIFY(std::abs(s.signedMaxDepth-double(stored.back())*factor)<1e-6);
                QVERIFY(std::abs(s.signedDepthNow-layer.signedDepthAtDisplayTriangle(s.displayTriIdx,s.scenePt)*factor)<1e-9);
            }
            // These are the exact corner attributes fed to CPU and GPU contours.
            for(const auto& tri:layer.m_sceneTris) for(float d:{tri.dv0,tri.dv1,tri.dv2})
                QVERIFY(std::abs(d-stored[size_t(frame)])<1e-6);
            if(scale==1 && frame==1 && !dir.isEmpty()) {
                MeshProfilePlotOptions options;options.setShowTimeLabel(false);
                MeshProfilePlotWidget plot;plot.setOptions(&options);plot.resize(1100,400);plot.setProfile(profile);
                QImage image(plot.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);plot.render(&image);
                QVERIFY(image.save(dir+"/flowing-slope-after.png"));
            }
        }
        QCOMPARE(raw->frames,stored);
        // Same-time flux loss must not reuse a previous moving surface.
        raw->fluxEnabled=false;layer.refreshCurrentFrame();
        QVERIFY(std::abs(layer.m_sceneTris[0].dv0-stored[1])>0.01);
        raw->fluxEnabled=true;layer.refreshCurrentFrame();
        QVERIFY(std::abs(layer.m_sceneTris[0].dv0-stored[1])<1e-6);
        raw->frames.back()=0.09f;
        const auto reduced=layer.maxSurfaceDepths();
        for(const auto& cell:reduced) for(int k=0;k<3;++k) QVERIFY(std::abs(cell[k]-0.09)<1e-6);
        layer.setThinFilmDepth(0.1); // classify inclined sheet BEFORE corner projection
        for(int c=0;c<layer.cellCount();++c) {
            QCOMPARE(layer.cellWaterDisplayState(c),CellWaterGeometry::DisplayState::ThinFilm);
            QVERIFY(!layer.cellHasSurface(c));
        }
        layer.setShowThinFilms(true);
        for(int c=0;c<layer.cellCount();++c) QVERIFY(layer.cellHasSurface(c));
    }
    if(!dir.isEmpty()) {
        SWMM2DResultsLayer before;
        auto legacy=std::make_unique<FlowingSlopeSource>();legacy->fluxEnabled=false;
        before.setSource(std::move(legacy));before.setCurrentTimeIndex(1);
        MeshProfilePlotOptions options;options.setShowTimeLabel(false);
        MeshProfilePlotWidget plot;plot.setOptions(&options);plot.resize(1100,400);
        plot.setProfile(MeshProfileSampler::buildMeshProfile(nullptr,&before,{{0,-5},{50,-5}},100));
        QImage image(plot.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);plot.render(&image);
        QVERIFY(image.save(dir+"/flowing-slope-before.png"));
    }
    FlowingSlopeSource fixture;
    std::vector<double> x,y,z;std::vector<std::array<int,3>> tris;
    fixture.readMeshGeometry(x,y,z,tris);
    auto live=std::make_unique<EngineMesh2DSource>(x,y,z,tris);
    for(int t=0;t<fixture.timeCount();++t) {
        std::vector<float> depth,flux;fixture.readDepthsAt(t,depth);fixture.readEdgeFluxAt(t,flux);
        live->pushDepths(depth,fixture.simTimeAt(t),t);live->pushFlux(flux,fixture.simTimeAt(t),t);
    }
    SWMM2DResultsLayer stream;stream.setSource(std::move(live));
    for(int t:{2,0,1}) {
        stream.setCurrentTimeIndex(t);
        for(const auto& tri:stream.m_sceneTris) for(float depth:{tri.dv0,tri.dv1,tri.dv2})
            QVERIFY(std::abs(depth-fixture.frames[size_t(t)])<1e-6);
    }
}

void Test2DResultsVizFixes::thinFilmPolicyPreservesRawWaterAndPartialPools()
{
    using S=CellWaterGeometry::DisplayState;
    SWMM2DResultsLayer layer;
    layer.setDryDepth(0.001);
    auto source=std::make_unique<VfrSource>(); auto *raw=source.get();
    source->z.assign(4,0);
    source->frames={{1.0f,0.0005f},{0.0005f,0.003f},{0.0f,0.0005f}};
    const auto original=source->frames;
    layer.setSource(std::move(source));
    for (int t : {0,2,1,0,1,2}) {
        layer.setCurrentTimeIndex(t);
        for (int c=0;c<2;++c) {
            const bool visible=original[t][c]>0.001;
            QCOMPARE(layer.cellHasSurface(c),visible);
            QCOMPARE(std::isfinite(layer.m_sceneTris[c].dv0),visible);
            QCOMPARE(layer.m_sceneTris[c].depth,original[t][c]);
        }
    }
    QCOMPARE(layer.cellWaterDisplayState(0),S::Dry);
    QCOMPARE(layer.cellWaterDisplayState(1),S::ThinFilm);
    const auto peaks=layer.maxSurfaceDepths();
    QVERIFY(std::abs(peaks[0][0]-1.0)<1e-6);
    QVERIFY(std::abs(peaks[1][0]-0.003)<1e-6); // independent of current dry frame
    QSignalSpy changed(&layer,&SWMM2DResultsLayer::waterDisplayPolicyChanged);
    const auto revision=layer.frameRevision();
    layer.setThinFilmDepth(0.005);
    QCOMPARE(changed.count(),1); QVERIFY(layer.frameRevision()>revision);
    QVERIFY(std::isnan(layer.maxSurfaceDepths()[1][0])); // threshold invalidates history
    layer.setShowThinFilms(true);
    QVERIFY(layer.cellHasSurface(1)); QVERIFY(!layer.cellHasSurface(0));
    QVERIFY(std::isfinite(layer.maxSurfaceDepths()[1][0]));
    QCOMPARE(raw->frames,original);
    layer.setShowThinFilms(false);
    layer.setThinFilmDepth(-1);
    // On a slope, a low whole-cell mean can hold a real deeper pool.
    layer.setFallbackCoordinateScale(0.3048);
    auto slope=std::make_unique<VfrSource>(); slope->frames={{0.00001f,0}};
    layer.setSource(std::move(slope));
    QCOMPARE(layer.cellWaterDisplayState(0),S::PartiallyWet);
    const auto profile=MeshProfileSampler::buildMeshProfile(nullptr,&layer,{{0.1,0},{0.1,-3}},100);
    QVERIFY(profile.hasResults);
    bool found=false;
    for (const auto &s:profile.samples) if (s.triIdx==0) {
        QCOMPARE(s.signedDepthNow,layer.signedDepthAtDisplayTriangle(s.displayTriIdx,s.scenePt)*layer.depthToMeshUnits());
        found=true;
    }
    QVERIFY(found);
    auto invalid=std::make_unique<VfrSource>();invalid->z.assign(4,0);
    invalid->frames={{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()},
                     {-1.0f,0.0f}};
    layer.setSource(std::move(invalid));
    QCOMPARE(layer.cellWaterDisplayState(0),S::Invalid);
    QCOMPARE(layer.cellWaterDisplayState(1),S::Dry);
    layer.setCurrentTimeIndex(0);
    for(int c=0;c<2;++c) { QCOMPARE(layer.cellWaterDisplayState(c),S::Invalid); QVERIFY(!layer.cellHasSurface(c)); }
}

void Test2DResultsVizFixes::thinFilmSettingsRoundTrip()
{
    SWMM2DResultsLayer layer;
    layer.setDryDepth(0.001);
    openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);
    auto *model=panel.findChild<QCheckBox *>("useModelThinFilmDepth");
    auto *depth=panel.findChild<QDoubleSpinBox *>("thinFilmDepth");
    auto *show=panel.findChild<QCheckBox *>("showThinFilms");
    QVERIFY(model); QVERIFY(depth); QVERIFY(show);
    QVERIFY(model->isChecked()); QVERIFY(!depth->isEnabled()); QVERIFY(!show->isChecked());
    model->setChecked(false); QVERIFY(depth->isEnabled()); depth->setValue(0.005);
    QCOMPARE(layer.thinFilmDepth(),0.005);
    show->setChecked(true); QVERIFY(layer.showThinFilms());
    SWMM2DResultsLayer restored;
    restored.restoreWaterDisplayPolicy(layer.waterDisplayPolicyToJson());
    QCOMPARE(restored.thinFilmDepth(),0.005); QVERIFY(restored.showThinFilms());
    QVERIFY(!restored.usesModelThinFilmDepth());
    model->setChecked(true); layer.setDryDepth(0.002);
    QCOMPARE(depth->value(),0.002);
    restored.setDryDepth(0.003);
    restored.restoreWaterDisplayPolicy(layer.waterDisplayPolicyToJson());
    QVERIFY(restored.usesModelThinFilmDepth()); QCOMPARE(restored.thinFilmDepth(),0.003);
    const auto saved=restored.waterDisplayPolicyToJson();
    restored.restoreWaterDisplayPolicy({{"version",1},{"thinFilmDepthMetres","bad"},{"showThinFilms",false}});
    QCOMPARE(restored.waterDisplayPolicyToJson(),saved);
    QUndoStack undo;
    {
        openswmmvis::ui::LayerStyleDialog dialog(&restored,{},nullptr,&undo);
        restored.setThinFilmDepth(0.05);restored.setShowThinFilms(false);
        dialog.reject();QCOMPARE(restored.waterDisplayPolicyToJson(),saved);QCOMPARE(undo.count(),0);
    }
    {
        openswmmvis::ui::LayerStyleDialog dialog(&restored,{},nullptr,&undo);
        restored.setThinFilmDepth(0.05);restored.setShowThinFilms(false);dialog.accept();
    }
    QCOMPARE(undo.count(),1);undo.undo();QCOMPARE(restored.waterDisplayPolicyToJson(),saved);
    undo.redo();QCOMPARE(restored.thinFilmDepth(),0.05);QVERIFY(!restored.showThinFilms());
}

void Test2DResultsVizFixes::thinFilmVisibilityCpuQsgAndPendingFrames()
{
    class Renderer : public SWMM2DResultsQSGRenderer {
    public: QSGNode *sync(QSGNode *old=nullptr) { return updatePaintNode(old,nullptr); }
    };
    SWMM2DResultsLayer layer;
    auto source=std::make_unique<VfrSource>(); auto *raw=source.get();
    source->z.assign(4,0); source->frames={{0.003f,0.003f}};
    layer.setSource(std::move(source)); layer.setDryDepth(0.001); layer.setVisible(true);
    QGraphicsScene scene; layer.populateScene(&scene,MapExtent(0,0,1,1),nullptr);
    for (int pass=0;pass<4;++pass) {
        for(auto *s:layer.sublayers())s->setVisible(false);
        if(pass<2) { layer.contourBandSublayer()->setVisible(true); layer.contourBandSublayer()->bandStyle()->setSmoothBands(pass==0); }
        else if(pass==2) layer.cellDepthFillSublayer()->setVisible(true);
        else layer.smoothDepthFillSublayer()->setVisible(true);
        layer.setShowThinFilms(false);layer.setThinFilmDepth(0.001);
        Renderer renderer; renderer.setWidth(400);renderer.setHeight(400);
        renderer.setMapExtent(MapExtent(0,0,1,1));renderer.setLayer(&layer);
        QSignalSpy ready(&renderer,&SWMM2DResultsQSGRenderer::contentReady);
        std::unique_ptr<QSGNode> root(renderer.sync());
        auto vertexCount=[&] {
            int count=0;
            for(auto *n=root->firstChild();n;n=n->nextSibling()) {
                if(n->type()!=QSGNode::GeometryNodeType)continue;
                auto *g=static_cast<QSGGeometryNode*>(n)->geometry();
                if(g&&g->attributeCount()==2)count+=g->vertexCount();
            }
            return count;
        };
        auto cpuAlpha=[&] {
            QImage img(400,400,QImage::Format_ARGB32_Premultiplied);img.fill(Qt::transparent);
            QPainter p(&img);scene.render(&p,QRectF(0,0,400,400),QRectF(0,-1,1,1));p.end();
            return img.pixelColor(100,300).alpha();
        };
        QVERIFY(vertexCount()>0);if(pass<2)QVERIFY(cpuAlpha()>0);
        raw->frames[0]={0.004f,0.004f};layer.refreshCurrentFrame();
        root.reset(renderer.sync(root.release())); // may launch an old-policy worker
        layer.setThinFilmDepth(0.005);
        root.reset(renderer.sync(root.release()));
        QCOMPARE(vertexCount(),0);if(pass<2)QCOMPARE(cpuAlpha(),0);
        if(pass==0&&qEnvironmentVariableIntValue("OPENSWMM_QSG_ASYNC_CONTOURS")==1) {
            QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,5000);
            root.reset(renderer.sync(root.release()));QCOMPARE(vertexCount(),0);
        }
        const auto profile=MeshProfileSampler::buildMeshProfile(nullptr,&layer,{{0,-0.5},{1,-0.5}});
        for(const auto &sample:profile.samples)QVERIFY(std::isnan(sample.signedDepthNow));
        layer.setShowThinFilms(true);root.reset(renderer.sync(root.release()));
        QVERIFY(vertexCount()>0);if(pass<2)QVERIFY(cpuAlpha()>0);
        const auto visible=MeshProfileSampler::buildMeshProfile(nullptr,&layer,{{0,-0.5},{1,-0.5}});
        for(const auto &sample:visible.samples)QVERIFY(std::isfinite(sample.signedDepthNow));
    }
}

void Test2DResultsVizFixes::thinFilmSavedAndLiveParity()
{
    const QString path=qEnvironmentVariable("VFR_TEST_ROAD_CULVERT");
    if(path.isEmpty())QSKIP("Set VFR_TEST_ROAD_CULVERT to verify the saved culvert fixture");
    auto file=std::make_unique<HDF5Mesh2DSource>();QVERIFY(file->open(path));
    std::vector<double> x,y,z;std::vector<std::array<int,4>> cells;
    QVERIFY(file->readCells(x,y,z,cells));
    std::vector<float> depths;QVERIFY(file->readDepthsAt(70,depths));
    auto live=std::make_unique<EngineMesh2DSource>(x,y,z,cells);
    live->pushDepths(depths,file->simTimeAt(70),0);
    std::vector<float> flux;
    if(file->readEdgeFluxAt(70,flux)) live->pushFlux(flux,file->simTimeAt(70),0);
    SWMM2DResultsLayer saved,stream;
    saved.setSource(std::move(file)); saved.setCurrentTimeIndex(70);
    stream.setSource(std::move(live));stream.setCurrentTimeIndex(0);
    for(auto *layer:{&saved,&stream}){layer->setDryDepth(0.001);layer->setThinFilmDepth(0.005);}
    int films=0,partial=0;
    for(int c=0;c<saved.cellCount();++c){
        QCOMPARE(saved.cellWaterDisplayState(c),stream.cellWaterDisplayState(c));
        QCOMPARE(saved.cellHasSurface(c),stream.cellHasSurface(c));
        if(saved.cellWaterDisplayState(c)==CellWaterGeometry::DisplayState::ThinFilm){++films;QVERIFY(!saved.cellHasSurface(c));}
        if(saved.cellWaterDisplayState(c)==CellWaterGeometry::DisplayState::PartiallyWet){++partial;QVERIFY(saved.cellHasSurface(c));}
    }
    QVERIFY(films>0);QVERIFY(partial>0);
    std::vector<float> after;QVERIFY(saved.source()->readDepthsAt(70,after));QCOMPARE(after,depths);
    const QString dir=qEnvironmentVariable("VFR_TEST_ARTIFACTS");
    if(!dir.isEmpty()) {
        QFile report(dir+"/road-culvert-film-states.txt");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QString("Frame 70; film threshold 0.005 m; %1 hidden film cells; %2 partially wet cells retained.\n"
                             "Saved and live classifications agree for all %3 cells; stored depths unchanged.\n")
                         .arg(films).arg(partial).arg(saved.cellCount()).toUtf8());
        for(int cell:{898,899,900,901,902,903})
            report.write(QString("cell %1: raw depth %2 m, display state %3\n").arg(cell)
                .arg(depths[size_t(cell)],0,'g',9).arg(int(saved.cellWaterDisplayState(cell))).toUtf8());
        MeshProfilePlotOptions options;
        options.setShowMaxEnvelopeFill(false);options.setShowMaxEnvelopeLine(false);
        options.setShowTimeLabel(false);options.setLegendVisible(false);
        MeshProfilePlotWidget plot;plot.setOptions(&options);plot.resize(1000,400);
        saved.setVisible(true);
        for(auto *sub:saved.sublayers())sub->setVisible(false);
        saved.contourBandSublayer()->setVisible(true);
        QGraphicsScene scene;saved.populateScene(&scene,MapExtent(470,35,535,65),nullptr);
        for(bool show:{true,false}) {
            saved.setShowThinFilms(show);
            plot.setProfile(MeshProfileSampler::buildMeshProfile(nullptr,&saved,{{470,-50},{535,-50}}));
            QImage profile(plot.size(),QImage::Format_ARGB32_Premultiplied);profile.fill(Qt::white);plot.render(&profile);
            const QString suffix=show?"shown":"hidden";
            QVERIFY(profile.save(dir+"/road-profile-films-"+suffix+".png"));
            QImage map(900,450,QImage::Format_ARGB32_Premultiplied);map.fill(Qt::white);
            QPainter painter(&map);scene.render(&painter,QRectF(0,0,900,450),QRectF(470,-65,65,30));painter.end();
            QVERIFY(map.save(dir+"/road-map-films-"+suffix+".png"));
        }
    }
}

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
        const double shore=1.5/4.0;
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

// Renderer-only scale fixture: fitted corners are supplied directly so this
// measures synchronization/contouring independently of the surface solver.
void Test2DResultsVizFixes::largeContoursNeverMarchDuringSync()
{
    if (qEnvironmentVariableIsSet("OPENSWMM_QSG_ASYNC_CONTOURS")
        && qEnvironmentVariableIntValue("OPENSWMM_QSG_ASYNC_CONTOURS")==0)
        QSKIP("This regression requires the default or enabled background worker");
    class Renderer : public SWMM2DResultsQSGRenderer {
    public: QSGNode* sync(QSGNode* old=nullptr) { return updatePaintNode(old,nullptr); }
    };
    SWMM2DResultsLayer layer;
    auto source=std::make_unique<FlowVfrSource>(); auto* raw=source.get();
    layer.setSource(std::move(source)); layer.setVisible(true);
    for (auto* sub:layer.sublayers()) sub->setVisible(false);
    layer.contourBandSublayer()->setVisible(true);
    layer.isolineSublayer()->setVisible(true);
    layer.isolineSublayer()->isolineStyle()->setLabels(false);
    int count=qEnvironmentVariableIntValue("OPENSWMM_RENDER_TEST_TRIANGLES");
    count=std::max(60000,count);
    const int cols=1600,rows=(count+cols-1)/cols;
    auto expand=[&] {
        const auto fitted=layer.m_sceneTris.front();
        layer.m_sceneTris.resize(count);
        for(int i=0;i<count;++i) {
            auto t=fitted; const double x=i%cols,y=-(i/cols);
            t.a=QPointF(x,y);t.b=QPointF(x+1,y);t.c=QPointF(x,y-1);
            layer.m_sceneTris[i]=t;
        }
        layer.m_sceneBBox=QRectF(0,-rows,cols,rows);
    };
    expand();
    Renderer renderer;renderer.setWidth(800);renderer.setHeight(500);
    renderer.setMapExtent(MapExtent(0,0,16,10));renderer.setLayer(&layer);
    QSignalSpy ready(&renderer,&SWMM2DResultsQSGRenderer::contentReady);
    QElapsedTimer timer;timer.start();
    std::unique_ptr<QSGNode> root(renderer.sync());
    qInfo()<<"scale triangles"<<count<<"initial sync ms"<<timer.nsecsElapsed()/1e6;
    // Before the fix this already presents the frame: marching was inline.
    QVERIFY(renderer.displayedFrameRevision()!=layer.frameRevision());
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,10000);
    root.reset(renderer.sync(root.release()));
    QCOMPARE(renderer.displayedFrameRevision(),layer.frameRevision());
    qInfo()<<"initial presented ms"<<timer.nsecsElapsed()/1e6;
    ready.clear();
    const auto oldFrame=renderer.displayedFrameRevision();
    for(float depth:{0.4f,0.6f,0.8f}) {
        layer.m_sceneTris.resize(2);raw->frames[0][0]=depth;
        layer.refreshCurrentFrame();expand();
        // Auto-range growth must not force synchronous work or reject every
        // completed intermediate frame while the producer keeps advancing.
        layer.setMaxDepth(depth*10);
        timer.restart();root.reset(renderer.sync(root.release()));
        qInfo()<<"replacement sync ms"<<timer.nsecsElapsed()/1e6;
        QCOMPARE(renderer.displayedFrameRevision(),oldFrame);
    }
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,10000);
    root.reset(renderer.sync(root.release()));
    QVERIFY(renderer.displayedFrameRevision()>oldFrame);
    for(int i=0;i<20 && renderer.displayedFrameRevision()!=layer.frameRevision();++i) {
        QTest::qWait(20);root.reset(renderer.sync(root.release()));
    }
    QCOMPARE(renderer.displayedFrameRevision(),layer.frameRevision());
    // Pan outside the prepared coverage: recompute the newly exposed area.
    renderer.setMapExtent(MapExtent(800,0,816,10));
    const int before=ready.count();root.reset(renderer.sync(root.release()));
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>before,10000);
    root.reset(renderer.sync(root.release()));
    int vertices=0;
    for(auto* child=root->firstChild();child;child=child->nextSibling())
        if(child->type()==QSGNode::GeometryNodeType) {
            auto* g=static_cast<QSGGeometryNode*>(child)->geometry();
            if(g)vertices+=g->vertexCount();
        }
    QVERIFY(vertices>0);
    // Geometry is proportional to the view rather than the 1.6M-cell domain.
    QVERIFY(vertices<100000);
    // Same-time style replacement also runs in the background and cannot be
    // undone by an earlier completed job.
    const int styleBefore=ready.count();
    layer.contourBandSublayer()->bandStyle()->setBandCount(5);
    root.reset(renderer.sync(root.release()));
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>styleBefore,10000);
    root.reset(renderer.sync(root.release()));
    QCOMPARE(renderer.displayedFrameRevision(),layer.frameRevision());
    // All-dry is a completed empty result, not a reason to retain stale flood.
    layer.m_sceneTris.resize(2);raw->frames[0][0]=0;
    layer.refreshCurrentFrame();expand();
    const int dryBefore=ready.count();root.reset(renderer.sync(root.release()));
    QTRY_VERIFY_WITH_TIMEOUT(ready.count()>dryBefore,10000);
    root.reset(renderer.sync(root.release()));
    for(auto* child=root->firstChild();child;child=child->nextSibling())
        if(child->type()==QSGNode::GeometryNodeType) {
            auto* g=static_cast<QSGGeometryNode*>(child)->geometry();
            if(g)QCOMPARE(g->vertexCount(),0);
        }
    if(qEnvironmentVariableIntValue("OPENSWMM_RENDER_TEST_OVERVIEW")==1) {
        layer.m_sceneTris.resize(2);raw->frames[0][0]=0.8f;
        layer.refreshCurrentFrame();expand();
        renderer.setMapExtent(MapExtent(0,0,cols,rows));
        const int overviewBefore=ready.count();timer.restart();
        root.reset(renderer.sync(root.release()));
        qInfo()<<"overview request sync ms"<<timer.nsecsElapsed()/1e6;
        QTRY_VERIFY_WITH_TIMEOUT(ready.count()>overviewBefore,60000);
        qInfo()<<"overview worker ready ms"<<timer.nsecsElapsed()/1e6;
        timer.restart();root.reset(renderer.sync(root.release()));
        qInfo()<<"overview presentation sync ms"<<timer.nsecsElapsed()/1e6;
        QCOMPARE(renderer.displayedFrameRevision(),layer.frameRevision());
    }

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
            const double y=1.5/4.0;
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
