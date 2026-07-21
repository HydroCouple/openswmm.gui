/*!
 * \file   attributetablepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/attributetablepanel.h"
#include "ui/panels/attributedelegates.h"
#include "ui/models/userflagsmodel.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/panels/tabulardatatablemodel.h"
#include "ui/properties/dataobjectref.h"
#include "ui/properties/linkcompoundeditref.h"
#include "ui/properties/nodecompoundeditref.h"
#include "ui/properties/subcatchcompoundeditref.h"
#include "ui/properties/userflagseditref.h"   // per-object User Flags cell
#include "ui/editors/comprehensiveeditorregistry.h"
#include "ui/dialogs/curveeditordialog.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "ui/dialogs/linkcompoundeditdialog.h"
#include "ui/dialogs/nodecompoundeditdialog.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "curve/curveregistry.h"
#include "pattern/patternregistry.h"
#include "timeseries/timeseriesregistry.h"

#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>

#include "core/queryparser.h"
#include "ui/dialogs/typeconversionflow.h"
#include "layers/swmmmodellayer.h"
#include "layers/tabulardatalayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/mapundostack.h"

#include <QAction>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTextStream>
#include <QUndoStack>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>   // std::sort / std::unique — selectionAsTsv row ordering

Q_LOGGING_CATEGORY(lcAttrTbl, "openswmm.attr-table")

namespace {

// Slice Z.2 — proxy that composes the existing "show selected only"
// regex filter with a query-predicate filter.  A row is accepted
// when (a) the regex matches (when set), AND (b) the predicate
// evaluates true (when set).  Either filter being unset is a pass.
class FilteringProxy : public QSortFilterProxyModel {
public:
    explicit FilteringProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

    void setQueryPredicate(const openswmmvis::QueryPredicate &p) {
        m_predicate = p;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        // Existing regex-on-name filter (show-selected-only).
        if (!QSortFilterProxyModel::filterAcceptsRow(row, parent)) return false;
        if (!m_predicate.isValid()) return true;

        // Build a QVariantMap of this row's column-key → value pairs
        // so the predicate can reference any column by header.  Works
        // for both SWMMAttributeTableModel (column keys = ColumnSpec
        // keys) and TabularDataTableModel (column keys = CSV/TSV
        // headers).  Generic over any QAbstractItemModel.
        auto *src = sourceModel();
        if (!src) return true;
        QVariantMap m;
        const int nCol = src->columnCount();
        for (int c = 0; c < nCol; ++c) {
            const QString key = src->headerData(c, Qt::Horizontal,
                                                  Qt::DisplayRole).toString();
            const QModelIndex idx = src->index(row, c, parent);
            m.insert(key, src->data(idx, Qt::DisplayRole));
        }
        // SWMMAttributeTableModel also exposes the identifyByName-map
        // keys as columnSpecs keys, which match the labels.  For
        // backward compat keep those entries too so users can
        // type either spelling.
        if (auto *swmm = qobject_cast<SWMMAttributeTableModel *>(src)) {
            const auto specs = swmm->columnSpecs();
            for (int c = 0; c < specs.size(); ++c) {
                const QModelIndex idx = swmm->index(row, c, parent);
                m.insert(specs[c].key, swmm->data(idx, Qt::DisplayRole));
            }
        }
        return openswmmvis::evaluateQuery(m_predicate, m);
    }

private:
    openswmmvis::QueryPredicate m_predicate;
};

} // anonymous

namespace {

// Map Category → the SWMMObjectRef ObjectType the SelectionManager
// understands.  Nodes / Links collapse multiple categories into one
// selection-ref type because that's the unit `SelectionManager`
// tracks.
SWMMObjectRef::ObjectType objectTypeForCategory(SWMMModelLayer::Category cat)
{
    switch (cat) {
    case SWMMModelLayer::CatJunctions:
    case SWMMModelLayer::CatOutfalls:
    case SWMMModelLayer::CatStorage:
    case SWMMModelLayer::CatDividers:
        return SWMMObjectRef::Node;
    case SWMMModelLayer::CatConduits:
    case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:
    case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return SWMMObjectRef::Link;
    case SWMMModelLayer::CatSubcatchments:
        return SWMMObjectRef::Subcatchment;
    case SWMMModelLayer::CatRainGages:
        return SWMMObjectRef::RainGage;
    default:
        return SWMMObjectRef::Unknown;
    }
}

const char *categoryLabel(SWMMModelLayer::Category cat)
{
    switch (cat) {
    case SWMMModelLayer::CatJunctions:     return "Junctions";
    case SWMMModelLayer::CatOutfalls:      return "Outfalls";
    case SWMMModelLayer::CatStorage:       return "Storage";
    case SWMMModelLayer::CatDividers:      return "Dividers";
    case SWMMModelLayer::CatConduits:      return "Conduits";
    case SWMMModelLayer::CatPumps:         return "Pumps";
    case SWMMModelLayer::CatOrifices:      return "Orifices";
    case SWMMModelLayer::CatWeirs:         return "Weirs";
    case SWMMModelLayer::CatOutlets:       return "Outlets";
    case SWMMModelLayer::CatSubcatchments: return "Subcatchments";
    case SWMMModelLayer::CatRainGages:     return "Rain Gages";
    default:                                return "Unknown";
    }
}

} // anonymous

AttributeTablePanel::AttributeTablePanel(QWidget *parent)
    : QWidget(parent)
{
    // Register the compound-attribute metatype + QString converter so
    // the Compound delegate's displayText() can render the summary
    // string when a cell isn't in edit mode. Idempotent — the property
    // browser may have already done this, both calls are safe.
    qRegisterMetaType<NodeCompoundEditRef>("NodeCompoundEditRef");
    registerNodeCompoundEditRefConverter();
    // Phase 3 — subcatchment-side compound cell (land use / GW / LID).
    qRegisterMetaType<SubcatchCompoundEditRef>("SubcatchCompoundEditRef");
    registerSubcatchCompoundEditRefConverter();
    // §S.SC.1.c — link-side compound cell (XSection / InletUsage).
    // Registered alongside the node variant so the attribute table
    // can render link-compound summaries.
    qRegisterMetaType<LinkCompoundEditRef>("LinkCompoundEditRef");
    registerLinkCompoundEditRefConverter();
    // §S.SC.1.c — data-object pickers (curve / pattern / TS / UH /
    // pollutant / rain-gage). The right-click "Edit in …" dispatch
    // surfaces editors from the table for any cell carrying a
    // DataObjectRef variant.
    qRegisterMetaType<DataObjectRef>("DataObjectRef");
    registerDataObjectRefConverter();
    // ATTRIBUTE_EDITOR_WIRING follow-up (2026-06-04) — per-object
    // "User Flags" cell (Property Browser parity); the converter
    // renders the "n of m set" summary in non-edit cells.
    qRegisterMetaType<UserFlagsEditRef>("UserFlagsEditRef");
    registerUserFlagsEditRefConverter();

    buildUi();
}

AttributeTablePanel::~AttributeTablePanel()
{
    // Persist the column widths of the currently-active category one
    // last time so the next session opens with the same layout.
    if (m_model)
        saveColumnWidths(m_model->category());
}

void AttributeTablePanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // Top row: category combo + spacer + toolbar.  Combo drives which
    // category the model is bound to; toolbar carries actions that
    // operate on the current row selection.
    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(4, 4, 4, 2);
    topRow->addWidget(new QLabel(tr("Category:"), this));
    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    topRow->addWidget(m_categoryCombo);
    topRow->addStretch();

    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto *actShow   = m_toolbar->addAction(tr("Show selected only"));
    actShow->setCheckable(true);
    auto *actZoom   = m_toolbar->addAction(tr("Zoom to selected"));
    // Copy carries no shortcut of its own — Ctrl+C is owned by the main
    // window's actionCopy, which routes to the focused panel.  The hint in
    // the label is there so the binding is still discoverable.
    m_copyAct       = m_toolbar->addAction(tr("Copy (Ctrl+C)"));
    m_copyAct->setToolTip(tr("Copy the selected rows to the clipboard as "
                             "tab-separated text"));
    auto *actExport = m_toolbar->addAction(tr("Export CSV…"));
    topRow->addWidget(m_toolbar);

    root->addLayout(topRow);

    // Slice Z.3 — selection-mode radios + single Apply.  Round-4
    // follow-up 2026-05-12: the original five-button row was
    // confusing because each button both *chose* a mode and *fired*
    // it.  Splitting choice from action — radios on the same row +
    // one explicit Apply — makes the behavior obvious and cuts the
    // accidental-click failure mode.  Replace is the default mode
    // (matches the most-common "show me these rows" workflow).
    auto *selRow = new QHBoxLayout();
    selRow->setContentsMargins(4, 0, 4, 2);
    selRow->addWidget(new QLabel(tr("Selection:"), this));
    m_selReplaceR   = new QRadioButton(tr("Replace"),   this);
    m_selAddR       = new QRadioButton(tr("Add"),       this);
    m_selSubtractR  = new QRadioButton(tr("Subtract"),  this);
    m_selIntersectR = new QRadioButton(tr("Intersect"), this);
    m_selInvertR    = new QRadioButton(tr("Invert"),    this);
    m_selReplaceR  ->setToolTip(tr("Replace the current selection with the matched rows"));
    m_selAddR      ->setToolTip(tr("Union the matched rows into the current selection"));
    m_selSubtractR ->setToolTip(tr("Subtract the matched rows from the current selection"));
    m_selIntersectR->setToolTip(tr("Keep only rows that are in BOTH matched ∩ selection"));
    m_selInvertR   ->setToolTip(tr("Replace selection with all rows in this category that are NOT currently selected (ignores the query)"));
    m_selReplaceR->setChecked(true);

    m_selGroup = new QButtonGroup(this);
    m_selGroup->setExclusive(true);
    m_selGroup->addButton(m_selReplaceR,   SelReplace);
    m_selGroup->addButton(m_selAddR,       SelAdd);
    m_selGroup->addButton(m_selSubtractR,  SelSubtract);
    m_selGroup->addButton(m_selIntersectR, SelIntersect);
    m_selGroup->addButton(m_selInvertR,    SelInvert);
    for (auto *r : {m_selReplaceR, m_selAddR, m_selSubtractR,
                       m_selIntersectR, m_selInvertR})
        selRow->addWidget(r);

    selRow->addStretch();
    root->addLayout(selRow);

    // Slice Z.2 — query bar (WHERE clause + Apply / Clear + status).
    // Round-4 follow-up 2026-05-12: a single "Apply" button now does
    // BOTH the visible-row filter AND the selection op chosen on the
    // radio row above.  The old two-button design (Run Query vs Apply
    // Selection) was confusing — the user expected one Apply to do
    // the whole flow.  m_selApply has been merged into m_queryApply.
    auto *queryRow = new QHBoxLayout();
    queryRow->setContentsMargins(4, 0, 4, 2);
    queryRow->addWidget(new QLabel(tr("Query:"), this));
    m_queryEdit = new QLineEdit(this);
    m_queryEdit->setPlaceholderText(
        tr("e.g.  \"Max depth\" > 5   •   Name LIKE 'J%'   •   Type IN ('Junction','Outfall')"));
    m_queryEdit->setToolTip(tr(
        "Filter rows by a SQL-like WHERE clause.\n"
        "\n"
        "Column names:\n"
        "• Quote with double quotes (or [brackets]) when they contain spaces:\n"
        "    \"Invert elev\" > 100\n"
        "• Lookup is case-insensitive — \"max depth\" works.\n"
        "\n"
        "Values:\n"
        "• Numbers: 100, 3.14, -2\n"
        "• Strings: single-quoted — 'Junction'\n"
        "\n"
        "Comparison: = != < <= > >=\n"
        "\n"
        "LIKE — case-insensitive pattern match on a string column:\n"
        "    Name LIKE 'J%'      — names starting with J\n"
        "    Name LIKE '%-OUT'   — names ending with -OUT\n"
        "    Name LIKE 'J__'    — J followed by exactly two characters\n"
        "    %  matches any sequence (zero or more chars)\n"
        "    _  matches exactly one character\n"
        "\n"
        "IN — match any of a list of values:\n"
        "    Type IN ('Junction','Outfall')\n"
        "\n"
        "Combine with AND / OR / NOT, group with ( )."));
    m_queryEdit->setClearButtonEnabled(true);
    queryRow->addWidget(m_queryEdit, 1);
    m_queryApply = new QPushButton(tr("Apply"), this);
    m_queryApply->setDefault(false);
    m_queryApply->setToolTip(tr(
        "Filter the rows to those matching the query AND apply the chosen "
        "selection mode (radios above) to those rows."));
    m_queryClear = new QPushButton(tr("Clear"), this);
    m_queryClear->setToolTip(tr("Clear the query and show all rows"));
    queryRow->addWidget(m_queryApply);
    queryRow->addWidget(m_queryClear);
    m_queryStatus = new QLabel(this);
    m_queryStatus->setMinimumWidth(140);
    m_queryStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    queryRow->addWidget(m_queryStatus);
    root->addLayout(queryRow);

    connect(m_queryEdit,  &QLineEdit::returnPressed,
            this, &AttributeTablePanel::onQueryApplyClicked);
    connect(m_queryApply, &QPushButton::clicked,
            this, &AttributeTablePanel::onQueryApplyClicked);
    connect(m_queryClear, &QPushButton::clicked,
            this, &AttributeTablePanel::onQueryClearClicked);

    m_model        = new SWMMAttributeTableModel(this);
    m_tabularModel = new TabularDataTableModel(this);
    m_proxy        = new FilteringProxy(this);
    m_proxy->setSourceModel(m_model);

    // Round-4 follow-up 2026-05-12 — when the flow-units system
    // flips (US ↔ SI) the model emits headerDataChanged and the
    // header strings change length ("Invert Elevation (ft)" vs
    // "(m)").  Re-fit any columns that became too narrow.
    connect(m_model, &QAbstractItemModel::headerDataChanged,
            this, [this](Qt::Orientation o, int, int) {
                if (o == Qt::Horizontal) ensureMinColumnWidths();
            });

    // Cross-view sync — forward direct-edit notifications so the
    // Property Browser dock can refresh its adapter view of the
    // same object.  Suppressed when we're refreshing in response
    // to an external edit ourselves (`m_suppressEditForward`).
    connect(m_model, &SWMMAttributeTableModel::objectEdited,
            this, [this](const QString &name) {
                if (!m_suppressEditForward) emit objectEdited(name);
            });
    m_proxy->setSortRole(Qt::DisplayRole);
    m_proxy->setFilterKeyColumn(0);  // Name column drives "show selected only"
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_view = new QTableView(this);
    m_view->setModel(m_proxy);
    m_view->setSortingEnabled(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setAlternatingRowColors(true);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setDefaultSectionSize(
        m_view->verticalHeader()->minimumSectionSize());

    // Use a sharper selection colour than the OS default — the
    // OS-grey for inactive widgets is hard to spot when the table
    // doesn't have focus (which is the common case during cross-view
    // selection from the canvas).  Yellow matches the canvas's
    // selection-highlight convention so the two views look related.
    {
        QPalette pal = m_view->palette();
        pal.setColor(QPalette::Active,    QPalette::Highlight,
                     QColor(0xFF, 0xE0, 0x66));  // warm yellow
        pal.setColor(QPalette::Inactive,  QPalette::Highlight,
                     QColor(0xFF, 0xE0, 0x66));
        pal.setColor(QPalette::Active,    QPalette::HighlightedText, Qt::black);
        pal.setColor(QPalette::Inactive,  QPalette::HighlightedText, Qt::black);
        m_view->setPalette(pal);
    }
    root->addWidget(m_view, 1);

    // Right-click context menu on the table — Change Type… etc.
    // Object-type conversion (Junction ↔ Outfall ↔ Storage ↔ Divider,
    // Conduit ↔ Pump ↔ …) is deliberately NOT inline-editable; the
    // user reaches it via this menu.
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTableView::customContextMenuRequested,
            this, &AttributeTablePanel::onContextMenuRequested);

    // Delete / Backspace on the table deletes the selected rows' objects.
    // Scoped to the view (WidgetWithChildrenShortcut) so it only fires while
    // the table has focus, never while the query line-edit or combo does.
    for (QKeySequence seq : {QKeySequence(QKeySequence::Delete),
                             QKeySequence(Qt::Key_Backspace)}) {
        auto *sc = new QShortcut(seq, m_view);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated,
                this, &AttributeTablePanel::deleteSelectedRows);
    }

    connect(m_categoryCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AttributeTablePanel::onCategoryChanged);
    connect(m_view->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, [this]() { onTableSelectionChanged(); });
    connect(actShow, &QAction::toggled,
            this, &AttributeTablePanel::onShowSelectedOnlyToggled);
    connect(actZoom, &QAction::triggered,
            this, &AttributeTablePanel::onZoomToSelectedClicked);
    connect(m_copyAct, &QAction::triggered,
            this, &AttributeTablePanel::copySelectionToClipboard);
    connect(actExport, &QAction::triggered,
            this, &AttributeTablePanel::onExportCsvClicked);
}

void AttributeTablePanel::setProject(SWMMModelLayer *layer,
                                      SelectionManager *selMgr,
                                      MapCanvas *canvas)
{
    qCDebug(lcAttrTbl) << "setProject layer=" << layer << "selMgr=" << selMgr
                       << "canvas=" << canvas;
    if (m_layer == layer && m_selMgr == selMgr && m_canvas == canvas)
        return;

    if (m_selMgr)
        QObject::disconnect(m_selMgr, &SelectionManager::selectionChanged,
                            this,     &AttributeTablePanel::onSelectionManagerChanged);
    if (m_layer) {
        QObject::disconnect(m_layer, &SWMMModelLayer::modelLoaded,
                            this,    &AttributeTablePanel::refresh);
        QObject::disconnect(m_layer, &SWMMModelLayer::geometryChanged,
                            this,    &AttributeTablePanel::refresh);
        QObject::disconnect(m_layer, &SWMMModelLayer::attributeChanged,
                            this,    &AttributeTablePanel::onObjectEditedExternally);
    }
    // Z.4.3 — also detach canvas layer-add/remove so we don't get
    // stale tabular-layer entries from a closed project.
    if (m_canvas) {
        QObject::disconnect(m_canvas, &MapCanvas::layerAdded,
                            this,     &AttributeTablePanel::refresh);
        QObject::disconnect(m_canvas, &MapCanvas::layerRemoved,
                            this,     &AttributeTablePanel::refresh);
    }

    m_layer  = layer;
    m_selMgr = selMgr;
    m_canvas = canvas;

    // Z.4.3 — listen for layer add/remove so loaded CSV/TSV layers
    // immediately surface in the category combo without a tab
    // switch.  refresh() rebuilds the combo entries.
    if (m_canvas) {
        connect(m_canvas, &MapCanvas::layerAdded,
                this,     &AttributeTablePanel::refresh,
                Qt::UniqueConnection);
        connect(m_canvas, &MapCanvas::layerRemoved,
                this,     &AttributeTablePanel::refresh,
                Qt::UniqueConnection);
    }

    if (m_layer) {
        connect(m_layer, &SWMMModelLayer::modelLoaded,
                this,    &AttributeTablePanel::refresh,
                Qt::UniqueConnection);
        connect(m_layer, &SWMMModelLayer::geometryChanged,
                this,    &AttributeTablePanel::refresh,
                Qt::UniqueConnection);
        connect(m_layer, &SWMMModelLayer::attributeChanged,
                this,    &AttributeTablePanel::onObjectEditedExternally,
                Qt::UniqueConnection);
    }
    if (m_selMgr)
        connect(m_selMgr, &SelectionManager::selectionChanged,
                this,     &AttributeTablePanel::onSelectionManagerChanged,
                Qt::UniqueConnection);

    // Slice Z.5.5 — wire the canvas's MapUndoStack into the model
    // so each cell commit lands as a QUndoCommand.  Ctrl+Z then
    // round-trips attribute edits alongside map / category-order
    // edits in a single user-visible stack.
    if (m_model)
        m_model->setUndoStack(m_canvas ? m_canvas->undoStack() : nullptr);

    refresh();
}

void AttributeTablePanel::refresh()
{
    qCDebug(lcAttrTbl) << "refresh() layer=" << m_layer
                       << "model=" << m_model
                       << "combo=" << m_categoryCombo;
    if (!m_categoryCombo || !m_model) {
        // Should never happen — buildUi() creates both unconditionally.
        // Guarded anyway so a stale invocation during teardown doesn't
        // explode.
        qCWarning(lcAttrTbl) << "refresh() called with null UI/model — skipping";
        return;
    }

    // Phase 3 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — rebuild the table
    // (schema + delegates) whenever the User Flags Manager changes the
    // definition set. The flags model is lazily created once the engine is
    // open, so (re-)establish the connection here rather than in
    // setProject(); UniqueConnection makes the repeat calls idempotent,
    // and ensureUserFlagsModel() re-creating the model on an engine swap
    // drops the stale connection with the old instance.
    if (m_layer) {
        if (auto *ufm = m_layer->ensureUserFlagsModel())
            connect(ufm, &openswmmvis::ui::UserFlagsModel::defsChanged,
                    this, &AttributeTablePanel::refresh,
                    Qt::UniqueConnection);
    }

    // Rebuild category combo entries — SWMM categories first
    // (keep only non-empty), then a separator, then any
    // TabularDataLayer instances loaded into the canvas (Z.4.3).
    // Data convention:
    //   - SWMM cat: combo data is the Category enum int.
    //   - Tabular layer: combo data is a string id "tab:<layerId>".
    const QString currentText = m_categoryCombo->currentText();
    m_categoryCombo->blockSignals(true);
    m_categoryCombo->clear();
    if (m_layer) {
        for (auto cat : m_layer->categoryOrder()) {
            const int n = m_layer->categoryCount(cat);
            if (n <= 0) continue;
            m_categoryCombo->addItem(
                QStringLiteral("%1 (%2)").arg(categoryLabel(cat)).arg(n),
                static_cast<int>(cat));
        }
    }
    // Z.4.3 — list TabularDataLayer entries from the canvas.
    if (m_canvas) {
        bool addedSeparator = false;
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            auto *tab = qobject_cast<TabularDataLayer *>(l);
            if (!tab) continue;
            if (!addedSeparator && m_categoryCombo->count() > 0) {
                m_categoryCombo->insertSeparator(m_categoryCombo->count());
                addedSeparator = true;
            }
            m_categoryCombo->addItem(
                QStringLiteral("▾ Table: %1 (%2)")
                    .arg(tab->name()).arg(tab->rowCount()),
                QStringLiteral("tab:%1").arg(tab->layerId()));
        }
    }
    int idx = m_categoryCombo->findText(currentText);
    if (idx < 0) idx = 0;
    m_categoryCombo->setCurrentIndex(idx);
    m_categoryCombo->blockSignals(false);

    // Block the table's selection-change signal during the model swap so the
    // view's QItemSelectionModel clear doesn't fire onTableSelectionChanged()
    // → m_selMgr->select({}, Replace) and wipe the global selection.
    auto *sm = m_view->selectionModel();
    sm->blockSignals(true);

    // Re-bind the model based on which kind the user picked.
    if (m_categoryCombo->count() == 0) {
        m_model->setSource(nullptr, SWMMModelLayer::CatJunctions);
        m_proxy->setSourceModel(m_model);
    } else {
        const QVariant data = m_categoryCombo->currentData();
        if (data.userType() == QMetaType::Int) {
            const auto cat = static_cast<SWMMModelLayer::Category>(data.toInt());
            m_model->setSource(m_layer, cat);
            m_proxy->setSourceModel(m_model);
            installColumnDelegates();
            restoreColumnWidths(cat);
        } else if (data.toString().startsWith(QStringLiteral("tab:"))) {
            const QString layerId = data.toString().mid(4);
            TabularDataLayer *tab = nullptr;
            if (m_canvas) {
                for (OpenSWMMVisLayer *l : m_canvas->layers()) {
                    if (l->layerId() == layerId) {
                        tab = qobject_cast<TabularDataLayer *>(l);
                        break;
                    }
                }
            }
            m_tabularModel->setLayer(tab);
            m_proxy->setSourceModel(m_tabularModel);
            // Tabular layer: no SWMM delegates / no per-category widths.
            for (int c = 0; c < m_proxy->columnCount(); ++c)
                m_view->setItemDelegateForColumn(c, nullptr);
        }
    }

    sm->blockSignals(false);
    qCDebug(lcAttrTbl) << "refresh() done; rowCount=" << m_proxy->rowCount();

    // Z.2 — refresh row-count badge whenever the model resets.
    if (m_queryStatus) {
        const int total = m_proxy->sourceModel()
                              ? m_proxy->sourceModel()->rowCount() : 0;
        m_queryStatus->setText(tr("%1 row%2").arg(total).arg(total == 1 ? "" : "s"));
    }

    if (m_selMgr && !m_selMgr->isEmpty())
        onSelectionManagerChanged(m_selMgr->selection(), {}, {});
}

void AttributeTablePanel::onCategoryChanged(int /*comboIdx*/)
{
    if (m_categoryCombo->currentIndex() < 0) return;

    // Save the outgoing SWMM category's column widths before any
    // model swap so the next visit to it restores the same layout.
    if (m_proxy->sourceModel() == m_model) {
        const auto previous = m_model->category();
        saveColumnWidths(previous);
    }

    const QVariant data = m_categoryCombo->currentData();
    if (data.userType() == QMetaType::Int) {
        // SWMM category.
        if (!m_layer) return;
        const auto cat = static_cast<SWMMModelLayer::Category>(data.toInt());
        m_model->setSource(m_layer, cat);
        m_proxy->setSourceModel(m_model);
        installColumnDelegates();
        restoreColumnWidths(cat);
    } else if (data.toString().startsWith(QStringLiteral("tab:"))) {
        // Z.4.3 — tabular layer source.
        const QString layerId = data.toString().mid(4);
        TabularDataLayer *tab = nullptr;
        if (m_canvas) {
            for (OpenSWMMVisLayer *l : m_canvas->layers()) {
                if (l->layerId() == layerId) {
                    tab = qobject_cast<TabularDataLayer *>(l);
                    break;
                }
            }
        }
        m_tabularModel->setLayer(tab);
        m_proxy->setSourceModel(m_tabularModel);
        for (int c = 0; c < m_proxy->columnCount(); ++c)
            m_view->setItemDelegateForColumn(c, nullptr);
    }

    // Z.2 — clear the query bar when the source changes because
    // the column-name set is different.
    onQueryClearClicked();

    if (m_showSelectedOnly && m_selMgr)
        onSelectionManagerChanged(m_selMgr->selection(), {}, {});
}

// Slice Round-4 polish 2026-05-12 — scope persisted column widths
// per project so users get reproducible layouts for each model.  The
// scope key derives from the active SWMMModelLayer's modelFilePath():
//   "<basename>-<8-hex-sha1-of-canonical-path>"
// Basename keeps the QSettings tree human-skimmable; the short hash
// disambiguates same-named files in different folders without
// pinning the full filesystem path (which leaks between machines).
// Returns "default" when no project is bound — so the panel still
// remembers widths across launches even before a project is opened.
static QString projectScopeKeyFor(const SWMMModelLayer *layer)
{
    if (!layer) return QStringLiteral("default");
    const QString path = layer->modelFilePath();
    if (path.isEmpty()) return QStringLiteral("default");
    const QFileInfo info(path);
    const QByteArray canonical = info.absoluteFilePath().toUtf8();
    const QByteArray digest =
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha1)
            .toHex().left(8);
    QString base = info.completeBaseName();
    // Strip path separators / chars QSettings would treat as
    // groups (defence-in-depth — completeBaseName already excludes
    // directory separators).
    base.replace(QRegularExpression(QStringLiteral("[/\\\\\\s]")),
                 QStringLiteral("_"));
    if (base.isEmpty()) base = QStringLiteral("project");
    return QStringLiteral("%1-%2").arg(base, QString::fromLatin1(digest));
}

void AttributeTablePanel::saveColumnWidths(SWMMModelLayer::Category cat) const
{
    if (!m_view) return;
    auto *header = m_view->horizontalHeader();
    if (!header || header->count() == 0) return;
    QSettings s;
    const QString scope = projectScopeKeyFor(m_model ? m_model->layer() : nullptr);
    s.setValue(QStringLiteral("SWMMVis/AttributeTablePanel/projects/%1/cat%2/columnWidths")
                   .arg(scope).arg(static_cast<int>(cat)),
               header->saveState());
}

void AttributeTablePanel::restoreColumnWidths(SWMMModelLayer::Category cat)
{
    if (!m_view) return;
    auto *header = m_view->horizontalHeader();
    if (!header) return;
    QSettings s;
    const QString scope = projectScopeKeyFor(m_model ? m_model->layer() : nullptr);

    // Fallback chain — project key first, then the legacy global
    // (pre-Round-4) key so users with previously saved widths see
    // their layout the first time they open a project under the
    // new scheme.
    const QString primaryKey =
        QStringLiteral("SWMMVis/AttributeTablePanel/projects/%1/cat%2/columnWidths")
            .arg(scope).arg(static_cast<int>(cat));
    const QString legacyKey =
        QStringLiteral("SWMMVis/AttributeTablePanel/cat%1/columnWidths")
            .arg(static_cast<int>(cat));

    QByteArray state = s.value(primaryKey).toByteArray();
    if (state.isEmpty())
        state = s.value(legacyKey).toByteArray();
    if (!state.isEmpty())
        header->restoreState(state);

    ensureMinColumnWidths();
}

void AttributeTablePanel::ensureMinColumnWidths()
{
    if (!m_view) return;
    auto *header = m_view->horizontalHeader();
    if (!header) return;
    const int count = header->count();
    for (int c = 0; c < count; ++c) {
        // `sectionSizeHint` measures the header text + sort indicator
        // padding for this section using the header's font metrics —
        // exactly what we need to keep "Invert Elevation (ft)" from
        // clipping.  Add a small breathing margin so adjacent headers
        // don't share a pixel boundary.
        const int hint = header->sectionSizeHint(c) + 8;
        if (header->sectionSize(c) < hint)
            header->resizeSection(c, hint);
    }
}

void AttributeTablePanel::installColumnDelegates()
{
    if (!m_view || !m_model) return;
    // Clear any delegates installed from the previous category.
    // QTableView doesn't own the delegate; we keep parents on the
    // panel so they're destroyed with the panel.
    for (int col = 0; col < m_model->columnCount(); ++col)
        m_view->setItemDelegateForColumn(col, nullptr);

    const auto specs = m_model->columnSpecs();
    for (int col = 0; col < specs.size(); ++col) {
        const auto &spec = specs[col];
        QStyledItemDelegate *del = nullptr;
        switch (spec.editor) {
        case openswmmvis::EditorKind::Numeric:
            del = new openswmmvis::NumericDelegate(this,
                                                     spec.minValue,
                                                     spec.maxValue,
                                                     spec.decimals);
            break;
        case openswmmvis::EditorKind::Integer:
            del = new openswmmvis::IntegerDelegate(this,
                                                     static_cast<int>(spec.minValue),
                                                     static_cast<int>(spec.maxValue));
            break;
        case openswmmvis::EditorKind::Enum:
            del = new openswmmvis::EnumDelegate(this, spec.enumValues);
            break;
        case openswmmvis::EditorKind::Interval:
            del = new openswmmvis::IntervalDelegate(this);
            break;
        case openswmmvis::EditorKind::Compound:
            del = new openswmmvis::CompoundEditDelegate(this);
            break;
        case openswmmvis::EditorKind::Text:
            // Qt's default QStyledItemDelegate provides a QLineEdit — no custom
            // delegate needed.  Fall through so nullptr is NOT installed.
            continue;
        case openswmmvis::EditorKind::ReadOnly:
        default:
            continue;
        }
        // Delegate column index is the *proxy* column; since proxy
        // doesn't reorder columns, source-col == proxy-col here.
        m_view->setItemDelegateForColumn(col, del);
    }
}

SWMMObjectRef::ObjectType
AttributeTablePanel::objectTypeFor(SWMMModelLayer::Category cat) const
{
    return objectTypeForCategory(cat);
}

void AttributeTablePanel::onTableSelectionChanged()
{
    if (m_applyingFromBus || !m_selMgr || !m_model || !m_view || !m_proxy) return;
    // Z.4.3 — only the SWMM model carries object refs; tabular
    // source has no canvas-linked selection.
    if (m_proxy->sourceModel() != m_model) return;
    auto *sel = m_view->selectionModel();
    if (!sel) return;

    const auto type = objectTypeForCategory(m_model->category());
    QSet<SWMMObjectRef> refs;
    for (const QModelIndex &proxyIdx : sel->selectedRows()) {
        const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
        const QString name = m_model->objectNameAt(srcIdx.row());
        if (!name.isEmpty())
            refs.insert(SWMMObjectRef(type, name));
    }
    m_selMgr->select(refs, SelectionManager::Replace);
}

void AttributeTablePanel::onSelectionManagerChanged(
    const QSet<SWMMObjectRef> &current,
    const QSet<SWMMObjectRef> & /*added*/,
    const QSet<SWMMObjectRef> & /*removed*/)
{
    if (!m_layer || !m_model || !m_view || !m_proxy) return;
    // Z.4.3 — when a tabular source is active, the bus selection
    // doesn't apply (no SWMMObjectRef → row mapping).
    if (m_proxy->sourceModel() != m_model) return;
    auto *sel = m_view->selectionModel();
    if (!sel) return;

    // Reentrancy guard: setting view selection below fires
    // selectionChanged on QItemSelectionModel, which would otherwise
    // bounce right back into onTableSelectionChanged → SelectionManager.
    m_applyingFromBus = true;

    const auto type = objectTypeForCategory(m_model->category());

    // "Show selected only" filter — only rows whose names are in the
    // current selection are visible.  Edge case: 0 matching refs with
    // filter on → use a regex that matches nothing.
    if (m_showSelectedOnly) {
        QStringList names;
        for (const auto &ref : current) {
            if (ref.objectType == type)
                names << QRegularExpression::escape(ref.name);
        }
        if (names.isEmpty())
            m_proxy->setFilterRegularExpression(
                QRegularExpression(QStringLiteral("(?!)")));   // never matches
        else
            m_proxy->setFilterRegularExpression(
                QRegularExpression(QStringLiteral("^(?:%1)$").arg(names.join('|'))));
    } else if (!m_proxy->filterRegularExpression().pattern().isEmpty()) {
        // Only clear when something was actually set — avoid a
        // gratuitous model reset on every selection change.
        m_proxy->setFilterRegularExpression(QRegularExpression());
    }

    // Now sync the view's row selection to the current set.
    sel->clearSelection();
    for (const auto &ref : current) {
        if (ref.objectType != type) continue;
        const int srcRow = m_model->rowForName(ref.name);
        if (srcRow < 0) continue;
        const QModelIndex srcIdx = m_model->index(srcRow, 0);
        const QModelIndex prxIdx = m_proxy->mapFromSource(srcIdx);
        if (prxIdx.isValid())
            sel->select(prxIdx,
                        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }

    m_applyingFromBus = false;
}

void AttributeTablePanel::onShowSelectedOnlyToggled(bool on)
{
    m_showSelectedOnly = on;
    if (m_selMgr)
        onSelectionManagerChanged(m_selMgr->selection(), {}, {});
    else
        m_proxy->setFilterRegularExpression(QRegularExpression());
}

void AttributeTablePanel::onZoomToSelectedClicked()
{
    if (!m_canvas || !m_layer || !m_selMgr || m_selMgr->isEmpty()) return;
    const auto type = objectTypeForCategory(m_model->category());

    // Build the layer-CRS bbox of the selected objects, skipping those
    // we can't resolve (e.g. ref belongs to a different category).
    MapExtent acc;
    bool any = false;
    for (const auto &ref : m_selMgr->selection()) {
        if (ref.objectType != type) continue;
        const MapExtent e = m_layer->objectExtent(ref.name);
        if (!std::isfinite(e.xMin()) || !std::isfinite(e.xMax())) continue;
        if (!any) { acc = e; any = true; }
        else      { acc = acc.united(e); }
    }
    if (!any) return;

    // Project to canvas CRS, then add a small pad so the selection
    // doesn't land flush with the viewport edges.  Point selections
    // (zero-width bbox) get an absolute buffer derived from the
    // layer's overall extent — same heuristic as ObjectBrowser's
    // zoomToObject().
    MapExtent obj = m_canvas->extentInCanvasCRS(m_layer, acc);
    if (!std::isfinite(obj.xMin()) || !std::isfinite(obj.xMax())) return;

    double x0 = obj.xMin(), y0 = obj.yMin();
    double x1 = obj.xMax(), y1 = obj.yMax();
    const bool isPoint = (obj.width() == 0.0 && obj.height() == 0.0);
    if (isPoint) {
        double buf = 100.0;
        if (const MapExtent le = m_canvas->layerExtentInCanvasCRS(m_layer);
            le.isValid()) {
            const double dx = le.xMax() - le.xMin();
            const double dy = le.yMax() - le.yMin();
            buf = std::max(25.0, 0.005 * std::max(dx, dy));
        }
        x0 -= buf; y0 -= buf; x1 += buf; y1 += buf;
    } else {
        const double padX = std::max(1e-6, obj.width()  * 0.10);
        const double padY = std::max(1e-6, obj.height() * 0.10);
        x0 -= padX; y0 -= padY; x1 += padX; y1 += padY;
    }
    const MapExtent zoom(x0, y0, x1, y1);
    if (zoom.isValid())
        m_canvas->setExtent(zoom);
}

// ---------------------------------------------------------------------------
// Copy — selected rows to the clipboard as TSV
//
// Everything is read through the proxy, so the copy honours the query filter,
// "show selected only", the user's sort, and any hidden columns — what you see
// is what you paste.  TSV (not CSV) because that is what Excel / Sheets accept
// straight out of the clipboard.  Consistent with onExportCsvClicked()'s .tsv
// branch, embedded tabs/newlines are flattened to spaces rather than quoted:
// TSV has no quoting convention.
// ---------------------------------------------------------------------------

QString AttributeTablePanel::selectionAsTsv() const
{
    if (!m_view || !m_proxy || m_proxy->rowCount() == 0) return {};

    QList<int> cols;
    const int nCol = m_proxy->columnCount();
    for (int c = 0; c < nCol; ++c)
        if (!m_view->isColumnHidden(c)) cols << c;
    if (cols.isEmpty()) return {};

    // Selected rows, in the order they appear on screen.  Nothing selected →
    // copy the whole visible table (matching Export CSV's behaviour).
    QList<int> rows;
    if (auto *sm = m_view->selectionModel(); sm && sm->hasSelection()) {
        const QModelIndexList sel = sm->selectedRows();
        for (const QModelIndex &pi : sel) rows << pi.row();
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    } else {
        const int nRow = m_proxy->rowCount();
        rows.reserve(nRow);
        for (int r = 0; r < nRow; ++r) rows << r;
    }
    if (rows.isEmpty()) return {};

    const auto flatten = [](QString s) {
        s.replace(QLatin1Char('\t'), QLatin1Char(' '));
        s.replace(QLatin1Char('\n'), QLatin1Char(' '));
        s.replace(QLatin1Char('\r'), QLatin1Char(' '));
        return s;
    };

    QStringList lines;
    lines.reserve(rows.size() + 1);

    QStringList header;
    header.reserve(cols.size());
    for (int c : std::as_const(cols))
        header << flatten(m_proxy->headerData(c, Qt::Horizontal).toString());
    lines << header.join(QLatin1Char('\t'));

    for (int r : std::as_const(rows)) {
        QStringList cells;
        cells.reserve(cols.size());
        for (int c : std::as_const(cols))
            cells << flatten(m_proxy->data(m_proxy->index(r, c)).toString());
        lines << cells.join(QLatin1Char('\t'));
    }

    return lines.join(QLatin1Char('\n'));
}

void AttributeTablePanel::copySelectionToClipboard()
{
    const QString tsv = selectionAsTsv();
    if (tsv.isEmpty()) return;
    QGuiApplication::clipboard()->setText(tsv);
}

void AttributeTablePanel::onExportCsvClicked()
{
    if (!m_model || m_model->rowCount() == 0) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Attribute Table"),
        QDir::homePath() + "/attribute_table.csv",
        tr("CSV (*.csv);;TSV (*.tsv);;All Files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"),
                              tr("Cannot write %1: %2").arg(path, f.errorString()));
        return;
    }
    QTextStream out(&f);
    const QString sep = path.endsWith(QStringLiteral(".tsv"),
                                       Qt::CaseInsensitive) ? "\t" : ",";

    auto quoteCsv = [&](const QString &s) -> QString {
        // RFC 4180-ish: wrap in quotes when the value contains the
        // separator, a quote, or a newline; double up internal quotes.
        if (sep == "\t") return s;  // TSV: no quoting convention
        if (s.contains(',') || s.contains('"') || s.contains('\n')) {
            QString out = s;
            out.replace('"', "\"\"");
            return '"' + out + '"';
        }
        return s;
    };

    // Header row uses the proxy column headers so user-resorted
    // columns export in the same order they see.
    const int nCol = m_proxy->columnCount();
    QStringList header;
    for (int c = 0; c < nCol; ++c)
        header << quoteCsv(m_proxy->headerData(c, Qt::Horizontal).toString());
    out << header.join(sep) << '\n';

    // Honour the "show selected only" filter — what's visible is what
    // gets exported.
    const int nRow = m_proxy->rowCount();
    for (int r = 0; r < nRow; ++r) {
        QStringList cells;
        for (int c = 0; c < nCol; ++c)
            cells << quoteCsv(m_proxy->data(m_proxy->index(r, c)).toString());
        out << cells.join(sep) << '\n';
    }
    f.close();
}

// ---------------------------------------------------------------------------
// Right-click context menu (Slice Z.5.4-followup)
//
// Object-type conversion (e.g. Junction → Outfall) is deliberately not
// inline-editable in the table or the property browser.  The user
// reaches it from this context menu so it's a deliberate action that
// can warn about destructive consequences.
// ---------------------------------------------------------------------------

void AttributeTablePanel::onContextMenuRequested(const QPoint &pos)
{
    if (!m_view || !m_model || !m_layer) return;
    const QModelIndex proxyIdx = m_view->indexAt(pos);
    if (!proxyIdx.isValid()) return;

    QMenu menu(this);

    QAction *copyAct = menu.addAction(tr("Copy (Ctrl+C)"));
    connect(copyAct, &QAction::triggered,
            this, &AttributeTablePanel::copySelectionToClipboard);
    menu.addSeparator();

    // §S.SC.1.c — Cell-type-aware "Edit in …" actions surfacing the
    // CRUD editors (CurveEditorDialog / PatternEditorDialog /
    // TimeseriesEditorDialog / HydrographGroupEditor /
    // NodeCompoundEditDialog / LinkCompoundEditDialog) directly from
    // the Attribute Table. Mirrors PropertiesPanel's right-click menu so
    // the two surfaces have parity per [[feedback_mvc_synchronized_uis]].
    // The dispatched action is queued via QAction::triggered (not
    // executed inline) so the menu can keep its existing "Change Type"
    // / "Zoom" entries unchanged below.
    const QModelIndex sourceIdx = m_proxy ? m_proxy->mapToSource(proxyIdx)
                                          : proxyIdx;
    const QVariant cellValue = sourceIdx.isValid()
        ? sourceIdx.data(Qt::EditRole) : QVariant();
    const int metaId = cellValue.userType();
    bool addedEditAction = false;

    if (metaId == qMetaTypeId<NodeCompoundEditRef>()) {
        const NodeCompoundEditRef ref = cellValue.value<NodeCompoundEditRef>();
        if (ref.engine && !ref.nodeName.isEmpty()) {
            QAction *act = menu.addAction(
                ref.summary.isEmpty() ? tr("Edit…")
                                      : tr("Edit \"%1\"…").arg(ref.summary));
            connect(act, &QAction::triggered, this, [this, ref, sourceIdx]() {
                NodeCompoundEditDialog dlg(ref, this);
                dlg.exec();
                NodeCompoundEditRef updated = ref;
                updated.summary = dlg.updatedSummary();
                m_model->setData(sourceIdx, QVariant::fromValue(updated),
                                  Qt::EditRole);
            });
            addedEditAction = true;
        }
    } else if (metaId == qMetaTypeId<LinkCompoundEditRef>()) {
        const LinkCompoundEditRef ref = cellValue.value<LinkCompoundEditRef>();
        if (ref.engine && !ref.linkName.isEmpty()) {
            QAction *act = menu.addAction(
                ref.summary.isEmpty() ? tr("Edit…")
                                      : tr("Edit \"%1\"…").arg(ref.summary));
            connect(act, &QAction::triggered, this, [this, ref, sourceIdx]() {
                LinkCompoundEditDialog dlg(ref, this);
                dlg.exec();
                LinkCompoundEditRef updated = ref;
                updated.summary = dlg.updatedSummary();
                m_model->setData(sourceIdx, QVariant::fromValue(updated),
                                  Qt::EditRole);
            });
            addedEditAction = true;
        }
    } else if (metaId == qMetaTypeId<DataObjectRef>()) {
        const DataObjectRef ref = cellValue.value<DataObjectRef>();
        if (ref.layer && ref.kind != DataObjectRef::RainGage
            && ref.kind != DataObjectRef::SubcatchOutlet) {
            SWMMModelLayer::DataCategory dc = SWMMModelLayer::DataTimeSeries;
            switch (ref.kind) {
            case DataObjectRef::TidalCurve:
            case DataObjectRef::AnyCurve:
            case DataObjectRef::StorageCurve:   dc = SWMMModelLayer::DataCurves;      break;
            case DataObjectRef::TimeSeries:     dc = SWMMModelLayer::DataTimeSeries;  break;
            case DataObjectRef::Pattern:        dc = SWMMModelLayer::DataPatterns;    break;
            case DataObjectRef::UnitHydrograph: dc = SWMMModelLayer::DataHydrographs; break;
            case DataObjectRef::Pollutant:      dc = SWMMModelLayer::DataPollutants;  break;
            case DataObjectRef::RainGage:       /* handled above */                   break;
            case DataObjectRef::SubcatchOutlet: /* handled above */                   break;
            }
            const auto &reg = ComprehensiveEditorRegistry::instance();
            const QString title  = reg.editorTitle(dc);
            const bool   shipped = reg.hasEditor(dc);
            QAction *act = menu.addAction(
                ref.currentName.isEmpty()
                    ? tr("Open %1…").arg(title.isEmpty() ? tr("Editor") : title)
                    : tr("Edit \"%1\" in %2…")
                          .arg(ref.currentName,
                               title.isEmpty() ? tr("Editor") : title));
            act->setEnabled(shipped);
            if (!shipped) act->setToolTip(reg.gapTooltip(dc));
            connect(act, &QAction::triggered, this, [this, ref, dc, sourceIdx]() {
                QString chosen;
                switch (dc) {
                case SWMMModelLayer::DataHydrographs:
                    chosen = HydrographGroupEditor::pickGroup(
                        ref.layer, ref.currentName, this);
                    break;
                case SWMMModelLayer::DataPatterns: {
                    using openswmmvis::pattern::PatternRegistry;
                    using openswmmvis::ui::PatternEditorDialog;
                    auto *r = qobject_cast<PatternRegistry *>(
                        ref.layer->ensurePatternRegistry());
                    if (!r) return;
                    chosen = PatternEditorDialog::pickPattern(
                        r, /*undoStack=*/nullptr, ref.currentName, this);
                    break;
                }
                case SWMMModelLayer::DataTimeSeries: {
                    using openswmmvis::timeseries::TimeseriesRegistry;
                    using openswmmvis::ui::TimeseriesEditorDialog;
                    auto *r = qobject_cast<TimeseriesRegistry *>(
                        ref.layer->ensureTimeseriesRegistry());
                    if (!r) return;
                    chosen = TimeseriesEditorDialog::pickTimeseries(
                        r, /*undoStack=*/nullptr, ref.currentName, this);
                    if (!chosen.isEmpty()) r->saveToEngine();
                    break;
                }
                case SWMMModelLayer::DataCurves: {
                    using openswmmvis::curve::CurveRegistry;
                    using openswmmvis::ui::CurveEditorDialog;
                    auto *r = qobject_cast<CurveRegistry *>(
                        ref.layer->ensureCurveRegistry());
                    if (!r) return;
                    QPointer<CurveEditorDialog> dlg = ref.currentName.isEmpty()
                        ? CurveEditorDialog::createNew(r, /*undoStack=*/nullptr, this)
                        : nullptr;
                    if (!dlg) {
                        dlg = new CurveEditorDialog(r, /*undoStack=*/nullptr, this);
                        dlg->openForCurve(ref.currentName);
                    }
                    if (dlg) {
                        dlg->setAttribute(Qt::WA_DeleteOnClose);
                        dlg->show();
                    }
                    return;
                }
                default:
                    return;
                }
                if (chosen.isEmpty()) return;
                DataObjectRef updated = ref;
                updated.currentName = chosen;
                m_model->setData(sourceIdx, QVariant::fromValue(updated),
                                  Qt::EditRole);
            });
            addedEditAction = true;
        }
    }

    if (addedEditAction) menu.addSeparator();

    // Bulk "apply to selected" — when the clicked column is a simple
    // editable attribute (Numeric / Integer / Enum / Text) and ≥2 rows
    // are selected, offer to push one value into that column for every
    // selected object. Two flavours: copy the clicked cell's value, or
    // prompt for one. Per-row editability is respected (e.g. an
    // inapplicable cross-section geom is skipped) and the whole batch
    // collapses into a single undo step.
    {
        const int col = sourceIdx.isValid() ? sourceIdx.column() : -1;
        const QList<openswmmvis::ColumnSpec> specs = m_model->columnSpecs();
        const QList<int> selRows = selectedSourceRows();
        // Require the clicked cell to be editable so there's a meaningful
        // value to copy (also suppresses the option for a non-applicable
        // geom cell or while a simulation is running).
        const bool clickedEditable =
            sourceIdx.isValid()
            && (m_model->flags(sourceIdx) & Qt::ItemIsEditable);
        if (clickedEditable && col >= 1 && col < specs.size()
            && selRows.size() >= 2) {
            using EditorKind = openswmmvis::EditorKind;
            const openswmmvis::ColumnSpec &spec = specs[col];
            const bool simpleEditable =
                spec.editor == EditorKind::Numeric ||
                spec.editor == EditorKind::Integer ||
                spec.editor == EditorKind::Enum    ||
                spec.editor == EditorKind::Text;
            if (simpleEditable) {
                QAction *copyAct = menu.addAction(
                    tr("Apply this \"%1\" value to %2 selected rows")
                        .arg(spec.label).arg(selRows.size()));
                connect(copyAct, &QAction::triggered, this,
                        [this, col, selRows, cellValue]() {
                            applyValueToSelectedRows(col, selRows, cellValue);
                        });
                QAction *promptAct = menu.addAction(
                    tr("Apply \"%1\" value to %2 selected rows…")
                        .arg(spec.label).arg(selRows.size()));
                connect(promptAct, &QAction::triggered, this,
                        [this, col, selRows, cellValue]() {
                            bool ok = false;
                            const QVariant v = promptBulkValue(col, cellValue, &ok);
                            if (ok) applyValueToSelectedRows(col, selRows, v);
                        });
                menu.addSeparator();
            }
        }
    }

    auto *changeTypeAct = menu.addAction(tr("Change Type…"));
    connect(changeTypeAct, &QAction::triggered,
            this, &AttributeTablePanel::onChangeTypeTriggered);

    // Delete — only for spatial categories that have an engine delete path.
    // Mirrors the map's right-click delete, and routes through the same undo
    // stack, so a deletion here is undoable and every other view refreshes.
    if (categoryIsDeletable()) {
        const int nSel = selectedSourceRows().size();
        // Hint the key in the label (matching "Copy (Ctrl+C)" above) rather
        // than via setShortcut(), which would fight the QShortcut on the view.
        auto *deleteAct = menu.addAction(
            nSel <= 1 ? tr("Delete (Del)")
                      : tr("Delete %1 selected (Del)").arg(nSel));
        deleteAct->setEnabled(nSel >= 1);
        connect(deleteAct, &QAction::triggered,
                this, &AttributeTablePanel::deleteSelectedRows);
    }

    menu.addSeparator();

    auto *zoomAct = menu.addAction(tr("Zoom to selected"));
    zoomAct->setEnabled(m_canvas && m_selMgr && !m_selMgr->isEmpty());
    connect(zoomAct, &QAction::triggered,
            this, &AttributeTablePanel::onZoomToSelectedClicked);

    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Bulk "apply value to selected rows" helpers
// ---------------------------------------------------------------------------

QList<int> AttributeTablePanel::selectedSourceRows() const
{
    QList<int> rows;
    if (!m_view || !m_view->selectionModel()) return rows;
    QSet<int> seen;
    const QModelIndexList sel = m_view->selectionModel()->selectedRows();
    for (const QModelIndex &pi : sel) {
        const QModelIndex si = m_proxy ? m_proxy->mapToSource(pi) : pi;
        if (si.isValid() && !seen.contains(si.row())) {
            seen.insert(si.row());
            rows.append(si.row());
        }
    }
    return rows;
}

void AttributeTablePanel::applyValueToSelectedRows(int column,
                                                   const QList<int> &sourceRows,
                                                   const QVariant &value)
{
    if (!m_model || column < 0 || sourceRows.isEmpty()) return;

    // Collapse the whole batch into one undo step when a stack is attached
    // (each setData pushes its own AttributeEditCommand inside the macro).
    QUndoStack *undo = m_model->undoStack();
    if (undo)
        undo->beginMacro(tr("Apply value to %1 rows").arg(sourceRows.size()));
    for (int row : sourceRows) {
        const QModelIndex idx = m_model->index(row, column);
        if (!idx.isValid()) continue;
        // Skip rows whose cell isn't editable (running sim, or an
        // inapplicable cross-section geom for that row's shape).
        if (!(m_model->flags(idx) & Qt::ItemIsEditable)) continue;
        m_model->setData(idx, value, Qt::EditRole);
    }
    if (undo) undo->endMacro();
}

QVariant AttributeTablePanel::promptBulkValue(int column,
                                              const QVariant &current,
                                              bool *ok) const
{
    if (ok) *ok = false;
    if (!m_model) return {};
    const QList<openswmmvis::ColumnSpec> specs = m_model->columnSpecs();
    if (column < 0 || column >= specs.size()) return {};
    using openswmmvis::EditorKind;
    const openswmmvis::ColumnSpec &spec = specs[column];

    auto *self = const_cast<AttributeTablePanel *>(this);
    const QString title  = tr("Apply Value");
    const QString prompt = tr("New value for \"%1\":").arg(spec.label);

    switch (spec.editor) {
    case EditorKind::Numeric: {
        bool got = false;
        const double dv = QInputDialog::getDouble(
            self, title, prompt, current.toDouble(),
            spec.minValue, spec.maxValue, spec.decimals, &got);
        if (ok) *ok = got;
        return got ? QVariant(dv) : QVariant();
    }
    case EditorKind::Integer: {
        bool got = false;
        const int iv = QInputDialog::getInt(
            self, title, prompt, current.toInt(),
            int(spec.minValue), int(spec.maxValue), 1, &got);
        if (ok) *ok = got;
        return got ? QVariant(iv) : QVariant();
    }
    case EditorKind::Enum: {
        // Present the human labels; map the chosen one back to its
        // enum data int (what setData/commitValueDirect expect).
        QStringList labels;
        int curIdx = 0;
        for (const QVariant &pv : spec.enumValues) {
            const QVariantList pair = pv.toList();
            if (pair.size() != 2) continue;
            labels << pair[0].toString();
            if (pair[1].toInt() == current.toInt()) curIdx = labels.size() - 1;
        }
        if (labels.isEmpty()) return {};
        bool got = false;
        const QString chosen = QInputDialog::getItem(
            self, title, prompt, labels, curIdx, /*editable=*/false, &got);
        if (!got) return {};
        for (const QVariant &pv : spec.enumValues) {
            const QVariantList pair = pv.toList();
            if (pair.size() == 2 && pair[0].toString() == chosen) {
                if (ok) *ok = true;
                return pair[1].toInt();
            }
        }
        return {};
    }
    case EditorKind::Text: {
        bool got = false;
        const QString tv = QInputDialog::getText(
            self, title, prompt, QLineEdit::Normal, current.toString(), &got);
        if (ok) *ok = got;
        return got ? QVariant(tv) : QVariant();
    }
    default:
        return {};
    }
}

// ---------------------------------------------------------------------------
// Slice Z.2 — query bar handlers
// ---------------------------------------------------------------------------

void AttributeTablePanel::onQueryApplyClicked()
{
    if (!m_queryEdit || !m_proxy || !m_queryStatus) return;
    auto *fp = static_cast<FilteringProxy *>(m_proxy);
    if (!fp) return;

    const QString text = m_queryEdit->text().trimmed();
    auto pred = openswmmvis::parseQuery(text);

    if (!text.isEmpty() && !pred.isValid()) {
        // Parser error — colour the line edit + show the message.
        m_queryEdit->setStyleSheet(
            QStringLiteral("background-color: #FFD6D6;"));
        m_queryStatus->setText(tr("Error col %1: %2")
                                  .arg(pred.errorPos).arg(pred.error));
        return;
    }
    m_queryEdit->setStyleSheet(QString());
    fp->setQueryPredicate(pred);

    const int matched = m_proxy->rowCount();
    const int total   = m_model ? m_model->rowCount() : 0;
    if (text.isEmpty())
        m_queryStatus->setText(tr("%1 row%2")
                                  .arg(total).arg(total == 1 ? "" : "s"));
    else
        m_queryStatus->setText(tr("%1 of %2 matched").arg(matched).arg(total));

    // Round-4 follow-up 2026-05-12 — single-Apply UX: after the
    // filter runs, also apply the selection-mode radio to the
    // matched rows so the canvas + Object Browser stay in lockstep
    // with the visible rows.  Without this the user has to type
    // their query, see rows filter, then click another button to
    // make the rows actually highlight — which the user pointed
    // out was confusing.
    onSelectionApplyClicked();
}

void AttributeTablePanel::onQueryClearClicked()
{
    if (!m_queryEdit || !m_proxy) return;
    auto *fp = static_cast<FilteringProxy *>(m_proxy);
    if (!fp) return;
    m_queryEdit->clear();
    m_queryEdit->setStyleSheet(QString());
    fp->setQueryPredicate({});
    if (m_queryStatus) {
        const int total = m_model ? m_model->rowCount() : 0;
        m_queryStatus->setText(tr("%1 row%2")
                                  .arg(total).arg(total == 1 ? "" : "s"));
    }
}

// ---------------------------------------------------------------------------
// Slice Z.3 — selection ops driven by the current query
// ---------------------------------------------------------------------------

QSet<SWMMObjectRef> AttributeTablePanel::matchedRefs() const
{
    QSet<SWMMObjectRef> out;
    if (!m_model || !m_queryEdit) return out;
    // Z.4.3 — selection ops require a SWMM model source; tabular
    // sources have no SWMMObjectRefs.
    if (m_proxy && m_proxy->sourceModel() != m_model) return out;
    const SWMMObjectRef::ObjectType type =
        objectTypeForCategory(m_model->category());
    const QString text = m_queryEdit->text().trimmed();
    const auto pred = openswmmvis::parseQuery(text);
    // Parse error → empty match set (the query bar shows the error
    // already; the selection ops are no-ops rather than surprising).
    if (!text.isEmpty() && !pred.isValid()) return out;

    const auto specs = m_model->columnSpecs();
    const int nRow = m_model->rowCount();
    for (int row = 0; row < nRow; ++row) {
        const QString name = m_model->objectNameAt(row);
        if (name.isEmpty()) continue;
        if (pred.root) {
            // Build a value map keyed by BOTH the identify-map key
            // (e.g. "Node type") and the user-facing header label
            // (e.g. "Type") so the query accepts either spelling.
            // Mirrors `FilteringProxy::filterAcceptsRow` so the
            // selection ops and the visible-row filter agree.
            QVariantMap m;
            for (int c = 0; c < specs.size(); ++c) {
                const QModelIndex idx = m_model->index(row, c);
                const QVariant val = m_model->data(idx, Qt::DisplayRole);
                m.insert(specs[c].key,   val);
                m.insert(specs[c].label, val);
            }
            if (!openswmmvis::evaluateQuery(pred, m)) continue;
        }
        out.insert(SWMMObjectRef(type, name));
    }
    return out;
}

QSet<SWMMObjectRef> AttributeTablePanel::allCategoryRefs() const
{
    QSet<SWMMObjectRef> out;
    if (!m_model) return out;
    if (m_proxy && m_proxy->sourceModel() != m_model) return out;
    const SWMMObjectRef::ObjectType type =
        objectTypeForCategory(m_model->category());
    const int nRow = m_model->rowCount();
    for (int row = 0; row < nRow; ++row) {
        const QString name = m_model->objectNameAt(row);
        if (!name.isEmpty()) out.insert(SWMMObjectRef(type, name));
    }
    return out;
}

void AttributeTablePanel::onSelectionApplyClicked()
{
    if (!m_selMgr || !m_selGroup) return;
    const int mode = m_selGroup->checkedId();
    if (mode < 0) return;

    switch (mode) {
    case SelReplace:
        m_selMgr->select(matchedRefs(), SelectionManager::Replace);
        return;
    case SelAdd:
        m_selMgr->select(matchedRefs(), SelectionManager::Add);
        return;
    case SelSubtract:
        m_selMgr->select(matchedRefs(), SelectionManager::Subtract);
        return;
    case SelIntersect: {
        // SelectionManager has no Intersect mode, so we compute
        // (current ∩ matched) here and push the result as Replace.
        const QSet<SWMMObjectRef> matched = matchedRefs();
        QSet<SWMMObjectRef> result;
        for (const auto &r : m_selMgr->selection())
            if (matched.contains(r)) result.insert(r);
        m_selMgr->select(result, SelectionManager::Replace);
        return;
    }
    case SelInvert: {
        // Invert ignores the query — it flips the current selection
        // set within this category, not the matched set.
        const auto all = allCategoryRefs();
        const auto cur = m_selMgr->selection();
        QSet<SWMMObjectRef> result;
        for (const auto &r : all)
            if (!cur.contains(r)) result.insert(r);
        m_selMgr->select(result, SelectionManager::Replace);
        return;
    }
    }
}

void AttributeTablePanel::onObjectEditedExternally(const QString &name)
{
    // Mirror an external attribute change (from vertex drag, undo, or
    // property-browser edit) into the table view.  Two cases:
    //
    // (a) The correct category tab is already active — do a targeted
    //     dataChanged refresh for that row so only the affected cells
    //     repaint.  data() reads from the engine directly, so no cache
    //     invalidation beyond what refreshObject() already does.
    //
    // (b) A different category tab is active — auto-switch the combo to
    //     the edited object's category so the user immediately sees the
    //     updated row.  onCategoryChanged() calls setSource() which
    //     resets the model; the view then re-reads all values from the
    //     engine, including the just-updated attribute.
    if (!m_model || name.isEmpty()) return;
    m_suppressEditForward = true;

    if (m_model->rowForName(name) >= 0) {
        m_model->refreshObject(name);
    } else if (m_layer) {
        SWMMModelLayer::Category cat;
        int unused = -1;
        if (m_layer->findObjectLocation(name, &cat, &unused)) {
            for (int i = 0; i < m_categoryCombo->count(); ++i) {
                if (m_categoryCombo->itemData(i).userType() == QMetaType::Int &&
                    m_categoryCombo->itemData(i).toInt() == static_cast<int>(cat)) {
                    m_categoryCombo->setCurrentIndex(i);
                    break;
                }
            }
        }
    }

    m_suppressEditForward = false;
}

// ---------------------------------------------------------------------------
// Delete selected objects
// ---------------------------------------------------------------------------

bool AttributeTablePanel::categoryIsDeletable() const
{
    if (!m_model) return false;
    switch (objectTypeForCategory(m_model->category())) {
    case SWMMObjectRef::Node:
    case SWMMObjectRef::Link:
    case SWMMObjectRef::Subcatchment:
    case SWMMObjectRef::RainGage:
        return true;
    default:
        return false;   // data-object categories have no spatial delete path
    }
}

int AttributeTablePanel::deleteObjects(const QStringList &names)
{
    if (!m_layer || !m_model || names.isEmpty() || !categoryIsDeletable())
        return 0;

    DeleteObjectCommand::TargetKind kind;
    switch (objectTypeForCategory(m_model->category())) {
    case SWMMObjectRef::Node:         kind = DeleteObjectCommand::DeleteNode;     break;
    case SWMMObjectRef::Link:         kind = DeleteObjectCommand::DeleteLink;     break;
    case SWMMObjectRef::Subcatchment: kind = DeleteObjectCommand::DeleteSubcatch; break;
    case SWMMObjectRef::RainGage:     kind = DeleteObjectCommand::DeleteGage;     break;
    default:                          return 0;
    }

    // Drop the current selection first so the post-delete refresh() (driven by
    // the layer's geometryChanged) doesn't try to reselect names that are gone.
    if (auto *sm = m_view ? m_view->selectionModel() : nullptr)
        sm->clearSelection();

    MapUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;
    int deleted = 0;

    if (stack) {
        // Undoable path — one macro so Ctrl+Z reverses the whole batch. A
        // deleted node cascades its links inside DeleteObjectCommand, exactly
        // as the map's right-click delete does.
        auto *macro = new QUndoCommand(
            names.size() == 1 ? tr("Delete \"%1\"").arg(names.first())
                              : tr("Delete %1 objects").arg(names.size()));
        for (const QString &name : names)
            new DeleteObjectCommand(m_layer, name, kind, m_canvas, macro);
        stack->push(macro);
        deleted = names.size();
    } else {
        // No canvas/undo stack (headless / tests): perform the SAME mutation
        // DeleteObjectCommand::redo() performs, minus the undo record.
        for (const QString &name : names) {
            bool ok = false;
            switch (kind) {
            case DeleteObjectCommand::DeleteNode:     ok = m_layer->applyNodeDelete(name);     break;
            case DeleteObjectCommand::DeleteLink:     ok = m_layer->applyLinkDelete(name);     break;
            case DeleteObjectCommand::DeleteGage:     ok = m_layer->applyGageDelete(name);     break;
            case DeleteObjectCommand::DeleteSubcatch: ok = m_layer->applySubcatchDelete(name); break;
            }
            if (ok) ++deleted;
        }
    }
    return deleted;
}

void AttributeTablePanel::deleteSelectedRows()
{
    if (!m_layer || !m_model || !categoryIsDeletable()) return;

    // Resolve selected rows → object names (source-model rows, de-duplicated).
    QStringList names;
    for (int row : selectedSourceRows()) {
        const QString name = m_model->objectNameAt(row);
        if (!name.isEmpty()) names << name;
    }
    if (names.isEmpty()) return;

    const QString msg = names.size() == 1
        ? tr("Delete \"%1\"?").arg(names.first())
        : tr("Delete %1 selected objects?").arg(names.size());
    const auto btn = QMessageBox::question(
        this, tr("Confirm Delete"), msg,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    deleteObjects(names);
}

void AttributeTablePanel::onChangeTypeTriggered()
{
    // Resolve the right-clicked object, then hand off to the shared
    // TypeConversionFlow (node + link). The flow warns that type-specific
    // attributes will be lost, converts via the layer, and shows the
    // engine-reported cleared fields + topology warnings.
    if (!m_view || !m_model || !m_layer || !m_layer->engine()) return;

    const auto sel = m_view->selectionModel();
    if (!sel) return;
    const auto rows = sel->selectedRows();
    if (rows.isEmpty()) {
        QMessageBox::information(this, tr("Change Type"),
            tr("Select a row first."));
        return;
    }
    const QModelIndex srcIdx = m_proxy->mapToSource(rows.first());
    const QString name = m_model->objectNameAt(srcIdx.row());
    if (name.isEmpty()) return;

    const auto cat = m_model->category();
    const bool isNode = (cat == SWMMModelLayer::CatJunctions ||
                         cat == SWMMModelLayer::CatOutfalls  ||
                         cat == SWMMModelLayer::CatStorage   ||
                         cat == SWMMModelLayer::CatDividers);
    const bool isLink = (cat == SWMMModelLayer::CatConduits  ||
                         cat == SWMMModelLayer::CatPumps     ||
                         cat == SWMMModelLayer::CatOrifices  ||
                         cat == SWMMModelLayer::CatWeirs     ||
                         cat == SWMMModelLayer::CatOutlets);
    if (!isNode && !isLink) {
        QMessageBox::information(this, tr("Change Type"),
            tr("Only nodes and links can be converted to another type."));
        return;
    }

    // Read the current type so the picker can exclude it (the engine
    // rejects a same-type convert with SWMM_ERR_BADPARAM). Nodes have four
    // kinds, links five.
    SWMM_Engine eng = m_layer->engine();
    const QByteArray id = name.toUtf8();
    const int idx = isNode ? swmm_node_index(eng, id.constData())
                           : swmm_link_index(eng, id.constData());
    if (idx < 0) return;
    int currentType = 0;
    if (isNode) swmm_node_get_type(eng, idx, &currentType);
    else        swmm_link_get_type(eng, idx, &currentType);

    QStringList labels;
    QVector<int> values;
    const int nKinds = isNode ? 4 : 5;
    for (int t = 0; t < nKinds; ++t) {
        if (t == currentType) continue;
        labels << (isNode
            ? openswmmvis::ui::TypeConversionFlow::nodeTypeLabel(t)
            : openswmmvis::ui::TypeConversionFlow::linkTypeLabel(t));
        values << t;
    }

    bool ok = false;
    const QString choice = QInputDialog::getItem(this,
        isNode ? tr("Change Node Type") : tr("Change Link Type"),
        tr("Convert <b>%1</b> to:").arg(name),
        labels, 0, false, &ok);
    if (!ok || choice.isEmpty()) return;
    const int newType = values[labels.indexOf(choice)];

    // The shared flow confirms, converts via the layer (which emits the
    // geometryChanged / attributeChanged signals this panel and the
    // Property Browser already listen to), and shows the summary. No
    // explicit reloadGeometry()/refresh()/emit is needed here.
    openswmmvis::ui::TypeConversionFlow::run(this, m_layer, isNode, name,
                                             currentType, newType);
}
