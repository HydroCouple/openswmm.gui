/*!
 * \file   test_meshstaticgeometrybuffers.cpp
 * \brief  Unit tests for MeshStaticGeometryBuffers (QSG-2D-1M Phase 5).
 *
 * Phase 5 contract:
 *   - two triangles sharing an edge produce SHARED vertex positions,
 *   - triangle indices reference valid vertices (invalid cells dropped),
 *   - anchor-relative positions are stable across pan and zoom (same
 *     revision → no rebuild, byte-identical buffers),
 *   - a geometry-revision change rebuilds,
 *   - style / data / selection events (same revision) never rebuild.
 */

#include <gtest/gtest.h>

#include "render/meshstaticgeometrybuffers.h"

#include <QPointF>
#include <QVector>

#include <array>
#include <vector>

using OpenSWMM::Render::MeshStaticGeometryBuffers;

namespace {

/*! Two triangles sharing edge (1,2): a unit-square split on the diagonal. */
struct TwoTriangleMesh
{
    QVector<QPointF> verts{
        QPointF(0.0, 0.0), QPointF(1.0, 0.0),
        QPointF(0.0, 1.0), QPointF(1.0, 1.0)};
    std::vector<std::array<int, 3>> tris{{0, 1, 2}, {1, 3, 2}};

    auto accessor() const
    {
        return [this](qint64 i, int &v0, int &v1, int &v2) {
            v0 = tris[size_t(i)][0];
            v1 = tris[size_t(i)][1];
            v2 = tris[size_t(i)][2];
        };
    }
};

} // namespace

TEST(MeshStaticGeometryBuffersTest, SharedEdgeProducesSharedVertexPositions)
{
    TwoTriangleMesh m;
    MeshStaticGeometryBuffers buf;
    ASSERT_TRUE(buf.ensureBuilt(1, 0.0, 0.0, m.verts,
                                qint64(m.tris.size()), m.accessor()));

    // 4 shared positions — NOT 6 expanded corners.
    EXPECT_EQ(buf.vertexCount(), 4);
    EXPECT_EQ(buf.triangleCount(), 2);
    ASSERT_EQ(buf.triIndices().size(), size_t(6));

    // Both triangles reference the same physical entries for the shared
    // edge (vertices 1 and 2).
    const auto &idx = buf.triIndices();
    EXPECT_EQ(idx[1], idx[3]);   // vertex 1
    EXPECT_EQ(idx[2], idx[5]);   // vertex 2
}

TEST(MeshStaticGeometryBuffersTest, TriangleIndicesReferenceValidVertices)
{
    TwoTriangleMesh m;
    m.tris.push_back({0, 1, 99});   // out of range → must be dropped
    m.tris.push_back({-1, 1, 2});   // negative     → must be dropped

    MeshStaticGeometryBuffers buf;
    ASSERT_TRUE(buf.ensureBuilt(1, 0.0, 0.0, m.verts,
                                qint64(m.tris.size()), m.accessor()));
    EXPECT_EQ(buf.triangleCount(), 2);   // only the two valid cells survive
    for (quint32 id : buf.triIndices())
        EXPECT_LT(id, quint32(buf.vertexCount()));
}

TEST(MeshStaticGeometryBuffersTest, PositionsAreAnchorRelative)
{
    TwoTriangleMesh m;
    // Big UTM-style coordinates: anchor keeps floats small.
    const double ax = 500'000.0, ay = 4'000'000.0;
    for (QPointF &p : m.verts) p += QPointF(ax, ay);

    MeshStaticGeometryBuffers buf;
    ASSERT_TRUE(buf.ensureBuilt(7, ax, ay, m.verts,
                                qint64(m.tris.size()), m.accessor()));
    EXPECT_FLOAT_EQ(buf.positions()[0].x, 0.0f);
    EXPECT_FLOAT_EQ(buf.positions()[0].y, 0.0f);
    EXPECT_FLOAT_EQ(buf.positions()[3].x, 1.0f);
    EXPECT_FLOAT_EQ(buf.positions()[3].y, 1.0f);
    EXPECT_EQ(buf.anchorX(), ax);
    EXPECT_EQ(buf.anchorY(), ay);
}

TEST(MeshStaticGeometryBuffersTest, SameRevisionNeverRebuilds)
{
    TwoTriangleMesh m;
    MeshStaticGeometryBuffers buf;
    ASSERT_TRUE(buf.ensureBuilt(42, 0.0, 0.0, m.verts,
                                qint64(m.tris.size()), m.accessor()));
    const auto *posData = buf.positions().data();
    const auto *idxData = buf.triIndices().data();

    // Pan / zoom / style / data / selection all re-enter ensureBuilt with
    // an unchanged geometry revision — every one must be a no-op, keeping
    // the exact same storage (pointer-stable, byte-identical).
    for (int event = 0; event < 5; ++event) {
        EXPECT_FALSE(buf.ensureBuilt(42, 0.0, 0.0, m.verts,
                                     qint64(m.tris.size()), m.accessor()));
        EXPECT_EQ(buf.positions().data(), posData);
        EXPECT_EQ(buf.triIndices().data(), idxData);
    }
    EXPECT_EQ(buf.revision(), quint64(42));
}

TEST(MeshStaticGeometryBuffersTest, GeometryRevisionChangeRebuilds)
{
    TwoTriangleMesh m;
    MeshStaticGeometryBuffers buf;
    ASSERT_TRUE(buf.ensureBuilt(1, 0.0, 0.0, m.verts,
                                qint64(m.tris.size()), m.accessor()));

    // Mesh edited: a vertex moved, revision bumped.
    m.verts[3] = QPointF(2.0, 2.0);
    EXPECT_TRUE(buf.ensureBuilt(2, 0.0, 0.0, m.verts,
                                qint64(m.tris.size()), m.accessor()));
    EXPECT_EQ(buf.revision(), quint64(2));
    EXPECT_FLOAT_EQ(buf.positions()[3].x, 2.0f);
    EXPECT_FLOAT_EQ(buf.positions()[3].y, 2.0f);
}

TEST(MeshStaticGeometryBuffersTest, EdgeExtractionDeduplicatesSharedEdge)
{
    TwoTriangleMesh m;
    MeshStaticGeometryBuffers buf;
    ASSERT_TRUE(buf.ensureBuilt(1, 0.0, 0.0, m.verts,
                                qint64(m.tris.size()), m.accessor(),
                                /*buildEdges=*/true));
    // 2 triangles × 3 edges = 6 directed, minus the shared (1,2) → 5 unique.
    EXPECT_EQ(buf.edgeCount(), 5);
    const auto &ep = buf.edgeEndpoints();
    for (size_t i = 0; i + 1 < ep.size(); i += 2) {
        EXPECT_LT(ep[i], ep[i + 1]);                 // canonical lo<hi order
        EXPECT_LT(ep[i + 1], quint32(buf.vertexCount()));
    }
}

TEST(MeshStaticGeometryBuffersTest, ClearResetsToUnbuilt)
{
    TwoTriangleMesh m;
    MeshStaticGeometryBuffers buf;
    ASSERT_TRUE(buf.ensureBuilt(1, 0.0, 0.0, m.verts,
                                qint64(m.tris.size()), m.accessor()));
    buf.clear();
    EXPECT_FALSE(buf.isBuilt());
    EXPECT_EQ(buf.vertexCount(), 0);
    // …and the same revision builds again after a clear.
    EXPECT_TRUE(buf.ensureBuilt(1, 0.0, 0.0, m.verts,
                                qint64(m.tris.size()), m.accessor()));
}
