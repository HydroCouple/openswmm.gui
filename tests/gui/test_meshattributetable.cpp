/*!
 * \file   test_meshattributetable.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MeshAttributeTableModel — the Attribute Table dock's view over a 2D
 *         mesh layer's vertices, edges and cells.
 *
 * The contracts that matter for the dock:
 *   - row identity, especially the edge de-duplication (an interior edge owns
 *     two slots but exactly one row, and either slot resolves to it);
 *   - x/y and the other derived columns are not editable;
 *   - an edit writes through the layer and lands as a single-row dataChanged
 *     plus one objectEdited, so the other views refresh without looping;
 *   - the BC columns are inert on interior edges, mirroring the toolbar.
 */
#include "layers/swmm2dmeshlayer.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshobjectref.h"
#include "mesh/meshresult.h"
#include "ui/panels/meshattributetablemodel.h"

#include <QSignalSpy>
#include <QTest>

using Kind = MeshAttributeTableModel::Kind;

namespace {

/*! `wide` × 1 strip of unit quads, each split into two triangles. Every quad
 *  contributes one interior (diagonal) edge, so the de-duplication has
 *  something to collapse. Unique edges = 3·T − interior. */
mesh::MeshResult makeStrip(int wide)
{
    mesh::MeshResult m;
    for (int x = 0; x <= wide; ++x) {
        mesh::MeshVertex b; b.xy = QPointF(double(x), 0.0); b.z = double(x);
        m.vertices.append(b);
        mesh::MeshVertex t; t.xy = QPointF(double(x), 1.0); t.z = double(x) + 0.5;
        m.vertices.append(t);
    }
    for (int x = 0; x < wide; ++x) {
        const int b0 = x * 2, t0 = x * 2 + 1, b1 = (x + 1) * 2, t1 = (x + 1) * 2 + 1;
        mesh::MeshTriangle a; a.v0 = b0; a.v1 = b1; a.v2 = t1; m.triangles.append(a);
        mesh::MeshTriangle c; c.v0 = b0; c.v1 = t1; c.v2 = t0; m.triangles.append(c);
    }
    m.ok = true;
    return m;
}

/*! Column index whose ColumnSpec key is \p key, or -1. */
int colFor(const MeshAttributeTableModel &model, const char *key)
{
    const auto specs = model.columnSpecs();
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].key == QLatin1String(key)) return i;
    return -1;
}

} // namespace

class TestMeshAttributeTable : public QObject
{
    Q_OBJECT
private slots:

    // ---- shape ----------------------------------------------------------

    void vertex_rows_and_columns()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Vertex);

        QCOMPARE(model.rowCount(), layer.vertexCount());
        QVERIFY(colFor(model, "z") >= 0);
        QVERIFY(colFor(model, "coupledNode") >= 0);

        const int xCol = colFor(model, "X");
        QVERIFY(xCol >= 0);
        QCOMPARE(model.data(model.index(2, xCol)).toDouble(),
                 layer.mesh().vertices[2].xy.x());
        QCOMPARE(model.data(model.index(2, colFor(model, "z"))).toDouble(),
                 layer.mesh().vertices[2].z);
    }

    void cell_rows_include_every_registered_parameter()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Cell);

        QCOMPARE(model.rowCount(), layer.triangleCount());
        for (const mesh::CellParamSpec &s : mesh::cellParamSpecs())
            QVERIFY2(colFor(model, s.key.constData()) >= 0, s.key.constData());

        // Unset parameters read back as the registry default, which is what
        // the engine would use.
        const int nCol = colFor(model, "mannings");
        QCOMPARE(model.data(model.index(0, nCol)).toDouble(),
                 mesh::cellParamSpec("mannings")->defaultValue);
        // Area of a unit right triangle.
        QCOMPARE(model.data(model.index(0, colFor(model, "Area"))).toDouble(), 0.5);
    }

    void interior_edges_collapse_to_one_row()
    {
        SWMM2DMeshLayer layer(makeStrip(4), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Edge);

        // 8 triangles ⇒ 24 slots; 4 interior diagonals + 3 interior verticals
        // are shared, so the unique count is 24 − 7 = 17. edgeCount() is the
        // layer's own independently-derived total, so agreeing with it is the
        // real check.
        QCOMPARE(model.rowCount(), layer.edgeCount());

        // Every slot must resolve, and both halves of an interior edge must
        // land on the same row.
        int pairsChecked = 0;
        for (int t = 0; t < layer.triangleCount(); ++t) {
            for (int e = 0; e < 3; ++e) {
                const SWMMObjectRef ref =
                    mesh::MeshObjectRef::edge(layer.sourcePath(), t, e);
                const int row = model.rowForRef(ref);
                QVERIFY(row >= 0 && row < model.rowCount());

                const QPair<int,int> n = layer.findEdgeNeighbour(t, e);
                if (n.first < 0) continue;
                const int nRow = model.rowForRef(
                    mesh::MeshObjectRef::edge(layer.sourcePath(), n.first, n.second));
                QCOMPARE(nRow, row);
                ++pairsChecked;
            }
        }
        QVERIFY(pairsChecked > 0);
    }

    void row_and_ref_round_trip()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        for (Kind k : {Kind::Vertex, Kind::Edge, Kind::Cell}) {
            MeshAttributeTableModel model;
            model.setSource(&layer, k);
            QVERIFY(model.rowCount() > 0);
            for (int row = 0; row < model.rowCount(); ++row)
                QCOMPARE(model.rowForRef(model.refForRow(row)), row);
        }
    }

    void refs_from_another_mesh_do_not_resolve()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QStringLiteral("/models/a.inp"));
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Vertex);
        QCOMPARE(model.rowForRef(
                     mesh::MeshObjectRef::vertex(QStringLiteral("/models/b.inp"), 1)),
                 -1);
        // Right layer, wrong element kind.
        QCOMPARE(model.rowForRef(
                     mesh::MeshObjectRef::cell(QStringLiteral("/models/a.inp"), 1)),
                 -1);
    }

    // ---- editability ----------------------------------------------------

    void derived_columns_are_not_editable()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Vertex);
        for (const char *key : {"Index", "X", "Y", "Marker"}) {
            const int c = colFor(model, key);
            QVERIFY(c >= 0);
            QVERIFY2(!(model.flags(model.index(0, c)) & Qt::ItemIsEditable), key);
        }
        QVERIFY(model.flags(model.index(0, colFor(model, "z")))
                & Qt::ItemIsEditable);
    }

    void bc_columns_are_inert_on_interior_edges()
    {
        SWMM2DMeshLayer layer(makeStrip(4), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Edge);

        const int typeCol     = colFor(model, "bcType");
        const int psiCol      = colFor(model, "conveyance");
        const int boundaryCol = colFor(model, "Boundary");
        QVERIFY(typeCol >= 0 && psiCol >= 0 && boundaryCol >= 0);

        int interior = 0, boundary = 0;
        for (int row = 0; row < model.rowCount(); ++row) {
            const bool isBoundary =
                model.data(model.index(row, boundaryCol)).toString() == tr("Yes");
            // The BC type itself is settable exactly on boundary edges.
            QCOMPARE(bool(model.flags(model.index(row, typeCol))
                          & Qt::ItemIsEditable), isBoundary);
            // Conveyance is orthogonal to the boundary/interior split.
            QVERIFY(model.flags(model.index(row, psiCol)) & Qt::ItemIsEditable);
            if (!isBoundary) {
                QCOMPARE(model.data(model.index(row, typeCol)).toString(),
                         QStringLiteral("—"));
                ++interior;
            } else {
                ++boundary;
            }
        }
        QVERIFY(interior > 0);
        QVERIFY(boundary > 0);
    }

    void only_the_parameter_the_bc_type_reads_is_live()
    {
        // Referential integrity within the row: a Wall carries no parameter, and
        // switching type moves which single cell is editable. Everything else
        // reads "—" so a boundary can't be left holding a value the engine
        // never looks at.
        SWMM2DMeshLayer layer(makeStrip(4), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Edge);

        const int typeCol     = colFor(model, "bcType");
        const int boundaryCol = colFor(model, "Boundary");
        int row = -1;
        for (int r = 0; r < model.rowCount(); ++r)
            if (model.data(model.index(r, boundaryCol)).toString() == tr("Yes")) {
                row = r;
                break;
            }
        QVERIFY(row >= 0);

        const auto editable = [&](const char *key) {
            const int c = colFor(model, key);
            return c >= 0 && (model.flags(model.index(row, c)) & Qt::ItemIsEditable);
        };
        const auto dash = [&](const char *key) {
            const int c = colFor(model, key);
            return c >= 0 && model.data(model.index(row, c)).toString()
                                 == QStringLiteral("—");
        };

        using T = mesh::MeshBCTypes::Type;
        // Wall (the default): no parameter at all.
        for (const char *k : {"head", "slope", "flow", "tseries", "curve"}) {
            QVERIFY2(!editable(k), k);
            QVERIFY2(dash(k), k);
        }
        // group is orthogonal to the type — always available on a boundary.
        QVERIFY(editable("group"));

        struct { T type; const char *live; } cases[] = {
            {T::NormalFlow,          "slope"},
            {T::SpecifiedStageConst, "head"},
            {T::SpecifiedStageTS,    "tseries"},
            {T::SpecifiedFlowConst,  "flow"},
            {T::SpecifiedFlowTS,     "tseries"},
            {T::RatingCurve,         "curve"},
        };
        for (const auto &c : cases) {
            QVERIFY(model.setData(model.index(row, typeCol), int(c.type),
                                  Qt::EditRole));
            for (const char *k : {"head", "slope", "flow", "tseries", "curve"}) {
                const bool want = (QLatin1String(k) == QLatin1String(c.live));
                QVERIFY2(editable(k) == want,
                         qPrintable(QStringLiteral("type=%1 key=%2")
                                        .arg(int(c.type)).arg(k)));
                QVERIFY2(dash(k) == !want,
                         qPrintable(QStringLiteral("dash type=%1 key=%2")
                                        .arg(int(c.type)).arg(k)));
            }
        }
    }

    void reference_columns_need_a_model_to_become_pickers()
    {
        // Without a bound SWMM model there is nothing to pick from, so the
        // reference columns stay plain text rather than offering an empty combo.
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Vertex);

        const auto specs = model.columnSpecs();
        const int nodeCol = colFor(model, "coupledNode");
        QVERIFY(nodeCol >= 0);
        QCOMPARE(specs[nodeCol].editor, openswmmvis::EditorKind::Text);
        // EditRole is the bare id — no DataObjectRef to drive a picker.
        QCOMPARE(model.data(model.index(0, nodeCol), Qt::EditRole).userType(),
                 int(QMetaType::QString));

        // Editing still works; it just isn't constrained to a candidate list.
        QVERIFY(model.setData(model.index(0, nodeCol),
                              QStringLiteral("J1"), Qt::EditRole));
        QCOMPARE(layer.mesh().vertices[0].coupledNode, QStringLiteral("J1"));
    }

    void coupling_columns_wake_up_with_the_coupling()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Vertex);
        const int cdCol   = colFor(model, "couplingCd");
        const int nodeCol = colFor(model, "coupledNode");
        QVERIFY(cdCol >= 0 && nodeCol >= 0);

        QVERIFY(!(model.flags(model.index(0, cdCol)) & Qt::ItemIsEditable));
        QCOMPARE(model.data(model.index(0, cdCol)).toString(),
                 QStringLiteral("—"));

        QVERIFY(model.setData(model.index(0, nodeCol),
                              QStringLiteral("J1"), Qt::EditRole));
        QVERIFY(model.flags(model.index(0, cdCol)) & Qt::ItemIsEditable);
        QCOMPARE(model.data(model.index(0, cdCol)).toDouble(), 0.65);
    }

    // ---- editing --------------------------------------------------------

    void edit_writes_through_and_signals_once()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Vertex);
        const int zCol = colFor(model, "z");

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy edited(&model, &MeshAttributeTableModel::objectEdited);

        QVERIFY(model.setData(model.index(2, zCol), 42.0, Qt::EditRole));
        QCOMPARE(layer.mesh().vertices[2].z, 42.0);

        // One row refreshed — driven by the layer's attributeChanged, not by a
        // model-side reset.
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(0).toModelIndex().row(), 2);
        QCOMPARE(changed.first().at(1).toModelIndex().row(), 2);

        QCOMPARE(edited.size(), 1);
        QCOMPARE(edited.first().at(0).toString(),
                 mesh::MeshObjectRef::vertex(layer.sourcePath(), 2).name);

        // A no-op edit changes nothing and reports nothing.
        QVERIFY(!model.setData(model.index(2, zCol), 42.0, Qt::EditRole));
        QCOMPARE(edited.size(), 1);
    }

    void editing_conveyance_from_the_table_mirrors_the_interior_pair()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Edge);
        const int psiCol      = colFor(model, "conveyance");
        const int boundaryCol = colFor(model, "Boundary");

        int interiorRow = -1;
        for (int row = 0; row < model.rowCount(); ++row)
            if (model.data(model.index(row, boundaryCol)).toString() == tr("No")) {
                interiorRow = row;
                break;
            }
        QVERIFY(interiorRow >= 0);

        QVERIFY(model.setData(model.index(interiorRow, psiCol), 0.3,
                              Qt::EditRole));
        // Both halves of the pair carry the value, and the single row reflects
        // it once.
        const SWMMObjectRef ref = model.refForRow(interiorRow);
        QString lk; int tri = -1, e = -1;
        QVERIFY(mesh::MeshObjectRef::parseEdge(ref, &lk, &tri, &e));
        const QPair<int,int> n = layer.findEdgeNeighbour(tri, e);
        QVERIFY(n.first >= 0);
        QCOMPARE(layer.edgeBCs()[tri * 3 + e].conveyance, 0.3);
        QCOMPARE(layer.edgeBCs()[n.first * 3 + n.second].conveyance, 0.3);
        QCOMPARE(model.data(model.index(interiorRow, psiCol)).toDouble(), 0.3);
    }

    void a_toolbar_edit_refreshes_the_table_row()
    {
        // The other direction of the MVC contract: a write made straight on the
        // layer (as the mesh toolbar does) must surface here.
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Cell);
        const int tagCol = colFor(model, "tag");

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy edited(&model, &MeshAttributeTableModel::objectEdited);

        QVERIFY(layer.applyMeshTriangleTag(1, QStringLiteral("road")));
        QCOMPARE(model.data(model.index(1, tagCol)).toString(),
                 QStringLiteral("road"));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(0).toModelIndex().row(), 1);
        // objectEdited fires for USER edits only, so an external write must not
        // echo back out — that is what keeps the two views from looping.
        QCOMPARE(edited.size(), 0);
    }

    // ---- progressive load ------------------------------------------------

    void edges_wait_for_the_deferred_geometry()
    {
        // Before the background pass lands there is no vertex adjacency, so
        // pairing interior edges is impossible; listing 3T rows would show
        // every interior edge twice. Stay empty, then fill in.
        SWMM2DMeshLayer layer(makeStrip(4), QString(), nullptr,
                              /*deferHeavyGeometry=*/true);
        QVERIFY(!layer.sceneGeometryComplete());

        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Edge);
        QCOMPARE(model.rowCount(), 0);

        QSignalSpy ready(&layer, &SWMM2DMeshLayer::sceneGeometryReady);
        layer.finishSceneGeometryAsync();
        QVERIFY(ready.wait(10000));

        QCOMPARE(model.rowCount(), layer.edgeCount());
    }
};

QTEST_MAIN(TestMeshAttributeTable)
#include "test_meshattributetable.moc"
