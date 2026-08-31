/*!
 * \file   objectbrowserpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/swmmobjecttreemodel.h"
#include "ui/dialogs/curveeditordialog.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/dialogs/ruleseditordialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/dialogs/transecteditordialog.h"
#include "ui/editors/comprehensiveeditorregistry.h"
#include "controls/controlruleregistry.h"
#include "curve/curveprovider.h"
#include "curve/curveregistry.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/mapundostack.h"
#include "pattern/patternprovider.h"
#include "pattern/patternregistry.h"
#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"
#include "transect/transectregistry.h"

#include <QPointer>

#include <cmath>

#include <QDebug>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QTreeView>
#include <QVariantMap>
#include <QVBoxLayout>

// Slice BM.0 — engine setters for the "Add New …" context-menu action.
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_subcatchments.h>
// Slice DA.3 — control rules + unit hydrographs creation flows.
#include <openswmm/engine/openswmm_controls.h>
#include <openswmm/engine/openswmm_inflows.h>

#include <algorithm>

namespace {

/*! Proxy that keeps category headers visible whenever at least one of
 *  their child leaves matches the user's filter. QSortFilterProxyModel
 *  has `setRecursiveFilteringEnabled(true)` since Qt 5.10, which does
 *  exactly that — no custom filterAcceptsRow needed. */
class NameFilterProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
};

/*! Debounce window for the search-box → proxy-filter path. 200 ms is
 *  fast enough to feel live but skips all but the last keystroke in a
 *  rapid burst, sparing the recursive filter rebuild on huge models. */
constexpr int kFilterDebounceMs = 200;

} // anonymous

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ObjectBrowserPanel::ObjectBrowserPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

ObjectBrowserPanel::~ObjectBrowserPanel() = default;

void ObjectBrowserPanel::buildUi()
{
    auto *vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(2, 2, 2, 2);
    vlay->setSpacing(2);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Filter by name…"));
    m_searchEdit->setClearButtonEnabled(true);
    vlay->addWidget(m_searchEdit);

    m_model = new SWMMObjectTreeModel(this);
    m_proxy = new NameFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setRecursiveFilteringEnabled(true);

    m_view = new QTreeView(this);
    m_view->setModel(m_proxy);
    m_view->setHeaderHidden(false);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setRootIsDecorated(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->header()->setStretchLastSection(true);

    // Slice T.2 — drag-and-drop category reordering. Only category
    // rows are drag-enabled (flags() on leaves omits ItemIsDragEnabled),
    // so the user can only shuffle group headers; leaves stay put.
    // The proxy filters mimeData but otherwise passes through, so
    // QSortFilterProxyModel works transparently with the drop path.
    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDropIndicatorShown(true);
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setDefaultDropAction(Qt::MoveAction);
    vlay->addWidget(m_view, 1);

    m_filterDebounce = new QTimer(this);
    m_filterDebounce->setSingleShot(true);
    m_filterDebounce->setInterval(kFilterDebounceMs);
    connect(m_filterDebounce, &QTimer::timeout,
            this,             &ObjectBrowserPanel::applyFilterNow);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this,         &ObjectBrowserPanel::onSearchTextChanged);
    connect(m_view, &QTreeView::customContextMenuRequested,
            this,   &ObjectBrowserPanel::onContextMenuRequested);
    connect(m_view, &QTreeView::doubleClicked,
            this,   &ObjectBrowserPanel::onItemDoubleClicked);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &, const QItemSelection &) {
                onTreeSelectionChanged();
            });

    // Model-initiated resets (data-object mutations reload the tree model
    // directly, bypassing refresh()'s blockSignals wrapper) clear the
    // view's selection, which would push an empty Replace to the
    // SelectionManager and wipe the map selection. Suppress the echo for
    // the duration of the reset, then re-apply the bus selection.
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset,
            this, [this] { m_applyingFromBus = true; });
    connect(m_model, &QAbstractItemModel::modelReset,
            this, [this] {
                m_applyingFromBus = false;
                if (m_selMgr && !m_selMgr->isEmpty())
                    onSelectionManagerChanged(m_selMgr->selection(), {}, {});
            });
}

// ---------------------------------------------------------------------------
// Project binding
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::setProject(SWMMModelLayer *layer,
                                     SelectionManager *selMgr,
                                     MapCanvas *canvas)
{
    if (m_layer == layer && m_selMgr == selMgr && m_canvas == canvas)
        return;

    if (m_selMgr)
        QObject::disconnect(m_selMgr, &SelectionManager::selectionChanged,
                            this,     &ObjectBrowserPanel::onSelectionManagerChanged);

    if (m_layer)
        QObject::disconnect(m_layer, &SWMMModelLayer::geometryChanged,
                            this,    &ObjectBrowserPanel::refresh);

    m_layer  = layer;
    m_selMgr = selMgr;
    m_canvas = canvas;

    m_model->setLayer(layer);

    if (m_selMgr)
        connect(m_selMgr, &SelectionManager::selectionChanged,
                this,     &ObjectBrowserPanel::onSelectionManagerChanged,
                Qt::UniqueConnection);

    if (m_layer)
        connect(m_layer, &SWMMModelLayer::geometryChanged,
                this,    &ObjectBrowserPanel::refresh,
                Qt::UniqueConnection);

    refresh();
}

void ObjectBrowserPanel::focusSearch()
{
    if (!m_searchEdit) return;
    m_searchEdit->setFocus(Qt::ShortcutFocusReason);
    m_searchEdit->selectAll();
}

void ObjectBrowserPanel::selectCategory(SWMMModelLayer::Category c)
{
    if (!m_view || !m_model || !m_proxy) return;
    const int topRow = m_model->topRowForCategory(c);
    if (topRow < 0) return;   // category empty → not shown in the tree
    const QModelIndex src = m_model->index(topRow, 0, QModelIndex());
    const QModelIndex proxy = m_proxy->mapFromSource(src);
    if (!proxy.isValid()) return;   // filtered out by the search box

    // Guard is mandatory, not defensive: a category header resolves to zero
    // object refs, so onTreeSelectionChanged would push an EMPTY Replace to
    // the SelectionManager and wipe the user's map selection.
    m_applyingFromBus = true;
    m_view->selectionModel()->setCurrentIndex(
        proxy, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_view->scrollTo(proxy, QAbstractItemView::PositionAtTop);
    m_applyingFromBus = false;
}

void ObjectBrowserPanel::refresh()
{
    // Block the tree's selection-change signal from propagating to the
    // SelectionManager while the model resets. Without this, endResetModel()
    // clears the QItemSelectionModel, which fires onTreeSelectionChanged()
    // with an empty set, calling m_selMgr->select({}, Replace) — wiping the
    // global selection and removing the canvas highlight for every object.
    // blockSignals() is scoped to the selection model only, so it has no
    // side-effects on canvas rendering, unlike the broader m_applyingFromBus.
    //
    // Every reload starts fully collapsed — user-driven expansion (clicking
    // a category, or selecting an object via the bus) is the only thing
    // that opens a category. SWMMObjectTreeModel only materialises rows
    // that the view actually paints, so a collapsed tree means zero leaf
    // data() calls until the user expands.
    auto *sm = m_view->selectionModel();
    sm->blockSignals(true);
    m_model->reload();
    sm->blockSignals(false);

    // Restore the tree's visual selection to match the SelectionManager.
    // onSelectionManagerChanged() expands parents of selected rows so the
    // highlight is visible.
    if (m_selMgr && !m_selMgr->isEmpty())
        onSelectionManagerChanged(m_selMgr->selection(), {}, {});
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

SWMMObjectRef
ObjectBrowserPanel::refForProxyIndex(const QModelIndex &proxyIdx) const
{
    if (!proxyIdx.isValid()) return {SWMMObjectRef::Unknown, {}};
    const QModelIndex src = m_proxy->mapToSource(proxyIdx);
    if (!src.isValid()) return {SWMMObjectRef::Unknown, {}};
    const QVariant isLeaf = m_model->data(src, SWMMObjectTreeModel::RoleIsLeaf);
    if (!isLeaf.toBool()) return {SWMMObjectRef::Unknown, {}};
    return m_model->data(src, SWMMObjectTreeModel::RoleObjectRef)
                  .value<SWMMObjectRef>();
}

// ---------------------------------------------------------------------------
// Context menu / double-click
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::onContextMenuRequested(const QPoint &pos)
{
    if (!m_layer) return;
    const QModelIndex proxyIdx = m_view->indexAt(pos);
    if (!proxyIdx.isValid()) return;

    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    if (!srcIdx.isValid()) return;

    const bool isLeaf = m_model->data(srcIdx, SWMMObjectTreeModel::RoleIsLeaf).toBool();
    const int  sectionInt = m_model->data(srcIdx, SWMMObjectTreeModel::RoleSection)
                                  .toInt();

    // Slice BM.0 — separator row has no menu.
    if (sectionInt == int(SWMMObjectTreeModel::SectionDivider))
        return;

    // Slice BM.0 — data-objects section uses its own (smaller) menu;
    // CRUD beyond "Add New …" lands when each editor slice ships its
    // engine setters.
    if (sectionInt == int(SWMMObjectTreeModel::SectionData)) {
        const int dcInt = m_model->data(srcIdx, SWMMObjectTreeModel::RoleDataCategory)
                                 .toInt();
        if (dcInt < 0 || dcInt >= int(SWMMModelLayer::NumDataCategories))
            return;
        const auto dc = static_cast<SWMMModelLayer::DataCategory>(dcInt);

        QMenu dmenu(this);
        if (!isLeaf) {
            // Slice BM.0-Add-New (2026-05-24) — Add-New launches the
            // category's complex MVC editor directly (via
            // launchAddNewEditor). For categories without a complex
            // editor yet, the action is disabled with a tooltip naming
            // the future slice. NewDataObjectDialog has been removed.
            QAction *actAdd = dmenu.addAction(QIcon(QStringLiteral(":/swmmvis/Layers")),
                                              tr("Add New…"));
            if (!hasComplexEditor(dc)) {
                actAdd->setEnabled(false);
                actAdd->setToolTip(gapTooltipFor(dc));
                dmenu.setToolTipsVisible(true);
            }
            QAction *picked = dmenu.exec(m_view->viewport()->mapToGlobal(pos));
            if (!picked) return;
            if (picked == actAdd) launchAddNewEditor(dc);
            return;
        }

        // 2026-05-29 — leaves with a shipped comprehensive editor (TS,
        // Curve, Pattern, Hydrograph, Transect, Control) get an "Edit…"
        // action that opens that editor with this object pre-selected.
        // For gap categories the action is disabled with the registry's
        // tooltip naming the future slice. "Properties…" stays as the
        // existing path that just routes the selection through the bus
        // so the PropertiesPanel mirrors the click.
        const auto &editorReg = ComprehensiveEditorRegistry::instance();
        const bool hasEditor  = editorReg.hasEditor(dc);
        QAction *actEdit = dmenu.addAction(QIcon(QStringLiteral(":/swmmvis/Layers")),
                                            tr("Edit…"));
        actEdit->setEnabled(hasEditor);
        if (!hasEditor) {
            actEdit->setToolTip(editorReg.gapTooltip(dc));
            dmenu.setToolTipsVisible(true);
        }
        QAction *actProps = dmenu.addAction(tr("Properties…"));

        // Delete… — data objects with a registry-backed delete path
        // (curves, time series, transects) route through the undoable
        // DeleteDataObjectCommand. Types whose engine delete API doesn't
        // exist yet (patterns, pollutants, aquifers, …) show a disabled
        // action so the menu is honest about the gap. See
        // workplans/GUI_DELETE_ALL_OBJECTS_PLAN_2026-07-22.md.
        const SWMMObjectRef leafRef = refForProxyIndex(proxyIdx);
        const bool canDelete =
            DeleteDataObjectCommand::supports(leafRef.objectType) && m_layer;
        QAction *actDelete = nullptr;
        if (leafRef.objectType != SWMMObjectRef::Unknown) {
            dmenu.addSeparator();
            actDelete = dmenu.addAction(tr("Delete…"));
            actDelete->setEnabled(canDelete);
            if (!canDelete) {
                actDelete->setToolTip(
                    tr("Delete is not yet available for this object type."));
                dmenu.setToolTipsVisible(true);
            }
        }

        QAction *picked = dmenu.exec(m_view->viewport()->mapToGlobal(pos));
        if (picked == actEdit && hasEditor) {
            openComprehensiveEditorFor(
                m_layer,
                m_canvas ? m_canvas->undoStack() : nullptr,
                refForProxyIndex(proxyIdx), this);
        } else if (picked == actProps) {
            // Future: dispatch through PropertyEditorRegistry. For now
            // this just emits the existing selection signal so the
            // attribute panel reflects the click.
            const SWMMObjectRef ref = refForProxyIndex(proxyIdx);
            if (m_selMgr && ref.objectType != SWMMObjectRef::Unknown)
                m_selMgr->select(ref, SelectionManager::Replace);
        } else if (picked && picked == actDelete && canDelete) {
            const QString msg = tr("Delete \"%1\"?").arg(leafRef.name);
            if (QMessageBox::question(
                    this, tr("Confirm Delete"), msg,
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                != QMessageBox::Yes)
                return;

            if (auto *sm = m_view->selectionModel())
                sm->clearSelection();

            if (m_canvas && m_canvas->undoStack()) {
                m_canvas->undoStack()->push(
                    new DeleteDataObjectCommand(m_layer, leafRef, m_canvas));
            } else {
                // No undo stack (headless / tests): perform the same
                // mutation DeleteDataObjectCommand::redo() would.
                DeleteDataObjectCommand cmd(m_layer, leafRef, nullptr);
                cmd.redo();
            }
        }
        return;
    }

    const int  catInt = m_model->data(srcIdx, SWMMObjectTreeModel::RoleCategory).toInt();
    if (catInt < 0 || catInt >= int(SWMMModelLayer::NumCategories)) return;
    const auto cat = static_cast<SWMMModelLayer::Category>(catInt);

    QMenu menu(this);

    if (!isLeaf) {
        // ── Category row ─────────────────────────────────────────────────
        const int visPos   = m_model->topRowForCategory(cat);
        const int visCount = m_model->rowCount({});

        QAction *actTop   = menu.addAction(tr("Move to Top"));
        QAction *actUp    = menu.addAction(tr("Move Up"));
        QAction *actDown  = menu.addAction(tr("Move Down"));
        QAction *actBot   = menu.addAction(tr("Move to Bottom"));
        menu.addSeparator();
        QAction *actReset = menu.addAction(tr("Reset to Default Order"));

        actTop ->setEnabled(visPos > 0);
        actUp  ->setEnabled(visPos > 0);
        actDown->setEnabled(visPos < visCount - 1);
        actBot ->setEnabled(visPos < visCount - 1);

        QAction *picked = menu.exec(m_view->viewport()->mapToGlobal(pos));
        if (!picked) return;

        QVector<SWMMModelLayer::Category> oldOrder = m_layer->categoryOrder();
        QVector<SWMMModelLayer::Category> newOrder = oldOrder;

        if (picked == actReset) {
            newOrder.clear();
            for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i)
                newOrder.append(static_cast<SWMMModelLayer::Category>(i));
        } else {
            const int fullPos = newOrder.indexOf(cat);
            if (fullPos < 0) return;
            if (picked == actTop) {
                newOrder.removeAt(fullPos);
                newOrder.prepend(cat);
            } else if (picked == actUp && fullPos > 0) {
                newOrder.swapItemsAt(fullPos, fullPos - 1);
            } else if (picked == actDown && fullPos < newOrder.size() - 1) {
                newOrder.swapItemsAt(fullPos, fullPos + 1);
            } else if (picked == actBot) {
                newOrder.removeAt(fullPos);
                newOrder.append(cat);
            }
        }
        if (newOrder == oldOrder) return;

        if (m_canvas && m_canvas->undoStack())
            m_canvas->undoStack()->push(
                new ReorderCategoriesCommand(m_layer, oldOrder, newOrder));
        else
            m_layer->setCategoryOrder(newOrder);
        return;
    }

    // ── Leaf row ─────────────────────────────────────────────────────────
    const SWMMObjectRef ref = refForProxyIndex(proxyIdx);

    // Plot entry layout depends on how many SWMM Output (.out) layers are
    // loaded on the canvas: 0 → no entry, 1 → flat action emitting the
    // implicit-pick signal, ≥2 → submenu listing each results layer so the
    // user picks which .out to plot against before the variable picker.
    QList<SWMMResultsLayer *> resultsLayers;
    if (m_canvas) {
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            if (auto *r = qobject_cast<SWMMResultsLayer *>(l))
                resultsLayers.push_back(r);
        }
    }

    QAction *actPlot = nullptr;
    QList<QAction *> resultsActs;   // one per results layer when submenu in use
    if (ref.objectType == SWMMObjectRef::Node
        || ref.objectType == SWMMObjectRef::Link
        || ref.objectType == SWMMObjectRef::Subcatchment)
    {
        if (resultsLayers.size() <= 1) {
            actPlot = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Chart")),
                                     tr("Plot Time Series…"));
        } else {
            QMenu *sub = menu.addMenu(QIcon(QStringLiteral(":/swmmvis/Chart")),
                                       tr("Plot Time Series"));
            for (SWMMResultsLayer *r : resultsLayers) {
                QAction *a = sub->addAction(r->name());
                a->setData(QVariant::fromValue(static_cast<void *>(r)));
                resultsActs.push_back(a);
            }
        }
    }
    QAction *actZoom = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                      tr("Zoom to Object"));
    actZoom->setEnabled(!m_canvas.isNull());
    menu.addSeparator();
    QAction *actSort  = menu.addAction(tr("Sort Category A→Z"));
    QAction *actReset = menu.addAction(tr("Reset Category to Default Order"));

    // Delete… — spatial objects only for now (node/link/subcatchment/gage),
    // routed through the same undoable DeleteObjectCommand path the map's
    // right-click delete and the attribute table use, so behaviour is
    // identical regardless of where the delete starts. Data-object leaves
    // (curves, patterns, …) get no Delete yet — pending the generic
    // DeleteDataObjectCommand + engine delete APIs (see
    // workplans/GUI_DELETE_ALL_OBJECTS_PLAN_2026-07-22.md).
    DeleteObjectCommand::TargetKind delKind = DeleteObjectCommand::DeleteNode;
    bool deletable = false;
    switch (ref.objectType) {
    case SWMMObjectRef::Node:         delKind = DeleteObjectCommand::DeleteNode;     deletable = true; break;
    case SWMMObjectRef::Link:         delKind = DeleteObjectCommand::DeleteLink;     deletable = true; break;
    case SWMMObjectRef::Subcatchment: delKind = DeleteObjectCommand::DeleteSubcatch; deletable = true; break;
    case SWMMObjectRef::RainGage:     delKind = DeleteObjectCommand::DeleteGage;     deletable = true; break;
    default: break;
    }
    QAction *actDelete = nullptr;
    if (deletable && m_layer) {
        menu.addSeparator();
        actDelete = menu.addAction(tr("Delete…"));
    }

    QAction *picked = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (!picked) return;

    if (picked == actPlot) {
        emit plotTimeSeriesRequested(ref);
        return;
    }
    if (resultsActs.contains(picked)) {
        auto *layer = static_cast<SWMMResultsLayer *>(picked->data().value<void *>());
        emit plotTimeSeriesForLayerRequested(ref, layer);
        return;
    }
    if      (picked == actZoom)  zoomToObject(ref);
    else if (picked == actSort)  sortCategoryAlphabetically(cat);
    else if (picked == actReset) {
        QVector<int> old = m_layer->objectOrder(cat);
        if (m_canvas && m_canvas->undoStack() && !old.isEmpty())
            m_canvas->undoStack()->push(
                new ReorderObjectsCommand(m_layer, cat, old, {}));
        m_layer->clearObjectOrder(cat);
    }
    else if (picked == actDelete && actDelete) {
        const QString msg = tr("Delete \"%1\"?").arg(ref.name);
        if (QMessageBox::question(
                this, tr("Confirm Delete"), msg,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;

        // Drop the selection so the post-delete model refresh (driven by the
        // layer's geometryChanged) doesn't try to hold a row that no longer
        // exists. Clear the canonical bus, not just the tree's own selection
        // model — otherwise SelectionManager keeps a ref naming the deleted
        // object and the reverse bridge republishes it as a phantom.
        if (m_selMgr)
            m_selMgr->clear();
        else if (auto *sm = m_view->selectionModel())
            sm->clearSelection();

        if (m_canvas && m_canvas->undoStack()) {
            // Undoable path — a deleted node cascades its links inside
            // DeleteObjectCommand, exactly as the map / attribute-table
            // deletes do. One object, but the cascade is not one mutation:
            // a node with 40 incident links pays 40 link-spatial-grid
            // rebuilds inside applyNodeDelete without the bulk scope.
            auto *macro = new BulkEditCommand(m_layer, tr("Delete \"%1\"").arg(ref.name));
            new DeleteObjectCommand(m_layer, ref.name, delKind, m_canvas, macro);
            m_canvas->undoStack()->push(macro);
        } else if (m_layer) {
            // No canvas/undo stack (headless / tests): perform the same
            // mutation DeleteObjectCommand::redo() would, minus the record.
            SWMMModelLayer::BulkEdit guard(m_layer);
            switch (delKind) {
            case DeleteObjectCommand::DeleteNode:     m_layer->applyNodeDelete(ref.name);     break;
            case DeleteObjectCommand::DeleteLink:     m_layer->applyLinkDelete(ref.name);     break;
            case DeleteObjectCommand::DeleteGage:     m_layer->applyGageDelete(ref.name);     break;
            case DeleteObjectCommand::DeleteSubcatch: m_layer->applySubcatchDelete(ref.name); break;
            }
        }
    }
}

void ObjectBrowserPanel::onItemDoubleClicked(const QModelIndex &proxyIdx)
{
    const SWMMObjectRef ref = refForProxyIndex(proxyIdx);
    if (ref.objectType == SWMMObjectRef::Unknown || ref.name.isEmpty())
        return;

    // Non-spatial data leaves (TS / Curve / Pattern / Hydrograph / Transect /
    // Control) route through the shared open-for-edit helper so double-click
    // and the leaf right-click "Edit…" action use one code path. The helper
    // is a no-op for spatial refs — we fall through to zoom-to-object for
    // those.
    switch (ref.objectType) {
    case SWMMObjectRef::Hydrograph:
    case SWMMObjectRef::Curve:
    case SWMMObjectRef::TimePattern:
    case SWMMObjectRef::Transect:
    case SWMMObjectRef::Control:
    case SWMMObjectRef::TimeSeries:
        openComprehensiveEditorFor(
            m_layer,
            m_canvas ? m_canvas->undoStack() : nullptr,
            ref, this);
        return;
    default:
        break;
    }

    zoomToObject(ref);
}

// 2026-05-29 — Shared open-for-edit dispatch used by three surfaces: object
// browser leaf double-click, leaf right-click "Edit…", and the attribute
// panel's header "Open in <Editor>…" button. Each branch mirrors the
// dialog wiring the corresponding Slice (BS.6.9.2 / BQ.6.7.1-4 / BR.6.8.1
// / BQ.6.7.3.8) established for double-click. File-scope `QPointer`
// statics keep one dialog instance per editor kind alive across calls so
// switching surfaces re-uses the same window.
void ObjectBrowserPanel::openComprehensiveEditorFor(SWMMModelLayer    *layer,
                                                     QUndoStack        *undoStack,
                                                     const SWMMObjectRef &ref,
                                                     QWidget           *parent)
{
    if (!layer || ref.name.isEmpty()) return;

    // Slice BS Phase 6.9.2 — non-spatial Unit Hydrograph nodes don't have
    // map geometry, so zoom-to-object is a no-op. Route to the
    // HydrographGroupEditor (non-modal, MVC-synced) instead.
    if (ref.objectType == SWMMObjectRef::Hydrograph) {
        static QPointer<HydrographGroupEditor> editor;
        if (!editor) editor = new HydrographGroupEditor(layer, parent);
        editor->openForGroup(ref.name);
        return;
    }
    // Slice BQ Phase 6.7.1 — CURVE leaves open the CurveEditorDialog
    // (modeless, MVC). The layer owns the registry; Add-New
    // (Slice BM.0-Add-New) and open-for-edit share one instance.
    if (ref.objectType == SWMMObjectRef::Curve) {
        using openswmmvis::curve::CurveRegistry;
        using openswmmvis::ui::CurveEditorDialog;
        auto *reg = qobject_cast<CurveRegistry *>(layer->ensureCurveRegistry());
        if (!reg) return;
        static QPointer<CurveEditorDialog> editor;
        if (!editor) {
            editor = new CurveEditorDialog(reg, undoStack, parent);
        }
        editor->openForCurve(ref.name);
        return;
    }

    // Slice BQ Phase 6.7.2 — TIMEPATTERN leaves open the PatternEditorDialog
    // (modeless, MVC). Layer owns the registry; Add-New and open-for-edit
    // share one instance.
    if (ref.objectType == SWMMObjectRef::TimePattern) {
        using openswmmvis::pattern::PatternRegistry;
        using openswmmvis::ui::PatternEditorDialog;
        auto *reg = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
        if (!reg) return;
        // Single instance kept alive across calls so the user can flick
        // between patterns via the dialog's left-pane list.
        static QPointer<PatternEditorDialog> editor;
        if (!editor) {
            editor = new PatternEditorDialog(reg, undoStack, parent);
        }
        editor->openForPattern(ref.name);
        return;
    }

    // Slice BQ Phase 6.7.4 — TRANSECT leaves open the TransectEditorDialog
    // (modeless, MVC). Registry is owned by the layer; the dialog is kept
    // alive across calls so the user can flick between transects via the
    // left-pane list.
    if (ref.objectType == SWMMObjectRef::Transect) {
        using openswmmvis::transect::TransectRegistry;
        using openswmmvis::ui::TransectEditorDialog;
        auto *reg = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
        if (!reg) return;
        static QPointer<TransectEditorDialog> editor;
        if (!editor)
            editor = new TransectEditorDialog(reg, layer, undoStack, parent);
        editor->openForTransect(ref.name);
        return;
    }

    // Slice BR Phase 6.8.1 — CONTROL leaves open the RulesEditorDialog
    // (modeless, MVC). Layer owns the registry; reuses the dialog across
    // calls so the user can navigate rules via its left list.
    if (ref.objectType == SWMMObjectRef::Control) {
        using openswmmvis::ui::RulesEditorDialog;
        static QPointer<RulesEditorDialog> editor;
        if (!editor)
            editor = new RulesEditorDialog(layer, undoStack, parent);
        editor->openForRule(ref.name);
        return;
    }

    // Slice BQ Phase 6.7.3.8 — TIMESERIES leaves open the TimeseriesEditorDialog
    // (modeless, MVC). Layer owns the registry; Add-New (Slice BM.0-Add-New)
    // and open-for-edit share one instance.
    if (ref.objectType == SWMMObjectRef::TimeSeries) {
        using openswmmvis::timeseries::TimeseriesRegistry;
        using openswmmvis::timeseries::TimeseriesProvider;
        using openswmmvis::ui::TimeseriesEditorDialog;

        auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
        if (!reg) return;

        TimeseriesProvider *p = reg->findByName(ref.name);
        if (!p) {
            // Engine has it but registry didn't load — recreate empty so the
            // editor at least opens and the user can see the rejection state.
            p = reg->create(ref.name);
            if (!p) return;
        }

        auto *dlg = new TimeseriesEditorDialog(reg, undoStack, p, parent);
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        // Phase 6.7.3.7 — auto-flush inline providers to the engine when the
        // editor closes. The registry remembers its bound engine handle from
        // the most recent loadFromEngine() call, so the no-arg overload is
        // sufficient here. This makes edits round-trip to .inp without the
        // user having to explicitly "Save Project" first.
        QPointer<TimeseriesRegistry> regPtr(reg);
        QObject::connect(dlg, &QDialog::finished, dlg, [regPtr]() {
            if (regPtr) regPtr->saveToEngine();
        });

        dlg->show();
        return;
    }
}

void ObjectBrowserPanel::zoomToObject(const SWMMObjectRef &ref)
{
    if (!m_canvas || !m_layer) return;

    // Unified across all object kinds: the layer returns the feature's
    // layer-CRS bounding box (single point for nodes / gages, polyline
    // bbox for links, polygon bbox for subcatchments). Anything that
    // previously fell off the identifyByName() X/Y path — subcatchments
    // and multi-vertex links without centred X/Y attrs — is now covered.
    // Unknown name → NaN-sentinel extent; finiteness check rejects it.
    const MapExtent obj = m_canvas->extentInCanvasCRS(
        m_layer, m_layer->objectExtent(ref.name));
    if (!std::isfinite(obj.xMin()) || !std::isfinite(obj.yMin())
        || !std::isfinite(obj.xMax()) || !std::isfinite(obj.yMax()))
        return;

    // Pad the object's bbox so it fills most of the viewport rather than
    // landing on the exact edge. Areal features (links / subcatchments)
    // get a 25 % pad relative to their own size. Point features
    // (objectExtent returns width==height==0) get an absolute buffer
    // derived from the layer's overall extent so the zoom scale is
    // comparable across CRS units.
    double x0 = obj.xMin(), y0 = obj.yMin();
    double x1 = obj.xMax(), y1 = obj.yMax();
    const bool isPoint = (obj.width() == 0.0 && obj.height() == 0.0);

    if (isPoint)
    {
        double buffer = 100.0;
        if (const MapExtent le = m_canvas->layerExtentInCanvasCRS(m_layer); le.isValid())
        {
            const double dx = le.xMax() - le.xMin();
            const double dy = le.yMax() - le.yMin();
            buffer = std::max(25.0, 0.005 * std::max(dx, dy));
        }
        x0 -= buffer; y0 -= buffer;
        x1 += buffer; y1 += buffer;
    }
    else
    {
        const double padX = std::max(1e-6, obj.width()  * 0.25);
        const double padY = std::max(1e-6, obj.height() * 0.25);
        x0 -= padX; y0 -= padY;
        x1 += padX; y1 += padY;
    }

    MapExtent zoom(x0, y0, x1, y1);
    if (zoom.isValid())
        m_canvas->setExtent(zoom);
}

// ---------------------------------------------------------------------------
// Selection ↔ bus
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::onTreeSelectionChanged()
{
    if (m_applyingFromBus || !m_selMgr) return;

    QSet<SWMMObjectRef> refs;
    const QModelIndexList picks = m_view->selectionModel()->selectedIndexes();
    for (const QModelIndex &proxyIdx : picks) {
        if (proxyIdx.column() != 0) continue; // each row lands once
        const SWMMObjectRef r = refForProxyIndex(proxyIdx);
        if (r.objectType != SWMMObjectRef::Unknown && !r.name.isEmpty())
            refs.insert(r);
    }
    m_selMgr->select(refs, SelectionManager::Replace);
}

void ObjectBrowserPanel::onSelectionManagerChanged(
    const QSet<SWMMObjectRef> &current,
    const QSet<SWMMObjectRef> & /*added*/,
    const QSet<SWMMObjectRef> & /*removed*/)
{
    if (!m_view || !m_model) return;

    m_applyingFromBus = true;
    auto *sm = m_view->selectionModel();

    // Tree-sync threshold. Even with the batched QItemSelection apply
    // below, building + applying ~80k single-row ranges costs tens of
    // seconds (QItemSelectionModel range coalescing scales worse than
    // linear). At those sizes the user can't visually see the tree
    // selection anyway — every category gets fully selected and the
    // browser becomes a wall of highlight. So above this threshold,
    // just clear the tree's selection: the canvas + selection-count
    // remain authoritative, and the fast-path is preserved on bulk
    // rubber-band selects across big models. Lower the threshold if
    // 5000-row sync ever feels sluggish.
    constexpr int kTreeSyncMaxRows = 5000;
    if (current.size() > kTreeSyncMaxRows) {
        sm->clearSelection();
        m_applyingFromBus = false;
        return;
    }

    // Build the new selection as a single QItemSelection (one range per
    // row), then apply it in one ClearAndSelect call. The previous loop
    // called sm->select() per row, which emitted selectionChanged once
    // per call and made selection-sync O(K²) on large selections (3.6k
    // rows ≈ 2 s). Same for expand(): the per-row call hammered the
    // viewport layout, so collect unique parents and expand them once.
    QItemSelection sel;
    QSet<QModelIndex> parentsToExpand;
    QModelIndex firstProxyIdx;
    for (const SWMMObjectRef &r : current)
    {
        const QModelIndex src = m_model->indexFor(r);
        if (!src.isValid()) continue;
        const QModelIndex proxy = m_proxy->mapFromSource(src);
        if (!proxy.isValid()) continue;
        sel.select(proxy, proxy);
        if (const QModelIndex parent = proxy.parent(); parent.isValid())
            parentsToExpand.insert(parent);
        if (!firstProxyIdx.isValid()) firstProxyIdx = proxy;
    }
    sm->select(sel, QItemSelectionModel::ClearAndSelect
                  | QItemSelectionModel::Rows);
    for (const QModelIndex &p : parentsToExpand)
        m_view->expand(p);
    if (firstProxyIdx.isValid())
        m_view->scrollTo(firstProxyIdx);
    m_applyingFromBus = false;
}

void ObjectBrowserPanel::onSearchTextChanged(const QString & /*text*/)
{
    if (m_filterDebounce)
        m_filterDebounce->start();
}

void ObjectBrowserPanel::applyFilterNow()
{
    const QString trimmed = m_searchEdit ? m_searchEdit->text().trimmed()
                                         : QString{};
    m_proxy->setFilterRegularExpression(
        QRegularExpression(QRegularExpression::escape(trimmed),
                           QRegularExpression::CaseInsensitiveOption));
    // Active filter → force categories open so matching leaves are
    // visible (the recursive proxy otherwise leaves them tucked under a
    // collapsed parent). Cleared filter → collapse everything; user
    // re-expands manually.
    if (!trimmed.isEmpty())
        m_view->expandAll();
    else
        m_view->collapseAll();
}

// ---------------------------------------------------------------------------
// Sorting helper (Slice AY)
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::sortCategoryAlphabetically(SWMMModelLayer::Category cat)
{
    if (!m_layer) return;
    const int n = m_layer->categoryCount(cat);
    if (n <= 1) return;

    // Collect (displayRow → name) using the current display order so that
    // objectNameAt(cat, i) already reflects any prior override.
    QVector<QPair<int, QString>> pairs(n);
    for (int i = 0; i < n; ++i)
        pairs[i] = {i, m_layer->objectNameAt(cat, i)};

    std::sort(pairs.begin(), pairs.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  return a.second < b.second;
              });

    // Map sorted display positions back to SoA indices.
    QVector<int> soaOrder = m_layer->objectOrder(cat);
    if (soaOrder.size() != n)
        soaOrder = m_layer->defaultObjectOrder(cat);

    QVector<int> newSoaOrder(n);
    for (int i = 0; i < n; ++i)
        newSoaOrder[i] = soaOrder[pairs[i].first];

    if (newSoaOrder == soaOrder) return;   // already sorted

    if (m_canvas && m_canvas->undoStack())
        m_canvas->undoStack()->push(
            new ReorderObjectsCommand(m_layer, cat, soaOrder, newSoaOrder));
    else
        m_layer->setObjectOrder(cat, newSoaOrder);
}

// ---------------------------------------------------------------------------
// Slice BM.0-Browse-Edit (2026-05-25) — three-surface dispatch consolidated
// behind ComprehensiveEditorRegistry. The static helpers below stay as
// thin forwarders so the five external callers (swmmvis.cpp x2,
// nodecompoundeditdialog.cpp, dataobjectpickereditor.cpp x2) keep
// compiling unchanged.
// ---------------------------------------------------------------------------

bool ObjectBrowserPanel::hasComplexEditor(SWMMModelLayer::DataCategory dc) noexcept
{
    return ComprehensiveEditorRegistry::instance().hasEditor(dc);
}

QString ObjectBrowserPanel::gapTooltipFor(SWMMModelLayer::DataCategory dc)
{
    return ComprehensiveEditorRegistry::instance().gapTooltip(dc);
}

// 2026-05-29 — `ensureTimeseriesRegistry_` / `ensurePatternRegistry_` /
// `ensureCurveRegistry_` were forwarders used by the previous double-click
// body. `openComprehensiveEditorFor` now calls layer->ensureXxxRegistry()
// directly (it's static and has no `m_layer`), so the forwarders became
// orphans of this refactor and were removed alongside their declarations.

void ObjectBrowserPanel::launchAddNewEditor(SWMMModelLayer::DataCategory dc)
{
    if (!m_layer) return;
    const auto *entry = ComprehensiveEditorRegistry::instance().find(dc);
    if (!entry || !entry->openCreateNew) {
        Q_ASSERT_X(false, "ObjectBrowserPanel::launchAddNewEditor",
                   "gap category dispatched — Add-New should have been disabled");
        return;
    }
    QUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;
    entry->openCreateNew(m_layer, stack, this);
}

void ObjectBrowserPanel::launchBrowseEditor(SWMMModelLayer::DataCategory dc)
{
    if (!m_layer) return;
    const auto *entry = ComprehensiveEditorRegistry::instance().find(dc);
    if (!entry || !entry->openBrowse) return;
    QUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;
    entry->openBrowse(m_layer, stack, this);
}

