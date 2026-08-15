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
