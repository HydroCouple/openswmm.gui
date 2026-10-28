/**
 * @file      test_attributetableschema.cpp
 * @brief     Attribute Table column-schema contract.
 *
 * Guards three properties the schema has to keep:
 *
 *   1. The conduit cross-section block (XSection compound cell, inline
 *      Geom 1-4, Barrels) is present, carries the right delegates, and
 *      stays EDITABLE for the tested circular/irregular shape cases. It used to blank + grey each geom
 *      the live shape didn't consume, so a CIRCULAR conduit showed three
 *      permanently empty uneditable columns — which reads as broken
 *      editors, and threw away a width typed before a shape switch.
 *      Only the picker-owned index slots (IRREGULAR / STREET geom1,
 *      CUSTOM geom2) stay locked.
 *
 *   2. Post-run "dynamics" statistics form a contiguous suffix of every
 *      category's schema — never interleaved with the editable inputs.
 *
 *   3. The per-category attribute coverage that the engine C API exposes
 *      is actually surfaced (conduit Slope, subcatchment Tag, storage
 *      exfiltration triple, outfall RouteTo).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/swmmlayeritem.h"
#include "map/swmmlayerqsgrenderer.h"
#include "mesh/meshobjectref.h"
#include "mesh/meshresult.h"
#include "plugins/extensionattributes.h"
#include "selection/selectionmanager.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/dataobjectattributetablemodel.h"
#include "ui/panels/meshattributetablemodel.h"
#include "ui/panels/attributedelegates.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/properties/swmmlinkpropertyadapter.h"
#include "ui/properties/linkendpointcombobox.h"
#include "ui/properties/attributegroups.h"
#ifdef HAVE_QPROPERTYMODEL
#include "ui/properties/groupedpropertymodel.h"
#include "ui/properties/swmmnodepropertyadapter.h"
#include <qpropertymodel.h>
#include <QAbstractItemModelTester>
#endif
#include "ui/properties/xsectshapegeom.h"
#include "ui/toolbars/mesheditingtoolbar.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"

#include "ui/properties/nodecompoundeditref.h"
#include "ui/properties/swmmsubcatchpropertyadapter.h"
#include "ui/properties/dataobjectref.h"
#include "ui/dialogs/snowpackeditordialog.h"
#include "snowpack/snowpackregistry.h"
#include <QTimer>
#include <QDialogButtonBox>
#include <QPushButton>

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_initial_quality.h> // Initial-quality UI round
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h> // swmm_options_set
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h> // Initial-quality UI round
#include <openswmm/engine/openswmm_subcatchments.h>

#include <algorithm> // std::reverse — ordering-override pin

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTreeView>
#include <QUndoStack>
#include <QDir>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QLocale>
#include <QObject>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTest>

#include <memory>

using openswmmvis::ColumnSpec;
using openswmmvis::EditorKind;

namespace
{

/**
 *
 * @brief Locate the test fixture/evidence base without creating it.
 * @return SWMMVIS_GUI_TEST_DATA verbatim when set, otherwise the relative dot path;
 * caller resolves filenames and creates any required evidence directories.
 *
 */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/**
 *
 * @brief Load typed_selection_fixture.inp into a test-owned editing model layer.
 * @return Owning unique_ptr on successful load, null on refusal; local warnings/errors
 * are collected but not surfaced. Layer destruction releases its engine; does not copy
 * or write the fixture. Callers retain it while borrowed model/adapter refs are used.
 *
 */
std::unique_ptr<SWMMModelLayer> openLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp")), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

/**
 *
 * @brief Find the first exact schema-key match.
 * @param specs Borrowed ordered column specifications.
 * @param key Borrowed exact case-sensitive key, without trimming or label/header aliasing.
 * @return Zero-based column or minus one; does not mutate/rebuild the schema.
 *
 */
int colFor(const QList<ColumnSpec> &specs, const QString &key)
{
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].key == key)
            return i;
    return -1;
}

/**
 *
 * @brief Read the native entity id for a category family and engine index.
 * @param eng Borrowed valid editing engine for supported categories.
 * @param cat SWMM spatial category, mapped to node/link/subcatchment/gage id getter.
 * @param idx Native engine index, not a displayed/category row; getter handles bounds.
 * @return Borrowed engine-owned C string or null for unsupported/missing entity.
 * Caller retains engine lifetime and copies before invalidating/rebuilding its storage.
 *
 */
const char *engineIdFor(SWMM_Engine eng, SWMMModelLayer::Category cat, int idx)
{
    switch (cat) {
    case SWMMModelLayer::CatJunctions:
    case SWMMModelLayer::CatOutfalls:
    case SWMMModelLayer::CatStorage:
    case SWMMModelLayer::CatDividers:
        return swmm_node_id(eng, idx);
    case SWMMModelLayer::CatConduits:
    case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:
    case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return swmm_link_id(eng, idx);
    case SWMMModelLayer::CatSubcatchments:
        return swmm_subcatch_id(eng, idx);
    case SWMMModelLayer::CatRainGages:
        return swmm_gage_id(eng, idx);
    default:
        return nullptr;
    }
}

/**
 *
 * @brief Enumerate the eleven spatial categories exercised by the schema invariants.
 * @return Owned list of four node, five link, subcatchment and rain-gage categories;
 * nonspatial data categories are outside this helper's coverage.
 *
 */
QList<SWMMModelLayer::Category> allCategories()
{
    return {
        SWMMModelLayer::CatJunctions,     SWMMModelLayer::CatOutfalls,  SWMMModelLayer::CatStorage,
        SWMMModelLayer::CatDividers,      SWMMModelLayer::CatConduits,  SWMMModelLayer::CatPumps,
        SWMMModelLayer::CatOrifices,      SWMMModelLayer::CatWeirs,     SWMMModelLayer::CatOutlets,
        SWMMModelLayer::CatSubcatchments, SWMMModelLayer::CatRainGages,
    };
}

/**
 *
 * @brief Classify a schema entry using its native statistic tag convention.
 * @param s Borrowed column spec; tests its setter string for case-sensitive _stat_.
 * @return True for that substring, false otherwise; does not inspect editor kind,
 * label, runtime availability or actual engine getter implementation.
 *
 */
bool isDynamics(const ColumnSpec &s)
{
    return s.setter.contains(QStringLiteral("_stat_"));
}

bool applyBulkCell(AttributeTablePanel &panel, QTableView *view, const QModelIndex &clicked,
                   bool prompt = false, double value = 0.0)
{
    if (!clicked.isValid())
        return false;
    panel.resize(1200, 600);
    panel.show();
    view->scrollTo(clicked);
    QCoreApplication::processEvents();
    const QPoint pos = view->visualRect(clicked).center();
    if (!view->viewport()->rect().contains(pos))
        return false;
    bool applied = false;
    QTimer::singleShot(0, &panel, [&] {
        auto *menu = panel.findChild<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
        if (!menu)
            return;
        for (auto *action : menu->actions()) {
            if (action->text().startsWith(prompt ? "Apply \"" : "Apply this ")) {
                if (prompt) {
                    QTimer::singleShot(0, &panel, [&] {
                        if (auto *input = qobject_cast<QInputDialog *>(
                                QApplication::activeModalWidget())) {
                            input->setDoubleValue(value);
                            input->accept();
                        }
                    });
                }
                action->trigger();
                applied = true;
                break;
            }
        }
        menu->close();
    });
    return QMetaObject::invokeMethod(&panel, "onContextMenuRequested", Q_ARG(QPoint, pos)) &&
           applied;
}

} // namespace

class TestAttributeTableSchema : public QObject
{
    Q_OBJECT

private slots:
    void meshBulkApplyRefreshesOnce_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("key");
        QTest::addColumn<double>("value");
        QTest::newRow("cells") << QString("c") << QString("mannings") << 0.04;
        QTest::newRow("edges") << QString("e") << QString("conveyance") << 0.5;
        QTest::newRow("vertices") << QString("v") << QString("z") << 42.0;
        QTest::newRow("infiltration") << QString("c") << QString("infil.f0") << 40.0;
        QTest::newRow("coupling") << QString("v") << QString("couplingCd") << 0.75;
    }

    void meshBulkApplyRefreshesOnce()
    {
        QFETCH(QString, kind);
        QFETCH(QString, key);
        QFETCH(double, value);
        auto layer = openLayer();
        QVERIFY(layer);
        mesh::MeshResult meshData;
        const int scaleVertices = qEnvironmentVariableIntValue("SWMMVIS_BULK_SCALE_VERTICES");
        const int cells = kind == "v" ? std::max(512, (scaleVertices + 2) / 3) : 512;
        meshData.vertices.reserve(cells * 3);
        meshData.triangles.reserve(cells);
        for (int i = 0; i < cells; ++i) {
            const int first = meshData.vertices.size();
            for (const auto &xy : {QPointF(i * 2, 0), QPointF(i * 2 + 1, 0), QPointF(i * 2, 1)}) {
                mesh::MeshVertex vertex;
                vertex.xy = xy;
                vertex.z = 0.0;
                if (key == "couplingCd") {
                    vertex.coupledNode = "J1";
                    vertex.couplingCdSet = false;
                    vertex.couplingAreaSet = false;
                }
                meshData.vertices.append(vertex);
            }
            mesh::MeshTriangle triangle;
            triangle.v0 = first;
            triangle.v1 = first + 1;
            triangle.v2 = first + 2;
            triangle.mannings = 0.02;
            meshData.triangles.append(triangle);
        }
        if (key.startsWith("infil.")) {
            mesh::InfilRow inherited;
            inherited.method = mesh::InfilMethod::Horton;
            for (int k = 0; k < mesh::kInfilMaxParams; ++k)
                inherited.p[k] = k + 1.0;
            meshData.infilDefaults.append({"*", inherited});
            for (int row = 1; row < cells; row += 2) {
                auto override = inherited;
                override.p[1] = row + 10.0;
                meshData.infilOverrides.insert(row, override);
            }
        }
        meshData.ok = true;
        SWMM2DMeshLayer meshLayer(meshData, QStringLiteral("bulk-mesh.2dm"));
        MapCanvas canvas;
        canvas.addLayer(&meshLayer, false);
        meshLayer.setActiveMesh(true);
        SelectionManager selection;
        MeshEditingToolbar toolbar(QStringLiteral("Mesh"));
        toolbar.rebindCanvas(&canvas);
        toolbar.rebindSelectionManager(&selection);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selection, &canvas);
        panel.refresh();
        auto *combo = panel.findChild<QComboBox *>();
        auto *view = panel.findChild<QTableView *>();
        auto *model = panel.findChild<MeshAttributeTableModel *>();
        QVERIFY(combo && view && model);
        const auto sourceKey = QString("mesh:%1:%2").arg(meshLayer.layerId(), kind);
        const int category = combo->findData(sourceKey);
        QVERIFY(category >= 0);
        combo->setCurrentIndex(category);
        const int column = colFor(model->columnSpecs(), qPrintable(key));
        QVERIFY(column >= 0);
        const auto clickedSource = model->index(model->rowCount() - 1, column);
        const double original = model->index(0, column).data(Qt::EditRole).toDouble();
        QVERIFY(model->setData(clickedSource, value));
        canvas.undoStack()->clear();
        QSet<SWMMObjectRef> selected;
        for (int row = 0; row < model->rowCount(); ++row)
            selected.insert(model->refForRow(row));
        selection.select(selected, SelectionManager::Replace);
        QCOMPARE(view->selectionModel()->selectedRows().size(), model->rowCount());
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
        QVERIFY(proxy);
        const auto clicked = proxy->mapFromSource(clickedSource);
        QSignalSpy attributes(&meshLayer, &SWMM2DMeshLayer::attributeChanged);
        QSignalSpy refreshes(model, &QAbstractItemModel::dataChanged);
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        // Mirror the main window's dirty bridge; it must be coalesced too.
        connect(&panel, &AttributeTablePanel::objectEdited, layer.get(),
                [&](const QString &) { layer->markEdited(); });
        QElapsedTimer timer;
        timer.start();
        QVERIFY(applyBulkCell(panel, view, clicked));
        qInfo() << kind << "bulk apply:" << model->rowCount() << "rows in" << timer.elapsed()
                << "ms; mesh refreshes:" << attributes.count();
        QCOMPARE(attributes.count(), 1);
        QCOMPARE(refreshes.count(), 1);
        QCOMPARE(dirty.count(), 1);
        QVERIFY(selection.selection() == selected);
        QCOMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(canvas.undoStack()->command(0)->childCount(), 1);
        for (int row = 0; row < model->rowCount(); ++row)
            QCOMPARE(model->index(row, column).data(Qt::EditRole).toDouble(), value);
        timer.restart();
        canvas.undoStack()->undo();
        qInfo() << kind << "bulk undo:" << timer.elapsed() << "ms";
        QCOMPARE(attributes.count(), 2);
        QCOMPARE(model->index(0, column).data(Qt::EditRole).toDouble(), original);
        QCOMPARE(clickedSource.data(Qt::EditRole).toDouble(), value);
        if (key.startsWith("infil.")) {
            QVERIFY(!meshLayer.mesh().infilOverrides.contains(0));
            QCOMPARE(meshLayer.mesh().infilOverrides[1].p[1], 11.0);
        }
        if (key == "couplingCd")
            QVERIFY(!meshLayer.mesh().vertices[0].couplingCdSet);
        timer.restart();
        canvas.undoStack()->redo();
        qInfo() << kind << "bulk redo:" << timer.elapsed() << "ms";
        QCOMPARE(attributes.count(), 3);
        QCOMPARE(model->index(0, column).data(Qt::EditRole).toDouble(), value);
    }

    void dataBulkApplyRetainsUndoAndReferences()
    {
        auto layer = std::make_unique<SWMMModelLayer>(
            QDir(dataDir()).filePath("data_object_table_fixture.inp"), nullptr);
        QList<QString> warnings, errors;
        QVERIFY(layer->loadModel(warnings, errors));
        auto *registry = qobject_cast<openswmmvis::street::StreetRegistry *>(
            layer->ensureStreetRegistry());
        QVERIFY(registry);
        DataObjectAttributeTableModel model;
        QUndoStack undo;
        model.setSource(layer.get(), SWMMModelLayer::DataStreets);
        model.setUndoStack(&undo);
        const int column = colFor(model.columnSpecs(), "roadRoughness");
        QVERIFY(column >= 0);
        const auto engineRoughness = [&](const char *name) {
            double roughness = -1.0;
            swmm_street_get_params(layer->engine(), swmm_street_index(layer->engine(), name),
                                   nullptr, nullptr, nullptr, &roughness, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr);
            return roughness;
        };
        QSignalSpy edits(&model, &DataObjectAttributeTableModel::objectEdited);
        model.setDataForRows(column, {0, 1, -1, model.rowCount()}, 0.02);
        QCOMPARE(edits.count(), 2);
        QCOMPARE(undo.count(), 1);
        for (int row = 0; row < model.rowCount(); ++row)
            QCOMPARE(model.index(row, column).data(Qt::EditRole).toDouble(), 0.02);
        QCOMPARE(engineRoughness("ST1"), 0.02);
        QCOMPARE(engineRoughness("ST2"), 0.02);
        model.setDataForRows(column, {0, 1}, 0.02); // no-op
        model.setDataForRows(column, {0, 1}, QStringLiteral("invalid"));
        model.setDataForRows(0, {0, 1}, QStringLiteral("read-only name"));
        QCOMPARE(undo.count(), 1);
        QCOMPARE(edits.count(), 2);
        // Replay remains tied to the original category after rebinding the view.
        model.setSource(layer.get(), SWMMModelLayer::DataPollutants);
        undo.undo();
        QCOMPARE(registry->findByName("ST1")->roadRoughness(), 0.016);
        QCOMPARE(registry->findByName("ST2")->roadRoughness(), 0.016);
        QCOMPARE(engineRoughness("ST1"), 0.016);
        QCOMPARE(engineRoughness("ST2"), 0.016);
        undo.redo();
        QCOMPARE(registry->findByName("ST1")->roadRoughness(), 0.02);
        QCOMPARE(registry->findByName("ST2")->roadRoughness(), 0.02);
        QCOMPARE(engineRoughness("ST1"), 0.02);
        QCOMPARE(engineRoughness("ST2"), 0.02);
    }

    void bulkApplyDefersSorting_data()
    {
        QTest::addColumn<bool>("filtered");
        QTest::newRow("sorted") << false;
        QTest::newRow("filtered") << true;
    }

    /** Bulk apply must edit the captured rows with one sort/filter pass and one undo step. */
    void bulkApplyDefersSorting()
    {
        QFETCH(bool, filtered);
        auto layer = openLayer();
        QVERIFY(layer);
        constexpr int count = 256;
        {
            SWMMModelLayer::BulkEdit bulk(layer.get());
            for (int i = 0; i < count; ++i) {
                const auto name = QString("Bulk%1").arg(i, 4, 10, QLatin1Char('0'));
                int node = -1;
                QVERIFY(layer->applyNodeAdd(name, SWMM_NODE_JUNCTION, i, 0.0, &node));
                QCOMPARE(swmm_node_set_invert_elev(layer->engine(), node, i + 1.0), SWMM_OK);
            }
        }
        SelectionManager selection;
        MapCanvas canvas;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selection, &canvas);
        panel.refresh();
        auto *combo = panel.findChild<QComboBox *>();
        auto *view = panel.findChild<QTableView *>();
        auto *model = panel.findChild<SWMMAttributeTableModel *>();
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view ? view->model() : nullptr);
        QVERIFY(combo && view && model && proxy);
        combo->setCurrentIndex(combo->findData(int(SWMMModelLayer::CatJunctions)));
        const int column = colFor(model->columnSpecs(), "Invert elev");
        QVERIFY(column >= 0);
        const auto cellFor = [&](int i) {
            const auto name = QString("Bulk%1").arg(i, 4, 10, QLatin1Char('0'));
            return model->index(model->rowForName(name), column);
        };
        view->sortByColumn(column, Qt::AscendingOrder);
        if (filtered) {
            auto *query = panel.findChild<QLineEdit *>();
            QVERIFY(query);
            query->setText("[Invert elev] < 256");
            QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        }
        QSet<SWMMObjectRef> selected;
        for (int i = 0; i < count - 1; i += 2)
            selected.insert({SWMMObjectRef::Node,
                             QString("Bulk%1").arg(i, 4, 10, QLatin1Char('0'))});
        selection.select(selected, SelectionManager::Replace);
        QCOMPARE(view->selectionModel()->selectedRows().size(), selected.size());
        const double value = filtered ? 512.0 : 255.0;
        const auto clicked = proxy->mapFromSource(cellFor(254));
        QVERIFY(clicked.isValid());
        QSignalSpy layouts(proxy, &QAbstractItemModel::layoutChanged);
        QSignalSpy edits(&panel, &AttributeTablePanel::objectEdited);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(applyBulkCell(panel, view, clicked, filtered, value));
        qInfo() << "bulk apply:" << timer.elapsed() << "ms; layout changes:" << layouts.count();
        const int layoutCount = layouts.count();
        if (!filtered)
            QVERIFY(selection.selection() == selected);
        QCOMPARE(edits.count(), selected.size() - (filtered ? 0 : 1));
        QCOMPARE(canvas.undoStack()->count(), 1);
        for (int i = 0; i < count; ++i) {
            const auto index = cellFor(i);
            QCOMPARE(index.data(Qt::EditRole).toDouble(), i % 2 == 0 ? value : i + 1.0);
            if (i % 2 == 0)
                QCOMPARE(proxy->mapFromSource(index).isValid(), !filtered);
        }
        canvas.undoStack()->undo();
        for (int i = 0; i < count; ++i)
            QCOMPARE(cellFor(i).data(Qt::EditRole).toDouble(), i + 1.0);
        canvas.undoStack()->redo();
        for (const auto &ref : selected)
            QCOMPARE(model->index(model->rowForName(ref.name), column).data(Qt::EditRole).toDouble(),
                     value);
        QVERIFY2(layoutCount <= (filtered ? 2 : 1),
                 "Bulk apply must rebuild the view once, rather than after every row");
    }

    void linkEndpointsAreEditable_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("upstream");
        for (int kind = SWMM_LINK_CONDUIT; kind <= SWMM_LINK_OUTLET; ++kind) {
            QTest::newRow(qPrintable(QString("type%1-from").arg(kind))) << kind << true;
            QTest::newRow(qPrintable(QString("type%1-to").arg(kind))) << kind << false;
        }
    }

    /** Both endpoint columns support validated edits, undo and persistence for every link type. */
    void linkEndpointsAreEditable()
    {
        QFETCH(int, kind);
        QFETCH(bool, upstream);
        auto layer = openLayer();
        QVERIFY(layer);
        int link = -1;
        const QVector<QPointF> bends = {QPointF(25, 35), QPointF(45, 55)};
        QVERIFY(layer->applyLinkAdd("LPicker", kind, "J1", "X1", bends, &link));
        const auto category = SWMMModelLayer::Category(int(SWMMModelLayer::CatConduits) + kind);
        QUndoStack undo;
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), category);
        model.setUndoStack(&undo);
        int row = -1;
        for (int r = 0; r < model.rowCount(); ++r)
            if (model.objectNameAt(r) == "LPicker")
                row = r;
        QVERIFY(row >= 0);
        const auto specs = model.columnSpecs();
        const int col = colFor(specs, upstream ? "From node" : "To node");
        QVERIFY(col >= 0);
        QCOMPARE(specs[col].editor, EditorKind::LinkEndpoint);
        const auto cell = model.index(row, col);
        QVERIFY(model.flags(cell) & Qt::ItemIsEditable);
        const QString original = upstream ? "J1" : "X1";
        QCOMPARE(cell.data().toString(), original);
        for (const QString invalid :
             {QString(), QString("missing"), upstream ? QString("X1") : QString("J1")})
            QVERIFY(!model.setData(cell, invalid));
        QCOMPARE(undo.count(), 0);
        QSignalSpy geometry(layer.get(), &SWMMModelLayer::geometryChanged);
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        QVERIFY(model.setData(cell, "O1"));
        QCOMPARE(cell.data().toString(), QString("O1"));
        QCOMPARE(layer->cachedLinkInteriorVertices(link), bends);
        const auto polyline = layer->cachedLinkPolyline(link);
        const auto oldPolyline = polyline;
        double x = 0, y = 0;
        QVERIFY(layer->cachedNodeCoord(layer->nodeIndex("O1"), &x, &y));
        QCOMPARE(upstream ? polyline.first() : polyline.last(), QPointF(x, y));
        QCOMPARE(geometry.count(), 1);
        QCOMPARE(dirty.count(), 1);
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(cell.data().toString(), original);
        undo.redo();
        QCOMPARE(cell.data().toString(), QString("O1"));
        QCOMPARE(layer->cachedLinkPolyline(link), oldPolyline);
        SWMMLinkPropertyAdapter adapter(layer->engine(), "LPicker");
        adapter.setModelLayer(layer.get());
        auto ref = upstream ? adapter.fromNodeRef() : adapter.toNodeRef();
        ref.nodeName = original;
        if (upstream)
            adapter.setFromNodeRef(ref);
        else
            adapter.setToNodeRef(ref);
        QCOMPARE(upstream ? adapter.fromNode() : adapter.toNode(), original);
        ref.nodeName = "O1";
        if (upstream)
            adapter.setFromNodeRef(ref);
        else
            adapter.setToNodeRef(ref);
        QCOMPARE(upstream ? adapter.fromNode() : adapter.toNode(), QString("O1"));
        QTemporaryDir saved;
        QVERIFY(saved.isValid());
        const auto path = saved.filePath("endpoints.inp");
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reloaded(path);
        QList<QString> warnings, errors;
        QVERIFY(reloaded.loadModel(warnings, errors));
        SWMMLinkPropertyAdapter reread(reloaded.engine(), "LPicker");
        QCOMPARE(upstream ? reread.fromNode() : reread.toNode(), QString("O1"));
    }

    /** The table's live delegate searches existing nodes and rejects incomplete IDs. */
    void linkEndpointPickerUsesCompletion()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.showModelCategory(layer.get(), SWMMModelLayer::CatConduits);
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        auto *model = view->model();
        int col = -1;
        for (int c = 0; c < model->columnCount(); ++c)
            if (model->headerData(c, Qt::Horizontal).toString() == "From Node")
                col = c;
        QVERIFY(col >= 0);
        int row = -1;
        for (int r = 0; r < model->rowCount(); ++r)
            if (model->index(r, 0).data().toString() == "C1")
                row = r;
        QVERIFY(row >= 0);
        const auto cell = model->index(row, col);
        auto *delegate = view->itemDelegateForIndex(cell);
        QWidget host;
        std::unique_ptr<QWidget> widget(delegate->createEditor(&host, {}, cell));
        auto *combo = qobject_cast<LinkEndpointComboBox *>(widget.get());
        QVERIFY(combo);
        delegate->setEditorData(combo, cell);
        QCOMPARE(combo->currentText(), QString("J1"));
        QVERIFY(combo->findText("X1") < 0); // Opposite endpoint.
        combo->completer()->setCompletionPrefix("o");
        QCOMPARE(combo->completer()->completionCount(), 1);
        QCOMPARE(combo->completer()->currentCompletion(), QString("O1"));
        combo->setEditText("missing");
        delegate->setModelData(combo, model, cell);
        QCOMPARE(cell.data().toString(), QString("J1"));
        combo->setEditText("O1");
        delegate->setModelData(combo, model, cell);
        QCOMPARE(cell.data().toString(), QString("O1"));
    }

    void linkEndpointPickerProtectsVirtualConnections()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        int virtualNode = -1, otherLink = -1;
        QString error;
        QVERIFY2(layer->applyInsertVirtualJunction("C1", 0.5, "VPicker", "CSplit", &virtualNode,
                                                   &otherLink, &error),
                 qPrintable(error));
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const int to = colFor(model.columnSpecs(), "To node");
        const int from = colFor(model.columnSpecs(), "From node");
        QVERIFY(to >= 0 && from >= 0);
        const auto fixed = model.index(0, to); // C1 now ends at VPicker.
        QCOMPARE(fixed.data().toString(), QString("VPicker"));
        QVERIFY(!(model.flags(fixed) & Qt::ItemIsEditable));
        QVERIFY(!model.setData(fixed, "O1"));
        // A different link must not create a third connection at that virtual node.
        const int link = swmm_link_index(layer->engine(), "X1");
        QVERIFY(!layer->applyLinkEndpoint(link, true, virtualNode));
        LinkEndpointComboBox picker;
        picker.setValue({layer->engine(), "X1", true, "X1"});
        QVERIFY(picker.findText("VPicker") < 0);
        const auto ref = fixed.data(openswmmvis::kLinkEndpointRole).value<LinkEndpointRef>();
        picker.setValue(ref);
        QCOMPARE(picker.count(), 1);
        QCOMPARE(picker.currentText(), QString("VPicker"));
    }

    void linkEndpointPickerHandlesLargeNodeLists()
    {
        const auto engine = swmm_engine_new();
        QVERIFY(engine);
        const auto cleanup = qScopeGuard([engine] { swmm_engine_destroy(engine); });
        for (int i = 0; i < 20000; ++i) {
            const auto id = QString("N%1").arg(i, 5, 10, QLatin1Char('0')).toUtf8();
            QCOMPARE(swmm_node_add(engine, id.constData(), SWMM_NODE_JUNCTION), SWMM_OK);
        }
        QCOMPARE(swmm_link_add(engine, "L", SWMM_LINK_CONDUIT), SWMM_OK);
        QCOMPARE(swmm_link_set_nodes(engine, 0, 0, 1), SWMM_OK);
        LinkEndpointComboBox picker;
        picker.setValue({engine, "L", true, "N00000"});
        QCOMPARE(picker.count(), 19999);
        picker.completer()->setCompletionPrefix("n1999");
        QCOMPARE(picker.completer()->completionCount(), 10);
        QCOMPARE(picker.completer()->currentCompletion(), QString("N19990"));
        QCOMPARE(picker.currentText(), QString("N00000")); // Searching alone never edits a link.
    }

    /** A viewport outside the old link bbox must draw and select the reconnected link immediately. */
    void linkEndpointGraphicsTrackEdits()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        layer->setCategoryVisible(SWMMModelLayer::CatConduits, false);
        layer->setShowNodes(false);
        layer->setShowSubcatchments(false);
        layer->setShowRainGages(false);
        layer->setShowLabels(false);
        layer->setQsgRenderKinds({});
        int link = -1;
        QVERIFY(layer->applyLinkAdd("LGraphics", SWMM_LINK_CONDUIT, "J1", "X1",
                                    {QPointF(500, 100)}, &link));
        SWMMLayerQSGRenderer renderer;
        renderer.setLayer(layer.get());
        const auto contentRevision = renderer.contentRevision();
        const auto geometryRevision = layer->geomRevision();
        const auto paint = [&] {
            QImage image(400, 80, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.scale(2, 2);
            painter.translate(-1400, 60);
            QStyleOptionGraphicsItem option;
            option.exposedRect = QRectF(1400, -60, 200, 40);
            SWMMLayerItem item(layer.get());
            item.paint(&painter, &option, nullptr);
            return image;
        };
        const auto before = paint();
        QVERIFY(!layer->linksInRect(1400, 20, 1600, 60).contains("LGraphics"));
        SWMMAttributeTableModel model;
        QUndoStack undo;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        model.setUndoStack(&undo);
        int row = -1;
        for (int r = 0; r < model.rowCount(); ++r)
            if (model.objectNameAt(r) == "LGraphics")
                row = r;
        QVERIFY(row >= 0);
        const auto cell = model.index(row, colFor(model.columnSpecs(), "To node"));
        QVERIFY(model.setData(cell, "O1"));
        QVERIFY(layer->geomRevision() > geometryRevision);
        QVERIFY(renderer.contentRevision() > contentRevision);
        QVERIFY(layer->linksInRect(1400, 20, 1600, 60).contains("LGraphics"));
        const auto after = paint();
        QVERIFY(after != before);
        undo.undo();
        QVERIFY(!layer->linksInRect(1400, 20, 1600, 60).contains("LGraphics"));
        QCOMPARE(paint(), before);
        undo.redo();
        QCOMPARE(paint(), after);
    }

#ifdef HAVE_QPROPERTYMODEL
    /** Exercise the actual Properties dock's editor registration and immediate selection commit. */
    void linkPropertyPickerCommitsSelection()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMConduitPropertyAdapter adapter(layer->engine(), "C1");
        adapter.setModelLayer(layer.get());
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showObject(&adapter);
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        const auto find = [&](auto &&self, const QModelIndex &parent) -> QModelIndex {
            for (int r = 0; r < tree->model()->rowCount(parent); ++r) {
                auto row = tree->model()->index(r, 0, parent);
                if (row.data().toString() == "From Node")
                    return row.siblingAtColumn(1);
                const auto child = self(self, row);
                if (child.isValid())
                    return child;
            }
            return {};
        };
        const auto cell = find(find, {});
        QVERIFY(cell.isValid());
        QVERIFY(cell.flags() & Qt::ItemIsEditable);
        panel.resize(500, 650);
        panel.show();
        tree->expandAll();
        tree->edit(cell);
        auto *combo = tree->findChild<LinkEndpointComboBox *>();
        QVERIFY(combo);
        QCOMPARE(combo->currentText(), QString("J1"));
        combo->setCurrentIndex(combo->findText("O1"));
        QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex())));
        QCOMPARE(adapter.fromNode(), QString("O1"));
        QCOMPARE(cell.data().toString(), QString("O1"));
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        QCOMPARE(adapter.fromNode(), QString("J1"));
        canvas.undoStack()->redo();
        QCOMPARE(adapter.fromNode(), QString("O1"));
        auto invalid = adapter.fromNodeRef();
        invalid.nodeName = "X1";
        adapter.setFromNodeRef(invalid);
        QCOMPARE(adapter.fromNode(), QString("O1"));
    }
#endif

    /**
     *
     * @brief Require station-id keys to remain ungrouped while statistic/coupling aliases share
     * their intended groups.
     * Pins naming classification without loading an engine or constructing a table.
     *
     */
    void groupingDoesNotMistakeStationForStatistics()
    {
        QVERIFY(openswmmvis::attributeGroup("stationId").isEmpty());
        QVERIFY(openswmmvis::attributeGroup("gage_station_id").isEmpty());
        QCOMPARE(openswmmvis::attributeGroup("statMaxDepth"),
                 openswmmvis::attributeGroup("node_stat_max_depth"));
        QCOMPARE(openswmmvis::attributeGroup("couplingType"),
                 openswmmvis::attributeGroup("node_2d_coupling_type"));
    }

#ifdef HAVE_QPROPERTYMODEL
    /**
     *
     * @brief Exercise grouped property-model mapping/edit/reset invariants with the optional
     * property library.
     * Loads a junction adapter, requires coupling/output subgroup structure, three flooding
     * rows and a read-only first flooding value. Edits/restores invert elevation, edits
     * coupling diameter text, refreshes/clears/rebinds and switches to an outfall schema.
     * A model tester checks Qt invariants. Local finder borrows exact display label and
     * optional parent index, returning first direct child or invalid; it is not recursive.
     * Only compiled with HAVE_QPROPERTYMODEL; no hydraulic run or project save.
     *
     */
    void propertyGroupsPreserveEditingAndReset()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const QString name = layer->objectNameAt(SWMMModelLayer::CatJunctions, 0);
        QVERIFY(!name.isEmpty());
        SWMMJunctionPropertyAdapter adapter(layer->engine(), name);
        adapter.setModelLayer(layer.get());
        QPropertyModel source(nullptr);
        GroupedPropertyModel grouped;
        grouped.setSourceModel(&source);
        source.setData(QVariant::fromValue<QObject *>(&adapter));
        QAbstractItemModelTester tester(&grouped,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto find = [&](const QString &label, const QModelIndex &parent = QModelIndex()) {
            for (int r = 0; r < grouped.rowCount(parent); ++r) {
                auto index = grouped.index(r, 0, parent);
                if (index.data().toString() == label)
                    return index;
            }
            return QModelIndex();
        };
        auto coupling = find("2D Coupling");
        auto output = find("Output Summary");
        QVERIFY(coupling.isValid());
        QVERIFY(output.isValid());
        QCOMPARE(output.row(), grouped.rowCount() - 1);
        QVERIFY(find("Setup", coupling).isValid());
        auto geometry = find("Geometry", coupling);
        QVERIFY(geometry.isValid());
        QVERIFY(find("Exchange", coupling).isValid());
        QVERIFY(find("Node Depth", output).isValid());
        auto flooding = find("Node Flooding", output);
        QCOMPARE(grouped.rowCount(flooding), 3);
        QVERIFY(!(grouped.flags(grouped.index(0, 1, flooding)) & Qt::ItemIsEditable));
        auto properties = find("Properties");
        auto invert = find(adapter.displayLabelFor("invertElev"), properties).siblingAtColumn(1);
        QVERIFY(invert.isValid());
        QCOMPARE(grouped.mapFromSource(grouped.mapToSource(invert)), invert);
        double previous = adapter.invertElev();
        QVERIFY(grouped.setData(invert, previous + 0.25));
        QCOMPARE(adapter.invertElev(), previous + 0.25);
        QVERIFY(grouped.setData(invert, previous));
        const auto diameter =
            find(adapter.displayLabelFor("couplingDiameter"), geometry).siblingAtColumn(1);
        QVERIFY(diameter.isValid());
        QVERIFY(grouped.setData(diameter, QStringLiteral("1.2")));
        QCOMPARE(adapter.couplingDiameter(), QStringLiteral("1.2"));
        source.refreshValues();
        QVERIFY(find("Geometry", find("2D Coupling")).isValid());
        source.clear();
        QCOMPARE(grouped.rowCount(), 0);
        source.setData(QVariant::fromValue<QObject *>(&adapter));
        QVERIFY(find("Output Summary").isValid());
        SWMMOutfallPropertyAdapter outfall(layer->engine(), QStringLiteral("O1"));
        source.setData(QVariant::fromValue<QObject *>(&outfall));
        const auto outfallCoupling = find("2D Coupling");
        QVERIFY(find("Outfall", outfallCoupling).isValid());
        QVERIFY(!find("Geometry", outfallCoupling).isValid());
        source.clear();
    }
#endif

    // =====================================================================
    // 1 — conduit cross-section block
    // =====================================================================

    /**
     *
     * @brief Verify derived column registration, read-only flags, value invalidation and cleanup.
     *
     */
    void computedExtensionColumnsStayReadOnlyAndRefresh()
    {
        struct Provider : QObject, OpenSWMM::Sdk::IComputedAttributeProvider {
            double factor = 2.;
            QVector<OpenSWMM::Render::AttributeField>
            fields(OpenSWMMVis::SwmmCategory category) const override
            {
                if (category != OpenSWMMVis::CatConduits)
                    return {};
                return {{"scaled", "Derived length", QMetaType::Double, false, "m"}};
            }
            QVariant value(OpenSWMMVis::SwmmCategory, const QString &, const QString &,
                           const QVariantMap &base) const override
            {
                return base.value("length").toDouble() * factor;
            }
        };
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        auto provider = std::make_unique<Provider>();
        const QString id = "ext.tests.columns";
        const auto changed = [source = layer.get()](bool schema) {
            emit source->extensionAttributesChanged(schema);
        };
        QString error;
        QVERIFY(openswmmvis::registerExtensionAttributes(layer.get(), id, provider.get(), provider.get(), changed, &error));
        const int column = colFor(model.columnSpecs(), id + ".scaled");
        QVERIFY(column >= 0);
        QVERIFY(model.rowCount() > 0);
        QVERIFY(!(model.flags(model.index(0, column)) & Qt::ItemIsEditable));
        const auto original = layer->identifyByName(model.objectNameAt(0), SWMMModelLayer::kKindLink).value("length").toDouble();
        QCOMPARE(model.data(model.index(0, column), Qt::EditRole).toDouble(), original * 2.);
        provider->factor = 3.;
        openswmmvis::refreshExtensionAttributes(layer.get(), id);
        QCOMPARE(model.data(model.index(0, column), Qt::EditRole).toDouble(), original * 3.);
        provider.reset();
        QCOMPARE(colFor(model.columnSpecs(), id + ".scaled"), -1);
    }

    /** @brief Require the conduit cross-section columns and their intended editor kinds. */
    void conduitCrossSectionBlockIsPresent()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();

        QVERIFY(colFor(specs, QStringLiteral("XSection")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Barrels")) >= 0);
        for (int k = 1; k <= 4; ++k)
            QVERIFY2(colFor(specs, QStringLiteral("Geom %1").arg(k)) >= 0,
                     qPrintable(QStringLiteral("Geom %1 column missing").arg(k)));

        QCOMPARE(specs[colFor(specs, QStringLiteral("XSection"))].editor, EditorKind::Compound);
        QCOMPARE(specs[colFor(specs, QStringLiteral("Barrels"))].editor, EditorKind::Integer);
        QCOMPARE(specs[colFor(specs, QStringLiteral("Geom 1"))].editor, EditorKind::Numeric);
    }

    /**
     *
     * @brief Require all four stored geom cells to remain visible-valued and editable on the
     * circular fixture conduit.
     * Also pins that circular geom two is not shape-consumed. Does not commit a value or
     * claim every shape/row combination is tested by this case.
     *
     */
    void geomCellsStayEditableOnCircular()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();
        QVERIFY(model.rowCount() > 0);

        // Row 0 of the fixture is a CIRCULAR conduit — only geom1 "applies".
        const int shapeCol = colFor(specs, QStringLiteral("Geom 1"));
        QVERIFY(shapeCol >= 0);
        QVERIFY(!openswmmvis::xsectGeomApplies(SWMM_XSECT_CIRCULAR, 2));

        for (int k = 1; k <= 4; ++k) {
            const int c = colFor(specs, QStringLiteral("Geom %1").arg(k));
            const QModelIndex idx = model.index(0, c);
            QVERIFY2(model.flags(idx) & Qt::ItemIsEditable,
                     qPrintable(QStringLiteral("Geom %1 not editable on CIRCULAR").arg(k)));
            QVERIFY2(model.data(idx, Qt::DisplayRole).isValid(),
                     qPrintable(QStringLiteral("Geom %1 blank on CIRCULAR").arg(k)));
        }
    }

    /**
     *
     * @brief Commit an otherwise unused circular geom-two value and require raw engine/readback
     * retention.
     * Writes 2.75 in authored project units, resolves the conduit id/index and checks raw g2
     * and unchanged circular shape. Does not initialize topology or save/reopen an input file.
     *
     */
    void geomEditRoundTripsThroughEngine()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Geom 2"));
        QVERIFY(c >= 0);

        const QModelIndex idx = model.index(0, c);
        QVERIFY(model.setData(idx, 2.75, Qt::EditRole));

        const QString name = model.objectNameAt(0);
        const int li = swmm_link_index(layer->engine(), name.toUtf8().constData());
        QVERIFY(li >= 0);
        int shape = 0;
        double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
        QCOMPARE(swmm_link_get_xsect(layer->engine(), li, &shape, &g1, &g2, &g3, &g4), SWMM_OK);
        QCOMPARE(g2, 2.75);
        // The shape itself must not have moved.
        QCOMPARE(shape, SWMM_XSECT_CIRCULAR);
    }

    /**
     *
     * @brief Require an irregular section's transect-index geom one to stay locked while geoms
     * two through four remain editable.
     * Creates/configures a three-station transect only when the fixture has none, assigns
     * index zero, reloads and checks flags. Does not exercise CUSTOM/STREET picker indices
     * or validate hydraulic results of that temporary editing configuration.
     *
     */
    void pickerIndexGeomStaysLocked()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();

        const QString name = model.objectNameAt(0);
        const int li = swmm_link_index(layer->engine(), name.toUtf8().constData());
        QVERIFY(li >= 0);
        // set_xsect(IRREGULAR, index) now validates and binds the transect
        // (previously it stored the index as a depth and left the reference
        // dangling — the .inp save corruption family), so index 0 must name a
        // real transect in the fixture.
        SWMM_Engine eng = layer->engine();
        if (swmm_transect_count(eng) == 0) {
            QCOMPARE(swmm_transect_add(eng, "T_SCHEMA"), SWMM_OK);
            const int ti = swmm_transect_index(eng, "T_SCHEMA");
            QCOMPARE(swmm_transect_set_roughness(eng, ti, 0.04, 0.04, 0.03), SWMM_OK);
            QCOMPARE(swmm_transect_add_station(eng, ti, 0.0, 10.0), SWMM_OK);
            QCOMPARE(swmm_transect_add_station(eng, ti, 5.0, 5.0), SWMM_OK);
            QCOMPARE(swmm_transect_add_station(eng, ti, 10.0, 10.0), SWMM_OK);
        }
        QCOMPARE(swmm_link_set_xsect(layer->engine(), li, SWMM_XSECT_IRREGULAR, 0, 0, 0, 0),
                 SWMM_OK);
        model.reload();

        const int g1 = colFor(specs, QStringLiteral("Geom 1"));
        QVERIFY(!(model.flags(model.index(0, g1)) & Qt::ItemIsEditable));
        for (int k = 2; k <= 4; ++k) {
            const int c = colFor(specs, QStringLiteral("Geom %1").arg(k));
            QVERIFY2(model.flags(model.index(0, c)) & Qt::ItemIsEditable,
                     qPrintable(QStringLiteral("Geom %1 locked on IRREGULAR").arg(k)));
        }
    }

    // =====================================================================
    // 2 — dynamics columns are a contiguous suffix
    // =====================================================================

    /**
     *
     * @brief Require top-level attribute group names never to reappear after another group within
     * each spatial schema.
     * Uses the prefix before group-path slash and treats empty as Model Attributes;
     * checks schema ordering across allCategories(), without numerical result assertions.
     *
     */
    void attributeGroupsAreContiguous()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        for (const auto category : allCategories()) {
            SWMMAttributeTableModel model;
            model.setSource(layer.get(), category);
            QSet<QString> completed;
            QString previous;
            for (int column = 0; column < model.columnCount(); ++column) {
                QString group =
                    model.headerData(column, Qt::Horizontal, openswmmvis::AttributeGroupRole)
                        .toString()
                        .section(" / ", 0, 0);
                if (group.isEmpty())
                    group = QStringLiteral("Model Attributes");
                if (group != previous) {
                    QVERIFY2(!completed.contains(group),
                             qPrintable(QString("Group %1 reappears in category %2 at column %3")
                                            .arg(group)
                                            .arg(int(category))
                                            .arg(column)));
                    completed.insert(group);
                    previous = group;
                }
            }
        }
    }

    /**
     *
     * @brief Require statistic-tagged columns to form a read-only suffix in every spatial schema.
     * After the first _stat_ tag rejects later inputs, checks ReadOnly editor/group path,
     * keeps Output Summary out of exported header text and checks noneditable row-zero
     * flags when rows exist. Does not require initialized statistics or nonempty categories.
     *
     */
    void dynamicsColumnsAreAppendedLast()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        for (const auto cat : allCategories()) {
            SWMMAttributeTableModel model;
            model.setSource(layer.get(), cat);
            const auto specs = model.columnSpecs();

            int firstDynamic = -1;
            for (int i = 0; i < specs.size(); ++i) {
                if (isDynamics(specs[i])) {
                    if (firstDynamic < 0)
                        firstDynamic = i;
                } else if (firstDynamic >= 0) {
                    QFAIL(qPrintable(QStringLiteral("category %1: input column '%2' at %3 sits "
                                                    "AFTER the dynamics block (starts at %4)")
                                         .arg(int(cat))
                                         .arg(specs[i].key)
                                         .arg(i)
                                         .arg(firstDynamic)));
                }
            }
            // Whatever is in the block must be read-only — a statistic is not
            // an input, and commitValueDirect() refuses ReadOnly writes.
            for (int i = std::max(firstDynamic, 0); i < specs.size(); ++i) {
                if (!isDynamics(specs[i]))
                    continue;
                QCOMPARE(specs[i].editor, EditorKind::ReadOnly);
                QVERIFY(model.headerData(i, Qt::Horizontal, openswmmvis::AttributeGroupRole)
                            .toString()
                            .startsWith("Output Summary / "));
                // Group headings must not become part of exported column names.
                QVERIFY(!model.headerData(i, Qt::Horizontal).toString().contains("Output Summary"));
                if (model.rowCount() > 0)
                    QVERIFY(!(model.flags(model.index(0, i)) & Qt::ItemIsEditable));
            }
        }
    }

    /**
     *
     * @brief Require at least one statistic-tagged column in each non-gage spatial schema and
     * none for rain gages.
     * Pins column presence only, without running a simulation or checking statistic values.
     *
     */
    void everyRoutedCategoryHasADynamicsBlock()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        // Rain gages are the one category the engine keeps no statistics for.
        for (const auto cat : allCategories()) {
            if (cat == SWMMModelLayer::CatRainGages)
                continue;
            SWMMAttributeTableModel model;
            model.setSource(layer.get(), cat);
            const auto specs = model.columnSpecs();
            int n = 0;
            for (const auto &s : specs)
                if (isDynamics(s))
                    ++n;
            QVERIFY2(n > 0, qPrintable(QStringLiteral("category %1 has no dynamics "
                                                      "columns")
                                           .arg(int(cat))));
        }
        SWMMAttributeTableModel gages;
        gages.setSource(layer.get(), SWMMModelLayer::CatRainGages);
        for (const auto &s : gages.columnSpecs())
            QVERIFY(!isDynamics(s));
    }

    /**
     *
     * @brief Require pump schema to expose shared maximum flow and pump cycle/time/volume
     * statistic keys.
     * Checks metadata presence even if the fixture has no pump rows; does not run a pump.
     *
     */
    void pumpsCarryTheirUtilisationStatistics()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatPumps);
        const auto specs = model.columnSpecs();
        // Shared link block plus the three pump-only ones.
        QVERIFY(colFor(specs, QStringLiteral("Max flow (stat)")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Pump cycles")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Pump on time (hr)")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Pump volume")) >= 0);
    }

    // =====================================================================
    // 3 — attribute coverage the engine API offers
    // =====================================================================

    /**
     *
     * @brief Require the conduit Slope column to be read-only and produce a positive numeric
     * row-zero value.
     * Uses the fixture's native authored slope convention; does not assert an exact numeric
     * value, recompute geometry or test a post-run statistic.
     *
     */
    void conduitExposesComputedSlope()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Slope"));
        QVERIFY2(c >= 0, "conduit Slope column missing");
        QCOMPARE(specs[c].editor, EditorKind::ReadOnly);

        // Reads through the getter-only tag, not identifyByName.
        const QModelIndex idx = model.index(0, c);
        QVERIFY(!(model.flags(idx) & Qt::ItemIsEditable));
        bool ok = false;
        const double slope = model.data(idx, Qt::DisplayRole).toDouble(&ok);
        QVERIFY(ok);
        QVERIFY(slope > 0.0); // fixture drops 5 ft over 1000 ft
    }

    /**
     *
     * @brief Assign/clear a subcatchment snow-pack ref across table, property adapter and modal
     * picker.
     * Adds Snow1, requires a Snowpack DataObjectRef, selects Use Snow Pack through a queued
     * modal callback and verifies clearing via the adapter refreshes the table ref.
     * Writes a reviewed screenshot to surface_r1_out/snowpack-assignment.png under dataDir(),
     * checking directory creation/image save. Callback borrows local state only while modal
     * exec runs. No snow simulation or input-file persistence is tested.
     *
     */
    void snowPackPickerSharesAssignmentWithProperties()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QCOMPARE(swmm_snowpack_add(eng, "Snow1"), SWMM_OK);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatSubcatchments);
        const int c = colFor(model.columnSpecs(), QStringLiteral("Snow pack"));
        QVERIFY(c >= 0);
        QVERIFY(model.rowCount() > 0);
        const auto idx = model.index(0, c);
        auto ref = model.data(idx, Qt::EditRole).value<DataObjectRef>();
        QCOMPARE(ref.kind, DataObjectRef::Snowpack);
        ref.currentName = "Snow1";
        QVERIFY(model.setData(idx, QVariant::fromValue(ref), Qt::EditRole));
        const auto sub = layer->objectNameAt(SWMMModelLayer::CatSubcatchments, 0);
        SWMMSubcatchPropertyAdapter adapter(eng, sub);
        QCOMPARE(adapter.snowPackRef().currentName, QStringLiteral("Snow1"));
        auto *registry = qobject_cast<openswmmvis::snowpack::SnowpackRegistry *>(
            layer->ensureSnowpackRegistry());
        QVERIFY(registry);
        bool selected = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<openswmmvis::ui::SnowpackEditorDialog *>(
                QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!buttons) {
                dialog->reject();
                return;
            }
            auto *use = buttons->button(QDialogButtonBox::Close);
            selected = use && use->text() == QStringLiteral("Use Snow Pack");
            if (use)
                QTest::mouseClick(use, Qt::LeftButton);
            else
                dialog->reject();
        });
        QCOMPARE(
            openswmmvis::ui::SnowpackEditorDialog::pickSnowpack(registry, layer.get(), "Snow1"),
            QStringLiteral("Snow1"));
        QVERIFY(selected);
        registerDataObjectRefConverter();
        QTableView view;
        view.setModel(&model);
        for (int k = 0; k < model.columnCount(); ++k)
            view.setColumnHidden(k, k != c && model.columnSpecs()[k].key != QStringLiteral("Name"));
        view.resize(640, 240);
        view.setWindowTitle(QStringLiteral("Subcatchment snow-pack assignment"));
        view.show();
        view.resizeColumnsToContents();
        QTest::qWait(30);
        const QString out = QDir(dataDir()).filePath(QStringLiteral("surface_r1_out"));
        QVERIFY(QDir().mkpath(out));
        QVERIFY(view.grab().save(QDir(out).filePath(QStringLiteral("snowpack-assignment.png"))));
        ref.currentName.clear();
        adapter.setSnowPackRef(ref);
        model.refreshObject(sub);
        QVERIFY(model.data(idx, Qt::EditRole).value<DataObjectRef>().currentName.isEmpty());
    }

    /**
     *
     * @brief Require editable subcatchment Tag schema and round-trip basin-7 through the model.
     * Checks row-zero display after setData, without save/reopen or undo coverage.
     *
     */
    void subcatchmentExposesTag()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatSubcatchments);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Tag"));
        QVERIFY2(c >= 0, "subcatchment Tag column missing");
        QVERIFY(model.rowCount() > 0);

        const QModelIndex idx = model.index(0, c);
        QVERIFY(model.flags(idx) & Qt::ItemIsEditable);
        QVERIFY(model.setData(idx, QStringLiteral("basin-7"), Qt::EditRole));
        QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), QStringLiteral("basin-7"));
    }

    /**
     *
     * @brief Require storage schema keys for exfiltration suction, conductivity and deficit.
     * Metadata presence is the assertion; no storage row values, editability or seepage run.
     *
     */
    void storageExposesExfiltrationParameters()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatStorage);
        const auto specs = model.columnSpecs();
        for (const char *key : {"Exfil suction", "Exfil conduct.", "Exfil deficit"})
            QVERIFY2(
                colFor(specs, QString::fromLatin1(key)) >= 0,
                qPrintable(
                    QStringLiteral("storage %1 column missing").arg(QString::fromLatin1(key))));
    }

    /**
     *
     * @brief Require the outfall Route to column and its Compound editor kind.
     * Does not commit a destination or verify routing/run behavior.
     *
     */
    void outfallExposesRouteTo()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatOutfalls);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Route to"));
        QVERIFY2(c >= 0, "outfall Route To column missing");
        QCOMPARE(specs[c].editor, EditorKind::Compound);
    }

    // =====================================================================
    // Panel-level: the delegates actually reach the view
    // =====================================================================

    /**
     *
     * @brief Require real grouped-header label clicks to sort names and numeric elevations in
     * both directions.
     * Sets J1/X1 elevations to 2/10 so lexicographic numeric order would fail, clicks Name
     * and Invert columns twice over two source-switch passes, and checks row identities/
     * sort indicators. Also saves a nonclickable header state to exercise restoration repair.
     * Can persist column layouts through panel destruction; no project data-file save.
     *
     */
    void columnHeaderClicksSortRows()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const auto engine = layer->engine();
        // Deliberately use values whose numeric and text orders differ.
        QCOMPARE(swmm_node_set_invert_elev(engine, swmm_node_index(engine, "J1"), 2.0), SWMM_OK);
        QCOMPARE(swmm_node_set_invert_elev(engine, swmm_node_index(engine, "X1"), 10.0), SWMM_OK);
        AttributeTablePanel panel;
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        QVERIFY(view->horizontalHeader()->sectionsClickable());
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.resize(1000, 600);
        panel.show();

        for (int pass = 0; pass < 2; ++pass) {
            panel.showModelCategory(layer.get(), SWMMModelLayer::CatJunctions);
            QCoreApplication::processEvents();
            SWMMAttributeTableModel schema;
            schema.setSource(layer.get(), SWMMModelLayer::CatJunctions);
            const int numericColumn = colFor(schema.columnSpecs(), QStringLiteral("Invert elev"));
            QVERIFY(numericColumn >= 0);
            QCOMPARE(view->model()->rowCount(), 2);
            auto *header = view->horizontalHeader();
            for (const int column : {0, numericColumn}) {
                view->sortByColumn(column, Qt::DescendingOrder);
                const QPoint click(header->sectionViewportPosition(column) +
                                       header->sectionSize(column) / 2,
                                   header->height() - 5); // ordinary label below the group band
                for (const auto order : {Qt::AscendingOrder, Qt::DescendingOrder}) {
                    QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, click);
                    QCOMPARE(header->sortIndicatorSection(), column);
                    QCOMPARE(header->sortIndicatorOrder(), order);
                    QCOMPARE(view->model()->index(0, 0).data().toString(),
                             order == Qt::AscendingOrder ? QStringLiteral("J1")
                                                         : QStringLiteral("X1"));
                    QCOMPARE(view->model()->index(1, 0).data().toString(),
                             order == Qt::AscendingOrder ? QStringLiteral("X1")
                                                         : QStringLiteral("J1"));
                }
            }
            // Reproduce header state saved by the broken grouped header.
            // Restoring column widths must not restore disabled sorting clicks.
            header->setSectionsClickable(false);
            panel.showModelCategory(layer.get(), SWMMModelLayer::CatRainGages);
        }
    }

    /**
     *
     * @brief Exercise wide/narrow category switches repeatedly, requiring live editors and a
     * renderable panel.
     * Across three passes and six category visits, flushes deferred deletes, requires each
     * cell to resolve a delegate, bounds direct-child delegate count by current width and
     * grabs a non-null widget image. Does not save that image or prove general leak freedom.
     *
     */
    void categorySwitchesRetainLiveDelegates()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        panel.resize(1000, 600);
        panel.show();
        // Narrower schemas used to leave high-numbered column bindings behind
        // when their delegates were deleted. Returning to a wider table then
        // crashed in Qt's drawCell() on a null delegate.
        const QList<SWMMModelLayer::Category> categories = {
            SWMMModelLayer::CatConduits,      SWMMModelLayer::CatRainGages,
            SWMMModelLayer::CatJunctions,     SWMMModelLayer::CatOutfalls,
            SWMMModelLayer::CatSubcatchments, SWMMModelLayer::CatConduits};
        for (int pass = 0; pass < 3; ++pass) {
            for (const auto category : categories) {
                panel.showModelCategory(layer.get(), category);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                QVERIFY(view->model()->rowCount() > 0);
                for (int column = 0; column < view->model()->columnCount(); ++column) {
                    QVERIFY2(view->itemDelegateForIndex(view->model()->index(0, column)),
                             qPrintable(QString("Missing delegate in category %1, column %2")
                                            .arg(int(category))
                                            .arg(column)));
                }
                // Retired editors must be released as well as detached.
                QVERIFY(
                    panel.findChildren<QStyledItemDelegate *>(QString(), Qt::FindDirectChildrenOnly)
                        .size() <= view->model()->columnCount());
                QCoreApplication::processEvents();
                QVERIFY(!panel.grab().isNull());
            }
        }
    }

    /**
     *
     * @brief Require a real conduit table to show its compound cross-section and all geom
     * delegates.
     * Checks visible columns, a compound Edit affordance and non-null per-geom delegates.
     * Local header finder borrows an exact display label and returns first logical column
     * or minus one. Does not open the compound editor or commit geometry values.
     *
     */
    void panelInstallsCrossSectionDelegates()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        int catIdx = -1;
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QStringLiteral("Conduits")))
                catIdx = i;
        QVERIFY(catIdx >= 0);
        combo->setCurrentIndex(catIdx);

        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        auto *m = view->model();
        QVERIFY(m);

        auto headerCol = [&](const QString &label) {
            for (int c = 0; c < m->columnCount(); ++c)
                if (m->headerData(c, Qt::Horizontal).toString() == label)
                    return c;
            return -1;
        };

        const int xs = headerCol(QStringLiteral("Cross Section"));
        QVERIFY(xs >= 0);
        QVERIFY(!view->isColumnHidden(xs));
        auto *del = qobject_cast<QStyledItemDelegate *>(view->itemDelegateForColumn(xs));
        QVERIFY2(del, "no delegate on the Cross Section column");
        // The compound cell renders a summary plus the "Edit…" affordance.
        const QString shown = del->displayText(m->index(0, xs).data(Qt::EditRole), QLocale());
        QVERIFY2(shown.contains(QStringLiteral("Edit")),
                 qPrintable(QStringLiteral("Cross Section cell shows '%1'").arg(shown)));

        for (int k = 1; k <= 4; ++k) {
            const int c = headerCol(QStringLiteral("Geom %1").arg(k));
            QVERIFY(c >= 0);
            QVERIFY(!view->isColumnHidden(c));
            QVERIFY(view->itemDelegateForColumn(c) != nullptr);
        }
    }

    // =====================================================================
    // Panel-level: the query bar filters on the columns it names
    // =====================================================================
    //
    // The filter proxy used to materialise EVERY column of every row (twice)
    // to test a predicate that reads one. It now resolves the predicate's
    // field names to columns once and reads only those. That is only a
    // performance change if the accepted-row set is unchanged, so these pin
    // the semantics that made the old build "work": a field is matchable by
    // its ColumnSpec key, by its label, or by the unit-suffixed header the
    // view displays.

    /**
     *
     * @brief Require schema key, label and unit-suffixed header aliases to select identical row
     * sets.
     * Applies plain-decimal predicates true for all junctions, then inverted predicates false
     * for all, proving the field is actually evaluated. Local match counter borrows field
     * and operator strings, sends Return and returns visible row count. Does not benchmark
     * column-read counts or prove behavior for arbitrary unsupported field names.
     *
     */
    void queryAcceptsKeyLabelAndHeaderSpellings()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        int catIdx = -1;
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QStringLiteral("Junctions")))
                catIdx = i;
        QVERIFY(catIdx >= 0);
        combo->setCurrentIndex(catIdx);

        auto *view = panel.findChild<QTableView *>();
        auto *edit = panel.findChild<QLineEdit *>();
        QVERIFY(view && edit);
        auto *m = view->model();
        QVERIFY(m);
        const int unfiltered = m->rowCount();
        QVERIFY2(unfiltered > 0, "fixture has no junctions");

        // "Invert elev" is the ColumnSpec key; "Invert Elev" the label; the
        // header adds a unit suffix. All three must select the same rows.
        SWMMAttributeTableModel probe;
        probe.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        const auto specs = probe.columnSpecs();
        const int col = colFor(specs, QStringLiteral("Invert elev"));
        QVERIFY2(col >= 0, "fixture schema lost the Invert elev column");

        QString header;
        for (int c = 0; c < m->columnCount(); ++c)
            if (m->headerData(c, Qt::Horizontal).toString().startsWith(specs[col].label))
                header = m->headerData(c, Qt::Horizontal).toString();
        QVERIFY(!header.isEmpty());

        // Use bounded plain decimals for both alias predicates. A parse error
        // would make Apply return early WITHOUT touching the
        // filter, which would leave every assertion below reading the
        // previous query's result instead of this one's.
        auto matchCount = [&](const QString &field, const char *op) {
            edit->setText(QStringLiteral("[%1] %2 -99999").arg(field, op));
            QTest::keyClick(edit, Qt::Key_Return);
            return view->model()->rowCount();
        };

        // A predicate true for every row keeps every row...
        QCOMPARE(matchCount(specs[col].key, ">"), unfiltered);
        QCOMPARE(matchCount(specs[col].label, ">"), unfiltered);
        QCOMPARE(matchCount(header, ">"), unfiltered);

        // ...and the same predicate inverted drops them all, which proves
        // the column is actually being READ rather than silently skipped
        // (a skipped column would leave the row count at `unfiltered` in
        // both directions).
        QCOMPARE(matchCount(specs[col].key, "<"), 0);
        QCOMPARE(matchCount(specs[col].label, "<"), 0);
        QCOMPARE(matchCount(header, "<"), 0);
    }

    /**
     *
     * @brief Require unknown and every nonqueryable schema field to report errors without
     * replacing a valid one-row filter.
     * Escapes the chosen object's quote in its valid query, checks error styling and row
     * count after each refused draft. Does not inspect the selection bus or persist queries.
     *
     */
    void queryOnUnresolvableFieldPreservesPreviousFilter()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.refresh();

        auto *view = panel.findChild<QTableView *>();
        auto *edit = panel.findChild<QLineEdit *>("attributeQuery");
        auto *model = panel.findChild<SWMMAttributeTableModel *>();
        QVERIFY(view && edit && model);
        QString name = view->model()->index(0, 0).data().toString();
        name.replace('\'', "''");
        edit->setText(QString("Name = '%1'").arg(name));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(view->model()->rowCount(), 1);
        QVERIFY(edit->styleSheet().isEmpty());

        edit->setText(QStringLiteral("NOT [No Such Column] = 1"));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(view->model()->rowCount(), 1);
        QVERIFY(!edit->styleSheet().isEmpty());
        for (const auto &spec : model->columnSpecs()) {
            if (spec.queryable)
                continue;
            edit->setText(QString("[%1] = 1").arg(spec.key));
            QTest::keyClick(edit, Qt::Key_Return);
            QCOMPARE(view->model()->rowCount(), 1);
            QVERIFY(!edit->styleSheet().isEmpty());
        }
    }

    // =====================================================================
    // 5 — compound-cell summaries follow the engine
    // =====================================================================
    //
    // Compound cells (External Inflows, Dry Weather Flow, RDII, Treatment)
    // are served from m_inflowCountByNode & friends, filled once by
    // ensureCompoundCacheBuilt(). That cache was only ever dropped by
    // setSource() / reload() — NOT by refreshObject(), and not by the
    // Compound branch of commitValueDirect(), whose own comment claims it
    // "invalidates the row cache so the summary recomputes on the next
    // paint". It invalidated m_rowCache, which is not where the summary
    // comes from. So adding an inflow through the table's own compound
    // dialog left the cell reading "(none)" until a category switch.

    /**
     *
     * @brief Require a previously cached inflow compound summary to change after a direct engine
     * edit and refreshObject().
     * Builds the cache first, adds a FLOW external inflow with baseline 2.5 through the API,
     * refreshes the named junction and compares summary strings. Does not invoke the modal
     * compound editor, check a particular summary spelling or run hydraulics.
     *
     */
    void compoundSummaryRefreshesAfterEngineEdit()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        const auto specs = model.columnSpecs();
        const int col = colFor(specs, QStringLiteral("Inflows"));
        QVERIFY2(col >= 0, "junction schema lost the Inflows compound column");
        QVERIFY(model.rowCount() > 0);

        const QModelIndex idx = model.index(0, col);
        const QString name = model.objectNameAt(0);

        // Touch the cell first so the compound cache is genuinely built —
        // otherwise a later first-build would mask a missing invalidation.
        const QVariant before = model.data(idx, Qt::DisplayRole);
        QVERIFY(before.isValid());
        const QString summaryBefore = before.value<NodeCompoundEditRef>().summary;

        // Add an inflow behind the model's back, exactly as the compound
        // dialog does (it writes to the engine, then the table is told to
        // refresh the object).
        SWMM_Engine eng = layer->engine();
        QVERIFY(eng);
        const int ni = swmm_node_index(eng, name.toUtf8().constData());
        QVERIFY(ni >= 0);
        QCOMPARE(swmm_ext_inflow_add(eng, ni, "FLOW", "", "FLOW", 1.0, 1.0, 2.5, ""), SWMM_OK);

        model.refreshObject(name);

        const QString summaryAfter =
            model.data(idx, Qt::DisplayRole).value<NodeCompoundEditRef>().summary;
        QVERIFY2(summaryAfter != summaryBefore,
                 qPrintable(QStringLiteral("compound summary stayed '%1' after an engine edit + "
                                           "refreshObject — the compound cache was not invalidated")
                                .arg(summaryAfter)));
    }

    // =====================================================================
    // 4 — SoA index == engine index
    // =====================================================================
    //
    // SWMMAttributeTableModel::data() resolves a row's engine index with
    // SWMMModelLayer::soaIndexAt() instead of re-deriving it from the
    // object's name. That is only correct while the layer's SoA index and
    // the engine's index are the same number. The invariant is already
    // load-bearing elsewhere (applyNodeDelete feeds a swmm_node_index()
    // result straight into m_nodes.removeAt()), but nothing pinned it —
    // and a violation would be silent: the cell would show a NEIGHBOURING
    // object's value rather than going blank.

    /**
     *
     * @brief Require every loaded category row's displayed name to match its SoA/native engine
     * index identity.
     * Iterates allCategories() and all extant rows; empty categories contribute no row checks.
     * Does not mutate order or initialize/run a model.
     *
     */
    void soaIndexMatchesEngineIndex()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QVERIFY(eng);

        for (auto cat : allCategories()) {
            const int n = layer->categoryCount(cat);
            for (int row = 0; row < n; ++row) {
                const QString name = layer->objectNameAt(cat, row);
                const int idx = layer->soaIndexAt(cat, row);
                QVERIFY2(
                    idx >= 0,
                    qPrintable(QStringLiteral("cat %1 row %2: no index").arg(int(cat)).arg(row)));
                QCOMPARE(QString::fromUtf8(engineIdFor(eng, cat, idx)), name);
            }
        }
    }

    /**
     *
     * @brief Require native index identity mapping to follow a real reversed junction display
     * permutation.
     * Builds an explicit order if needed, reverses it and first proves the visible first name
     * changed, then checks every row's name against engineIdFor(). Does not save the override.
     *
     */
    void soaIndexHonoursOrderingOverride()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QVERIFY(eng);

        const auto cat = SWMMModelLayer::CatJunctions;
        const int n = layer->categoryCount(cat);
        QVERIFY2(n >= 2, "fixture needs >= 2 junctions to permute");

        // Reverse the default order — a permutation, so setObjectOrder
        // accepts it.
        QVector<int> order = layer->objectOrder(cat);
        if (order.isEmpty()) {
            order.reserve(n);
            for (int r = 0; r < n; ++r)
                order << layer->soaIndexAt(cat, r);
        }
        const QString firstBefore = layer->objectNameAt(cat, 0);
        std::reverse(order.begin(), order.end());
        layer->setObjectOrder(cat, order);

        // setObjectOrder rejects a non-permutation by silently returning,
        // which would leave this test asserting the DEFAULT path twice and
        // pin nothing. Prove the override actually took effect first.
        QVERIFY2(layer->objectNameAt(cat, 0) != firstBefore,
                 "setObjectOrder did not reorder — the override path below "
                 "would be untested");

        for (int row = 0; row < n; ++row) {
            const QString name = layer->objectNameAt(cat, row);
            const int idx = layer->soaIndexAt(cat, row);
            QVERIFY(idx >= 0);
            QCOMPARE(QString::fromUtf8(engineIdFor(eng, cat, idx)), name);
        }
    }

    // =====================================================================
    // 5 — [INITIAL_QUALITY] override columns (initial-quality UI round)
    // =====================================================================

    /**
     *
     * @brief Require initial-quality columns on node/link schemas and age gating after a schema
     * rebuild.
     * Adds TSS, checks editable-text/schema setter metadata for nodes/conduits, absence on
     * subcatchment/gage schemas and no reserved age key until WATER_AGE YES. Does not run
     * transport or commit constituent values in this case.
     *
     */
    void initialQualityColumnsFollowConstituentsAndScope()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QCOMPARE(swmm_pollutant_add(eng, "TSS", 0 /*MG/L*/), SWMM_OK);

        SWMMAttributeTableModel model;
        const QString tssKey = QStringLiteral("initq:TSS");
        const QString ageKey = QStringLiteral("initq:__WATER_AGE__");

        // Node and link categories carry one editable Text column per
        // pollutant; the reserved age column stays hidden while the
        // option is off.
        for (auto cat : {SWMMModelLayer::CatJunctions, SWMMModelLayer::CatConduits}) {
            model.setSource(layer.get(), cat);
            const auto specs = model.columnSpecs();
            const int c = colFor(specs, tssKey);
            QVERIFY2(c >= 0, "initq:TSS column missing");
            QCOMPARE(specs[c].editor, EditorKind::Text);
            QVERIFY(!specs[c].setter.isEmpty());
            QCOMPARE(colFor(specs, ageKey), -1);
        }

        // Non-node/link categories get no initial-quality columns —
        // [INITIAL_QUALITY] scopes are NODE | LINK only.
        model.setSource(layer.get(), SWMMModelLayer::CatSubcatchments);
        QCOMPARE(colFor(model.columnSpecs(), tssKey), -1);
        model.setSource(layer.get(), SWMMModelLayer::CatRainGages);
        QCOMPARE(colFor(model.columnSpecs(), tssKey), -1);

        // Turning WATER_AGE on surfaces the reserved age column on the
        // next schema rebuild.
        QCOMPARE(swmm_options_set(eng, "WATER_AGE", "YES"), SWMM_OK);
        model.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        QVERIFY(colFor(model.columnSpecs(), ageKey) >= 0);
    }

    /**
     *
     * @brief Require a junction TSS override to upsert/read/clear through the engine and table.
     * Checks blank unset display, exact row identity/TSS/value12.5, EditRole text and count
     * retention after garbage/negative refusals. Clearing removes the override and a second
     * clear returns false. Does not compare post-refusal value separately or save/reopen.
     *
     */
    void initialQualityCellRoundTripsThroughEngine()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QCOMPARE(swmm_pollutant_add(eng, "TSS", 0 /*MG/L*/), SWMM_OK);

        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        const int col = colFor(model.columnSpecs(), QStringLiteral("initq:TSS"));
        QVERIFY(col >= 0);
        const int row = model.rowForName(QStringLiteral("J1"));
        QVERIFY(row >= 0);
        const QModelIndex idx = model.index(row, col);
        QVERIFY(model.flags(idx) & Qt::ItemIsEditable);

        // Unset reads blank — the global initial concentration applies.
        QVERIFY(!model.data(idx, Qt::DisplayRole).isValid());

        // Numeric commit upserts the element's [INITIAL_QUALITY] row.
        QVERIFY(model.commitValueDirect(idx, QStringLiteral("12.5")));
        QCOMPARE(swmm_init_quality_count(eng), 1);
        int is_link = 0, elem = -1;
        char cons[64];
        double v = 0.0;
        QCOMPARE(swmm_init_quality_get(eng, 0, &is_link, &elem, cons, sizeof(cons), &v), SWMM_OK);
        QCOMPARE(is_link, 0);
        QCOMPARE(elem, swmm_node_index(eng, "J1"));
        QCOMPARE(QString::fromUtf8(cons), QStringLiteral("TSS"));
        QCOMPARE(v, 12.5);
        QCOMPARE(model.data(idx, Qt::EditRole).toString(), QStringLiteral("12.5"));

        // Garbage and negative pollutant commits are refused and leave
        // the store untouched (the engine setter validates the sign).
        QVERIFY(!model.commitValueDirect(idx, QStringLiteral("abc")));
        QVERIFY(!model.commitValueDirect(idx, QStringLiteral("-5")));
        QCOMPARE(swmm_init_quality_count(eng), 1);

        // Blank commit clears the override; a second blank is a no-op.
        QVERIFY(model.commitValueDirect(idx, QString()));
        QCOMPARE(swmm_init_quality_count(eng), 0);
        QVERIFY(!model.data(idx, Qt::DisplayRole).isValid());
        QVERIFY(!model.commitValueDirect(idx, QString()));
    }
};

QTEST_MAIN(TestAttributeTableSchema)
#include "test_attributetableschema.moc"
