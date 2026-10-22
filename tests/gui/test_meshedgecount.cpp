/**
 * @file      test_meshedgecount.cpp
 * @brief     Tests for meshedgecount.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Validates the conforming-mesh edge identity that
 * SWMM2DMeshLayer::edgeCount() falls back on before the scene geometry is
 * built: total unique edges E satisfies  2·E = Σ nv + B  (nv = 3 per triangle,
 * 4 per quad; B = boundary edges) — for an all-triangle mesh the historical
 * 2·E = 3·T + B. Hand-counted against four meshes (single triangle, quad as
 * two triangles, strip, mixed triangle + quad), checked both ways — by
 * deduplicating cell edges via mesh::edgeEndpoints (as rebuildSceneGeometry
 * does) and by the formula — so a mis-stated formula or operator-precedence
 * slip is caught. Pure value types; links no .cpp, writes no temp files.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/meshcellgeom.h"
#include "mesh/meshresult.h"

#include <QObject>
#include <QPair>
#include <QSet>
#include <QTest>

using mesh::MeshResult;
using mesh::MeshTriangle;
using mesh::MeshEdge;

namespace
{

/**
 * @brief Construct a three-vertex cell topology value without geometry lookup.
 * @param[in] a First fixture vertex ID.
 * @param[in] b Second fixture vertex ID.
 * @param[in] c Third fixture vertex ID.
 * @return Owned triangle with supplied IDs and default fourth-vertex sentinel. No ID
 * validity/orientation check, vertex storage ownership or allocation.
 */
MeshTriangle tri(int a, int b, int c)
{
    MeshTriangle t;
    t.v0 = a;
    t.v1 = b;
    t.v2 = c;
    return t;
}

/**
 * @brief Construct a four-vertex cell topology value without geometry lookup.
 * @param[in] a First fixture vertex ID.
 * @param[in] b Second fixture vertex ID.
 * @param[in] c Third fixture vertex ID.
 * @param[in] d Fourth fixture vertex ID, marks quad topology.
 * @return Owned cell with supplied order, no convexity/orientation/index validation or
 * allocation.
 */
MeshTriangle quad(int a, int b, int c, int d)
{
    MeshTriangle t;
    t.v0 = a;
    t.v1 = b;
    t.v2 = c;
    t.v3 = d;
    return t;
}

/**
 * @brief Construct an unordered-boundary fixture edge from two vertex IDs.
 * @param[in] a First endpoint ID, copied verbatim.
 * @param[in] b Second endpoint ID, copied verbatim.
 * @return Owned MeshEdge with default other metadata; no coordinate lookup, validation or
 * mutation.
 */
MeshEdge edge(int a, int b)
{
    MeshEdge e;
    e.v0 = a;
    e.v1 = b;
    return e;
}

// Deduplicated unique-edge count over the cell list — the same keying
// rebuildSceneGeometry() uses (unordered vertex pair), one edge per local
// edge slot k < vertexCount() (never the padding slot).
/**
 * @brief Count unique cell-edge identities using unordered endpoint pairs.
 * @param[in] m Stable valid triangle/quad topology, coordinates/boundary-edge list not needed.
 * @return Number of distinct actual edges over vertexCount() slots; triangle padding excluded.
 * No conforming-mesh check, input mutation or external state; QSet allocation may fail.
 */
int uniqueEdges(const MeshResult &m)
{
    QSet<QPair<int, int>> seen;
    auto add = [&](int a, int b) { seen.insert(a < b ? qMakePair(a, b) : qMakePair(b, a)); };
    for (const auto &t : m.triangles) {
        for (int k = 0; k < t.vertexCount(); ++k) {
            int a = 0, b = 0;
            mesh::edgeEndpoints(t, k, a, b);
            add(a, b);
        }
    }
    return seen.size();
}

// The shipped fallback formula: 2·E = Σ nv + B.
/**
 * @brief Evaluate the conforming mesh edge identity from cell vertex counts and boundary list
 * size.
 * @param[in] m Stable conforming triangle/quad cells with each physical boundary edge listed
 * once.
 * @return Integer(sum cell vertexCount+boundaryEdges.size)/2. No parity/nonmanifold/topology
 * validation; invalid fixture may truncate half edge. Read-only, allocation-free, products must
 * fit int.
 */
int formulaEdges(const MeshResult &m)
{
    int sumNv = 0;
    for (const auto &t : m.triangles)
        sumNv += t.vertexCount();
    return (sumNv + int(m.boundaryEdges.size())) / 2;
}

} // namespace

class TestMeshEdgeCount : public QObject
{
    Q_OBJECT
private slots:

    /**
     * @brief Verify explicit edge deduplication and conforming fallback both count 3 for one
     * boundary triangle.
     * @note Fixture needs only connectivity IDs, no layer/rendering/native engine; exact integer
     * QtTest checks.
     */
    void single_triangle()
    {
        MeshResult m;
        m.triangles = {tri(0, 1, 2)};
        m.boundaryEdges = {edge(0, 1), edge(1, 2), edge(2, 0)}; // all on boundary
        QCOMPARE(uniqueEdges(m), 3);
        QCOMPARE(formulaEdges(m), 3);
    }

    /**
     * @brief Verify two triangles sharing a diagonal yield 5 unique edges under both counting
     * methods.
     * @note Four explicit boundary edges plus shared interior(0,2), exact deterministic topology
     * assertions.
     */
    void quad_two_triangles()
    {
        MeshResult m;
        m.triangles = {tri(0, 1, 2), tri(0, 2, 3)}; // shared edge (0,2)
        m.boundaryEdges = {edge(0, 1), edge(1, 2), edge(2, 3), edge(3, 0)};
        QCOMPARE(uniqueEdges(m), 5); // 4 boundary + 1 interior
        QCOMPARE(formulaEdges(m), 5);
    }

    /**
     * @brief Verify a three-triangle strip counts 7 edges from 5 boundary and 2 shared interior
     * edges.
     * @note Compares independent pair deduplication and vertex-count formula against hand count,
     * no geometry allocation.
     */
    void triangle_strip()
    {
        MeshResult m;
        m.triangles = {tri(0, 1, 2), tri(1, 2, 3), tri(2, 3, 4)}; // shared (1,2) and (2,3)
        m.boundaryEdges = {edge(0, 1), edge(0, 2), edge(1, 3), edge(3, 4), edge(2, 4)};
        QCOMPARE(uniqueEdges(m), 7); // 5 boundary + 2 interior
        QCOMPARE(formulaEdges(m), 7);
    }

    // Mixed: 2x1 strip, left square as two triangles, right square one quad
    // (cell 2). Edge slots are stride 4; a triangle's slot 3 is never an edge.
    /**
     * @brief Verify mixed-cell fallback counts 8 edges and quad edge-slot identity uses stride
     * 4.
     * @note Two triangles plus one quad with 6 boundary edges, total reserved slots 12; packed
     * cell/local round trip and quad local edge 3 endpoints(1,2) checked. No triangle padding
     * treated as physical edge.
     */
    void mixed_triangles_and_quad()
    {
        MeshResult m;
        m.triangles = {tri(0, 1, 4), tri(0, 4, 3), quad(1, 2, 5, 4)};
        m.boundaryEdges = {edge(0, 1), edge(1, 2), edge(2, 5), edge(5, 4), edge(4, 3), edge(3, 0)};
        QCOMPARE(uniqueEdges(m), 8);  // 6 boundary + (0,4) + (1,4)
        QCOMPARE(formulaEdges(m), 8); // (3+3+4 + 6) / 2
        QCOMPARE(mesh::edgeSlotCount(int(m.triangles.size())), 12);
        QCOMPARE(mesh::slotCell(mesh::edgeSlot(2, 3)), 2);
        QCOMPARE(mesh::slotLocal(mesh::edgeSlot(2, 3)), 3);
        // The quad's edge 3 is its (V1,V2) side, as in the engine.
        int a = 0, b = 0;
        mesh::edgeEndpoints(m.triangles[2], 3, a, b);
        QCOMPARE(a, 1);
        QCOMPARE(b, 2);
    }
};

QTEST_APPLESS_MAIN(TestMeshEdgeCount)
#include "test_meshedgecount.moc"
