/**
 * @file      test_linkendpointproperties.cpp
 * @brief     Link endpoint edits made from the Properties dock, with the
 *            main window's companion views attached.
 *
 * Reproduces the application wiring rather than a bare panel: a selection
 * on the SelectionManager routes to PropertiesPanel::showIdentifyResults
 * (SWMMVis::showPropertiesSelection), the Attribute Table and Object
 * Browser follow the same manager and the layer's geometryChanged, the
 * Section View redraws on objectEdited, both panels mark the layer edited,
 * and the QSG renderer rebuilds its link buffers. Every one of those runs
 * synchronously inside the LinkEndpointComboBox commit, so this is where a
 * use-after-free or a stale index during a To Node change would surface.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/tools/maptoolidentify.h" // IdentifyResult
#include "selection/selectionmanager.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/sectionviewpanel.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/properties/dataobjectpickereditor.h"
#include "ui/properties/linkendpointcombobox.h"
#include "ui/properties/swmmnodepropertyadapter.h"

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QLineEdit>
#include <QPointer>
#include <QSGNode>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QTreeView>

#include <memory>

namespace
{

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

class ProbeRenderer : public SWMMLayerQSGRenderer
{
public:
    using SWMMLayerQSGRenderer::updatePaintNode;
};

/**
 * Exposes the receiver count: a connection that outlives its adapter reads freed
 * memory, which only crashes once the block has been reused. The count fails
 * every time.
 */
class ProbeLayer : public SWMMModelLayer
{
public:
    using SWMMModelLayer::SWMMModelLayer;
    int attributeChangedReceivers() const { return receivers(SIGNAL(attributeChanged(QString))); }
};

QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

/** Value cell of the property row whose label is @p label (searched recursively). */
QModelIndex propertyCellIn(QTreeView *tree, const QString &label)
{
    if (!tree || !tree->model())
        return {};
    const auto find = [&](auto &&self, const QModelIndex &parent) -> QModelIndex {
        for (int r = 0; r < tree->model()->rowCount(parent); ++r) {
            const auto row = tree->model()->index(r, 0, parent);
            if (row.data().toString() == label)
                return row.siblingAtColumn(1);
            const auto child = self(self, row);
            if (child.isValid())
                return child;
        }
        return {};
    };
    return find(find, {});
}

/** Open the picker on @p label and return it. */
LinkEndpointComboBox *openPickerIn(QTreeView *tree, const QString &label)
{
    const auto cell = propertyCellIn(tree, label);
    if (!tree || !cell.isValid() || !(cell.flags() & Qt::ItemIsEditable))
        return nullptr;
    tree->expandAll();
    tree->scrollTo(cell);
    tree->edit(cell);
    return tree->findChild<LinkEndpointComboBox *>();
}

QString endpointOf(SWMMModelLayer *layer, const char *link, bool upstream)
{
    const auto engine = layer->engine();
    const int idx = swmm_link_index(engine, link);
    int node = -1;
    const int rc = upstream ? swmm_link_get_from_node(engine, idx, &node)
                            : swmm_link_get_to_node(engine, idx, &node);
    if (rc != SWMM_OK || node < 0)
        return {};
    return QString::fromUtf8(swmm_node_id(engine, node));
}

/** The popup's click route: the row becomes current, then activated() fires. */
bool pick(LinkEndpointComboBox *picker, const QString &node)
{
    const int row = picker->findText(node);
    if (row < 0)
        return false;
    picker->setCurrentIndex(row);
    return QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, row));
}

/** Change a real Attribute Table field, including its normal layer notifications. */
bool setTableField(SWMMAttributeTableModel &model, const QString &name,
                   const QString &setter, const QVariant &value)
{
    const auto specs = model.columnSpecs();
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.index(row, 0).data().toString() != name)
            continue;
        for (int col = 0; col < specs.size(); ++col)
            if (specs[col].setter == setter)
                return model.setData(model.index(row, col), value);
    }
    return false;
}

/** The main window's docks, bound to one project the way SWMMVis binds them. */
struct Workbench {
    MapCanvas canvas;
    ProbeLayer *layer = nullptr; // owned by the canvas
    SelectionManager selection;
    PropertiesPanel properties;
    AttributeTablePanel table;
    ObjectBrowserPanel browser;
    openswmmvis::ui::SectionViewPanel section;
    ProbeRenderer renderer;
    std::unique_ptr<QSGNode> scene;

    bool open(const QString &fixture = fixturePath())
    {
        canvas.resize(800, 600);
        layer = new ProbeLayer(fixture, nullptr);
        QList<QString> warnings, errors;
        if (!layer->loadModel(warnings, errors))
            return false;
        canvas.addLayer(layer, false);
        layer->setEditCanvas(&canvas);
        layer->setGlRenderingEnabled(true);

        properties.setProject(layer);
        table.setProject(layer, &selection, &canvas);
        browser.setProject(layer, &selection, &canvas);
        section.setProject(layer);

        // SWMMVis::initializePropertiesDock — two-way sync + dirty tracking.
        QObject::connect(&properties, &PropertiesPanel::objectEdited, &table,
                         &AttributeTablePanel::onObjectEditedExternally);
        QObject::connect(&table, &AttributeTablePanel::objectEdited, &properties,
                         &PropertiesPanel::onObjectEditedExternally);
        QObject::connect(&properties, &PropertiesPanel::objectEdited, &section,
                         &openswmmvis::ui::SectionViewPanel::onObjectEditedExternally);
        QObject::connect(&table, &AttributeTablePanel::objectEdited, &section,
                         &openswmmvis::ui::SectionViewPanel::onObjectEditedExternally);
        const auto markEdited = [this](const QString &) { layer->markEdited(); };
        QObject::connect(&properties, &PropertiesPanel::objectEdited, &canvas, markEdited);
        QObject::connect(&table, &AttributeTablePanel::objectEdited, &canvas, markEdited);

        // SWMMVis::showPropertiesSelection, single network object branch.
        QObject::connect(&selection, &SelectionManager::selectionChanged, &properties,
                         [this](const QSet<SWMMObjectRef> &current, const QSet<SWMMObjectRef> &,
                                const QSet<SWMMObjectRef> &) {
                             if (current.isEmpty()) {
                                 properties.clear();
                                 section.clearSelection();
                                 return;
                             }
                             if (current.size() > 1) {
                                 properties.showSelection(&canvas, current.values());
                                 section.clearSelection();
                                 return;
                             }
                             const SWMMObjectRef first = *current.constBegin();
                             section.showObject(static_cast<int>(first.objectType), first.name);
                             const QVariantMap attrs = layer->identifyByName(
                                 first.name, SWMMModelLayer::kindMaskForObjectType(
                                                 static_cast<int>(first.objectType)));
                             if (attrs.isEmpty()) {
                                 properties.clear();
                                 return;
                             }
                             IdentifyResult r;
                             r.layerName = layer->name();
                             r.features = {attrs};
                             properties.showIdentifyResults({r});
                         });

        renderer.setWidth(800);
        renderer.setHeight(600);
        renderer.setLayer(layer);
        renderer.setMapExtent(canvas.extent());

        properties.resize(500, 700);
        properties.show();
        table.resize(1000, 500);
        table.show();
        table.showModelCategory(layer, SWMMModelLayer::CatConduits);
        section.show();
        browser.show();
        return true;
    }

    /** Full QSG rebuild of the model layer, as the canvas does on the next frame. */
    void paint()
    {
        scene.reset(renderer.updatePaintNode(scene.release(), nullptr));
    }

    /** Bind @p name and let the adapters it replaced be destroyed. */
    void selectNode(const QString &name)
    {
        selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Node, name}},
                         SelectionManager::Replace);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    QTreeView *propertyTree() const { return properties.findChild<QTreeView *>(); }

    QModelIndex propertyCell(const QString &label) const
    {
        return propertyCellIn(propertyTree(), label);
    }

    LinkEndpointComboBox *openPicker(const QString &label)
    {
        return openPickerIn(propertyTree(), label);
    }

    QString endpoint(const char *link, bool upstream) const
    {
        return endpointOf(layer, link, upstream);
    }

    QString tableCell(const QString &link, const QString &header) const
    {
        auto *view = table.findChild<QTableView *>();
        if (!view || !view->model())
            return {};
        auto *model = view->model();
        int col = -1;
        for (int c = 0; c < model->columnCount(); ++c)
            if (model->headerData(c, Qt::Horizontal).toString() == header)
                col = c;
        for (int r = 0; r < model->rowCount(); ++r)
            if (model->index(r, 0).data().toString() == link)
                return col >= 0 ? model->index(r, col).data().toString() : QString();
        return {};
    }
};

} // namespace

class TestLinkEndpointProperties : public QObject
{
    Q_OBJECT

private slots:
    /** Picking a downstream node from the dropdown reconnects the link and refreshes every view. */
    void downstreamPickFromPropertiesDock()
    {
        Workbench w;
        QVERIFY(w.open());
        w.paint();
        QVERIFY(w.scene);
        const auto contentRevision = w.renderer.contentRevision();

        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Link, "C1"}}, SelectionManager::Replace);
        QCOMPARE(w.propertyCell("To Node").data().toString(), QString("X1"));
        QCOMPARE(w.tableCell("C1", "To Node"), QString("X1"));

        QPointer<LinkEndpointComboBox> picker = w.openPicker("To Node");
        QVERIFY(picker);
        QCOMPARE(picker->currentText(), QString("X1"));
        QVERIFY(picker->findText("J1") < 0); // The upstream node is never offered.
        QSignalSpy modelResets(picker->model(), &QAbstractItemModel::modelReset);
        const int choice = picker->findText("O1");
        QVERIFY(choice >= 0);
        picker->setCurrentIndex(choice);
        QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, choice)));

        QCOMPARE(w.endpoint("C1", false), QString("O1"));
        QCOMPARE(w.canvas.undoStack()->count(), 1);
        QCOMPARE(modelResets.count(), 0); // Re-applying the value keeps the candidate list.
        double x = 0, y = 0;
        QVERIFY(w.layer->cachedNodeCoord(w.layer->nodeIndex("O1"), &x, &y));
        QCOMPARE(w.layer->cachedLinkPolyline(w.layer->linkIndex("C1")).last(), QPointF(x, y));
        QVERIFY(w.renderer.contentRevision() > contentRevision);
        w.paint();
        QVERIFY(w.scene);

        // The editor survives its own commit and reflects the new node.
        QVERIFY(picker);
        QCOMPARE(picker->currentText(), QString("O1"));
        QCOMPARE(w.propertyCell("To Node").data().toString(), QString("O1"));
        QCOMPARE(w.tableCell("C1", "To Node"), QString("O1"));
        QVERIFY(w.selection.selection().contains(SWMMObjectRef{SWMMObjectRef::Link, "C1"}));

        // A second pick in the same editing session goes the same way.
        const int back = picker->findText("X1");
        QVERIFY(back >= 0);
        picker->setCurrentIndex(back);
        QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, back)));
        QCOMPARE(w.endpoint("C1", false), QString("X1"));
        QCOMPARE(w.canvas.undoStack()->count(), 2);
        QCOMPARE(w.propertyCell("To Node").data().toString(), QString("X1"));
        w.paint();

        w.canvas.undoStack()->undo();
        QCOMPARE(w.endpoint("C1", false), QString("O1"));
        QCOMPARE(w.propertyCell("To Node").data().toString(), QString("O1"));
        QCOMPARE(w.tableCell("C1", "To Node"), QString("O1"));
        w.canvas.undoStack()->undo();
        QCOMPARE(w.endpoint("C1", false), QString("X1"));
        w.canvas.undoStack()->redo();
        QCOMPARE(w.endpoint("C1", false), QString("O1"));
        w.paint();
        QVERIFY(w.scene);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void endpointTypedWithReturn_data()
    {
        QTest::addColumn<bool>("upstream");
        QTest::newRow("downstream") << false;
        QTest::newRow("upstream") << true;
    }

    /** Typing and rejecting IDs exercise both endpoint directions. */
    void endpointTypedWithReturn()
    {
        QFETCH(bool, upstream);
        Workbench w;
        QVERIFY(w.open());
        w.selectNode("J1"); // Destroy a node adapter before either endpoint edit.
        const QString label = upstream ? "From Node" : "To Node";
        const char *link = upstream ? "X1" : "C1";
        const QString target = upstream ? "J1" : "O1";
        const QString opposite = upstream ? "O1" : "J1";
        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Link, link}}, SelectionManager::Replace);

        QPointer<LinkEndpointComboBox> picker = w.openPicker(label);
        QVERIFY(picker);
        picker->setFocus();
        picker->lineEdit()->selectAll();
        QTest::keyClicks(picker->lineEdit(), target);
        QTest::keyClick(picker->lineEdit(), Qt::Key_Return);
        QTRY_COMPARE(w.endpoint(link, upstream), target);
        QCOMPARE(w.canvas.undoStack()->count(), 1);
        QTRY_COMPARE(w.propertyCell(label).data().toString(), target);
        QCOMPARE(w.tableCell(link, label), target);
        w.paint();
        QVERIFY(w.scene);

        // Neither the opposite endpoint nor an unknown ID can become a commit.
        for (const auto &invalid : {opposite, QString("missing-node")}) {
            picker = w.openPicker(label);
            QVERIFY(picker);
            picker->setFocus();
            picker->lineEdit()->selectAll();
            QTest::keyClicks(picker->lineEdit(), invalid);
            QTest::keyClick(picker->lineEdit(), Qt::Key_Return);
            QCoreApplication::processEvents();
            QCOMPARE(w.endpoint(link, upstream), target);
            QCOMPARE(w.canvas.undoStack()->count(), 1);
        }
        w.canvas.undoStack()->undo();
        QCOMPARE(w.endpoint(link, upstream), QString("X1"));
        QCOMPARE(w.propertyCell(label).data().toString(), QString("X1"));
        QCOMPARE(w.tableCell(link, label), QString("X1"));
        w.canvas.undoStack()->redo();
        QCOMPARE(w.endpoint(link, upstream), target);
        QCOMPARE(w.propertyCell(label).data().toString(), target);
        QCOMPARE(w.tableCell(link, label), target);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    /** The upstream picker follows the same path. */
    void upstreamPickFromPropertiesDock()
    {
        Workbench w;
        QVERIFY(w.open());
        w.selectNode("J1");
        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Link, "X1"}}, SelectionManager::Replace);
        QCOMPARE(w.propertyCell("From Node").data().toString(), QString("X1"));

        QPointer<LinkEndpointComboBox> picker = w.openPicker("From Node");
        QVERIFY(picker);
        QVERIFY(picker->findText("O1") < 0); // The downstream node is never offered.
        const int choice = picker->findText("J1");
        QVERIFY(choice >= 0);
        picker->setCurrentIndex(choice);
        QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, choice)));
        QCOMPARE(w.endpoint("X1", true), QString("J1"));
        QCOMPARE(w.canvas.undoStack()->count(), 1);
        double x = 0, y = 0;
        QVERIFY(w.layer->cachedNodeCoord(w.layer->nodeIndex("J1"), &x, &y));
        QCOMPARE(w.layer->cachedLinkPolyline(w.layer->linkIndex("X1")).first(), QPointF(x, y));
        QCOMPARE(w.propertyCell("From Node").data().toString(), QString("J1"));
        QCOMPARE(w.tableCell("X1", "From Node"), QString("J1"));
        w.paint();
        QVERIFY(w.scene);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    /** Re-selecting and editing the same link after a change does not reuse stale editors. */
    void reselectAfterEndpointChange()
    {
        Workbench w;
        QVERIFY(w.open());
        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Link, "C1"}}, SelectionManager::Replace);
        QPointer<LinkEndpointComboBox> picker = w.openPicker("To Node");
        QVERIFY(picker);
        const int choice = picker->findText("O1");
        picker->setCurrentIndex(choice);
        QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, choice)));
        QCOMPARE(w.endpoint("C1", false), QString("O1"));

        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Node, "J1"}}, SelectionManager::Replace);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Link, "C1"}}, SelectionManager::Replace);
        QCOMPARE(w.propertyCell("To Node").data().toString(), QString("O1"));
        picker = w.openPicker("To Node");
        QVERIFY(picker);
        const int back = picker->findText("X1");
        QVERIFY(back >= 0);
        picker->setCurrentIndex(back);
        QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, back)));
        QCOMPARE(w.endpoint("C1", false), QString("X1"));
        QCOMPARE(w.canvas.undoStack()->count(), 2);
        w.paint();
        QVERIFY(w.scene);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    /**
     * The reported To Node crash: faulted in an attributeChanged lambda of
     * PropertiesPanel::onLayerComboIndexChanged that still held a replaced node
     * adapter. The receiver count must stay constant across selection changes.
     */
    void nodeSelectionLeavesNoConnectionToReplacedAdapter()
    {
        Workbench w;
        QVERIFY(w.open());
        w.selectNode("J1");
        QPointer<SWMMNodePropertyAdapter> replaced =
            w.properties.findChild<SWMMNodePropertyAdapter *>();
        QVERIFY(replaced);
        const int receivers = w.layer->attributeChangedReceivers();
        w.selectNode("X1");
        QVERIFY(!replaced); // Destroyed, so a surviving connection would read freed memory.
        w.selectNode("J1");
        QCOMPARE(w.layer->attributeChangedReceivers(), receivers);

        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Link, "C1"}},
                           SelectionManager::Replace);
        auto *picker = w.openPicker("To Node");
        QVERIFY(picker);
        QVERIFY(pick(picker, "O1"));
        QCOMPARE(w.endpoint("C1", false), QString("O1"));
        QCOMPARE(w.canvas.undoStack()->count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    /** Storage nodes also bind the LID row flags, a second lambda with the same lifetime. */
    void storageSelectionLeavesNoConnectionToReplacedAdapter()
    {
        Workbench w;
        QVERIFY(w.open(QDir(dataDir()).filePath(QStringLiteral("lid_treatment_editor.inp"))));
        w.selectNode("S");
        const int receivers = w.layer->attributeChangedReceivers();
        w.selectNode("O");
        w.selectNode("S");
        QCOMPARE(w.layer->attributeChangedReceivers(), receivers);
    }

    /** Identify population must not bind the first item twice. */
    void identifyCreatesOneAdapter()
    {
        Workbench w;
        QVERIFY(w.open());
        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Node, "J1"}},
                           SelectionManager::Replace);
        QCOMPARE(w.properties.findChildren<SWMMNodePropertyAdapter *>().size(), 1);
    }

    /** External coupling edits must still update the current node's row flags. */
    void couplingRowFlagsFollowAttributeTableAndUndo()
    {
        Workbench w;
        QVERIFY(w.open());
        w.selectNode("J1");
        SWMMAttributeTableModel model;
        model.setSource(w.layer, SWMMModelLayer::CatJunctions);
        model.setUndoStack(w.canvas.undoStack());
        const auto diameter = w.propertyCell("2D Manhole Diameter (ft)");
        const auto perimeter = w.propertyCell("2D Inlet Perimeter (ft)");
        QVERIFY(diameter.isValid());
        QVERIFY(perimeter.isValid());
        const auto editable = [](const QModelIndex &cell) {
            return bool(cell.flags() & Qt::ItemIsEditable) && bool(cell.flags() & Qt::ItemIsEnabled);
        };
        using A = SWMMNodePropertyAdapter;
        QVERIFY(setTableField(model, "J1", "node_2d_coupling_type", int(A::MANHOLE)));
        QVERIFY(editable(diameter));
        QVERIFY(!editable(perimeter));
        QVERIFY(setTableField(model, "J1", "node_2d_coupling_type", int(A::GRATE)));
        QVERIFY(!editable(diameter));
        QVERIFY(editable(perimeter));
        w.canvas.undoStack()->undo();
        QVERIFY(editable(diameter));
        QVERIFY(!editable(perimeter));
        w.canvas.undoStack()->redo();
        QVERIFY(!editable(diameter));
        QVERIFY(editable(perimeter));
        QVERIFY(setTableField(model, "J1", "node_2d_coupling_type", int(A::NOT_COUPLEABLE)));
        QVERIFY(!editable(diameter));
        QVERIFY(!editable(perimeter));
    }

    /** A deleteLater adapter must not change flags on the newly selected node. */
    void replacedAdapterCannotRefreshCurrentRows()
    {
        Workbench w;
        QVERIFY(w.open());
        SWMMAttributeTableModel model;
        model.setSource(w.layer, SWMMModelLayer::CatJunctions);
        QVERIFY(setTableField(model, "J1", "node_2d_coupling_type",
                             int(SWMMNodePropertyAdapter::MANHOLE)));
        QVERIFY(setTableField(model, "X1", "node_2d_coupling_type",
                             int(SWMMNodePropertyAdapter::GRATE)));
        w.selectNode("J1");
        QPointer<SWMMNodePropertyAdapter> previous =
            w.properties.findChild<SWMMNodePropertyAdapter *>();
        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Node, "X1"}},
                           SelectionManager::Replace);
        QVERIFY(previous); // Its deferred deletion has not run yet.
        const auto diameter = w.propertyCell("2D Manhole Diameter (ft)");
        QVERIFY(diameter.isValid());
        QVERIFY(!(diameter.flags() & Qt::ItemIsEditable));
        emit w.layer->attributeChanged("J1");
        QVERIFY(!(diameter.flags() & Qt::ItemIsEditable));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!previous);
    }

    /** The second fixed callback enables LID rows and ignores replaced storage adapters. */
    void lidRowFlagsFollowAttributeTableAndUndo()
    {
        Workbench w;
        QVERIFY(w.open(QDir(dataDir()).filePath(QStringLiteral("lid_treatment_editor.inp"))));
        w.selectNode("S");
        SWMMAttributeTableModel model;
        model.setSource(w.layer, SWMMModelLayer::CatStorage);
        model.setUndoStack(w.canvas.undoStack());
        const auto saturation = w.propertyCell("LID Initial Saturation (%)");
        const auto bottom = w.propertyCell("LID Bottom Ownership");
        QVERIFY(saturation.isValid());
        QVERIFY(bottom.isValid());
        QVERIFY(saturation.flags() & Qt::ItemIsEditable);
        QVERIFY(bottom.flags() & Qt::ItemIsEditable);
        DataObjectRef cleared;
        cleared.engine = w.layer->engine();
        cleared.layer = w.layer;
        cleared.kind = DataObjectRef::LidControl;
        QVERIFY(setTableField(model, "S", "node_lid_control", QVariant::fromValue(cleared)));
        QVERIFY(!(saturation.flags() & Qt::ItemIsEditable));
        QVERIFY(!(bottom.flags() & Qt::ItemIsEditable));
        w.canvas.undoStack()->undo();
        QVERIFY(saturation.flags() & Qt::ItemIsEditable);
        QVERIFY(bottom.flags() & Qt::ItemIsEditable);
        w.canvas.undoStack()->redo();
        QVERIFY(!(saturation.flags() & Qt::ItemIsEditable));
        QVERIFY(!(bottom.flags() & Qt::ItemIsEditable));

        w.canvas.undoStack()->undo(); // S is assigned again.
        QVERIFY(w.layer->applyNodeAdd("S2", SWMM_NODE_STORAGE, 50, 50));
        QPointer<SWMMNodePropertyAdapter> previous =
            w.properties.findChild<SWMMNodePropertyAdapter *>();
        w.selection.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Node, "S2"}},
                           SelectionManager::Replace);
        QVERIFY(previous);
        const auto currentSaturation = w.propertyCell("LID Initial Saturation (%)");
        QVERIFY(currentSaturation.isValid());
        QVERIFY(!(currentSaturation.flags() & Qt::ItemIsEditable));
        emit w.layer->attributeChanged("S");
        QVERIFY(!(currentSaturation.flags() & Qt::ItemIsEditable));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!previous);
    }

    /** Re-applying after an external endpoint change must not keep stale candidates. */
    void endpointCandidatesFollowOppositeEndpoint()
    {
        Workbench w;
        QVERIFY(w.open());
        LinkEndpointComboBox editor;
        LinkEndpointRef ref;
        ref.engine = w.layer->engine();
        ref.linkName = "C1";
        ref.upstream = false;
        ref.nodeName = "X1";
        editor.setValue(ref);
        QVERIFY(editor.findText("J1") < 0);
        QVERIFY(editor.findText("O1") >= 0);
        QVERIFY(w.layer->applyLinkEndpoint(w.layer->linkIndex("C1"), true,
                                           w.layer->nodeIndex("O1")));
        editor.setValue(ref);
        QVERIFY(editor.findText("O1") < 0);
        QVERIFY(editor.findText("J1") >= 0);
    }

    /** A synchronous commit refresh keeps indices; external changes rebuild choices. */
    void dataObjectPickerKeepsListAcrossReapply()
    {
        Workbench w;
        QVERIFY(w.open());
        DataObjectPickerEditor editor;
        auto *combo = editor.findChild<QComboBox *>();
        QVERIFY(combo);
        DataObjectRef ref;
        ref.engine = w.layer->engine();
        ref.layer = w.layer;
        ref.kind = DataObjectRef::Node;
        ref.currentName = QStringLiteral("J1");
        editor.setValue(ref);
        QCOMPARE(combo->currentText(), QString("J1"));
        const int rows = combo->count();

        QSignalSpy rebuilt(combo->model(), &QAbstractItemModel::rowsRemoved);
        connect(&editor, &DataObjectPickerEditor::valueChanged, &editor,
                [&editor] { editor.setValue(editor.value()); });
        combo->setCurrentIndex(combo->findText("X1"));
        QCOMPARE(combo->currentText(), QString("X1"));
        combo->setCurrentIndex(0);
        QCOMPARE(combo->currentIndex(), 0); // "(none)"
        QCOMPARE(rebuilt.count(), 0);
        QCOMPARE(combo->count(), rows);

        QVERIFY(w.layer->applyNodeAdd("J2", SWMM_NODE_JUNCTION, 50, 50));
        ref.currentName = QStringLiteral("J1");
        editor.setValue(ref);
        QVERIFY(combo->findText("J2") >= 0);
        QVERIFY(rebuilt.count() > 0);
        rebuilt.clear();
        ref.currentName = QStringLiteral("ZZ"); // Rebuilt so the assigned name stays visible.
        editor.setValue(ref);
        QCOMPARE(combo->currentText(), QString("ZZ"));
        QVERIFY(rebuilt.count() > 0);

        rebuilt.clear();
        ref.kind = DataObjectRef::Subcatchment;
        ref.currentName.clear();
        editor.setValue(ref);
        QVERIFY(rebuilt.count() > 0);
        QVERIFY(combo->findText("J1") < 0);
    }

    void mainWindowEndpointEditAfterSelectingNodes_data()
    {
        endpointTypedWithReturn_data();
    }

    /** The reported sequence in both directions through the real main window. */
    void mainWindowEndpointEditAfterSelectingNodes()
    {
        QFETCH(bool, upstream);
        const QString label = upstream ? "From Node" : "To Node";
        const char *link = upstream ? "X1" : "C1";
        const QString target = upstream ? "J1" : "O1";
        auto *win = new SWMMVis(nullptr);
        win->resize(1400, 900);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        QVERIFY(QMetaObject::invokeMethod(win, "openSingleINP", Q_ARG(QString, fixturePath())));
        QTRY_VERIFY_WITH_TIMEOUT(win->findChild<SWMMVisProjectWindow *>() &&
                                     win->findChild<SWMMVisProjectWindow *>()->modelLayer(),
                                 20000);
        QTest::qWait(1500);
        auto *project = win->findChild<SWMMVisProjectWindow *>();
        auto *layer = project->modelLayer();
        auto *panel = win->findChild<PropertiesPanel *>();
        auto *table = win->findChild<AttributeTablePanel *>();
        QVERIFY(panel);
        QVERIFY(table);
        table->showModelCategory(layer, SWMMModelLayer::CatConduits);
        auto *tree = panel->findChild<QTreeView *>();
        const auto select = [&](SWMMObjectRef::ObjectType type, const QString &name) {
            project->selectionManager()->select(QSet<SWMMObjectRef>{{type, name}},
                                                SelectionManager::Replace);
            QTest::qWait(50); // A real event loop destroys the replaced adapters.
        };
        select(SWMMObjectRef::Node, "J1");
        select(SWMMObjectRef::Node, "X1");
        select(SWMMObjectRef::Link, link);
        QCOMPARE(propertyCellIn(tree, label).data().toString(), QString("X1"));

        auto *picker = openPickerIn(tree, label);
        QVERIFY(picker);
        QVERIFY(pick(picker, target));
        QCOMPARE(endpointOf(layer, link, upstream), target);
        QTest::qWait(50);

        picker->setFocus();
        picker->lineEdit()->selectAll();
        QTest::keyClicks(picker->lineEdit(), "X1");
        QTest::keyClick(picker->lineEdit(), Qt::Key_Return);
        QTRY_COMPARE(endpointOf(layer, link, upstream), QString("X1"));
        QTRY_COMPARE(propertyCellIn(tree, label).data().toString(), QString("X1"));

        // Deleting SWMMVis directly crashes in a status-bar units slot during
        // teardown (test_link_click_selection); hide it and let the process reclaim it.
        win->hide();
    }
};

QTEST_MAIN(TestLinkEndpointProperties)
#include "test_linkendpointproperties.moc"
