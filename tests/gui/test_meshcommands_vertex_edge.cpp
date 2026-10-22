/**
 * @file      test_meshcommands_vertex_edge.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Undoable per-vertex / per-edge mesh attribute edits — the commands
 *         behind the mesh-editing toolbar and the Attribute Table's mesh views.
 *
 * The push helpers here take a MapCanvas to find the undo stack; passing null
 * exercises their headless path (the edit still happens, unundoably), so the
 * round-trip slots drive the commands directly against a real SWMM2DMeshLayer.
 *
 * What is guarded:
 *   - redo/undo restore every editable field, including the ones the layer
 *     couples (clearing a coupling resets Cd/Area — undo must bring them back);
 *   - an interior edge's conveyance mirror survives undo on BOTH halves;
 *   - the whole-BC write keeps each slot's own group + conveyance;
 *   - no-ops never enter the undo stack, and BC writes skip interior edges.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmm2dmeshlayer.h"
#include "map/meshcommands.h"
#include "mesh/meshresult.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshcellstats.h"

#include <QTest>

namespace
{

/*! `wide` × 1 strip of unit quads, each split into two triangles.
 *  Vertex for column x, row y is `x * 2 + y`. The shared diagonal of each quad
 *  is an interior edge, which is what the conveyance-mirror slots need. */
/**
 * @brief Build a horizontal unit-height strip with two CCW triangles per square.
 * @param[in] wide Nonnegative fixture square count; produces 2*(wide+1)vertices and 2*wide
 * triangles.
 * @return Owned ok mesh with column/row index 2*x+y, unit planar spacing and default metadata.
 * No validation or model registration; local value construction may allocate.
 */
mesh::MeshResult makeStrip(int wide)
{
    mesh::MeshResult m;
    for (int x = 0; x <= wide; ++x) {
        mesh::MeshVertex b;
        b.xy = QPointF(double(x), 0.0);
        m.vertices.append(b);
        mesh::MeshVertex t;
        t.xy = QPointF(double(x), 1.0);
        m.vertices.append(t);
    }
    for (int x = 0; x < wide; ++x) {
        const int b0 = x * 2, t0 = x * 2 + 1, b1 = (x + 1) * 2, t1 = (x + 1) * 2 + 1;
        mesh::MeshTriangle a;
        a.v0 = b0;
        a.v1 = b1;
        a.v2 = t1;
        m.triangles.append(a);
        mesh::MeshTriangle c;
        c.v0 = b0;
        c.v1 = t1;
        c.v2 = t0;
        m.triangles.append(c);
    }
    m.ok = true;
    return m;
}

/*! First interior slot found on \p layer, plus its neighbour. */
/**
 * @brief Find the first interior triangle edge with a valid neighbor in deterministic scan
 * order.
 * @param[in] layer Stable triangle-only fixture layer, preserved; only local edge 0..2 scanned.
 * @param[out] tri Required nonnull cell-index output, assigned only on success.
 * @param[out] e Required nonnull local-edge output, assigned only on success.
 * @param[out] nbrTri Required nonnull neighboring cell output, assigned only on success.
 * @param[out] nbrE Required nonnull neighboring local-edge output, assigned only on success.
 * @return True when a pair is found, false leaves every output unchanged; borrowed outputs
 * retain ownership.
 */
bool findInteriorPair(const SWMM2DMeshLayer &layer, int *tri, int *e, int *nbrTri, int *nbrE)
{
    for (int t = 0; t < layer.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            if (layer.isBoundaryEdge(t, k))
                continue;
            const QPair<int, int> n = layer.findEdgeNeighbour(t, k);
            if (n.first < 0)
                continue;
            *tri = t;
            *e = k;
            *nbrTri = n.first;
            *nbrE = n.second;
            return true;
        }
    return false;
}

/*! First boundary slot on \p layer. */
/**
 * @brief Find the first exterior edge in a triangle-only layer.
 * @param[in] layer Stable layer, preserved; scans cells then local edges 0..2.
 * @param[out] tri Required nonnull cell index, assigned only when a boundary exists.
 * @param[out] e Required nonnull local edge index, assigned only when found.
 * @return True on match, false leaves outputs untouched; no ownership transfer or mutation.
 */
bool findBoundary(const SWMM2DMeshLayer &layer, int *tri, int *e)
{
    for (int t = 0; t < layer.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k)
            if (layer.isBoundaryEdge(t, k)) {
                *tri = t;
                *e = k;
                return true;
            }
    return false;
}

/*! Two triangles + one quad sharing edges:
 *
 *      3-----2-----5          cells: T0 = (0,1,2)   T1 = (0,2,3)
 *      |    /|     |                 Q2 = (1,4,5,2) (after the triangles)
 *      |   / |  Q2 |
 *      |  /  |     |          interior edges: (0,2) T0/T1, (1,2) T0/Q2
 *      | /   |     |
 *      0-----1-----4                                                    */
mesh::MeshResult makeMixed()
{
    mesh::MeshResult m;
    auto vtx = [&m](double x, double y, double z) {
        mesh::MeshVertex v;
        v.xy = QPointF(x, y);
        v.z = z;
        m.vertices.append(v);
    };
    vtx(0, 0, 0.0);
    vtx(1, 0, 1.0);
    vtx(1, 1, 3.0);
    vtx(0, 1, 2.0);
    vtx(2, 0, 1.5);
    vtx(2, 1, 2.5);
    mesh::MeshTriangle t0;
    t0.v0 = 0;
    t0.v1 = 1;
    t0.v2 = 2;
    mesh::MeshTriangle t1;
    t1.v0 = 0;
    t1.v1 = 2;
    t1.v2 = 3;
    mesh::MeshTriangle q;
    q.v0 = 1;
    q.v1 = 4;
    q.v2 = 5;
    q.v3 = 2;
    m.triangles = {t0, t1, q};
    m.ok = true;
    return m;
}

} // namespace

class TestMeshCommandsVertexEdge : public QObject
{
    Q_OBJECT
private slots:

    // ---- vertex ---------------------------------------------------------

    /**
     * @brief Verify headless vertex elevation edits apply and direct command redo/undo restores
     * exact elevations.
     * @note Uses local strip layer, null canvas means immediate unundoable helper edit 12.5;
     * explicit command then 99 and undo original. No project/native engine IO or shared undo
     * stack.
     */
    void vertex_z_round_trips()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        const double before = layer.mesh().vertices[2].z;

        QCOMPARE(mesh::pushVertexParamEdit(&layer, {2}, "z", 12.5, nullptr), 1);
        QCOMPARE(layer.mesh().vertices[2].z, 12.5);

        // No canvas ⇒ no stack, so drive the command itself for the round trip.
        mesh::MeshVertex oldA, newA;
        oldA.z = before;
        newA.z = 99.0;
        MeshSetVertexAttributeCommand cmd(&layer, "z", {2}, {newA}, {oldA}, QStringLiteral("z"),
                                          nullptr);
        cmd.redo();
        QCOMPARE(layer.mesh().vertices[2].z, 99.0);
        cmd.undo();
        QCOMPARE(layer.mesh().vertices[2].z, before);
    }

    /**
     * @brief Verify identical vertex edits and invalid indices/keys produce no recorded change.
     * @note First inlet tag write returns 1, repeated/out-of-range/unknown-key writes return 0;
     * headless local layer only.
     */
    void vertex_noop_is_dropped()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        QCOMPARE(mesh::pushVertexParamEdit(&layer, {1}, "tag", QStringLiteral("inlet"), nullptr),
                 1);
        // Same value again — nothing to record.
        QCOMPARE(mesh::pushVertexParamEdit(&layer, {1}, "tag", QStringLiteral("inlet"), nullptr),
                 0);
        // Out-of-range and unknown-key writes are refused outright.
        QCOMPARE(mesh::pushVertexParamEdit(&layer, {9999}, "tag", QStringLiteral("x"), nullptr), 0);
        QCOMPARE(mesh::pushVertexParamEdit(&layer, {1}, "nosuchkey", QStringLiteral("x"), nullptr),
                 0);
    }

    /**
     * @brief Verify coupling discharge coefficient edits require an existing node coupling.
     * @note Uncoupled vertex rejects.8 coefficient; assigning J1 then accepts and stores.8, with
     * helper change counts 0/1/1. No simulation or node lookup fixture.
     */
    void vertex_coupling_cd_needs_a_coupled_vertex()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        // Uncoupled: the layer refuses Cd, so the helper must not record it.
        QCOMPARE(mesh::pushVertexParamEdit(&layer, {0}, "couplingCd", 0.8, nullptr), 0);

        QCOMPARE(
            mesh::pushVertexParamEdit(&layer, {0}, "coupledNode", QStringLiteral("J1"), nullptr),
            1);
        QCOMPARE(mesh::pushVertexParamEdit(&layer, {0}, "couplingCd", 0.8, nullptr), 1);
        QCOMPARE(layer.mesh().vertices[0].couplingCd, 0.8);
    }

    /**
     * @brief Verify undo restores coupling node plus discharge coefficient and exchange area as
     * one snapshot.
     * @note Clearing J1 resets coefficient.65/area 1; undo restores J1,.8,3.5 exactly. Guards
     * side effects of partial attribute restoration; local command/layer, no saved file
     * mutation.
     */
    void undoing_a_cleared_coupling_restores_cd_and_area()
    {
        // The layer resets Cd/Area to the engine defaults when a coupling is
        // cleared. A command that snapshotted only `coupledNode` would restore
        // the node id and silently leave the defaults behind — this is the
        // regression that drove the whole-snapshot design.
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        mesh::pushVertexParamEdit(&layer, {0}, "coupledNode", QStringLiteral("J1"), nullptr);
        mesh::pushVertexParamEdit(&layer, {0}, "couplingCd", 0.8, nullptr);
        mesh::pushVertexParamEdit(&layer, {0}, "couplingArea", 3.5, nullptr);

        mesh::MeshVertex oldA;
        oldA.coupledNode = QStringLiteral("J1");
        oldA.couplingCd = 0.8;
        oldA.couplingArea = 3.5;
        mesh::MeshVertex newA = oldA;
        newA.coupledNode = QString();
        newA.couplingCd = 0.65;
        newA.couplingArea = 1.0;

        MeshSetVertexAttributeCommand cmd(&layer, "coupledNode", {0}, {newA}, {oldA},
                                          QStringLiteral("clear"), nullptr);
        cmd.redo();
        QVERIFY(layer.mesh().vertices[0].coupledNode.isEmpty());
        QCOMPARE(layer.mesh().vertices[0].couplingCd, 0.65);

        cmd.undo();
        QCOMPARE(layer.mesh().vertices[0].coupledNode, QStringLiteral("J1"));
        QCOMPARE(layer.mesh().vertices[0].couplingCd, 0.8);
        QCOMPARE(layer.mesh().vertices[0].couplingArea, 3.5);
    }

    // ---- edge -----------------------------------------------------------

    /**
     * @brief Verify an interior conveyance command mirrors both half-edge slots through redo and
     * undo.
     * @note Both start 1, redo.25, undo 1; direct local command avoids dependence on GUI undo
     * ownership.
     */
    void interior_conveyance_mirror_survives_undo()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        int tri = -1, e = -1, nTri = -1, nE = -1;
        QVERIFY(findInteriorPair(layer, &tri, &e, &nTri, &nE));
        const int flat = mesh::edgeSlot(tri, e);
        const int nFlat = mesh::edgeSlot(nTri, nE);

        QCOMPARE(layer.edgeBCs()[flat].conveyance, 1.0);
        QCOMPARE(layer.edgeBCs()[nFlat].conveyance, 1.0);

        mesh::MeshEdgeBC before = layer.edgeBCs()[flat];
        mesh::MeshEdgeBC after = before;
        after.conveyance = 0.25;
        MeshSetEdgeAttributeCommand cmd(&layer, "conveyance", {flat}, {after}, {before},
                                        QStringLiteral("psi"), nullptr);
        cmd.redo();
        QCOMPARE(layer.edgeBCs()[flat].conveyance, 0.25);
        QCOMPARE(layer.edgeBCs()[nFlat].conveyance, 0.25); // mirrored

        cmd.undo();
        QCOMPARE(layer.edgeBCs()[flat].conveyance, 1.0);
        QCOMPARE(layer.edgeBCs()[nFlat].conveyance, 1.0); // mirrored back
    }

    /**
     * @brief Verify interior edges permit conveyance while stage/head edits apply only on
     * boundary edges.
     * @note Interior conveyance.4 counts 1,head 3 counts 0; boundary head 3 counts 1 and
     * persists in slot.
     */
    void conveyance_applies_to_interior_edges_but_bc_fields_do_not()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        int tri = -1, e = -1, nTri = -1, nE = -1;
        QVERIFY(findInteriorPair(layer, &tri, &e, &nTri, &nE));

        QCOMPARE(mesh::pushEdgeParamEdit(&layer, {qMakePair(tri, e)}, "conveyance", 0.4, nullptr),
                 1);
        // A boundary-condition field on an interior edge is skipped.
        QCOMPARE(mesh::pushEdgeParamEdit(&layer, {qMakePair(tri, e)}, "head", 3.0, nullptr), 0);

        int bTri = -1, bE = -1;
        QVERIFY(findBoundary(layer, &bTri, &bE));
        QCOMPARE(mesh::pushEdgeParamEdit(&layer, {qMakePair(bTri, bE)}, "head", 3.0, nullptr), 1);
        QCOMPARE(layer.edgeBCs()[mesh::edgeSlot(bTri, bE)].head, 3.0);
    }

    /**
     * @brief Verify replacing a boundary condition preserves the edge's independent group and
     * conveyance.
     * @note SpecifiedStageConst head 7.25 leaves west group/.5 conveyance unchanged, helper
     * counts one edit.
     */
    void bc_write_preserves_group_and_conveyance()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        int bTri = -1, bE = -1;
        QVERIFY(findBoundary(layer, &bTri, &bE));
        const int flat = mesh::edgeSlot(bTri, bE);

        mesh::pushEdgeParamEdit(&layer, {qMakePair(bTri, bE)}, "group", QStringLiteral("west"),
                                nullptr);
        mesh::pushEdgeParamEdit(&layer, {qMakePair(bTri, bE)}, "conveyance", 0.5, nullptr);

        mesh::MeshEdgeBC bc;
        bc.type = mesh::MeshBCTypes::Type::SpecifiedStageConst;
        bc.head = 7.25;
        QCOMPARE(mesh::pushEdgeBCEdit(&layer, {qMakePair(bTri, bE)}, bc, nullptr), 1);

        const mesh::MeshEdgeBC &slot = layer.edgeBCs()[flat];
        QCOMPARE(slot.type, mesh::MeshBCTypes::Type::SpecifiedStageConst);
        QCOMPARE(slot.head, 7.25);
        QCOMPARE(slot.group, QStringLiteral("west")); // untouched
        QCOMPARE(slot.conveyance, 0.5);               // untouched
    }

    /**
     * @brief Verify undo of a boundary-type change restores the stage value together with its
     * type.
     * @note Stage 4->Wall->undoSpecifiedStageConst/head 4, guarding partial snapshot reset of
     * head.
     */
    void bc_type_change_round_trips_the_whole_slot()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        int bTri = -1, bE = -1;
        QVERIFY(findBoundary(layer, &bTri, &bE));
        const int flat = mesh::edgeSlot(bTri, bE);

        mesh::MeshEdgeBC stage;
        stage.type = mesh::MeshBCTypes::Type::SpecifiedStageConst;
        stage.head = 4.0;
        mesh::pushEdgeBCEdit(&layer, {qMakePair(bTri, bE)}, stage, nullptr);

        const mesh::MeshEdgeBC before = layer.edgeBCs()[flat];
        mesh::MeshEdgeBC after = before;
        after.type = mesh::MeshBCTypes::Type::Wall;
        MeshSetEdgeAttributeCommand cmd(&layer, "bcType", {flat}, {after}, {before},
                                        QStringLiteral("type"), nullptr);
        cmd.redo();
        QCOMPARE(layer.edgeBCs()[flat].type, mesh::MeshBCTypes::Type::Wall);
        cmd.undo();
        // The stage value comes back with the type — a partial restore would
        // have left a Specified-Stage edge holding head 0.
        QCOMPARE(layer.edgeBCs()[flat].type, mesh::MeshBCTypes::Type::SpecifiedStageConst);
        QCOMPARE(layer.edgeBCs()[flat].head, 4.0);
    }

    /**
     * @brief Verify conveyance values outside[0,1] refuse without changing the boundary slot.
     * @note 1.7 and-.1 each return 0 and retain default 1; no undo entries or project IO.
     */
    void out_of_range_conveyance_is_refused()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        int bTri = -1, bE = -1;
        QVERIFY(findBoundary(layer, &bTri, &bE));
        QCOMPARE(mesh::pushEdgeParamEdit(&layer, {qMakePair(bTri, bE)}, "conveyance", 1.7, nullptr),
                 0);
        QCOMPARE(
            mesh::pushEdgeParamEdit(&layer, {qMakePair(bTri, bE)}, "conveyance", -0.1, nullptr), 0);
        QCOMPARE(layer.edgeBCs()[mesh::edgeSlot(bTri, bE)].conveyance, 1.0);
    }

    // ---- mixed triangle / quad mesh (TRI_QUAD_MESHING_PLAN G1) ----------

    /**
     * @brief Verify mixed triangle/quad edge indexing, neighbor mirroring, picking and geometry
     * statistics agree.
     * @note Three cells use 12 stride 4 slots; quad edge 3 valid, triangle edge 3 invalid;
     * shared T0/Q 2 conveyance mirrors. Four render subtriangles map to cell IDs, scene y=-map
     * y, eight unique edges, centroid rectangle selection returns quad 2; areas.5/.5/1,90 degree
     * quad and twist.25 verified.
     */
    void mixed_mesh_edge_slots_endpoints_hit_test_and_stats()
    {
        SWMM2DMeshLayer layer(makeMixed(), QString());
        QCOMPARE(layer.triangleCount(), 3);
        QCOMPARE(layer.quadCount(), 1);

        // Stride-4 slot layout: 3 cells → 12 slots, whether or not a cell
        // uses its fourth slot.
        QCOMPARE(layer.edgeBCs().size(), mesh::edgeSlotCount(3));
        QCOMPARE(layer.edgeBCs().size(), 12);

        // The quad's four edges, edge k = (v[(k+1)%4], v[(k+2)%4]).
        const mesh::MeshTriangle &q = layer.mesh().triangles[2];
        QVERIFY(q.isQuad());
        int a = -1, b = -1;
        mesh::edgeEndpoints(q, 0, a, b);
        QCOMPARE(qMakePair(a, b), qMakePair(4, 5));
        mesh::edgeEndpoints(q, 1, a, b);
        QCOMPARE(qMakePair(a, b), qMakePair(5, 2));
        mesh::edgeEndpoints(q, 2, a, b);
        QCOMPARE(qMakePair(a, b), qMakePair(2, 1));
        mesh::edgeEndpoints(q, 3, a, b);
        QCOMPARE(qMakePair(a, b), qMakePair(1, 4));
        // A triangle's edge 0 is still the edge opposite vertex 0.
        mesh::edgeEndpoints(layer.mesh().triangles[0], 0, a, b);
        QCOMPARE(qMakePair(a, b), qMakePair(1, 2));

        // Boundary flags: the quad's edge 2 = (2,1) is shared with T0 and is
        // interior; its other three edges are on the outline. A triangle's
        // unused slot 3 is never a boundary edge.
        QVERIFY(!layer.isBoundaryEdge(2, 2));
        QVERIFY(layer.isBoundaryEdge(2, 0));
        QVERIFY(layer.isBoundaryEdge(2, 1));
        QVERIFY(layer.isBoundaryEdge(2, 3));
        QVERIFY(!layer.isBoundaryEdge(0, 3));
        // The interior pairing crosses the tri/quad boundary both ways:
        // T0 edge 0 = (1,2) ↔ Q2 edge 2 = (2,1).
        QCOMPARE(layer.findEdgeNeighbour(0, 0), qMakePair(2, 2));
        QCOMPARE(layer.findEdgeNeighbour(2, 2), qMakePair(0, 0));

        // Conveyance mirrors onto the quad's slot through edgeEndpoints.
        QVERIFY(layer.applyMeshEdgeConveyance(0, 0, 0.4));
        QCOMPARE(layer.edgeBCs()[mesh::edgeSlot(2, 2)].conveyance, 0.4);
        // Local edge 3 is valid on the quad, invalid on a triangle.
        QVERIFY(layer.applyMeshEdgeConveyance(2, 3, 0.7));
        QVERIFY(!layer.applyMeshEdgeConveyance(0, 3, 0.7));

        // Fan: one SceneTri per triangle, two for the quad, tagged by cell.
        QCOMPARE(layer.m_sceneTris.size(), 4);
        QCOMPARE(layer.m_cellSceneStart.size(), 4);
        QCOMPARE(layer.m_cellSceneStart[2], 2);
        QCOMPARE(layer.m_cellSceneStart[3], 4);
        QCOMPARE(layer.m_sceneTris[2].cell, 2);
        QCOMPARE(layer.m_sceneTris[3].cell, 2);

        // Hit-test inside the quad (scene y is -map y) returns the CELL index,
        // whichever sub-triangle the point falls in. With z = (1.0, 1.5, 2.5,
        // 3.0) at corners (1,4,5,2) the B&S split is Case 2: sub-triangles
        // (1,4,2) and (4,5,2), diagonal 4–2 (the line x + y = 2) — probe
        // both sides of it.
        QCOMPARE(layer.locateTriangleAt(1.2, -0.5), 2); // x + y < 2 → (1,4,2)
        QCOMPARE(layer.locateTriangleAt(1.8, -0.7), 2); // x + y > 2 → (4,5,2)
        QCOMPARE(layer.pickCellAt(QPointF(1.5, -0.5)), 2);
        QCOMPARE(layer.locateTriangleAt(0.75, -0.25), 0);
        QCOMPARE(layer.locateTriangleAt(0.25, -0.75), 1);
        QCOMPARE(layer.locateTriangleAt(2.5, -0.5), -1);
        // The quad's outline (four edges, no diagonal) — the deduplicated
        // scene edge set: 3 + 3 + 4 − 2 shared = 8.
        QCOMPARE(layer.edgeCount(), 8);
        // Rect pick by centroid: the quad's centroid is (1.5, 0.5).
        const QVector<int> inRect = layer.pickCellsInRect(QRectF(1.1, -0.9, 0.8, 0.8));
        QCOMPARE(inRect, QVector<int>{2});

        // Cell statistics on the mixed mesh.
        const mesh::CellAreaStats as = mesh::computeCellAreaStats(layer.mesh());
        QCOMPARE(as.count, 3);
        QCOMPARE(as.max, 1.0); // the unit-square quad
        QCOMPARE(as.min, 0.5);
        QCOMPARE(mesh::triangleArea(layer.mesh(), 2), 1.0);
        const mesh::QuadStats qs = mesh::computeQuadStats(layer.mesh());
        QCOMPARE(qs.count, 1);
        QVERIFY(qFuzzyCompare(qs.minAngleDeg, 90.0));
        QVERIFY(qFuzzyCompare(qs.maxAngleDeg, 90.0));
        // z = 1.0, 1.5, 2.5, 3.0 → twist |1.0 − 1.5 + 2.5 − 3.0| / 4 = 0.25.
        QVERIFY(qFuzzyCompare(qs.maxNonPlanarity, 0.25));
    }
};

QTEST_MAIN(TestMeshCommandsVertexEdge)
#include "test_meshcommands_vertex_edge.moc"
