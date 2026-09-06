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

#include <QDateTime>
#include <QObject>
#include <QTest>

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

} // namespace

class Test2DResultsVizFixes : public QObject
{
    Q_OBJECT
private slots:
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

    // Vertex reconstruction on the flat bed: η = h per cell, so at a shared
    // vertex (v1) the depth-weighted blend with weights h·(3/nv) is
    //   (0.75·0.4·0.4 + 1·0.2·0.2) / (0.75·0.4 + 0.2) = 0.16 / 0.5 = 0.32;
    // at a quad-only vertex (v0) it is the quad's 0.4.
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
