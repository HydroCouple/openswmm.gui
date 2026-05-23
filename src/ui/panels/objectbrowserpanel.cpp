/*!
 * \file   objectbrowserpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/swmmobjecttreemodel.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/mapundostack.h"

#include <cmath>

#include <QDebug>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStringList>
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

// Slice DA.3 — typed New Data Object dialog (replaces the bare
// QInputDialog::getText flow that hardcoded one subtype per category).
#include "ui/dialogs/newdataobjectdialog.h"

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

/*! QSettings group root for per-project Object Browser expansion state.
 *  Keys are nested as
 *      ObjectBrowser/expansion/<sanitised file path>/(network|data) */
constexpr auto kSettingsGroup = "ObjectBrowser/expansion";

/*! Translate a model file path into a settings-safe key. QSettings
 *  uses '/' as group separator, so we percent-encode the path to keep
 *  it on a single leaf. */
QString settingsKeyForLayer(const QString &modelFilePath)
{
    if (modelFilePath.isEmpty()) return {};
    return QString::fromLatin1(modelFilePath.toUtf8().toPercentEncoding());
}

} // anonymous

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ObjectBrowserPanel::ObjectBrowserPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

ObjectBrowserPanel::~ObjectBrowserPanel()
{
    // Capture whatever the user has open right now and flush it before
    // the panel goes away — covers app close, dock detach, MDI teardown.
    snapshotExpansion();
    savePersistedExpansion();
}

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

    // Capture the outgoing project's expansion state before we rebind.
    // We persist immediately so a crash before close still leaves the
    // user's preferred groups recorded.
    if (m_layer) {
        snapshotExpansion();
        savePersistedExpansion();
    }

    if (m_selMgr)
        QObject::disconnect(m_selMgr, &SelectionManager::selectionChanged,
                            this,     &ObjectBrowserPanel::onSelectionManagerChanged);

    if (m_layer)
        QObject::disconnect(m_layer, &SWMMModelLayer::geometryChanged,
                            this,    &ObjectBrowserPanel::refresh);

    m_layer  = layer;
    m_selMgr = selMgr;
    m_canvas = canvas;

    // Seed the in-memory sets from disk for the new project — empty if
    // never visited. refresh() will reapply them after reload().
    m_expandedCategories.clear();
    m_expandedDataCategories.clear();
    if (m_layer)
        loadPersistedExpansion();

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

void ObjectBrowserPanel::refresh()
{
    // Block the tree's selection-change signal from propagating to the
    // SelectionManager while the model resets. Without this, endResetModel()
    // clears the QItemSelectionModel, which fires onTreeSelectionChanged()
    // with an empty set, calling m_selMgr->select({}, Replace) — wiping the
    // global selection and removing the canvas highlight for every object.
    // blockSignals() is scoped to the selection model only, so it has no
    // side-effects on canvas rendering, unlike the broader m_applyingFromBus.
    // Keep categories collapsed on (re)load so 1M-object projects don't pay
    // the per-leaf viewport-layout cost up-front. SWMMObjectTreeModel only
    // materialises rows that the view actually paints, so a collapsed tree
    // means zero leaf data() calls until the user expands a category.
    // Parents of bus-selected items are re-expanded below via
    // onSelectionManagerChanged().
    //
    // Before the reset, capture whatever the user has open so we can
    // re-expand the same enum-keyed categories afterwards — drag-drop
    // reordering moves rows around but the Category / DataCategory enum
    // is stable.
    snapshotExpansion();

    auto *sm = m_view->selectionModel();
    sm->blockSignals(true);
    m_model->reload();
    sm->blockSignals(false);

    restoreExpansion();

    // Restore the tree's visual selection to match the SelectionManager.
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
            // Group header — "Add New <Type>…" is the only fully-wired
            // action today. Rename / Duplicate / Delete are reserved for
            // BN/BO/BP editor slices, which ship the engine setters /
            // mutators these actions need.
            QAction *actAdd = dmenu.addAction(QIcon(QStringLiteral(":/swmmvis/Layers")),
                                              tr("Add New…"));
            QAction *picked = dmenu.exec(m_view->viewport()->mapToGlobal(pos));
            if (!picked) return;
            if (picked == actAdd) {
                // Slice DA.3 — typed mini-dialog with auto-suggested
                // name and per-type subtype combo, replacing the bare
                // QInputDialog (which hardcoded subtypes per category).
                auto spec = NewDataObjectDialog::getNew(dc, m_layer, this);
                if (!spec) return;
                addNewDataObject(spec->category, spec->name, spec->options);
            }
            return;
        }

        // Leaf — Properties stub (registry returns nullptr today; the
        // legacy AttributePanel property-tree path still drives editing
        // of pollutants / land-uses via existing Z.5 delegates).
        QAction *actProps = dmenu.addAction(tr("Properties…"));
        QAction *picked = dmenu.exec(m_view->viewport()->mapToGlobal(pos));
        if (picked == actProps) {
            // Future: dispatch through PropertyEditorRegistry. For now
            // this just emits the existing selection signal so the
            // attribute panel reflects the click.
            const SWMMObjectRef ref = refForProxyIndex(proxyIdx);
            if (m_selMgr && ref.objectType != SWMMObjectRef::Unknown)
                m_selMgr->select(ref, SelectionManager::Replace);
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
    QAction *actPlot = nullptr;
    if (ref.objectType == SWMMObjectRef::Node
        || ref.objectType == SWMMObjectRef::Link
        || ref.objectType == SWMMObjectRef::Subcatchment)
    {
        actPlot = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Chart")),
                                 tr("Plot Time Series…"));
    }
    QAction *actZoom = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                      tr("Zoom to Object"));
    actZoom->setEnabled(!m_canvas.isNull());
    menu.addSeparator();
    QAction *actSort  = menu.addAction(tr("Sort Category A→Z"));
    QAction *actReset = menu.addAction(tr("Reset Category to Default Order"));

    QAction *picked = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (!picked) return;

    if      (picked == actPlot)  emit plotTimeSeriesRequested(ref);
    else if (picked == actZoom)  zoomToObject(ref);
    else if (picked == actSort)  sortCategoryAlphabetically(cat);
    else if (picked == actReset) {
        QVector<int> old = m_layer->objectOrder(cat);
        if (m_canvas && m_canvas->undoStack() && !old.isEmpty())
            m_canvas->undoStack()->push(
                new ReorderObjectsCommand(m_layer, cat, old, {}));
        m_layer->clearObjectOrder(cat);
    }
}

void ObjectBrowserPanel::onItemDoubleClicked(const QModelIndex &proxyIdx)
{
    const SWMMObjectRef ref = refForProxyIndex(proxyIdx);
    if (ref.objectType == SWMMObjectRef::Unknown || ref.name.isEmpty())
        return;
    zoomToObject(ref);
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

void ObjectBrowserPanel::onSearchTextChanged(const QString &text)
{
    // Capture the user's expansion state on the rising edge — i.e. the
    // first non-empty keystroke after a clear. Doing this here (rather
    // than inside the debounced applyFilterNow()) makes sure we snapshot
    // *before* expandAll() forces every category open and overwrites the
    // user's intent.
    const bool willBeActive = !text.trimmed().isEmpty();
    if (!m_filterActive && willBeActive)
        snapshotExpansion();
    m_filterActive = willBeActive;

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
    // collapsed parent). Cleared filter → collapse back to the panel's
    // default lazy state, then restore whatever the user had open
    // before filtering started.
    if (!trimmed.isEmpty()) {
        m_view->expandAll();
    } else {
        m_view->collapseAll();
        restoreExpansion();
    }
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
// Slice BM.0 — Add New <Type> dispatch
// ---------------------------------------------------------------------------

namespace {

/*! \brief Build a canonical RULE skeleton text body for the four
 *         pre-canned templates surfaced by NewDataObjectDialog's
 *         "skeleton" combo. The user can edit the body afterwards in
 *         AttributePanel (DA.2 ControlRule adapter) or the BR
 *         RulesEditorDialog. */
QString buildRuleSkeleton(const QString &skeleton, const QString &name)
{
    if (skeleton == QLatin1String("pump"))
        return QStringLiteral(
            "RULE %1\n"
            "IF NODE J1 DEPTH > 5.0\n"
            "THEN PUMP P1 STATUS = ON").arg(name);
    if (skeleton == QLatin1String("orifice"))
        return QStringLiteral(
            "RULE %1\n"
            "IF NODE J1 DEPTH > 5.0\n"
            "THEN ORIFICE O1 SETTING = 0.5").arg(name);
    if (skeleton == QLatin1String("weir"))
        return QStringLiteral(
            "RULE %1\n"
            "IF LINK W1 FLOW > 10.0\n"
            "THEN WEIR W1 SETTING = 0\n"
            "ELSE WEIR W1 SETTING = 1").arg(name);
    // "empty" or unknown → headerless template; the user fills it in.
    return QStringLiteral("RULE %1\n").arg(name);
}

} // namespace

void ObjectBrowserPanel::addNewDataObject(SWMMModelLayer::DataCategory dc,
                                          const QString &name,
                                          const QVariantMap &options)
{
    if (!m_layer) return;
    SWMM_Engine eng = m_layer->engine();
    if (!eng) {
        qWarning() << "ObjectBrowserPanel::addNewDataObject:"
                   << "no engine handle";
        return;
    }
    const QByteArray utf = name.toUtf8();
    const char     *idC = utf.constData();

    // Per-type defaults match the legacy SWMM 5 first-entry-per-type
    // fallback when the dialog page didn't surface a control for the
    // value (e.g. tests calling addNewDataObject(...) directly).
    constexpr int kCurveTypeStorage     = 0;
    constexpr int kPatternTypeMonthly   = 0;
    constexpr int kLidTypeBioCell       = 0;
    constexpr int kPollutantUnitsMgPerL = 0;

    int rc = -1;
    switch (dc) {
    case SWMMModelLayer::DataCurves: {
        const int t = options.value(QStringLiteral("curveType"),
                                     kCurveTypeStorage).toInt();
        rc = swmm_curve_add(eng, idC, t);
        break;
    }
    case SWMMModelLayer::DataTimeSeries:
        rc = swmm_timeseries_add(eng, idC);
        break;
    case SWMMModelLayer::DataPatterns: {
        const int t = options.value(QStringLiteral("patternType"),
                                     kPatternTypeMonthly).toInt();
        rc = swmm_pattern_add(eng, idC, t);
        break;
    }
    case SWMMModelLayer::DataLIDControls: {
        const int t = options.value(QStringLiteral("lidType"),
                                     kLidTypeBioCell).toInt();
        rc = swmm_lid_add(eng, idC, t);
        break;
    }
    case SWMMModelLayer::DataPollutants: {
        const int u = options.value(QStringLiteral("units"),
                                     kPollutantUnitsMgPerL).toInt();
        rc = swmm_pollutant_add(eng, idC, u);
        break;
    }
    case SWMMModelLayer::DataLandUses:
        rc = swmm_landuse_add(eng, idC);
        break;
    case SWMMModelLayer::DataAquifers:
        rc = swmm_aquifer_add(eng, idC);
        break;
    case SWMMModelLayer::DataSnowpacks:
        rc = swmm_snowpack_add(eng, idC);
        break;
    case SWMMModelLayer::DataTransects:
        rc = swmm_transect_add(eng, idC);
        break;
    case SWMMModelLayer::DataStreets:
        rc = swmm_street_add(eng, idC);
        break;
    case SWMMModelLayer::DataInlets: {
        // Inlet "type" is a string (GRATE / CURB / SLOTTED / CUSTOM);
        // legacy default is GRATE.
        const QString t = options.value(QStringLiteral("inletType"),
                                          QStringLiteral("GRATE")).toString();
        const QByteArray tu = t.toUtf8();
        rc = swmm_inlet_add(eng, idC, tu.constData());
        break;
    }
    case SWMMModelLayer::DataControls: {
        // DA.3 — insert a canonical RULE skeleton via the engine's
        // `swmm_control_add_rule` text-only entry point. The skeleton
        // surfaces `RULE <name>` so DA.1's `swmm_control_get_id` parses
        // the name back out immediately.
        const QString skeleton = options.value(QStringLiteral("skeleton"),
                                                 QStringLiteral("empty")).toString();
        const QByteArray body = buildRuleSkeleton(skeleton, name).toUtf8();
        rc = swmm_control_add_rule(eng, body.constData());
        break;
    }
    case SWMMModelLayer::DataHydrographs: {
        // DA.3 — establish the group via a gage-assignment + one
        // placeholder parameter row (month = ALL, response = user
        // pick, R/T/K = 0/0/0). User fills in the real values via
        // AttributePanel (DA.2 monthly-summary) or the BS
        // HydrographGroupEditor later.
        const QString gage = options.value(QStringLiteral("rainGage"))
                                    .toString().trimmed();
        const int     response = options.value(QStringLiteral("response"), 0).toInt();
        if (!gage.isEmpty()) {
            const QByteArray gu = gage.toUtf8();
            swmm_hydrograph_add_gage(eng, idC, gu.constData());
        }
        rc = swmm_hydrograph_add(eng, idC, -1 /*ALL*/, response,
                                  0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        break;
    }
    default:
        return;
    }

    if (rc != 0 /*SWMM_OK*/) {
        qWarning() << "ObjectBrowserPanel::addNewDataObject: engine returned"
                   << rc << "for" << name;
        return;
    }
    refresh();

    // Slice DA.3 — auto-select the newly-created object so the
    // AttributePanel hydrates immediately, mirroring the spatial
    // create-and-select UX in the map tools.
    using K = SWMMObjectRef::ObjectType;
    auto refKindFor = [](SWMMModelLayer::DataCategory c) -> K {
        switch (c) {
        case SWMMModelLayer::DataCurves:      return K::Curve;
        case SWMMModelLayer::DataTimeSeries:  return K::TimeSeries;
        case SWMMModelLayer::DataPatterns:    return K::TimePattern;
        case SWMMModelLayer::DataLIDControls: return K::LIDControl;
        case SWMMModelLayer::DataPollutants:  return K::Pollutant;
        case SWMMModelLayer::DataLandUses:    return K::LandUse;
        case SWMMModelLayer::DataAquifers:    return K::Aquifer;
        case SWMMModelLayer::DataSnowpacks:   return K::Snowpack;
        case SWMMModelLayer::DataControls:    return K::Control;
        case SWMMModelLayer::DataTransects:   return K::Transect;
        case SWMMModelLayer::DataHydrographs: return K::Hydrograph;
        case SWMMModelLayer::DataStreets:     return K::Street;
        case SWMMModelLayer::DataInlets:      return K::Inlet;
        default:                              return K::Unknown;
        }
    };
    if (m_selMgr) {
        const SWMMObjectRef ref{refKindFor(dc), name};
        if (ref.objectType != K::Unknown)
            m_selMgr->select(ref, SelectionManager::Replace);
    }
}

// ---------------------------------------------------------------------------
// Expansion state — snapshot/restore + QSettings persistence
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::snapshotExpansion()
{
    if (!m_view || !m_model || !m_proxy) return;

    m_expandedCategories.clear();
    m_expandedDataCategories.clear();

    const int topRows = m_proxy->rowCount({});
    for (int row = 0; row < topRows; ++row) {
        const QModelIndex proxyIdx = m_proxy->index(row, 0, {});
        if (!proxyIdx.isValid() || !m_view->isExpanded(proxyIdx))
            continue;

        const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
        if (!srcIdx.isValid()) continue;

        const int section = m_model->data(srcIdx,
                                SWMMObjectTreeModel::RoleSection).toInt();
        if (section == int(SWMMObjectTreeModel::SectionNetwork)) {
            const int cat = m_model->data(srcIdx,
                                SWMMObjectTreeModel::RoleCategory).toInt();
            if (cat >= 0 && cat < int(SWMMModelLayer::NumCategories))
                m_expandedCategories.insert(cat);
        } else if (section == int(SWMMObjectTreeModel::SectionData)) {
            const int dc = m_model->data(srcIdx,
                                SWMMObjectTreeModel::RoleDataCategory).toInt();
            if (dc >= 0 && dc < int(SWMMModelLayer::NumDataCategories))
                m_expandedDataCategories.insert(dc);
        }
    }
}

void ObjectBrowserPanel::restoreExpansion()
{
    if (!m_view || !m_model || !m_proxy) return;
    if (m_expandedCategories.isEmpty() && m_expandedDataCategories.isEmpty())
        return;

    for (int catInt : m_expandedCategories) {
        const auto cat = static_cast<SWMMModelLayer::Category>(catInt);
        const int topRow = m_model->topRowForCategory(cat);
        if (topRow < 0) continue; // category empty / hidden in this project
        const QModelIndex srcIdx = m_model->index(topRow, 0, {});
        const QModelIndex proxyIdx = m_proxy->mapFromSource(srcIdx);
        if (proxyIdx.isValid())
            m_view->expand(proxyIdx);
    }

    for (int dcInt : m_expandedDataCategories) {
        const auto dc = static_cast<SWMMModelLayer::DataCategory>(dcInt);
        const int topRow = m_model->topRowForDataCategory(dc);
        if (topRow < 0) continue;
        const QModelIndex srcIdx = m_model->index(topRow, 0, {});
        const QModelIndex proxyIdx = m_proxy->mapFromSource(srcIdx);
        if (proxyIdx.isValid())
            m_view->expand(proxyIdx);
    }
}

void ObjectBrowserPanel::loadPersistedExpansion()
{
    if (!m_layer) return;
    const QString key = settingsKeyForLayer(m_layer->modelFilePath());
    if (key.isEmpty()) return;

    QSettings s;
    s.beginGroup(QString::fromLatin1(kSettingsGroup));
    s.beginGroup(key);
    const QStringList net  = s.value(QStringLiteral("network")).toStringList();
    const QStringList data = s.value(QStringLiteral("data")).toStringList();
    s.endGroup();
    s.endGroup();

    for (const QString &v : net) {
        bool ok = false;
        const int c = v.toInt(&ok);
        if (ok && c >= 0 && c < int(SWMMModelLayer::NumCategories))
            m_expandedCategories.insert(c);
    }
    for (const QString &v : data) {
        bool ok = false;
        const int c = v.toInt(&ok);
        if (ok && c >= 0 && c < int(SWMMModelLayer::NumDataCategories))
            m_expandedDataCategories.insert(c);
    }
}

void ObjectBrowserPanel::savePersistedExpansion()
{
    if (!m_layer) return;
    const QString key = settingsKeyForLayer(m_layer->modelFilePath());
    if (key.isEmpty()) return;

    QStringList net, data;
    net.reserve(m_expandedCategories.size());
    data.reserve(m_expandedDataCategories.size());
    for (int c : m_expandedCategories)     net  << QString::number(c);
    for (int c : m_expandedDataCategories) data << QString::number(c);

    QSettings s;
    s.beginGroup(QString::fromLatin1(kSettingsGroup));
    s.beginGroup(key);
    s.setValue(QStringLiteral("network"), net);
    s.setValue(QStringLiteral("data"),    data);
    s.endGroup();
    s.endGroup();
}
