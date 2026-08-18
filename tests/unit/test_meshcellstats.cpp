/*!
 * \file   test_meshcellstats.cpp
 * \brief  Cell-area statistics for the mesh Metadata tab (Plan Part A).
 *
 * Exercises mesh::computeCellAreaStats: known areas, odd/even median,
 * degenerate-index skipping, and the empty mesh.
 */
#include <gtest/gtest.h>

#include "mesh/meshcellstats.h"
#include "mesh/meshresult.h"

#include <cmath>

namespace {

mesh::MeshVertex v(double x, double y)
{
    mesh::MeshVertex mv;
    mv.xy = QPointF(x, y);
    return mv;
}

mesh::MeshTriangle tri(int a, int b, int c)
{
    mesh::MeshTriangle t;
    t.v0 = a; t.v1 = b; t.v2 = c;
    return t;
}

} // namespace

TEST(MeshCellStats, EmptyMeshYieldsZeroCount)
{
    const mesh::MeshResult m;
    const auto s = mesh::computeCellAreaStats(m);
    EXPECT_EQ(s.count, 0);
}

TEST(MeshCellStats, KnownAreasOddCount)
{
    // Three right triangles with legs (1,1), (2,2), (3,3):
    // areas 0.5, 2.0, 4.5 → min 0.5, max 4.5, mean 7/3, median 2.0.
    mesh::MeshResult m;
    m.vertices = { v(0,0), v(1,0), v(0,1),
                   v(10,0), v(12,0), v(10,2),
                   v(20,0), v(23,0), v(20,3) };
    m.triangles = { tri(0,1,2), tri(3,4,5), tri(6,7,8) };

    const auto s = mesh::computeCellAreaStats(m);
    ASSERT_EQ(s.count, 3);
    EXPECT_DOUBLE_EQ(s.min,    0.5);
    EXPECT_DOUBLE_EQ(s.max,    4.5);
    EXPECT_DOUBLE_EQ(s.mean,   7.0 / 3.0);
    EXPECT_DOUBLE_EQ(s.median, 2.0);
}

TEST(MeshCellStats, EvenCountMedianAveragesMiddlePair)
{
    // Areas 0.5, 2.0, 4.5, 8.0 → median (2.0 + 4.5)/2 = 3.25.
    mesh::MeshResult m;
    m.vertices = { v(0,0), v(1,0), v(0,1),
                   v(10,0), v(12,0), v(10,2),
                   v(20,0), v(23,0), v(20,3),
                   v(30,0), v(34,0), v(30,4) };
    m.triangles = { tri(0,1,2), tri(3,4,5), tri(6,7,8), tri(9,10,11) };

    const auto s = mesh::computeCellAreaStats(m);
    ASSERT_EQ(s.count, 4);
    EXPECT_DOUBLE_EQ(s.median, 3.25);
    EXPECT_DOUBLE_EQ(s.mean,   (0.5 + 2.0 + 4.5 + 8.0) / 4.0);
}

TEST(MeshCellStats, OutOfRangeIndicesAreSkipped)
{
    mesh::MeshResult m;
    m.vertices  = { v(0,0), v(1,0), v(0,1) };
    m.triangles = { tri(0,1,2), tri(0,1,99), tri(-1,1,2) };

    const auto s = mesh::computeCellAreaStats(m);
    ASSERT_EQ(s.count, 1);
    EXPECT_DOUBLE_EQ(s.min, 0.5);
    EXPECT_DOUBLE_EQ(s.max, 0.5);
}

TEST(MeshCellStats, WindingOrderDoesNotAffectArea)
{
    mesh::MeshResult m;
    m.vertices  = { v(0,0), v(1,0), v(0,1) };
    m.triangles = { tri(0,1,2), tri(0,2,1) };   // CCW and CW

    const auto s = mesh::computeCellAreaStats(m);
    ASSERT_EQ(s.count, 2);
    EXPECT_DOUBLE_EQ(s.min, 0.5);
    EXPECT_DOUBLE_EQ(s.max, 0.5);
}

// ── CFL characteristic length ───────────────────────────────────────────────
// mesh::computeCflStats mirrors the engine's cell_lchar
// (openswmm.engine src/engine/2d/solver/InertialEdges.cpp:99-135). These pin
// the two closed forms the engine's own comment derives, so a divergence in
// either copy shows up here rather than as a wrong timestep.

TEST(MeshCflStats, EmptyMeshYieldsZeroCount)
{
    const mesh::MeshResult m;
    const auto s = mesh::computeCflStats(m);
    EXPECT_EQ(s.count, 0);
}

TEST(MeshCflStats, UnionJackRightTrianglePairMatchesTheEngineConstant)
{
    // Two right triangles sharing the diagonal of a d x d square — the
    // configuration InertialEdges.cpp:108-110 says yields 0.408*d.
    //   (0,0) (d,0) (d,d) (0,d), diagonal 0-2.
    const double d = 10.0;
    mesh::MeshResult m;
    m.vertices = { v(0,0), v(d,0), v(d,d), v(0,d) };
    m.triangles = { tri(0,1,2), tri(0,2,3) };

    const QVector<double> L = mesh::computeCellLchar(m);
    ASSERT_EQ(L.size(), 2);

    // One interior face: the diagonal, length d*sqrt(2). Centroids are
    // (2d/3, d/3) and (d/3, 2d/3); their offset is perpendicular to the
    // diagonal, so dn = |dc| = d*sqrt(2)/3 and the 0.3*chord floor is slack.
    // S = xi/dn = 3, A = d^2/2, so L = sqrt(2*(d^2/2)/3) = d/sqrt(3).
    const double expect = d / std::sqrt(3.0);
    EXPECT_NEAR(L[0], expect, 1e-12);
    EXPECT_NEAR(L[1], expect, 1e-12);

    const auto s = mesh::computeCflStats(m);
    EXPECT_EQ(s.count, 2);
    EXPECT_EQ(s.interiorFaces, 1);
    EXPECT_EQ(s.isolatedCells, 0);
    EXPECT_NEAR(s.min, expect, 1e-12);
    EXPECT_NEAR(s.ratio, 1.0, 1e-12);   // uniform mesh
    EXPECT_EQ(s.impliedTiers, 1);
}

TEST(MeshCflStats, IsolatedCellUsesTheBoundaryFallback)
{
    // A lone triangle has no interior face, so the engine falls back to
    // 2A / longest_edge (InertialEdges.cpp:127-133).
    mesh::MeshResult m;
    m.vertices = { v(0,0), v(4,0), v(0,3) };
    m.triangles = { tri(0,1,2) };

    const QVector<double> L = mesh::computeCellLchar(m);
    ASSERT_EQ(L.size(), 1);
    EXPECT_NEAR(L[0], 2.0 * 6.0 / 5.0, 1e-12);   // A=6, longest edge=5

    const auto s = mesh::computeCflStats(m);
    EXPECT_EQ(s.interiorFaces, 0);
    EXPECT_EQ(s.isolatedCells, 1);
}

TEST(MeshCflStats, SliverDominatesTheMinimumAndTheWorkProxy)
{
    // A uniform pair plus a sliver sharing an edge with it. The sliver must
    // set min, and ltsWork must rise sharply — that is the whole point of the
    // proxy over a spread ratio.
    const double d = 10.0;
    mesh::MeshResult uniform;
    uniform.vertices = { v(0,0), v(d,0), v(d,d), v(0,d) };
    uniform.triangles = { tri(0,1,2), tri(0,2,3) };
    const auto su = mesh::computeCflStats(uniform);

    mesh::MeshResult withSliver = uniform;
    withSliver.vertices.append(v(d + 0.001, d / 2.0));   // a hair off edge 1-2
    withSliver.triangles.append(tri(1, 4, 2));
    const auto ss = mesh::computeCflStats(withSliver);

    // The 0.001-wide sliver against a d/sqrt(3) = 5.77 uniform cell lands
    // near 0.058 — about 100x down. Assert the behaviour with room, not a
    // round number that happens to sit on the boundary.
    ASSERT_EQ(ss.count, 3);
    EXPECT_LT(ss.min, su.min / 50.0);
    EXPECT_GT(ss.ratio, 50.0);
    EXPECT_GT(ss.impliedTiers, su.impliedTiers);
    EXPECT_GT(ss.ltsWork, su.ltsWork);
}

TEST(MeshCflStats, RefinementExplosionIsPunishedByWorkButNotByTheRatio)
{
    // The h=8 pathology in miniature: many more cells AND a smaller minimum.
    // A spread ratio can improve under that (the median falls); ltsWork must
    // not. This is the reason the gate is ltsWork.
    const double d = 10.0;
    mesh::MeshResult coarse;
    coarse.vertices = { v(0,0), v(d,0), v(d,d), v(0,d) };
    coarse.triangles = { tri(0,1,2), tri(0,2,3) };
    const auto sc = mesh::computeCflStats(coarse);

    // Same square, cut into many small triangles, plus one sliver.
    mesh::MeshResult fine;
    const int n = 12;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            fine.vertices.append(v(i * d / n, j * d / n));
    auto id = [n](int i, int j) { return j * (n + 1) + i; };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
        {
            fine.triangles.append(tri(id(i,j), id(i+1,j), id(i+1,j+1)));
            fine.triangles.append(tri(id(i,j), id(i+1,j+1), id(i,j+1)));
        }
    fine.vertices.append(v(d + 0.001, d / 2.0));
    fine.triangles.append(tri(id(n,0), int(fine.vertices.size()) - 1, id(n,n)));
    const auto sf = mesh::computeCflStats(fine);

    EXPECT_GT(sf.count, sc.count * 10);
    EXPECT_LT(sf.min, sc.min);
    EXPECT_GT(sf.ltsWork, sc.ltsWork);   // strictly worse, and the proxy says so
}
