/**
 * @file      test_vertexdepthreconstruct.cpp
 * @brief     Tests for vertexdepthreconstruct.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Pins the GUI-fallback vertex free-surface reconstruction
 * (VertexDepthReconstruct::reconstructVertexSignedDepths) — the mirror of the
 * engine's reconstructVertexRenderDepths — including the wetted-contact gate:
 * a wet cell votes at a corner only when its water surface reaches it
 * (η > z_v). Mirrors the engine tests WallTopVertexIsNoDataNotNotched /
 * WallBaseFilmDoesNotNotchPool in tests/unit/engine/test_2d_surface_routing.cpp.
 * Pure assertions — writes no files.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/vertexdepthreconstruct.h"

#include <QObject>
#include <QTest>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace
{

// Step mesh (mirror of the engine fixture): T0 (v0,v1,v3) flat at z=0,
// T1 (v0,v3,v2) climbing to a crest at v2 (z=5).
//
//   v2 (0,1, z=5) ---- v3 (1,1, z=0)
//     |    \  T1  |
//     | T0   \    |
//   v0 (0,0, z=0) ---- v1 (1,0, z=0)
struct StepFixture {
    std::vector<std::array<int, 3>> tris{{0, 1, 3}, {0, 3, 2}};
    std::vector<double> vz{0.0, 0.0, 5.0, 0.0};
    std::vector<float> cellZc{0.0f, 5.0f / 3.0f};
    std::vector<float> vsum, wsum, out;

    /**
     *
     * @brief Reconstruct this two-triangle step fixture's vertex depths.
     * @param d0 First cell mean depth in common elevation units (metres in the step example).
     * @param d1 Second cell mean depth in the same units.
     * @param dryF Positive dry-depth threshold, default .001 in those units.
     * Overwrites member scratch/output vectors through production reconstruction using
     * current tris/vz/cellZc. Inputs must stay shape-compatible; no file IO or assertions
     * inside this helper, and no rollback if an allocation fails.
     *
     */
    void run(float d0, float d1, float dryF = 1e-3f)
    {
        const std::vector<float> depths{d0, d1};
        VertexDepthReconstruct::reconstructVertexSignedDepths(tris, depths, cellZc, vz, dryF, vsum,
                                                              wsum, out);
    }
};

} // namespace

class TestVertexDepthReconstruct : public QObject
{
    Q_OBJECT
private slots:

    // Wetted-contact gate: T1's water pools far below its crest vertex v2, so
    // v2 must read the 0 no-data sentinel — not a negative signed depth.
    /**
     *
     * @brief Require an unwetted high crest to keep zero sentinel while lower vertices retain
     * nonnegative wet depths.
     *
     */
    void wallTopVertexIsNoDataNotNotched()
    {
        StepFixture f;
        f.run(0.5f, 0.2f);
        QCOMPARE(f.out[2], 0.0f);
        QVERIFY(f.out[0] > 0.0f);
        QVERIFY(f.out[1] > 0.0f);
        QVERIFY(f.out[3] > 0.0f);
        for (float d : f.out)
            QVERIFY(d >= 0.0f); // gate ⇒ non-negative field
    }

    // Pool + thin flank film: shared base vertices stay bracketed by the two
    // contributing cell η's; the pool-only vertex reads the pool exactly.
    /**
     *
     * @brief Require shared vertex water surfaces to stay between contributing pool/film surfaces
     * and preserve pool-only depth.
     *
     */
    void wallBaseFilmDoesNotNotchPool()
    {
        StepFixture f;
        f.run(1.0f, 0.01f);
        const double eta0 = VertexDepthReconstruct::cellEtaFromMeanDepth(1.0, f.vz[0], f.vz[1],
                                                                         f.vz[3]); // pool: 1.0
        const double eta1 = VertexDepthReconstruct::cellEtaFromMeanDepth(0.01, f.vz[0], f.vz[3],
                                                                         f.vz[2]); // film ≈ base
        QCOMPARE(f.out[2], 0.0f); // crest: sentinel
        for (int v : {0, 3}) {
            const double etaV = double(f.out[v]) + f.vz[v];
            QVERIFY(etaV >= std::min(eta0, eta1) - 1e-6);
            QVERIFY(etaV <= std::max(eta0, eta1) + 1e-6);
        }
        QVERIFY(std::abs(double(f.out[1]) - eta0) < 1e-6); // pool-only vertex
    }

    // Lake at rest on a flat step-free cell pair stays flat at every vertex.
    /**
     *
     * @brief Require equal-depth flat-bed cells to reconstruct uniform .7 depth at every vertex.
     *
     */
    void lakeAtRestIsFlat()
    {
        StepFixture f;
        f.vz = {0.0, 0.0, 0.0, 0.0};
        f.cellZc = {0.0f, 0.0f};
        f.run(0.7f, 0.7f);
        for (float d : f.out)
            QVERIFY(std::abs(d - 0.7f) < 1e-6f);
    }

    // Dry cells (below dryF) and all-dry frames contribute nothing.
    /**
     *
     * @brief Require two fully dry cells to produce zero at all vertices.
     *
     */
    void dryCellsYieldZero()
    {
        StepFixture f;
        f.run(0.0f, 0.0f);
        for (float d : f.out)
            QCOMPARE(d, 0.0f);
    }

    // NaN robustness: a NaN cell depth is skipped (not spread), and a NaN
    // vertex elevation yields a dry vertex.
    /**
     *
     * @brief Require NaN cell depths not to contaminate finite output and a NaN crest elevation
     * to produce zero sentinel.
     *
     */
    void nanRobustness()
    {
        StepFixture f;
        const float nanf = std::numeric_limits<float>::quiet_NaN();
        const std::vector<float> depths{0.5f, nanf};
        VertexDepthReconstruct::reconstructVertexSignedDepths(f.tris, depths, f.cellZc, f.vz, 1e-3f,
                                                              f.vsum, f.wsum, f.out);
        for (float d : f.out)
            QVERIFY(std::isfinite(d));

        StepFixture g;
        g.vz[2] = std::numeric_limits<double>::quiet_NaN();
        g.run(0.5f, 0.2f);
        QCOMPARE(g.out[2], 0.0f);
    }
};

QTEST_APPLESS_MAIN(TestVertexDepthReconstruct)
#include "test_vertexdepthreconstruct.moc"
