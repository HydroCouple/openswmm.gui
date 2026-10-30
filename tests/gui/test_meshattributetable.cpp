/**
 * @file      test_meshattributetable.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     MeshAttributeTableModel — the Attribute Table dock's view over a 2D
 *         mesh layer's vertices, edges and cells.
 *
 * The contracts that matter for the dock:
 *   - row identity, especially the edge de-duplication (an interior edge owns
 *     two slots but exactly one row, and either slot resolves to it);
 *   - x/y and the other derived columns are not editable;
 *   - an edit writes through the layer and lands as a single-row dataChanged
 *     plus one objectEdited, so the other views refresh without looping;
 *   - the BC columns are inert on interior edges, mirroring the toolbar.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmm2dmeshlayer.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshcellstats.h"
#include "mesh/meshobjectref.h"
#include "mesh/meshresult.h"
#include "ui/panels/meshattributetablemodel.h"
#ifdef HAVE_QPROPERTYMODEL
#include "ui/panels/propertiespanel.h"
#include "ui/properties/meshtrianglepropertyadapter.h"
#include "ui/properties/meshvertexpropertyadapter.h"
#include "ui/properties/meshedgepropertyadapter.h"
#include <QAbstractItemModelTester>
#include <QTreeView>
#include <qvariantpropertyItem.h>
#endif

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <limits>

using Kind = MeshAttributeTableModel::Kind;

namespace
{

/*! `wide` × 1 strip of unit quads, each split into two triangles. Every quad
 *  contributes one interior (diagonal) edge, so the de-duplication has
 *  something to collapse. Unique edges = 3·T − interior. */
/**
 * @brief Construct a unit-height strip of split squares with nonuniform elevation fixtures.
 * @param[in] wide Nonnegative square count; unchecked size arithmetic, unit planar spacing.
 * @return Owned ok mesh with 2*(wide+1)vertices,2*widetriangles,bottomz=xandtopz=x+.5, valid CCW
 * connectivity. No layer registration, validation or model IO.
 */
mesh::MeshResult makeStrip(int wide)
{
    mesh::MeshResult m;
    for (int x = 0; x <= wide; ++x) {
        mesh::MeshVertex b;
        b.xy = QPointF(double(x), 0.0);
        b.z = double(x);
        m.vertices.append(b);
        mesh::MeshVertex t;
        t.xy = QPointF(double(x), 1.0);
        t.z = double(x) + 0.5;
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

/*! Column index whose ColumnSpec key is \p key, or -1. */
/**
 * @brief Locate a table column by exact registry key.
 * @param[in] model Stable attribute-table model whose columnSpecs are copied for this search.
 * @param[in] key Required nonnull Latin1 key, case-sensitive, borrowed only during call.
 * @return First matching zero-based column or-1 if absent; no edit or model reset.
 */
int colFor(const MeshAttributeTableModel &model, const char *key)
{
    const auto specs = model.columnSpecs();
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].key == QLatin1String(key))
            return i;
    return -1;
}

} // namespace

class TestMeshAttributeTable : public QObject
{
    Q_OBJECT
private slots:

#ifdef HAVE_QPROPERTYMODEL
    /**
     * @brief Verify optional property adapters expose all mesh fields and support undoable edits
     * and live refresh.
     * @note Requires HAVE_QPROPERTYMODEL; shown panel uses model-tester validation while
     * switching cell/vertex/edge references. Exactly one owned adapter remains after deferred
     * deletion, Manning.045 undoes, elevation 2.5 edits and external 3.5 refreshes,edgeflow 1.25
     * writes; stale source clears rows/adapters. Live GUI/event loop, fixture canvas only, no
     * project save.
     */
    void meshPropertiesRemainVisibleAndEditable()
    {
        SWMM2DMeshLayer layer(makeStrip(1), QString());
        MeshTrianglePropertyAdapter cell(&layer, 0);
        MeshVertexPropertyAdapter vertex(&layer, 0);
        MeshEdgePropertyAdapter edge(&layer, 0, 0);
        MapCanvas canvas;
        canvas.addLayer(&layer, false);
        PropertiesPanel panel;
        panel.resize(600, 600);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        auto *model = tree->model();
        QAbstractItemModelTester tester(model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto findProperty = [&](auto &&self, const QString &name,
                                const QModelIndex &parent) -> QModelIndex {
            for (int r = 0; r < model->rowCount(parent); ++r) {
                const auto index = model->index(r, 0, parent);
                auto *property = dynamic_cast<QVariantPropertyItem *>(
                    static_cast<QPropertyItem *>(index.internalPointer()));
                if (property && QString::fromLatin1(property->metaProperty().name()) == name)
                    return index.siblingAtColumn(1);
                const auto child = self(self, name, index);
                if (child.isValid())
                    return child;
            }
            return {};
        };
        const QList<SWMMObjectRef> refs = {mesh::MeshObjectRef::cell(layer.sourcePath(), 0),
                                           mesh::MeshObjectRef::vertex(layer.sourcePath(), 0),
                                           mesh::MeshObjectRef::edge(layer.sourcePath(), 0, 0),
                                           mesh::MeshObjectRef::cell(layer.sourcePath(), 0)};
        const QList<QObject *> adapters = {&cell, &vertex, &edge, &cell};
        for (int i = 0; i < refs.size(); ++i) {
            // Exercise the same ref-to-panel path as map/table selections,
            // with no 1D model attached to either the panel or the canvas.
            panel.showMeshObject(&canvas, refs[i]);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
            auto *adapter = adapters[i];
            const auto *meta = adapter->metaObject();
            for (int p = meta->propertyOffset(); p < meta->propertyCount(); ++p) {
                const auto property = meta->property(p);
                QVERIFY2(
                    findProperty(findProperty, QString::fromLatin1(property.name()), {}).isValid(),
                    property.name());
            }
            QCOMPARE(panel.findChildren<MeshTrianglePropertyAdapter *>().size() +
                         panel.findChildren<MeshVertexPropertyAdapter *>().size() +
                         panel.findChildren<MeshEdgePropertyAdapter *>().size(),
                     1);
        }
        const auto roughness = findProperty(findProperty, QStringLiteral("mannings"), {});
        QVERIFY(roughness.isValid());
        QVERIFY(model->setData(roughness, 0.045, Qt::EditRole));
        QCOMPARE(cell.mannings(), 0.045);
        QVERIFY(canvas.undoStack()->canUndo());
        canvas.undoStack()->undo();
        QVERIFY(cell.mannings() != 0.045);
        panel.showMeshObject(&canvas, refs[1]);
        const auto elevation = findProperty(findProperty, QStringLiteral("z"), {});
        QVERIFY(model->setData(elevation, 2.5, Qt::EditRole));
        QCOMPARE(vertex.z(), 2.5);
        QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
        QVERIFY(layer.applyMeshVertexZ(0, 3.5));
        QVERIFY(!changed.isEmpty());
        QCOMPARE(elevation.data(Qt::EditRole).toDouble(), 3.5);
        panel.showMeshObject(&canvas, refs[2]);
        const auto flow = findProperty(findProperty, QStringLiteral("bcFlow"), {});
        QVERIFY(model->setData(flow, 1.25, Qt::EditRole));
        QCOMPARE(edge.bcFlow(), 1.25);
        // A stale or unrelated ref clears the previous object's attributes.
        panel.showMeshObject(&canvas, mesh::MeshObjectRef::cell("missing.2dm", 0));
        QCOMPARE(model->rowCount(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(panel.findChildren<MeshEdgePropertyAdapter *>().isEmpty());
    }
#endif

    // ---- shape ----------------------------------------------------------

    /**
     * @brief Verify one vertex row per mesh vertex with coupling/elevation and matching derived
     * X values.
     * @note Local 3 square strip, row 2 readback equals stored xy/z; no edits or file IO.
     */
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
        QCOMPARE(model.data(model.index(2, xCol)).toDouble(), layer.mesh().vertices[2].xy.x());
        QCOMPARE(model.data(model.index(2, colFor(model, "z"))).toDouble(),
                 layer.mesh().vertices[2].z);
    }

    /**
     * @brief Verify cell rows expose every registered parameter with engine defaults and correct
     * derived area.
     * @note Unset Manning equals registry default, unit right-triangle area.5; no saved model
     * required.
     */
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

    /**
     * @brief Verify the cell-area column and mesh statistics use identical geometry, including a
     * deliberate sliver.
     * @note Checks every row against triangleArea, matching extrema and fuzzy-equal mean, with
     * min<max. Mutates only fixture vertices before constructing local layer, no output
     * artifacts.
     */
    void cell_area_column_agrees_with_the_metadata_statistics()
    {
        // Hunting slivers means sorting this column and trusting that the
        // smallest row IS the mesh's minimum cell. Both surfaces read
        // mesh::triangleArea, and this pins them together so a future change to
        // one cannot silently disagree with the other.
        mesh::MeshResult m = makeStrip(4);
        // Squash one quad's diagonal pair into slivers so min != max and the
        // comparison has something to bite on.
        m.vertices[4].xy = QPointF(2.0, 0.999);
        SWMM2DMeshLayer layer(m, QString());

        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Cell);
        const int areaCol = colFor(model, "Area");
        QVERIFY(areaCol >= 0);

        const mesh::CellAreaStats stats = mesh::computeCellAreaStats(layer.mesh());
        QCOMPARE(stats.count, model.rowCount());

        double tableMin = std::numeric_limits<double>::max();
        double tableMax = 0.0, tableSum = 0.0;
        for (int row = 0; row < model.rowCount(); ++row) {
            const double a = model.data(model.index(row, areaCol)).toDouble();
            // Every cell reports the same number the statistic pass measured.
            QCOMPARE(a, mesh::triangleArea(layer.mesh(), row));
            tableMin = std::min(tableMin, a);
            tableMax = std::max(tableMax, a);
            tableSum += a;
        }
        QVERIFY(tableMin < tableMax); // the sliver is distinguishable
        QCOMPARE(tableMin, stats.min);
        QCOMPARE(tableMax, stats.max);
        QVERIFY(qFuzzyCompare(tableSum / model.rowCount(), stats.mean));
    }

    /**
     * @brief Verify both half-edge references of every interior edge resolve to one unique
     * attribute-table row.
     * @note 4 square strip, row count equals layer edgeCount; every valid slot resolves in range
     * and at least one interior pair checked. Does not merge geometry or modify mesh metadata.
     */
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
                const SWMMObjectRef ref = mesh::MeshObjectRef::edge(layer.sourcePath(), t, e);
                const int row = model.rowForRef(ref);
                QVERIFY(row >= 0 && row < model.rowCount());

                const QPair<int, int> n = layer.findEdgeNeighbour(t, e);
                if (n.first < 0)
                    continue;
                const int nRow = model.rowForRef(
                    mesh::MeshObjectRef::edge(layer.sourcePath(), n.first, n.second));
                QCOMPARE(nRow, row);
                ++pairsChecked;
            }
        }
        QVERIFY(pairsChecked > 0);
    }

    /**
     * @brief Verify vertex/edge/cell table row identities round-trip through typed mesh
     * references.
     * @note Every row resolves back to itself for each kind in local 3 square strip.
     */
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

    /**
     * @brief Verify row lookup refuses references with a different source path or element kind.
     * @note Vertex model for a.inp rejects b.inpvertex and a.inpcell as-1 without row mutation.
     */
    void refs_from_another_mesh_do_not_resolve()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QStringLiteral("/models/a.inp"));
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Vertex);
        QCOMPARE(model.rowForRef(mesh::MeshObjectRef::vertex(QStringLiteral("/models/b.inp"), 1)),
                 -1);
        // Right layer, wrong element kind.
        QCOMPARE(model.rowForRef(mesh::MeshObjectRef::cell(QStringLiteral("/models/a.inp"), 1)),
                 -1);
    }

    // ---- editability ----------------------------------------------------

    /**
     * @brief Verify index, coordinates and marker columns are read-only while vertex elevation
     * remains editable.
     * @note Checks Qtflags only on local fixture row 0, no edits or persistence.
     */
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
        QVERIFY(model.flags(model.index(0, colFor(model, "z"))) & Qt::ItemIsEditable);
    }

    /**
     * @brief Verify boundary-condition type is editable only on exterior rows while conveyance
     * is editable everywhere.
     * @note Interior type has invalid data, both boundary/interior cases exist; no layer writes.
     */
    void bc_columns_are_inert_on_interior_edges()
    {
        SWMM2DMeshLayer layer(makeStrip(4), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Edge);

        const int typeCol = colFor(model, "bcType");
        const int psiCol = colFor(model, "conveyance");
        const int boundaryCol = colFor(model, "Boundary");
        QVERIFY(typeCol >= 0 && psiCol >= 0 && boundaryCol >= 0);

        int interior = 0, boundary = 0;
        for (int row = 0; row < model.rowCount(); ++row) {
            const bool isBoundary =
                model.data(model.index(row, boundaryCol)).toString() == tr("Yes");
            // The BC type itself is settable exactly on boundary edges.
            QCOMPARE(bool(model.flags(model.index(row, typeCol)) & Qt::ItemIsEditable), isBoundary);
            // Conveyance is orthogonal to the boundary/interior split.
            QVERIFY(model.flags(model.index(row, psiCol)) & Qt::ItemIsEditable);
            if (!isBoundary) {
                QVERIFY(!model.data(model.index(row, typeCol)).isValid());
                ++interior;
            } else {
                ++boundary;
            }
        }
        QVERIFY(interior > 0);
        QVERIFY(boundary > 0);
    }

    /**
     * @brief Verify changing BC type activates exactly its applicable parameter and hides
     * unrelated parameter values.
     * @note Wall has none, group always available on boundary;
     * NormalFlow/specifiedstage/flow/time-series/ ratingcurve select
     * slope/head/flow/tseries/curve. Optional references remain invalid until set. Edits only
     * local layer/type through table, no engine model or file mutation.
     */
    void only_the_parameter_the_bc_type_reads_is_live()
    {
        // Referential integrity within the row: a Wall carries no parameter, and
        // switching type moves which single cell is editable. Everything else
        // reads "—" so a boundary can't be left holding a value the engine
        // never looks at.
        SWMM2DMeshLayer layer(makeStrip(4), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Edge);

        const int typeCol = colFor(model, "bcType");
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
        const auto missing = [&](const char *key) {
            const int c = colFor(model, key);
            return c >= 0 && !model.data(model.index(row, c)).isValid();
        };

        using T = mesh::MeshBCTypes::Type;
        // Wall (the default): no parameter at all.
        for (const char *k : {"head", "slope", "flow", "tseries", "curve"}) {
            QVERIFY2(!editable(k), k);
            QVERIFY2(missing(k), k);
        }
        // group is orthogonal to the type — always available on a boundary.
        QVERIFY(editable("group"));

        struct {
            T type;
            const char *live;
        } cases[] = {
            {T::NormalFlow, "slope"},         {T::SpecifiedStageConst, "head"},
            {T::SpecifiedStageTS, "tseries"}, {T::SpecifiedFlowConst, "flow"},
            {T::SpecifiedFlowTS, "tseries"},  {T::RatingCurve, "curve"},
        };
        for (const auto &c : cases) {
            QVERIFY(model.setData(model.index(row, typeCol), int(c.type), Qt::EditRole));
            for (const char *k : {"head", "slope", "flow", "tseries", "curve"}) {
                const bool want = (QLatin1String(k) == QLatin1String(c.live));
                QVERIFY2(editable(k) == want,
                         qPrintable(QStringLiteral("type=%1 key=%2").arg(int(c.type)).arg(k)));
                const bool optionalRef = QLatin1String(k) == QLatin1String("tseries") ||
                                         QLatin1String(k) == QLatin1String("curve");
                QVERIFY2(missing(k) == (!want || optionalRef),
                         qPrintable(QStringLiteral("dash type=%1 key=%2").arg(int(c.type)).arg(k)));
            }
        }
    }

    /**
     * @brief Verify unbound-model node reference columns use editable text rather than empty
     * pickers.
     * @note coupledNode editorText/EditRoleQString, setting J 1 updates vertex without candidate
     * resolution; no live native SWMMmodel is attached.
     */
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
        QVERIFY(model.setData(model.index(0, nodeCol), QStringLiteral("J1"), Qt::EditRole));
        QCOMPARE(layer.mesh().vertices[0].coupledNode, QStringLiteral("J1"));
    }

    /**
     * @brief Verify coupling coefficient is unavailable until a node IDis assigned, then exposes
     * default.65.
     * @note Same local row transitions from noneditable/invalid to editable after J 1 edit, no
     * simulation.
     */
    void coupling_columns_wake_up_with_the_coupling()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Vertex);
        const int cdCol = colFor(model, "couplingCd");
        const int nodeCol = colFor(model, "coupledNode");
        QVERIFY(cdCol >= 0 && nodeCol >= 0);

        QVERIFY(!(model.flags(model.index(0, cdCol)) & Qt::ItemIsEditable));
        QVERIFY(!model.data(model.index(0, cdCol)).isValid());

        QVERIFY(model.setData(model.index(0, nodeCol), QStringLiteral("J1"), Qt::EditRole));
        QVERIFY(model.flags(model.index(0, cdCol)) & Qt::ItemIsEditable);
        QCOMPARE(model.data(model.index(0, cdCol)).toDouble(), 0.65);
    }

    // ---- editing --------------------------------------------------------

    /**
     * @brief Verify a table edit updates its layer and emits one row refresh plus one
     * objectEdited notification.
     * @note Vertex 2 z 42, dataChanged endpoints both row 2,editedreference identity exact;
     * repeated no-op returns false and emits no additional user-edit notification. Local
     * synchronous signal spies.
     */
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

    /**
     * @brief Verify editing one interior edge row updates both layer half-edges and the table's
     * unique value.
     * @note Finds an interior row, writes.3, resolves typed reference/neighbor and checks all
     * three values; no file IO or duplicated row write.
     */
    void editing_conveyance_from_the_table_mirrors_the_interior_pair()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        MeshAttributeTableModel model;
        model.setSource(&layer, Kind::Edge);
        const int psiCol = colFor(model, "conveyance");
        const int boundaryCol = colFor(model, "Boundary");

        int interiorRow = -1;
        for (int row = 0; row < model.rowCount(); ++row)
            if (model.data(model.index(row, boundaryCol)).toString() == tr("No")) {
                interiorRow = row;
                break;
            }
        QVERIFY(interiorRow >= 0);

        QVERIFY(model.setData(model.index(interiorRow, psiCol), 0.3, Qt::EditRole));
        // Both halves of the pair carry the value, and the single row reflects
        // it once.
        const SWMMObjectRef ref = model.refForRow(interiorRow);
        QString lk;
        int tri = -1, e = -1;
        QVERIFY(mesh::MeshObjectRef::parseEdge(ref, &lk, &tri, &e));
        const QPair<int, int> n = layer.findEdgeNeighbour(tri, e);
        QVERIFY(n.first >= 0);
        QCOMPARE(layer.edgeBCs()[mesh::edgeSlot(tri, e)].conveyance, 0.3);
        QCOMPARE(layer.edgeBCs()[mesh::edgeSlot(n.first, n.second)].conveyance, 0.3);
        QCOMPARE(model.data(model.index(interiorRow, psiCol)).toDouble(), 0.3);
    }

    /**
     * @brief Verify external layer changes refresh the table without echoing objectEdited user
     * intent.
     * @note Cell 1 tagroad yields exactly one row 1 dataChanged and zero objectEdited
     * notifications, preventing toolbar/table feedback loops; local layer/model lifetimes only.
     */
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
        QCOMPARE(model.data(model.index(1, tagCol)).toString(), QStringLiteral("road"));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(0).toModelIndex().row(), 1);
        // objectEdited fires for USER edits only, so an external write must not
        // echo back out — that is what keeps the two views from looping.
        QCOMPARE(edited.size(), 0);
    }

    // ---- progressive load ------------------------------------------------

    /**
     * @brief Verify edge rows stay empty until background scene geometry establishes unique-edge
     * adjacency.
     * @note Deferred 4 square layer initially incomplete, starts async completion and waits up
     * to 10 seconds for sceneGeometryReady; then row count equals edgeCount. Live event
     * loop/worker completion required.
     */
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

    // ---- mixed triangle / quad mesh -------------------------------------

    /**
     * @brief Verify mixed triangle/quad tables retain cell geometry and all four quad edge
     * identities.
     * @note Two triangles plus unit quad show vertex counts 3/4,quad area
     * 1/centroid(1.5,.5)/extent; eight unique edges,shared T0/Q 2 edge same row,quad edge 3
     * boundary label 2:3 and length 1. Synthetic owned layer, no mesh export or project
     * mutation.
     */
    void mixed_mesh_vertices_column_and_four_edge_rows()
    {
        // Strip of 2 triangles plus a unit-square quad (1,4,5,2) on the right:
        //   3---2---5
        //   | / |   |
        //   0---1---4
        mesh::MeshResult m;
        for (const QPointF p : {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1),
                                QPointF(2, 0), QPointF(2, 1)}) {
            mesh::MeshVertex v;
            v.xy = p;
            m.vertices.append(v);
        }
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
        SWMM2DMeshLayer layer(m, QString());

        MeshAttributeTableModel cells;
        cells.setSource(&layer, Kind::Cell);
        const int nvCol = colFor(cells, "Vertices");
        QVERIFY(nvCol >= 0);
        QCOMPARE(cells.data(cells.index(0, nvCol)).toInt(), 3);
        QCOMPARE(cells.data(cells.index(2, nvCol)).toInt(), 4);
        QCOMPARE(cells.data(cells.index(2, colFor(cells, "Area"))).toDouble(), 1.0);
        QCOMPARE(cells.data(cells.index(2, colFor(cells, "Centroid X"))).toDouble(), 1.5);
        QCOMPARE(cells.data(cells.index(2, colFor(cells, "Centroid Y"))).toDouble(), 0.5);
        bool ok = false;
        const QRectF qr = cells.elementExtent(2, &ok);
        QVERIFY(ok);
        QCOMPARE(qr, QRectF(QPointF(1, 0), QPointF(2, 1)));

        // One row per unique edge: 3 + 3 + 4 − 2 shared = 8; the quad's four
        // edges all resolve, with (1,2) collapsing onto T0's slot.
        MeshAttributeTableModel edges;
        edges.setSource(&layer, Kind::Edge);
        QCOMPARE(edges.rowCount(), 8);
        QCOMPARE(edges.rowCount(), layer.edgeCount());
        const QString path = layer.sourcePath();
        for (int e = 0; e < 4; ++e)
            QVERIFY(edges.rowForRef(mesh::MeshObjectRef::edge(path, 2, e)) >= 0);
        QCOMPARE(edges.rowForRef(mesh::MeshObjectRef::edge(path, 2, 2)),
                 edges.rowForRef(mesh::MeshObjectRef::edge(path, 0, 0)));
        // The quad's edge 3 = (1,4) is a boundary row labelled "2:3".
        const int row23 = edges.rowForRef(mesh::MeshObjectRef::edge(path, 2, 3));
        QCOMPARE(edges.data(edges.index(row23, colFor(edges, "Edge"))).toString(),
                 QStringLiteral("2:3"));
        QCOMPARE(edges.data(edges.index(row23, colFor(edges, "Boundary"))).toString(),
                 QStringLiteral("Yes"));
        QCOMPARE(edges.data(edges.index(row23, colFor(edges, "Length"))).toDouble(), 1.0);
    }
};

QTEST_MAIN(TestMeshAttributeTable)
#include "test_meshattributetable.moc"
