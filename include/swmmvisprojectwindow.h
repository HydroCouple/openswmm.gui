/*!
 * \file   swmmvisprojectwindow.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MDI sub-window that hosts a single SWMM model session.
 * Each opened .inp file gets its own window with a dedicated MapCanvas and tools.
 */
#ifndef SWMMVISPROJECTWINDOW_H
#define SWMMVISPROJECTWINDOW_H

#include <QHash>
#include <QJsonArray>
#include <QMdiSubWindow>
#include <QList>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QVector>

#include "plot/plotattribute.h"   // PlotAttribute (edge flow vs flux relay)
#include "layers/swmmmodellayer.h"  // SWMMModelLayer::NewProjectSpec (nested)

class OpenSWMMVisWorkspace;
class MapCanvas;
class SWMMModelLayer;
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

namespace openswmmvis { class OutputStatsRegistry; }    // Slice QA.2

/**
 * @brief MDI sub-window owning a MapCanvas, SWMMModelLayer, and map tools.
 *
 * One instance is created per opened .inp file.
 */
class SWMMVisProjectWindow : public QMdiSubWindow
{
    Q_OBJECT

public:
    explicit SWMMVisProjectWindow(
        OpenSWMMVisWorkspace *workspace,
        const QString &filePath = QString(),
        QWidget *parent = nullptr
    );
    
    ~SWMMVisProjectWindow() override;

    MapCanvas        *canvas()           const;
    SWMMModelLayer   *modelLayer()       const;
    UnitSystem       *unitSystem()       const;
    SelectionManager *selectionManager() const;

    /** Offset mode (LINK_OFFSETS option). True = ELEVATION, false = DEPTH. */
    bool isElevationOffsetMode() const { return mElevationOffsetMode; }
    void setElevationOffsetMode(bool elevation);

    /*! Convert all link offsets between Depth and Elevation conventions
     *  (legacy UpdateOffsets → ComputeDepth/ElevationOffsets parity) and mark
     *  the project dirty. \param toElevation true → to Elevation, false → to
     *  Depth. */
    void convertLinkOffsets(bool toElevation);

    /*! Re-read LINK_OFFSETS from the engine into the cached
     *  mElevationOffsetMode flag. Used by the main-window listener for
     *  SWMMModelLayer::optionsChanged so the status-bar checkbox
     *  refreshes after a Simulation Options Apply. */
    void reloadElevationOffsetModeFromEngine();

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
     */
    void attachMeshLayer(class SWMM2DMeshLayer *meshLayer, bool pristine = false);

    /*!
     * \brief Browse-and-load an existing OpenSWMM 2D mesh (.2dm) into this
     *        project.
     *
     * Until now a .2dm could only reach a model by already sitting next to its
     * .inp (Simulation Options → Mesh only lists siblings of the model file).
     * This stages a mesh from anywhere on disk: the file is copied next to the
     * .inp when it lives elsewhere — so the saved `[2D_MESH_FILE]` reference
     * stays relative and the project remains portable — then parsed and built
     * on a worker thread and added to the canvas as the ACTIVE external mesh.
     * That is the layer the save path retargets `[2D_MESH_FILE]` at, so the
     * import survives save → reopen.
     *
     * Existing mesh layers are kept (merely deactivated); a layer already
     * reading the destination file is replaced rather than stacked, matching
     * the mesh-generation path.
     *
     * Asynchronous — the outcome arrives via meshImportFinished().
     */
    void importMeshFileAsync(const QString &srcPath);

    /*!
     * \brief Non-blocking variant of loadModel().
     *
     * Runs the engine create+open (the dominant cost — full .inp parse) in a
     * QtConcurrent worker so the GUI event loop stays responsive; completes
     * the load (SoA adoption, CRS resolution, canvas zoom) on the GUI thread
     * and then emits modelLoadFinished(). Safe against the window being
     * closed mid-load.
     *
     * \param progress Optional determinate-progress sink. The worker reports
     *        into the EngineParse / SoaCopy / GeomCache stages; null is the
     *        "nobody is watching" case used by tests and the sync path.
     */
    void loadModelAsync(class OpenProgressModel *progress = nullptr);

    /*!
     * \brief In-memory File → New: build a blank BUILDING-state engine from
     *        \p spec (SWMMModelLayer::adoptNewEngine) and finish the load
     *        synchronously. The window keeps its empty model path until the
     *        first Save As.
     */
    bool initializeBlankModel(const SWMMModelLayer::NewProjectSpec &spec,
                              QList<QString> &warnings,
                              QList<QString> &errors);

    /**
     * @brief Save the model to its current path or to a new path.
     * @return true on success.
     */
    bool save(QString *errorOut = nullptr);
    bool saveAs(const QString &newPath, QString *errorOut = nullptr);

    /*! Engine warnings the LAST successful saveAs() produced (empty when the
     *  save was clean, or none has run). The writer reports through the
     *  engine's warning list — notably "embedded [REACTION_*] sections are
     *  lost from this save" (engine 7d43a1ff) — and saveAs() captures the
     *  delta across the write so callers and tests can see exactly what THIS
     *  save said, not the whole accumulated history. */
    QStringList lastSaveWarnings() const { return mLastSaveWarnings; }

    /** Whether the project has unsaved changes. */
    bool hasChanges() const { return mHasChanges; }

    /** Mark the project dirty/clean (also updates the title). */
    void setHasChanges(bool dirty);

    /** Whether an editing session is active (gate for all geometry mutations). */
    bool isEditSessionActive() const { return mEditSessionActive; }
    void setEditSessionActive(bool active);

    /*! Slice Y — flag a window as a fresh, never-saved project.
     *  Untitled windows skip recent-files registration, force the
     *  title to "Untitled[*]", route Save through Save As, and always
     *  prompt on close (Save As… / Discard / Cancel) — the model lives
     *  only in memory until the first successful Save As clears the flag. */
    bool isUntitled() const { return mUntitled; }
    void markUntitled();

    void activatePanTool();
    void activateZoomInTool();
    void activateZoomOutTool();
    void activateSelectTool();
    void activateSelectByPolygonTool();
    void activateMeasureTool();
    void activateSelectProfileTool();

    /*! Slice CF.3 — activate the Pick 2D Cells tool on this canvas.
     *  Lazy-creates the tool on first call. No-op when no 2D results
     *  layer is loaded. */
    void activatePick2DCellsTool();

    /*! Trace a free-form polyline across the 2D mesh to plot a longitudinal
     *  profile. Lazy-creates the tool; toggles back to Select when already
     *  active (mirrors activateSelectProfileTool).
     *
     *  Slice US.A1 — this is the MESH-TOOLBAR profile tool: bed/terrain only
     *  (emits meshProfileTraced, routed to a results=nullptr dialog). The
     *  Analysis-toolbar water-depth variant is activateAnalysisMeshProfileTool. */
    void activateMeshProfileTool();

    /*! Slice US.A1 — the ANALYSIS-toolbar 2D-surface profile tool: traces the
     *  same polyline but plots ground + animated depth + envelope against the
     *  active 2D results layer (emits analysisMeshProfileTraced). A second
     *  MapToolMeshProfile instance so its checked state tracks actionPlotProfile
     *  while the mesh-toolbar tool tracks actionMeshProfile independently. */
    void activateAnalysisMeshProfileTool();

    /*! The TERRAIN-TOOLBAR profile tool: traces the same polyline but plots a
     *  ground profile sampled from the active DEM raster rather than the 2D
     *  mesh (emits terrainProfileTraced). A third MapToolMeshProfile instance —
     *  the tool is purely geometric — so its checked state tracks
     *  actionTerrainProfile independently of the two mesh profile actions. */
    void activateTerrainProfileTool();

    /*! Slice US.A2 — availability probes for the context-sensitive Plot Profile
     *  dispatcher: is there a 1D SWMM model / a 2D mesh on the canvas? */
    [[nodiscard]] bool hasModelLayer() const;
    [[nodiscard]] bool hasMeshLayer() const;

    /*! Slice §V.VB — activate the Mesh Vertex Select tool. Lazy-creates
     *  on first call. */
    void activateMeshSelectVertexTool();

    /*! Slice §V.VB — activate the Mesh Edge Select tool. Lazy-creates
     *  on first call. */
    void activateMeshSelectEdgeTool();

    /*! Direct access to the Select tool so the main window can wire its
     *  context-menu `plotTimeSeriesRequested` into the same chart
     *  dialog that Object Browser right-clicks use. */
    class OpenSWMMVisMapToolSelect *selectTool() const { return mSelectTool; }

    /*! Same direct-access pattern for Slice BC's profile tool — lets
     *  SWMMVis connect `profilePathSelected` to the ProfilePlotDialog
     *  spawner. */
    class OpenSWMMVisMapToolSelectProfile *selectProfileTool() const
    { return mSelectProfileTool; }

    /*! Virtual-junction insertion tool (click-a-conduit split). Exposed so
     *  SWMMVis can route its statusMessageChanged to the status bar. */
    class OpenSWMMVisMapToolAddVirtualNode *addVirtualJunctionTool() const
    { return mAddVirtualJunctionTool; }

    /*! Slice CF.3 — Pick 2D mesh cells tool (box + lasso). Returns null
     *  until a SWMM2DResultsLayer exists on the canvas (created lazily
     *  on first access via activatePick2DCellsTool). */
    class MapToolPick2DCells *pick2DCellsTool() const { return mPick2DCellsTool; }

    /*! Free-form 2D-mesh profile-trace tool. Null until first activation. */
    class MapToolMeshProfile *meshProfileTool() const { return mMeshProfileTool; }

    /*! Slice QA.2 — per-project registry that tags every loaded
     *  SWMMResultsLayer with a stable identity. Owned by the project
     *  window; lives for the project's lifetime. Consumers (the node
     *  attribute panel today, Slice CB Statistics dashboard tomorrow)
     *  read from this single source of truth so the "which output
     *  produced these stats?" answer is uniform across the GUI.
     *  Never null. */
    class openswmmvis::OutputStatsRegistry *statsRegistry() const
    { return mStatsRegistry; }

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
     *  insufficient for QPointer's internal static_cast. */
    [[nodiscard]] SWMMResultsLayer *activeResultsLayer() const;

    /*! Active 2D results layer for analysis tools (may be null). */
    [[nodiscard]] SWMM2DResultsLayer *active2DResultsLayer() const;

    /*! Set the active 1D results layer. No-op if unchanged. Rejects a layer
     *  not currently on this window's canvas. Passing nullptr clears the
     *  selection (analysis tools then report "no results"). Emits
     *  activeResultsLayerChanged on a real change. */
    void setActiveResultsLayer(SWMMResultsLayer *layer);

    /*! Set the active 2D results layer. Same contract as the 1D setter. */
    void setActive2DResultsLayer(SWMM2DResultsLayer *layer);

    /*! 2D results restore entries stashed by ProjectSerializer::applySession
     *  (the .h5 open is asynchronous and runs after the sidecar applies).
     *  Consumed once by the 2D auto-load; entries carry absolute .h5 paths
     *  plus the persisted layer settings and sublayer styles. */
    void setPending2DResultsRestore(const QJsonArray &entries)
    { mPending2DResultsRestore = entries; }
    [[nodiscard]] QJsonArray pending2DResultsRestore() const
    { return mPending2DResultsRestore; }
    void clearPending2DResultsRestore() { mPending2DResultsRestore = {}; }

    /*! Maps each tool pointer to a stable action-object-name key so the
     *  main window can sync toolbar checked states via activeToolChanged. */
    QHash<class OpenSWMMVisMapTool *, QString> toolActionKeys() const;
    void activateAddJunctionTool();
    void activateAddVirtualJunctionTool();
    void activateAddOutfallTool();
    void activateAddStorageTool();
    void activateAddDividerTool();
    void activateAddConduitTool();
    void activateAddPumpTool();
    void activateAddOrificeTool();
    void activateAddWeirTool();
    void activateAddOutletTool();
    void activateAddGageTool();
    void activateAddSubcatchmentTool();
    void activateAddTextTool();
    void zoomToFullExtent();

    /*! Annotation layer for user-placed text labels. Lazily added to the
     *  canvas on first text placement; non-null after that point. */
    OpenSWMMVisAnnotationLayer *annotationLayer() const { return mAnnotationLayer; }

    /*! Get-or-create accessor used by ProjectSerializer to restore
     *  annotations before any tool has been activated. */
    OpenSWMMVisAnnotationLayer *ensureAnnotationLayer();

    /** Auto-length recalculates conduit length from polyline on every
     *  endpoint / vertex edit. Per-project, persisted to QSettings. */
    bool isAutoLengthEnabled() const { return mAutoLengthEnabled; }
    void setAutoLengthEnabled(bool enabled);

    /** Engine version selector (e.g., "5.3.0", "6.0.0", "6.0.0-alpha.1"). Per-project, persisted to project file. */
    QString engineVersion() const { return mEngineVersion; }
    void setEngineVersion(const QString &version);

    /** Rich-text notes mirrored to the engine's [TITLE] section as plain text.
     *  HTML form is preserved in the .oswp sidecar; the engine only ever sees
     *  the flattened plain text. */
    QString notesHtml() const { return mNotesHtml; }
    void setNotesHtml(const QString &html) { mNotesHtml = html; }

    // ── Terrain editing ──────────────────────────────────────────────────────

    /*! Returns the file path of the active terrain raster (empty if none). */
    [[nodiscard]] QString activeTerrainLayerPath() const;

    /*! Direct (non-owning) accessor to the active terrain raster.  Used by
     *  Slice BC's profile plot when the user wants the ground line to
     *  follow a DEM rather than the rim formula (invert + maxDepth). */
    [[nodiscard]] GISRasterLayer *activeTerrain() const { return mActiveTerrain; }

    /*! Multiplier that converts a raw raster sample (in the DEM's native
     *  vertical unit) into the model's vertical unit.  Returns 1.0 when
     *  no terrain is active.  Stays in sync with FLOW_UNITS changes via
     *  `setTerrainVerticalUnit` + the unitsChanged handler. */
    [[nodiscard]] double terrainVertFactor() const { return mTerrainVertFactor; }

    /*! Signed node invert offset relative to terrain Z (model vertical units). */
    [[nodiscard]] double terrainNodeOffset() const { return mTerrainNodeOffset; }

    /*! Signed link endpoint invert offset relative to terrain Z. */
    [[nodiscard]] double terrainLinkOffset() const { return mTerrainLinkOffset; }

    /*!
     * \brief Sets the active terrain raster and propagates it to all add-node
     *        and add-link tools owned by this window.
     * \param layer  Raster layer to sample; nullptr = no terrain assistance.
     */
    void setActiveTerrain(GISRasterLayer *layer);

    /*! Sets node offset and forwards it to all add-node tools. */
    void setTerrainNodeOffset(double offset);

    /*! Sets link offset and forwards it to all add-link tools. */
    void setTerrainLinkOffset(double offset);

    /*! Vertical unit of the active terrain raster ("m" or "ft"). */
    [[nodiscard]] QString terrainVerticalUnit() const { return mTerrainVertUnit; }

    /*!
     * \brief Sets the terrain raster's vertical unit and updates the conversion
     *        factor applied when computing node/link invert elevations.
     */
    void setTerrainVerticalUnit(const QString &unit);

    /*!
     * \brief Restores terrain state from a persisted .oswp session.
     *        Called by ProjectSerializer::applySession after all layers are loaded.
     * \param absoluteLayerPath  Absolute path of the saved raster (may be empty).
     * \param nodeOffset         Saved node invert offset.
     * \param linkOffset         Saved link invert offset.
     * \param vertUnit           Saved vertical unit ("m" or "ft").
     */
    void restoreTerrainState(const QString &absoluteLayerPath,
                             double nodeOffset,
                             double linkOffset,
                             const QString &vertUnit = QString());

signals:
    void modelLoaded();
    void modelLoadError(const QString &msg);

    /*! Completion signal for loadModelAsync(): fired exactly once per call,
     *  on the GUI thread, after the model is fully adopted (success) or the
     *  open failed. Warnings/errors carry the same diagnostics the sync
     *  loadModel() returns via out-params. */
    void modelLoadFinished(bool ok, const QList<QString> &warnings,
                           const QList<QString> &errors);
    /*! Fired after a SUCCESSFUL save whose engine write emitted warnings —
     *  the save-time analogue of the open path's warning routing. The list is
     *  the delta across this write only. Data-loss notices (embedded
     *  [REACTION_*] dropped) arrive here; SWMMVis routes every entry to the
     *  log panel and escalates the data-loss family to a modal. */
    void saveCompletedWithEngineWarnings(const QStringList &warnings);
    /*! Completion signal for importMeshFileAsync(): fired exactly once per
     *  call, on the GUI thread. \p meshPath is the file the new layer reads
     *  (the copy inside the project folder, when one was made) and is empty
     *  on failure. \p message is user-facing (an error, or a summary plus any
     *  reader warning). */
    void meshImportFinished(bool ok, const QString &message,
                            const QString &meshPath);

    void hasChangesChanged(bool dirty);
    void editSessionChanged(bool active);
    void offsetModeChanged(bool elevation);
    void autoLengthChanged(bool enabled);

    /*! Fires when the active 1D / 2D results layer changes (including to
     *  null). The main window re-points the shared AnimationController, the
     *  analysis-toolbar combos, and the layer-tree check-state at these. */
    void activeResultsLayerChanged(SWMMResultsLayer *layer);
    void active2DResultsLayerChanged(SWMM2DResultsLayer *layer);

    /*! Slice CF.3 — forwards MapToolPick2DCells::cellsPicked up to the
     *  main window so it can open the Comparison Plot Dialog. */
    void pick2DCellsPicked(class SWMM2DResultsLayer *layer,
                            const QVector<int> &triIdxList,
                            const QVector<openswmmvis::plot::PlotAttribute> &attrs);

    /*! Forwards MapToolMeshProfile::profilePathTraced up to the main window
     *  so it can open the MeshProfilePlotDialog. The polyline is in scene
     *  coords (sx = mapX, sy = -mapY).
     *
     *  Slice US.A1 — meshProfileTraced is the MESH-TOOLBAR (bed-only) channel;
     *  analysisMeshProfileTraced is the ANALYSIS (ground+depth+envelope)
     *  channel. Same polyline payload; different dialog target. */
    void meshProfileTraced(const QVector<QPointF> &scenePolyline);
    void analysisMeshProfileTraced(const QVector<QPointF> &scenePolyline);

    /*! The TERRAIN-TOOLBAR channel — same polyline payload, but the main window
     *  routes it to a RasterProfilePlotDialog sampling the active DEM raster. */
    void terrainProfileTraced(const QVector<QPointF> &scenePolyline);

    /*! Forwards MapToolMeshSelectEdge::plotEdgeFluxRequested up to the main
     *  window so it can open the comparison plot with the edge's flow OR flux
     *  series (carried by `attr`). */
    void meshEdgeFluxRequested(class SWMM2DMeshLayer *mesh, int triIdx, int edgeLocal,
                               openswmmvis::plot::PlotAttribute attr);

    /*! Forwards MapToolMeshSelectEdge::statusMessageChanged (Ctrl-click
     *  boundary path picking hints) up to the main window's status bar. */
    void meshEdgeStatusMessage(const QString &message);

    /*! Forwards MapToolMeshSelectVertex::plotVertexSeriesRequested up to the
     *  main window so it can plot the chosen interpolated attributes
     *  (depth / HGL) for the vertices. */
    void meshVertexSeriesRequested(class SWMM2DMeshLayer *mesh, const QVector<int> &vertexIdxList,
                                   const QVector<openswmmvis::plot::PlotAttribute> &attrs);

    /*! Slice BC — fires whenever the active terrain raster changes
     *  (set / cleared / swapped) or its vertical-unit conversion
     *  factor is updated.  The profile-plot dialog listens so the
     *  ground line stays in sync with the toolbar's terrain selection. */
    void activeTerrainChanged(GISRasterLayer *newTerrain);

    /*! Fires once during closeEvent, AFTER the user-prompt save / cancel
     *  decision is committed but BEFORE the QMdiSubWindow teardown has
     *  begun.  All owned QObjects (model layer, results layers, canvas,
     *  etc.) are still alive and safe to dereference.  External windows
     *  that hold raw back-pointers to this project (e.g. ProfilePlotDialog)
     *  connect here so they can drop their references gracefully — using
     *  `QObject::destroyed` instead is unsafe because by the time it
     *  fires the children are already gone. */
    void aboutToClose();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateWindowTitle();
    void repositionMeasurePanel();
    void updateMeasureUnitCombo();

    /*! Post-open completion shared by loadModel() and loadModelAsync():
     *  engine option sync, selection bridge, CRS adoption (may prompt),
     *  zoom-to-extent. GUI thread only. */
    bool finishModelLoad(QList<QString> &warnings, QList<QString> &errors);

    /*! Mirror the current MeshCell selection onto the active 2D results layer
     *  (and clear it off all other 2D results layers). Driven by selection
     *  changes and active-2D-layer changes (results-analysis demarcation). */
    void refreshActive2DCellHighlight();

    OpenSWMMVisWorkspace *mWorkspace          = nullptr;
    MapCanvas            *mCanvas             = nullptr;
    SWMMModelLayer       *mModelLayer         = nullptr;
    UnitSystem           *mUnits              = nullptr;
    SelectionManager     *mSelectionManager   = nullptr;
    bool                 mCanvasCRSAdopted    = false;  // true after first successful loadModel
    bool                 mHasChanges          = false;
    bool                 mEditSessionActive   = false;
    bool                 mElevationOffsetMode = false;  // OPTIONS LINK_OFFSETS = ELEVATION
    bool                 mUntitled            = false;  // Slice Y — never saved
    bool                 mClosePromptActive   = false;  // re-entrancy guard for closeEvent's prompt
    QStringList          mLastSaveWarnings;   // delta across the last successful engine write
    QString              mEngineVersion       = "6.0.0";  // Default to newest version
    QString              mNotesHtml;                      // [TITLE] notes (rich HTML)
    QJsonArray           mPending2DResultsRestore;        // .oswp 2D results entries

    OpenSWMMVisMapToolPan         *mPanTool           = nullptr;
    OpenSWMMVisMapToolZoom        *mZoomInTool        = nullptr;
    OpenSWMMVisMapToolZoom        *mZoomOutTool       = nullptr;
    OpenSWMMVisMapToolSelect      *mSelectTool        = nullptr;
    OpenSWMMVisMapToolSelectPolygon *mSelectPolygonTool = nullptr;
    OpenSWMMVisMapToolMeasure     *mMeasureTool       = nullptr;
    class OpenSWMMVisMapToolSelectProfile *mSelectProfileTool = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddJunctionTool   = nullptr;
    OpenSWMMVisMapToolAddVirtualNode *mAddVirtualJunctionTool = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddOutfallTool    = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddStorageTool    = nullptr;
    OpenSWMMVisMapToolAddNode     *mAddDividerTool    = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddConduitTool    = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddPumpTool       = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddOrificeTool    = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddWeirTool       = nullptr;
    OpenSWMMVisMapToolAddLink     *mAddOutletTool     = nullptr;
    OpenSWMMVisMapToolAddGage     *mAddGageTool       = nullptr;
    OpenSWMMVisMapToolAddSubcatchment *mAddSubcatchTool = nullptr;
    OpenSWMMVisMapToolAddText     *mAddTextTool      = nullptr;
    OpenSWMMVisAnnotationLayer    *mAnnotationLayer  = nullptr;  ///< lazy; created on first text op
    class MapToolPick2DCells          *mPick2DCellsTool = nullptr;  ///< CF.3 — lazy
    class MapToolMeshProfile          *mMeshProfileTool = nullptr;  ///< mesh-toolbar (bed-only) profile-trace — lazy
    class MapToolMeshProfile          *mAnalysisMeshProfileTool = nullptr; ///< US.A1 — analysis (ground+depth) profile-trace — lazy
    class MapToolMeshProfile          *mTerrainProfileTool = nullptr; ///< terrain-toolbar (DEM raster) profile-trace — lazy
    class MapToolMeshSelectVertex     *mMeshSelectVertexTool = nullptr; ///< §V.VB — lazy
    class MapToolMeshSelectEdge       *mMeshSelectEdgeTool   = nullptr; ///< §V.VB — lazy

    // Measure tool floating panel (child of mCanvas)
    QFrame    *mMeasurePanel       = nullptr;
    QComboBox *mMeasureModeCombo   = nullptr;
    QComboBox *mMeasureUnitCombo   = nullptr;
    QLabel    *mMeasureTotalLabel  = nullptr;

    bool                           mAutoLengthEnabled = false;

    // Slice QA.2 — output identity registry. Owned via QObject parent
    // (constructed with `this` as parent in the .cpp constructor) so
    // it follows the project window's lifetime.
    openswmmvis::OutputStatsRegistry *mStatsRegistry = nullptr;

    // Active analysis layers (results-analysis demarcation). QPointer so a
    // layer destroyed/removed from the canvas auto-nulls the active pointer
    // without a dangling reference.
    QPointer<SWMMResultsLayer>   mActiveResultsLayer;
    QPointer<SWMM2DResultsLayer> mActive2DResultsLayer;

    // Terrain editing state (per-project, persisted to .oswp)
    GISRasterLayer *mActiveTerrain      = nullptr;
    double          mTerrainNodeOffset  = 0.0;
    double          mTerrainLinkOffset  = 0.0;
    QString         mTerrainVertUnit    = QStringLiteral("m"); // raster vertical unit
    double          mTerrainVertFactor  = 1.0;                 // rasterUnit → modelUnit
};

#endif // SWMMVISPROJECTWINDOW_H
