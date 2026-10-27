/**
 * @file      swmmattributetablemodel.h
 * @brief     Per-category tabular model over the active SWMMModelLayer.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMATTRIBUTETABLEMODEL_H
#define SWMMATTRIBUTETABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

#include <optional>

class QUndoStack;

#include "layers/swmmmodellayer.h"
// Not forward-declarable: m_resultsSource is a QPointer, which static_casts
// through QObject* and so needs the complete type at instantiation.
#include "layers/swmmresultslayer.h"

namespace openswmmvis
{

/*! Slice Z.5.2 — How a column is edited (or not). */
enum class EditorKind {
    ReadOnly,     ///< Default; flags() omits ItemIsEditable.
    Numeric,      ///< NumericDelegate (QDoubleSpinBox).
    Integer,      ///< IntegerDelegate (QSpinBox).
    Enum,         ///< EnumDelegate (QComboBox) with pre-baked pairs.
    Text,         ///< Plain QLineEdit (default delegate); used for Name column.
    Compound,     ///< CompoundEditDelegate (NodeCompoundEditButton). Used for
                  ///< per-node multi-row attributes (Inflows / DWF / RDII /
                  ///< Treatment) where the cell shows a summary + opens a
                  ///< dedicated dialog. The model returns a
                  ///< NodeCompoundEditRef QVariant for these columns.
    Interval,     ///< IntervalDelegate (editable QComboBox of legacy H:MM
                  ///< presets). The cell stores/edits a clock string; the
                  ///< setter wrappers convert to/from engine seconds. First
                  ///< user: rain gage Recording Interval (DA.2 parity).
    FileBrowse,   ///< FileBrowseDelegate (QLineEdit + "…" QFileDialog button).
                  ///< ColumnSpec::fileFilter carries the dialog's name filter.
                  ///< First user: rain gage Rain File (path) — multi-column
                  ///< series files, spec §4 task 4.
    FileColumn,   ///< FileColumnDelegate (editable QComboBox). Per-row options
                  ///< come from the model via kFileColumnOptionsRole (the
                  ///< row's rain-file column headers). First user: rain gage
                  ///< Rain File Column.
    LinkEndpoint, ///< Searchable existing-node picker with live endpoint validation.
};

/*! Custom data() role for EditorKind::FileColumn cells — returns the
 *  QStringList of column names enumerated from the row's resolved data
 *  file (ui/util/externalcolumnfile.h readHeaders). */
inline constexpr int kFileColumnOptionsRole = Qt::UserRole + 21;
inline constexpr int kLinkEndpointRole = Qt::UserRole + 22;

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
    QString key;   ///< identifyByName-map key (display)
    QString label; ///< Header label (without unit suffix)
    EditorKind editor = EditorKind::ReadOnly;
    QString setter;          ///< Setter tag (empty == read-only)
    QVariantList enumValues; ///< For Enum: list of {label, data} pairs
    double minValue = -std::numeric_limits<double>::infinity();
    double maxValue = std::numeric_limits<double>::infinity();
    int decimals = 4;
    UnitKind unit = UnitKind::None; ///< Semantic unit (resolved at render)
    QString tooltip;                ///< Header tooltip override (user-flag
                                    ///< description); empty = unit tooltip.
    QString fileFilter;             ///< For FileBrowse: QFileDialog name filter.
    /*! Feedback C7 — spin-box increment. 0 = derive one unit of the
     *  second-to-last decimal (10^(1-decimals)); set explicitly where that
     *  is still wrong for the field (Length 1.0, Manning's n 0.001). */
    double step = 0.0;
    int scalarType = QMetaType::UnknownType; ///< Optional type for scalar values edited as text.
    bool queryable = true; ///< Scalar references remain queryable even with a compound picker.
};

/*! Effective spin-box step for a numeric column (see ColumnSpec::step).
 * @param[in] spec Numeric column metadata, explicit positive step used verbatim even +infinity.
 * @return spec.step when >0, otherwise pow(10,1-decimals); zero/negative/NaN step derives fallback.
 *   No finite/overflow/precision clamp, source access or value rounding.
 */
[[nodiscard]] double columnStep(const ColumnSpec &spec);

/*! Feedback C7 — the attribute-table column that backs a Properties-panel
 *  Q_PROPERTY, so both surfaces share one set of decimals, range and
 *  step. Matched through the setter-tag convention: "offsetUp" is
 *  "link_offset_up", "startupDepth" is "link_pump_startup_depth". The
 *  category-stripped tag is tried first, then a suffix match. Empty when
 *  no numeric column corresponds to the property.
 * @param[in] propertyName Raw camelCase Q_PROPERTY name converted by prefixing underscore before
 *   EVERY uppercase letter, no trimming.
 * @return First Numeric spec with exact category-stripped setter match, otherwise first suffix
 *   match, otherwise nullopt. Scan base schemas in category enum order; prefixes node/link/subcatch
 *   only, gage can match suffix. No integer/dynamic heat/quality/user-flag match or
 *   category/source-specific binding; caller handles ambiguous shared names. Schema copies
 *   allocated, no source/engine writes.
 */
[[nodiscard]] std::optional<ColumnSpec> columnSpecForProperty(const QString &propertyName);

} // namespace openswmmvis

class SWMMAttributeTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner; source/results are borrowed guarded pointers, undo
     *   stack borrowed raw.
     * @brief Build Junction base/computed/dynamics schema with no source/rows. Connect global
     *   UnitSystem headers refresh only if singleton exists now. GUI-thread model/getter/cache
     *   access only, no engine write.
     */
    explicit SWMMAttributeTableModel(QObject *parent = nullptr);

    /*! Bind the model to a (layer, category) pair.  Rebuilds the
     *  column schema, then `beginResetModel` / `endResetModel`.  Pass
     *  a null layer to clear.
     * @param[in] layer Optional borrowed source retained through QPointer; null gives zero rows but
     *   base/dynamics schema for category remains.
     * @param[in] category Spatial category stored verbatim, normally valid enum; unsupported base
     *   schema is Name only.
     * @note Always reset/rebuild schema and all row/compound/initial-quality caches, even equal
     *   binding. Disconnect prior optionsChanged and connect heat-related option keys to full-grid
     *   dataChanged, not schema rebuild. Dynamic flags/quality/species schema requires rebind to
     *   update; reload alone does not rebuild. Does not connect structural/attribute changes
     *   automatically, clear/rekey existing undo commands, reset results source or own source. Host
     *   coordinates resets and source lifetime.
     */
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
     *  Pass nullptr to fall back to the editing engine.
     * @param[in] layer Optional borrowed output guarded by QPointer; null permits editing-engine
     *   dynamics fallback.
     * @note Equal pointer no-op, otherwise emit display/shared-attribute-role dataChanged across
     *   grid without reset/cache clear. Does not connect output reopened/finalized/destroyed
     *   refresh, activate project source or verify source matches editing model. Rows resolve
     *   result indexes by name each read; host triggers refresh after same-pointer output updates.
     */
    void setResultsSource(SWMMResultsLayer *layer);
    /**
     * @return Borrowed chosen output or null absent/destroyed; no
     *   output-handle/timestep/source-coherence guarantee.
     */
    [[nodiscard]] SWMMResultsLayer *resultsSource() const { return m_resultsSource; }

    /**
     * @return Borrowed editing source or null after destruction/detach; no engine-state validation
     *   or ownership pin.
     */
    [[nodiscard]] SWMMModelLayer *layer() const noexcept { return m_layer; }
    /**
     * @return Stored row category, initially Junctions, independent of source validity/count and
     *   result source.
     */
    [[nodiscard]] SWMMModelLayer::Category category() const noexcept { return m_category; }

    /*! Object name for a row, or empty string if out of range.
     * @param[in] row Zero-based current source category DISPLAY row, including user object-order
     *   overrides.
     * @return Exact source name, empty null source/negative/out-of-range. No identify/cache
     *   population or source mutation.
     */
    [[nodiscard]] QString objectNameAt(int row) const;

    /*! Inverse — find the model row for a given object name in the
     *  current category.  Returns -1 if not found.
     * @param[in] name Exact nonempty name looked up in layer shared spatial name-location hash, no
     *   type mask.
     * @return Hash row only when returned category equals current category, otherwise -1. Same-name
     *   cross-kind hash collisions follow layer map rather than typed rescan; no proxy conversion,
     *   selection or ownership transfer.
     */
    [[nodiscard]] int rowForName(const QString &name) const;

    /*! Column-zero header is always "Name"; column 0 returns the
     *  object's name unmodified.  The remaining columns map to keys
     *  from the layer's identify map; column count is fixed per
     *  category.
     * @return Cached key list by value, current schema order; dynamic quality/flags keys captured
     *   at last source bind, not live registry. No source mutation.
     */
    QStringList columnKeys() const { return m_columnKeys; }

    /*! Slice Z.5.2 — column metadata (delegate kind, setter, range).
     *  Returns the full spec list for the bound category so callers
     *  (e.g. AttributeTablePanel) can install delegates.
     * @return Metadata list copy, ordered ordinary attributes/heat/quality/flags, node coupling,
     *   then dynamics. Some ReadOnly columns have getter-only nonempty setter tags; editor kind
     *   must also be checked for editability. No live data/callback ownership transfer.
     */
    QList<openswmmvis::ColumnSpec> columnSpecs() const { return m_columnSpecs; }

    /*! Re-query every horizontal header. Call after a change that alters a
     *  render-time label without touching the schema (LINK_OFFSETS mode →
     *  offset columns read "… Elevation").
     * @note Emit full horizontal headerDataChanged when nonempty; labels/units resolved at read
     *   time. No schema/row-cache rebuild, engine option change or value notification. Host calls
     *   for LINK_OFFSETS changes.
     */
    void refreshHeaders();

    /*! Slice Z.5.5 — optional QUndoStack for cell-edit commands.
     *  When set, `setData()` wraps each commit in an `EditCommand`
     *  pushed onto this stack so Ctrl+Z / Ctrl+Y round-trip.  Pass
     *  nullptr to disable undo (edits commit immediately).
     * @param[in] stack Optional borrowed RAW stack, must outlive binding or be explicitly detached;
     *   null direct-commit path.
     * @note Store only, no ownership/lifetime guard, clear/history migration or notifications.
     *   Existing cell commands replay against THIS model current binding and row/column, so host
     *   must preserve compatible binding/order/schema while they remain undoable.
     */
    void setUndoStack(QUndoStack *stack) { m_undoStack = stack; }
    /**
     * @return Borrowed raw stack or null, no destruction guard or ownership transfer.
     */
    [[nodiscard]] QUndoStack *undoStack() const { return m_undoStack; }

    /*! Direct-commit setter — bypasses the undo stack.  Used by
     *  `EditCommand::redo` / `undo` so re-applying a command
     *  doesn't push another command.
     * @param[in] index Current model cell and row context. Invalid/unknown/ReadOnly rejected;
     *   general row validity handled by downstream lookup rather than uniformly preflighted.
     * @param[in] value Raw candidate: numeric generic dispatch converts/clamps double to ColumnSpec
     *   bounds, int converts without metadata clamp, string UTF-8. Special paths have separate
     *   semantics.
     * @return True for accepted underlying write or compound refresh; false invalid
     *   context/type/conversion/engine result, empty required picker, missing initial-quality row
     *   on clear, or no-op rename. True does not guarantee every side effect succeeded: storage
     *   shape can return success without available curve, compound path assumes earlier dialog
     *   writes.
     * @note Bypasses undo and flags() RUNNING/applicability gates. Name routes layer applyRename;
     *   coordinate edits preserve unedited ordinate via layer move; heat uses direct heat funnel;
     *   user-flag/initial-quality blanks clear assignments. DataObjectRef setter uses currentName
     *   and CURRENT model engine, not ref engine/layer/kind: empty clears optional
     *   pump/storage/aquifer/outfall-route/snowpack, required references reject empty; outlet node
     *   takes name precedence. Outfall picker can switch type, storage curve switches shape. Other
     *   compound refs only invalidate caches/notify. Success emits cell/row dataChanged and
     *   objectEdited, including undo/redo calling this method. No disk save or rollback across
     *   grouped side effects; source/engine must remain alive.
     */
    bool commitValueDirect(const QModelIndex &index, const QVariant &value);

    // QAbstractTableModel ----------------------------------------------------
    /**
     * @param[in] parent Flat-table parent; valid returns zero.
     * @return Current live source categoryCount or zero without source. Cache sizing is separately
     *   maintained by source bind/reload; host synchronizes structural changes before painting.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Valid parent returns zero; invalid root cached schema count.
     * @return Cached key count independent of source lifetime or live option/flag/species changes;
     *   no rebuild.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Current model index; invalid/outside columns empty, row checks depend on
     *   branch/live lookup; stale/foreign indexes unsupported.
     * @param[in] role Display/Edit/Tooltip, shared attribute value/sort roles, or
     *   kFileColumnOptionsRole; others invalid.
     * @return Name directly, enum label/display vs raw int/edit, scalar native project units, heat
     *   authored overrides SI/°C, text edit strings for optional flag/quality, or typed compound
     *   ref. Custom attribute roles wrap DISPLAY values, so enum sort uses label. Optional
     *   references/unset values blank.
     * @note Live engine getters preferred over kind-scoped cached identify map, with read-through
     *   caches for row/compound/initial-quality. Compound reads can lazily ensure user-flags model
     *   and scan engine stores; readHeaders can perform file I/O, fabricated headers not offered.
     *   Results dynamics require output handle/timesteps/name match; absent chosen output falls
     *   back only editing engine RUNNING/ENDED, unavailable blank. Node flooded seconds converted
     *   to hours only on engine helper path; output accessor units follow results provider. Storage
     *   dimensions not used by shape blank; inline xsect dimensions remain stored except
     *   picker-only slots gated for editing. No engine content mutation except lazy model ensuring;
     *   refs borrow engine/layer and must not outlive source.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Expected current mutable cell, valid source and column; role/readonly
     *   checked but DOES NOT call flags() for running-state/applicability validation.
     * @param[in] value Raw candidate passed to direct/special commit or captured unchanged in
     *   command. Equality uses QVariant, no preflight engine acceptance on command path.
     * @param[in] role Only EditRole supported.
     * @return False rejected context or failed direct/special write; true for no-op rename
     *   including EMPTY requested name, unchanged QVariant, rain-file macro attempt or queued
     *   ordinary command regardless replay success.
     * @note Heat uses shared undo-aware funnel; rename command uses model current layer/category,
     *   scalar commands retain model row/column, not immutable object/source identity. Compound
     *   pickers/dialog summaries commit directly without table undo. Rain path groups
     *   path/format/column commands but not atomic rollback. User flag/quality ordinary text edits
     *   use scalar history when stack. Host preserves binding/order/schema and raw stack lifetime
     *   across replay. No error dialog or universal engine-running gate; notifications occur from
     *   direct commit path.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @param[in] index Expected current model index; invalid/outside columns returns base flags,
     *   general row existence not independently checked.
     * @return Editable for nonReadOnly/nonempty setter unless live engine reports RUNNING,
     *   picker-owned xsect index slot or shape-unused storage dimension. Missing source/failed
     *   state query does not globally remove editability. Other type-specific cells (outfall,
     *   infiltration, storage curve/coefficient) are not gated here despite narrower editor UI
     *   elsewhere. No mutation/range checking.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] section Logical horizontal column bounds checked, vertical display returns
     *   section+1 without row bounds.
     * @param[in] orientation Horizontal metadata, Vertical row numbering.
     * @param[in] role DisplayRole, ToolTipRole or horizontal AttributeGroupRole; other invalid.
     * @return Mode-aware offset label plus current global unit suffix; explicit spec tooltip
     *   overrides Units fallback, group role maps setter. Unit labels do not convert data; heat SI
     *   override headings tooltip differ from project-unit quantities. No schema rebuild or engine
     *   write.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

public slots:
    /*! Force a full reload (e.g. after the underlying SoA changed).
     * @note Reset row-cache sizes from current source count and invalidate compound/quality caches.
     *   DOES NOT rebuild columns, reference metadata or UnitSystem connection. No source
     *   reload/engine write or automatic undo invalidation; old indexes invalid.
     */
    void reload();

    /*! Round-4 follow-up 2026-05-12 — refresh a single row by object
     *  name.  Invalidates the row cache so the next data() read
     *  re-queries the engine, and emits dataChanged across the row
     *  so the view repaints.  Used by the panel to mirror property-
     *  browser edits without doing a full reset.
     * @param[in] name Exact nonempty name resolved in current category through shared hash;
     *   unmatched/no source no-op.
     * @note Invalidate row identify map plus ALL compound/initial-quality caches, emit all
     *   display/edit/tooltip/shared roles across row. Lazy subsequent reads rescan stores. No
     *   schema/reset, objectEdited, engine mutation or cache resize; host invokes for external
     *   edits.
     */
    void refreshObject(const QString &name);

signals:
    /*! Emitted after a successful direct-edit commit (Attribute
     *  Table cell editor → engine setter).  Listeners (e.g. the
     *  Property Browser dock) use this to refresh their view of
     *  the same object.  Distinct from QAbstractItemModel::
     *  dataChanged because it accompanies direct commits and command replay,
     *  not on synthetic refreshes — preventing feedback loops.
     * @param[in] name Exact current edited/renamed name, no source/type ownership identity
     *   included.
     * @note Emitted by successful direct commits and compound summary refresh, therefore ordinary
     *   AttributeEditCommand undo/redo also emits. Rename-history command bypasses direct commit
     *   and only emits dataChanged. Not emitted by synthetic refreshObject/reload/results rebind.
     *   Receiver manages cross-view update without assuming user-only or disk persistence.
     */
    void objectEdited(const QString &name);

private:
    /**
     * @note Replace base category schema, append 6 node/14 conduit heat text overrides when source
     *   bound, initial quality and flags, stable-move node_2d tags after ordinary fields, then
     *   dynamics last. Refresh cached keys/labels. Can lazily ensure UserFlagsModel, but does not
     *   bracket reset, resize row caches, subscribe schema changes or write engine. Caller binds
     *   coherent source/category.
     */
    void rebuildColumnSchema();
    /*! Header label for \p spec with the LINK_OFFSETS mode applied: the
     *  offset columns read "Upstream/Downstream Elevation" in ELEVATION mode.
     * @param[in] spec Column metadata; only link_offset_up/dn tags mode-adjusted.
     * @return Original label unless current engine LINK_OFFSETS elevation mode; then
     *   Downstream/Upstream Elevation, or Elevation for single Offset label. No unit suffix/value
     *   conversion or option write.
     */
    QString offsetModeLabel(const openswmmvis::ColumnSpec &spec) const;
    /*! Initial-quality UI round — append one editable column per
     *  constituent (key "initq:<NAME>": every pollutant, plus water age /
     *  temperature while their [OPTIONS] toggle is on) when the bound
     *  category is a node or link kind. Cells read/write the engine's
     *  [INITIAL_QUALITY] row store; blank = no override (the global
     *  initial concentration applies), and clearing a cell removes the
     *  element's row.
     * @note Node/link with engine only: one text initq: column per current pollutant, plus reserved
     *   water age if exact WATER_AGE YES and temperature if HEAT_TRANSPORT YES. Values use
     *   pollutant input units, hours, °C respectively; blank removes override. No row data
     *   read/cache clear/reset/write, duplicates if repeatedly appended without clearing schema.
     */
    void appendInitialQualityColumns();
    /*! Phase 3 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — append one
     *  editable column per defined user flag (key "userflag:<NAME>")
     *  when the bound category maps to an engine object type.
     * @note Supported category/source lazily ensures flags model, appends compound summary then
     *   definitions in current order. Boolean enum unset=-1/YES=1/NO=0, other types Text with
     *   scalarType metadata and blank-clear semantics. No assignment write or automatic
     *   future-definition subscription, duplicate append if caller did not clear schema.
     */
    void appendUserFlagColumns();

    /*! Append the category's post-run "dynamics" statistics columns.
     *  Called last by `rebuildColumnSchema()` — after the user-flag
     *  columns — so every table reads [inputs | user flags | results]
     *  left to right. The columns are ReadOnly with a getter-only tag;
     *  unavailable statistics read as blank until a compatible run is available.
     * @note Append read-only getter-tag summary block for current category:
     *   node/link/pump/subcatch, none rain gages. No run/read/cache initialization, source/engine
     *   writes or duplicate prevention. Unavailable dynamics normally blank rather than assured
     *   zero.
     */
    void appendDynamicsColumns();
    /**
     * @param[in] row Zero-based cached row, negative/outside CACHE size returns empty even if live
     *   source count grew.
     * @return Copy of lazily cached kind-scoped identify map, or empty without source. Empty failed
     *   lookup still marked valid until invalidated. Uses current row name only on first read;
     *   reorder/structural edit requires host reload to avoid stale row contents. No source write.
     */
    QVariantMap rowData(int row) const;

    /*! Column index whose ColumnSpec::setter equals \p tag, or -1. Used where
     *  one cell edit has to reach a sibling cell of the same row.
     * @param[in] tag Exact setter token, may occur more than once.
     * @return FIRST cached matching column, -1 absent; no source query or schema mutation.
     */
    [[nodiscard]] int columnForSetter(const QString &tag) const;

    /*! The rain file bound to \p row, resolved absolute when the engine has
     *  resolved it, else the original token. Empty when the row has no file.
     * @param[in] row Current object row normally RainGage, not independently category-gated.
     * @return Engine-resolved absolute path if nonempty, otherwise original token, empty missing
     *   engine/name/path lookup failure. Fixed 1024-byte local buffers; no file existence read,
     *   resolve attempt or source write here.
     */
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
     *  path and keep the column.
     * @param[in] pathIndex Current valid file-path cell in this model; caller validates
     *   role/source/schema before entry.
     * @param[in] oldPath Pre-edit path captured by caller, not reread.
     * @param[in] newPath Raw target path submitted through string setter, no existence/type
     *   validation here.
     * @return Always true after attempts, even setter/engine/file-header failures; not an atomic
     *   success signal.
     * @note Capture sibling column/format BEFORE write, begin/end one stack macro if available,
     *   commit path then reconcile selector against resolved new file headers; nonempty new
     *   selector records USER_CSV before column. If clearing selector, no explicit
     *   reset-to-standard format step here. Failed path can leave old path used for reconciliation,
     *   no rollback. Without stack direct writes execute sequentially. Inner commands retain
     *   current model row/column; host preserves schema/source through notifications.
     */
    bool commitRainFilePath(const QModelIndex &pathIndex, const QVariant &oldPath,
                            const QVariant &newPath);

    QPointer<SWMMModelLayer> m_layer;
    QPointer<SWMMResultsLayer> m_resultsSource; ///< Dynamics-column source
    SWMMModelLayer::Category m_category = SWMMModelLayer::CatJunctions;
    QStringList m_columnKeys;                     ///< Map keys from identifyByName
    QStringList m_columnLabels;                   ///< User-facing header labels
    QList<openswmmvis::ColumnSpec> m_columnSpecs; ///< Per-column metadata (Z.5.2)
    QUndoStack *m_undoStack = nullptr;            ///< Z.5.5 — cell-edit undo

    // Per-row cache: name → row-data map.  Invalidated by reload().
    // The cache is mutable so const data() can populate it on first
    // access without hammering identifyByName() on every paint.
    mutable QVector<QVariantMap> m_rowCache;
    mutable QVector<bool> m_rowCacheValid;

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
    mutable int m_compoundPollutantCount = 0;
    mutable bool m_compoundCacheBuilt = false;
    /**
     * @note GUI-thread mutable lazy cache: scan inflows/DWF/RDII once to count nonnegative node
     *   destinations, record pollutant count, defer treatment per-node scan to data. Missing
     *   engine/source still marks built-empty until invalidation. Failed rows skipped; no engine
     *   write/reset/notification. Counts remain stale after external mutations until host
     *   refreshObject/reload.
     */
    void ensureCompoundCacheBuilt() const;
    /**
     * @brief Clear inflow/DWF/RDII/treatment and initial-quality snapshots, reset built
     *   flags/pollutant count.
     * @note No source query/write, model notification or eager rebuild. Coarse invalidation across
     *   all rows, safe GUI-thread only.
     */
    void invalidateCompoundCache();

    // Initial-quality UI round — element engine index → (constituent →
    // value) for the bound category's scope (NODE or LINK), filled by one
    // engine-wide scan so per-cell paints don't rescan the row store.
    // Invalidated together with the compound caches.
    mutable QHash<int, QHash<QString, double>> m_initQualityByElem;
    mutable bool m_initQualityCacheBuilt = false;
    /**
     * @note Lazily scan full store once, node/link scope from current category, constituent names
     *   through 128-byte buffer; failed entries skipped, later duplicate key overwrites earlier.
     *   Missing source/engine marked built-empty, requires invalidation after later availability.
     *   No active-species filter or source mutation; mutable GUI-thread cache.
     */
    void ensureInitQualityCacheBuilt() const;
};

#endif // SWMMATTRIBUTETABLEMODEL_H
