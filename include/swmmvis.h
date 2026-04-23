/*!
 * \file   swmmvis.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#ifndef SWMMVIS_H
#define SWMMVIS_H

#include <QMainWindow>
#include <QSettings>
#include <functional>

#include "core/openswmmvislogmessage.h"

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
class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;
class MapCanvas;
class SWMMVisProjectWindow;
class LayerTreePanel;
class ObjectBrowserPanel;
class AttributePanel;

/**
 * @brief Main application window for the OpenSWMM GUI.
 *
 * Wraps MapCanvas, manages project workspace/session, map tools, and all
 * top-level UI interactions (menus, toolbars, status bar, docks).
 */
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
    void onOpenRecentFile();
    void onSetLayerCRS();
    void onSaveProject();
    void onSaveProjectAs();
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

    void onAddWMSLayer();
    void onAddWMTSLayer();
    void onAddVectorLayer();
    void onAddRasterLayer();
    void onAddSWMMResultsLayer();
    void onSimulationOptions();
    void onRunSimulation();
    void onPlotTimeSeries();
    void onActiveSubWindowChanged(QMdiSubWindow *window);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Ui::SWMMVis *ui;
    QStringList mRecentFiles;
    bool mShowSplashScreenOnStartUp  = true;
    bool mShowWelcomeScreenOnStartUp = true;

    // Status bar widgets
    QCheckBox    *mCheckBoxLevelOffsetMode             = nullptr;
    QToolButton  *mToolButtonCoordinateReferenceSystem = nullptr;
    QLineEdit    *mLineEditCoordinates                 = nullptr;
    QComboBox    *mComboBoxMapScale                    = nullptr;
    QComboBox    *mComboBoxFlowUnits                   = nullptr;
    QSlider      *mSliderAnimationTime                 = nullptr;
    QProgressBar *mProgressBar                        = nullptr;
    QDateTimeEdit *mDateTimeEditAnimationTime          = nullptr;

    QSettings          mSettings;
    QStandardItemModel *mLogMessagesModel  = nullptr;
    LayerTreePanel    *mLayerTreePanel     = nullptr;
    ObjectBrowserPanel *mObjectBrowserPanel = nullptr;
    AttributePanel    *mAttributePanel     = nullptr;

    OpenSWMMVisWorkspace *mProject              = nullptr;
    SWMMVisProjectWindow *mActiveProjectWindow  = nullptr;  // last-bound project; survives transient focus loss
    QToolButton          *mWelcomeCloseButton   = nullptr;  // floating top-right close button on the welcome page
    std::function<void()> mWelcomeRepositionFn;             // captured layout helper

    SWMMVisProjectWindow *activeProjectWindow() const;
    MapCanvas            *activeCanvas()        const;
};

#endif // SWMMVIS_H
