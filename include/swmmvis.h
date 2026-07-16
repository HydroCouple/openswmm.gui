/*!
 * \file   swmmvis.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 * \brief  Main application window for the OpenSWMM GUI.
 *
 * \details SWMMVis is a QMainWindow MDI host that coordinates:
 *  - Multiple SWMMVisProjectWindow sub-windows (one per loaded project).
 *  - The shared MapCanvas (inside each project window).
 *  - Dock panels: Layer Tree, Object Browser, Attribute Panel, Attribute Table,
 *    Simulation Status log, and Message Log.
 *  - Toolbars for map tools, animation controls, and editing operations.
 *  - Application menus (File, View, Layer, Simulation, Help, Window).
 *
 * The active project window is the last-focused MDI sub-window; switching
 * sub-windows rebinds all panels to the new project's canvas and selection
 * bus without recreating any widgets.
 */
#ifndef SWMMVIS_H
#define SWMMVIS_H

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <functional>

#include "core/openswmmvislogmessage.h"
#include "selection/selectionmanager.h"   // SWMMObjectRef
#include "plot/plotattribute.h"           // PlotAttribute (AT.2)
#include "plot/profilerouter.h"           // ProfileRouter::Path

QT_BEGIN_NAMESPACE
namespace Ui { class SWMMVis; }
QT_END_NAMESPACE

class QCheckBox;
class QLabel;
class QToolButton;
class QComboBox;
class QLineEdit;
class QSlider;
class QDoubleSpinBox;
class QDateTimeEdit;
class QProgressBar;
class QStandardItemModel;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class OpenSWMMVisLayer;   // Slice BI-MK.LT — onLayerKindStyleRequested signature
class QMdiSubWindow;
class QAction;
class QMenu;
class GISRasterLayer;
class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;
class MapCanvas;
class SWMMVisProjectWindow;
class LayerTreePanel;
class ObjectBrowserPanel;
class PropertiesPanel;
class AttributeTablePanel;
class SimulationStatusModel;
class ProfilePlotDialog;

namespace openswmmvis::ui { class CursorWindowSlider; }
namespace openswmmvis::ui { class PerAttributeThemingWidget; }
namespace openswmmvis::ui { class LegendDock; }
namespace openswmmvis::ui { class LayerStylingDock; }

/*!
 * \class SWMMVis
 * \brief Main application window for the OpenSWMM GUI.
 *
 * \details Wraps MapCanvas, manages the project workspace/session, map tools,
 *          and all top-level UI interactions (menus, toolbars, status bar,
 *          docks). Each MDI sub-window contains one SWMMVisProjectWindow whose
 *          canvas is rebound to the shared panels on focus change.
 */
class AnimationController;

class SWMMVis : public QMainWindow
{
    Q_OBJECT

public:
    /*!
     * \brief Constructs the main window, initialises toolbars, docks, and
     *        menus, and restores the previous session geometry from QSettings.
     * \param parent  Qt parent widget (normally nullptr for a top-level window).
     */
    explicit SWMMVis(QWidget *parent = nullptr);

    /*!
     * \brief Destructor. Saves window geometry and dock layout to QSettings.
     */
    virtual ~SWMMVis();

public slots:
    /*!
     * \brief Appends a log entry to the Message Log dock panel.
     * \param message      Human-readable message text.
     * \param messageType  Severity level (default: Information).
     */
    void onLogMessage(const QString &message,
                      OpenSWMMVisLogMessage::LogMessageType messageType
                          = OpenSWMMVisLogMessage::LogMessageType::Information);

private:
    void initializeWelcomeScreen();
    void initializeToolBars();
    void initializeAnimationToolBar();
    void initializeTerrainToolBar();
    void initializeMeshEditingToolBar();   // Slice §V.VB

    /*! Build the active 1D / 2D results-layer selectors on the Analysis
     *  toolbar (results-analysis demarcation). */
    void initializeAnalysisLayerCombos();

    /*! Repopulate both active-results combos from the active project window's
     *  registry / canvas, re-selecting the window's current active layers.
     *  Called on tab switch, on OutputStatsRegistry::identitiesChanged, and on
     *  active*ResultsLayerChanged. */
    void refreshActiveResultsCombos();
    void initializeMapTools();
    void initializeStatusBar();
    void initializeDockWidgets();
    void initializeLayersDockWidget();
    void initializeObjectBrowserDockWidget();
    void initializePropertiesPanelDockWidget();
    void initializeSimulationStatusDockWidget();
    void initializeMessageLogDockWidget();
    void initializeLegendDockWidget();   // Slice BB Phase 8.6.11 / 8.6.16
    void initializeMenus();
    void initializeSettings();
    void saveSettings();
    void clearPreviousWelcomeScreenElements();
    void initializeDefaultWorkspaceSession();
    void applyEditSessionToActions(bool active);
    void applyProjectOpenToActions(bool open);

    /*! Bold the active side of the "Offset Mode: Elevation [ ] Depth" status-bar
     *  toggle so the current LINK_OFFSETS convention is legible at a glance. */
    void updateOffsetModeLabels(bool elevation);

private slots:
    /*! \brief Slice BI-MK.LT — apply a kind-renderer change from the
     *         3-level layer-tree context menu. \p rendererId selects
     *         the renderer class ("single" / "graduated" / "categorized");
     *         empty falls back to opening the layer-scope SymbologyDialog. */
    void onLayerKindStyleRequested(OpenSWMMVisLayer *layer,
                                   int kindOrdinal,
                                   const QString &rendererId);

    /*! \brief Right-click on a SWMM Output layer → "Plot Time Series…".
     *  Pops a cascading Type/Object/Variable picker dialog populated
     *  from \p layer's .out file, then opens / focuses the Comparison
     *  Plot Dialog and adds the chosen series against this specific
     *  layer (skipping the auto-pick-first-results path). */
    void onPlotTimeSeriesFromOutputLayer(class SWMMResultsLayer *layer);

    /*! \brief Create a new untitled project. */
    void onNewProject();

    /*! \brief Prompt the user to open a project file; if \p path is non-empty
     *         it bypasses the file dialog. */
    void onOpenProject(const QString &path = QString());

    /*! \brief Load a single `.inp` file directly (no `.oswp` sidecar).
     *         Non-blocking: kicks off SWMMVisProjectWindow::loadModelAsync()
     *         and completes in finalizeSingleINPOpen(). */
    void openSingleINP(const QString &filePath);

    /*! \brief Completion handler for openSingleINP()'s async load: logs
     *         diagnostics, then (on success) registers recent files, applies
     *         the .oswp sidecar, auto-loads sibling results and 2D mesh/HDF5
     *         layers, and activates the window; on failure closes it. */
    void finalizeSingleINPOpen(SWMMVisProjectWindow *window,
                               const QString &filePath,
                               bool ok,
                               const QList<QString> &warnings,
                               const QList<QString> &errors,
                               qint64 elapsedMs);

    /*! \brief Mesh Tiled LOD P1.2 — async half of the 2D mesh + prior-run
     *         HDF5 auto-load that finalizeSingleINPOpen() kicks off. The
     *         worker parses the `[2D_*]` sections / linked `.2dm` and builds
     *         the SWMM2DMeshLayer's scene geometry off the GUI thread (the
     *         two dominant costs — 36 s combined on a 5M-triangle mesh); the
     *         layer is adopted onto the canvas here on completion
     *         (hidden-until-adopted: nothing is added on failure or if the
     *         window closed mid-load). Timing lands in openswmm.load.mesh. */
    void attachMesh2DLayersAsync(SWMMVisProjectWindow *window,
                                 const QString &filePath);

    /*! \brief Announce the start of a file open: Message-Log line, status-bar
     *         message, and busy spinner. Pair with \ref endFileOpen. */
    void beginFileOpen(const QString &path);

    /*! \brief Announce the end of a file open: clears the spinner + status bar
     *         and logs one Information (\p ok) or Error line. \p summary is a
     *         short content descriptor (e.g. "3 layers"); \p elapsedMs is the
     *         total wall-clock; \p errorDetail is appended on failure. */
    void endFileOpen(const QString &path, bool ok,
                     const QString &summary, qint64 elapsedMs,
                     const QString &errorDetail = QString());

    /*! \brief Load a `.oswp` project sidecar file and its associated sessions. */
    void openProjectFile(const QString &oswpPath);

    /*! \brief Triggered when the user clicks a recent-file entry in the File menu. */
    void onOpenRecentFile(QAction *action);

    /*! \brief Save the active project to its current path; prompt for path if untitled. */
    void onSaveProject();

    /*! \brief Save the active project to a new path chosen via a file dialog. */
    void onSaveAs();

    /*! \brief Export the current canvas view to an image file. */
    void onExportMap();

    /*! \brief Clear the recent-files list from QSettings and the File menu. */
    void onClearRecentFiles();

    /*! \brief Rebuild the recent-files sub-menu after the user changes the list size. */
    void onRecentFilesSizeChanged();

    /*! \brief Show (or restore) the Welcome Screen MDI sub-window. */
    void onShowWelcomeScreen();

    /*! \brief Close the active project window after prompting for unsaved changes. */
    void onClose();

    /*! \brief Show or hide the progress bar in the status bar in busy/idle mode. */
    void onSetProgressBarBusy(bool busy);

    /*! \brief Show the About dialog. */
    void onAbout();

    /*! \brief React to a successful model load — zoom the canvas to full extent. */
    void onModelLoaded();

    /*! \brief React to a model-load failure — show an error in the log. */
    void onModelLoadError(const QString &msg);

    /*! \brief Update the engine's FLOW_UNITS option when the status-bar combo changes. */
    void onFlowUnitsChanged(int comboIndex);

    /*! \brief Update the coordinate display in the status bar. */
    void onCursorPositionChanged(double mapX, double mapY);

    /*! \brief Update the CRS button label in the status bar. */
    void onCanvasSRSChanged(SpatialReferenceSystem *srs);

    /*! \brief Open the CRS picker dialog when the user clicks the CRS button. */
    void onCRSButtonClicked();

    /*! \brief User picked a preset or typed a "1:N" string into the statusbar
     *         map-scale combo — zoom the active canvas to that scale. */
    void onMapScaleEntered(const QString &text);

    /*! \brief Active canvas reported a new scale (from wheel/pinch/zoomTo);
     *         refresh the statusbar combo's displayed text. */
    void onCanvasScaleChanged(double denominator);

    /*! \brief Open the Add Basemap dialog. */
    void onAddBasemapLayer();

    /*! \brief Open the Add Basemap dialog pre-selected on the WMS/WMTS tab. */
    void onAddWMSLayer();

    /*! \brief Prompt for an OGR vector file and add it as a GISVectorLayer. */
    void onAddVectorLayer();

    /*! \brief Prompt for a GDAL raster file and add it as a GISRasterLayer. */
    void onAddRasterLayer();

    /*! \brief Prompt for a SWMM `.out` file and add it as a SWMMResultsLayer. */
    void onAddSWMMResultsLayer();

    /*! \brief Open the Simulation Options dialog for the active project. */
    void onSimulationOptions();

    /*! \brief Open the Climatology dialog at the given tab (ClimatologyDialog::Tab). */
    void onClimatology(int tab);

    // ── Toolbar quick-wins (Phase 2) ────────────────────────────────────────
    /*! \brief Show + focus the Object Browser search box. */
    void onSearch();
    /*! \brief Show + raise the Attribute Table dock. */
    void onTabularView();
    /*! \brief Import a delimited (CSV/TSV) data file as a map layer. */
    void onAddDelimitedData();
    /*! \brief Open the statistics dashboard for the active results layer. */
    void onSummarizeResults();
    /*! \brief Ctrl+C dispatcher: copies the focused Attribute Table's selected
     *         rows as TSV when focus is inside one, otherwise the active map
     *         view as an image. One shortcut registration, routed by focus —
     *         registering Ctrl+C a second time on the table would make Qt's
     *         shortcut map treat it as ambiguous. */
    void onCopyActiveView();
    /*! \brief Print the active map view. */
    void onPrintActiveView();
    /*! \brief Invert the current map selection. */
    void onInvertSelection();

    // ── Network analysis (Phase 3) ──────────────────────────────────────────
    /*! \brief Select the upstream subnetwork (nodes, links, subcatchments). */
    void onSelectUpstream();
    /*! \brief Select the downstream subnetwork (nodes, links, subcatchments). */
    void onSelectDownstream();
    /*! \brief Shared body of onSelectUpstream / onSelectDownstream. Replaces
     *         the current selection with the traced subnetwork. */
    void onStreamSelect(bool upstream);
    /*! \brief Flow balance across the up/down-stream subnetwork boundary. */
    void onFlowBalance(bool upstream);
    /*! \brief Travel time over the up/down-stream subnetwork. */
    void onTravelTime(bool upstream);

    /*! \brief Open the User Flags Manager for the active project
     *  (docs/USER_FLAGS_UI_PLAN_2026-06-03.md Phase 2). */
    void onUserFlags();

    /*! \brief Start a simulation run for the active project. */
    void onRunSimulation();

    /*! \brief Open the Time Series Plot dialog without a pre-selected object. */
    void onPlotTimeSeries();

    /*! Slice GUI-2026-05-30 §6 — open the two-panel Report Viewer over the
     *  active project's .rpt file.  No-op if there is no project / no .rpt. */
    void onShowReport();

    /*! Slice GUI-2026-05-30 §5 — invoked after the user has armed a
     *  one-shot map pick by clicking Plot Timeseries with no selection. */
    void onPlotTimeSeriesPickComplete(const SWMMObjectRef &ref);

    /*! Open the Time Series Plot dialog for the given object ref against
     *  the active project's SWMM results .out file. Shared by
     *  ObjectBrowserPanel::plotTimeSeriesRequested and
     *  OpenSWMMVisMapToolSelect::plotTimeSeriesRequested (both deliver
     *  the same user intent from different surfaces).
     *
     *  Slice BL: this is now a backwards-compat shim that forwards to
     *  openComparisonPlotFor(); existing wiring is preserved. */
    void openTimeSeriesPlotFor(const SWMMObjectRef &ref);

    /*! Variant of \ref openTimeSeriesPlotFor that targets a specific
     *  results layer (one of several .out files loaded on the canvas).
     *  Pops the AttributePickerMenu against \p ref's kind and then plots
     *  against \p layer specifically (no auto-pick-first-found). Used by
     *  ObjectBrowserPanel's "Plot Time Series ▸ <layer>" submenu when
     *  multiple SWMMResultsLayers are loaded. */
    void openTimeSeriesPlotForOnLayer(const SWMMObjectRef &ref,
                                       SWMMResultsLayer *layer);

    /*! Slice BL — open / focus the Comparison Plot dialog and add a
     *  series for \p ref on the active project's first SWMMResultsLayer.
     *  The dialog is created once and re-used; subsequent calls add
     *  series to the existing dialog. */
    void openComparisonPlotFor(const SWMMObjectRef &ref);

    /*! Variant of \ref openComparisonPlotFor that uses a specific results
     *  layer instead of auto-picking the first one on the canvas. */
    void openComparisonPlotForOnLayer(const SWMMObjectRef &ref,
                                       SWMMResultsLayer *layer);

    /*! Slice AT.2 — open / focus the dialog and add a series for \p ref
     *  with a specific \p attribute (replaces the per-object-kind default
     *  picked by openComparisonPlotFor). When `attribute` is `Unknown`,
     *  adds one series per attribute valid for the object kind (the
     *  AttributePickerMenu "All attributes" entry). */
    void openComparisonPlotForAttribute(const SWMMObjectRef &ref,
                                        openswmmvis::plot::PlotAttribute attribute);

    /*! Variant of \ref openComparisonPlotForAttribute that uses a specific
     *  results layer instead of auto-picking. */
    void openComparisonPlotForAttributeOnLayer(const SWMMObjectRef &ref,
                                                openswmmvis::plot::PlotAttribute attribute,
                                                SWMMResultsLayer *layer);

    /*! Profile-dialog overlay variant: opens / focuses a ComparisonPlotDialog
     *  parented to \p profileDlg (rather than this main window) and given
     *  Qt::Tool flags so it floats above the profile. Reused across multiple
     *  right-click "Plot Time Series" picks from the same profile; dies with
     *  the profile dialog. */
    void openComparisonPlotOverlayForProfile(ProfilePlotDialog *profileDlg,
                                              const SWMMObjectRef &ref,
                                              openswmmvis::plot::PlotAttribute attribute);

    /*! Slice AT.2 — open / focus the dialog and add a system-wide series
     *  (rainfall, runoff, flooding, …). Resolved against the active
     *  project's first SWMMResultsLayer. */
    void openComparisonPlotForSystemAttribute(openswmmvis::plot::PlotAttribute attribute);

    /*! Slice CF.3 — open / focus the Comparison Plot dialog and add one
     *  Mesh2D-cell series per (cell, ticked-attribute) on the given 2D
     *  results layer. Called by MapToolPick2DCells. */
    void openComparisonPlotForCells(class SWMM2DResultsLayer *layer,
                                    const QVector<int> &triIdxList);

    /*! Slice AT.2 — toggle the MapToolPlotPick on/off in response to
     *  the dialog's "Add from Map…" action. Saves the previously active
     *  map tool on push and restores it on pop. */
    void onAddFromMapToggled(bool active);

    /*! Slice BC — open the profile plot dialog for the path the
     *  MapToolSelectProfile just confirmed.  Builds the dialog on the
     *  fly so each path gets a fresh non-modal window. */
    void openProfilePlotFor(const ProfileRouter::Path &path);

    /*! Open the 2D-mesh longitudinal profile dialog for the polyline the
     *  MapToolMeshProfile just traced (scene coords). One window per trace.
     *
     *  Slice US.A1 — openMeshProfilePlotFor is the ANALYSIS variant (ground +
     *  animated depth + envelope against the active 2D results layer);
     *  openMeshBedProfilePlotFor is the MESH-TOOLBAR variant (bed/terrain only,
     *  results = nullptr). Both delegate to openMeshProfileDialog. */
    void openMeshProfilePlotFor(const QVector<QPointF> &scenePolyline);
    void openMeshBedProfilePlotFor(const QVector<QPointF> &scenePolyline);

    /*! Open the DEM-raster longitudinal profile dialog for the polyline the
     *  terrain-toolbar trace tool just emitted (scene coords). The raster peer
     *  of openMeshBedProfilePlotFor: ground only, sampled off
     *  TerrainToolbar::activeTerrain() with its vertical conversion factor. */
    void openTerrainProfilePlotFor(const QVector<QPointF> &scenePolyline);

    /*! Slice US.A2 — context-sensitive Analysis "Plot Profile": dispatch to a
     *  network (pipe HGL) profile or a 2D-surface profile based on selection +
     *  what's loaded. \p forceMode: 0 = auto, 1 = network, 2 = mesh surface. */
    void onPlotProfileTriggered(int forceMode = 0);

    /*! Shared mesh-profile dialog builder. \p results may be null (bed-only). */
    void openMeshProfileDialog(const QVector<QPointF> &scenePolyline,
                               class SWMM2DResultsLayer *results,
                               const QString &title);

    /*! Open the comparison plot with one time series — edge flow (Q) or edge
     *  flux (q), per `attr` — of the mesh edge the user right-clicked in the
     *  edge-select tool. */
    void openMeshEdgeFluxPlotFor(class SWMM2DMeshLayer *mesh, int triIdx, int edgeLocal,
                                 openswmmvis::plot::PlotAttribute attr);

    /*! Open the comparison plot with interpolated depth + HGL time series for
     *  the selected mesh vertices (right-clicked in the vertex-select tool). */
    void openMeshVertexSeriesFor(class SWMM2DMeshLayer *mesh, const QVector<int> &vertexIdxList);

    void onActiveSubWindowChanged(QMdiSubWindow *window);

    /*!
     * \brief Rebuild the macOS-style Window menu: Minimize / Zoom / separator /
     *        dynamic list of open project sub-windows / separator / Bring All
     *        to Front. Called on subWindowActivated and on every project
     *        window's windowTitleChanged so dirty `*` markers refresh.
     */
    void rebuildWindowMenu();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Feature A — drag-and-drop of .inp / .oswp files onto the main window.
    // Accepted local paths are routed through onOpenProject().
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    /*! Local file paths from a drag payload that we know how to open
     *  (currently *.inp and *.oswp), in drop order. Empty when the payload
     *  carries nothing droppable. */
    [[nodiscard]] QStringList acceptableDropPaths(const QMimeData *mime) const;

    /*! Return the QMdiSubWindow currently wrapping welcomeWidget, or nullptr
     *  if it has been removed (closed). */
    QMdiSubWindow *welcomeSubWindow() const;

    /*! Tear the welcome sub-window and its tab out of the MDI area without
     *  destroying the inner widget, so Help → Show Welcome Screen can
     *  re-add it later. */
    void removeWelcomeSubWindow();

private:
    Ui::SWMMVis *ui;
    QStringList mRecentFiles;
    bool mShowSplashScreenOnStartUp  = true;
    bool mShowWelcomeScreenOnStartUp = true;

    // Status bar widgets
    QCheckBox    *mCheckBoxLevelOffsetMode             = nullptr;
    QLabel       *mLabelOffsetElevation                = nullptr;
    QLabel       *mLabelOffsetDepth                    = nullptr;
    QCheckBox    *mCheckBoxAutoLength                  = nullptr;
    QToolButton  *mToolButtonCoordinateReferenceSystem = nullptr;
    QLineEdit    *mLineEditCoordinates                 = nullptr;
    QComboBox    *mComboBoxMapScale                    = nullptr;
    QComboBox    *mComboBoxFlowUnits                   = nullptr;
    QComboBox    *mComboBoxEngineVersion               = nullptr;
    QProgressBar  *mProgressBar                       = nullptr;
    QDateTimeEdit *mDateTimeEditAnimationTime         = nullptr;
    QLabel        *mLabelAnimationSpeed               = nullptr;
    QComboBox     *mComboAnimationSpeed               = nullptr;
    // Causal "as-of within timespan" sync controls (look-back window).
    QLabel        *mLabelAnimationWindow              = nullptr;
    QDoubleSpinBox *mSpinAnimationWindow              = nullptr;
    // Issue 1 — single-thumb scrubber (cursor) + painted look-back window band.
    // Replaces the laggy two-thumb RangeSliderWidget; the window is driven by
    // mSpinAnimationWindow, not by a second thumb.
    openswmmvis::ui::CursorWindowSlider *mAnimationSlider = nullptr;
    // Active analysis-layer selectors on the Analysis toolbar. The user picks
    // which 1D / 2D results layer every analysis tool targets; "— none —"
    // returns to model editing. Populated from the active project window's
    // OutputStatsRegistry (1D) / canvas 2D results layers, kept in sync via
    // SWMMVisProjectWindow::activeResultsLayerChanged / active2DResultsLayerChanged.
    QLabel        *mLabelActiveResults1D              = nullptr;
    QComboBox     *mComboActiveResults1D              = nullptr;
    QLabel        *mLabelActiveResults2D              = nullptr;
    QComboBox     *mComboActiveResults2D              = nullptr;
    QCheckBox     *mCheckBoxLive2D                    = nullptr;  // live 2D render on/off (Issue 2)
    class AnimationController *mAnimationController  = nullptr;
    class TerrainToolbar      *mTerrainToolbar       = nullptr;
    class MeshEditingToolbar  *mMeshEditingToolbar   = nullptr;   // Slice §V.VB
    openswmmvis::ui::PerAttributeThemingWidget *mThemingWidget = nullptr;

    QSettings          mSettings;
    QStandardItemModel *mLogMessagesModel   = nullptr;
    LayerTreePanel        *mLayerTreePanel        = nullptr;
    ObjectBrowserPanel    *mObjectBrowserPanel    = nullptr;
    PropertiesPanel        *mPropertiesPanel        = nullptr;
    AttributeTablePanel   *mAttributeTablePanel   = nullptr;
    SimulationStatusModel *mSimStatusModel        = nullptr;
    // Slice BB Phase 8.6.11 / 8.6.16 — dockable per-class legend / style editor.
    openswmmvis::ui::LegendDock *mLegendDock      = nullptr;
    // Slice Z.18 — always-open layer-styling editor dock.
    openswmmvis::ui::LayerStylingDock *mLayerStylingDock = nullptr;

    // Live simulation-progress tracker for the status-bar bottom bar.
    // Keyed by SimulationRunner jobId, value is the latest 0.0–1.0
    // fraction reported by the engine progress callback. The bar shows
    // min-across-running-jobs as a real percent (never busy).
    QHash<int, double> mRunningSimProgress;

    // Keyed map of active simulation runners by jobId so the Pause /
    // Cancel toolbar actions can reach any running runner. Entries are
    // removed on SimulationRunner::finished.
    QHash<int, class SimulationRunner *> mActiveRunners;

    // Slice CF.MVP — keyed map of in-flight 2D results layers so per-tick
    // twoDDepthsAvailable signals can find the layer to push into. Uses
    // QPointer so the entry is auto-cleared if the layer is destroyed (e.g.
    // the user closes the project window mid-run).
    QHash<int, QPointer<class SWMM2DResultsLayer>> mActive2DResultsLayers;

    // Per-job simulation start time captured from simulationDatesKnown so the
    // post-run HDF5Mesh2DSource can map its /time (days-since-start) to
    // calendar QDateTime for the global animation slider.
    QHash<int, QDateTime>                          mSimulationStarts;

    // Slice AT.2 — Add-from-Map mode state. When active, mPlotPickTool is
    // the canvas's active tool and mPrevMapTool holds whichever tool was
    // active when the user toggled "Add from Map…" on. Both cleared when
    // the user toggles back off / presses Esc.
    QPointer<class OpenSWMMVisMapToolPlotPick>     mPlotPickTool;
    QPointer<class OpenSWMMVisMapTool>             mPrevMapTool;

    /** Recompute the status-bar progress bar from mRunningSimProgress. */
    void updateSimulationProgressBar();

    /**
     * Drop every simulation-status / progress-bar trace bound to @p pw —
     * cancel any still-running runners for that project, purge the
     * per-job hashes, remove the rows from the status model, and refresh
     * the bottom progress bar and Pause / Cancel actions. Invoked when
     * the project window emits aboutToClose() or a SWMMResultsLayer is
     * removed from its canvas.
     */
    void clearSimulationStatusForProject(class SWMMVisProjectWindow *pw);

    // Phase 2 editing actions — declared for future programmatic wiring
    QAction           *mActionAddJunction  = nullptr;
    QAction           *mActionAddOutfall   = nullptr;
    QAction           *mActionAddStorage   = nullptr;
    QAction           *mActionAddConduit   = nullptr;
    QAction           *mActionDeleteObject = nullptr;

    // macOS-style Window menu (programmatically built — the .ui has no
    // menuWindow). Entries rebuild on every MDI subWindowActivated and on
    // each project window's windowTitleChanged so the list stays in sync.
    QMenu             *mMenuWindow              = nullptr;
    QAction           *mActionWindowMinimize    = nullptr;
    QAction           *mActionWindowZoom        = nullptr;
    QAction           *mActionWindowBringAllToFront = nullptr;

    OpenSWMMVisWorkspace *mProject              = nullptr;
    SWMMVisProjectWindow *mActiveProjectWindow  = nullptr;  // last-bound project; survives transient focus loss

    SWMMVisProjectWindow *activeProjectWindow() const;
    MapCanvas            *activeCanvas()        const;

    /*! Zoom the given canvas to the union extent of all selected SWMM
     *  objects across visible SWMMModelLayers. Returns false when no
     *  selection exists (caller can fall back to full-extent zoom). */
    bool                  zoomCanvasToSelection(MapCanvas *c) const;

    /*! Slice GUI-2026-05-30 §5 — one-shot "Plot Timeseries" pending state.
     *  Set true by onPlotTimeSeries() when no canvas selection exists;
     *  cleared after the next map pick (or tool change). */
    bool                  mPendingPlotTimeseriesPick = false;
};

#endif // SWMMVIS_H
