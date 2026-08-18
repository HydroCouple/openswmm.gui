/*!
 * \file   attributetablepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/attributetablepanel.h"
#include "ui/panels/attributedelegates.h"
#include "ui/models/userflagsmodel.h"
#include "ui/panels/meshattributetablemodel.h"
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
#include "layers/swmm2dmeshlayer.h"
#include "layers/tabulardatalayer.h"
#include "layers/gisvectorlayer.h"
#include "mesh/meshobjectref.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/mapundostack.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDebug>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
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
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QTimer>
#include <QUndoStack>
#include <QToolBar>
#include <QVBoxLayout>
#include <QAbstractTableModel>
#include <QVector>

#include <ogrsf_frmts.h>
#include <ogr_feature.h>
#include <ogr_geometry.h>

#include <algorithm>   // std::sort / std::unique — selectionAsTsv row ordering
#include <utility>     // std::move — GIS attribute-row caching

Q_LOGGING_CATEGORY(lcAttrTbl, "openswmm.attr-table")

// ---------------------------------------------------------------------------
// GISVectorAttributeTableModel — read-only QAbstractTableModel over an
// externally loaded OGR feature layer (Shapefile / GeoPackage / GeoJSON …),
// so the Attribute Table view can display feature attributes the same way it
// shows SWMM categories and tabular (CSV/TSV) layers. Mirrors
// TabularDataTableModel: attributes are cached in memory on setLayer() and
// served read-only. Selection ops + cross-view selection are no-ops when this
// source is active (the layer has no SWMM object refs) — the panel already
// guards those on `sourceModel() == m_model`. No Q_OBJECT: it declares no new
// signals/slots, so no moc/CMake change is required.
// ---------------------------------------------------------------------------
class GISVectorAttributeTableModel : public QAbstractTableModel
{
public:
    explicit GISVectorAttributeTableModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent) {}

    /*! Bind to a feature layer (nullptr clears). Reads columns + rows now. */
    void setLayer(GISVectorLayer *layer)
    {
        m_layer = layer;
        reload();
    }
    [[nodiscard]] GISVectorLayer *layer() const { return m_layer.data(); }

    /*! Re-read the field schema and every feature from the bound OGR layer. */
    void reload()
    {
        beginResetModel();
        m_headers.clear();
        m_rows.clear();

        OGRLayer *ol = m_layer ? m_layer->ogrLayer() : nullptr;
        OGRFeatureDefn *defn = ol ? ol->GetLayerDefn() : nullptr;
        if (ol && defn) {
            const int nFields = defn->GetFieldCount();
            m_headers.reserve(nFields);
            for (int i = 0; i < nFields; ++i)
                m_headers << QString::fromUtf8(defn->GetFieldDefn(i)->GetNameRef());

            // The canvas sets a spatial filter on this OGRLayer while rendering.
            // Clear it so the table lists every feature, then restore it so we
            // don't disturb the map. (Clone first: SetSpatialFilter(nullptr)
            // deletes the layer's internal filter, dangling GetSpatialFilter's
            // return.) The attribute filter is left in place so the table
            // matches the map's filtered feature set.
            OGRGeometry *savedFilter = ol->GetSpatialFilter();
            OGRGeometry *savedClone  = savedFilter ? savedFilter->clone() : nullptr;
            ol->SetSpatialFilter(nullptr);
            ol->ResetReading();

            OGRFeature *f = nullptr;
            while ((f = ol->GetNextFeature()) != nullptr) {
                QVector<QVariant> row;
                row.reserve(nFields);
                for (int i = 0; i < nFields; ++i) {
                    if (!f->IsFieldSetAndNotNull(i)) { row.push_back(QVariant()); continue; }
                    switch (defn->GetFieldDefn(i)->GetType()) {
                    case OFTInteger:
                        row.push_back(f->GetFieldAsInteger(i)); break;
                    case OFTInteger64:
                        row.push_back(static_cast<qlonglong>(f->GetFieldAsInteger64(i))); break;
                    case OFTReal:
                        row.push_back(f->GetFieldAsDouble(i)); break;
                    default:
                        row.push_back(QString::fromUtf8(f->GetFieldAsString(i))); break;
                    }
                }
                m_rows.push_back(std::move(row));
                OGRFeature::DestroyFeature(f);
            }

            ol->SetSpatialFilter(savedClone);   // layer clones internally
            if (savedClone) OGRGeometryFactory::destroyGeometry(savedClone);
            ol->ResetReading();
        }
        endResetModel();
    }

    [[nodiscard]] QStringList columnHeaders() const { return m_headers; }

    int rowCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }

    int columnCount(const QModelIndex &parent = {}) const override
    { return parent.isValid() ? 0 : static_cast<int>(m_headers.size()); }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid()) return {};
        if (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole)
            return {};
        const int r = index.row(), c = index.column();
        if (r < 0 || r >= m_rows.size() || c < 0 || c >= m_headers.size()) return {};
        return m_rows[r].value(c);
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole) return {};
        if (orientation == Qt::Horizontal
            && section >= 0 && section < m_headers.size())
            return m_headers[section];
        if (orientation == Qt::Vertical) return section + 1;
        return {};
    }

private:
    QPointer<GISVectorLayer>   m_layer;
    QStringList                m_headers;
    QVector<QVector<QVariant>> m_rows;
};

namespace {

// Column specs for whichever of the two spec-carrying models `src` is,
// or an empty list for the tabular / GIS sources (which have headers
// but no ColumnSpec schema).
QList<openswmmvis::ColumnSpec> specsFor(const QAbstractItemModel *src)
{
    if (auto *swmm = qobject_cast<const SWMMAttributeTableModel *>(src))
        return swmm->columnSpecs();
    if (auto *mesh = qobject_cast<const MeshAttributeTableModel *>(src))
        return mesh->columnSpecs();
    return {};
}

// Evaluates one QueryPredicate against a source model, materialising ONLY
// the columns the predicate actually names.
//
// This used to build a whole-row QVariantMap over every column — and then
// a second one over columnSpecs() — just to test a predicate that reads
// one or two of them.  On the 272k-conduit corpus model that was ~30M
// data() calls per filter pass, each carrying a ~101-deep string-compare
// tag dispatch and an engine name→index lookup.  Resolving field name →
// column once in bind() makes it ~one data() call per row per referenced
// field.
class RowPredicate {
public:
    /*! Resolve `p`'s field names against `src`'s columns.  Cheap to call
     *  again; it is the per-row work this exists to avoid. */
    void bind(QAbstractItemModel *src, const openswmmvis::QueryPredicate &p)
    {
        m_src  = src;
        m_pred = p;
        m_cols.clear();
        if (!src || !p.isValid()) return;

        const auto specs = specsFor(src);
        const int  nCol  = src->columnCount();

        for (const QString &field : openswmmvis::queryFieldNames(p)) {
            int hit = -1;
            // Two passes so an exact match anywhere beats a
            // case-insensitive one earlier in the table.
            for (int pass = 0; pass < 2 && hit < 0; ++pass) {
                const auto cs = (pass == 0) ? Qt::CaseSensitive
                                            : Qt::CaseInsensitive;
                for (int c = 0; c < nCol; ++c) {
                    // Compound cells hand back a QVariant-wrapped edit-ref
                    // struct that no predicate can compare against, and
                    // reading one runs an engine-wide scan (LID usages,
                    // land uses, pollutants).  Never resolve to one.
                    if (c < specs.size()
                        && specs[c].editor == openswmmvis::EditorKind::Compound)
                        continue;
                    const bool match =
                        (c < specs.size()
                         && (QString::compare(specs[c].key,   field, cs) == 0
                          || QString::compare(specs[c].label, field, cs) == 0))
                        || QString::compare(
                               src->headerData(c, Qt::Horizontal,
                                               Qt::DisplayRole).toString(),
                               field, cs) == 0;
                    if (match) { hit = c; break; }
                }
            }
            // A field naming no column contributes no map entry, which is
            // exactly what the old all-columns build did for an unknown
            // name: lookupField returns an invalid QVariant and every
            // comparison against it is false.
            if (hit >= 0) m_cols.append({hit, field});
        }
    }

    [[nodiscard]] bool accepts(int srcRow, const QModelIndex &parent = {}) const
    {
        if (!m_pred.root) return true;   // no filter
        if (!m_src) return true;
        QVariantMap m;
        for (const auto &col : m_cols) {
            const QModelIndex idx = m_src->index(srcRow, col.first, parent);
            // Keyed by the name as the user typed it, so lookupField's
            // exact-key probe hits and its linear fallback never runs.
            m.insert(col.second, m_src->data(idx, Qt::DisplayRole));
        }
        return openswmmvis::evaluateQuery(m_pred, m);
    }

private:
    QAbstractItemModel          *m_src = nullptr;
    openswmmvis::QueryPredicate  m_pred;
    QVector<QPair<int, QString>> m_cols;   // (source column, field as typed)
};

// Slice Z.2 — proxy that composes the "show selected only" name filter
// with a query-predicate filter.  A row is accepted when (a) its name is
// in the selected-name set (when that filter is active), AND (b) the
// predicate evaluates true (when set).  Either filter being unset is a
// pass.
//
// The name filter used to be a QSortFilterProxyModel regex built as an
// alternation over every selected name — `^(?:a|b|c|…)$` — which was
// recompiled and re-matched against every row on each selection change.
// A QSet probe is the same test without the quadratic blowup.
class FilteringProxy : public QSortFilterProxyModel {
public:
    explicit FilteringProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

    /*! `text` is the string `p` was parsed from; `queryText()` hands it
     *  back so callers can tell whether the proxy's visible rows are
     *  still the match set for what is in the query bar. */
    void setQueryPredicate(const openswmmvis::QueryPredicate &p,
                           const QString &text = {})
    {
        m_predicate = p;
        m_queryText = text;
        m_bound     = false;
        invalidateFilter();
    }
    [[nodiscard]] QString queryText() const { return m_queryText; }

    /*! Restrict to rows whose name (column `filterKeyColumn()`) is in
     *  `names`.  `active == false` clears the restriction; an empty set
     *  with `active == true` matches nothing, which is what the old
     *  `(?!)` sentinel pattern expressed. */
    void setNameFilter(const QSet<QString> &names, bool active)
    {
        if (m_nameFilterActive == active && m_names == names) return;
        m_names            = names;
        m_nameFilterActive = active;
        invalidateFilter();
    }
    [[nodiscard]] bool nameFilterActive() const { return m_nameFilterActive; }

    void setSourceModel(QAbstractItemModel *src) override
    {
        // Drop ONLY our own connection, by handle. A blanket
        // disconnect(src, &QAbstractItemModel::modelReset, this, nullptr)
        // also tears down QSortFilterProxyModel's OWN internal reaction to
        // modelReset — and because the base setSourceModel early-returns
        // when handed the same pointer it already has (which is exactly
        // what onCategoryChanged does: setSource() resets the model, then
        // re-hands the proxy the same m_model), it never gets rebuilt.
        // The proxy then stops seeing source resets entirely and its row
        // count freezes on the previous category.
        QObject::disconnect(m_resetConn);
        QSortFilterProxyModel::setSourceModel(src);
        m_bound = false;
        // A category switch resets the model AND rebuilds its column
        // schema while the predicate persists, so cached column indices
        // would otherwise point into the previous category's schema.
        if (src)
            m_resetConn = connect(src, &QAbstractItemModel::modelReset, this,
                                  [this] { m_bound = false; });
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        auto *src = sourceModel();
        if (!src) return true;
        if (m_nameFilterActive) {
            const QModelIndex nameIdx =
                src->index(row, filterKeyColumn(), parent);
            if (!m_names.contains(src->data(nameIdx, filterRole()).toString()))
                return false;
        }
        if (!m_predicate.isValid()) return true;
        ensureBound();
        return m_rp.accepts(row, parent);
    }

private:
    void ensureBound() const {
        if (m_bound) return;
        m_rp.bind(sourceModel(), m_predicate);
        m_bound = true;
    }

    openswmmvis::QueryPredicate m_predicate;
    QString                     m_queryText;
    QSet<QString>               m_names;
    bool                        m_nameFilterActive = false;
    mutable RowPredicate        m_rp;
    mutable bool                m_bound = false;
    QMetaObject::Connection     m_resetConn;
};

} // anonymous

namespace {

// Apply a set of SOURCE rows to the view's selection in one emission.
// Selecting one index at a time makes QItemSelectionModel re-merge its
// whole range list per call and fire selectionChanged on each, so a
// large selection was quadratic; coalescing contiguous runs into ranges
// and issuing a single select() is the same result in one pass.
//
// ClearAndSelect with an empty selection clears, which is what the
// preceding clearSelection() used to do.
void selectSourceRows(QItemSelectionModel *sel, QSortFilterProxyModel *proxy,
                      QAbstractItemModel *src, const QList<int> &srcRows)
{
    if (!sel || !proxy || !src) return;
    const int lastCol = proxy->columnCount() - 1;
    QList<int> prxRows;
    prxRows.reserve(srcRows.size());
    for (int r : srcRows) {
        const QModelIndex p = proxy->mapFromSource(src->index(r, 0));
        if (p.isValid()) prxRows << p.row();
    }
    std::sort(prxRows.begin(), prxRows.end());
    prxRows.erase(std::unique(prxRows.begin(), prxRows.end()), prxRows.end());

    QItemSelection selection;
    if (lastCol >= 0) {
        for (int i = 0; i < prxRows.size(); ) {
            int j = i;
            while (j + 1 < prxRows.size() && prxRows[j + 1] == prxRows[j] + 1) ++j;
            selection.append(QItemSelectionRange(
                proxy->index(prxRows[i], 0), proxy->index(prxRows[j], lastCol)));
            i = j + 1;
        }
    }
    sel->select(selection, QItemSelectionModel::ClearAndSelect
                             | QItemSelectionModel::Rows);
}

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

// ---------------------------------------------------------------------------
// 2D mesh source keys — the category combo stores "mesh:<layerId>:<v|e|c>" for
// each of a mesh layer's three element tables, alongside the existing "tab:" /
// "gis:" payloads.
// ---------------------------------------------------------------------------
const QLatin1String kMeshPrefix("mesh:");

QChar meshKindChar(MeshAttributeTableModel::Kind k)
{
    switch (k) {
    case MeshAttributeTableModel::Kind::Vertex: return QLatin1Char('v');
    case MeshAttributeTableModel::Kind::Edge:   return QLatin1Char('e');
    case MeshAttributeTableModel::Kind::Cell:   return QLatin1Char('c');
    }
    return QLatin1Char('v');
}

QString meshSourceKey(const SWMM2DMeshLayer *layer,
                      MeshAttributeTableModel::Kind kind)
{
    return QStringLiteral("mesh:%1:%2").arg(layer->layerId())
                                        .arg(meshKindChar(kind));
}

/*! QSettings sub-key for a mesh table's column widths. Keyed by ELEMENT KIND,
 *  not by layer: layerId is a fresh UUID every session, so keying on it would
 *  mean the saved layout never comes back. The schema is identical for every
 *  mesh of a given kind, so one remembered layout per kind is the useful unit. */
QString meshWidthsKey(MeshAttributeTableModel::Kind kind)
{
    return QStringLiteral("mesh-%1").arg(meshKindChar(kind));
}

/*! Split a "mesh:<layerId>:<kind>" payload. Returns false when it isn't one. */
bool parseMeshSourceKey(const QString &key, QString *layerId,
                        MeshAttributeTableModel::Kind *kind)
{
    if (!key.startsWith(kMeshPrefix)) return false;
    const int sep = key.lastIndexOf(QLatin1Char(':'));
    if (sep <= int(kMeshPrefix.size()) - 1) return false;
    if (layerId) *layerId = key.mid(kMeshPrefix.size(), sep - kMeshPrefix.size());
    if (kind) {
        const QChar k = key.at(sep + 1);
        *kind = (k == QLatin1Char('e')) ? MeshAttributeTableModel::Kind::Edge
              : (k == QLatin1Char('c')) ? MeshAttributeTableModel::Kind::Cell
                                        : MeshAttributeTableModel::Kind::Vertex;
    }
    return true;
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
    qApp->installEventFilter(this);

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
    qApp->removeEventFilter(this);

    // Persist the column widths of the currently-active category one
    // last time so the next session opens with the same layout.
    if (meshSourceActive())
        saveColumnWidths(meshWidthsKey(m_meshModel->kind()));
    else if (m_model)
        saveColumnWidths(m_model->category());
}

bool AttributeTablePanel::eventFilter(QObject *watched, QEvent *event)
{
    auto *watchedWidget = qobject_cast<QWidget *>(watched);
    if (!m_view || !watchedWidget)
        return QWidget::eventFilter(watched, event);

    const bool isViewOrDescendant =
        (watchedWidget == m_view) || m_view->isAncestorOf(watchedWidget);
    if (!isViewOrDescendant)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            // Keep Enter local to table editing; do not leak to global shortcuts.
            event->accept();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            // Let delegates commit, then keep focus anchored in the table.
            QTimer::singleShot(0, m_view, [view = m_view]() {
                if (view)
                    view->setFocus(Qt::OtherFocusReason);
            });
        }
    }

    return QWidget::eventFilter(watched, event);
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
    m_gisModel     = new GISVectorAttributeTableModel(this);
    m_meshModel    = new MeshAttributeTableModel(this);
    m_proxy        = new FilteringProxy(this);
    m_proxy->setSourceModel(m_model);

    // Mesh edits are undoable through the shared map stack, so an edit made
    // here refreshes the mesh toolbar and the map the same way a toolbar edit
    // refreshes this table.
    connect(m_meshModel, &MeshAttributeTableModel::objectEdited,
            this, [this](const QString &refName) {
                if (!m_suppressEditForward) emit objectEdited(refName);
            });

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
    // No setFilterCaseSensitivity: the name filter is a QSet probe now,
    // and both sides of it are the model's own spelling of the name
    // (the set is built from refs that came out of objectNameAt(), which
    // is also what column 0 displays), so an exact match always holds.

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
    connect(m_view->verticalHeader(), &QHeaderView::sectionDoubleClicked,
            this, &AttributeTablePanel::onRowHeaderDoubleClicked);
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
        QObject::disconnect(m_layer, &SWMMModelLayer::dataObjectsChanged,
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
        // Queued: providerAboutToBeRemoved-driven emissions arrive before
        // the removal lands — deferring to the next event-loop turn makes
        // refresh() read post-mutation state.
        connect(m_layer, &SWMMModelLayer::dataObjectsChanged,
                this,    &AttributeTablePanel::refresh,
                static_cast<Qt::ConnectionType>(Qt::QueuedConnection
                                                | Qt::UniqueConnection));
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
    // The mesh model pushes through mesh::push*ParamEdit, which take the
    // canvas and find the stack themselves. The model layer supplies the
    // candidate lists behind the coupled-node / time-series / curve pickers.
    if (m_meshModel) {
        m_meshModel->setCanvas(m_canvas);
        m_meshModel->setModelLayer(m_layer);
    }

    refresh();
}

void AttributeTablePanel::setResultsSource(SWMMResultsLayer *layer)
{
    if (m_model) m_model->setResultsSource(layer);
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
    // List externally loaded GIS feature layers (Shapefile / GeoPackage /
    // GeoJSON …). Data convention: combo data is "gis:<layerId>".
    if (m_canvas) {
        bool addedSeparator = false;
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            auto *gis = qobject_cast<GISVectorLayer *>(l);
            if (!gis) continue;
            if (!addedSeparator && m_categoryCombo->count() > 0) {
                m_categoryCombo->insertSeparator(m_categoryCombo->count());
                addedSeparator = true;
            }
            m_categoryCombo->addItem(
                QStringLiteral("◆ Features: %1 (%2)")
                    .arg(gis->name()).arg(gis->featureCount()),
                QStringLiteral("gis:%1").arg(gis->layerId()));
            // GIS layers open asynchronously; refresh the combo + active source
            // once the dataset is ready so the row/feature counts populate.
            connect(gis, &GISVectorLayer::openFinished,
                    this, &AttributeTablePanel::refresh, Qt::UniqueConnection);
        }
    }
    // 2D mesh layers contribute three tables each — vertices, edges, cells.
    // Until the deferred heavy geometry lands the edge pairing and boundary
    // flags do not exist, so the entries are listed but disabled; the
    // sceneGeometryReady connection re-runs refresh() to enable them.
    if (m_canvas) {
        bool addedSeparator = false;
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            auto *mesh = qobject_cast<SWMM2DMeshLayer *>(l);
            if (!mesh) continue;
            if (!addedSeparator && m_categoryCombo->count() > 0) {
                m_categoryCombo->insertSeparator(m_categoryCombo->count());
                addedSeparator = true;
            }
            const bool ready = mesh->sceneGeometryComplete();
            struct { MeshAttributeTableModel::Kind kind; QString label; int count; } rows[] = {
                {MeshAttributeTableModel::Kind::Vertex, tr("Vertices"), mesh->vertexCount()},
                {MeshAttributeTableModel::Kind::Edge,   tr("Edges"),    mesh->edgeCount()},
                {MeshAttributeTableModel::Kind::Cell,   tr("Cells"),    mesh->triangleCount()},
            };
            for (const auto &r : rows) {
                m_categoryCombo->addItem(
                    QStringLiteral("△ Mesh %1 — %2 (%3)")
                        .arg(mesh->name(), r.label).arg(r.count),
                    meshSourceKey(mesh, r.kind));
                const int i = m_categoryCombo->count() - 1;
                if (!ready) {
                    auto *model = qobject_cast<QStandardItemModel *>(
                        m_categoryCombo->model());
                    if (auto *item = model ? model->item(i) : nullptr)
                        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                    m_categoryCombo->setItemData(
                        i, tr("Available once the mesh finishes loading."),
                        Qt::ToolTipRole);
                }
            }
            connect(mesh, &SWMM2DMeshLayer::sceneGeometryReady,
                    this, &AttributeTablePanel::refresh, Qt::UniqueConnection);
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
        } else if (data.toString().startsWith(QStringLiteral("gis:"))) {
            const QString layerId = data.toString().mid(4);
            GISVectorLayer *gis = nullptr;
            if (m_canvas) {
                for (OpenSWMMVisLayer *l : m_canvas->layers()) {
                    if (l->layerId() == layerId) {
                        gis = qobject_cast<GISVectorLayer *>(l);
                        break;
                    }
                }
            }
            m_gisModel->setLayer(gis);
            m_proxy->setSourceModel(m_gisModel);
            // Feature layer: no SWMM delegates / no per-category widths.
            for (int c = 0; c < m_proxy->columnCount(); ++c)
                m_view->setItemDelegateForColumn(c, nullptr);
        } else if (data.toString().startsWith(kMeshPrefix)) {
            bindMeshSource(data.toString());
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

void AttributeTablePanel::showLayerSource(OpenSWMMVisLayer *layer)
{
    if (!layer) return;

    QString key;
    if (qobject_cast<GISVectorLayer *>(layer))
        key = QStringLiteral("gis:%1").arg(layer->layerId());
    else if (qobject_cast<TabularDataLayer *>(layer))
        key = QStringLiteral("tab:%1").arg(layer->layerId());
    else if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer))
        // A mesh layer contributes three tables; open the vertices one and let
        // the user switch to edges / cells from the combo.
        key = meshSourceKey(mesh, MeshAttributeTableModel::Kind::Vertex);
    if (key.isEmpty())
        return;   // model / results layer — keep the current SWMM category

    int idx = m_categoryCombo->findData(key);
    if (idx < 0) {
        // Layer added since the combo was last rebuilt.
        refresh();
        idx = m_categoryCombo->findData(key);
    }
    if (idx >= 0)
        m_categoryCombo->setCurrentIndex(idx);   // fires onCategoryChanged
}

void AttributeTablePanel::onCategoryChanged(int /*comboIdx*/)
{
    if (m_categoryCombo->currentIndex() < 0) return;

    // Save the outgoing SWMM category's column widths before any
    // model swap so the next visit to it restores the same layout.
    if (m_proxy->sourceModel() == m_model) {
        const auto previous = m_model->category();
        saveColumnWidths(previous);
    } else if (meshSourceActive()) {
        saveColumnWidths(meshWidthsKey(m_meshModel->kind()));
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
    } else if (data.toString().startsWith(QStringLiteral("gis:"))) {
        // External OGR feature-layer source.
        const QString layerId = data.toString().mid(4);
        GISVectorLayer *gis = nullptr;
        if (m_canvas) {
            for (OpenSWMMVisLayer *l : m_canvas->layers()) {
                if (l->layerId() == layerId) {
                    gis = qobject_cast<GISVectorLayer *>(l);
                    break;
                }
            }
        }
        m_gisModel->setLayer(gis);
        m_proxy->setSourceModel(m_gisModel);
        for (int c = 0; c < m_proxy->columnCount(); ++c)
            m_view->setItemDelegateForColumn(c, nullptr);
    } else if (data.toString().startsWith(kMeshPrefix)) {
        bindMeshSource(data.toString());
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
    saveColumnWidths(QStringLiteral("cat%1").arg(static_cast<int>(cat)));
}

void AttributeTablePanel::saveColumnWidths(const QString &sourceKey) const
{
    if (!m_view || sourceKey.isEmpty()) return;
    auto *header = m_view->horizontalHeader();
    if (!header || header->count() == 0) return;
    QSettings s;
    const QString scope = projectScopeKeyFor(m_model ? m_model->layer() : nullptr);
    s.setValue(QStringLiteral("SWMMVis/AttributeTablePanel/projects/%1/%2/columnWidths")
                   .arg(scope, sourceKey),
               header->saveState());
}

void AttributeTablePanel::restoreColumnWidths(const QString &sourceKey)
{
    if (!m_view || sourceKey.isEmpty()) return;
    auto *header = m_view->horizontalHeader();
    if (!header) return;
    QSettings s;
    const QString scope = projectScopeKeyFor(m_model ? m_model->layer() : nullptr);
    const QByteArray state =
        s.value(QStringLiteral("SWMMVis/AttributeTablePanel/projects/%1/%2/columnWidths")
                    .arg(scope, sourceKey)).toByteArray();
    if (!state.isEmpty()) header->restoreState(state);
    ensureMinColumnWidths();
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
    installColumnDelegates(m_model->columnSpecs(), m_model->columnCount());
}

void AttributeTablePanel::installColumnDelegates(
    const QList<openswmmvis::ColumnSpec> &specs, int clearUpTo)
{
    if (!m_view) return;
    // Clear any delegates installed from the previous category.
    // QTableView doesn't own the delegate; we keep parents on the
    // panel so they're destroyed with the panel.  The header count is in the
    // sweep because the outgoing source may have had MORE columns than the
    // incoming one — a delegate left on a high index would otherwise linger.
    int clearTo = std::max(clearUpTo, int(specs.size()));
    if (auto *header = m_view->horizontalHeader())
        clearTo = std::max(clearTo, header->count());
    for (int col = 0; col < clearTo; ++col)
        m_view->setItemDelegateForColumn(col, nullptr);

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
        case openswmmvis::EditorKind::FileBrowse:
            del = new openswmmvis::FileBrowseDelegate(this, spec.fileFilter);
            break;
        case openswmmvis::EditorKind::FileColumn:
            del = new openswmmvis::FileColumnDelegate(
                this, openswmmvis::kFileColumnOptionsRole);
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

// ---------------------------------------------------------------------------
// 2D mesh source — vertices / edges / cells of a loaded SWMM2DMeshLayer
//
// The mesh model speaks the same ColumnSpec vocabulary as the SWMM model, so
// the delegates, the query bar and the bulk "apply to selected rows" flow all
// work unchanged.  What differs is identity: rows map to MeshObjectRefs rather
// than object names, which is why the selection sync has its own pair of
// helpers instead of reusing the name-keyed ones.
// ---------------------------------------------------------------------------

bool AttributeTablePanel::meshSourceActive() const
{
    return m_proxy && m_meshModel && m_proxy->sourceModel() == m_meshModel;
}

void AttributeTablePanel::bindMeshSource(const QString &key)
{
    if (!m_meshModel || !m_proxy) return;

    QString layerId;
    MeshAttributeTableModel::Kind kind = MeshAttributeTableModel::Kind::Vertex;
    SWMM2DMeshLayer *mesh = nullptr;
    if (parseMeshSourceKey(key, &layerId, &kind) && m_canvas) {
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            if (l->layerId() == layerId) {
                mesh = qobject_cast<SWMM2DMeshLayer *>(l);
                break;
            }
        }
    }

    m_meshModel->setCanvas(m_canvas);
    m_meshModel->setModelLayer(m_layer);
    m_meshModel->setSource(mesh, kind);
    m_proxy->setSourceModel(m_meshModel);
    installColumnDelegates(m_meshModel->columnSpecs(),
                           m_meshModel->columnCount());
    restoreColumnWidths(meshWidthsKey(kind));
}

QSet<SWMMObjectRef> AttributeTablePanel::meshRefs(bool applyQuery) const
{
    QSet<SWMMObjectRef> out;
    if (!meshSourceActive()) return out;

    openswmmvis::QueryPredicate pred;
    if (applyQuery && m_queryEdit) {
        const QString text = m_queryEdit->text().trimmed();
        pred = openswmmvis::parseQuery(text);
        // Parse error → empty match set; the query bar already shows why.
        if (!text.isEmpty() && !pred.isValid()) return out;
    }

    // Same column-resolving predicate the proxy filters with, so the mesh
    // selection ops and the visible rows agree — and so a query naming one
    // column reads one column per row instead of every column.
    RowPredicate rp;
    rp.bind(m_meshModel, pred);
    const int nRow = m_meshModel->rowCount();
    for (int row = 0; row < nRow; ++row) {
        if (!rp.accepts(row)) continue;
        const SWMMObjectRef ref = m_meshModel->refForRow(row);
        if (ref.isValid()) out.insert(ref);
    }
    return out;
}

void AttributeTablePanel::meshSelectionToBus()
{
    if (!m_selMgr || !meshSourceActive() || !m_view) return;
    auto *sel = m_view->selectionModel();
    if (!sel) return;

    QSet<SWMMObjectRef> refs;
    for (const QModelIndex &proxyIdx : sel->selectedRows()) {
        const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
        const SWMMObjectRef ref = m_meshModel->refForRow(srcIdx.row());
        if (ref.isValid()) refs.insert(ref);
    }
    // Replace, matching what picking rows in a SWMM category does.
    m_selMgr->select(refs, SelectionManager::Replace);
    // Picking a row says nothing about WHERE the cell is — and at model-wide
    // zoom its highlight is sub-pixel. Raise the map beacon so the selection
    // is findable. Interactive map picks deliberately do not do this: you
    // just clicked the thing.
    if (m_canvas) m_canvas->flashSelection();
}

void AttributeTablePanel::meshSelectionFromBus(const QSet<SWMMObjectRef> &current)
{
    if (!meshSourceActive() || !m_view) return;
    auto *sel = m_view->selectionModel();
    if (!sel) return;

    m_applyingFromBus = true;

    // Resolve the bus refs to rows first — the "show selected only" filter is
    // driven off the id column's text, which is what the rows display.
    QList<int> rows;
    for (const auto &ref : current) {
        const int row = m_meshModel->rowForRef(ref);
        if (row >= 0) rows << row;
    }

    auto *fp = static_cast<FilteringProxy *>(m_proxy);
    if (m_showSelectedOnly) {
        QSet<QString> ids;
        ids.reserve(rows.size());
        for (int row : std::as_const(rows)) {
            ids.insert(m_meshModel->data(m_meshModel->index(row, 0),
                                         Qt::DisplayRole).toString());
        }
        fp->setNameFilter(ids, true);
    } else {
        fp->setNameFilter({}, false);
    }

    selectSourceRows(sel, m_proxy, m_meshModel, rows);

    m_applyingFromBus = false;
}

void AttributeTablePanel::onTableSelectionChanged()
{
    if (m_applyingFromBus || !m_selMgr || !m_model || !m_view || !m_proxy) return;
    if (meshSourceActive()) { meshSelectionToBus(); return; }
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
    // See meshSelectionToBus: locate the row's feature on the map. Guarded
    // by the m_applyingFromBus early-return above, so this only fires on a
    // genuine user pick in the table, never on a bus-driven sync.
    if (m_canvas) m_canvas->flashSelection();
}

void AttributeTablePanel::onSelectionManagerChanged(
    const QSet<SWMMObjectRef> &current,
    const QSet<SWMMObjectRef> & /*added*/,
    const QSet<SWMMObjectRef> & /*removed*/)
{
    if (!m_view || !m_proxy) return;
    if (meshSourceActive()) { meshSelectionFromBus(current); return; }
    if (!m_layer || !m_model) return;
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
    // the filter on → an empty set, which matches nothing.
    auto *fp = static_cast<FilteringProxy *>(m_proxy);
    if (m_showSelectedOnly) {
        QSet<QString> names;
        names.reserve(current.size());
        for (const auto &ref : current)
            if (ref.objectType == type) names.insert(ref.name);
        fp->setNameFilter(names, true);
    } else {
        // setNameFilter early-returns when nothing changes, so this no
        // longer needs the "only clear when something was set" guard
        // that avoided a gratuitous reset on every selection change.
        fp->setNameFilter({}, false);
    }

    // Now sync the view's row selection to the current set.
    QList<int> rows;
    rows.reserve(current.size());
    for (const auto &ref : current) {
        if (ref.objectType != type) continue;
        const int srcRow = m_model->rowForName(ref.name);
        if (srcRow >= 0) rows << srcRow;
    }
    selectSourceRows(sel, m_proxy, m_model, rows);

    m_applyingFromBus = false;
}

void AttributeTablePanel::onShowSelectedOnlyToggled(bool on)
{
    m_showSelectedOnly = on;
    if (m_selMgr)
        onSelectionManagerChanged(m_selMgr->selection(), {}, {});
    else
        static_cast<FilteringProxy *>(m_proxy)->setNameFilter({}, false);
}

void AttributeTablePanel::onRowHeaderDoubleClicked(int row)
{
    if (!m_view || row < 0) return;
    // A double-clicked row that isn't part of the selection replaces it —
    // selectRow flows through onTableSelectionChanged into the bus, so
    // onZoomToSelectedClicked reads a selection that includes this row.
    if (!m_view->selectionModel()->isRowSelected(row, QModelIndex()))
        m_view->selectRow(row);
    onZoomToSelectedClicked();
}

void AttributeTablePanel::onZoomToSelectedClicked()
{
    if (!m_canvas || !m_selMgr || m_selMgr->isEmpty()) return;

    // The extent is accumulated in the SOURCE layer's own CRS, then projected
    // once — so the mesh branch resolves against the mesh layer, not the model.
    OpenSWMMVisLayer *sourceLayer = m_layer.data();
    MapExtent acc;
    bool any = false;

    if (meshSourceActive()) {
        SWMM2DMeshLayer *mesh = m_meshModel->layer();
        if (!mesh) return;
        sourceLayer = mesh;
        for (const auto &ref : m_selMgr->selection()) {
            const int row = m_meshModel->rowForRef(ref);
            if (row < 0) continue;
            bool resolved = false;
            const QRectF r = m_meshModel->elementExtent(row, &resolved);
            if (!resolved) continue;
            const MapExtent e(r.left(), r.top(), r.right(), r.bottom());
            if (!any) { acc = e; any = true; }
            else      { acc = acc.united(e); }
        }
    } else {
        if (!m_layer || !m_model) return;
        const auto type = objectTypeForCategory(m_model->category());

        // Build the layer-CRS bbox of the selected objects, skipping those
        // we can't resolve (e.g. ref belongs to a different category).
        for (const auto &ref : m_selMgr->selection()) {
            if (ref.objectType != type) continue;
            const MapExtent e = m_layer->objectExtent(ref.name);
            if (!std::isfinite(e.xMin()) || !std::isfinite(e.xMax())) continue;
            if (!any) { acc = e; any = true; }
            else      { acc = acc.united(e); }
        }
    }
    if (!any || !sourceLayer) return;

    // Project to canvas CRS, then add a small pad so the selection
    // doesn't land flush with the viewport edges.  Point selections
    // (zero-width bbox) get an absolute buffer derived from the
    // layer's overall extent — same heuristic as ObjectBrowser's
    // zoomToObject().
    MapExtent obj = m_canvas->extentInCanvasCRS(sourceLayer, acc);
    if (!std::isfinite(obj.xMin()) || !std::isfinite(obj.xMax())) return;

    double x0 = obj.xMin(), y0 = obj.yMin();
    double x1 = obj.xMax(), y1 = obj.yMax();
    const bool isPoint = (obj.width() == 0.0 && obj.height() == 0.0);
    if (isPoint) {
        double buf = 100.0;
        if (const MapExtent le = m_canvas->layerExtentInCanvasCRS(sourceLayer);
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
    if (!m_proxy || !m_proxy->sourceModel()
        || m_proxy->sourceModel()->rowCount() == 0)
        return;
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
    if (!m_view) return;
    const QModelIndex proxyIdx = m_view->indexAt(pos);
    if (!proxyIdx.isValid()) return;

    // Mesh source: Copy + bulk apply + Zoom. Change Type and Delete are SWMM
    // object operations with no mesh equivalent — a mesh element cannot be
    // deleted individually, and there is no type to convert.
    if (meshSourceActive()) {
        QMenu meshMenu(this);
        QAction *copy = meshMenu.addAction(tr("Copy (Ctrl+C)"));
        connect(copy, &QAction::triggered,
                this, &AttributeTablePanel::copySelectionToClipboard);
        meshMenu.addSeparator();

        const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
        const QVariant cellValue = srcIdx.isValid() ? srcIdx.data(Qt::EditRole)
                                                    : QVariant();
        const int col = srcIdx.isValid() ? srcIdx.column() : -1;
        const QList<openswmmvis::ColumnSpec> specs = m_meshModel->columnSpecs();
        const QList<int> selRows = selectedSourceRows();
        const bool clickedEditable =
            srcIdx.isValid() && (m_meshModel->flags(srcIdx) & Qt::ItemIsEditable);
        if (clickedEditable && col >= 0 && col < specs.size()
            && selRows.size() >= 2) {
            const openswmmvis::ColumnSpec &spec = specs[col];
            QAction *copyVal = meshMenu.addAction(
                tr("Apply this \"%1\" value to %2 selected rows")
                    .arg(spec.label).arg(selRows.size()));
            connect(copyVal, &QAction::triggered, this,
                    [this, col, selRows, cellValue]() {
                        applyValueToSelectedRows(col, selRows, cellValue);
                    });
            // The "prompt for a value" flavour needs a typed input dialog. A
            // reference column (coupled node / time series / curve) has no such
            // dialog — its whole point is that the value comes from a closed
            // picker — so only the copy-the-clicked-cell flavour is offered
            // there, and the copied id is one the picker already validated.
            if (spec.editor != openswmmvis::EditorKind::Compound) {
                QAction *promptVal = meshMenu.addAction(
                    tr("Apply \"%1\" value to %2 selected rows…")
                        .arg(spec.label).arg(selRows.size()));
                connect(promptVal, &QAction::triggered, this,
                        [this, col, selRows, cellValue]() {
                            bool ok = false;
                            const QVariant v = promptBulkValue(col, cellValue, &ok);
                            if (ok) applyValueToSelectedRows(col, selRows, v);
                        });
            }
            meshMenu.addSeparator();
        }

        QAction *zoom = meshMenu.addAction(tr("Zoom to selected"));
        zoom->setEnabled(m_canvas && m_selMgr && !m_selMgr->isEmpty());
        connect(zoom, &QAction::triggered,
                this, &AttributeTablePanel::onZoomToSelectedClicked);
        meshMenu.exec(m_view->viewport()->mapToGlobal(pos));
        return;
    }

    if (!m_model || !m_layer) return;

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
            case DataObjectRef::Node:           /* handled above */                   break;
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
    if (column < 0 || sourceRows.isEmpty()) return;
    // Whichever source is bound — SWMM objects or mesh elements — the writes
    // go through that model's setData, which pushes onto the same undo stack.
    QAbstractItemModel *model = meshSourceActive()
        ? static_cast<QAbstractItemModel *>(m_meshModel)
        : static_cast<QAbstractItemModel *>(m_model);
    if (!model) return;

    // Collapse the whole batch into one undo step when a stack is attached
    // (each setData pushes its own AttributeEditCommand inside the macro).
    QUndoStack *undo = meshSourceActive()
        ? (m_canvas ? static_cast<QUndoStack *>(m_canvas->undoStack()) : nullptr)
        : (m_model ? m_model->undoStack() : nullptr);
    if (undo)
        undo->beginMacro(tr("Apply value to %1 rows").arg(sourceRows.size()));
    for (int row : sourceRows) {
        const QModelIndex idx = model->index(row, column);
        if (!idx.isValid()) continue;
        // Skip rows whose cell isn't editable (running sim, an inapplicable
        // cross-section geom, or a BC field on an interior mesh edge).
        if (!(model->flags(idx) & Qt::ItemIsEditable)) continue;
        model->setData(idx, value, Qt::EditRole);
    }
    if (undo) undo->endMacro();
}

QVariant AttributeTablePanel::promptBulkValue(int column,
                                              const QVariant &current,
                                              bool *ok) const
{
    if (ok) *ok = false;
    const QList<openswmmvis::ColumnSpec> specs =
        meshSourceActive() ? m_meshModel->columnSpecs()
                           : (m_model ? m_model->columnSpecs()
                                      : QList<openswmmvis::ColumnSpec>{});
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

    // Per-leg timings for the perf harness. Off unless
    // QT_LOGGING_RULES="openswmm.attr-table.debug=true".
    QElapsedTimer legTimer;
    legTimer.start();
    fp->setQueryPredicate(pred, text);

    const int matched = m_proxy->rowCount();
    const qint64 filterMs = legTimer.restart();
    const int total   = m_proxy->sourceModel() ? m_proxy->sourceModel()->rowCount() : 0;
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

    qCDebug(lcAttrTbl).noquote()
        << "query apply: filter_ms=" << filterMs
        << " select_ms=" << legTimer.elapsed()
        << " matched=" << matched << "/" << total;
}

void AttributeTablePanel::onQueryClearClicked()
{
    if (!m_queryEdit || !m_proxy) return;
    auto *fp = static_cast<FilteringProxy *>(m_proxy);
    if (!fp) return;
    m_queryEdit->clear();
    m_queryEdit->setStyleSheet(QString());
    fp->setQueryPredicate({}, {});
    if (m_queryStatus) {
        const int total = m_proxy->sourceModel()
                              ? m_proxy->sourceModel()->rowCount() : 0;
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
    if (meshSourceActive()) return meshRefs(/*applyQuery=*/true);
    if (!m_model || !m_queryEdit) return out;
    // Z.4.3 — selection ops require a SWMM model source; tabular
    // sources have no SWMMObjectRefs.
    if (m_proxy && m_proxy->sourceModel() != m_model) return out;
    const SWMMObjectRef::ObjectType type =
        objectTypeForCategory(m_model->category());
    const QString text = m_queryEdit->text().trimmed();
    auto *fp = static_cast<FilteringProxy *>(m_proxy);

    // Fast path — the proxy has already evaluated this exact query, so its
    // visible rows ARE the match set.  This is the Apply path
    // (onQueryApplyClicked sets the predicate, then calls us through
    // onSelectionApplyClicked), which used to walk the whole grid a second
    // time to recompute what the filter had just computed.
    //
    // Only valid when the name filter is off: matchedRefs is documented to
    // ignore "show selected only" so the selection ops act on the full
    // population, and the proxy's rows are intersected with it when it's on.
    if (fp && !fp->nameFilterActive() && fp->queryText() == text) {
        const int nVisible = m_proxy->rowCount();
        for (int r = 0; r < nVisible; ++r) {
            const int srcRow =
                m_proxy->mapToSource(m_proxy->index(r, 0)).row();
            const QString name = m_model->objectNameAt(srcRow);
            if (!name.isEmpty()) out.insert(SWMMObjectRef(type, name));
        }
        return out;
    }

    // Slow path — the query bar was edited without pressing Apply, or the
    // name filter is on.  Evaluate directly, but through the same
    // column-resolving predicate the proxy uses so the two always agree.
    const auto pred = openswmmvis::parseQuery(text);
    // Parse error → empty match set (the query bar shows the error
    // already; the selection ops are no-ops rather than surprising).
    if (!text.isEmpty() && !pred.isValid()) return out;

    RowPredicate rp;
    rp.bind(m_model, pred);
    const int nRow = m_model->rowCount();
    for (int row = 0; row < nRow; ++row) {
        const QString name = m_model->objectNameAt(row);
        if (name.isEmpty()) continue;
        if (!rp.accepts(row)) continue;
        out.insert(SWMMObjectRef(type, name));
    }
    return out;
}

QSet<SWMMObjectRef> AttributeTablePanel::allCategoryRefs() const
{
    QSet<SWMMObjectRef> out;
    if (meshSourceActive()) return meshRefs(/*applyQuery=*/false);
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
    // Only the SWMM model source has a spatial delete path. When a feature
    // (GIS) or tabular layer is the active source, Delete must be a no-op —
    // otherwise a row index would be mis-resolved against the SWMM model and
    // delete an unrelated object.
    if (!m_proxy || m_proxy->sourceModel() != m_model) return;
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

    // Virtual junctions surface as their own node kind (mirrors the map's
    // Convert To ▸ menu): a flagged node reports kVirtualNodeType so
    // "Junction" (demote) is offered, and the "Virtual Junction" target is
    // listed only when the engine's usage rules are met (the item picker
    // has no way to grey an entry out).
    constexpr int kVJ = openswmmvis::ui::TypeConversionFlow::kVirtualNodeType;
    int vjRule = 0;
    if (isNode) {
        int isVirtual = 0;
        swmm_node_is_virtual(eng, idx, &isVirtual);
        if (isVirtual) currentType = kVJ;
        swmm_node_virtual_eligible(eng, idx, &vjRule);
    }

    QStringList labels;
    QVector<int> values;
    const int nKinds = 5;
    for (int t = 0; t < nKinds; ++t) {
        if (t == currentType) continue;
        if (isNode && t == kVJ && vjRule != 0) continue;
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
