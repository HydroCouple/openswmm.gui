/*!
 * \file   swmmattributetablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice Z.1 — Per-category tabular model over the active SWMMModelLayer.
 *
 * One instance is bound to one (layer, category) pair via setSource().
 * Rows correspond 1:1 to objects in that category in the layer's stable
 * ordering (matches Object Browser's row order); the column schema is
 * fixed per category and currently sourced from
 * `SWMMModelLayer::identifyByName()` for parity with the Identify tool.
 *
 * The model is intentionally NOT a generic QAbstractItemModel<Category>
 * template — keeping it a concrete class avoids template-in-header
 * compile-time overhead and lets `AttributeTablePanel` swap the bound
 * category in O(1) without rebuilding the model object.
 */

#ifndef SWMMATTRIBUTETABLEMODEL_H
#define SWMMATTRIBUTETABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

class QUndoStack;

#include "layers/swmmmodellayer.h"
// Not forward-declarable: m_resultsSource is a QPointer, which static_casts
// through QObject* and so needs the complete type at instantiation.
#include "layers/swmmresultslayer.h"

namespace openswmmvis {

/*! Slice Z.5.2 — How a column is edited (or not). */
enum class EditorKind {
    ReadOnly,   ///< Default; flags() omits ItemIsEditable.
    Numeric,    ///< NumericDelegate (QDoubleSpinBox).
    Integer,    ///< IntegerDelegate (QSpinBox).
    Enum,       ///< EnumDelegate (QComboBox) with pre-baked pairs.
    Text,       ///< Plain QLineEdit (default delegate); used for Name column.
    Compound,   ///< CompoundEditDelegate (NodeCompoundEditButton). Used for
                ///< per-node multi-row attributes (Inflows / DWF / RDII /
                ///< Treatment) where the cell shows a summary + opens a
                ///< dedicated dialog. The model returns a
                ///< NodeCompoundEditRef QVariant for these columns.
    Interval,   ///< IntervalDelegate (editable QComboBox of legacy H:MM
                ///< presets). The cell stores/edits a clock string; the
                ///< setter wrappers convert to/from engine seconds. First
                ///< user: rain gage Recording Interval (DA.2 parity).
    FileBrowse, ///< FileBrowseDelegate (QLineEdit + "…" QFileDialog button).
                ///< ColumnSpec::fileFilter carries the dialog's name filter.
                ///< First user: rain gage Rain File (path) — multi-column
                ///< series files, spec §4 task 4.
    FileColumn, ///< FileColumnDelegate (editable QComboBox). Per-row options
                ///< come from the model via kFileColumnOptionsRole (the
                ///< row's rain-file column headers). First user: rain gage
                ///< Rain File Column.
};

/*! Custom data() role for EditorKind::FileColumn cells — returns the
 *  QStringList of column names enumerated from the row's resolved data
 *  file (ui/util/externalcolumnfile.h readHeaders). */
inline constexpr int kFileColumnOptionsRole = Qt::UserRole + 21;

/*! Round-4 follow-up 2026-05-12 — semantic unit class.  Resolves to
 *  a string at render time via `UnitSystem::instance()` so the
 *  Attribute Table header reflects the current flow-units system
 *  (US-customary vs SI).  Columns without a physical unit set
 *  `UnitKind::None`; the column header then carries no suffix. */
enum class UnitKind {
    None,         ///< No physical unit (counts, ids, names, etc.)
    Length,       ///< ft / m  (invert, max depth, offsets, crest height)
    Area,         ///< ft² / m²  (ponded area, cross-section)
    SubcatchArea, ///< ac / ha  (subcatchment area; SWMM convention)
    Volume,       ///< ft³ / m³
    Velocity,     ///< ft/s / m/s
    FlowRate,     ///< CFS / CMS — uses UnitSystem::flowUnitLabel
    Depression,   ///< in / mm  (depression storage, rainfall)
    Percent,      ///< % — always
    Rate,         ///< in/hr / mm/hr (infiltration, seepage)
};

/*! Column metadata.  Drives both the read path (identify-map key
 *  for display) and the edit path (engine setter for commit).
 *
 *  `setter` is the name of a tag used to dispatch to an engine
 *  setter via a function-pointer table in the .cpp.  Empty means
 *  read-only — `flags(idx)` will not return ItemIsEditable. */
struct ColumnSpec {
    QString      key;          ///< identifyByName-map key (display)
    QString      label;        ///< Header label (without unit suffix)
    EditorKind   editor   = EditorKind::ReadOnly;
    QString      setter;       ///< Setter tag (empty == read-only)
    QVariantList enumValues;   ///< For Enum: list of {label, data} pairs
    double       minValue = -std::numeric_limits<double>::infinity();
    double       maxValue =  std::numeric_limits<double>::infinity();
    int          decimals = 4;
    UnitKind     unit = UnitKind::None;  ///< Semantic unit (resolved at render)
    QString      tooltip;      ///< Header tooltip override (user-flag
                               ///< description); empty = unit tooltip.
    QString      fileFilter;   ///< For FileBrowse: QFileDialog name filter.
};

} // namespace openswmmvis

class SWMMAttributeTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit SWMMAttributeTableModel(QObject *parent = nullptr);

    /*! Bind the model to a (layer, category) pair.  Rebuilds the
     *  column schema, then `beginResetModel` / `endResetModel`.  Pass
     *  a null layer to clear. */
    void setSource(SWMMModelLayer *layer, SWMMModelLayer::Category category);

    /*! Bind the post-run "dynamics" columns to a loaded output.
     *
     *  Those columns used to read the EDITING engine's ambient statistics
     *  (`swmm_node_get_stat_*` and friends). Nothing ever populates them:
     *  SimulationRunner runs on its own throw-away SWMM_Engine (or an
     *  out-of-process worker), so the editing engine never reaches the
     *  ENDED state the stat getters need and every dynamics cell read back
     *  zero. Pointing the model at the active results layer instead makes
     *  the columns show the run the user actually selected, and re-pointing
     *  it swaps every dynamics value to the new run.
     *
     *  Pass nullptr to fall back to the editing engine. */
    void setResultsSource(SWMMResultsLayer *layer);
    [[nodiscard]] SWMMResultsLayer *resultsSource() const { return m_resultsSource; }

    [[nodiscard]] SWMMModelLayer *layer() const noexcept { return m_layer; }
    [[nodiscard]] SWMMModelLayer::Category category() const noexcept
    { return m_category; }

    /*! Object name for a row, or empty string if out of range. */
    [[nodiscard]] QString objectNameAt(int row) const;

    /*! Inverse — find the model row for a given object name in the
     *  current category.  Returns -1 if not found. */
    [[nodiscard]] int rowForName(const QString &name) const;

    /*! Column-zero header is always "Name"; column 0 returns the
     *  object's name unmodified.  The remaining columns map to keys
     *  from the layer's identify map; column count is fixed per
     *  category. */
    QStringList columnKeys() const { return m_columnKeys; }

    /*! Slice Z.5.2 — column metadata (delegate kind, setter, range).
     *  Returns the full spec list for the bound category so callers
     *  (e.g. AttributeTablePanel) can install delegates. */
    QList<openswmmvis::ColumnSpec> columnSpecs() const { return m_columnSpecs; }

    /*! Re-query every horizontal header. Call after a change that alters a
     *  render-time label without touching the schema (LINK_OFFSETS mode →
     *  offset columns read "… Elevation"). */
    void refreshHeaders();

    /*! Slice Z.5.5 — optional QUndoStack for cell-edit commands.
     *  When set, `setData()` wraps each commit in an `EditCommand`
     *  pushed onto this stack so Ctrl+Z / Ctrl+Y round-trip.  Pass
     *  nullptr to disable undo (edits commit immediately). */
    void setUndoStack(QUndoStack *stack) { m_undoStack = stack; }
    [[nodiscard]] QUndoStack *undoStack() const { return m_undoStack; }

    /*! Direct-commit setter — bypasses the undo stack.  Used by
     *  `EditCommand::redo` / `undo` so re-applying a command
     *  doesn't push another command. */
    bool commitValueDirect(const QModelIndex &index, const QVariant &value);

    // QAbstractTableModel ----------------------------------------------------
    int      rowCount(const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData(const QModelIndex &index, const QVariant &value,
                     int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

public slots:
    /*! Force a full reload (e.g. after the underlying SoA changed). */
    void reload();

    /*! Round-4 follow-up 2026-05-12 — refresh a single row by object
     *  name.  Invalidates the row cache so the next data() read
     *  re-queries the engine, and emits dataChanged across the row
     *  so the view repaints.  Used by the panel to mirror property-
     *  browser edits without doing a full reset. */
    void refreshObject(const QString &name);

signals:
    /*! Emitted after a successful direct-edit commit (Attribute
     *  Table cell editor → engine setter).  Listeners (e.g. the
     *  Property Browser dock) use this to refresh their view of
     *  the same object.  Distinct from QAbstractItemModel::
     *  dataChanged because it fires only on user-initiated edits,
     *  not on synthetic refreshes — preventing feedback loops. */
    void objectEdited(const QString &name);

private:
    void rebuildColumnSchema();
    /*! Header label for \p spec with the LINK_OFFSETS mode applied: the
     *  offset columns read "Upstream/Downstream Elevation" in ELEVATION mode. */
    QString offsetModeLabel(const openswmmvis::ColumnSpec &spec) const;
    /*! Initial-quality UI round — append one editable column per
     *  constituent (key "initq:<NAME>": every pollutant, plus water age /
     *  temperature while their [OPTIONS] toggle is on) when the bound
     *  category is a node or link kind. Cells read/write the engine's
     *  [INITIAL_QUALITY] row store; blank = no override (the global
     *  initial concentration applies), and clearing a cell removes the
     *  element's row. */
    void appendInitialQualityColumns();
    /*! Phase 3 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — append one
     *  editable column per defined user flag (key "userflag:<NAME>")
     *  when the bound category maps to an engine object type. */
    void appendUserFlagColumns();

    /*! Append the category's post-run "dynamics" statistics columns.
     *  Called last by `rebuildColumnSchema()` — after the user-flag
     *  columns — so every table reads [inputs | user flags | results]
     *  left to right. The columns are ReadOnly with a getter-only tag;
     *  they read back zero until a simulation has been initialized. */
    void appendDynamicsColumns();
    QVariantMap rowData(int row) const;

    /*! Column index whose ColumnSpec::setter equals \p tag, or -1. Used where
     *  one cell edit has to reach a sibling cell of the same row. */
    [[nodiscard]] int columnForSetter(const QString &tag) const;

    /*! The rain file bound to \p row, resolved absolute when the engine has
     *  resolved it, else the original token. Empty when the row has no file. */
    [[nodiscard]] QString rainFileFor(int row) const;

    /*! Commit a rain-file path edit together with the column selector (and the
     *  file-format flip that a column implies) as ONE undoable step.
     *
     *  A column belongs to the file it was picked from, so a path edit that
     *  leaves a stale column behind writes a model whose run fails, and one
     *  that leaves a multi-column file unbound writes the standard
     *  `FILE "path" Station Units` grammar for a file with no station column
     *  (review R3). Both writes therefore have to happen — and both have to
     *  undo, which is why this is a QUndoStack macro rather than a second
     *  engine write inside commitValueDirect: a write made there would sit
     *  outside the edit command's captured state, so undo would restore the
     *  path and keep the column. */
    bool commitRainFilePath(const QModelIndex &pathIndex,
                            const QVariant &oldPath, const QVariant &newPath);

    QPointer<SWMMModelLayer>           m_layer;
    QPointer<SWMMResultsLayer>         m_resultsSource;  ///< Dynamics-column source
    SWMMModelLayer::Category           m_category = SWMMModelLayer::CatJunctions;
    QStringList                        m_columnKeys;     ///< Map keys from identifyByName
    QStringList                        m_columnLabels;   ///< User-facing header labels
    QList<openswmmvis::ColumnSpec>     m_columnSpecs;    ///< Per-column metadata (Z.5.2)
    QUndoStack                        *m_undoStack = nullptr; ///< Z.5.5 — cell-edit undo

    // Per-row cache: name → row-data map.  Invalidated by reload().
    // The cache is mutable so const data() can populate it on first
    // access without hammering identifyByName() on every paint.
    mutable QVector<QVariantMap> m_rowCache;
    mutable QVector<bool>        m_rowCacheValid;

    // Per-node summary caches used by compound cells (Inflows / DWF /
    // RDII / Treatment columns). Without these, each cell paint runs
    // an O(N) engine-wide scan over all external inflows / DWF entries /
    // RDII entries, multiplied by the number of visible rows × paint
    // events — millions of calls per second on a junction table.
    // Filled lazily on first compound-cell access and cleared whenever
    // the per-row cache is invalidated.
    mutable QHash<int, int> m_inflowCountByNode;
    mutable QHash<int, int> m_dwfCountByNode;
    mutable QHash<int, int> m_rdiiCountByNode;
    mutable QHash<int, int> m_treatmentActiveByNode;
    mutable int             m_compoundPollutantCount = 0;
    mutable bool            m_compoundCacheBuilt     = false;
    void ensureCompoundCacheBuilt() const;
    void invalidateCompoundCache();

    // Initial-quality UI round — element engine index → (constituent →
    // value) for the bound category's scope (NODE or LINK), filled by one
    // engine-wide scan so per-cell paints don't rescan the row store.
    // Invalidated together with the compound caches.
    mutable QHash<int, QHash<QString, double>> m_initQualityByElem;
    mutable bool                               m_initQualityCacheBuilt = false;
    void ensureInitQualityCacheBuilt() const;
};

#endif // SWMMATTRIBUTETABLEMODEL_H
