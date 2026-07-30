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
 * NB (link wiring — ACTION REQUIRED): this test drives the real
 * SWMM2DResultsLayer, whose link closure is large (layer base, mesh spatial
 * grid, all render sublayers, value-type specs, io/mesh2dh5reader, …). It is
 * NOT yet registered in tests/gui/CMakeLists.txt so the build stays green until
 * the closure is completed. To enable it, add an add_swmmvis_gui_test() entry
 * and resolve the undefined-reference list the linker prints (start from the
 * source list in PLAN_2D_VIZ_REFINEMENT.md → "Enabling the layer test").
 * Pure assertions — writes no temp files.
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

private:
    bool               m_live;
    std::vector<float> m_frameDepth;
    QDateTime          m_t0 = QDateTime(QDate(2026, 1, 1), QTime(0, 0));
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
};

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
