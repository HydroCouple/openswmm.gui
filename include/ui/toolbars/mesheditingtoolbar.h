/**
 * @file      mesheditingtoolbar.h
 * @brief     Slice §V.VB — Mesh Editing toolbar.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice §V.VB — Mesh Editing toolbar.
 *
 * Peer of TerrainToolbar. Lets the user pick which 2D mesh is active,
 * hover-read elevation, click a vertex and retype its elevation, and
 * (in §V.VC) select edges and assign per-edge BCs.
 *
 * MVC contract: the toolbar is one of N views over `SWMM2DMeshLayer`.
 * It reads from the layer on every focus (no shadow state), writes
 * through `applyMesh*` helpers, and subscribes to `attributeChanged`
 * so edits from other views (future Property Browser, Attribute Table)
 * keep the toolbar's spinbox in sync. The active-mesh combo mirrors
 * the existing `SWMM2DMeshLayer::isActiveMesh` flag (Q-V4-resolved).
 *
 * Lifecycle: `rebindCanvas(MapCanvas*)` is called by SWMMVis each time
 * the active project window changes — mirrors TerrainToolbar's pattern.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H
#define OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H

#include "mesh/meshgwpreview.h"

#include <QPair>
#include <QPointF>
#include <QPointer>
#include <QToolBar>
#include <QVector>

#include <functional>
#include <optional>

namespace openswmmvis::ui
{
class RibbonGroup;
}

class MapCanvas;
class OpenSWMMVisLayer;
class SWMM2DMeshLayer;

class QAction;
class QActionGroup;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolButton;

namespace mesh
{
class MeshHoverProbe;
}

class SelectionManager;
struct SWMMObjectRef;

class MeshEditingToolbar : public QToolBar
{
    Q_OBJECT
public:
    /**
     * @param[in] title Display title copied into QToolBar.
     * @param[in] parent Optional QWidget owner, caller owns until parented/hosted.
     * @brief Build owned contextual ribbon controls, exclusive vertex/edge actions, unplaced cell
     *   editor widgets and owned hover probe.
     * @note GUI-thread only. No canvas/model/selection initially. Callbacks are injected later;
     *   toolbar does not own bound guarded objects. Z spin raw 3 decimal range [-1e6,1e6], Cd
     *   dimensionless [0.001,1], area mesh-length-squared up to 1e6, conveyance [0,1]; widget
     *   bounds do not replace layer validation. Numeric keyboard tracking disabled for vertex/cell
     *   edits, BC scalar/name changes apply as they change.
     */
    explicit MeshEditingToolbar(const QString &title, QWidget *parent = nullptr);
    /**
     * @brief Destroy QObject/widget children/connections and stored callback values; bound
     *   canvas/mesh/selection remain externally owned. No automatic save, commit, undo or restoring
     *   another tool.
     */
    ~MeshEditingToolbar() override;

protected:
    //! Flushes a node-list refresh that was deferred while hidden.
    /**
     * @param[in] event Required borrowed Qt show event.
     * @note Calls QToolBar base then flushes deferred node-list refresh if stale. Does not flush
     *   stale BC lists here, auto-select mesh or edit engine data.
     */
    void showEvent(QShowEvent *event) override;

public:
    /*! \brief Bind the toolbar to a new project canvas. Disconnects from
     *  the previous canvas's signals, populates the mesh combo from the
     *  new canvas's `SWMM2DMeshLayer` instances, and re-establishes the
     *  active-mesh probe / selection subscriptions. Pass nullptr when no
     *  project is active (clears + disables).
     * @param[in] canvas Optional borrowed canvas guarded by QPointer; null detaches.
     * @note Equality including null→null no-op. Disconnect old canvas layer hooks/active mesh, set
     *   new canvas/probe, rebuild mesh combo and conditionally populate BC/node lists only if mesh
     *   exists, then update enabled state. If no mesh, BC lists remain stale for later mesh
     *   arrival; hidden node list defers. Does not rebind SelectionManager or injected callbacks,
     *   so host must keep those in step. No destroyed→rebind hook; guarded null alone does not
     *   clear combo automatically.
     */
    void rebindCanvas(MapCanvas *canvas);

    /*! \brief Bind the toolbar to a project's SelectionManager so the
     *  spinbox / edge-info label update when selection changes (e.g.
     *  from a future Property Browser tab or an Attribute Table click).
     *  Pass nullptr to detach.
     * @param[in] sel Optional borrowed selection manager guarded by QPointer; null detaches.
     * @note Equality no-op. Replace selectionChanged connection, refresh vertex and edge editor
     *   ONLY. No immediate cell refresh/highlight synchronization until later onSelectionChanged;
     *   canvas/callback binding unchanged.
     */
    void rebindSelectionManager(SelectionManager *sel);

    /*! \brief Install a callback that opens a timeseries picker dialog
     *  and returns the picked name (empty = user cancelled). Per
     *  feedback_data_object_pickers, the implementation must dispatch
     *  through `TimeseriesEditorDialog::pickTimeseries`. SWMMVis sets
     *  this; the toolbar stays decoupled from the registries.
     *  Same shape for the curve picker (RatingCurve). */
    using PickerFn = std::function<QString(const QString &currentName)>;
    /**
     * @param[in] fn Moved synchronous stage boundary time-series picker callback; empty disables
     *   browse. Receives current raw name, returns selected ID or empty cancel.
     * @note Setter stores only, no immediate invocation/refresh. Callback captures must remain
     *   valid and coordinate modal reentry/model lifetime; exceptions propagate. No registry
     *   ownership, unit conversion or engine write here.
     */
    void setStageTimeseriesPicker(PickerFn fn) { m_stageTSPicker = std::move(fn); }
    /**
     * @param[in] fn Moved synchronous flow boundary time-series picker callback; empty disables
     *   browse. Receives current raw name, returns selected ID or empty cancel.
     * @note Setter stores only, no immediate invocation/refresh. Callback captures must remain
     *   valid and coordinate modal reentry/model lifetime; exceptions propagate. No registry
     *   ownership, unit conversion or engine write here.
     */
    void setFlowTimeseriesPicker(PickerFn fn) { m_flowTSPicker = std::move(fn); }
    /**
     * @param[in] fn Moved synchronous rating-curve picker callback; empty disables browse. Receives
     *   current raw name, returns selected ID or empty cancel.
     * @note Setter stores only, no immediate invocation/refresh. Callback captures must remain
     *   valid and coordinate modal reentry/model lifetime; exceptions propagate. No registry
     *   ownership, unit conversion or engine write here.
     */
    void setRatingCurvePicker(PickerFn fn) { m_curvePicker = std::move(fn); }

    /*! \brief Install lister callbacks that return the available
     *  timeseries / curve names for the project. The TS/curve BC pages
     *  populate their QComboBox from these so the user can pick an
     *  existing object without opening the CRUD dialog. Empty list →
     *  combobox shows only "(none)". */
    using ListerFn = std::function<QStringList()>;
    /**
     * @param[in] fn Moved GUI-thread synchronous lister for available time-series IDs for both
     *   stage/flow; empty means empty list.
     * @note Stores only, no immediate repopulation. Caller invokes
     *   refreshBCNameLists/refreshNodeList after replacement and preserves captured context
     *   lifetime. Returned order/duplicates accepted verbatim.
     */
    void setTimeseriesLister(ListerFn fn) { m_tsLister = std::move(fn); }
    /**
     * @param[in] fn Moved GUI-thread synchronous lister for available rating-curve IDs; empty means
     *   empty list.
     * @note Stores only, no immediate repopulation. Caller invokes
     *   refreshBCNameLists/refreshNodeList after replacement and preserves captured context
     *   lifetime. Returned order/duplicates accepted verbatim.
     */
    void setCurveLister(ListerFn fn) { m_curveLister = std::move(fn); }
    /*! \brief Lister of couplable SWMM node ids for the coupled-node dropdown.
     * @param[in] fn Moved GUI-thread synchronous lister for couplable SWMM node IDs; empty means
     *   empty list.
     * @note Stores only, no immediate repopulation. Caller invokes
     *   refreshBCNameLists/refreshNodeList after replacement and preserves captured context
     *   lifetime. Returned order/duplicates accepted verbatim.
     */
    void setNodeLister(ListerFn fn) { m_nodeLister = std::move(fn); }
    /*! \brief Locator of SWMM node ids + map coordinates ([COORDINATES],
     *  same CRS as the mesh vertices) for the Auto-couple action. */
    using NodeLocatorFn = std::function<QVector<QPair<QString, QPointF>>()>;
    /**
     * @param[in] fn Moved synchronous callback returning node ID/native-map XY pairs in SAME
     *   CRS/units as mesh vertices; empty yields no candidates.
     * @note Stores only; invoked by auto-couple/remap, no coordinate conversion or caching. Caller
     *   owns captured model lifetime and valid finite geometry.
     */
    void setNodeLocator(NodeLocatorFn fn) { m_nodeLocator = std::move(fn); }
    /*! \brief Model inputs for Remap's 2D-aquifer preview; std::nullopt
     *  when the project's 2D groundwater is not enabled. */
    using GwPreviewSourceFn = std::function<std::optional<mesh::GwPreviewInput>()>;
    /**
     * @param[in] fn Moved optional synchronous dialog/review action passed into Remap1D2DDialog;
     *   empty disables action there.
     * @note No invocation/refresh/ownership change now; host manages captured state and reentry
     *   lifetime.
     */
    void setSurfaceOwnershipReview(std::function<void()> fn)
    {
        m_surfaceOwnershipReview = std::move(fn);
    }
    /**
     * @param[in] fn Moved optional synchronous supplier of groundwater mapping inputs, nullopt when
     *   disabled/unavailable.
     * @note Stores only; Remap uses returned value for read-only preview, not engine
     *   initialization/enrolment. Caller preserves captured model lifetime/coordinate
     *   compatibility.
     */
    void setGwPreviewSource(GwPreviewSourceFn fn) { m_gwPreviewSource = std::move(fn); }
    /**
     * @note Invoke optional time-series and curve listers once each, repopulate three editable
     *   combos in batched inserts under signal blockers, prepend blank and preserve previous
     *   nonempty currentText even if absent from list. No sorting/deduplication, validation, BC
     *   write or unit conversion. Empty current text stays blank. GUI-thread callback exceptions
     *   propagate.
     */
    void refreshBCNameLists(); // re-query listers + repopulate combos
    /**
     * @note Missing control no-op. Hidden toolbar marks stale and returns without calling lister;
     *   showEvent flushes. Visible refresh clears stale, invokes optional lister, batched-inserts
     *   blank plus raw IDs and restores prior currentText under blocker. No coupling
     *   commit/validation; old typed unknown ID can remain.
     */
    void refreshNodeList(); // re-query node lister + repopulate the combo

    /*! \brief Add a trailing tool action (e.g. Pick 2D Cells / Trace Profile)
     *  to the *left* of the expanding spacer so it stays visible. Use this
     *  instead of QToolBar::addAction, which appends after the spacer and
     *  pushes the action off the right edge into the overflow chevron.
     * @param[in] action Optional borrowed QAction, null ignored; action ownership unchanged.
     * @note Append to embedded 2D Results mini toolbar when present, not after outer spacer. Does
     *   not trigger action or manage its lifetime.
     */
    void addToolAction(QAction *action);
    /**
     * @brief Compatibility no-op; captioned group boundary already separates controls. No
     *   separator/widget/width change.
     */
    void addToolSeparator();

    /*! \brief Add the mesh profile-plot action to its standalone "Profile"
     *  ribbon group (iteration 4 — kept apart from the cell-selection
     *  cluster whose width changes contextually).
     * @param[in] action Optional borrowed QAction, null ignored; action ownership unchanged.
     * @note Append to embedded Profile mini toolbar when present, not after outer spacer. Does not
     *   trigger action or manage its lifetime.
     */
    void addProfileAction(QAction *action);

    /*! \brief Add a trailing widget (e.g. the cell-selection info label) to
     *  the left of the expanding spacer, mirroring addToolAction. Returns the
     *  QAction handle for the embedded widget.
     * @param[in] widget Optional QWidget adopted by embedded Results toolbar ownership; null no-op.
     * @return Borrowed QWidgetAction handle from mini toolbar, null if widget/target absent.
     *   Layout/Qt widget parent ownership transfers through QToolBar::addWidget. No contextual
     *   visibility until caller supplies appropriate action handle.
     */
    QAction *addToolWidget(QWidget *widget);

    /*! \brief The cell-selection info label (created in the ctor; placed by
     *  SWMMVis right after the Select-2D-Cells action via addToolWidget).
     * @return Borrowed toolbar-owned label initially unplaced; caller can host through
     *   addToolWidget. Not a selected-cell validation API.
     */
    [[nodiscard]] QLabel *cellInfoLabel() const { return m_cellInfoLbl; }

    /*! \brief Per-cell editor widgets. The first is a parameter page —
     *  a combo naming the attribute to prescribe (Manning's n, initial
     *  depth, …, from mesh::cellParamSpecs) beside the value editor; the
     *  second is the descriptive tag field.
     *
     *  Created in the ctor but NOT placed — SWMMVis positions them right
     *  after the cell info label so they sit in the 2D-cell group, then
     *  hands the embedding QActions back via setCellEditorActions() so the
     *  toolbar can hide them when no cell is selected.
     * @return Borrowed owned parameter page, originally unplaced; caller embeds and returns action
     *   handle through setCellEditorActions.
     */
    [[nodiscard]] QWidget *cellParamEditorWidget() const;
    /**
     * @return Borrowed owned cell tag editor, originally unplaced; caller embeds and returns action
     *   handle. No data copy.
     */
    [[nodiscard]] QWidget *cellTagWidget() const;
    /**
     * @param[in] paramAct Optional borrowed action embedding parameter page, must outlive toolbar
     *   use.
     * @param[in] tagAct Optional borrowed action embedding tag editor, must outlive toolbar use.
     * @note Store raw handles then updateEnabledState applies contextual visibility/enabling/width
     *   recalculation. Does not create/place/own actions or edit values.
     */
    void setCellEditorActions(QAction *paramAct, QAction *tagAct);

    /*! \brief Place a cell-scoped action (currently "Assign Infiltration to
     *  Selection…") in the 2D-cell cluster, beside the param / tag editors.
     *
     *  Enabled only while at least one cell is selected — the dialog it opens
     *  reads the selection, so offering it with nothing selected is offering a
     *  dead end. Disabled rather than hidden (unlike the editor widgets)
     *  because the same QAction is mirrored into the Model ▸ Mesh menu.
     * @param[in] action Optional borrowed QAction; null/absent target no-op.
     * @note Stores as CURRENT infiltration action and appends to Results mini toolbar, then updates
     *   enablement. Repeated calls leave old action in bar but only latest tracked for contextual
     *   enabling. Action shared with menus, not hidden when no cells.
     */
    void addCellAction(QAction *action);

    /*! \brief Key of the parameter currently shown in the cell editor
     *         (mesh::CellParamSpec::key). Empty when the editor is absent.
     * @return Current combo item data as key, empty if absent/invalid. No spec lookup,
     *   enabled/selection check or engine access.
     */
    [[nodiscard]] QByteArray currentCellParamKey() const;

    /*! \brief Project depth unit ("ft" / "m") shown on length-valued cell
     *         parameters. Set by SWMMVis on each project-window switch.
     * @param[in] label Copied nonempty raw suffix, normally m or ft; empty/equal ignored.
     * @note Reconfigures current cell parameter widgets and rehydrates selection via
     *   onCellParamChanged. Label-only: does not convert stored cell/vertex values, BC stage or
     *   coupling area; other unit paths use global UnitSystem/mesh flags.
     */
    void setDepthUnitLabel(const QString &label);

    /**
     * @return Borrowed currently bound mesh guarded by QPointer, null after destruction. Can be
     *   auto-selected first mesh without isActiveMesh flag set by rebuild; do not treat getter as
     *   sole canvas active-flag guarantee.
     */
    [[nodiscard]] SWMM2DMeshLayer *activeMesh() const { return m_activeMesh; }

signals:
    /*! \brief Emitted when the user toggles the Edit Vertex action.
     *  SWMMVis hooks this to activate / deactivate MapToolMeshSelectVertex
     *  on the corresponding project window.
     * @param[in] active New QAction checked state, including programmatic toggles.
     * @note Signal forwarded before updateEnabledState; receiver switches canvas tool. Toolbar does
     *   not activate tool itself or clear selection.
     */
    void editVertexToggled(bool active);

    /*! \brief Emitted when the user toggles the Edit Edge action.
     * @param[in] active New QAction checked state, including programmatic toggles.
     * @note Signal forwarded before updateEnabledState; receiver switches canvas tool. Toolbar does
     *   not activate tool itself or clear selection.
     */
    void editEdgeToggled(bool active);

    /*! \brief Mirrors the mesh hover-Z probe outward (\a finite false =
     *  cursor off-mesh). SWMMVis feeds this into the status-bar
     *  coordinate readout so the elevation survives the toolbar's
     *  tab-scoped visibility.
     * @param[in] z Probe value in mesh native elevation units; finite flag determines usability.
     * @param[in] finite False indicates no usable probe elevation; z then not meaningful.
     * @note Forwarded before toolbar label update; no conversion/terrain offset. Label 3 decimals
     *   or em dash, no engine write.
     */
    void hoverElevationChanged(double z, bool finite);

private slots:
    /**
     * @param[in] index Combo row; 0 or invalid selects null.
     * @note Resolve raw stored pointer, clear old/other canvas active flags, set picked active
     *   flag, replace selected mesh hooks/probe and refresh vertex/edge/enabled state. Can
     *   synchronously trigger flag-change reentry; layer pointers must remain live. Does not push
     *   undo or refresh cell editor directly.
     */
    void onActiveMeshComboChanged(int index);
    /**
     * @param[in] layer Optional borrowed layer still live during signal; nonmesh ignored.
     * @note Mesh only: rebuild combo, then flush BC/node lists if previously skipped for no-mesh
     *   project.
     */
    void onLayerAdded(OpenSWMMVisLayer *layer);
    /**
     * @param[in] layer Optional borrowed layer still live during signal; nonmesh ignored.
     * @note Mesh only: disconnect/clear active mesh/probe if removed, then rebuild fallback combo.
     *   Does not delete layer or clear selection manager.
     */
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    /**
     * @param[in] isActive Ignored emitted flag; current canvas flags reread.
     * @brief Rebuild combo/editor state from all current mesh layers; does not trust sender/value.
     */
    void onActiveMeshFlagChanged(bool isActive);
    /**
     * @param[in] refName Ignored element reference; any active-mesh attribute update refreshes all
     *   editors.
     * @note No active mesh no-op. Rereads vertex/edge/cell controls under their hydration blockers;
     *   no explicit engine write, selection change or name filtering.
     */
    void onAttributeChanged(const QString &refName);
    /**
     * @param[in] z Probe elevation passed through unchanged.
     * @param[in] finite Probe validity, false shows em dash.
     * @note Emits hoverElevationChanged first, then updates label with 3 decimals; no model edit.
     */
    void onHoverElevation(double z, bool finite);
    /**
     * @param[in] z Raw requested vertex elevation in mesh stored/project vertical units; UI
     *   normalizes 3 decimal [-1e6,1e6], direct call not locally validated.
     * @note Suppression flag/no mesh no-op. pushVertexParamEdit(key=z) targets every currently
     *   selected keyed vertex on canvas undo stack, or helper direct fallback if absent. Does not
     *   require EditVertex action checked when called directly, inspect count/status or mutate
     *   selection.
     */
    void onZSpinChanged(double z);
    /**
     * @brief Submit trimmed descriptive tag through pushVertexParamEdit(tag) for all keyed selected
     *   vertices as one helper command.
     * @note Missing mesh/tag control/selection no-op. Helper/layer validate indices/value/coupling;
     *   Cd/area reject uncoupled vertices. No tool-mode guard, confirmation or checked result in
     *   this wrapper; helper chooses undo/direct path.
     */
    void onVertexTagCommit(); // descriptive vertex tag → selected vertex
    /**
     * @brief Submit trimmed current node ID, blank uncouples through
     *   pushVertexParamEdit(coupledNode) for all keyed selected vertices as one helper command.
     * @note Missing mesh/coupled-node combo/selection no-op. Helper/layer validate
     *   indices/value/coupling; Cd/area reject uncoupled vertices. No tool-mode guard, confirmation
     *   or checked result in this wrapper; helper chooses undo/direct path.
     */
    void onVertexCoupledCommit(); // coupled SWMM node → selected vertex
    /**
     * @brief Submit dimensionless spin Cd through pushVertexParamEdit(couplingCd) for all keyed
     *   selected vertices as one helper command.
     * @note Missing mesh/Cd spin/selection no-op. Helper/layer validate indices/value/coupling;
     *   Cd/area reject uncoupled vertices. No tool-mode guard, confirmation or checked result in
     *   this wrapper; helper chooses undo/direct path.
     */
    void onVertexCdCommit(); // coupling Cd → selected coupled vertices
    /**
     * @brief Submit spin exchange area in MESH LENGTH UNITS SQUARED, not always m² through
     *   pushVertexParamEdit(couplingArea) for all keyed selected vertices as one helper command.
     * @note Missing mesh/area spin/selection no-op. Helper/layer validate indices/value/coupling;
     *   Cd/area reject uncoupled vertices. No tool-mode guard, confirmation or checked result in
     *   this wrapper; helper chooses undo/direct path.
     */
    void onVertexAreaCommit(); // coupling area → selected coupled vertices
    /**
     * @brief Find coincident model nodes for selected keyed vertices, or whole mesh when none
     *   selected, then directly apply each matched coupling.
     * @note No active mesh no-op; empty locator result shows information. Already coupled vertices
     *   left unchanged by matcher; counts only true layer setter results and reports unmatched
     *   nodes. Coordinates must already share native CRS, no projection. Unlike ordinary vertex
     *   commit, this path directly calls applyMeshVertexCoupledNode and creates NO undo command or
     *   atomic rollback. Modal information permits reentry; host preserves model/toolbar lifetime.
     */
    void onAutoCoupleClicked(); // couple vertices to coincident SWMM nodes
    /**
     * @brief Build model-node and optional groundwater preview inputs and execute modal
     *   Remap1D2DDialog.
     * @note Requires active mesh, nonempty locator and FIRST model layer on canvas, otherwise
     *   info/no-op. Selected Node refs passed without per-model key filtering. Groundwater preview
     *   read-only, not engine enrolment; surface review callback forwarded. Dialog owns actual
     *   map/undo/confirmation behavior, wrapper ignores exec return. Native inputs must share mesh
     *   frame; injected callbacks/model lifetime coherent through reentry.
     */
    void onRemapClicked(); // Remap 1D↔2D: vertex + cell coupling (Plan C.4)

    /*! Unit label for the coupling AREA column: the active mesh's length
     *  units squared ("m²" for an SI-tagged mesh, else project length²).
     * @return m² for explicitly SI-tagged active mesh, else global UnitSystem lengthLabel plus²,
     *   fallback ft² if singleton absent. No numeric area conversion or per-engine unit lookup.
     */
    [[nodiscard]] QString meshAreaUnitLabel() const;
    /**
     * @note Missing spin no-op; forwards current numeric spin value to commitCellParam, which
     *   supplies selection/provenance/undo handling. Does not check visible numeric mode
     *   independently.
     */
    void onCellParamCommit(); // selected parameter → selected cells
    /**
     * @param[in] index Enum combo row; negative ignored, upper bound not explicitly checked
     *   (invalid data converts 0).
     * @note Missing combo no-op; passes item data double (spec min+row, not row number) to
     *   commitCellParam. No local spec/type/value validation.
     */
    void onCellEnumCommit(int index); // Kind::Enum editor → selected cells
    /**
     * @param[in] index Parameter combo row, item data looked up in CellParamSpec registry;
     *   invalid/unknown no-op.
     * @note Switch numeric versus enum control, hydrate labels/range/precision/step/prefix/depth
     *   suffix or enum labels under blockers, then refreshCellEditor. Does not write defaults to
     *   mesh. Disabled specs remain listed but programmatic calls are not blocked by enabled flag
     *   here.
     */
    void onCellParamChanged(int index); // reconfigure the value editor
    /**
     * @brief Trim current cell tag and submit shared pushCellTagEdit for all parsed MeshCell
     *   selection indices.
     * @note Missing mesh/control/selection no-op. Cell refs are NOT layer-key filtered in this
     *   toolbar; caller must keep selected cells coherent with active mesh. Helper validates
     *   bounds, uses undo/direct fallback; result ignored, no rollback or selection change here.
     */
    void onCellTagCommit(); // descriptive triangle tag → selected cell
    /**
     * @brief Project keyed selection refs into highlight sets for EVERY canvas mesh layer, then
     *   refresh vertex/edge/cell controls.
     * @note Vertex/edge/cell highlights filtered by sourcePath-derived layer key, no index validity
     *   check before setters. Does not drive 2D RESULTS highlights (project window handles active
     *   result layer). Cell editor extraction later ignores layer key, so caller must prevent
     *   mixed-mesh cell edits. No selection-manager mutation here, no undo.
     */
    void onSelectionChanged();
    /**
     * @param[in] index BC combo/page row; valid range switches stack, negative leaves previous
     *   page.
     * @note Display-hydration guard no-op. Otherwise commit current type/its currently retained
     *   page parameters immediately, then updateEnabledState; mixed invalid combo safely stops
     *   commit. Does not independently validate selected type enum.
     */
    void onBCTypeChanged(int index);
    /**
     * @brief Assemble current boundary type with ONLY its corresponding parameter and submit shared
     *   boundary edit for selected keyed edges.
     * @note Guarded during hydration/no active mesh/no combo/negative mixed-type index/empty
     *   selection. Slope signed dimensionless, stage raw project elevation, flow raw per-length
     *   discharge, TS/curve names untrimmed. Helper skips interior edges, preserves group labels
     *   and other helper-defined fields, records one undo entry/direct fallback; wrapper ignores
     *   result. No name existence lookup.
     */
    void commitBCParam(); // apply-as-you-go: write current param to selected edges
    /**
     * @brief Submit spin ψ through pushEdgeParamEdit(conveyance) to EVERY keyed selected edge,
     *   including interior.
     * @note Guarded during hydration/missing mesh/control/empty selection. UI range [0,1],
     *   dimensionless; layer mirrors neighbour half. Shared helper handles undo/direct
     *   fallback/validation; no boundary-only gate, no selection edit.
     */
    void commitConveyance(); // apply-as-you-go: ψ → every selected edge (interior + boundary)
    /**
     * @brief Dispatch stage-TS/flow-TS/rating-curve current type to matching picker;
     *   scalar/Wall/absent combo no-op.
     * @note No separate mode/selection guard, unit conversion or model lookup; child picker helper
     *   can trigger BC commit via changed text.
     */
    void onBrowseBCObject(); // shared "…" → dispatches to right CRUD picker
    /**
     * @brief Invoke optional stage time-series picker with current raw text, refresh all BC name
     *   lists, then apply nonempty returned ID.
     * @note Missing callback/combo no-op. Empty result cancels assignment but still refreshes
     *   lists. Setting changed text is unblocked and may immediately invoke commitBCParam on
     *   CURRENT type/selection; modal callback must preserve coherent context/lifetime. No direct
     *   model success return.
     */
    void onPickStageTseries();
    /**
     * @brief Invoke optional flow time-series picker with current raw text, refresh all BC name
     *   lists, then apply nonempty returned ID.
     * @note Missing callback/combo no-op. Empty result cancels assignment but still refreshes
     *   lists. Setting changed text is unblocked and may immediately invoke commitBCParam on
     *   CURRENT type/selection; modal callback must preserve coherent context/lifetime. No direct
     *   model success return.
     */
    void onPickFlowTseries();
    /**
     * @brief Invoke optional rating-curve picker with current raw text, refresh all BC name lists,
     *   then apply nonempty returned ID.
     * @note Missing callback/combo no-op. Empty result cancels assignment but still refreshes
     *   lists. Setting changed text is unblocked and may immediately invoke commitBCParam on
     *   CURRENT type/selection; modal callback must preserve coherent context/lifetime. No direct
     *   model success return.
     */
    void onPickRatingCurve();

private:
    /*! The BC / coupled-node dropdowns were skipped because the project has
     *  no mesh, and must be built if one appears. rebindCanvas() runs on every
     *  window activation and these lists span the whole model (one entry per
     *  node, plus every timeseries and curve), so building them for a 1D-only
     *  project is pure cost. */
    bool m_bcListsStale = false;

    /*! refreshNodeList() was skipped because the toolbar was hidden, and must
     *  run before the user can see the combo. See refreshNodeList() for why
     *  deferring this one is safe when deferring the attribute table's model
     *  was not. */
    bool m_nodeListStale = false;

    /**
     * @brief Replace list with none/current canvas meshes, prefer LAST flagged active or first mesh
     *   fallback, bind selected mesh/probe and refresh controls.
     * @note Null canvas clears/disables combo and returns before clearing activeMesh/probe;
     *   rebindCanvas clears those beforehand. Raw combo entries rely on coherent layer
     *   lifetime/removal signals. Fallback selection does not set isActiveMesh flags; only user
     *   combo callback enforces exclusivity. Refreshes vertex/edge/enabled state, not cell display
     *   directly.
     */
    void rebuildMeshCombo();
    /**
     * @brief Clear each owned ribbon group measured widths and synchronously reset outer toolbar
     *   layout geometry.
     * @note Used after contextual visibility changes; no action trigger, model edits or compactor
     *   mode solve. All raw group pointers expected live.
     */
    void refreshGroupWidths(); // re-measure the ribbon groups after
                               // contextual clusters show/hide
    /**
     * @param[in] layer Optional borrowed mesh; null no-op.
     * @note Connect unique member callbacks for active flag/attributes. Replace all layer
     *   selectionInvalidated→this hookups with one lambda that CLEARS ENTIRE bound SelectionManager
     *   (not only this mesh) and refreshes vertex/edge. No ownership, initial value hydration or
     *   destroyed rebind.
     */
    void connectMeshLayer(SWMM2DMeshLayer *layer);
    /**
     * @param[in] layer Optional borrowed mesh, null no-op.
     * @note Disconnect every signal from this layer to this toolbar, including externally installed
     *   ones on same receiver; does not clear pointer/probe/selection or change active flag.
     */
    void disconnectMeshLayer(SWMM2DMeshLayer *layer);
    /**
     * @brief Hydrate area suffix, mean Z, common descriptive tag/node and coupled-only common
     *   Cd/area from keyed valid selected vertices.
     * @note No valid vertices resets/disables Z and info, leaving some hidden controls unchanged.
     *   Mixed tag shows placeholder, mixed node blank; mixed/no-coupled Cd/area display defaults
     *   0.65/1. Values signal-blocked, no mesh writes. Does not itself update all contextual
     *   enablement, callers also invoke edge/enabled refresh. Mesh SI flag/global units label area
     *   without converting numeric values.
     */
    void refreshVertexEditor();
    /**
     * @brief Hydrate deterministic selected-edge summary and common boundary fields, with
     *   conveyance aggregated over all selected edges.
     * @note Missing manager/mesh updates info only. Hydration guard plus signal blockers prevent
     *   nested writes during attributeChanged from running bulk command. Mixed BC type sets index
     *   -1; mixed scalar fields use zero, mixed names blank placeholder, mixed ψ default 1. Then
     *   updateEnabledState/reflow. No BC/conveyance mutation.
     */
    void refreshEdgeEditor();
    /**
     * @brief Hydrate parsed cell selection summary/current parameter and tag, common value or spec
     *   default on disagreement/nonfinite values.
     * @note MeshCell refs unkeyed; reads active mesh valid indices only but displayed selection
     *   count can include invalid/other-mesh refs. Numeric/enum/tag assignment blocked; missing
     *   cells leaves editor values unchanged. Label works results-only with no active editable
     *   mesh, no writes/undo. Does not itself update contextual visibility.
     */
    void refreshCellEditor();
    /**
     * @brief Recompute edit-action, vertex/coupling/cell/BC/conveyance visibility/enabling and
     *   remeasure ribbon groups.
     * @note Vertex controls require vertex mode and keyed selection, Cd/area at least one coupled
     *   vertex; cell edits need active mesh plus ANY parsed MeshCell refs; BC needs edge
     *   mode+boundary selection, ψ any edge. Remap needs mesh; tool toggles not programmatically
     *   unchecked here. No selection/model write.
     */
    void updateEnabledState();
    /*! \brief Write \p value into the currently selected cell parameter on
     *  every selected cell, as one undo entry.
     *
     *  The `infil.*` keys are routed to mesh::pushCellInfilEdit rather than
     *  mesh::pushCellParamEdit: only that command snapshots PROVENANCE, so
     *  only its undo puts an inheriting cell back to inheriting instead of
     *  leaving a materialised override carrying the same numbers.
     * @param[in] value Raw current parameter value; units/range from CellParamSpec, no general
     *   finite/local range validation here.
     * @note Missing active mesh/cells/key no-op. Generic fields go through pushCellParamEdit;
     *   infiltration fields resolve each valid selected cell current effective row, modify
     *   requested slot/method, clear unused method slots to NaN and group identical resulting rows
     *   for pushCellInfilEdit so undo restores inheritance provenance. Invalid method returns;
     *   unsupported slot skipped. Multiple rows wrap one canvas macro if stack present, otherwise
     *   helper direct edits.
     * @note All MeshCell refs accepted regardless of layer key; caller ensures active-mesh
     *   coherence. Command/helper result ignored; invalid elements can be skipped and bulk edit is
     *   not promised atomic. No displayed-unit conversion, validation dialog or engine run restart.
     */
    void commitCellParam(double value);

    /**
     * @return New list of Parsed vertex IDs whose sourcePath-derived layer key matches active mesh,
     *   empty without mesh/selection. Hash iteration order retained, not sorted or bounds checked.
     *   No ownership transfer or selection mutation.
     */
    QList<int> currentSelectedVertices() const; // all selected vertex indices
    /**
     * @return New list of Parsed (cell,localEdge) pairs matching active mesh key, lexicographically
     *   sorted for deterministic commands/display. Empty without mesh/selection; no topology bounds
     *   validation. No ownership transfer or selection mutation.
     */
    QList<QPair<int, int>> currentSelectedEdges() const; // (tri, eLocal) pairs
    /**
     * @return New list of ALL parsed MeshCell IDs from selection, ignoring layer key and
     *   active-mesh presence. Hash iteration order retained, no bounds validation. Intended label
     *   fallback also reaches edit path, so host must ensure coherence. No ownership transfer or
     *   selection mutation.
     */
    QList<int> currentSelectedCells() const; // all selected triangle indices

    // Iteration 3 — captioned ribbon groups, each hosting a mini
    // toolbar so the action-based show/hide machinery keeps working.
    QList<openswmmvis::ui::RibbonGroup *> m_groups;
    QToolBar *m_barMesh = nullptr;
    QToolBar *m_barVertices = nullptr;
    QToolBar *m_barEdges = nullptr;
    QToolBar *m_barCoupling = nullptr;
    QToolBar *m_barResults = nullptr; // Pick cells cluster (SWMMVis inserts)
    QToolBar *m_barProfile = nullptr; // standalone profile plotter

    QPointer<MapCanvas> m_canvas;
    QPointer<SelectionManager> m_selection;
    QPointer<SWMM2DMeshLayer> m_activeMesh;
    mesh::MeshHoverProbe *m_hover = nullptr;

    // Top row
    QComboBox *m_meshCombo = nullptr;
    QLabel *m_hoverLabel = nullptr;
    QAction *m_spacerAction = nullptr; // trailing stretch; tool actions insert before it

    // Edit mode toggles
    QAction *m_actEditVertex = nullptr;
    QAction *m_actEditEdge = nullptr;
    QActionGroup *m_editGroup = nullptr;

    // Vertex Z editor
    QLabel *m_vertexInfoLbl = nullptr;
    QDoubleSpinBox *m_zSpin = nullptr;
    QLineEdit *m_vertexTagEdit = nullptr;      // descriptive tag
    QComboBox *m_vertexCoupledCombo = nullptr; // coupled SWMM node (dropdown)
    QAction *m_actVertexTag = nullptr;         // embedding actions for hide
    QAction *m_actVertexCoupled = nullptr;
    QDoubleSpinBox *m_vertexCdSpin = nullptr;   // coupling Cd ([2D_VERTEX_NODE_MAP] CD)
    QDoubleSpinBox *m_vertexAreaSpin = nullptr; // coupling exchange area, m² (AREA)
    QAction *m_actVertexCd = nullptr;           // embedding actions for hide
    QAction *m_actVertexArea = nullptr;
    QAction *m_actAutoCouple = nullptr; // couple vertices to coincident nodes
    QAction *m_actRemap = nullptr;      // Remap 1D↔2D (Plan C.4)

    // BC controls (Slice §V.VC — fully wired for all 7 GUI BC types).
    QComboBox *m_bcTypeCombo = nullptr;
    QStackedWidget *m_bcParamStack = nullptr;
    QAction *m_actBrowseObj = nullptr; // launches TS / Curve CRUD dialog
    QLabel *m_edgeInfoLbl = nullptr;
    // addWidget() handles for the BC combo + param stack so they can be
    // shown/hidden contextually (hiding the QAction collapses the gap).
    QAction *m_actBCTypeCombo = nullptr;
    QAction *m_actBCParamStack = nullptr;

    // Cell-selection info label (after the Select-2D-Cells tool).
    QLabel *m_cellInfoLbl = nullptr;
    // Cell parameter page: "<parameter> [value]" — one editor for every
    // per-cell attribute in mesh::cellParamSpecs().
    QWidget *m_cellParamPage = nullptr;
    QComboBox *m_cellParamCombo = nullptr;
    QDoubleSpinBox *m_cellValueSpin = nullptr;
    // Value editor for CellParamSpec::Kind::Enum parameters (infiltration
    // method). Shares the slot the spin box occupies — exactly one of the two
    // is visible, chosen by the selected parameter's kind. A −1…5 numeric
    // spinner for an enumeration is not an editor, it is a puzzle.
    QComboBox *m_cellEnumCombo = nullptr;
    QLineEdit *m_cellTagEdit = nullptr; // descriptive triangle tag
    QAction *m_actCellParam = nullptr;  // embedding actions for hide
    QAction *m_actCellTag = nullptr;
    QAction *m_actCellInfil = nullptr; // "Assign Infiltration to Selection…"
    // Project depth unit ("ft"/"m") shown on length-valued parameters.
    QString m_depthUnitLabel = QStringLiteral("m");

    // Param-page widgets (held directly so the apply path reads values
    // without going through QStackedWidget::currentWidget casts).
    QDoubleSpinBox *m_wallDragSpin = nullptr;
    QDoubleSpinBox *m_slopeSpin = nullptr; // NormalFlow
    QDoubleSpinBox *m_stageSpin = nullptr; // SpecifiedStageConst
    QComboBox *m_stageTSCombo = nullptr;   // SpecifiedStageTS — editable combo over registry
    QDoubleSpinBox *m_flowSpin = nullptr;  // SpecifiedFlowConst
    QComboBox *m_flowTSCombo = nullptr;    // SpecifiedFlowTS  — editable combo over registry
    QComboBox *m_curveCombo = nullptr;     // RatingCurve     — editable combo over registry

    // Engine §11A — per-edge flux attenuation (conveyance) widget. Lives
    // OUTSIDE the BC param stack because it applies to every edge (interior
    // and boundary), not just boundary edges. Visibility tracks edgeMode +
    // any selected edge (independent of haveBoundaryEdge).
    QDoubleSpinBox *m_conveySpin = nullptr;
    QAction *m_actConveySpin = nullptr;

    bool m_suppressZSignal = false;
    /*! True while refreshEdgeEditor() hydrates the BC widgets — the
     *  commit slots early-return on it so a display refresh (including a
     *  re-entrant one fired by a running bulk command's per-slot
     *  attributeChanged) can never push an edit. */
    bool m_refreshingEdgeEditor = false;

    PickerFn m_stageTSPicker;
    PickerFn m_flowTSPicker;
    PickerFn m_curvePicker;

    ListerFn m_tsLister;
    ListerFn m_curveLister;
    ListerFn m_nodeLister;
    NodeLocatorFn m_nodeLocator;
    GwPreviewSourceFn m_gwPreviewSource;
    std::function<void()> m_surfaceOwnershipReview;
};

#endif // OPENSWMMVIS_UI_TOOLBARS_MESHEDITINGTOOLBAR_H
