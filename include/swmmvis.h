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
class QDateTimeEdit;
class QProgressBar;
class QStandardItemModel;
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
class AttributePanel;
class AttributeTablePanel;
class SimulationStatusModel;

namespace openswmmvis::ui { class PerAttributeThemingWidget; }

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
    void initializeMapTools();
    void initializeStatusBar();
    void initializeDockWidgets();
    void initializeLayersDockWidget();
    void initializeObjectBrowserDockWidget();
    void initializeAttributePanelDockWidget();
    void initializeSimulationStatusDockWidget();
    void initializeMessageLogDockWidget();
    void initializeMenus();
    void initializeSettings();
    void saveSettings();
    void clearPreviousWelcomeScreenElements();
    void initializeDefaultWorkspaceSession();
    void applyEditSessionToActions(bool active);
    void applyProjectOpenToActions(bool open);

private slots:
    /*! \brief Slice BI-MK.LT — apply a kind-renderer change from the
     *         3-level layer-tree context menu. \p rendererId selects
     *         the renderer class ("single" / "graduated" / "categorized");
     *         empty falls back to opening the layer-scope SymbologyDialog. */
    void onLayerKindStyleRequested(OpenSWMMVisLayer *layer,
                                   int kindOrdinal,
                                   const QString &rendererId);

    /*! \brief Create a new untitled project. */
    void onNewProject();

    /*! \brief Prompt the user to open a project file; if \p path is non-empty
     *         it bypasses the file dialog. */
    void onOpenProject(const QString &path = QString());

    /*! \brief Load a single `.inp` file directly (no `.oswp` sidecar). */
    void openSingleINP(const QString &filePath);

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

    /*! \brief Start a simulation run for the active project. */
    void onRunSimulation();

    /*! \brief Open the Time Series Plot dialog without a pre-selected object. */
    void onPlotTimeSeries();

    /*! Open the Time Series Plot dialog for the given object ref against
     *  the active project's SWMM results .out file. Shared by
     *  ObjectBrowserPanel::plotTimeSeriesRequested and
     *  OpenSWMMVisMapToolSelect::plotTimeSeriesRequested (both deliver
     *  the same user intent from different surfaces).
     *
     *  Slice BL: this is now a backwards-compat shim that forwards to
     *  openComparisonPlotFor(); existing wiring is preserved. */
    void openTimeSeriesPlotFor(const SWMMObjectRef &ref);

    /*! Slice BL — open / focus the Comparison Plot dialog and add a
     *  series for \p ref on the active project's first SWMMResultsLayer.
     *  The dialog is created once and re-used; subsequent calls add
     *  series to the existing dialog. */
    void openComparisonPlotFor(const SWMMObjectRef &ref);

    /*! Slice AT.2 — open / focus the dialog and add a series for \p ref
     *  with a specific \p attribute (replaces the per-object-kind default
     *  picked by openComparisonPlotFor). When `attribute` is `Unknown`,
     *  adds one series per attribute valid for the object kind (the
     *  AttributePickerMenu "All attributes" entry). */
    void openComparisonPlotForAttribute(const SWMMObjectRef &ref,
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
    QCheckBox    *mCheckBoxAutoLength                  = nullptr;
    QToolButton  *mToolButtonCoordinateReferenceSystem = nullptr;
    QLineEdit    *mLineEditCoordinates                 = nullptr;
    QComboBox    *mComboBoxMapScale                    = nullptr;
    QComboBox    *mComboBoxFlowUnits                   = nullptr;
    QComboBox    *mComboBoxEngineVersion               = nullptr;
    QSlider       *mSliderAnimationTime                = nullptr;
    QProgressBar  *mProgressBar                       = nullptr;
    QDateTimeEdit *mDateTimeEditAnimationTime         = nullptr;
    QLabel        *mLabelAnimationSpeed               = nullptr;
    QComboBox     *mComboAnimationSpeed               = nullptr;
    class AnimationController *mAnimationController  = nullptr;
    class TerrainToolbar      *mTerrainToolbar       = nullptr;
    openswmmvis::ui::PerAttributeThemingWidget *mThemingWidget = nullptr;

    QSettings          mSettings;
    QStandardItemModel *mLogMessagesModel   = nullptr;
    LayerTreePanel        *mLayerTreePanel        = nullptr;
    ObjectBrowserPanel    *mObjectBrowserPanel    = nullptr;
    AttributePanel        *mAttributePanel        = nullptr;
    AttributeTablePanel   *mAttributeTablePanel   = nullptr;
    SimulationStatusModel *mSimStatusModel        = nullptr;

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
};

#endif // SWMMVIS_H
