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
#include "map/mapcanvas.h"
#include "core/unitsystem.h"
#include "ui/dialogs/groundwaterassigndialog.h"
#include <openswmm/engine/openswmm_gw2d.h>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include "mesh/inpmeshreader.h"
#include "mesh/inpmeshwriter.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/propertiespanel.h"
#include "ui/properties/dataobjectpickereditor.h"
#include <QAbstractItemModelTester>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableView>
#include <QTreeView>
#ifdef HAVE_QPROPERTYMODEL
#include <qpropertymodel.h>
#endif
#include <QTimer>

#include <QSortFilterProxyModel>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>
#include <memory>

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

/*! Four cells, two tags, one Manning's n, four initial depths (Phase 3 fixtures). */
mesh::MeshResult aggregateMesh()
{
    mesh::MeshResult m = makeStrip(2);
    for (int t = 0; t < m.triangles.size(); ++t) {
        m.triangles[t].mannings = 0.03;
        m.triangles[t].initDepth = 0.1 * t;
        m.triangles[t].tag = t < 2 ? QStringLiteral("north") : QStringLiteral("south");
    }
    for (int v = 0; v < m.vertices.size(); ++v)
        m.vertices[v].tag = QStringLiteral("v");
    return m;
}

/*! A mesh on a canvas, Properties, and a table model for labels and the table route. */
struct AggregateRig {
    SWMM2DMeshLayer layer;
    MapCanvas canvas;
    PropertiesPanel panel;
    MeshAttributeTableModel table;

    AggregateRig(const mesh::MeshResult &m, const QString &path) : layer(m, path)
    {
        canvas.addLayer(&layer, false);
        table.setCanvas(&canvas);
    }
    QTreeView *view() { return panel.findChild<QTreeView *>(); }
    QAbstractItemModel *tree() { return view()->model(); }
    void show(Kind kind, const QList<SWMMObjectRef> &refs)
    {
        table.setSource(&layer, kind);
        panel.showMeshSelection(&canvas, refs);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QList<SWMMObjectRef> cells(const QList<int> &ids) const
    {
        QList<SWMMObjectRef> refs;
        for (int id : ids)
            refs << mesh::MeshObjectRef::cell(layer.sourcePath(), id);
        return refs;
    }
    /*! Properties value cell of table column \p key. */
    QModelIndex field(const char *key)
    {
        const auto label = table.headerData(colFor(table, key), Qt::Horizontal);
        auto *model = tree();
        for (int i = 0; i < model->rowCount(); ++i) {
            const auto group = model->index(i, 0);
            for (int j = 0; j < model->rowCount(group); ++j)
                if (model->index(j, 0, group).data() == label)
                    return model->index(j, 1, group);
        }
        return {};
    }
    int fieldCount()
    {
        int fields = 0;
        for (int i = 0; i < tree()->rowCount(); ++i)
            fields += tree()->rowCount(tree()->index(i, 0));
        return fields;
    }
    QPushButton *apply() { return panel.findChild<QPushButton *>("meshAggregateApply"); }
    QPushButton *cancel() { return panel.findChild<QPushButton *>("meshAggregateCancel"); }
    QString summary() { return panel.findChild<QLabel *>("meshAggregateSummary")->text(); }
    /*! Open \p key's editor through the tree's delegate, let \p touch act on it, commit. */
    template <typename Touch> void edit(const char *key, Touch touch)
    {
        const QModelIndex index = field(key);
        auto *delegate = view()->itemDelegateForIndex(index);
        std::unique_ptr<QWidget> editor(delegate->createEditor(view()->viewport(), {}, index));
        QVERIFY(editor);
        delegate->setEditorData(editor.get(), index);
        touch(editor.get());
        delegate->setModelData(editor.get(), tree(), index);
    }
};

/*! Rows (cell, local) of the first \p n boundary edges. */
QVector<QPair<int, int>> boundaryEdges(const SWMM2DMeshLayer &layer, int n)
{
    QVector<QPair<int, int>> out;
    for (int t = 0; t < layer.triangleCount() && out.size() < n; ++t)
        for (int e = 0; e < 3 && out.size() < n; ++e)
            if (layer.isBoundaryEdge(t, e))
                out.append({t, e});
    return out;
}

} // namespace

class TestMeshAttributeTable : public QObject
{
    Q_OBJECT
private slots:

    void meshPropertiesRemainVisibleAndEditable()
    {
        SWMM2DMeshLayer layer(makeStrip(1), QString());
        MapCanvas canvas;
        canvas.addLayer(&layer, false);
        PropertiesPanel panel;
        panel.show();
        MeshAttributeTableModel table;
        table.setCanvas(&canvas);
        auto *grid = panel.findChild<QTreeView *>();
        QVERIFY(grid);
        auto *treeModel = grid->model();
#ifdef HAVE_QPROPERTYMODEL
        auto *proxy = qobject_cast<QAbstractProxyModel *>(treeModel);
        QVERIFY(proxy && qobject_cast<QPropertyModel *>(proxy->sourceModel()));
#endif
        QAbstractItemModelTester tester(treeModel,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto bind = [&](Kind kind, const SWMMObjectRef &ref) {
            table.setSource(&layer, kind);
            panel.showMeshObject(&canvas, ref);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCOMPARE(grid->model(), treeModel);
            QVERIFY(grid->isVisible());
            QVERIFY(!panel.findChild<QTableView *>());
            int fields = 0;
            for (int i = 0; i < treeModel->rowCount(); ++i)
                fields += treeModel->rowCount(treeModel->index(i, 0));
            QCOMPARE(fields, table.columnCount());
        };
        const QString path = layer.sourcePath();
        bind(Kind::Vertex, mesh::MeshObjectRef::vertex(path, 0));
        auto field = [&](const char *key) -> QModelIndex {
            const int col = colFor(table, key);
            const auto label = table.headerData(col, Qt::Horizontal);
            for (int i = 0; i < treeModel->rowCount(); ++i) {
                const auto group = treeModel->index(i, 0);
                for (int j = 0; j < treeModel->rowCount(group); ++j)
                    if (treeModel->index(j, 0, group).data() == label)
                        return treeModel->index(j, 1, group);
            }
            return {};
        };
        QVERIFY(!(field("Marker").flags() & Qt::ItemIsEditable));
        QVERIFY(!(field("couplingArea").flags() & Qt::ItemIsEditable));
        QVERIFY(grid->model()->setData(field("coupledNode"), "N1"));
        QVERIFY(field("couplingArea").flags() & Qt::ItemIsEditable);
        QVERIFY(grid->model()->setData(field("couplingArea"), 3.25));
        const auto area = table.index(0, colFor(table, "couplingArea"));
        QCOMPARE(area.data().toDouble(), 3.25);
        canvas.undoStack()->undo();
        QVERIFY(area.data().toDouble() != 3.25);
        QVERIFY(table.setData(area, 2.75));
        QCOMPARE(field("couplingArea").data().toDouble(), 2.75);

        bind(Kind::Cell, mesh::MeshObjectRef::cell(path, 0));
        auto *infiltration = panel.findChild<QPushButton *>("editMeshInfiltration");
        auto *groundwater = panel.findChild<QPushButton *>("editMeshGroundwater");
        QVERIFY(infiltration && infiltration->isVisible());
        QVERIFY(groundwater && !groundwater->isEnabled());
        QVERIFY(grid->model()->setData(field("mannings"), 0.045));
        QCOMPARE(table.index(0, colFor(table, "mannings")).data().toDouble(), 0.045);
        QVERIFY(field("corners").data().toString().contains(','));
        bind(Kind::Edge, mesh::MeshObjectRef::edge(path, 0, 0));
        grid->openPersistentEditor(field("bcType"));
        auto *enumEditor = qobject_cast<QComboBox *>(grid->indexWidget(field("bcType")));
        QVERIFY(enumEditor);
        QCOMPARE(enumEditor->count(), 7);
        grid->closePersistentEditor(field("bcType"));
        QVERIFY(!(field("flow").flags() & Qt::ItemIsEditable));
        auto *open = panel.findChild<QPushButton *>("editMeshBoundary");
        QVERIFY(open && open->isVisible());
        open->click();
        auto *dialog = panel.findChild<QDialog *>("meshBoundaryDialog");
        QVERIFY(dialog && dialog->isVisible());
        auto *type = dialog->findChild<QComboBox *>("boundaryType");
        type->setCurrentIndex(type->findData(int(mesh::MeshBCTypes::Type::SpecifiedFlowConst)));
        dialog->findChild<QLineEdit *>("boundaryValue")->setText("1.25");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(field("flow").data().toDouble(), 1.25);
        QVERIFY(field("flow").flags() & Qt::ItemIsEditable);
        canvas.undoStack()->undo();
        QVERIFY(!(field("flow").flags() & Qt::ItemIsEditable));
        canvas.undoStack()->redo();

        const QString dir = QCoreApplication::applicationDirPath() + "/mesh_editor_roundtrip";
        QVERIFY(QDir().mkpath(dir));
        mesh::CouplingMap coupling;
        coupling.vertexToNode.insert(0, "N1");
        for (bool external : {false, true}) {
            const QString inp = dir + (external ? "/external.inp" : "/inline.inp");
            QFile deck(inp);
            QVERIFY(deck.open(QIODevice::WriteOnly | QIODevice::Truncate));
            deck.write("[TITLE]\nMesh editor parity\n");
            deck.close();
            QString error;
            const bool saved =
                external
                    ? mesh::InpMeshWriter::writeExternal(inp, dir + "/external.2dm", layer.mesh(),
                                                         coupling, layer.edgeBCs(), 0.035, &error)
                    : mesh::InpMeshWriter::writeInline(inp, layer.mesh(), coupling, layer.edgeBCs(),
                                                       0.035, &error);
            QVERIFY2(saved, qPrintable(error));
            const auto read = mesh::InpMeshReader::read(inp);
            QVERIFY2(read.hasMesh, qPrintable(read.errorMsg));
            QCOMPARE(read.edgeBCs[mesh::edgeSlot(0, 0)].flow, 1.25);
            QCOMPARE(read.mesh.vertices[0].coupledNode, QString("N1"));
            QCOMPARE(read.mesh.vertices[0].couplingArea, 2.75);
        }
        panel.showMeshObject(&canvas, mesh::MeshObjectRef::cell("missing.2dm", 0));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(treeModel->rowCount(), 0);
    }

    void clickMeshValues_data()
    {
        QTest::addColumn<bool>("properties");
        QTest::newRow("properties") << true;
        QTest::newRow("attribute-table") << false;
    }

    void clickMeshValues()
    {
        QFETCH(bool, properties);
        SWMM2DMeshLayer layer(makeStrip(1), "click.2dm");
        MapCanvas canvas;
        canvas.addLayer(&layer, false);
        SelectionManager selection;
        AttributeTablePanel table;
        table.setProject(nullptr, &selection, &canvas);
        table.showMeshTable(&layer, 1);
        PropertiesPanel grid;
        const auto edge = mesh::MeshObjectRef::edge(layer.sourcePath(), 0, 0);
        grid.showMeshObject(&canvas, edge);
        auto *source = table.findChild<MeshAttributeTableModel *>();
        const int row = source->rowForRef(edge);
        QVERIFY(row >= 0);
        auto *tableView = table.findChild<QTableView *>();
        auto *treeView = grid.findChild<QTreeView *>();
        QAbstractItemView *view =
            properties ? static_cast<QAbstractItemView *>(treeView) : tableView;
        QWidget *panel = properties ? static_cast<QWidget *>(&grid) : &table;
        panel->resize(700, 460);
        panel->show();
        panel->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(panel));

        const auto field = [&](const char *key) -> QModelIndex {
            const int column = colFor(*source, key);
            if (!properties)
                return qobject_cast<QAbstractProxyModel *>(view->model())
                    ->mapFromSource(source->index(row, column));
            const auto label = source->headerData(column, Qt::Horizontal);
            for (int i = 0; i < view->model()->rowCount(); ++i) {
                const auto parent = view->model()->index(i, 0);
                for (int j = 0; j < view->model()->rowCount(parent); ++j)
                    if (view->model()->index(j, 0, parent).data() == label)
                        return view->model()->index(j, 1, parent);
            }
            return {};
        };
        const auto click = [view](const QModelIndex &idx) {
            QVERIFY(idx.flags().testFlag(Qt::ItemIsEditable));
            view->scrollTo(idx);
            const QPoint point = view->visualRect(idx).center();
            QCOMPARE(view->indexAt(point), idx);
            QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, point);
        };
        QVERIFY(layer.applyMeshEdgeConveyance(0, 0, 0.123456789));
        click(field("conveyance"));
        const auto type = field("bcType");
        click(type);
        QCOMPARE(layer.edgeBCs()[mesh::edgeSlot(0, 0)].conveyance, 0.123456789);
        QCOMPARE(canvas.undoStack()->count(), 0);
        auto *combo = qobject_cast<QComboBox *>(view->indexWidget(type));
        QVERIFY2(combo, "Clicking BC Type did not open a combo box");
        QTRY_VERIFY_WITH_TIMEOUT(combo->view()->isVisible(), 1000);
        const int choice = combo->findData(int(mesh::MeshBCTypes::Type::SpecifiedFlowConst));
        const auto item = combo->model()->index(choice, 0);
        combo->view()->scrollTo(item);
        QTest::mouseClick(combo->view()->viewport(), Qt::LeftButton, {},
                          combo->view()->visualRect(item).center());
        QTRY_COMPARE_WITH_TIMEOUT(layer.edgeBCs()[mesh::edgeSlot(0, 0)].type,
                                  mesh::MeshBCTypes::Type::SpecifiedFlowConst, 1000);

        const auto flow = field("flow");
        click(flow);
        auto *spin = qobject_cast<QDoubleSpinBox *>(view->indexWidget(flow));
        QVERIFY(spin);
        spin->selectAll();
        QTest::keyClicks(spin, "2.75");
        QTest::keyClick(spin, Qt::Key_Return);
        QTRY_COMPARE(layer.edgeBCs()[mesh::edgeSlot(0, 0)].flow, 2.75);
        const auto group = field("group");
        click(group);
        auto *text = qobject_cast<QLineEdit *>(view->indexWidget(group));
        // The offscreen plugin can leave the entire application inactive after
        // closing a popup. When active, typing must go directly to this editor.
        if (QApplication::activeWindow() == panel)
            QCOMPARE(QApplication::focusWidget(), text);
        QVERIFY2(text, "Clicking the group value must focus its text editor");
        QTest::keyClicks(text, "Coast");
        QTest::keyClick(text, Qt::Key_Return);
        QTRY_COMPARE(layer.edgeBCs()[mesh::edgeSlot(0, 0)].group, QStringLiteral("Coast"));

        QCOMPARE(canvas.undoStack()->count(), 3);
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        QCOMPARE(type.data(Qt::EditRole).toInt(), int(mesh::MeshBCTypes::Type::Wall));
    }

    void boundaryEditorFromTableTargetsClickedRow()
    {
        SWMM2DMeshLayer layer(makeStrip(2), "mesh.2dm");
        MapCanvas canvas;
        canvas.addLayer(&layer, false);
        SelectionManager selection;
        AttributeTablePanel panel;
        panel.setProject(nullptr, &selection, &canvas);
        panel.showMeshTable(&layer, 1);
        panel.resize(950, 650);
        panel.show();
        auto *model = panel.findChild<MeshAttributeTableModel *>();
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(model && view);
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
        QVERIFY(proxy && proxy->sourceModel() == model);
        view->sortByColumn(0, Qt::DescendingOrder);
        const int sourceRow = model->rowForRef(mesh::MeshObjectRef::edge(layer.sourcePath(), 0, 2));
        const auto idx = proxy->mapFromSource(model->index(sourceRow, 0));
        QVERIFY(idx.isValid());
        view->scrollTo(idx);
        const auto pos = view->visualRect(idx).center();
        QTimer::singleShot(0, &panel, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            for (auto *action : menu->actions()) {
                if (action->text().startsWith("Edit surface boundary")) {
                    const bool enabled = action->isEnabled();
                    if (!enabled) menu->close();
                    QVERIFY(enabled);
                    action->trigger();
                    menu->close();
                    return;
                }
            }
            menu->close();
            QFAIL("Missing surface boundary action");
        });
        QVERIFY(QMetaObject::invokeMethod(&panel, "onContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, pos)));
        auto *dialog = panel.findChild<QDialog *>("meshBoundaryDialog");
        QVERIFY(dialog && dialog->isVisible());
        auto *type = dialog->findChild<QComboBox *>("boundaryType");
        type->setCurrentIndex(type->findData(int(mesh::MeshBCTypes::Type::SpecifiedStageConst)));
        dialog->findChild<QLineEdit *>("boundaryValue")->setText("4.75");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(layer.edgeBCs()[mesh::edgeSlot(0, 2)].head, 4.75);
        QCOMPARE(canvas.undoStack()->count(), 1);
    }

    void wholeBoundaryRecordTypes_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("input");
        using T = mesh::MeshBCTypes::Type;
        QTest::newRow("wall") << int(T::Wall) << QString("wallDrag") << QString("0.25");
        QTest::newRow("normal") << int(T::NormalFlow) << QString("slope") << QString("0.004");
        // A negative slope feeds the domain (engine: q = sign(S)·…); the
        // removed toolbar page allowed it, so every route must keep it.
        QTest::newRow("normal-inflow")
            << int(T::NormalFlow) << QString("slope") << QString("-0.004");
        QTest::newRow("stage") << int(T::SpecifiedStageConst) << QString("head") << QString("1.25");
        QTest::newRow("stage-series")
            << int(T::SpecifiedStageTS) << QString("tseries") << QString("Tide");
        QTest::newRow("flow") << int(T::SpecifiedFlowConst) << QString("flow") << QString("-0.5");
        QTest::newRow("flow-series")
            << int(T::SpecifiedFlowTS) << QString("tseries") << QString("Inflow");
        QTest::newRow("curve") << int(T::RatingCurve) << QString("curve") << QString("Outlet");
    }

    void wholeBoundaryRecordTypes()
    {
        QFETCH(int, type);
        QFETCH(QString, key);
        QFETCH(QString, input);
        SWMM2DMeshLayer layer(makeStrip(1), "bounds.2dm");
        MapCanvas canvas;
        MeshAttributeTableModel edges;
        edges.setCanvas(&canvas);
        edges.setSource(&layer, Kind::Edge);
        QList<int> rows;
        for (int r = 0; r < edges.rowCount(); ++r)
            rows << r;
        QVERIFY(edges.setData(edges.index(0, colFor(edges, "group")), "Coast"));
        QVERIFY(edges.setData(edges.index(0, colFor(edges, "conveyance")), .75));
        const auto before = layer.edgeBCs();
        QWidget host;
        const auto open = [&] {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            edges.editBoundaryCondition(rows, &host);
            return host.findChild<QDialog *>("meshBoundaryDialog");
        };
        auto *dialog = open();
        QVERIFY(dialog);
        dialog->reject();
        QCOMPARE(layer.edgeBCs(), before);
        dialog = open();
        QVERIFY(dialog);
        auto *combo = dialog->findChild<QComboBox *>("boundaryType");
        combo->setCurrentIndex(combo->findData(type));
        dialog->findChild<QLineEdit *>("boundaryValue")->setText(input);
        const int count = canvas.undoStack()->count();
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(canvas.undoStack()->count(), count + 1);
        for (int r : rows) {
            const auto bcType = edges.index(r, colFor(edges, "bcType"));
            if (!(bcType.flags() & Qt::ItemIsEditable))
                continue;
            QCOMPARE(bcType.data(Qt::EditRole).toInt(), type);
            QCOMPARE(edges.index(r, colFor(edges, key.toLatin1().constData())).data().toString(),
                     input);
        }
        QCOMPARE(layer.edgeBCs()[0].group, QString("Coast"));
        QCOMPARE(layer.edgeBCs()[0].conveyance, .75);
        // Reopening shows the record just written: type and value hydrate.
        dialog = open();
        QVERIFY(dialog);
        combo = dialog->findChild<QComboBox *>("boundaryType");
        QCOMPARE(combo->currentData().toInt(), type);
        QCOMPARE(dialog->findChild<QLineEdit *>("boundaryValue")->text(), input);
        dialog->reject();
        canvas.undoStack()->undo();
        QCOMPARE(layer.edgeBCs(), before);
    }

    /** @brief With a model open, the editor's reference row is a picker named for its field
     *  ("Time Series", "Rating Curve"), the name the no-model text row also carries. */
    void boundaryPickerRowsNameTheirField()
    {
        const QString dir = QCoreApplication::applicationDirPath() + "/mesh_editor_roundtrip";
        QVERIFY(QDir().mkpath(dir));
        const QString path = dir + "/boundary_picker.inp";
        QFile::remove(path);
        QVERIFY(QFile::copy(QFINDTESTDATA("data/mini_2d.inp"), path));
        SWMMModelLayer model(path);
        QList<QString> warnings, errors;
        QVERIFY2(model.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        SWMM2DMeshLayer layer(makeStrip(1), "picker.2dm");
        MeshAttributeTableModel edges;
        edges.setModelLayer(&model);
        edges.setSource(&layer, Kind::Edge);
        QList<int> rows;
        for (int r = 0; r < edges.rowCount(); ++r)
            rows << r;
        QWidget host;
        edges.editBoundaryCondition(rows, &host);
        auto *dialog = host.findChild<QDialog *>("meshBoundaryDialog");
        QVERIFY(dialog);
        auto *combo = dialog->findChild<QComboBox *>("boundaryType");
        auto *picker = dialog->findChild<DataObjectPickerEditor *>();
        auto *value = dialog->findChild<QLineEdit *>("boundaryValue");
        auto *form = dialog->findChild<QFormLayout *>();
        QVERIFY(combo && picker && value && form);
        using T = mesh::MeshBCTypes::Type;
        const struct {
            T type;
            const char *key;
        } cases[] = {{T::SpecifiedStageTS, "tseries"},
                     {T::SpecifiedFlowTS, "tseries"},
                     {T::RatingCurve, "curve"}};
        for (const auto &c : cases) {
            combo->setCurrentIndex(combo->findData(int(c.type)));
            QVERIFY(picker->isVisibleTo(dialog));
            QVERIFY(!value->isVisibleTo(dialog));
            auto *label = qobject_cast<QLabel *>(form->labelForField(picker));
            QVERIFY(label);
            QCOMPARE(label->text(),
                     edges.headerData(colFor(edges, c.key), Qt::Horizontal).toString());
        }
        dialog->reject();
    }

    void cellEditorsUseInvokingCells()
    {
        const QString dir = QCoreApplication::applicationDirPath() + "/mesh_editor_roundtrip";
        QVERIFY(QDir().mkpath(dir));
        QFile input(QFINDTESTDATA("data/mini_2d.inp"));
        QVERIFY(input.open(QIODevice::ReadOnly));
        const QString path = dir + "/cell_editors.inp";
        QFile fixture(path);
        QVERIFY(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate));
        fixture.write(input.readAll());
        fixture.close();
        SWMMModelLayer model(path);
        QList<QString> warnings, errors;
        QVERIFY2(model.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        const auto parsed = mesh::InpMeshReader::read(path);
        QVERIFY(parsed.hasMesh);
        SWMM2DMeshLayer meshLayer(parsed.mesh, path);
        MapCanvas canvas;
        canvas.addLayer(&meshLayer, false);
        // The map remains on cell 0 while both forms target cell 1.
        SelectionManager selection;
        selection.select(mesh::MeshObjectRef::cell(path, 0));
        PropertiesPanel properties;
        properties.setProject(&model);
        properties.show();
        meshLayer.setActiveMesh(true);
        properties.showMeshObject(&canvas, mesh::MeshObjectRef::cell(path, 1));
        auto *groundwater = properties.findChild<QPushButton *>("editMeshGroundwater");
        QVERIFY(groundwater && groundwater->isEnabled());
        QSignalSpy request(&properties, &PropertiesPanel::groundwaterCellsRequested);
        groundwater->click();
        QCOMPARE(request.count(), 1);
        QCOMPARE(request.first().at(0).value<SWMM2DMeshLayer *>(), &meshLayer);
        const auto targets = request.first().at(1).value<QList<int>>();
        QCOMPARE(targets, QList<int>{1});
        auto *edit = properties.findChild<QPushButton *>("editMeshInfiltration");
        QVERIFY(edit && edit->isEnabled());
        edit->click();
        auto *infil = properties.findChild<QDialog *>("infilAssignToSelectionDialog");
        QVERIFY(infil);
        auto *method = infil->findChild<QComboBox *>("infiltrationMethod");
        QVERIFY(method);
        method->setCurrentIndex(method->findData(int(mesh::InfilMethod::Constant)));
        QVERIFY(QMetaObject::invokeMethod(infil, "onApply", Qt::DirectConnection));
        QVERIFY(mesh::resolveInfil(meshLayer.mesh(), 1).isOverride());
        QVERIFY(!mesh::resolveInfil(meshLayer.mesh(), 0).isOverride());
        canvas.undoStack()->undo();
        QVERIFY(!mesh::resolveInfil(meshLayer.mesh(), 1).isOverride());
        infil->reject();

        QCOMPARE(swmm_gw2d_row_add(model.engine(), 0, nullptr, -1, .125, 5, .45, .1, 2), SWMM_OK);
        UnitSystem units;
        openswmmvis::ui::GroundwaterAssignDialog gw(&model, &meshLayer, &canvas, &selection,
                                                    &units);
        gw.selectTarget("KS");
        gw.setInitialCells(targets);
        gw.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(2.0);
        gw.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        auto *apply = gw.findChild<QPushButton *>("gwAssignmentApplyButton");
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 10000);
        auto *preview = gw.findChild<QTableWidget *>("gwAssignmentPreview");
        QCOMPARE(preview->rowCount(), 1);
        QCOMPARE(preview->item(0, 0)->text(), QString("2"));
        apply->click();
        int count = 0;
        QCOMPARE(swmm_gw2d_row_count(model.engine(), &count), SWMM_OK);
        QCOMPARE(count, 2);
        canvas.undoStack()->undo();
        QCOMPARE(swmm_gw2d_row_count(model.engine(), &count), SWMM_OK);
        QCOMPARE(count, 1);
    }

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

    /** @brief The read-only Segment column names each boundary edge's source segment, and
     *  follows the table to another mesh even when both share a geometry revision. */
    void segment_tags_follow_the_source()
    {
        auto tagged = [](const QString &tag) {
            mesh::MeshResult m = makeStrip(1);
            mesh::MeshEdge bottom;
            bottom.v0 = 0; // (0,0)–(1,0)
            bottom.v1 = 2;
            bottom.tag = tag;
            m.boundaryEdges.append(bottom);
            return m;
        };
        SWMM2DMeshLayer a(tagged(QStringLiteral("north_A")), QStringLiteral("a.2dm"));
        SWMM2DMeshLayer b(tagged(QStringLiteral("south_B")), QStringLiteral("b.2dm"));
        QCOMPARE(a.geomRevision(), b.geomRevision()); // the case a revision-keyed cache misses

        MeshAttributeTableModel model;
        const auto tags = [&model] {
            const int col = colFor(model, "segment");
            QStringList out;
            for (int r = 0; r < model.rowCount(); ++r) {
                const QString tag = model.index(r, col).data().toString();
                if (!tag.isEmpty())
                    out << tag;
            }
            return out;
        };
        model.setSource(&a, Kind::Edge);
        QVERIFY(colFor(model, "segment") >= 0);
        QCOMPARE(tags(), QStringList{QStringLiteral("north_A")});
        model.setSource(&b, Kind::Edge);
        QCOMPARE(tags(), QStringList{QStringLiteral("south_B")});
        const int col = colFor(model, "segment");
        QVERIFY(!(model.flags(model.index(0, col)) & Qt::ItemIsEditable));
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
        MapCanvas canvas;
        canvas.addLayer(&layer, false);
        PropertiesPanel properties;
        properties.showMeshObject(&canvas, mesh::MeshObjectRef::edge(path, 2, 3));
        QCOMPARE(properties.windowTitle(), QStringLiteral("2D Edge 2:3"));
        const int row23 = edges.rowForRef(mesh::MeshObjectRef::edge(path, 2, 3));
        QCOMPARE(edges.data(edges.index(row23, colFor(edges, "Edge"))).toString(),
                 QStringLiteral("2:3"));
        QCOMPARE(edges.data(edges.index(row23, colFor(edges, "Boundary"))).toString(),
                 QStringLiteral("Yes"));
        QCOMPARE(edges.data(edges.index(row23, colFor(edges, "Length"))).toDouble(), 1.0);
    }

    // ---- Phase 3: aggregate editing (plan §7.1, §9) -----------------------

    /** @brief Several cells show one tree: common values as values, differing ones as Mixed,
     *  every field of the single-element tree, nothing staged. */
    void aggregateShowsCommonAndMixed()
    {
        AggregateRig rig(aggregateMesh(), "agg.2dm");
        QAbstractItemModelTester tester(rig.tree(),
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        rig.show(Kind::Cell, rig.cells({0, 1, 2}));
        QCOMPARE(rig.panel.windowTitle(), QStringLiteral("2D mesh: 3 cells"));
        QCOMPARE(rig.fieldCount(), rig.table.columnCount());
        QCOMPARE(rig.field("mannings").data().toDouble(), 0.03);
        QCOMPARE(rig.field("initDepth").data().toString(), QStringLiteral("Mixed"));
        QCOMPARE(rig.field("tag").data().toString(), QStringLiteral("Mixed"));
        QVERIFY(rig.field("initDepth").data(Qt::FontRole).value<QFont>().italic());
        QVERIFY(rig.field("initDepth").flags() & Qt::ItemIsEditable);
        QVERIFY(rig.field("initDepth").data(Qt::ToolTipRole).toString().contains("different"));
        QVERIFY(!(rig.field("Area").flags() & Qt::ItemIsEditable));
        QVERIFY(rig.apply()->isVisible() || !rig.panel.isVisible());
        QVERIFY(!rig.apply()->isEnabled());
        QVERIFY(rig.summary().contains(QStringLiteral("3 cells · no changes")));
    }

    /** @brief Gate: a multi-field edit writes only the staged fields, to every target, as one
     *  undo step; Mixed fields keep each element's own value; nothing is written before Apply. */
    void aggregateMultiFieldEditLeavesUntouchedFieldsAlone()
    {
        AggregateRig rig(aggregateMesh(), "agg.2dm");
        rig.show(Kind::Cell, rig.cells({0, 1, 2}));
        const int undoBefore = rig.canvas.undoStack()->count();
        QVERIFY(rig.tree()->setData(rig.field("mannings"), 0.05));
        QVERIFY(rig.tree()->setData(rig.field("tag"), QStringLiteral("pond")));
        QCOMPARE(rig.field("mannings").data().toDouble(), 0.05);
        QVERIFY(rig.field("mannings").data(Qt::FontRole).value<QFont>().bold());
        QCOMPARE(rig.layer.mesh().triangles[0].mannings, 0.03); // staged, not written
        QVERIFY(rig.summary().contains(QStringLiteral("2 changes to apply")));
        QVERIFY(rig.apply()->isEnabled());
        rig.apply()->click();

        const auto &tris = rig.layer.mesh().triangles;
        for (int t = 0; t < 3; ++t) {
            QCOMPARE(tris[t].mannings, 0.05);
            QCOMPARE(tris[t].tag, QStringLiteral("pond"));
            QCOMPARE(tris[t].initDepth, 0.1 * t); // Mixed and untouched
        }
        QCOMPARE(tris[3].mannings, 0.03); // not selected
        QCOMPARE(tris[3].tag, QStringLiteral("south"));
        QCOMPARE(rig.canvas.undoStack()->count(), undoBefore + 1);
        QVERIFY(!rig.field("mannings").data(Qt::FontRole).value<QFont>().bold());
        QCOMPARE(rig.field("tag").data().toString(), QStringLiteral("pond"));
        QCOMPARE(rig.field("initDepth").data().toString(), QStringLiteral("Mixed"));
        QVERIFY(!rig.apply()->isEnabled());

        rig.canvas.undoStack()->undo();
        for (int t = 0; t < 3; ++t) {
            QCOMPARE(tris[t].mannings, 0.03);
            QCOMPARE(tris[t].tag, t < 2 ? QStringLiteral("north") : QStringLiteral("south"));
        }
        QCOMPARE(rig.field("tag").data().toString(), QStringLiteral("Mixed"));
    }

    /** @brief Opening and leaving a Mixed field's editor stages nothing (numeric, text, enum,
     *  picker); a deliberate entry stages, including the value an untouched editor would hold. */
    void openingMixedEditorsStagesNothing()
    {
        // Cells: numeric and text.
        AggregateRig rig(aggregateMesh(), "agg.2dm");
        rig.show(Kind::Cell, rig.cells({0, 1, 2}));
        rig.edit("initDepth", [](QWidget *) {});
        rig.edit("tag", [](QWidget *) {});
        QCOMPARE(rig.field("initDepth").data().toString(), QStringLiteral("Mixed"));
        QCOMPARE(rig.field("tag").data().toString(), QStringLiteral("Mixed"));
        QVERIFY(!rig.apply()->isEnabled());
        // Typing 0 into the blank numeric editor is a real choice (0 starts a cell dry).
        rig.edit("initDepth", [](QWidget *editor) {
            auto *line = editor->findChild<QLineEdit *>();
            QVERIFY(line);
            QCOMPARE(line->placeholderText(), QStringLiteral("Mixed"));
            QTest::keyClicks(line, "0");
        });
        rig.edit("tag", [](QWidget *editor) {
            QTest::keyClicks(qobject_cast<QLineEdit *>(editor), "pond");
        });
        QCOMPARE(rig.field("initDepth").data().toDouble(), 0.0);
        QCOMPARE(rig.field("tag").data().toString(), QStringLiteral("pond"));
        QVERIFY(rig.summary().contains(QStringLiteral("2 changes")));
        rig.cancel()->click();
        QCOMPARE(rig.field("initDepth").data().toString(), QStringLiteral("Mixed"));
        QVERIFY(!rig.apply()->isEnabled());

        // Edges: the BC type enum.
        AggregateRig edges(makeStrip(2), "edges.2dm");
        edges.table.setSource(&edges.layer, Kind::Edge);
        const auto bs = boundaryEdges(edges.layer, 2);
        QCOMPARE(bs.size(), 2);
        const auto ref = [&](int i) {
            return mesh::MeshObjectRef::edge(edges.layer.sourcePath(), bs[i].first, bs[i].second);
        };
        using T = mesh::MeshBCTypes::Type;
        QVERIFY(edges.table.setData(edges.table.index(edges.table.rowForRef(ref(1)),
                                                      colFor(edges.table, "bcType")),
                                    int(T::NormalFlow)));
        edges.show(Kind::Edge, {ref(0), ref(1)});
        QCOMPARE(edges.field("bcType").data().toString(), QStringLiteral("Mixed"));
        edges.edit("bcType", [](QWidget *editor) {
            auto *combo = qobject_cast<QComboBox *>(editor);
            QVERIFY(combo);
            QCOMPARE(combo->currentIndex(), -1);
        });
        QCOMPARE(edges.field("bcType").data().toString(), QStringLiteral("Mixed"));
        edges.edit("bcType", [](QWidget *editor) {
            auto *combo = qobject_cast<QComboBox *>(editor);
            combo->setCurrentIndex(combo->findData(int(T::Wall)));
        });
        QCOMPARE(edges.field("bcType").data(Qt::EditRole).toInt(), int(T::Wall));

        // Vertices with a model: the coupled-node picker.
        const QString dir = QCoreApplication::applicationDirPath() + "/mesh_editor_roundtrip";
        QVERIFY(QDir().mkpath(dir));
        const QString path = dir + "/aggregate_picker.inp";
        QFile::remove(path);
        QVERIFY(QFile::copy(QFINDTESTDATA("data/mini_2d.inp"), path));
        SWMMModelLayer model(path);
        QList<QString> warnings, errors;
        QVERIFY2(model.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        mesh::MeshResult coupled = makeStrip(1);
        coupled.vertices[0].coupledNode = QStringLiteral("A");
        coupled.vertices[1].coupledNode = QStringLiteral("B");
        AggregateRig verts(coupled, "verts.2dm");
        verts.panel.setProject(&model);
        verts.table.setModelLayer(&model);
        const QString vpath = verts.layer.sourcePath();
        verts.show(Kind::Vertex,
                   {mesh::MeshObjectRef::vertex(vpath, 0), mesh::MeshObjectRef::vertex(vpath, 1)});
        QCOMPARE(verts.field("coupledNode").data().toString(), QStringLiteral("Mixed"));
        verts.edit("coupledNode", [](QWidget *editor) {
            QVERIFY(qobject_cast<DataObjectPickerEditor *>(editor));
        });
        QCOMPARE(verts.field("coupledNode").data().toString(), QStringLiteral("Mixed"));
        QVERIFY(!verts.apply()->isEnabled());
        QString chosen;
        verts.edit("coupledNode", [&chosen](QWidget *editor) {
            auto *combo = editor->findChild<QComboBox *>();
            QVERIFY(combo);
            for (int i = 0; i < combo->count() && chosen.isEmpty(); ++i)
                if (!combo->itemText(i).isEmpty() && combo->itemText(i) != combo->currentText()) {
                    chosen = combo->itemText(i);
                    combo->setCurrentIndex(i); // a user choice of a model node
                }
            QVERIFY(!chosen.isEmpty());
        });
        QCOMPARE(verts.field("coupledNode").data().toString(), chosen);
    }

    /** @brief Gate: the same edit through aggregate Properties and through the table's
     *  selected-row bulk apply gives the same mesh, the same file on save, the same state on
     *  reopen, and the same state after undo and redo. */
    void aggregateAndTableBulkApplyAgree()
    {
        AggregateRig a(aggregateMesh(), "agree_a.2dm");
        AggregateRig b(aggregateMesh(), "agree_b.2dm");
        const QList<int> rows{0, 1, 2};
        const QList<QPair<const char *, QVariant>> edits{
            {"mannings", 0.05}, {"initDepth", 0.25}, {"tag", QStringLiteral("pond")}};

        a.show(Kind::Cell, a.cells(rows));
        for (const auto &e : edits)
            QVERIFY(a.tree()->setData(a.field(e.first), e.second));
        a.apply()->click();

        // The table route: one "Apply value to N selected rows" per field.
        b.table.setSource(&b.layer, Kind::Cell);
        for (const auto &e : edits) {
            b.canvas.undoStack()->beginMacro(QStringLiteral("Apply value to 3 rows"));
            b.table.setDataForRows(colFor(b.table, e.first), rows, e.second);
            b.canvas.undoStack()->endMacro();
        }

        const auto same = [](const mesh::MeshResult &x, const mesh::MeshResult &y) {
            if (x.triangles.size() != y.triangles.size())
                return false;
            for (int t = 0; t < x.triangles.size(); ++t)
                if (x.triangles[t].tag != y.triangles[t].tag ||
                    x.triangles[t].mannings != y.triangles[t].mannings ||
                    x.triangles[t].initDepth != y.triangles[t].initDepth)
                    return false;
            return true;
        };
        QVERIFY(same(a.layer.mesh(), b.layer.mesh()));
        QCOMPARE(a.layer.mesh().triangles[0].initDepth, 0.25);

        const QString dir = QCoreApplication::applicationDirPath() + "/mesh_editor_roundtrip";
        QVERIFY(QDir().mkpath(dir));
        QByteArray saved[2];
        mesh::MeshResult reread[2];
        for (int i = 0; i < 2; ++i) {
            AggregateRig &rig = i == 0 ? a : b;
            const QString inp = dir + (i == 0 ? "/aggregate_route.inp" : "/table_route.inp");
            QFile deck(inp);
            QVERIFY(deck.open(QIODevice::WriteOnly | QIODevice::Truncate));
            deck.write("[TITLE]\nAggregate parity\n");
            deck.close();
            QString error; // the project save for a mesh the app owns
            QVERIFY2(mesh::InpMeshWriter::replaceMeshSections(inp, rig.layer.mesh(),
                                                              rig.layer.edgeBCs(), &error, 0.035),
                     qPrintable(error));
            QFile written(inp);
            QVERIFY(written.open(QIODevice::ReadOnly));
            saved[i] = written.readAll();
            const auto reopened = mesh::InpMeshReader::read(inp);
            QVERIFY2(reopened.hasMesh, qPrintable(reopened.errorMsg));
            reread[i] = reopened.mesh;
            for (int t = 0; t < 3; ++t) { // the edit survives the round trip exactly
                QCOMPARE(reopened.mesh.triangles[t].mannings, 0.05);
                QCOMPARE(reopened.mesh.triangles[t].initDepth, 0.25);
                QCOMPARE(reopened.mesh.triangles[t].tag, QStringLiteral("pond"));
            }
        }
        QCOMPARE(saved[0], saved[1]);
        QVERIFY(same(reread[0], reread[1]));

        a.canvas.undoStack()->undo();
        for (int i = 0; i < int(edits.size()); ++i)
            b.canvas.undoStack()->undo();
        QVERIFY(same(a.layer.mesh(), aggregateMesh()));
        QVERIFY(same(b.layer.mesh(), aggregateMesh()));
        a.canvas.undoStack()->redo();
        for (int i = 0; i < int(edits.size()); ++i)
            b.canvas.undoStack()->redo();
        QVERIFY(same(a.layer.mesh(), b.layer.mesh()));
    }

    /** @brief Gate: a value outside the field's range is refused by both routes with nothing
     *  written and no undo entry. */
    void aggregateRefusesWhatTheTableRefuses()
    {
        AggregateRig a(aggregateMesh(), "refuse_a.2dm");
        AggregateRig b(aggregateMesh(), "refuse_b.2dm");
        const QList<int> rows{0, 1, 2};
        a.show(Kind::Cell, a.cells(rows));
        const int undoA = a.canvas.undoStack()->count();
        QVERIFY(a.tree()->setData(a.field("mannings"), 0.0)); // staged; Apply validates
        a.apply()->click();
        QVERIFY(a.summary().contains(QStringLiteral("must be between")));
        QCOMPARE(a.layer.mesh().triangles[0].mannings, 0.03);
        QCOMPARE(a.canvas.undoStack()->count(), undoA);
        QVERIFY(a.apply()->isEnabled()); // the staged value stays for correction

        b.table.setSource(&b.layer, Kind::Cell);
        const int undoB = b.canvas.undoStack()->count();
        b.table.setDataForRows(colFor(b.table, "mannings"), rows, 0.0);
        QCOMPARE(b.layer.mesh().triangles[0].mannings, 0.03);
        QCOMPARE(b.canvas.undoStack()->count(), undoB);
    }

    /** @brief A staged parameter that a staged type change makes inapplicable is refused at
     *  write time: the whole Apply rolls back and leaves no undo or redo entry. */
    void aggregateRollsBackARefusedField()
    {
        AggregateRig rig(makeStrip(2), "rollback.2dm");
        rig.table.setSource(&rig.layer, Kind::Edge);
        const auto bs = boundaryEdges(rig.layer, 2);
        QList<SWMMObjectRef> refs;
        using T = mesh::MeshBCTypes::Type;
        for (int i = 0; i < 2; ++i) {
            const auto ref =
                mesh::MeshObjectRef::edge(rig.layer.sourcePath(), bs[i].first, bs[i].second);
            refs << ref;
            const int row = rig.table.rowForRef(ref);
            QVERIFY(rig.table.setData(rig.table.index(row, colFor(rig.table, "bcType")),
                                      int(T::NormalFlow)));
            QVERIFY(rig.table.setData(rig.table.index(row, colFor(rig.table, "slope")),
                                      0.01 * (i + 1)));
        }
        QVERIFY(colFor(rig.table, "bcType") < colFor(rig.table, "slope"));
        const auto before = rig.layer.edgeBCs();
        rig.show(Kind::Edge, refs);
        QCOMPARE(rig.field("slope").data().toString(), QStringLiteral("Mixed"));
        const int undoBefore = rig.canvas.undoStack()->count();
        QVERIFY(rig.tree()->setData(rig.field("slope"), 0.02));
        QVERIFY(rig.tree()->setData(rig.field("bcType"), int(T::Wall)));
        rig.apply()->click();
        QVERIFY2(rig.summary().contains(QStringLiteral("Nothing was changed")),
                 qPrintable(rig.summary()));
        QCOMPARE(rig.layer.edgeBCs(), before);
        QCOMPARE(rig.canvas.undoStack()->count(), undoBefore);
        QVERIFY(!rig.canvas.undoStack()->canRedo());
    }

    /** @brief Vertices and edges aggregate too; a field that applies to only some targets is
     *  read-only with a reason, and the boundary editor counts the boundary edges it will edit. */
    void aggregateVerticesAndEdges()
    {
        mesh::MeshResult m = makeStrip(2);
        m.vertices[0].z = 0.0;
        m.vertices[1].z = 0.5;
        m.vertices[2].z = 1.0;
        AggregateRig rig(m, "kinds.2dm");
        const QString path = rig.layer.sourcePath();
        rig.show(Kind::Vertex, {mesh::MeshObjectRef::vertex(path, 0),
                                mesh::MeshObjectRef::vertex(path, 1),
                                mesh::MeshObjectRef::vertex(path, 2)});
        QCOMPARE(rig.panel.windowTitle(), QStringLiteral("2D mesh: 3 vertices"));
        QCOMPARE(rig.field("z").data().toString(), QStringLiteral("Mixed"));
        QVERIFY(rig.tree()->setData(rig.field("z"), 2.0));
        rig.apply()->click();
        for (int v = 0; v < 3; ++v)
            QCOMPARE(rig.layer.mesh().vertices[v].z, 2.0);
        QCOMPARE(rig.layer.mesh().vertices[3].z, m.vertices[3].z);

        rig.table.setSource(&rig.layer, Kind::Edge);
        const auto bs = boundaryEdges(rig.layer, 2);
        QList<SWMMObjectRef> refs;
        for (const auto &b : bs)
            refs << mesh::MeshObjectRef::edge(path, b.first, b.second);
        int interiorCell = -1, interiorLocal = -1;
        for (int t = 0; t < rig.layer.triangleCount() && interiorCell < 0; ++t)
            for (int e = 0; e < 3; ++e)
                if (!rig.layer.isBoundaryEdge(t, e)) {
                    interiorCell = t;
                    interiorLocal = e;
                    break;
                }
        QVERIFY(interiorCell >= 0);
        refs << mesh::MeshObjectRef::edge(path, interiorCell, interiorLocal);
        rig.show(Kind::Edge, refs);
        QCOMPARE(rig.panel.windowTitle(), QStringLiteral("2D mesh: 3 edges"));
        QVERIFY(!(rig.field("bcType").flags() & Qt::ItemIsEditable));
        QVERIFY(rig.field("bcType").data(Qt::ToolTipRole).toString().contains(
            QStringLiteral("Applies to 2 of the 3")));
        QVERIFY(!rig.tree()->setData(rig.field("bcType"), 0)); // refused, not staged
        QVERIFY(rig.field("conveyance").flags() & Qt::ItemIsEditable);
        auto *boundary = rig.panel.findChild<QPushButton *>("editMeshBoundary");
        QVERIFY(boundary && boundary->isEnabled());
        QVERIFY(boundary->toolTip().contains(QStringLiteral("2 selected boundary edges")));
    }

    /** @brief Elements of different kinds or meshes are summarized with counts, not edited. */
    void aggregateSummarizesMixedKinds()
    {
        AggregateRig rig(aggregateMesh(), "mixed.2dm");
        const QString path = rig.layer.sourcePath();
        rig.show(Kind::Cell, {mesh::MeshObjectRef::cell(path, 0), mesh::MeshObjectRef::cell(path, 1),
                              mesh::MeshObjectRef::edge(path, 0, 0)});
        QCOMPARE(rig.panel.windowTitle(), QStringLiteral("Mixed selection"));
        QCOMPARE(rig.summary(), QStringLiteral("2 cells, 1 edge. Select elements of one kind on "
                                               "one mesh to edit them together."));
        QCOMPARE(rig.tree()->rowCount(), 0);
        QVERIFY(rig.apply()->isHidden());
    }

    /** @brief Staged edits are discarded with a note when the panel moves on, and a remesh or
     *  a vanished target never writes. */
    void aggregateDiscardsAndReportsStaleTargets()
    {
        AggregateRig rig(aggregateMesh(), "stale.2dm");
        const QString path = rig.layer.sourcePath();
        auto *note = rig.panel.findChild<QLabel *>("meshAggregateNote");
        QVERIFY(note);
        rig.show(Kind::Cell, rig.cells({0, 1}));
        QVERIFY(rig.tree()->setData(rig.field("mannings"), 0.07));
        rig.panel.showMeshObject(&rig.canvas, mesh::MeshObjectRef::cell(path, 3));
        QVERIFY(!note->isHidden());
        QCOMPARE(note->text(),
                 QStringLiteral("1 unapplied change to the previous selection was discarded."));
        QCOMPARE(rig.layer.mesh().triangles[0].mannings, 0.03);
        rig.show(Kind::Cell, rig.cells({0, 1}));
        QVERIFY(note->isHidden());

        QVERIFY(rig.tree()->setData(rig.field("mannings"), 0.07));
        emit rig.layer.selectionInvalidated(); // what a remesh announces
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(rig.tree()->rowCount(), 0);
        QCOMPARE(rig.layer.mesh().triangles[0].mannings, 0.03);

        rig.show(Kind::Cell, {mesh::MeshObjectRef::cell(path, 0),
                              mesh::MeshObjectRef::cell(path, 99)});
        QVERIFY(rig.summary().contains(QStringLiteral("1 no longer exist")));
        QVERIFY(rig.tree()->setData(rig.field("mannings"), 0.07));
        rig.apply()->click();
        QVERIFY(rig.summary().contains(QStringLiteral("no longer exist. Select them again")));
        QCOMPARE(rig.layer.mesh().triangles[0].mannings, 0.03);
    }

    /** @brief Parity matrix (plan §7.3): for every mesh field of every kind, the aggregate is
     *  editable exactly when the table can edit that field on every selected element. Writes
     *  the matrix as Markdown when SWMMVIS_PARITY_MATRIX_OUT names a file. */
    void meshParityMatrix()
    {
        AggregateRig rig(aggregateMesh(), "matrix.2dm");
        const QString path = rig.layer.sourcePath();
        QStringList lines{QStringLiteral("| Kind | Key | Label | Group | Editor | Range | "
                                         "Table editable (selected) | Properties aggregate |"),
                          QStringLiteral("|---|---|---|---|---|---|---|---|")};
        const char *kindNames[] = {"Vertex", "Edge", "Cell"};
        for (Kind kind : {Kind::Vertex, Kind::Edge, Kind::Cell}) {
            rig.table.setSource(&rig.layer, kind);
            QList<SWMMObjectRef> refs;
            QList<int> rows;
            for (int r = 0; r < rig.table.rowCount(); ++r) {
                refs << rig.table.refForRow(r);
                rows << r;
            }
            rig.show(kind, refs);
            QCOMPARE(rig.fieldCount(), rig.table.columnCount());
            const auto specs = rig.table.columnSpecs();
            for (int c = 0; c < specs.size(); ++c) {
                int editable = 0;
                for (int r : rows)
                    if (rig.table.flags(rig.table.index(r, c)) & Qt::ItemIsEditable)
                        ++editable;
                const QModelIndex item = rig.field(specs[c].key.toLatin1().constData());
                QVERIFY2(item.isValid(), qPrintable(specs[c].key));
                const bool aggregate = item.flags() & Qt::ItemIsEditable;
                QVERIFY2(aggregate == (editable == int(rows.size())),
                         qPrintable(QStringLiteral("%1.%2: table %3/%4, aggregate %5")
                                        .arg(kindNames[int(kind)], specs[c].key)
                                        .arg(editable)
                                        .arg(rows.size())
                                        .arg(aggregate)));
                const QString range = std::isfinite(specs[c].minValue) &&
                                              std::isfinite(specs[c].maxValue) &&
                                              (specs[c].editor == openswmmvis::EditorKind::Numeric)
                                          ? QStringLiteral("%1 … %2")
                                                .arg(specs[c].minValue)
                                                .arg(specs[c].maxValue)
                                          : QString();
                lines << QStringLiteral("| %1 | `%2` | %3 | %4 | %5 | %6 | %7/%8 | %9 |")
                             .arg(kindNames[int(kind)], specs[c].key, specs[c].label,
                                  item.parent().data().toString())
                             .arg(int(specs[c].editor))
                             .arg(range)
                             .arg(editable)
                             .arg(rows.size())
                             .arg(aggregate ? QStringLiteral("editable")
                                            : QStringLiteral("read-only"));
            }
        }
        const QString out = qEnvironmentVariable("SWMMVIS_PARITY_MATRIX_OUT");
        if (!out.isEmpty()) {
            QFile file(out);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
            file.write(lines.join('\n').toUtf8() + '\n');
        }
    }

    /** @brief A large selection builds and reads its summaries in bounded time (logged). */
    void aggregateLargeSelectionIsResponsive()
    {
        AggregateRig rig(makeStrip(10000), "large.2dm"); // 20,000 cells
        QList<SWMMObjectRef> refs;
        for (int t = 0; t < rig.layer.triangleCount(); ++t)
            refs << mesh::MeshObjectRef::cell(rig.layer.sourcePath(), t);
        // Process CPU time, not wall time: other builds share this machine.
        QElapsedTimer timer;
        timer.start();
        const std::clock_t cpuStart = std::clock();
        rig.show(Kind::Cell, refs);
        for (int i = 0; i < rig.tree()->rowCount(); ++i) {
            const auto group = rig.tree()->index(i, 0);
            for (int j = 0; j < rig.tree()->rowCount(group); ++j)
                rig.tree()->index(j, 1, group).data();
        }
        const double cpuMs = 1000.0 * double(std::clock() - cpuStart) / CLOCKS_PER_SEC;
        qInfo().noquote() << "aggregate of" << refs.size() << "cells, every field read:"
                          << qRound(cpuMs) << "ms CPU," << timer.elapsed() << "ms wall";
        QVERIFY2(cpuMs < 10000, qPrintable(QString::number(cpuMs)));
        QVERIFY(rig.tree()->setData(rig.field("mannings"), 0.05));
        timer.restart();
        rig.apply()->click();
        qInfo().noquote() << "apply to" << refs.size() << "cells:" << timer.elapsed() << "ms";
        QCOMPARE(rig.layer.mesh().triangles.last().mannings, 0.05);
    }

    /** @brief Review captures of the panel when SWMMVIS_REVIEW_CAPTURE_DIR is set. */
    void aggregateReviewCaptures()
    {
        const QString dir = qEnvironmentVariable("SWMMVIS_REVIEW_CAPTURE_DIR");
        if (dir.isEmpty())
            QSKIP("SWMMVIS_REVIEW_CAPTURE_DIR not set");
        QVERIFY(QDir().mkpath(dir));
        AggregateRig rig(aggregateMesh(), "review.2dm");
        rig.panel.resize(460, 720);
        rig.panel.show();
        rig.show(Kind::Cell, rig.cells({0, 1, 2}));
        QVERIFY(rig.tree()->setData(rig.field("mannings"), 0.05));
        QVERIFY(rig.tree()->setData(rig.field("tag"), QStringLiteral("pond")));
        QCoreApplication::processEvents();
        QVERIFY(rig.panel.grab().save(dir + "/aggregate-cells-staged.png"));
        rig.apply()->click();
        QCoreApplication::processEvents();
        QVERIFY(rig.panel.grab().save(dir + "/aggregate-cells-applied.png"));
        const QString path = rig.layer.sourcePath();
        rig.show(Kind::Cell, {mesh::MeshObjectRef::cell(path, 0),
                              mesh::MeshObjectRef::edge(path, 0, 0)});
        QCoreApplication::processEvents();
        QVERIFY(rig.panel.grab().save(dir + "/aggregate-mixed-kinds.png"));
    }
};

QTEST_MAIN(TestMeshAttributeTable)
#include "test_meshattributetable.moc"
