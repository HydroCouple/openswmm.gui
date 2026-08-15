/**
 * @file test_profilebuilder.cpp
 * @brief Unit tests for ProfileBuilder — pure-logic chainage, HGL/EGL, and
 *        envelope computation.  Slice BC, Stage 2.
 */

#include <gtest/gtest.h>

#include "plot/profilebuilder.h"

#include <QDateTime>
#include <QVector>

#include <cmath>

using namespace ProfileBuilder;

namespace {

// Build a simple linear path of N nodes and N-1 conduits.  All conduits
// have unit length, zero offsets, and a 2.0-ft max depth.  All nodes share
// `invertElev` (default 100 ft) and `maxDepth` (default 5 ft).  Use the
// override helpers below for non-uniform fixtures.
PathStatic linearPath(int n,
                      double invertElev = 100.0,
                      double nodeMaxDepth = 5.0,
                      double linkLen = 1.0)
{
    PathStatic p;
    for (int i = 0; i < n; ++i) {
        NodeStatic ns;
        ns.name = QString::number(i);
        ns.invertElev = invertElev;
        ns.maxDepth   = nodeMaxDepth;
        ns.kind       = NodeKind::Junction;
        p.nodes.push_back(ns);
    }
    for (int i = 0; i + 1 < n; ++i) {
        LinkStatic ls;
        ls.name = QStringLiteral("L%1").arg(i);
        ls.length   = linkLen;
        ls.offset1  = 0.0;
        ls.offset2  = 0.0;
        ls.maxDepth = 2.0;
        ls.kind     = LinkKind::Conduit;
        p.links.push_back(ls);
    }
    p.chainage = computeChainage(p.links);
    return p;
}

// Build a SourceSeries with constant head per node and constant velocity per
// link.  Per-node head values come from `heads`; per-link velocities from
// `vels`.
SourceSeries constantSource(const PathStatic &path,
                            const QVector<double> &heads,
                            const QVector<double> &vels,
                            int periodCount = 1)
{
    SourceSeries s;
    s.sourceId      = QStringLiteral("src1");
    s.startTime     = QDateTime::fromString("2026-05-17T00:00:00Z", Qt::ISODate);
    s.reportStepSec = 300;
    s.periodCount   = periodCount;
    s.nodeHead.resize(path.nodes.size());
    s.nodeDepth.resize(path.nodes.size());
    s.linkVelocity.resize(path.links.size());
    for (int n = 0; n < path.nodes.size(); ++n) {
        s.nodeHead[n].resize(periodCount);
        s.nodeDepth[n].resize(periodCount);
        for (int p = 0; p < periodCount; ++p) {
            s.nodeHead[n][p]  = static_cast<float>(heads.value(n, 0.0));
            s.nodeDepth[n][p] = static_cast<float>(heads.value(n, 0.0) - path.nodes[n].invertElev);
        }
    }
    for (int l = 0; l < path.links.size(); ++l) {
        s.linkVelocity[l].resize(periodCount);
        for (int p = 0; p < periodCount; ++p)
            s.linkVelocity[l][p] = static_cast<float>(vels.value(l, 0.0));
    }
    return s;
}

} // namespace

// ---- 1. Chainage accumulation -------------------------------------------

TEST(ProfileBuilder, Chainage_AccumulatesAlongPath)
{
    auto p = linearPath(4, 100.0, 5.0, 10.0);
    ASSERT_EQ(p.chainage.size(), 4);
    EXPECT_DOUBLE_EQ(p.chainage[0],  0.0);
    EXPECT_DOUBLE_EQ(p.chainage[1], 10.0);
    EXPECT_DOUBLE_EQ(p.chainage[2], 20.0);
    EXPECT_DOUBLE_EQ(p.chainage[3], 30.0);
}

// ---- 2. Multi-pipe chainage with mixed link lengths ---------------------

TEST(ProfileBuilder, Chainage_HandlesMixedLengths)
{
    PathStatic p;
    p.nodes.resize(4);
    LinkStatic a, b, c;
    a.length = 12.5; b.length = 7.0; c.length = 100.0;
    p.links = { a, b, c };
    p.chainage = computeChainage(p.links);
    EXPECT_DOUBLE_EQ(p.chainage[3], 119.5);
}

// ---- 3. Conduit invert with non-zero up/downstream offsets ---------------

TEST(ProfileBuilder, ConduitInverts_HonorOffset1AndOffset2)
{
    PathStatic p = linearPath(2, /*invertElev*/ 100.0, /*nodeMaxDepth*/ 5.0);
    p.nodes[0].invertElev = 100.0;
    p.nodes[1].invertElev = 95.0;
    p.links[0].offset1 = 0.5;
    p.links[0].offset2 = 0.25;
    EXPECT_DOUBLE_EQ(conduitInvertUpstream(p, 0), 100.5);
    EXPECT_DOUBLE_EQ(conduitInvertDownstream(p, 0), 95.25);
}

// ---- 4. EGL = HGL + v²/(2g) within tolerance ----------------------------
//
//   3 nodes, 2 links, constant velocity 4 ft/s, gravity 32.174 ft/s².
//   Expected velocity head = 16 / 64.348 ≈ 0.2487 ft.
//   Interior node averages two link velocities (both 4): vh ≈ 0.2487.

TEST(ProfileBuilder, EGL_EqualsHGLPlusVelocityHead_USUnits)
{
    auto path = linearPath(3, 100.0, 5.0);
    auto src  = constantSource(path, {103.0, 103.0, 103.0}, {4.0, 4.0});
    auto d    = compute(path, src, kGravityFps2);
    // SourceDerived is period-major: outer = periodCount (1), inner = N (3).
    ASSERT_EQ(d.eglByPeriod.size(), 1);
    ASSERT_EQ(d.eglByPeriod[0].size(), 3);
    const double expectedVH = (4.0 * 4.0) / (2.0 * kGravityFps2);
    for (int n = 0; n < 3; ++n) {
        EXPECT_NEAR(d.hglByPeriod[0][n], 103.0,             1e-9);
        EXPECT_NEAR(d.eglByPeriod[0][n], 103.0 + expectedVH, 1e-3);
    }
}

// ---- 5. EGL head node uses downstream velocity only ----------------------

TEST(ProfileBuilder, EGL_HeadNode_UsesDownstreamVelocityOnly)
{
    auto path = linearPath(3);
    // Distinct velocities: head node sees only link 0 velocity (5 ft/s)
    auto src  = constantSource(path, {103.0, 103.0, 103.0}, {5.0, 1.0});
    const double vh0 = velocityHead(path, src, /*node*/ 0, /*period*/ 0, kGravityFps2);
    const double expected = (5.0 * 5.0) / (2.0 * kGravityFps2);
    EXPECT_NEAR(vh0, expected, 1e-9);
}

// ---- 6. EGL tail node uses upstream velocity only ------------------------

TEST(ProfileBuilder, EGL_TailNode_UsesUpstreamVelocityOnly)
{
    auto path = linearPath(3);
    auto src  = constantSource(path, {103.0, 103.0, 103.0}, {5.0, 1.0});
    const double vh2 = velocityHead(path, src, /*node*/ 2, /*period*/ 0, kGravityFps2);
    const double expected = (1.0 * 1.0) / (2.0 * kGravityFps2);
    EXPECT_NEAR(vh2, expected, 1e-9);
}

// ---- 7. EGL interior node averages adjacent link velocities --------------

TEST(ProfileBuilder, EGL_InteriorNode_AveragesAdjacentLinkVelocities)
{
    auto path = linearPath(3);
    auto src  = constantSource(path, {103.0, 103.0, 103.0}, {6.0, 2.0});
    // mean v = 4, vh = 16 / (2 * 32.174)
    const double vh1 = velocityHead(path, src, /*node*/ 1, /*period*/ 0, kGravityFps2);
    const double expected = (4.0 * 4.0) / (2.0 * kGravityFps2);
    EXPECT_NEAR(vh1, expected, 1e-9);
}

// ---- 8. Negative velocity contributes positive kinetic energy ------------

TEST(ProfileBuilder, EGL_NegativeVelocity_StillContributesPositiveVelocityHead)
{
    auto path = linearPath(2);
    auto src  = constantSource(path, {103.0, 103.0}, {-4.0});
    const double vh = velocityHead(path, src, /*node*/ 0, /*period*/ 0, kGravityFps2);
    EXPECT_NEAR(vh, (16.0) / (2.0 * kGravityFps2), 1e-9);
    EXPECT_GE(vh, 0.0);
}

// ---- 9. Envelope min/max match brute-force scan -------------------------
//
//   Per-node head varies across periods; envelope must equal min/max of
//   the per-node series.

TEST(ProfileBuilder, Envelope_MinMaxMatchesBruteForce)
{
    auto path = linearPath(3);
    SourceSeries s;
    s.sourceId      = "src";
    s.reportStepSec = 60;
    s.periodCount   = 5;
    s.nodeHead.resize(3);
    s.nodeDepth.resize(3);
    s.linkVelocity.resize(2);
    const float heads[3][5] = {
        {100, 102, 101, 103, 100},
        {101, 100, 104, 102, 105},
        {102, 102, 100, 100, 103},
    };
    for (int n = 0; n < 3; ++n) {
        s.nodeHead[n].resize(5);
        s.nodeDepth[n].resize(5);
        for (int p = 0; p < 5; ++p) {
            s.nodeHead[n][p]  = heads[n][p];
            s.nodeDepth[n][p] = heads[n][p] - 100.0f;
        }
    }
    for (int l = 0; l < 2; ++l) {
        s.linkVelocity[l].resize(5);
        for (int p = 0; p < 5; ++p) s.linkVelocity[l][p] = 0.0f;
    }
    auto d = compute(path, s, kGravityFps2);
    EXPECT_NEAR(d.minHgl[0], 100.0, 1e-9);
    EXPECT_NEAR(d.maxHgl[0], 103.0, 1e-9);
    EXPECT_NEAR(d.minHgl[1], 100.0, 1e-9);
    EXPECT_NEAR(d.maxHgl[1], 105.0, 1e-9);
    EXPECT_NEAR(d.minHgl[2], 100.0, 1e-9);
    EXPECT_NEAR(d.maxHgl[2], 103.0, 1e-9);
    // With zero velocity throughout, EGL min/max == HGL min/max.
    EXPECT_NEAR(d.minEgl[1], d.minHgl[1], 1e-9);
    EXPECT_NEAR(d.maxEgl[1], d.maxHgl[1], 1e-9);
}

// ---- 10. Empty / single-node path → validate() error ---------------------

TEST(ProfileBuilder, Validate_RejectsLessThanTwoNodes)
{
    PathStatic empty;
    SourceSeries src;
    auto d = validate(empty, src);
    EXPECT_FALSE(d.error.isEmpty());

    PathStatic single;
    single.nodes.push_back(NodeStatic{});
    d = validate(single, src);
    EXPECT_FALSE(d.error.isEmpty());
}

// ---- 11. Mismatched links-vs-nodes count → validate() error -------------

TEST(ProfileBuilder, Validate_RejectsLinkCountMismatch)
{
    PathStatic p;
    p.nodes.resize(3);
    p.links.resize(1);  // should be 2
    SourceSeries src;
    src.nodeHead.resize(3);
    auto d = validate(p, src);
    EXPECT_FALSE(d.error.isEmpty());
}

// ---- 12. Mismatched nodeHead first-dim → validate() error ---------------

TEST(ProfileBuilder, Validate_RejectsNodeHeadDimMismatch)
{
    auto path = linearPath(3);
    SourceSeries src;
    src.periodCount = 1;
    src.reportStepSec = 60;
    src.nodeHead.resize(2);  // should be 3
    src.linkVelocity.resize(2);
    auto d = validate(path, src);
    EXPECT_FALSE(d.error.isEmpty());
}

// ---- 13. Crown elevation = invert + maxDepth ----------------------------
//   Ground is invert + nodeMaxDepth; crown above a conduit at its upstream
//   end is `conduitInvertUpstream + link.maxDepth`.

TEST(ProfileBuilder, Crown_EqualsConduitInvertPlusLinkMaxDepth)
{
    PathStatic p = linearPath(2, 100.0, 5.0);
    p.links[0].offset1 = 0.5;
    p.links[0].maxDepth = 2.0;
    const double crownUp = conduitInvertUpstream(p, 0) + p.links[0].maxDepth;
    EXPECT_DOUBLE_EQ(crownUp, 102.5);
    EXPECT_DOUBLE_EQ(groundElev(p.nodes[0]), 105.0);
}

// ---- 14. invert→max fill semantic: max_hgl ≥ invert per node ------------

TEST(ProfileBuilder, MaxHgl_GreaterThanOrEqualToInvert_AtEveryNode)
{
    auto path = linearPath(4);
    auto src  = constantSource(path,
                               {100.5, 101.0, 100.2, 100.0},
                               {1.0, 1.0, 1.0});
    auto d = compute(path, src, kGravityFps2);
    for (int n = 0; n < path.nodes.size(); ++n)
        EXPECT_GE(d.maxHgl[n], path.nodes[n].invertElev);
}

// ---- 15. SI gravity produces different EGL than US gravity, same HGL ----

TEST(ProfileBuilder, GravityUnits_AffectEGLButNotHGL)
{
    auto path = linearPath(2);
    auto src  = constantSource(path, {103.0, 103.0}, {3.0});
    auto us = compute(path, src, kGravityFps2);
    auto si = compute(path, src, kGravityMps2);
    EXPECT_NEAR(us.hglByPeriod[0][0], si.hglByPeriod[0][0], 1e-9);
    EXPECT_GT(us.eglByPeriod[0][0], us.hglByPeriod[0][0]);
    EXPECT_GT(si.eglByPeriod[0][0], si.hglByPeriod[0][0]);
    EXPECT_NE(us.eglByPeriod[0][0], si.eglByPeriod[0][0]);
}

// ---- 16. Zero velocity: EGL == HGL exactly -------------------------------

TEST(ProfileBuilder, ZeroVelocity_EGLEqualsHGL)
{
    auto path = linearPath(3);
    auto src  = constantSource(path, {102.0, 102.0, 102.0}, {0.0, 0.0});
    auto d    = compute(path, src, kGravityFps2);
    // SourceDerived is period-major: outer = period (0), inner = node n.
    for (int n = 0; n < 3; ++n) {
        EXPECT_DOUBLE_EQ(d.hglByPeriod[0][n], d.eglByPeriod[0][n]);
        EXPECT_DOUBLE_EQ(d.minHgl[n], d.minEgl[n]);
        EXPECT_DOUBLE_EQ(d.maxHgl[n], d.maxEgl[n]);
    }
}

// ---- 17. WaterSurface = invert + nodeDepth per node, per period ----------

TEST(ProfileBuilder, WaterSurface_EqualsInvertPlusDepth)
{
    auto path = linearPath(3, /*invertElev*/ 100.0, /*nodeMaxDepth*/ 5.0);
    // constantSource sets nodeDepth = head - invert, so WaterSurface == head here.
    auto src  = constantSource(path, {102.0, 103.5, 101.0}, {0.0, 0.0});
    auto d    = compute(path, src, kGravityFps2);
    // SourceDerived is period-major: outer = periodCount (1), inner = N (3).
    ASSERT_EQ(d.waterSurfaceByPeriod.size(), 1);
    ASSERT_EQ(d.waterSurfaceByPeriod[0].size(), 3);
    EXPECT_NEAR(d.waterSurfaceByPeriod[0][0], 102.0, 1e-9);
    EXPECT_NEAR(d.waterSurfaceByPeriod[0][1], 103.5, 1e-9);
    EXPECT_NEAR(d.waterSurfaceByPeriod[0][2], 101.0, 1e-9);
}

// ---- 18. WaterSurface differs from HGL under pressurization --------------
//
//   Pressurized scenario: NodeHead exceeds rim (invert + maxDepth), but the
//   free-surface depth caps at maxDepth — so WaterSurface = invert + maxDepth
//   while HGL keeps climbing.  Confirms ProfileBuilder doesn't conflate them.

TEST(ProfileBuilder, WaterSurface_CapsAtRimWhenPressurized)
{
    auto path = linearPath(2, /*invertElev*/ 100.0, /*nodeMaxDepth*/ 5.0);
    SourceSeries s;
    s.sourceId      = "src";
    s.reportStepSec = 60;
    s.periodCount   = 1;
    s.nodeHead.resize(2);
    s.nodeDepth.resize(2);
    s.linkVelocity.resize(1);
    s.nodeHead[0]  = QVector<float>{108.0f};  // 3 ft above rim
    s.nodeHead[1]  = QVector<float>{107.0f};
    s.nodeDepth[0] = QVector<float>{5.0f};    // depth capped at maxDepth
    s.nodeDepth[1] = QVector<float>{5.0f};
    s.linkVelocity[0] = QVector<float>{0.0f};

    auto d = compute(path, s, kGravityFps2);
    // SourceDerived is period-major: indices are [period][node].
    EXPECT_NEAR(d.hglByPeriod[0][0], 108.0, 1e-9);
    EXPECT_NEAR(d.hglByPeriod[0][1], 107.0, 1e-9);
    EXPECT_NEAR(d.waterSurfaceByPeriod[0][0], 105.0, 1e-9);
    EXPECT_NEAR(d.waterSurfaceByPeriod[0][1], 105.0, 1e-9);
    EXPECT_GT(d.hglByPeriod[0][0], d.waterSurfaceByPeriod[0][0]);
}

// ---- 19. WaterSurface envelope tracks min/max across periods -------------

TEST(ProfileBuilder, WaterSurface_EnvelopeMatchesBruteForce)
{
    auto path = linearPath(3, /*invertElev*/ 100.0, /*nodeMaxDepth*/ 5.0);
    SourceSeries s;
    s.sourceId      = "src";
    s.reportStepSec = 60;
    s.periodCount   = 4;
    s.nodeHead.resize(3);
    s.nodeDepth.resize(3);
    s.linkVelocity.resize(2);
    const float depths[3][4] = {
        {1.0f, 2.5f, 0.5f, 3.0f},
        {2.0f, 2.0f, 4.0f, 1.0f},
        {0.0f, 0.5f, 1.5f, 0.5f},
    };
    for (int n = 0; n < 3; ++n) {
        s.nodeHead[n].resize(4);
        s.nodeDepth[n].resize(4);
        for (int p = 0; p < 4; ++p) {
            s.nodeHead[n][p]  = 100.0f + depths[n][p];
            s.nodeDepth[n][p] = depths[n][p];
        }
    }
    for (int l = 0; l < 2; ++l) {
        s.linkVelocity[l].assign(4, 0.0f);
    }
    auto d = compute(path, s, kGravityFps2);
    EXPECT_NEAR(d.minWaterSurface[0], 100.5, 1e-9);
    EXPECT_NEAR(d.maxWaterSurface[0], 103.0, 1e-9);
    EXPECT_NEAR(d.minWaterSurface[1], 101.0, 1e-9);
    EXPECT_NEAR(d.maxWaterSurface[1], 104.0, 1e-9);
    EXPECT_NEAR(d.minWaterSurface[2], 100.0, 1e-9);
    EXPECT_NEAR(d.maxWaterSurface[2], 101.5, 1e-9);
}

// ---- 20. Missing nodeDepth → waterSurfaceByPeriod populated as NaN -------
//
//   Builder must not fabricate a water surface when the caller skipped
//   fetching depth.  Periods exist (so the array is sized) but each entry
//   is NaN; envelope remains at the +/-inf sentinel to flag "no data".

TEST(ProfileBuilder, WaterSurface_MissingDepthLeavesNaN)
{
    auto path = linearPath(2, /*invertElev*/ 100.0, /*nodeMaxDepth*/ 5.0);
    SourceSeries s;
    s.sourceId      = "src";
    s.reportStepSec = 60;
    s.periodCount   = 2;
    s.nodeHead.resize(2);
    s.linkVelocity.resize(1);
    s.nodeHead[0] = QVector<float>{102.0f, 103.0f};
    s.nodeHead[1] = QVector<float>{102.0f, 103.0f};
    s.linkVelocity[0] = QVector<float>{0.0f, 0.0f};
    // nodeDepth intentionally left empty.

    auto d = compute(path, s, kGravityFps2);
    ASSERT_EQ(d.waterSurfaceByPeriod.size(), 2);
    ASSERT_EQ(d.waterSurfaceByPeriod[0].size(), 2);
    EXPECT_TRUE(std::isnan(d.waterSurfaceByPeriod[0][0]));
    EXPECT_TRUE(std::isnan(d.waterSurfaceByPeriod[1][1]));
    EXPECT_TRUE(std::isinf(d.minWaterSurface[0]));
    EXPECT_TRUE(std::isinf(d.maxWaterSurface[0]));
}
