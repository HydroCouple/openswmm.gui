/*!
 * \file   test_meshgwpreview.cpp
 * \brief  Remap's read-only 2D-aquifer preview (INFILTRATION_TO_2D_AQUIFER
 *         _AND_REMAP_PLAN Step 2).
 *
 * Exercises mesh::previewGroundwaterMapping and its clipping helpers: node
 * inside / outside, a seeping conduit half inside, a conduit lying on a
 * shared cell edge counted once, a subcatchment half inside (area share),
 * a concave subcatchment, lumped-GW and polygon-less subcatchments, a
 * clockwise cell, and a quad cell.
 */
#include <gtest/gtest.h>

#include <QPointF>
#include <QVector>

#include <algorithm>
#include <cmath>

#include "mesh/meshgwpreview.h"
#include "mesh/meshresult.h"

namespace {

/*! Unit square split into two triangles along the (1,0)–(0,1) diagonal;
 *  tri 1 is deliberately clockwise. */
mesh::MeshResult makeSquareMesh()
{
    mesh::MeshResult m;
    auto v = [](double x, double y) {
        mesh::MeshVertex mv;
        mv.xy = QPointF(x, y);
        return mv;
    };
    m.vertices = { v(0,0), v(1,0), v(0,1), v(1,1) };
    mesh::MeshTriangle t0; t0.v0 = 0; t0.v1 = 1; t0.v2 = 2;   // CCW
    mesh::MeshTriangle t1; t1.v0 = 1; t1.v1 = 2; t1.v2 = 3;   // CW
    m.triangles = { t0, t1 };
    m.ok = true;
    return m;
}

mesh::Ring rect(double x0, double y0, double x1, double y1)
{
    return mesh::Ring({ QPointF(x0, y0), QPointF(x1, y0), QPointF(x1, y1), QPointF(x0, y1) });
}

} // namespace

TEST(MeshGwPreview, ClippedAreaOfOverlappingSquare)
{
    const mesh::Ring cell = rect(0, 0, 1, 1);
    EXPECT_NEAR(mesh::clippedArea(rect(0.5, 0, 1.5, 1), cell), 0.5, 1e-12);
    EXPECT_NEAR(mesh::clippedArea(rect(2, 2, 3, 3), cell), 0.0, 1e-12);
    // Clockwise clip polygon gives the same answer.
    mesh::Ring cw = cell;
    std::reverse(cw.begin(), cw.end());
    EXPECT_NEAR(mesh::clippedArea(rect(0.5, 0, 1.5, 1), cw), 0.5, 1e-12);
}

TEST(MeshGwPreview, ClippedAreaOfConcaveSubject)
{
    // L-shape covering [0,2]x[0,2] minus [1,2]x[1,2] (area 3); clipped to
    // the unit cell [0,1]^2 → 1; to [1,2]x[0,1] → 1; to [1,2]x[1,2] → 0.
    const mesh::Ring L({ QPointF(0,0), QPointF(2,0), QPointF(2,1),
                        QPointF(1,1), QPointF(1,2), QPointF(0,2) });
    EXPECT_NEAR(mesh::clippedArea(L, rect(0, 0, 1, 1)), 1.0, 1e-12);
    EXPECT_NEAR(mesh::clippedArea(L, rect(1, 0, 2, 1)), 1.0, 1e-12);
    EXPECT_NEAR(mesh::clippedArea(L, rect(1, 1, 2, 2)), 0.0, 1e-12);
}

TEST(MeshGwPreview, ClippedLength)
{
    const mesh::Ring cell = rect(0, 0, 1, 1);
    EXPECT_NEAR(mesh::clippedLength(QPointF(-1, 0.5), QPointF(1, 0.5), cell), 1.0, 1e-12);
    EXPECT_NEAR(mesh::clippedLength(QPointF(-1, 2), QPointF(1, 2), cell), 0.0, 1e-12);
}

TEST(MeshGwPreview, NodesInsideAndOutside)
{
    mesh::GwPreviewInput in;
    in.nodes = { { QStringLiteral("IN1"), QPointF(0.2, 0.2) },     // tri 0
                 { QStringLiteral("IN2"), QPointF(0.9, 0.9) },     // tri 1 (CW)
                 { QStringLiteral("OUT"), QPointF(5.0, 5.0) } };
    const auto r = mesh::previewGroundwaterMapping(makeSquareMesh(), in);
    EXPECT_EQ(r.nodesInside, 2);
    EXPECT_EQ(r.nodesOutside, QStringList{ QStringLiteral("OUT") });
}

TEST(MeshGwPreview, SeepingConduitShares)
{
    mesh::GwPreviewInput in;
    in.seepingConduits = {
        { QStringLiteral("HALF"), { QPointF(-1, 0.25), QPointF(1, 0.25) } },
        // Lies exactly on the shared diagonal — inside both cells; counted once.
        { QStringLiteral("EDGE"), { QPointF(0, 1), QPointF(1, 0) } },
        { QStringLiteral("AWAY"), { QPointF(3, 3), QPointF(4, 3) } },
    };
    const auto r = mesh::previewGroundwaterMapping(makeSquareMesh(), in);
    EXPECT_EQ(r.conduitsInside, 2);
    EXPECT_EQ(r.conduitsOutside, QStringList{ QStringLiteral("AWAY") });
    EXPECT_NEAR(r.conduitLengthTotal, 2.0 + std::sqrt(2.0) + 1.0, 1e-12);
    EXPECT_NEAR(r.conduitLengthInside, 1.0 + std::sqrt(2.0), 1e-12);
}

TEST(MeshGwPreview, SubcatchmentRouting)
{
    mesh::GwPreviewInput in;
    in.subcatchments = {
        { QStringLiteral("HALF"),   rect(0.5, 0, 1.5, 1), false },
        { QStringLiteral("LUMPED"), rect(0, 0, 1, 1),     true  },
        { QStringLiteral("AWAY"),   rect(3, 3, 4, 4),     false },
        { QStringLiteral("NOPOLY"), mesh::Ring(),          false },
    };
    const auto r = mesh::previewGroundwaterMapping(makeSquareMesh(), in);
    EXPECT_EQ(r.subcatchTo2D, 1);
    EXPECT_EQ(r.subcatchLumped, QStringList{ QStringLiteral("LUMPED") });
    EXPECT_EQ(r.subcatchOutside, QStringList{ QStringLiteral("AWAY") });
    EXPECT_EQ(r.subcatchNoPolygon, QStringList{ QStringLiteral("NOPOLY") });
    // Lumped subcatchments contribute no area to the 2D share.
    EXPECT_NEAR(r.subcatchAreaTotal, 2.0, 1e-12);
    EXPECT_NEAR(r.subcatchAreaInside, 0.5, 1e-12);
}

TEST(MeshGwPreview, QuadCell)
{
    mesh::MeshResult m;
    auto v = [](double x, double y) { mesh::MeshVertex mv; mv.xy = QPointF(x, y); return mv; };
    m.vertices = { v(0,0), v(2,0), v(2,2), v(0,2) };
    mesh::MeshTriangle q; q.v0 = 0; q.v1 = 1; q.v2 = 2; q.v3 = 3;
    m.triangles = { q };
    m.ok = true;

    mesh::GwPreviewInput in;
    in.nodes = { { QStringLiteral("N"), QPointF(1.5, 1.5) } };
    in.subcatchments = { { QStringLiteral("S"), rect(1, 1, 3, 3), false } };
    const auto r = mesh::previewGroundwaterMapping(m, in);
    EXPECT_EQ(r.nodesInside, 1);
    EXPECT_NEAR(r.subcatchAreaInside, 1.0, 1e-12);
    EXPECT_NEAR(r.subcatchAreaTotal, 4.0, 1e-12);
}
