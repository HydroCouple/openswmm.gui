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

#include <QDateTime>   // 2026-07-19 — mPendingSeekTime (scrub coalescing)
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <functional>
#include <optional>

#include "core/openswmmvislogmessage.h"
#include "layers/swmmmodellayer.h"        // NewProjectSpec (nested type)
#include "selection/selectionmanager.h"   // SWMMObjectRef
#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h"           // PlotAttribute (AT.2)
#include "plot/profilerouter.h"           // ProfileRouter::Path

QT_BEGIN_NAMESPACE
namespace Ui { class SWMMVis; }
QT_END_NAMESPACE

namespace openswmmvis::ui { class CompactToolbarController; }
namespace openswmmvis::ui { class ComparisonPlotDialog; }
namespace openswmmvis::ui { class RibbonGroup; }
namespace openswmmvis::project::examples { struct ExampleInfo; }

class QCheckBox;
class QLabel;
class QToolButton;
class QComboBox;
class QLineEdit;
class QSlider;
class QDoubleSpinBox;
class QDateTimeEdit;
class QProgressBar;
class QTimer;          // 2026-07-19 — mScrubCoalesceTimer (scrub coalescing)
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
namespace openswmmvis::ui { class SectionViewPanel; }
class AttributeTablePanel;
class SimulationStatusModel;
class ProfilePlotDialog;

namespace openswmmvis::ui { class CursorWindowSlider; }
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

    /*! \brief The Save As flow (path dialog, filter memory, normalization,
     *         write) for \p pw. Returns false when the user cancels or the
     *         save fails. Public so SWMMVisProjectWindow's untitled close
     *         prompt can offer a real Save As and keep the window open on
     *         cancel. */
    bool saveProjectWindowAs(SWMMVisProjectWindow *pw);

    /*! \brief Run the pre-save portability check for \p pw against
     *         \p targetPath and log any warnings.
     *
     *  Advance preview of the cross-volume / missing-file diagnostics the
     *  engine's writer produces, so they land in the log panel next to the
     *  save-success line instead of only inside the saved file. Non-blocking.
     *  Called from BOTH Save and Save As — it used to run only on Save As, so
     *  a plain Save of a model with an unportable reference reported nothing.
     *
     *  \param pw          Project window; no-op when it has no live engine.
     *  \param targetPath  Path the save will write to.
     *  \param isGpkg      True for a GeoPackage export, false for `.inp`.
     */
    void logPortabilityPreflight(SWMMVisProjectWindow *pw,
                                 const QString &targetPath,
                                 bool isGpkg);

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

    /*! Copy a bundled example to a user-chosen folder and open the copy.
     *  Examples are NEVER opened in place — simulation results land next to
     *  the .inp, which would pollute the seeded baseline. */
    void openExampleCopy(const openswmmvis::project::examples::ExampleInfo &info);

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

    /*! \brief Rebuild the status-bar coordinate text from the cached
     *  cursor position, mesh hover-Z and terrain Z. */
    void updateCoordinateReadout();
    void initializeDockWidgets();
    void initializeLayersDockWidget();
    void initializeObjectBrowserDockWidget();
    void initializePropertiesPanelDockWidget();
    void initializeSimulationStatusDockWidget();
    void initializeMessageLogDockWidget();
    void initializeLegendDockWidget();   // Slice BB Phase 8.6.11 / 8.6.16
    void initializeMenus();
    void initializeSettings();

    /*! UI redesign P1 — creates the app-level Undo/Redo actions and adopts
     *  every catalog-listed QAction into the ActionRegistry (which then
     *  owns shortcut application). Defined in src/swmmvisactions.cpp;
     *  called at the end of the constructor once all menus, toolbars and
     *  docks exist. */
    void registerActions();

    /*! Rebind the app-level Undo/Redo actions' enabled-state to \a pw's
     *  canvas undo stack (nullptr disables both). Defined in
     *  src/swmmvisactions.cpp; called from onActiveSubWindowChanged. */
    void rebindUndoRedoActions(SWMMVisProjectWindow *pw);
    QMetaObject::Connection mUndoEnableConn;
    QMetaObject::Connection mRedoEnableConn;

    /*! UI redesign P6 — assemble the tabbed compact toolbar (Home/Model/
     *  Mesh 2D/Analysis/Results/View) from the registered actions and the
     *  four adopted widget-heavy toolbars. Defined in swmmvisactions.cpp;
     *  called from registerActions() once actions and menus exist. */
    void initializeCompactToolbar();

    /*! Show/hide the contextual Mesh 2D tab based on whether the active
     *  project's canvas carries a 2D mesh layer; rewires the canvas
     *  layerAdded/layerRemoved connections on project switch. */
    void updateContextualTabs();

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

    /*! \brief Create a new untitled project (in memory only — no file is
     *         written until the first Save As). */
    void onNewProject();

    /*! \brief Build and show a pathless project window around a blank
     *         BUILDING-state engine stamped from \p spec. Synchronous — a
     *         blank engine builds instantly, so no async hop or file-open
     *         bookkeeping is involved. */
    void openUntitledProject(const SWMMModelLayer::NewProjectSpec &spec);

    /*! \brief Construct + wire a project window (signal connects, MDI
     *         registration). Shared by openSingleINP (file-backed, async
     *         load) and openUntitledProject (pathless, in-memory). */
    SWMMVisProjectWindow *createProjectWindow(const QString &filePath);


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
                               qint64 elapsedMs,
                               class OpenProgressModel *progress = nullptr);

    /*! \brief Mesh Tiled LOD P1.2 — async half of the 2D mesh + prior-run
     *         HDF5 auto-load that finalizeSingleINPOpen() kicks off. The
     *         worker parses the `[2D_*]` sections / linked `.2dm` and builds
     *         the SWMM2DMeshLayer's scene geometry off the GUI thread (the
     *         two dominant costs — 36 s combined on a 5M-triangle mesh); the
     *         layer is adopted onto the canvas here on completion
     *         (hidden-until-adopted: nothing is added on failure or if the
     *         window closed mid-load). Timing lands in openswmm.load.mesh. */
    void attachMesh2DLayersAsync(SWMMVisProjectWindow *window,
                                 const QString &filePath,
                                 class OpenProgressModel *progress = nullptr);

    /*! \brief Reopen a previous run's HDF5 2D results for \p filePath (the
     *         .inp). The .h5 comes from `[2D_OPTIONS] OUTPUT_FILE`, falling
     *         back to the project sidecar's persisted entry; saved layer
     *         settings and sublayer styles stashed on the window by
     *         ProjectSerializer::applySession are applied after the source
     *         opens. Runs whether or not a mesh layer was built — the HDF5
     *         source carries its own geometry.
     *
     *  \param h5Override when non-empty, load THIS .h5 instead of resolving
     *         one from the model — the explicit path the Import menu's
     *         "Add 2D Results…" browsed to. An explicit add also becomes the
     *         active 2D layer unconditionally (an auto-load only claims the
     *         slot when it is still empty) and leaves any pending sidecar
     *         restore alone, since it is not the project-open pass. \p
     *         filePath is still read for the model's DRY_DEPTH. */
    void maybeLoad2DResults(SWMMVisProjectWindow *window,
                            const QString &filePath,
                            const QString &h5Override = QString());

    /*! \brief Mount one selected 2D mesh cell in the Properties panel.
     *
     *  Mesh cells are not SWMM network objects, so they never reach
     *  SWMMModelLayer::identifyByName — this resolves \p ref to its mesh
     *  layer and shows a MeshTrianglePropertyAdapter (Manning's n, initial
     *  depth, tag), wired to the canvas undo stack and the project's depth
     *  unit. No-op when the ref does not resolve. */
    void showMeshCellProperties(SWMMVisProjectWindow *window,
                                const SWMMObjectRef &ref);

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

    /*! \brief Mesh hover-Z from the Mesh Editing toolbar's probe —
     *  folded into the status-bar coordinate readout (takes precedence
     *  over terrain Z while the cursor is on the mesh). */
    void onMeshHoverElevation(double z, bool finite);

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

    /*!
     * \brief Add features from an OGC Web Feature Service.
     *
     * The feature half of the OGC family, which this program has not had:
     * WMS, WMTS and WCS all answer with pictures, and a WFS answers with
     * the data — so what it returns joins the layer tree as a vector layer
     * that can be queried, classified and labelled.
     */
    void onAddWFSLayer();

    /*! \brief Prompt for an OGR vector file and add it as a GISVectorLayer. */
    void onAddVectorLayer();

    /*! \brief Prompt for a GDAL raster file and add it as a GISRasterLayer. */
    void onAddRasterLayer();

    /*! \brief Prompt for a SWMM `.out` file and add it as a SWMMResultsLayer. */
    void onAddSWMMResultsLayer();

    /*! \brief Prompt for an existing OpenSWMM 2D mesh (`.2dm`) anywhere on
     *         disk and load it into the active project as the active mesh.
     *         Delegates to SWMMVisProjectWindow::importMeshFileAsync. */
    void onAddMesh2DLayer();

    /*! \brief Prompt for an OpenSWMM 2D results file (`.h5`) anywhere on disk
     *         and add it as a SWMM2DResultsLayer, becoming the active 2D
     *         results layer. Delegates the build to maybeLoad2DResults so an
     *         explicitly added layer is identical to an auto-loaded one. */
    void onAdd2DResultsLayer();

    /*! \brief Open the Simulation Options dialog for the active project. */
    void onSimulationOptions();

    /*! \brief Open the Climatology dialog at the given tab (ClimatologyDialog::Tab). */
    void onClimatology(int tab);

    /*! \brief Open the Water Age Sources editor (Y3b — subplan G3g wiring). */
    void onEditWaterAgeSources();

    /*! \brief Open the per-element Initial Quality editor (G-A1). */
    void onEditInitialQuality();

    /*! \brief Open the Reaction System editor (G-B3). */
    void onEditReactionSystem();

    //! G4g: [HEAT_SOURCES] / [HEAT_FLUXES] / radiative-solar-cloud editor.
    void onEditHeatConfig();

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

    /*! \brief Open the Plot Variables dialog — system variables plus the
     *  plottable attributes of the current map selection — and bulk-add
     *  the checked entries to the Comparison Plot. */
    void onPlotTimeSeries();

    /*! Slice GUI-2026-05-30 §6 — open the two-panel Report Viewer over the
     *  active project's .rpt file.  No-op if there is no project / no .rpt. */
    void onShowReport();

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

    /*! Find-or-create the shared ComparisonPlotDialog (WA_DeleteOnClose;
     *  wires addFromMapToggled + the AnimationController cursor). Every
     *  plot entry point goes through this so the wiring exists exactly
     *  once. */
    openswmmvis::ui::ComparisonPlotDialog *ensureComparisonPlotDialog();

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

    /*! Y2b-2 (amendment D-Y4): descriptor-shaped variants — a fixed
     *  attribute or a species BY NAME; the quick map menus emit these.
     *  An INVALID descriptor is the "All attributes" sentinel and fans
     *  out across the run's full descriptor list, species included. The
     *  enum variants above forward here. */
    void openComparisonPlotForDescriptor(
        const SWMMObjectRef &ref,
        const openswmmvis::plot::ResultDescriptor &descriptor);
    void openComparisonPlotForDescriptorOnLayer(
        const SWMMObjectRef &ref,
        const openswmmvis::plot::ResultDescriptor &descriptor,
        SWMMResultsLayer *layer);

    /*! Profile-dialog overlay variant: opens / focuses a ComparisonPlotDialog
     *  parented to \p profileDlg (rather than this main window) and given
     *  Qt::Tool flags so it floats above the profile. Reused across multiple
     *  right-click "Plot Time Series" picks from the same profile; dies with
     *  the profile dialog. */
    void openComparisonPlotOverlayForProfile(
        ProfilePlotDialog *profileDlg,
        const SWMMObjectRef &ref,
        const openswmmvis::plot::ResultDescriptor &descriptor);

    /*! Slice AT.2 — open / focus the dialog and add a system-wide series
     *  (rainfall, runoff, flooding, …). Resolved against the active
     *  project's first SWMMResultsLayer. */
    void openComparisonPlotForSystemAttribute(openswmmvis::plot::PlotAttribute attribute);

    /*! Slice CF.3 — open / focus the Comparison Plot dialog and add one
     *  Mesh2D-cell series per (cell, attribute) on the given 2D results
     *  layer. \p attrs comes from MapToolPick2DCells' context menu. */
    void openComparisonPlotForCells(class SWMM2DResultsLayer *layer,
                                    const QVector<int> &triIdxList,
                                    const QVector<openswmmvis::plot::PlotAttribute> &attrs);

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

    /*! Open the comparison plot with the chosen interpolated series (depth
     *  and/or HGL) for the selected mesh vertices (right-clicked in the
     *  vertex-select tool). */
    void openMeshVertexSeriesFor(class SWMM2DMeshLayer *mesh, const QVector<int> &vertexIdxList,
                                 const QVector<openswmmvis::plot::PlotAttribute> &attrs);

    void onActiveSubWindowChanged(QMdiSubWindow *window);

    /*!
     * \brief Rebuild the macOS-style Window menu: Minimize / Zoom / separator /
     *        dynamic list of open project sub-windows / separator / open
     *        modeless dialogs / separator / Bring All to Front / Reset Window
     *        Positions. Called on subWindowActivated, on every project
     *        window's windowTitleChanged so dirty `*` markers refresh, and on
     *        DialogRegistry::openDialogsChanged.
     */
    void rebuildWindowMenu();

    /*!
     * \brief Recovery path for windows that have become unreachable — dragged
     *        onto a monitor that was since disconnected, or restored from a
     *        geometry that no longer maps onto any connected screen.
     *
     * Clears every saved window position (dialog `geometry` keys and the main
     * window's), then moves the main window and all open modeless dialogs
     * back onto the main window's current screen. Layout state that is not
     * position data — splitter sizes, header widths, dock/toolbar arrangement
     * — is deliberately preserved.
     */
    void resetWindowPositions();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    /*! UI redesign P6 — the toolbar-area context menu offers only the
     *  dock toggles: the compact-toolbar rows are managed by the tab
     *  strip and must not be individually hideable. */
    QMenu *createPopupMenu() override;

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
    // Coordinate-readout state: last cursor position (canvas CRS) + the
    // mesh hover-Z mirrored from the Mesh Editing toolbar's probe, so
    // either input can rebuild the readout.
    double        mLastCursorMapX                      = 0.0;
    double        mLastCursorMapY                      = 0.0;
    bool          mHasCursorPos                        = false;
    std::optional<double> mMeshHoverZ;
    QComboBox    *mComboBoxMapScale                    = nullptr;
    QComboBox    *mComboBoxFlowUnits                   = nullptr;
    QComboBox    *mComboBoxEngineVersion               = nullptr;
    QProgressBar  *mProgressBar                       = nullptr;
    /*! Stage text shown beside \ref mProgressBar ("Parsing 2D mesh…").
     *  Hidden whenever the bar is. */
    QLabel        *mProgressLabel                     = nullptr;
    QDateTimeEdit *mDateTimeEditAnimationTime         = nullptr;
    QLabel        *mLabelAnimationSpeed               = nullptr;
    QComboBox     *mComboAnimationSpeed               = nullptr;
    QCheckBox     *mCheckBoxAnimationCycle            = nullptr;  // loop playback at end-of-range (default on)
    // Causal "as-of within timespan" sync controls (look-back window).
    QLabel        *mLabelAnimationWindow              = nullptr;
    QDoubleSpinBox *mSpinAnimationWindow              = nullptr;
    // Issue 1 — single-thumb scrubber (cursor) + painted look-back window band.
    // Replaces the laggy two-thumb RangeSliderWidget; the window is driven by
    // mSpinAnimationWindow, not by a second thumb.
    openswmmvis::ui::CursorWindowSlider *mAnimationSlider = nullptr;
    // 2026-07-19 — scrub-seek coalescing (slider lag fix). cursorChanged
    // fires per drag pixel; each seek used to run the full synchronous
    // layer fetch + 2D frame load. Drag now stores the pending time and
    // restarts this ~40 ms single-shot timer (trailing edge, same idiom as
    // MapCanvas::m_refreshTimer); cursorReleased seeks immediately so the
    // final frame matches the thumb exactly.
    QTimer        *mScrubCoalesceTimer                = nullptr;
    QDateTime      mPendingSeekTime;
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

    // UI redesign P6 — tabbed compact toolbar (strip + per-tab rows).
    openswmmvis::ui::CompactToolbarController *mCompactToolbar = nullptr;
    class QToolBar            *mToolBarHome          = nullptr;
    class QToolBar            *mToolBarModel         = nullptr;
    class QToolBar            *mToolBarMesh2D        = nullptr;
    class QToolBar            *mToolBarView          = nullptr;
    // Iteration 2 (R3) — the two formerly .ui-authored bars, rebuilt in
    // code with the SAME objectNames so saved window state keeps working.
    class QToolBar            *mToolBarAnimation     = nullptr;
    class QToolBar            *mToolBarAnalysis      = nullptr;
    // Ribbon groups later phases re-anchor into: the analysis layer
    // combos, the animation timeline widgets, and the Plots group whose
    // Plot-Profile button carries the override dropdown.
    openswmmvis::ui::RibbonGroup *mGroupResultsLayers = nullptr;
    openswmmvis::ui::RibbonGroup *mGroupTimeline      = nullptr;
    openswmmvis::ui::RibbonGroup *mGroupPlots         = nullptr;
    QMetaObject::Connection    mMeshTabConnAdd;
    QMetaObject::Connection    mMeshTabConnRemove;

    QSettings          mSettings;
    QStandardItemModel *mLogMessagesModel   = nullptr;
    /*! Perf plan B2 — one Message-Log scrollToBottom per event-loop turn,
     *  not one per appended row (engine-warning drains are bursts). */
    bool                mLogScrollPending   = false;
    LayerTreePanel        *mLayerTreePanel        = nullptr;
    ObjectBrowserPanel    *mObjectBrowserPanel    = nullptr;
    PropertiesPanel        *mPropertiesPanel        = nullptr;
    // Slice SP.4 — dockable vector section / profile view of the selection.
    openswmmvis::ui::SectionViewPanel *mSectionViewPanel = nullptr;
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

    // ── Status-bar progress arbitration (LOAD_PERF Phase 1c) ──────────────
    // One QProgressBar serves three producers: simulation runs (real
    // percent), project opens (real percent), and a handful of genuinely
    // unmeasurable operations (raster/table open) that still want a spinner.
    // They are mutually exclusive, so ownership is explicit and every writer
    // goes through applyProgressBarState().

    /*! Who currently owns \ref mProgressBar. Simulation outranks Open: a run
     *  is the longer, explicitly user-initiated task, and a 200 ms open must
     *  not blank a 40-minute run's percentage. */
    enum class ProgressOwner { None, Busy, Open, Simulation };

    /*! In-flight project opens keyed by open id. The bar shows the MINIMUM
     *  percent across them, matching the min-across-jobs rule the simulation
     *  path already uses, so a multi-model .oswp reports the laggard. */
    QHash<int, class OpenProgressModel *> mRunningOpens;
    int  mNextOpenId       = 1;
    int  mBusyRefCount     = 0;   //!< nesting depth of onSetProgressBarBusy(true)

    /*! Single funnel that decides owner, range, value, label and visibility.
     *  Every progress writer calls this rather than touching the widget. */
    void applyProgressBarState();

    /*! Create (and register) a progress model for one open. Ownership stays
     *  with SWMMVis; \ref endOpenProgress destroys it. */
    OpenProgressModel *beginOpenProgress();

    /*! Retire the model for \p openId, hiding the bar if nothing else owns it. */
    void endOpenProgress(int openId);

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
    QAction           *mActionWindowResetPositions  = nullptr;

    OpenSWMMVisWorkspace *mProject              = nullptr;
    SWMMVisProjectWindow *mActiveProjectWindow  = nullptr;  // last-bound project; survives transient focus loss

    SWMMVisProjectWindow *activeProjectWindow() const;
    MapCanvas            *activeCanvas()        const;

    /*! Zoom the given canvas to the union extent of all selected SWMM
     *  objects across visible SWMMModelLayers. Returns false when no
     *  selection exists (caller can fall back to full-extent zoom). */
    bool                  zoomCanvasToSelection(MapCanvas *c) const;
};

#endif // SWMMVIS_H
