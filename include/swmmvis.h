/*!
 * \file   swmmvis.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#ifndef SWMMVIS_H
#define SWMMVIS_H

#include <QHash>
#include <QMainWindow>
#include <QSettings>
#include <functional>

#include "core/openswmmvislogmessage.h"
#include "selection/selectionmanager.h"   // SWMMObjectRef

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
class QMdiSubWindow;
class QAction;
class QMenu;
class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;
class MapCanvas;
class SWMMVisProjectWindow;
class LayerTreePanel;
class ObjectBrowserPanel;
class AttributePanel;
class AttributeTablePanel;
class SimulationStatusModel;

/**
 * @brief Main application window for the OpenSWMM GUI.
 *
 * Wraps MapCanvas, manages project workspace/session, map tools, and all
 * top-level UI interactions (menus, toolbars, status bar, docks).
 */
class AnimationController;

class SWMMVis : public QMainWindow
{
    Q_OBJECT

public:
    explicit SWMMVis(QWidget *parent = nullptr);
    virtual ~SWMMVis();

public slots:
    void onLogMessage(const QString &message,
                      OpenSWMMVisLogMessage::LogMessageType messageType
                          = OpenSWMMVisLogMessage::LogMessageType::Information);

private:
    void initializeWelcomeScreen();
    void initializeToolBars();
    void initializeAnimationToolBar();
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

private slots:
    void onNewProject();
    void onOpenProject(const QString &path = QString());
    void openSingleINP(const QString &filePath);
    void openProjectFile(const QString &oswpPath);
    void onOpenRecentFile(QAction *action);
    void onSaveProject();
    void onSaveAs();
    void onExportMap();
    void onClearRecentFiles();
    void onRecentFilesSizeChanged();
    void onShowWelcomeScreen();
    void onClose();
    void onSetProgressBarBusy(bool busy);
    void onAbout();
    void onModelLoaded();
    void onModelLoadError(const QString &msg);
    void onFlowUnitsChanged(int comboIndex);

    void onCursorPositionChanged(double mapX, double mapY);
    void onCanvasSRSChanged(SpatialReferenceSystem *srs);
    void onCRSButtonClicked();

    void onAddBasemapLayer();
    void onAddWMSLayer();   // opens AddBasemapDialog pre-selected on WMS/WMTS tab
    void onAddVectorLayer();
    void onAddRasterLayer();
    void onAddSWMMResultsLayer();
    void onSimulationOptions();
    void onRunSimulation();
    void onPlotTimeSeries();

    /*! Open the Time Series Plot dialog for the given object ref against
     *  the active project's SWMM results .out file. Shared by
     *  ObjectBrowserPanel::plotTimeSeriesRequested and
     *  OpenSWMMVisMapToolSelect::plotTimeSeriesRequested (both deliver
     *  the same user intent from different surfaces). */
    void openTimeSeriesPlotFor(const SWMMObjectRef &ref);

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
    QSlider       *mSliderAnimationTime                = nullptr;
    QProgressBar  *mProgressBar                       = nullptr;
    QDateTimeEdit *mDateTimeEditAnimationTime         = nullptr;
    class AnimationController *mAnimationController  = nullptr;

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
};

#endif // SWMMVIS_H
