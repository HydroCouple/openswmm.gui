/**
 * @file      swmmvisprojectwindow.h
 * @brief     MDI sub-window that hosts a single SWMM model session.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * MDI sub-window that hosts a single SWMM model session.
 * Each opened .inp file gets its own window with a dedicated MapCanvas and tools.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMVISPROJECTWINDOW_H
#define SWMMVISPROJECTWINDOW_H

#include "mesh/channelburnprofile.h"
#include "mesh/corridorsource.h"

#include <QHash>
#include <QJsonArray>
#include <QMdiSubWindow>
#include <QList>
#include <QPointF>
#include <QPointer>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

#include "plot/plotattribute.h" // PlotAttribute (edge flow vs flux relay)
#include "plot/resultdescriptor.h"
#include "layers/swmmmodellayer.h" // SWMMModelLayer::NewProjectSpec (nested)

class OpenSWMMVisWorkspace;
class MapCanvas;
class SWMMModelLayer;
class SpatialReferenceSystem;
class SWMMResultsLayer;
class SWMM2DResultsLayer;
class SelectionManager;
class UnitSystem;
class OpenSWMMVisMapTool;
class OpenSWMMVisMapToolPan;
class OpenSWMMVisMapToolZoom;
class OpenSWMMVisMapToolSelect;
class OpenSWMMVisMapToolSelectPolygon;
class OpenSWMMVisMapToolMeasure;
class OpenSWMMVisMapToolAddNode;
class OpenSWMMVisMapToolAddVirtualNode;
class OpenSWMMVisMapToolAddLink;
class OpenSWMMVisMapToolAddGage;
class OpenSWMMVisMapToolAddSubcatchment;
class OpenSWMMVisMapToolAddText;
class OpenSWMMVisAnnotationLayer;
class GISRasterLayer;
class QComboBox;
class QLabel;
class QFrame;

namespace openswmmvis
{
class OutputStatsRegistry;
} // Slice QA.2

/**
 * @brief MDI sub-window owning a MapCanvas, per-project services and map tools, referring to a
 *   workspace-associated model layer.
 *
 * One instance is created per opened .inp file.
 */
class SWMMVisProjectWindow : public QMdiSubWindow
{
    Q_OBJECT

public:
    /**
     * @param[in] workspace Borrowed workspace passed to model-layer construction; keep its
     *   resources/model alive while window uses them.
     * @param[in] filePath Copied initial model path; empty creates an unloaded Untitled-named
     *   layer, not automatically a blank engine.
     * @param[in] parent Optional Qt/MDI owner; window has WA_DeleteOnClose and should normally be
     *   heap allocated.
     * @brief Create canvas, units, selection/statistics services and tools; wire dirty tracking and
     *   result registration without loading a model.
     * @note Default engine/tool/auto-length derive from preferences. Model layer is allocated with
     *   workspace, canvas layers are non-owning references; this window does not explicitly delete
     *   model on destruction. Annotation layers created here are canvas-parented. Project APIs and
     *   engine access are GUI-thread only unless expressly described as worker preparation.
     */
    explicit SWMMVisProjectWindow(OpenSWMMVisWorkspace *workspace,
                                  const QString &filePath = QString(), QWidget *parent = nullptr);

    /**
     * @brief If not already closing, set closing flag and emit aboutToClose while canvas/services
     *   still exist, then allow Qt child teardown.
     * @note Direct destruction does not prompt/save. Does not explicitly close/delete
     *   workspace-associated model or all non-owned canvas layers; pending job guards discard
     *   results and pending mesh artifacts are abandoned through attached-layer destruction
     *   connection.
     */
    ~SWMMVisProjectWindow() override;

    /**
     * @return Required canvas child created in constructor; borrowed, do not delete or retain after
     *   close.
     */
    MapCanvas *canvas() const;
    /**
     * @return Borrowed workspace-associated model layer; can be null after guarded async source
     *   destruction and may have no engine.
     */
    SWMMModelLayer *modelLayer() const;
    /**
     * @return Borrowed project-owned unit labels/conversion service, nonnull during normal project
     *   lifetime.
     */
    UnitSystem *unitSystem() const;
    /**
     * @return Borrowed project-owned cross-view selection bus, nonnull during normal project
     *   lifetime.
     */
    SelectionManager *selectionManager() const;

    /** Offset mode (LINK_OFFSETS option). True = ELEVATION, false = DEPTH.
     * @return Cached LINK_OFFSETS interpretation; not a fresh engine read or guarantee a prior
     *   option write succeeded.
 */
    bool isElevationOffsetMode() const { return mElevationOffsetMode; }
    /**
     * @param[in] elevation True requests ELEVATION, false DEPTH interpretation; does not
     *   numerically convert existing offsets.
     * @brief On cached change, update cache, attempt engine option write when present, mark dirty
     *   and emit offsetModeChanged.
     * @note Engine write status ignored, so cache can differ on rejection. Without engine still
     *   caches/emits but does not dirty; unchanged cache is a no-op. Numeric conversion is separate
     *   convertLinkOffsets call.
     */
    void setElevationOffsetMode(bool elevation);

    /*! Apply the answer to the legacy UpdateOffsets convert prompt after the
     *  mode flip (see SWMMModelLayer::convertLinkOffsets) and mark the project
     *  dirty. \param toElevation the mode switched TO; \param convertValues the
     *  prompt answer (true = Yes).
     * @note Calls model conversion and marks dirty regardless of conversion result/prompt answer
     *   when model exists. Missing model no-op; does not itself change cached interpretation or
     *   emit offsetModeChanged. Node invert and link offsets use native project ft/m.
 */
    void convertLinkOffsets(bool toElevation, bool convertValues);

    /*! Re-read LINK_OFFSETS from the engine into the cached
     *  mElevationOffsetMode flag. Used by the main-window listener for
     *  SWMMModelLayer::optionsChanged so the status-bar checkbox
     *  refreshes after a Simulation Options Apply.
     * @note Reads up to 32-byte option text; successful exact case-insensitive trimmed ELEVATION
     *   sets true, other successful text false. Missing engine/getter failure preserves cache. Does
     *   not emit offsetModeChanged or dirty project.
 */
    void reloadElevationOffsetModeFromEngine();

    /**
     * @param[in] warnings Borrowed output diagnostics forwarded to layer load and completion;
     *   recovery notices appended here.
     * @param[in] errors Borrowed output diagnostics; window refusal/recovery failure appends one
     *   message.
     * @return True only after synchronous recovery, layer load and finishModelLoad succeed; false
     *   for opening/closing/missing context or any load/completion failure.
     * @note May prompt for CRS and process nested GUI events. No automatic window deletion/rollback
     *   on failure; caller handles partially loaded layer. Unlike async entry, this wrapper does
     *   not independently refuse an already loaded engine.
     */
    bool loadModel(QList<QString> &warnings, QList<QString> &errors);

    /*!
     * \brief Subscribe a 2D mesh layer's edit signals to the project's dirty
     *        flag.
     *
     * Every mesh mutation funnels through SWMM2DMeshLayer::apply* and reports
     * via attributeChanged / meshEditsChanged. Without this the project stays
     * "clean" after a cell edit, so Run skips its auto-save and the engine
     * re-reads the stale .inp — the edit silently never reaches the solver.
     * Call once per mesh layer, right after it joins the canvas.
     *
     * \param pristine  Pass true only when the layer was built from the very
     *   file the engine parsed, so the two meshes are known to agree and the
     *   save path may skip re-pushing this layer until something edits it.
     *   The default (false) is the safe one: a mesh generated or imported
     *   in-session leaves the engine holding the old mesh.
     * @param[in] meshLayer Optional borrowed mesh layer, no ownership transfer; null no-op.
     *   Expected live for its own edit signals.
     * @note Binds model infiltration context, subscribes active-mesh and per-attribute/bulk edits
     *   to dirty flags, and abandons generated disk artifacts on project destruction. Pristine
     *   immediately marks mesh edits saved; does not compare content. Intended once: repeated
     *   attachment adds duplicate connections. Does not add layer to canvas or select it.
     */
    void attachMeshLayer(class SWMM2DMeshLayer *meshLayer, bool pristine = false);

    /*!
     * \brief Browse-and-load an existing SWMMVis 2D mesh (.2dm) into this
     *        project.
     *
     * The source is snapshotted and parsed on a worker. SMS conversion writes
     * only that private snapshot. The source, chosen destination and engine
     * reference remain unchanged until project Save publishes the import.
     * A current successful result becomes the ACTIVE external mesh; changed,
     * closed or superseded projects discard the result and its snapshot.
     *
     * Existing mesh layers are kept (merely deactivated); a layer already
     * reading the destination file is replaced rather than stacked, matching
     * the mesh-generation path.
     *
     * Asynchronous — the outcome arrives via meshImportFinished().
     * @param[in] srcPath Existing regular source mesh path, copied for private worker snapshot; no
     *   ownership transfer or source modification.
     * @note Each call supersedes earlier import serial, even when new request fails preflight. GUI
     *   may prompt overwrite/keep-both for logical project-directory destination; captured
     *   fingerprints rechecked before staging. Untitled import can stage in temp and rebase at
     *   first Save As. SMS converts only private snapshot; standalone section imports reject nested
     *   mesh/initial-quality/output file references.
     * @note Worker owns plain mesh data/private artifacts, GUI validates captured
     *   model/engine/path/revision/canvas and broad change signals before adoption. Existing meshes
     *   deactivated, same absolute destination layer removed/deleted later, successful new layer
     *   becomes active, starts deferred scene preparation and dirties project. No undo command or
     *   live-engine mesh replacement here; publication and mesh reference update belong to Save.
     */
    void importMeshFileAsync(const QString &srcPath);

    /*!
     * \brief Open a model with engine parsing on a worker.
     *
     * Engine create+open runs in a QtConcurrent worker. Geometry/cache
     * preparation, adoption and CRS resolution run on the GUI thread and may
     * take time on large models. Duplicate requests and replacing an already
     * loaded model are refused; closed or changed owners discard the result.
     *
     * \param progress Optional determinate-progress sink. The worker reports
     *        into EngineParse; GUI adoption completes SoaCopy / GeomCache. Null is the
     *        "nobody is watching" case used by tests and the sync path.
     * @note Progress is borrowed/guarded and may be destroyed during job. Refused
     *   duplicate/loaded/closing requests queue false completion; closed/destroyed owners receive
     *   no completion. Hides layer during work, catches worker/adoption exceptions and restores
     *   visibility on failure only if layer has no engine.
     * @note Captures path/revision/identity and invalidates on close, path/geometry/engine changes.
     *   Worker owns detached engine/plain data; GUI copies caches then transfers engine ownership
     *   to layer. A later completion failure can leave an adopted engine, not roll back to unopened
     *   state. No caller cancellation handle; no QObject source access from worker.
     */
    void loadModelAsync(class OpenProgressModel *progress = nullptr);

    /*!
     * \brief In-memory File → New: build a blank BUILDING-state engine from
     *        \p spec (SWMMModelLayer::adoptNewEngine) and finish the load
     *        synchronously. The window keeps its empty model path until the
     *        first Save As.
     * @param[in] spec Borrowed initial blank-model options passed to adoptNewEngine; no source file
     *   written here.
     * @param[in] warnings Borrowed initialization diagnostic output forwarded to model.
     * @param[in] errors Borrowed initialization/completion diagnostic output, refusal appended
     *   here.
     * @return True after engine adoption and synchronous shared completion. False leaves any
     *   completed partial adoption to caller; no automatic markUntitled here, caller must set that
     *   flag separately.
     */
    bool initializeBlankModel(const SWMMModelLayer::NewProjectSpec &spec, QList<QString> &warnings,
                              QList<QString> &errors);

    /**
     * @brief Save the model to its current path or to a new path.
     * @return true on success.
     * @param[in] errorOut Optional borrowed diagnostic; missing path sets message, otherwise
     *   follows saveAs clear/failure behavior.
     * @note Does not open Save As dialog itself. Missing layer/path or untitled returns false
     *   requesting Save As; otherwise delegates current model path. Can perform substantial
     *   synchronous disk/engine work.
     */
    bool save(QString *errorOut = nullptr);
    /**
     * @param[in] newPath Requested model destination as supplied, including extension selecting
     *   enabled writer plugin; caller handles chooser/overwrite consent.
     * @param[in] errorOut Optional output cleared after live-engine check; assigned diagnostic on
     *   reported failure. Missing engine sets message.
     * @return True only after write, required settings/mesh publication and final path/reference
     *   adoption; false leaves project dirty for reported save failures.
     * @note Built-in path stages physical engine outputs, GUI mesh patches and .oswp, validates
     *   prepared project and publishes via recovery-aware output manager. Plugin path writes
     *   directly and skips GUI sidecar publication; successful plugin path still adopts requested
     *   model path and clears dirty here. No universal atomic rollback: engine
     *   synchronization/registry flush/CRS changes can precede failure, and
     *   publication/final-reference failures may leave disk updated.
     * @note Resolves one active mesh (sole layer fallback), rejects multiple
     *   active/ambiguous/inactive-unsaved meshes, invalid geometry/BC shape, conflicting file
     *   identities and unsupported generated-topology writer. Clean sole matching-count mesh can
     *   skip engine push; generated topology is fully serialized. Protects corridor/generated
     *   dependencies and snapshots imported mesh; source/destination fingerprints enforced by
     *   output manager.
     * @note On full success migrates path-keyed settings, promotes untitled, clears project and
     *   pushed mesh dirty state and emits warning delta last. Last warnings list can contain
     *   writer-success warnings even if later stage fails. Does not enforce simulation
     *   lifecycle/read-only gate itself or catch every thrown exception.
     */
    bool saveAs(const QString &newPath, QString *errorOut = nullptr);

    /*! Captured warnings from the most recent save attempt that reached the writer.
     * Early missing-model/corridor failures can retain the previous list; later failures can leave
     * the current writer/validation warnings even though publication or final adoption failed.
     * The successful-save warning signal is emitted only after the entire save settles.
     * @return Copy of cached writer/validation warnings; see attempt/early-failure qualifications
     *   above. Does not query engine or certify save success.
 */
    QStringList lastSaveWarnings() const { return mLastSaveWarnings; }

    /** Whether the project has unsaved changes.
     * @return Cached unsaved-project flag, independent of undo stack cleanliness or external file
     *   changes.
 */
    bool hasChanges() const { return mHasChanges; }

    /** Mark the project dirty/clean (also updates the title).
     * @param[in] dirty New cached state; equality is a no-op.
     * @note Updates title and emits hasChangesChanged only on transition. Does not save, mark
     *   individual mesh edits or change undo history.
 */
    void setHasChanges(bool dirty);

    /** True while the model's CRS is only provisional: never assigned by the
     *  user (defaulted on open), missing, or a local engineering frame. In
     *  that state there is one CRS to choose, and choosing it from the status
     *  bar sets the PROJECT CRS directly (owner decision 2026-10-17).
     * @return False without model; otherwise true for unassigned, absent or local SRS. Uses layer
     *   metadata, not fresh engine query.
 */
    bool projectCrsIsProvisional() const;

    /** Assign \p srs as the project CRS without moving any coordinate: the
     *  engine's spatial frame (written to [OPTIONS] CRS on save), the model
     *  layer, and the canvas all adopt it; the view is refitted and the
     *  project marked dirty. Returns false (with \p errorOut) if the engine
     *  rejects the CRS.
     * @param[in] srs Borrowed CRS copied to layer/canvas after engine acceptance; coordinates are
     *   reinterpreted, not transformed.
     * @param[in] errorOut Optional failure text for absent context/rejected engine setter; not
     *   cleared on success.
     * @return False before view changes if no model/engine/canvas or spatial setter rejects; true
     *   after owned CRS copies, geometry reload/invalidation, refit and dirty mark.
     * @note Uses authority unless absent/Local, then WKT. Explicit assignment also sets canvas
     *   geographic CRS when requested; general load synchronization may keep projected canvas. No
     *   undo or rollback of later exceptions.
 */
    bool assignProjectCrs(const SpatialReferenceSystem &srs, QString *errorOut = nullptr);

    /** Whether an editing session is active (gate for all geometry mutations).
     * @return Cached edit-session UI gate; does not inspect engine state or every editor
     *   independently.
 */
    bool isEditSessionActive() const { return mEditSessionActive; }
    /**
     * @param[in] active New cached edit-session flag; equality no-op.
     * @note Emits editSessionChanged only; no save/dirty/undo or general active-tool replacement.
     *   Text tool connection returns to Select when session ends.
     */
    void setEditSessionActive(bool active);

    /*! Slice Y — flag a window as a fresh, never-saved project.
     *  Untitled windows skip recent-files registration, force the
     *  title to "Untitled[*]", route Save through Save As, and always
     *  prompt on close (Save As… / Discard / Cancel) — the model lives
     *  only in memory until the first successful Save As clears the flag.
     * @return Cached never-saved flag; markUntitled and successful saveAs control it.
 */
    bool isUntitled() const { return mUntitled; }

    /*! True from the moment closeEvent() commits to closing (just before
     *  aboutToClose() fires) — the window is still in the MDI area's
     *  subWindowList() then, so callers that scan it for "live" project
     *  windows must skip a closing one or they rebind to a dying canvas.
     * @return Cached close-commit flag, also set before destructor fallback aboutToClose; not
     *   merely a pending close prompt.
 */
    bool isClosing() const { return mClosing; }
    /**
     * @brief Set untitled cache and refresh title, without changing model path/engine, dirty flag
     *   or emitting notification. Close then prompts even with no edits.
     */
    void markUntitled();

    /**
     * @brief Select preconstructed pan tool on required canvas; no toggle, immediate geometry write
     *   or edit-session activation in this wrapper. Tool handles subsequent gestures/eligibility.
     */
    void activatePanTool();
    /**
     * @brief Select preconstructed zoom-in tool on required canvas; no toggle, immediate geometry
     *   write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateZoomInTool();
    /**
     * @brief Select preconstructed zoom-out tool on required canvas; no toggle, immediate geometry
     *   write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateZoomOutTool();
    /**
     * @brief Clear 1D profile selection only when leaving that active tool, then select ordinary
     *   selection tool. No engine mutation or selection-bus clearing otherwise.
     */
    void activateSelectTool();
    /**
     * @brief Select preconstructed polygon selection tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateSelectByPolygonTool();
    /**
     * @brief Toggle measure tool with ordinary Select, preserving its existing measurement;
     *   active-tool signal shows/hides owned floating panel and syncs mode/units.
     */
    void activateMeasureTool();
    /**
     * @brief Toggle preconstructed 1D profile tool with Select; this toggle path does not call
     *   activateSelectTool and its explicit profile clear.
     */
    void activateSelectProfileTool();

    /*! Slice CF.3 — activate the Pick 2D Cells tool on this canvas.
     *  Lazy-creates the tool on first call even without a 2D results layer; the tool resolves
     *   availability itself.
     * @note Always creates/activates on first use, no results-layer preflight in wrapper. Forwards
     *   cellsPicked; target and source availability handled by tool/active-layer setters. Repeated
     *   calls keep same instance, do not toggle.
 */
    void activatePick2DCellsTool();

    /*! Trace a free-form polyline across the 2D mesh to plot a longitudinal
     *  profile. Lazy-creates the tool; toggles back to Select when already
     *  active (mirrors activateSelectProfileTool).
     *
     *  Slice US.A1 — this is the MESH-TOOLBAR profile tool: bed/terrain only
     *  (emits meshProfileTraced, routed to a results=nullptr dialog). The
     *  Analysis-toolbar water-depth variant is activateAnalysisMeshProfileTool.
     * @note Lazily owns a separate geometric trace tool, forwards meshProfileTraced (bed-only
     *   channel) and toggles to Select if already active. No mesh/terrain/results availability
     *   check or sampling here; receiver selects data source.
 */
    void activateMeshProfileTool();

    /*! Slice US.A1 — the ANALYSIS-toolbar 2D-surface profile tool: traces the
     *  same polyline but plots ground + animated depth + envelope against the
     *  active 2D results layer (emits analysisMeshProfileTraced). A second
     *  MapToolMeshProfile instance so its checked state tracks actionPlotProfile
     *  while the mesh-toolbar tool tracks actionMeshProfile independently.
     * @note Lazily owns a separate geometric trace tool, forwards analysisMeshProfileTraced
     *   (analysis depth/envelope channel) and toggles to Select if already active. No
     *   mesh/terrain/results availability check or sampling here; receiver selects data source.
 */
    void activateAnalysisMeshProfileTool();

    /*! The TERRAIN-TOOLBAR profile tool: traces the same polyline but plots a
     *  ground profile sampled from the active DEM raster rather than the 2D
     *  mesh (emits terrainProfileTraced). A third MapToolMeshProfile instance —
     *  the tool is purely geometric — so its checked state tracks
     *  actionTerrainProfile independently of the two mesh profile actions.
     * @note Lazily owns a separate geometric trace tool, forwards terrainProfileTraced (DEM
     *   channel) and toggles to Select if already active. No mesh/terrain/results availability
     *   check or sampling here; receiver selects data source.
 */
    void activateTerrainProfileTool();

    /*! Slice US.A2 — availability probes for the context-sensitive Plot Profile
     *  dispatcher: is there a 1D SWMM model / a 2D mesh on the canvas?
     * @return True if any current canvas layer has SWMMModelLayer type; false without canvas. Does
     *   not require visible, active, loaded engine or nonempty geometry.
 */
    [[nodiscard]] bool hasModelLayer() const;
    /**
     * @return True if any current canvas layer has SWMM2DMeshLayer type; false without canvas. Does
     *   not require visible, active, loaded engine or nonempty geometry.
     */
    [[nodiscard]] bool hasMeshLayer() const;

    /*! Slice §V.VB — activate the Mesh Vertex Select tool. Lazy-creates
     *  on first call.
     * @note Lazily create project-owned selector with canvas/selection bus, forward
     *   meshVertexSeriesRequested and set active; no toggle or mesh-presence preflight.
 */
    void activateMeshSelectVertexTool();

    /*! Slice §V.VB — activate the Mesh Edge Select tool. Lazy-creates
     *  on first call.
     * @note Lazily create project-owned selector with canvas/selection bus, forward
     *   meshEdgeFluxRequested and meshEdgeStatusMessage and set active; no toggle or mesh-presence
     *   preflight.
 */
    void activateMeshSelectEdgeTool();

    /*! Direct access to the Select tool so the main window can wire its
     *  context-menu `plotTimeSeriesRequested` into the same chart
     *  dialog that Object Browser right-clicks use.
     * @return Preconstructed ordinary selection tool; borrowed project-owned pointer, no
     *   creation/activation or ownership transfer.
 */
    class OpenSWMMVisMapToolSelect *selectTool() const { return mSelectTool; }

    /*! Same direct-access pattern for Slice BC's profile tool — lets
     *  SWMMVis connect `profilePathSelected` to the ProfilePlotDialog
     *  spawner.
     * @return Preconstructed 1D profile selection tool; borrowed project-owned pointer, no
     *   creation/activation or ownership transfer.
 */
    class OpenSWMMVisMapToolSelectProfile *selectProfileTool() const { return mSelectProfileTool; }

    /*! Virtual-junction insertion tool (click-a-conduit split). Exposed so
     *  SWMMVis can route its statusMessageChanged to the status bar.
     * @return Preconstructed virtual-junction split tool; borrowed project-owned pointer, no
     *   creation/activation or ownership transfer.
 */
    class OpenSWMMVisMapToolAddVirtualNode *addVirtualJunctionTool() const
    {
        return mAddVirtualJunctionTool;
    }

    /*! The generic add-node tools (junction / outfall / storage / divider),
     *  which also split a conduit when the click lands on one. Exposed so
     *  SWMMVis can route their statusMessageChanged to the status bar.
     * @return Owning list of borrowed junction/outfall/storage/divider tool pointers in that order.
     *   Does not include LID, virtual or inlet tools. No activation or ownership transfer.
 */
    QList<OpenSWMMVisMapToolAddNode *> addNodeTools() const
    {
        return {mAddJunctionTool, mAddOutfallTool, mAddStorageTool, mAddDividerTool};
    }

    /*! Inlet-junction insertion tool (click-a-street-conduit split). Exposed
     *  for the same reason as the virtual-junction tool above.
     * @return Preconstructed inlet-junction split tool; borrowed project-owned pointer, no
     *   creation/activation or ownership transfer.
 */
    class OpenSWMMVisMapToolAddInletNode *addInletJunctionTool() const
    {
        return mAddInletJunctionTool;
    }

    /*! Slice CF.3 — Pick 2D mesh cells tool (box + lasso). Returns null
     *  until activatePick2DCellsTool first creates it; creation itself does not require a results
     *   layer.
     * @return 2D cell picker, null before activation; borrowed project-owned pointer, no
     *   creation/activation or ownership transfer.
 */
    class MapToolPick2DCells *pick2DCellsTool() const { return mPick2DCellsTool; }

    /*! Free-form 2D-mesh profile-trace tool. Null until first activation.
     * @return Bed-only mesh-profile tool, null before activation; borrowed project-owned pointer,
     *   no creation/activation or ownership transfer.
 */
    class MapToolMeshProfile *meshProfileTool() const { return mMeshProfileTool; }

    /*! Slice QA.2 — per-project registry that tags every loaded
     *  SWMMResultsLayer with a stable identity. Owned by the project
     *  window; lives for the project's lifetime. Consumers (the node
     *  attribute panel today, Slice CB Statistics dashboard tomorrow)
     *  read from this single source of truth so the "which output
     *  produced these stats?" answer is uniform across the GUI.
     *  Never null.
     * @return Borrowed project-owned registry created during construction, maintained by canvas
     *   results-layer add/remove and open/finalize/path signals. Do not delete or retain after
     *   close.
 */
    class openswmmvis::OutputStatsRegistry *statsRegistry() const { return mStatsRegistry; }

    // ── Active analysis layers (results-analysis demarcation) ────────────────
    //
    // A single model can have several loaded result sets. The "active" 1D and
    // 2D results layers are the ones every analysis/visualization tool targets
    // (Comparison plot, Profile plot, Tabular results, color-by-result, the
    // animation transport, 2D cell picking) instead of the old "first results
    // layer found on the canvas" guess. The user chooses them from the two
    // analysis-toolbar combos or the layer-tree "Set as Active Results Layer"
    // context action. State is per-project so each tab keeps its own choice.

    /*! Active 1D results layer for analysis tools (may be null). Defined
     *  out-of-line so the QPointer→T* conversion is only instantiated in the
     *  .cpp (which includes the full layer headers); a forward declaration is
     *  insufficient for QPointer's internal static_cast.
     * @return Borrowed cached 1D analysis layer or null; guarded pointer nulls on destruction.
     *   Getter does not select fallback, check file readiness or rescan canvas membership.
 */
    [[nodiscard]] SWMMResultsLayer *activeResultsLayer() const;

    /*! Active 2D results layer for analysis tools (may be null).
     * @return Borrowed cached 2D analysis layer or null; guarded pointer nulls on destruction.
     *   Getter does not select fallback, check file readiness or rescan canvas membership.
 */
    [[nodiscard]] SWMM2DResultsLayer *active2DResultsLayer() const;

    /*! Set the active 1D results layer. No-op if unchanged. Rejects a layer
     *  not currently on this window's canvas. Passing nullptr clears the
     *  selection (analysis tools then report "no results"). Emits
     *  activeResultsLayerChanged on a real change.
     * @param[in] layer Optional borrowed 1D results layer already present in this canvas; null
     *   clears choice, foreign layer ignored.
     * @note Equality no-op, no ownership/readiness validation/dirty mark. Only caches/emits; no
     *   automatic animation/controller retargeting within this method.
 */
    void setActiveResultsLayer(SWMMResultsLayer *layer);

    /*! Set the active 2D results layer. Same contract as the 1D setter.
     * @param[in] layer Optional borrowed 2D results layer already present in this canvas; null
     *   clears choice, foreign layer ignored.
     * @note Equality no-op, no ownership/readiness validation/dirty mark. 2D also retargets
     *   existing cell picker and refreshes highlight before emitting.
 */
    void setActive2DResultsLayer(SWMM2DResultsLayer *layer);

    /*! 2D results restore entries stashed by ProjectSerializer::applySession
     *  (the .h5 open is asynchronous and runs after the sidecar applies).
     *  Consumed once by the 2D auto-load; entries carry absolute .h5 paths
     *  plus the persisted layer settings and sublayer styles.
     * @param[in] entries Copied session entries, no schema/path/readiness validation here.
     * @note Replaces cache only; no IO, layer creation, signals or dirty mark.
 */
    void setPending2DResultsRestore(const QJsonArray &entries)
    {
        mPending2DResultsRestore = entries;
    }
    /**
     * @return Copy of unconsumed restoration cache; does not clear or start loading.
     */
    [[nodiscard]] QJsonArray pending2DResultsRestore() const { return mPending2DResultsRestore; }
    /**
     * @brief Empty restoration cache without applying/discarding already loaded layers or notifying
     *   observers.
     */
    void clearPending2DResultsRestore() { mPending2DResultsRestore = {}; }

    /*! Maps each tool pointer to a stable action-object-name key so the
     *  main window can sync toolbar checked states via activeToolChanged.
     * @return Owning hash of borrowed tool pointers to action object names. Lazy null keys collide
     *   in QHash, so only one null-key action can survive before tools exist; nonnull keys track
     *   constructed instances. No creation/activation or ownership transfer.
 */
    QHash<class OpenSWMMVisMapTool *, QString> toolActionKeys() const;
    /**
     * @brief Select preconstructed junction insertion tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateAddJunctionTool();
    /**
     * @brief Select preconstructed virtual junction conduit-split insertion tool on required
     *   canvas; no toggle, immediate geometry write or edit-session activation in this wrapper.
     *   Tool handles subsequent gestures/eligibility.
     */
    void activateAddVirtualJunctionTool();
    /**
     * @brief Select preconstructed inlet junction street-split insertion tool on required canvas;
     *   no toggle, immediate geometry write or edit-session activation in this wrapper. Tool
     *   handles subsequent gestures/eligibility.
     */
    void activateAddInletJunctionTool();
    /**
     * @brief Select preconstructed outfall insertion tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateAddOutfallTool();
    /**
     * @brief Select preconstructed storage insertion tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateAddStorageTool();
    /**
     * @brief Require model engine, offer controls whose native type is 0/1/3/4/8, then configure
     *   preconstructed storage-based LID insertion tool and activate after accepted choice.
     * @note Empty list shows information; cancel no-op. No insertion/dirty/session change until
     *   subsequent tool action. Constructor nodeAdded callback can offer underdrain/overflow
     *   destinations and push undoable links for created LID. No lifecycle guard beyond engine
     *   presence here.
     */
    void activateAddLidTool();
    /**
     * @brief Select preconstructed divider insertion tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateAddDividerTool();
    /**
     * @brief Select preconstructed conduit insertion tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateAddConduitTool();
    /**
     * @brief Select preconstructed pump insertion tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateAddPumpTool();
    /**
     * @brief Select preconstructed orifice insertion tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateAddOrificeTool();
    /**
     * @brief Select preconstructed weir insertion tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateAddWeirTool();
    /**
     * @brief Select preconstructed outlet insertion tool on required canvas; no toggle, immediate
     *   geometry write or edit-session activation in this wrapper. Tool handles subsequent
     *   gestures/eligibility.
     */
    void activateAddOutletTool();
    /**
     * @brief Select preconstructed rain-gage insertion tool on required canvas; no toggle,
     *   immediate geometry write or edit-session activation in this wrapper. Tool handles
     *   subsequent gestures/eligibility.
     */
    void activateAddGageTool();
    /**
     * @brief Select preconstructed subcatchment polygon insertion tool on required canvas; no
     *   toggle, immediate geometry write or edit-session activation in this wrapper. Tool handles
     *   subsequent gestures/eligibility.
     */
    void activateAddSubcatchmentTool();
    /**
     * @brief Ensure annotation layer, run live TextLayersDialog, then on acceptance set chosen
     *   layer visible, enable edit session and lazily bind/activate text placement tool.
     * @note Cancel can still leave newly ensured layer or child-dialog edits. Null accepted layer
     *   stops. New text tool connection returns to Select when session ends. No immediate text
     *   placement or rollback of layer-manager edits.
     */
    void activateAddTextTool();
    /**
     * @brief Delegate current canvas full-extent fitting (normal view-history behavior); no source
     *   geometry/CRS changes. Requires constructed canvas.
     */
    void zoomToFullExtent();

    /*! Annotation layer for user-placed text labels. Lazily added to the
     *  canvas on first text placement; non-null after that point.
     * @return Cached borrowed annotation layer, initially null; getter does not verify current
     *   canvas membership or create one. Raw cache requires external layer lifetime discipline.
 */
    OpenSWMMVisAnnotationLayer *annotationLayer() const { return mAnnotationLayer; }

    /*! Get-or-create accessor used by ProjectSerializer to restore
     *  annotations before any tool has been activated.
     * @return Borrowed cached annotation layer if still on canvas, otherwise first annotation
     *   canvas layer, otherwise new canvas-parented Text layer with copied canvas SRS.
     * @note Creation adds to canvas and constructor annotation tracking marks project dirty. Does
     *   not activate placement tool or own existing layer through this accessor.
 */
    OpenSWMMVisAnnotationLayer *ensureAnnotationLayer();

    /** Auto-length recalculates conduit length from polyline on every
     *  endpoint / vertex edit. Per-project, persisted to QSettings.
     * @return Cached conduit auto-length policy mirrored to canvas dynamic property, not a
     *   recalculation of current lengths.
 */
    bool isAutoLengthEnabled() const { return mAutoLengthEnabled; }
    /**
     * @param[in] enabled New auto-length policy; equality is a no-op.
     * @note Update cache/canvas property and GLOBAL persisted preference default, emit
     *   autoLengthChanged. Does not dirty project or recalculate existing conduit lengths
     *   immediately.
     */
    void setAutoLengthEnabled(bool enabled);

    /** Absolute path of the model this window holds, or empty when untitled.
     *  The window title is only the base name, so two models of the same name
     *  from different folders are indistinguishable by it — this is what the
     *  tab's tooltip shows to tell them apart.
     * @return Copied model-layer path, or empty for untitled/missing layer; normally absolute as
     *   supplied by caller, no canonicalization here.
 */
    [[nodiscard]] QString modelFilePath() const;

    /** Engine version selector (e.g., "5.3.0", "6.0.0", "6.0.0-alpha.1"). Per-project, persisted to project file.
     * @return Copied engine-mode metadata string, not queried engine build/version or proof a
     *   different engine library is loaded.
 */
    QString engineVersion() const { return mEngineVersion; }
    /** Restore saved metadata with markDirty=false; user changes mark dirty by default.
     * @param[in] version Arbitrary copied version metadata, no validation or engine recreation.
     * @param[in] markDirty Mark project dirty on changed metadata when true; false for restoration.
     * @note Equality no-op; no dedicated version-changed signal or immediate engine option write.
 */
    void setEngineVersion(const QString &version, bool markDirty = true);

    /** Per-project calibration bindings; callers mark accepted edits dirty. */
    QJsonArray calibrationEntries() const { return mCalibrationEntries; }
    void setCalibrationEntries(const QJsonArray &entries) { mCalibrationEntries = entries; }

    /** Rich-text notes mirrored to the engine's [TITLE] section as plain text.
     *  HTML form is preserved in the .oswp sidecar; the engine only ever sees
     *  the flattened plain text.
     * @return Copied rich-text notes cache; getter does not read current engine TITLE or convert
     *   HTML.
 */
    QString notesHtml() const { return mNotesHtml; }
    /**
     * @param[in] html Copied rich-text cache; no sanitization or immediate engine TITLE write.
     * @note No dirty mark/notification here; caller/save serializer handles persistence and
     *   plain-text mirroring.
     */
    void setNotesHtml(const QString &html) { mNotesHtml = html; }

    /** Channel burn-in settings for this project
     *  (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md D-H). The Generate 2D
     *  Mesh dialog is created fresh each time it opens, so the settings have
     *  to live somewhere that outlives it; the .oswp round-trips them from
     *  here. This is the first mesh-dialog state to persist at all — the quad
     *  and patch groups remain unpersisted.
     * @return Borrowed const reference to cached channel burn settings, valid until
     *   replacement/project destruction. No validation, IO or fresh dialog read.
 */
    [[nodiscard]] const mesh::ChannelBurnSettings &channelBurnSettings() const
    {
        return mChannelBurn;
    }
    /**
     * @param[in] s Copied channel burn settings; replaces cache, caller validates and decides when
     *   to adopt.
     * @note No engine/file write, dirty mark or change signal; caller controls
     *   persistence/notification.
     */
    void setChannelBurnSettings(const mesh::ChannelBurnSettings &s) { mChannelBurn = s; }
    /** Last mesh-dialog options, including canceled/failed attempts. Values
     *  are separate from adopted mesh artifacts and saved in the .oswp.
     * @return Borrowed const reference to cached mesh-generation dialog options, valid until
     *   replacement/project destruction. No validation, IO or fresh dialog read.
 */
    const QJsonObject &meshGenerationOptions() const { return mMeshGenerationOptions; }
    /**
     * @param[in] options Copied mesh-generation dialog options; replaces cache, caller validates
     *   and decides when to adopt.
     * @note No engine/file write, dirty mark or change signal; caller controls
     *   persistence/notification.
     */
    void setMeshGenerationOptions(const QJsonObject &options) { mMeshGenerationOptions = options; }

    /** Last successfully adopted GIS corridor recipe. Dialog drafts remain
     *  local until generation succeeds; project Save publishes this state.
     * @return Borrowed const reference to cached adopted corridor recipe, valid until
     *   replacement/project destruction. No validation, IO or fresh dialog read.
 */
    [[nodiscard]] const QVector<mesh::CorridorSource> &corridorSources() const
    {
        return mCorridorSources;
    }
    /**
     * @param[in] sources Copied adopted corridor recipe; replaces cache, caller validates and
     *   decides when to adopt.
     * @note No engine/file write, dirty mark or change signal; caller controls
     *   persistence/notification.
     */
    void setCorridorSources(const QVector<mesh::CorridorSource> &sources)
    {
        mCorridorSources = sources;
    }
    /**
     * @return Copied recipe restoration error; nonempty gates saveAs before warning-list reset.
     *   Does not retry loading.
     */
    [[nodiscard]] QString corridorRecipeLoadError() const { return mCorridorRecipeLoadError; }
    /**
     * @param[in] error Copied error text; empty clears save gate.
     * @note No dirty mark, recipe mutation, validation or notification.
     */
    void setCorridorRecipeLoadError(const QString &error) { mCorridorRecipeLoadError = error; }

    // ── Terrain editing ──────────────────────────────────────────────────────

    /*! Returns the file path of the active terrain raster (empty if none).
     * @return Copied file path of raw active terrain, empty for null; does not check canvas
     *   membership/file existence.
 */
    [[nodiscard]] QString activeTerrainLayerPath() const;

    /*! Direct (non-owning) accessor to the active terrain raster.  Used by
     *  Slice BC's profile plot when the user wants the ground line to
     *  follow a DEM rather than the rim formula (invert + maxDepth).
     * @return Borrowed raw active raster; no ownership/QPointer/deletion guard or canvas membership
     *   validation here. External owners must clear before deleting raster.
 */
    [[nodiscard]] GISRasterLayer *activeTerrain() const { return mActiveTerrain; }

    /*! Multiplier that converts a raw raster sample (in the DEM's native
     *  vertical unit) into the model's vertical unit.  Retains its cached conversion even when no
     *   terrain is active. Stays in sync with FLOW_UNITS changes via
     *  `setTerrainVerticalUnit` + the unitsChanged handler.
     * @return Cached raw raster Z to model vertical unit multiplier, including when terrain is
     *   null. Computed from exact ft token and project SI/customary mode, no sample read.
 */
    [[nodiscard]] double terrainVertFactor() const { return mTerrainVertFactor; }

    /*! Signed node invert offset relative to terrain Z (model vertical units).
     * @return Cached signed node invert offset added after vertical conversion, in model ft/m. No
     *   finite/range validation or engine query.
 */
    [[nodiscard]] double terrainNodeOffset() const { return mTerrainNodeOffset; }

    /*! Signed link endpoint invert offset relative to terrain Z.
     * @return Cached signed link endpoint invert offset added after vertical conversion, in model
     *   ft/m. No finite/range validation or engine query.
 */
    [[nodiscard]] double terrainLinkOffset() const { return mTerrainLinkOffset; }

    /*!
     * \brief Sets the active terrain raster and propagates it to all add-node
     *        and add-link tools owned by this window.
     * \param layer  Raster layer to sample; nullptr = no terrain assistance.
     * @param[in] markDirty Mark project dirty on pointer change when true; false used during
     *   restoration.
     * @note Raw borrowed pointer retained without canvas/type/readiness/deletion validation. Equal
     *   pointer no-op; changed pointer propagates cached offsets/factor to generic node/LID and
     *   link tools, clears Z display only for null and emits activeTerrainChanged. Does not
     *   auto-detect/recompute vertical units; separate setter required. Virtual/inlet tools not
     *   included in propagation loops.
     */
    void setActiveTerrain(GISRasterLayer *layer, bool markDirty = true);

    /*! Sets node offset and forwards it to all add-node tools.
     * @param[in] offset Signed native model vertical ft/m added after raster Z conversion; no
     *   clamping/finite check.
     * @note Exact equality no-op, otherwise cache/propagate to node/LID insertion tools and mark
     *   dirty. Does not alter existing engine geometry or emit activeTerrainChanged.
 */
    void setTerrainNodeOffset(double offset);

    /*! Sets link offset and forwards it to all add-link tools.
     * @param[in] offset Signed native model vertical ft/m added after raster Z conversion; no
     *   clamping/finite check.
     * @note Exact equality no-op, otherwise cache/propagate to link insertion tools and mark dirty.
     *   Does not alter existing engine geometry or emit activeTerrainChanged.
 */
    void setTerrainLinkOffset(double offset);

    /*! Vertical unit of the active terrain raster ("m" or "ft").
     * @return Copied cached authored unit token; may be arbitrary string because setter normalizes
     *   only empty. Exact ft is treated as feet, all other strings as metres.
 */
    [[nodiscard]] QString terrainVerticalUnit() const { return mTerrainVertUnit; }

    /*!
     * \brief Sets the terrain raster's vertical unit and updates the conversion
     *        factor applied when computing node/link invert elevations.
     * @param[in] unit Copied raster vertical-unit token; empty becomes m, exact case-sensitive ft
     *   uses 0.3048 metres, every other token treated as metres.
     * @param[in] markDirty On changed token/factor mark project dirty when true; false for
     *   restoration.
     * @note Recompute even unchanged token when project SI mode changes. Factor = raster-to-metres
     *   / model-to-metres; offset remains model ft/m. Changed token/factor updates canvas unit
     *   label, generic node/LID/link tools and emits activeTerrainChanged even if raster null. No
     *   engine coordinates changed.
     */
    void setTerrainVerticalUnit(const QString &unit, bool markDirty = true);

    /*!
     * \brief Restores terrain state from a persisted .oswp session.
     *        Called by ProjectSerializer::applySession after all layers are loaded.
     * \param absoluteLayerPath  Absolute path of the saved raster (may be empty).
     * \param nodeOffset         Saved node invert offset.
     * \param linkOffset         Saved link invert offset.
     * \param vertUnit           Saved vertical unit ("m" or "ft").
     * @note Copy offsets directly, choose first canvas raster with EXACT saved path, then set
     *   terrain and supplied/detected vertical unit with markDirty=false. No path
     *   canonicalization/load, offsets validation or dirty reset.
     * @note If both pointer and unit/factor are already identical, setters return early and newly
     *   copied offsets are not repropagated to tools; intended initial session restoration. Missing
     *   raster clears terrain, default unit m unless explicitly supplied.
     */
    void restoreTerrainState(const QString &absoluteLayerPath, double nodeOffset, double linkOffset,
                             const QString &vertUnit = QString());

signals:
    /**
     * @brief Relay of model-layer loaded signal; may precede finishModelLoad CRS/selection/view
     *   completion. Use modelLoadFinished for async settled outcome.
     */
    void modelLoaded();
    /**
     * @param[in] msg Copied/borrowed signal diagnostic text, no structured code.
     * @brief Relay of model-layer error, not emitted for every window-level
     *   refusal/recovery/adoption failure.
     */
    void modelLoadError(const QString &msg);

    /*! Completion signal for loadModelAsync(): fired once per call while its
     *  window remains open (closed/destroyed owners discard their result),
     *  on the GUI thread, after the model is fully adopted (success) or the
     *  open failed. Warnings/errors carry the same diagnostics the sync
     *  loadModel() returns via out-params.
     * @param[in] ok True after complete successful adoption/post-load setup.
     * @param[in] warnings Borrowed diagnostic list valid for signal call; copy for later use.
     * @param[in] errors Borrowed failure diagnostics, can be empty for some cancellation paths.
     * @brief Asynchronous request completion on GUI thread while owner survives; callbacks may
     *   immediately retry after failure.
 */
    void modelLoadFinished(bool ok, const QList<QString> &warnings, const QList<QString> &errors);
    /*! Fired after a SUCCESSFUL save whose engine write emitted warnings —
     *  the save-time analogue of the open path's warning routing. The list is
     *  the delta across this write only. Data-loss notices (embedded
     *  [REACTION_*] dropped) arrive here; SWMMVis routes every entry to the
     *  log panel and escalates the data-loss family to a modal.
     * @param[in] warnings Borrowed writer delta plus prepared-project validation warnings; copy if
     *   retained.
     * @brief Successful save settled warning list, never emitted merely because intermediate writer
     *   succeeded.
 */
    void saveCompletedWithEngineWarnings(const QStringList &warnings);
    /*! Completion signal for importMeshFileAsync(): fired exactly once per
     *  call while the window survives, on the GUI thread. \p meshPath is the
     *  intended destination (published only by Save), and is empty on failure.
     *  An untitled project's first Save As may relocate that destination.
     *  \p message is an error or summary plus any reader warning.
     * @param[in] ok True only when current prepared layer adopted.
     * @param[in] message Error/summary and optional parse warning, plain display text.
     * @param[in] meshPath Intended destination, empty on failure; not yet published, can rebase at
     *   Save As.
     * @brief Import outcome on GUI thread, potentially synchronously for preflight refusal; success
     *   is prepared layer, not published disk files.
 */
    void meshImportFinished(bool ok, const QString &message, const QString &meshPath);

    /**
     * @param[in] dirty New unsaved-cache state.
     * @brief Dirty-cache transition after title update, not every engine/mesh edit.
     */
    void hasChangesChanged(bool dirty);
    /**
     * @param[in] active New session flag.
     * @brief Edit-session cache transition; does not certify engine lifecycle permits editing.
     */
    void editSessionChanged(bool active);
    /**
     * @param[in] elevation True ELEVATION, false DEPTH interpretation.
     * @brief Cached offset-mode transition, even if engine option setter rejected.
     */
    void offsetModeChanged(bool elevation);
    /**
     * @param[in] enabled New automatic conduit-length policy.
     * @brief Auto-length cache/global preference transition; existing lengths not changed here.
     */
    void autoLengthChanged(bool enabled);

    /*! Fires when the active 1D / 2D results layer changes (including to
     *  null). The main window re-points the shared AnimationController, the
     *  analysis-toolbar combos, and the layer-tree check-state at these.
     * @param[in] layer Borrowed selected layer or null.
     * @brief Borrowed active 1D layer selection transition; no ownership/readiness promise.
 */
    void activeResultsLayerChanged(SWMMResultsLayer *layer);
    /**
     * @param[in] layer Borrowed selected layer or null.
     * @brief Borrowed active 2D selection after picker retarget/highlight refresh.
     */
    void active2DResultsLayerChanged(SWMM2DResultsLayer *layer);

    /*! Slice CF.3 — forwards MapToolPick2DCells::cellsPicked up to the
     *  main window so it can open the Comparison Plot Dialog.
     * @param[in] layer Borrowed result source from tool, copy guarded pointer if retaining.
     * @param[in] triIdxList Borrowed zero-based cell/triangle indices as supplied by picker.
     * @param[in] attrs Borrowed selected result descriptors, copied by receivers as needed.
     * @brief Forwarded cell-analysis selection from picker; no ownership transfer.
 */
    void pick2DCellsPicked(class SWMM2DResultsLayer *layer, const QVector<int> &triIdxList,
                           const QVector<openswmmvis::plot::ResultDescriptor> &attrs);

    /*! Forwards MapToolMeshProfile::profilePathTraced up to the main window
     *  so it can open the MeshProfilePlotDialog. The polyline is in scene
     *  coords (sx = mapX, sy = -mapY).
     *
     *  Slice US.A1 — meshProfileTraced is the MESH-TOOLBAR (bed-only) channel;
     *  analysisMeshProfileTraced is the ANALYSIS (ground+depth+envelope)
     *  channel. Same polyline payload; different dialog target.
     * @param[in] scenePolyline Borrowed trace points in canvas scene coordinates: x=mapX, y=-mapY.
     * @brief Forwarded bed-only profile geometry; receiving window chooses data source.
 */
    void meshProfileTraced(const QVector<QPointF> &scenePolyline);
    /**
     * @param[in] scenePolyline Borrowed trace in canvas scene coordinates: x=mapX, y=-mapY.
     * @brief Forwarded analysis-profile geometry; receiving window selects active results.
     */
    void analysisMeshProfileTraced(const QVector<QPointF> &scenePolyline);

    /*! The TERRAIN-TOOLBAR channel — same polyline payload, but the main window
     *  routes it to a RasterProfilePlotDialog sampling the active DEM raster.
     * @param[in] scenePolyline Borrowed trace in canvas scene coordinates: x=mapX, y=-mapY.
     * @brief Forwarded terrain-profile geometry; receiving window selects active raster.
 */
    void terrainProfileTraced(const QVector<QPointF> &scenePolyline);

    /*! Forwards MapToolMeshSelectEdge::plotEdgeFluxRequested up to the main
     *  window so it can open the comparison plot with the edge's flow OR flux
     *  series (carried by `attr`).
     * @param[in] mesh Borrowed mesh source.
     * @param[in] triIdx Zero-based host cell/triangle index.
     * @param[in] edgeLocal Zero-based local edge slot, triangle/quad topology as forwarded by tool.
     * @param[in] attr Requested edge flow or flux plot attribute, forwarded unchanged.
     * @brief Forwarded edge-analysis request, no read/compute performed here.
 */
    void meshEdgeFluxRequested(class SWMM2DMeshLayer *mesh, int triIdx, int edgeLocal,
                               openswmmvis::plot::PlotAttribute attr);

    /*! Forwards MapToolMeshSelectEdge::statusMessageChanged (Ctrl-click
     *  boundary path picking hints) up to the main window's status bar.
     * @param[in] message Borrowed user-facing status string.
     * @brief Forwarded edge-picker status hint, not necessarily an error.
 */
    void meshEdgeStatusMessage(const QString &message);

    /*! Forwards MapToolMeshSelectVertex::plotVertexSeriesRequested up to the
     *  main window so it can plot the chosen interpolated attributes
     *  (depth / HGL) for the vertices.
     * @param[in] mesh Borrowed mesh source.
     * @param[in] vertexIdxList Borrowed zero-based vertex indices.
     * @param[in] attrs Borrowed requested interpolated plot attributes.
     * @brief Forwarded vertex-analysis request, no sampling here.
 */
    void meshVertexSeriesRequested(class SWMM2DMeshLayer *mesh, const QVector<int> &vertexIdxList,
                                   const QVector<openswmmvis::plot::PlotAttribute> &attrs);

    /*! Slice BC — fires whenever the active terrain raster changes
     *  (set / cleared / swapped) or its vertical-unit conversion
     *  factor is updated.  The profile-plot dialog listens so the
     *  ground line stays in sync with the toolbar's terrain selection.
     * @param[in] newTerrain Borrowed raw raster or null; same pointer may reemit after unit change.
     * @brief Terrain pointer or vertical conversion change; null permitted, no ownership transfer.
 */
    void activeTerrainChanged(GISRasterLayer *newTerrain);

    /*! Fires once during closeEvent, AFTER the user-prompt save / cancel
     *  decision is committed but BEFORE the QMdiSubWindow teardown has
     *  begun.  All owned QObjects (model layer, results layers, canvas,
     *  etc.) are still alive and safe to dereference.  External windows
     *  that hold raw back-pointers to this project (e.g. ProfilePlotDialog)
     *  connect here so they can drop their references gracefully — using
     *  `QObject::destroyed` instead is unsafe because by the time it
     *  fires the children are already gone.
     * @note Emitted after committed close prompt with mClosing true, or once from destructor if no
     *   committed close occurred. Owned widget/services still live during direct handlers; callers
     *   must drop references then. Does not certify disk save (Discard/direct destruction allowed).
 */
    void aboutToClose();

protected:
    /**
     * @param[in] event Required borrowed close event; ignored for reentrant prompt, Cancel or
     *   failed/canceled Save.
     * @brief Prompt whenever dirty or untitled, perform requested save, then set closing flag/emit
     *   aboutToClose and delegate Qt close.
     * @note Untitled Save As requires enclosing SWMMVis main window; missing host keeps open. Modal
     *   prompts run nested events; reentrant close ignored. Direct destruction bypasses prompt.
     *   Accepted close triggers WA_DeleteOnClose.
     */
    void closeEvent(QCloseEvent *event) override;
    /**
     * @param[in] event Borrowed Qt change event forwarded unchanged to QMdiSubWindow.
     * @brief Preserve base window-state behavior, including minimize state used internally by MDI
     *   tab visibility. No custom blocking or source updates.
     */
    void changeEvent(QEvent *event) override;
    /**
     * @param[in] watched Borrowed target; only canvas resize with visible measure panel has extra
     *   work.
     * @param[in] event Required borrowed Qt event; forwarded unchanged to base.
     * @return Base QMdiSubWindow filter result; side effect only repositions visible measurement
     *   overlay.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class TestMeshImport;
    friend class TestAsyncLoad;
    // Empty in production; copied into an async job by deterministic failure tests.
    std::function<void()> mAsyncLoadWorkerTestHook;
    /**
     * @brief Set Untitled or model complete base name plus literal space-asterisk when dirty; does
     *   not set Qt windowModified or emit hasChangesChanged.
     */
    void updateWindowTitle();
    /**
     * @brief Center measurement sizeHint horizontally at canvas bottom with eight logical-pixel
     *   margin and raise it. Missing canvas/panel no-op; no clamping to small viewport.
     */
    void repositionMeasurePanel();
    /**
     * @brief Replace distance/area names in enum order and select current tool unit under signal
     *   block, without changing tool mode/unit. Missing combo/tool no-op.
     */
    void updateMeasureUnitCombo();

    /*! Post-open completion shared by loadModel() and loadModelAsync():
     *  engine option sync, selection bridge, CRS adoption (may prompt),
     *  zoom-to-extent. GUI thread only.
     * @param[in] warnings Forwarded diagnostic collection; this helper does not currently add
     *   warnings.
     * @param[in] errors Appended when CRS selection aborts or guarded context changes during modal
     *   interaction.
     * @return True after post-load setup, false for missing/closing context or canceled/stale CRS
     *   interaction. False does not close an already adopted engine.
     * @note Clears dirty cache/title directly without hasChangesChanged, exposes model, syncs
     *   units/offsets and installs typed selection bridge. First unknown Untitled local CRS prompts
     *   until picked/local shortcut/abort; geographic model keeps projected canvas here. Refits
     *   immediately or retries 20 times at 50 ms if unsized. Repeated calls install additional
     *   bridges; intended once per adoption.
 */
    bool finishModelLoad(QList<QString> &warnings, QList<QString> &errors);

    /*! Mirror the current MeshCell selection onto the active 2D results layer
     *  (and clear it off all other 2D results layers). Driven by selection
     *  changes and active-2D-layer changes (results-analysis demarcation).
     * @note Parse nonnegative MeshCell indices from whole selection, deduplicate and apply to
     *   active results only, clearing all others. Parsed layer key is currently ignored, so caller
     *   selection must correspond to active mesh. No explicit bounds check here; results layer
     *   handles indices.
 */
    void refreshActive2DCellHighlight();

    OpenSWMMVisWorkspace *mWorkspace = nullptr;
    MapCanvas *mCanvas = nullptr;
    SWMMModelLayer *mModelLayer = nullptr;
    UnitSystem *mUnits = nullptr;
    SelectionManager *mSelectionManager = nullptr;
    bool mCanvasCRSAdopted = false; // true after first successful loadModel
    bool mHasChanges = false;
    bool mEditSessionActive = false;
    bool mElevationOffsetMode = false;      // OPTIONS LINK_OFFSETS = ELEVATION
    bool mUntitled = false;                 // Slice Y — never saved
    bool mClosePromptActive = false;        // re-entrancy guard for closeEvent's prompt
    bool mClosing = false;                  // see isClosing()
    bool mAsyncLoadInProgress = false;      // GUI-thread request/adoption guard
    quint64 mMeshImportSerial = 0;          // only the latest import may be adopted
    QStringList mLastSaveWarnings;          // delta across the last successful engine write
    QString mEngineVersion = "6.0.0";       // Default to newest version
    QString mNotesHtml;                     // [TITLE] notes (rich HTML)
    mesh::ChannelBurnSettings mChannelBurn; // Channel burn-in tab state
    QJsonArray mCalibrationEntries;
    QJsonObject mMeshGenerationOptions;
    QVector<mesh::CorridorSource> mCorridorSources; // adopted GIS corridor recipe
    QString mCorridorRecipeLoadError;               // prevents overwriting an unsupported recipe
    QJsonArray mPending2DResultsRestore;            // .oswp 2D results entries

    OpenSWMMVisMapToolPan *mPanTool = nullptr;
    OpenSWMMVisMapToolZoom *mZoomInTool = nullptr;
    OpenSWMMVisMapToolZoom *mZoomOutTool = nullptr;
    OpenSWMMVisMapToolSelect *mSelectTool = nullptr;
    OpenSWMMVisMapToolSelectPolygon *mSelectPolygonTool = nullptr;
    OpenSWMMVisMapToolMeasure *mMeasureTool = nullptr;
    class OpenSWMMVisMapToolSelectProfile *mSelectProfileTool = nullptr;
    OpenSWMMVisMapToolAddNode *mAddJunctionTool = nullptr;
    OpenSWMMVisMapToolAddVirtualNode *mAddVirtualJunctionTool = nullptr;
    class OpenSWMMVisMapToolAddInletNode *mAddInletJunctionTool = nullptr;
    OpenSWMMVisMapToolAddNode *mAddOutfallTool = nullptr;
    OpenSWMMVisMapToolAddNode *mAddStorageTool = nullptr;
    OpenSWMMVisMapToolAddNode *mAddLidTool = nullptr;
    OpenSWMMVisMapToolAddNode *mAddDividerTool = nullptr;
    OpenSWMMVisMapToolAddLink *mAddConduitTool = nullptr;
    OpenSWMMVisMapToolAddLink *mAddPumpTool = nullptr;
    OpenSWMMVisMapToolAddLink *mAddOrificeTool = nullptr;
    OpenSWMMVisMapToolAddLink *mAddWeirTool = nullptr;
    OpenSWMMVisMapToolAddLink *mAddOutletTool = nullptr;
    OpenSWMMVisMapToolAddGage *mAddGageTool = nullptr;
    OpenSWMMVisMapToolAddSubcatchment *mAddSubcatchTool = nullptr;
    OpenSWMMVisMapToolAddText *mAddTextTool = nullptr;
    OpenSWMMVisAnnotationLayer *mAnnotationLayer = nullptr; ///< lazy; created on first text op
    class MapToolPick2DCells *mPick2DCellsTool = nullptr;   ///< CF.3 — lazy
    class MapToolMeshProfile *mMeshProfileTool =
        nullptr; ///< mesh-toolbar (bed-only) profile-trace — lazy
    class MapToolMeshProfile *mAnalysisMeshProfileTool =
        nullptr; ///< US.A1 — analysis (ground+depth) profile-trace — lazy
    class MapToolMeshProfile *mTerrainProfileTool =
        nullptr; ///< terrain-toolbar (DEM raster) profile-trace — lazy
    class MapToolMeshSelectVertex *mMeshSelectVertexTool = nullptr; ///< §V.VB — lazy
    class MapToolMeshSelectEdge *mMeshSelectEdgeTool = nullptr;     ///< §V.VB — lazy

    // Measure tool floating panel (child of mCanvas)
    QFrame *mMeasurePanel = nullptr;
    QComboBox *mMeasureModeCombo = nullptr;
    QComboBox *mMeasureUnitCombo = nullptr;
    QLabel *mMeasureTotalLabel = nullptr;

    bool mAutoLengthEnabled = false;

    // Slice QA.2 — output identity registry. Owned via QObject parent
    // (constructed with `this` as parent in the .cpp constructor) so
    // it follows the project window's lifetime.
    openswmmvis::OutputStatsRegistry *mStatsRegistry = nullptr;

    // Active analysis layers (results-analysis demarcation). QPointer so a
    // layer destroyed/removed from the canvas auto-nulls the active pointer
    // without a dangling reference.
    QPointer<SWMMResultsLayer> mActiveResultsLayer;
    QPointer<SWMM2DResultsLayer> mActive2DResultsLayer;

    // Terrain editing state (per-project, persisted to .oswp)
    GISRasterLayer *mActiveTerrain = nullptr;
    double mTerrainNodeOffset = 0.0;
    double mTerrainLinkOffset = 0.0;
    QString mTerrainVertUnit = QStringLiteral("m"); // raster vertical unit
    double mTerrainVertFactor = 1.0;                // rasterUnit → modelUnit
};

#endif // SWMMVISPROJECTWINDOW_H
