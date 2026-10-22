/**
 * @file      featurelayerpanel.h
 * @brief     The Features dock: pick the edit target, edit the schema, edit the Z policy
 *            (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §5.3).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * The Features dock: pick the edit target, edit the schema, edit the Z policy
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §5.3).
 *
 * This is a VIEW in the CLAUDE.md §5.1 sense. It never touches OGR and never
 * writes schema or feature content outside the undo stack: schema edits go
 * through AddFieldCommand / RemoveFieldCommand and the resample goes through
 * ResampleZCommand, so the same edit is undoable whether it was made here, on
 * the map, or in the attribute table. It rebuilds itself from the layer's
 * featuresChanged / schemaChanged / zPolicyChanged signals and from the
 * canvas's layerAdded / layerRemoved. Z policy configuration is applied directly,
 * without undo or immediate resampling; sampling itself is undoable.
 *
 * The selected layer is the EDIT TARGET: the drawing and editing tools all
 * read it from here, which is why selecting a layer is a first-class action
 * rather than a side effect of the layer tree's selection.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef FEATURELAYERPANEL_H
#define FEATURELAYERPANEL_H

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QToolButton;

class FeatureLayer;
class MapCanvas;
class QStyledItemDelegate;

namespace openswmmvis::feature
{
struct FieldUpdatePlan;
}

namespace openswmmvis::ui
{

class FeatureLayerPanel : public QWidget
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QWidget owner; panel owns its controls/delegates, never the bound
     *   canvas or layer.
     * @brief Build the Features dock with no canvas/active layer, empty read-only grids and
     *   disabled layer actions.
     * @note GUI-thread widget and synchronous model/undo operations only; no project opened, layer
     *   created or edit session started.
     */
    explicit FeatureLayerPanel(QWidget *parent = nullptr);

    /*! \brief Bind to \p canvas. Passing nullptr detaches and clears.
     * @param[in] canvas Optional borrowed canvas, guarded by QPointer; null detaches and empties
     *   the layer list.
     * @note Equal pointer is a no-op. Disconnect all previous canvas-to-panel signals, connect
     *   layer add/remove, rebuild feature-layer list and resolve edit target. Does not own canvas,
     *   start editing or change its active tool. Destruction guards pointer access but does not
     *   itself refresh stale visible controls.
     */
    void setCanvas(MapCanvas *canvas);
    /**
     * @return Borrowed currently bound canvas or null after detachment/destruction; no ownership
     *   transfer or live-project guarantee.
     */
    [[nodiscard]] MapCanvas *canvas() const { return m_canvas.data(); }

    /*! \brief The layer the drawing / editing tools should write into.
     * @return Borrowed selected feature-layer edit target or null after removal/destruction;
     *   independent of layer-tree selection and edit-session state. No ownership transfer.
     */
    [[nodiscard]] FeatureLayer *activeLayer() const { return m_active.data(); }

    /*!
     * \brief Apply "Update fields to role…" to the active layer as ONE undo
     *        step (FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md P7, R6):
     *        renames first, then added columns, then attached metadata, then
     *        the retired columns named in \p drop.
     * \details The dialog-free core behind the button, so the update is
     *          testable without a modal.
     * \returns Empty on success, else the first command's error.
     * @param[in] plan Schema operations for the CURRENT active layer, consumed synchronously
     *   without recomputing/validating plan freshness.
     * @param[in] drop Retired plan field names to remove, matched case-insensitively; names absent
     *   from retired plan ignored.
     * @note Requires active layer, canvas and undo stack, but does not independently require an
     *   edit session. Empty child list succeeds without undo entry. Pushes one macro, then returns
     *   first child error; successful earlier/later children are not rolled back on failure, so
     *   failure can leave a partially applied macro that remains undoable. No dialog/message
     *   emitted here; commands own storage writes and notifications.
     * @return Empty for success/no operations; localized missing-context error or first
     *   child-command error otherwise.
     */
    QString applyFieldUpdate(const openswmmvis::feature::FieldUpdatePlan &plan,
                             const QStringList &drop);

public slots:
    /*! \brief Re-read the layer list from the canvas, preserving the selection
     *         where the layer still exists.
     * @note Rebuild in canvas layer order, feature layers only; preserve selected layer ID when
     *   found, otherwise select first or clear target. May emit activeLayerChanged through row
     *   transition; same target only refreshes details. Does not alter layer visibility/edit state,
     *   canvas order or undo history. List signal blocking is reset to false, rather than
     *   preserving a caller-supplied blocked state.
     */
    void refreshLayerList();
    /*! \brief Select \p layer if it is in the list.
     * @param[in] layer Optional borrowed target; null or layer ID absent from current list is
     *   ignored.
     * @note Match persistent layerId, not pointer identity; selecting row resolves actual target
     *   from canvas list. Does not refresh list, add layer, select its features or enable editing.
     */
    void selectLayer(FeatureLayer *layer);

signals:
    /*! \brief The edit target changed; the tools re-target on this.
     * @param[in] layer Borrowed newly resolved edit target, possibly null; receivers must not
     *   assume edit mode or retain without lifetime protection.
     * @note Emitted after binding signals and refreshing details on pointer transition, not
     *   same-target refresh; panel does not retarget tools itself.
     */
    void activeLayerChanged(FeatureLayer *layer);
    /*! \brief The user asked to create a layer. The host owns the dialog and
     *         the project-GeoPackage path, so the panel only asks.
     * @note New button emits request only. Receiver owns project-path selection, dialog,
     *   creation/addition and undo decisions; no layer allocated here.
     */
    void newLayerRequested();
    /**
     * @param[in] into Borrowed current feature-layer target, normally nonnull because button
     *   disabled without target; signal contract permits null.
     * @brief Ask host to import into the target; no chooser, storage/selection mutation or undo
     *   step performed by emitting. Receiver preserves target lifetime during modal work.
     */
    void importRequested(FeatureLayer *into);
    /**
     * @param[in] layer Borrowed current target, normally nonnull because button disabled without
     *   target; not lifetime-pinned by signal.
     * @brief Ask host to export; no chooser or file write performed here, receiver owns
     *   destination/error handling.
     */
    void exportRequested(FeatureLayer *layer);
    /*! \brief A non-fatal message for the status bar.
     * @param[in] text Copied-by-signal localized status/error text, including command failures and
     *   sampling/deletion results.
     * @note Nonfatal status request, no message box or logging by emission itself; receiver chooses
     *   presentation. Not a machine-readable success/failure channel.
     */
    void message(const QString &text);

private slots:
    /**
     * @param[in] row Zero-based list row; negative/out-of-range, no canvas or unresolved ID clears
     *   target.
     * @brief Resolve first canvas layer matching row ID, cast to FeatureLayer, bind/refresh and
     *   emit only when target pointer changes.
     * @note Same pointer still refreshes details. No ownership transfer, edit-session or layer-tree
     *   selection mutation.
     */
    void onLayerRowChanged(int row);
    /**
     * @brief Open field editor seeded with existing stored names, then push AddFieldCommand on
     *   accepted unchanged target.
     * @note Requires active layer/canvas, not locally an edit-session gate. Cancel or changed
     *   target is no-op. Missing undo stack deletes unexecuted command with no storage fallback;
     *   command error emitted through message. Dialog/child lifetime is panel-owned, schema
     *   notifications rebuild grids.
     */
    void onAddField();
    /**
     * @brief Confirm removal of selected schema-row name and all its feature values, then push
     *   RemoveFieldCommand.
     * @note Missing context/selection, declined confirmation or missing undo stack does nothing. No
     *   local edit-session check; command handles validation/errors. Unlike add/update preview,
     *   does not recheck active target after modal confirmation: host must preserve original
     *   layer/canvas lifetime through modal dispatch. Error goes through message.
     */
    void onRemoveField();
    /*! Preview, then apply, the registry's current fields for the role.
     * @brief Count nonempty retired values, compute role plan and show explicit
     *   rename/add/metadata/drop preview.
     * @note Retired empty fields initially checked for deletion; populated ones unchecked. No
     *   operations shows information dialog. Acceptance rechecks target pointer/canvas before
     *   applyFieldUpdate; schema changes during dialog are not independently detected. One macro is
     *   attempted, message reports returned error or success; no edit-session gate or direct
     *   rollback on partial command failure.
     */
    void onUpdateFieldsToRole();
    /**
     * @brief Push ResampleZCommand for all active-layer features, then report error, unsampled
     *   vertices or all-sampled status.
     * @note Requires active layer/canvas and undo stack; missing stack deletes unexecuted command.
     *   UI enables for 3D, handler itself adds no 3D/edit-mode gate. Policy controls
     *   sampling/densification in command; status refreshed afterward, model notifications rebuild
     *   grids. No direct file write fallback.
     */
    void onResampleZ();
    /**
     * @brief Copy all Z widgets into active layer policy through setZPolicy, unless hydration guard
     *   is set.
     * @note Direct configuration change without undo, no immediate resampling/geometry write.
     *   Source and layer ID come from current combo values, so unavailable saved source IDs can be
     *   replaced by current fallback. Band widget 1..512, constant +/-1e9, scale +/-1e6, spacing
     *   0..1e9; values use policy units, no automatic global-unit conversion. No
     *   canvas/edit-mode/3D check beyond UI enablement.
     */
    void onZPolicyEdited();
    /**
     * @brief Confirm map removal, find target index and call canvas takeLayer with undo enabled;
     *   retain project GeoPackage table.
     * @note Command owns off-canvas layer, panel never deletes it. Missing target/canvas/index or
     *   declined confirmation is no-op. No post-dialog target/canvas lifetime recheck before use;
     *   host must preserve them. Refresh list after request, which may select next edit target and
     *   emit activeLayerChanged.
     */
    void onRemoveLayer();

    // ----- Feature grid ---------------------------------------------------
    /*! Rebuild the whole grid: one row per feature, one column per field.
     * @brief Rebuild all feature rows in featureIds order from role-aware editor schema, preserving
     *   layer selection via mirror pass.
     * @note Block table signals and guard hydration; IDs immutable, fields editable/checkable only
     *   during layer edit session. Boolean null displays unchecked; other values display toString,
     *   raw type conversion occurs on commit. Failed feature reads leave blank rows. Null target
     *   clears both grids. Delegates rebuilt only on schema/layer-ID cache key change; no
     *   model/undo write. Vertex refresh is suppressed during vertex commit.
     */
    void refreshFeatureTable();
    /*! A cell was edited by the user → EditFeatureAttributesCommand.
     * @param[in] row Current zero-based feature-table row, identity read from column zero UserRole.
     * @param[in] column Logical column; zero/negative ignored, positive indexes editorSchema field
     *   at column-1.
     * @brief Convert changed text/check state, replace field in full attribute map and push
     *   EditFeatureAttributesCommand if value differs.
     * @note Ignore hydration, missing context/feature/field, no edit session or missing undo stack.
     *   Nonboolean coerceToFieldType uses schema conversions rather than local parse success
     *   checks. No direct fallback write; command error emitted, then grid rebuilt to normalized
     *   committed representation. Command notifications can synchronously trigger rebuilds.
     */
    void onFeatureCellChanged(int row, int column);
    /*! Grid rows → the layer's map selection, so the two views agree.
     * @brief Copy selected table-row FeatureIds into active layer map selection and refresh vertex
     *   grid.
     * @note Hydration/missing target is no-op. Selection is immediate, not undoable; no feature
     *   edits or canvas SelectionManager update. Table items without identity ignored. Layer
     *   notification may invoke inverse mirror with signals blocked.
     */
    void onFeatureSelectionChanged();
    /*! The map selection changed → mirror it into the grid's rows.
     * @brief Clear and select table rows matching active-layer selectedFeatureIds, then refresh
     *   vertex grid.
     * @note Block table signals to avoid reverse propagation. Missing target no-op; no layer/undo
     *   mutation or selection ownership transfer. Qt table selection semantics determine resulting
     *   visible selected rows.
     */
    void onFeatureSelectionChangedFromLayer();
    /*! Delete every feature whose row is selected (undoable).
     * @brief Collect selected table-row IDs, sort and push DeleteFeaturesCommand as one undo
     *   operation.
     * @note Requires active target/canvas/edit session; missing edit session or selected rows emits
     *   explanatory message. Missing stack deletes unexecuted command without direct writes. Error
     *   or deleted count emitted; command notifications refresh content. Does not independently
     *   clear/rebuild selection outside command effects.
     */
    void onDeleteSelectedFeatureRows();
    /**
     * @param[in] pos Viewport-local feature-table pixel position, converted to global for
     *   synchronous menu exec.
     * @brief Offer delete only with open edit session and selected rows, dispatching existing
     *   deletion slot if chosen.
     * @note Missing active target no-op, menu is transient panel-owned stack object. Merely
     *   opening/canceling changes no selection or data; host keeps widget/context alive during
     *   modal menu.
     */
    void onFeatureTableContextMenu(const QPoint &pos);

    // ----- Vertex grid ----------------------------------------------------
    /*! Rebuild the vertex rows for the one selected feature, or clear.
     * @brief Display every exterior/hole vertex for exactly one selected feature, otherwise clear
     *   with explanatory hint.
     * @note Skip during in-flight vertex commit. Refuse more than 10000 vertices. Store zero-based
     *   part/ring/index addresses per row; display part/vertex one-based, exterior ring zero
     *   internally. XY/Z use native geometry/policy units and fixed six decimals, missing Z shows
     *   dash and NaN empty. XY editable only in edit session; Z additionally requires 3D and a
     *   Z-bearing ring. Rebuild blocks signals, preserves current row only for same feature and
     *   chooses X column; no geometry/undo writes.
     */
    void refreshVertexTable();
    /*! An X / Y / Z cell was edited → EditFeatureGeometryCommand.
     * @param[in] row Current zero-based vertex-table row; saved address is bounds-checked against
     *   freshly read geometry.
     * @param[in] column Logical X/Y/Z column only; all others ignored before establishing commit
     *   guard.
     * @brief Parse changed ordinate and push nonmergeable EditFeatureGeometryCommand, leaving
     *   untouched coordinates at full stored precision.
     * @note Requires active target/canvas/edit session and current feature. Invalid number emits
     *   error; blank Z becomes NaN, blank XY invalid. XY moves validate geometry; failure leaves
     *   storage unchanged. A 3D resampleOnEdit XY move samples with densify=false, then restores
     *   every other vertex Z. Explicit Z edit never resamples. Missing undo stack causes no write;
     *   command error emitted. A scope guard always clears in-flight state and queues one
     *   vertex-grid rebuild after eligible-column handler, including early failures, avoiding
     *   destruction of committing item. No separate finite-value check beyond parsing/geometry
     *   validation.
     */
    void onVertexCellChanged(int row, int column);

private:
    /**
     * @brief Allocate owned scroll page, layer/schema/feature/vertex grids and Z controls,
     *   connecting local slots/request signals.
     * @note Constructor-only initialization, not idempotent. No canvas binding, file I/O, layer
     *   creation or undo edits. Delete shortcut is scoped to feature table; Z widget ranges
     *   constrain subsequent user policy changes rather than validate existing stored policy.
     */
    void buildUi();
    /*! Rebuild the schema table, Z controls and status line from m_active.
     * @note Rebuild feature/schema tables, enable actions from target/3D/source state, hydrate Z
     *   controls under write guard and refresh status. Schema actions depend on target existence,
     *   not edit mode. Missing source ID can leave first available source selected visually;
     *   clamped spin display does not write policy during hydration. No global-unit conversion or
     *   undo/storage write.
     */
    void refreshDetails();
    /**
     * @brief Replace status with active feature count, geometry label and 3D unsampled-Z count, or
     *   no-target text.
     * @note Read-only synchronous layer queries; no resampling, full geometry grid rebuild or
     *   message signal.
     */
    void refreshStatus();
    /*! Connect / disconnect the per-layer signals as the target changes.
     * @param[in] layer Optional borrowed target retained through QPointer, never owned; null
     *   detaches.
     * @note Disconnect all previous layer-to-panel signals. Bind schema/policy to details refresh,
     *   feature writes to count/grid refresh, edit state to grid refresh and selection to row
     *   mirror. Does not itself refresh/emit activeLayerChanged, change edit state or install a
     *   destruction-triggered visual reset.
     */
    void bindActiveLayer(FeatureLayer *layer);
    /**
     * @brief Clear and refill source combo with current canvas GISRasterLayer or SWMM2DMeshLayer
     *   instances according to selected Z source.
     * @note All matching layers included regardless of visibility; names displayed, layer IDs
     *   stored. Missing context/non-raster/non-mesh source leaves empty. Does not block widget
     *   signals itself; refreshDetails caller uses Z guard. No selection preservation, validation,
     *   ownership transfer or sampling.
     */
    void populateZSourceLayers();
    /*! One FeatureFieldDelegate per value column of the feature grid, from
     *  the role-aware schema; rebuilt only when that schema changes.
     * @note Cache by active layer ID and compact editorSchema JSON. Unchanged key leaves existing
     *   suffix/theme/delegates untouched. Detach old column delegates and deleteLater to preserve
     *   current commit; create feature-table-owned delegate for each nonboolean field. Length
     *   suffix is presentation only; BC type column enables advisory applicability styling. Name
     *   callback resolves first current canvas SWMM model (even hidden) each call for time
     *   series/all curves, with no retained model ownership. Does not change schema, flags or
     *   source values.
     */
    void installGridDelegates();
    /**
     * @return Current Z-control hydration guard; true suppresses onZPolicyEdited only, not feature
     *   or vertex writes. No state mutation.
     */
    [[nodiscard]] bool zEditsSuppressed() const { return m_suppressZEdits; }

    QPointer<MapCanvas> m_canvas;
    QPointer<FeatureLayer> m_active;

    // Layers
    QListWidget *m_layerList = nullptr;
    QPushButton *m_newBtn = nullptr;
    QPushButton *m_importBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;

    // Schema
    QTableWidget *m_fieldTable = nullptr;
    QPushButton *m_addFieldBtn = nullptr;
    QPushButton *m_removeFieldBtn = nullptr;
    QPushButton *m_updateFieldsBtn = nullptr;

    // Feature grid — the rows are features, not columns. Read-only until the
    // layer is in an edit session; Delete and the context menu remove rows.
    QTableWidget *m_featureTable = nullptr;
    QPushButton *m_deleteFeatBtn = nullptr;
    QLabel *m_featHintLabel = nullptr;
    /*! Guards refreshFeatureTable()'s own setItem calls from being read back
     *  as user edits, and the selection round trip from echoing. */
    bool m_suppressFeatureEdits = false;
    /*! The grid's column delegates and the schema they were built for. */
    QList<QPointer<QStyledItemDelegate>> m_gridDelegates;
    QString m_gridDelegateKey;

    // Vertex grid — the coordinates of the ONE selected feature, one row per
    // vertex. Each row carries its (part, ring, index) address, so interior
    // rings and multi-part geometries are reachable; ring 0 is the exterior.
    // Editable on the same terms as the feature grid: an open edit session.
    QTableWidget *m_vertexTable = nullptr;
    QLabel *m_vertexHintLabel = nullptr;
    /*! Same role as m_suppressFeatureEdits, for the vertex grid. */
    bool m_suppressVertexEdits = false;
    /*! Set while onVertexCellChanged() runs. Rebuilding the grid from inside
     *  cellChanged would delete the QTableWidgetItem Qt is still committing —
     *  the view then loses focus and Tab stops advancing — and the write also
     *  re-enters here via featuresChanged. While this is set refreshVertexTable()
     *  defers; the handler queues exactly one rebuild as it leaves. */
    bool m_vertexEditInFlight = false;
    /*! The feature the vertex grid is showing; -1 when it is empty. Held as
     *  qint64 rather than feature::FeatureId to keep this header free of the
     *  feature headers. */
    qint64 m_vertexFeatureId = -1;

    // Z
    QComboBox *m_zSourceCombo = nullptr;
    QComboBox *m_zLayerCombo = nullptr;
    QSpinBox *m_zBandSpin = nullptr;
    QDoubleSpinBox *m_zConstantSpin = nullptr;
    QDoubleSpinBox *m_zScaleSpin = nullptr;
    QDoubleSpinBox *m_zDensifySpin = nullptr;
    QCheckBox *m_zResampleBox = nullptr;
    QPushButton *m_resampleBtn = nullptr;

    QLabel *m_statusLabel = nullptr;

    /*! Guards the Z widgets while refreshDetails() populates them, so
     *  programmatic setValue calls do not look like user edits. */
    bool m_suppressZEdits = false;
};

} // namespace openswmmvis::ui

#endif // FEATURELAYERPANEL_H
