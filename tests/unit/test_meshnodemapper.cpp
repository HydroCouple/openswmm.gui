/*!
 * \file   test_meshnodemapper.cpp
 * \brief  Node→mesh remapping behind the toolbar's Remap 1D↔2D (Plan C.1).
 *
 * Exercises mesh::mapNodesToMesh: coincident → vertex coupling, interior
 * → cell coupling with defaults (Cd 0.65, area 2 m²), several nodes in
 * one cell, outside → unmatched, preserve-existing skips, and the
 * first-wins claim on a shared coincident vertex.
 */
#include <gtest/gtest.h>

#include <QPointF>
#include <QVector>

#include "mesh/meshnodemapper.h"
#include "mesh/meshresult.h"

namespace {

/*! Unit square split into two triangles:
 *
 *      2 ─── 3
 *      │ \ 1 │
 *      │ 0 \ │
 *      0 ─── 1
 *
 *  tri 0 = (0,1,2) lower-left, tri 1 = (1,3,2) upper-right.
 */
mesh::MeshResult makeSquareMesh()
{
    mesh::MeshResult m;
    auto v = [](double x, double y) {
        mesh::MeshVertex mv;
        mv.xy = QPointF(x, y);
        return mv;
    };
    m.vertices = { v(0,0), v(1,0), v(0,1), v(1,1) };
    mesh::MeshTriangle t0; t0.v0 = 0; t0.v1 = 1; t0.v2 = 2;
    mesh::MeshTriangle t1; t1.v0 = 1; t1.v1 = 3; t1.v2 = 2;
    m.triangles = { t0, t1 };
    m.ok = true;
    return m;
}

} // namespace

TEST(MeshNodeMapper, CoincidentNodeCouplesToVertex)
{
    const auto m = makeSquareMesh();
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(1.0, 0.0) },   // vertex 1
    };
    const auto r = mesh::mapNodesToMesh(m, nodes);
    ASSERT_EQ(r.vertexMatches.size(), 1);
    EXPECT_EQ(r.vertexMatches.value(1), QStringLiteral("J1"));
    EXPECT_TRUE(r.cellMatches.isEmpty());
    EXPECT_TRUE(r.unmatched.isEmpty());
}

TEST(MeshNodeMapper, InteriorNodeCouplesToContainingCellWithDefaults)
{
    const auto m = makeSquareMesh();
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J2"), QPointF(0.25, 0.25) },  // inside tri 0
    };
    const auto r = mesh::mapNodesToMesh(m, nodes);
    EXPECT_TRUE(r.vertexMatches.isEmpty());
    ASSERT_EQ(r.cellMatches.size(), 1);
    EXPECT_EQ(r.cellMatches[0].tri, 0);
    EXPECT_EQ(r.cellMatches[0].nodeId, QStringLiteral("J2"));
    EXPECT_DOUBLE_EQ(r.cellMatches[0].cd,   mesh::kCellCouplingDefaultCd);
    EXPECT_DOUBLE_EQ(r.cellMatches[0].area, mesh::kCellCouplingDefaultArea);
}

TEST(MeshNodeMapper, MultipleNodesShareOneCell)
{
    const auto m = makeSquareMesh();
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("W_UP"), QPointF(0.20, 0.20) },  // both in tri 0 —
        { QStringLiteral("W_DN"), QPointF(0.30, 0.30) },  // the weir case
    };
    const auto r = mesh::mapNodesToMesh(m, nodes);
    ASSERT_EQ(r.cellMatches.size(), 2);
    EXPECT_EQ(r.cellMatches[0].tri, 0);
    EXPECT_EQ(r.cellMatches[1].tri, 0);
    EXPECT_EQ(r.sharedCells, 1);
    EXPECT_TRUE(r.unmatched.isEmpty());
}

TEST(MeshNodeMapper, OutsideNodeIsUnmatched)
{
    const auto m = makeSquareMesh();
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("FAR"), QPointF(5.0, 5.0) },
    };
    const auto r = mesh::mapNodesToMesh(m, nodes);
    EXPECT_TRUE(r.vertexMatches.isEmpty());
    EXPECT_TRUE(r.cellMatches.isEmpty());
    ASSERT_EQ(r.unmatched.size(), 1);
    EXPECT_EQ(r.unmatched.first(), QStringLiteral("FAR"));
}

TEST(MeshNodeMapper, PreserveExistingSkipsAlreadyCoupledNode)
{
    auto m = makeSquareMesh();
    m.vertices[1].coupledNode = QStringLiteral("J1");   // pre-coupled
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(0.25, 0.25) },  // would cell-couple
        { QStringLiteral("J2"), QPointF(0.75, 0.75) },  // tri 1
    };
    const auto r = mesh::mapNodesToMesh(m, nodes, -1.0, /*preserveExisting=*/true);
    ASSERT_EQ(r.skippedExisting.size(), 1);
    EXPECT_EQ(r.skippedExisting.first(), QStringLiteral("J1"));
    ASSERT_EQ(r.cellMatches.size(), 1);
    EXPECT_EQ(r.cellMatches[0].nodeId, QStringLiteral("J2"));
    EXPECT_EQ(r.cellMatches[0].tri, 1);
}

TEST(MeshNodeMapper, FullRemapIgnoresExistingWhenAsked)
{
    auto m = makeSquareMesh();
    m.vertices[1].coupledNode = QStringLiteral("J1");
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(0.25, 0.25) },
    };
    const auto r = mesh::mapNodesToMesh(m, nodes, -1.0, /*preserveExisting=*/false);
    EXPECT_TRUE(r.skippedExisting.isEmpty());
    ASSERT_EQ(r.cellMatches.size(), 1);
    EXPECT_EQ(r.cellMatches[0].nodeId, QStringLiteral("J1"));
}

TEST(MeshNodeMapper, FullRemapDoesNotDoubleCoupleANodeOnItsOwnVertex)
{
    // The toolbar's "Re-map all" clears cell rows but KEEPS vertex
    // couplings. A node sitting on its own coupled vertex must not also
    // get a cell row — the engine would then build two coupling points
    // for one node and exchange twice.
    auto m = makeSquareMesh();
    m.vertices[1].coupledNode = QStringLiteral("J1");   // vertex 1 = (1,0)
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(1.0, 0.0) },
    };
    const auto r = mesh::mapNodesToMesh(m, nodes, -1.0, /*preserveExisting=*/false);
    EXPECT_TRUE(r.cellMatches.isEmpty());
    EXPECT_TRUE(r.vertexMatches.isEmpty());
    ASSERT_EQ(r.skippedExisting.size(), 1);
    EXPECT_EQ(r.skippedExisting.first(), QStringLiteral("J1"));
}

TEST(MeshNodeMapper, VertexHeldByAnotherNodeFallsThroughToCell)
{
    // Same spot, different node id: the vertex is not available, so the
    // node still needs a coupling — its containing cell.
    auto m = makeSquareMesh();
    m.vertices[1].coupledNode = QStringLiteral("OTHER");
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J1"), QPointF(1.0, 0.0) },
    };
    const auto r = mesh::mapNodesToMesh(m, nodes, -1.0, /*preserveExisting=*/false);
    EXPECT_TRUE(r.vertexMatches.isEmpty());
    ASSERT_EQ(r.cellMatches.size(), 1);
    EXPECT_EQ(r.cellMatches[0].nodeId, QStringLiteral("J1"));
}

TEST(MeshNodeMapper, TwoNodesOneVertexFirstWins)
{
    const auto m = makeSquareMesh();
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("A"), QPointF(0.0, 0.0) },   // vertex 0
        { QStringLiteral("B"), QPointF(0.0, 0.0) },   // same spot
    };
    const auto r = mesh::mapNodesToMesh(m, nodes);
    ASSERT_EQ(r.vertexMatches.size(), 1);
    EXPECT_EQ(r.vertexMatches.value(0), QStringLiteral("A"));
    // B falls through to cell containment (corner is edge-inclusive).
    ASSERT_EQ(r.cellMatches.size(), 1);
    EXPECT_EQ(r.cellMatches[0].nodeId, QStringLiteral("B"));
}

TEST(MeshNodeMapper, NodeOnSharedEdgePicksLowestTriangleDeterministically)
{
    const auto m = makeSquareMesh();
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("E"), QPointF(0.5, 0.5) },   // on diagonal 1–2
    };
    const auto r = mesh::mapNodesToMesh(m, nodes);
    ASSERT_EQ(r.cellMatches.size(), 1);
    EXPECT_EQ(r.cellMatches[0].tri, 0);
}

// ── 1D2D fine-scale coupling plan §7.4 / G5 ──────────────────────────────

// Under COUPLING_LAW WEIR_ORIFICE the mapper writes rows that do not author
// CD / AREA, so the node structures govern; the legacy default still does.
TEST(MeshNodeMapper, CellRowsAuthorCdAreaOnlyWhenAsked)
{
    const auto m = makeSquareMesh();
    const QVector<QPair<QString, QPointF>> nodes = {
        { QStringLiteral("J"), QPointF(0.25, 0.25) },
    };
    const auto legacy = mesh::mapNodesToMesh(m, nodes);
    ASSERT_EQ(legacy.cellMatches.size(), 1);
    EXPECT_TRUE(legacy.cellMatches[0].cdSet && legacy.cellMatches[0].areaSet);
    EXPECT_TRUE(legacy.authorCdArea);
    const auto weir = mesh::mapNodesToMesh(m, nodes, -1.0, true, /*authorCdArea=*/false);
    ASSERT_EQ(weir.cellMatches.size(), 1);
    EXPECT_FALSE(weir.cellMatches[0].cdSet);
    EXPECT_FALSE(weir.cellMatches[0].areaSet);
    EXPECT_FALSE(weir.authorCdArea);
}

namespace {
mesh::MeshResult coupledSquare()
{
    auto m = makeSquareMesh();
    m.vertices[1].coupledNode = QStringLiteral("V");   // vertex-coupled
    m.vertices[1].couplingCd = 0.8;
    m.vertices[1].couplingArea = 3.0;
    mesh::CellCoupling c;
    c.tri = 1; c.nodeId = QStringLiteral("C"); c.cd = 0.9; c.area = 5.0;
    m.cellCouplings = { c };
    return m;
}
const QVector<QPair<QString, QPointF>> kNodes = {
    { QStringLiteral("V"), QPointF(1.0, 0.0) },     // on vertex 1
    { QStringLiteral("C"), QPointF(0.8, 0.8) },     // in tri 1
    { QStringLiteral("N"), QPointF(0.2, 0.2) },     // new, in tri 0
    { QStringLiteral("X"), QPointF(5.0, 5.0) },     // outside
};
auto everyNode = [](const QString &) { return true; };
} // namespace

// Clear & rebuild: a node landing where it was keeps its CD / AREA (vertex
// and cell), a new node gets the mapper defaults, an outside node is listed.
TEST(MeshRemapPlan, RebuildKeepsParametersOfNodesThatLandAgain)
{
    const auto m = coupledSquare();
    const auto p = mesh::planRemap(m, kNodes, everyNode, /*clearRebuild=*/true, true);
    EXPECT_TRUE(p.vertices.isEmpty()) << "V re-matches vertex 1 — no vertex change";
    ASSERT_EQ(p.newCells.size(), 2);
    for (const auto &c : p.newCells) {
        if (c.nodeId == QStringLiteral("C")) { EXPECT_EQ(c.tri, 1); EXPECT_DOUBLE_EQ(c.area, 5.0); }
        else { EXPECT_EQ(c.nodeId, QStringLiteral("N")); EXPECT_DOUBLE_EQ(c.area, mesh::kCellCouplingDefaultArea); }
    }
    EXPECT_EQ(p.proposed.value(QStringLiteral("X")), QStringLiteral("outside the mesh"));
    EXPECT_EQ(p.current.value(QStringLiteral("V")), QStringLiteral("vertex 1"));
}

// A node switched to "not coupleable" loses its couplings in either mode.
TEST(MeshRemapPlan, NonCoupleableNodesAreUncoupled)
{
    const auto m = coupledSquare();
    auto notV = [](const QString &n) { return n != QStringLiteral("V"); };
    for (bool rebuild : {true, false}) {
        const auto p = mesh::planRemap(m, kNodes, notV, rebuild, true);
        ASSERT_EQ(p.vertices.size(), 1) << rebuild;
        EXPECT_EQ(p.vertices[0], 1);
        EXPECT_TRUE(p.newAttrs[0].coupledNode.isEmpty());
        EXPECT_EQ(p.oldAttrs[0].coupledNode, QStringLiteral("V"));
        EXPECT_EQ(p.proposed.value(QStringLiteral("V")), QStringLiteral("— (not coupleable)"));
        for (const auto &c : p.newCells) EXPECT_NE(c.nodeId, QStringLiteral("V"));
    }
}

// Preserve existing: coupled nodes keep their rows untouched; only the
// uncoupled node is mapped (unauthored under the weir/orifice law).
TEST(MeshRemapPlan, PreserveMapsOnlyUncoupledNodes)
{
    const auto m = coupledSquare();
    const auto p = mesh::planRemap(m, kNodes, everyNode, /*clearRebuild=*/false, false);
    EXPECT_TRUE(p.vertices.isEmpty());
    ASSERT_EQ(p.newCells.size(), 2);
    EXPECT_EQ(p.newCells[0].nodeId, QStringLiteral("C"));
    EXPECT_DOUBLE_EQ(p.newCells[0].cd, 0.9);
    EXPECT_EQ(p.newCells[1].nodeId, QStringLiteral("N"));
    EXPECT_FALSE(p.newCells[1].cdSet);
    EXPECT_TRUE(p.mapped.skippedExisting.contains(QStringLiteral("V")));
    EXPECT_EQ(p.proposed.value(QStringLiteral("C")), p.current.value(QStringLiteral("C")));
}

// The plan's mapper verdict is exactly mapNodesToMesh on the cleared mesh:
// the dialog's preview and its Apply cannot disagree with the mapper.
TEST(MeshRemapPlan, RebuildPreviewEqualsMapperOutput)
{
    const auto m = coupledSquare();
    const auto p = mesh::planRemap(m, kNodes, everyNode, true, true);
    auto cleared = m;
    cleared.cellCouplings.clear();
    for (auto &v : cleared.vertices) v.coupledNode.clear();
    const auto r = mesh::mapNodesToMesh(cleared, kNodes, -1.0, false, true);
    EXPECT_EQ(p.mapped.vertexMatches, r.vertexMatches);
    EXPECT_EQ(p.mapped.unmatched, r.unmatched);
    ASSERT_EQ(p.mapped.cellMatches.size(), r.cellMatches.size());
    for (int i = 0; i < r.cellMatches.size(); ++i) {
        EXPECT_EQ(p.mapped.cellMatches[i].tri, r.cellMatches[i].tri);
        EXPECT_EQ(p.mapped.cellMatches[i].nodeId, r.cellMatches[i].nodeId);
    }
}
