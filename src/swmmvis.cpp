/*!
 * \file   swmmvis.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */

#include <QCommandLinkButton>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QGridLayout>
#include <QComboBox>
#include <QToolButton>
#include <QSlider>
#include <QDateTimeEdit>
#include <QProgressBar>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QHeaderView>
#include <QMetaEnum>
#include <QFileInfo>
#include <QCloseEvent>
#include <QGraphicsItem>
#include <QFileDialog>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTabBar>

#include "swmmvis.h"
#include "ui_swmmvis.h"
#include "version.h"

#include "core/unitsystem.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "map/openswmmvisscene.h"
#include "map/mapextent.h"
#include "project/openswmmvisworkspace.h"
#include "layers/swmmmodellayer.h"
#include "swmmvisprojectwindow.h"
#include "core/crsreproject.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "ui/dialogs/crschangedialog.h"
#include "ui/dialogs/aboutdialog.h"
#include "ui/dialogs/layerpropertiesdialog.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/timeseriesplotdialog.h"
#include "ui/dialogs/wmsconnectiondialog.h"
#include "ui/dialogs/wmtsconnectiondialog.h"
#include "ui/panels/layertreepanel.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/attributepanel.h"
#include "selection/selectionmanager.h"
#include "simulation/simulationrunner.h"
#include "simulation/simulationstatusmodel.h"
#include "map/tools/maptoolidentify.h"   // IdentifyResult

#include <QPointer>
#include <QEvent>
#include <QFrame>
#include <QMdiSubWindow>
#include <QTimer>
#include "layers/openswmmvislayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/wmslayer.h"
#include "layers/wmtslayer.h"

#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>
#include <QCommandLinkButton>
#include <QScrollArea>
#include <QSignalBlocker>

SWMMVis::SWMMVis(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::SWMMVis)
{
    ui->setupUi(this);

    initializeToolBars();
    initializeStatusBar();
    initializeWelcomeScreen();
    initializeDockWidgets();
    initializeMenus();
    initializeMapTools();
    initializeSettings();
    initializeDefaultWorkspaceSession();
}

SWMMVis::~SWMMVis()
{
    delete ui;
}

// ── Logging ───────────────────────────────────────────────────────────────

void SWMMVis::onLogMessage(const QString &message,
                            OpenSWMMVisLogMessage::LogMessageType messageType)
{
    QString name = QString::fromStdString(
        std::string(QMetaEnum::fromType<OpenSWMMVisLogMessage::LogMessageType>()
                        .valueToKey(messageType)));

    mLogMessagesModel->appendRow(QList<QStandardItem *>()
        << new QStandardItem(QDateTime::currentDateTime().toString("MM/dd/yyyy hh:mm:ss"))
        << new QStandardItem(name)
        << new QStandardItem(message));

    ui->treeViewMessageLogs->scrollToBottom();
}

// ── Initialization ────────────────────────────────────────────────────────

void SWMMVis::initializeDefaultWorkspaceSession()
{
    if (!mProject)
        mProject = OpenSWMMVisWorkspace::newInstance(QString(), this);

    onLogMessage(QStringLiteral("Workspace initialized."));
}

// ── Active-window helpers ─────────────────────────────────────────────────

SWMMVisProjectWindow *SWMMVis::activeProjectWindow() const
{
    // Prefer the cached value so a focus-on-dock click doesn't make
    // toolbar actions ("Add Vector", etc.) think there's no active project.
    // Fall back to the MDI area's notion only if the cache is stale (e.g.
    // before onActiveSubWindowChanged has fired for the first time).
    if (mActiveProjectWindow)
        return mActiveProjectWindow;
    QMdiSubWindow *sub = ui->mdiAreaCentral->activeSubWindow();
    return qobject_cast<SWMMVisProjectWindow *>(sub);
}

MapCanvas *SWMMVis::activeCanvas() const
{
    if (auto *w = activeProjectWindow())
        return w->canvas();
    return nullptr;
}

namespace {

/**
 * @brief QMdiSubWindow subclass for the Welcome tab whose closeEvent
 *        is the single, reliable hook for tear-down. QMdiArea's tab-X
 *        ultimately calls subWindow->close() which calls closeEvent;
 *        overriding it here guarantees interception regardless of
 *        platform-specific event-filter / tabCloseRequested quirks
 *        that proved unreliable on macOS Cocoa.
 */
class WelcomeSubWindow : public QMdiSubWindow
{
public:
    explicit WelcomeSubWindow(QWidget *parent = nullptr)
        : QMdiSubWindow(parent)
    {
        setWindowTitle(QObject::tr("Welcome"));
        // Never auto-destroy on close — the inner welcome widget must
        // survive so Help → Show Welcome Screen can re-insert it.
        setAttribute(Qt::WA_DeleteOnClose, false);
        // NOTE: do NOT call setWindowFlags here. In QMdiArea's TabbedView
        // mode, re-flagging a freshly-added sub-window detaches it from
        // the tab stack and floats it as an undocked window. The tab bar
        // already renders the close X (tabsClosable=true in the .ui) and
        // does not expose minimize/maximize on tabs, so the desired
        // "close-only, not minimizable" look is the default.
    }

    /** Callback invoked from closeEvent. Set this at construction to
     *  forward the close to SWMMVis::removeWelcomeSubWindow. */
    std::function<void(QCloseEvent *)> onClose;

protected:
    void closeEvent(QCloseEvent *event) override
    {
        qDebug() << "[welcome] WelcomeSubWindow::closeEvent — forwarding to handler";
        if (onClose)
            onClose(event);
        // Base class ignores unless we accept; accept so the sub-window
        // proceeds to hide/remove per our handler's side-effects.
        event->accept();
    }
};

} // anonymous namespace

void SWMMVis::initializeWelcomeScreen()
{
    clearPreviousWelcomeScreenElements();

    // setupUi() wraps welcomeWidget in a plain QMdiSubWindow. Replace
    // that with our WelcomeSubWindow subclass, whose overridden
    // closeEvent() is the single reliable hook for tear-down regardless
    // of how the close was triggered (tab-X, programmatic close,
    // platform-specific tab-bar signal). Reparent welcomeWidget out of
    // the auto-created sub-window first so removeSubWindow doesn't take
    // our inner widget with it.
    if (QMdiSubWindow *orig = welcomeSubWindow())
    {
        ui->mdiAreaCentral->removeSubWindow(orig);
        orig->deleteLater();
    }
    if (ui->welcomeWidget)
    {
        auto *sub = new WelcomeSubWindow();
        sub->setWidget(ui->welcomeWidget);
        sub->onClose = [this](QCloseEvent *) { removeWelcomeSubWindow(); };
        ui->mdiAreaCentral->addSubWindow(sub);
        sub->show();
        qDebug() << "[welcome] WelcomeSubWindow installed — closeEvent hook active";
    }


    // Wire welcome buttons
    connect(ui->commandLinkButtonNew,  &QCommandLinkButton::clicked, this, &SWMMVis::onNewProject);
    connect(ui->commandLinkButtonOpen, &QCommandLinkButton::clicked, this, [this]{ onOpenProject(); });
    connect(ui->commandLinkButtonClearRecentFiles, &QCommandLinkButton::clicked,
            this, &SWMMVis::onClearRecentFiles);

    // Persist "show welcome on startup" toggle and honour it at launch.
    mShowWelcomeScreenOnStartUp = mSettings.value(
        QStringLiteral("SWMMVis::ShowWelcomeOnStartup"), true).toBool();
    ui->checkBoxShowWelcomeOnStartUp->setChecked(mShowWelcomeScreenOnStartUp);
    connect(ui->checkBoxShowWelcomeOnStartUp, &QCheckBox::toggled, this, [this](bool on) {
        mShowWelcomeScreenOnStartUp = on;
        mSettings.setValue(QStringLiteral("SWMMVis::ShowWelcomeOnStartup"), on);
    });
    if (!mShowWelcomeScreenOnStartUp)
        removeWelcomeSubWindow();

    // Populate Learn SWMM links
    if (auto *frame = ui->frameLearnSWMM)
    {
        auto *layout = new QVBoxLayout(frame);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(2);
        struct Link { const char *text; const char *url; };
        const Link links[] = {
            { "User Manual",          "https://www.openswmm.org/gui/manual/" },
            { "Engine API Reference", "https://www.openswmm.org/api/" },
            { "Report an Issue",      "https://github.com/openswmm/openswmm/issues" },
        };
        for (const auto &lk : links) {
            auto *btn = new QCommandLinkButton(tr(lk.text), frame);
            btn->setIcon(QIcon(QStringLiteral(":/swmmvis/Help")));
            const QString url = QString::fromUtf8(lk.url);
            connect(btn, &QCommandLinkButton::clicked, this,
                    [url]{ QDesktopServices::openUrl(QUrl(url)); });
            layout->addWidget(btn);
        }
        layout->addStretch(1);
    }

    // Populate Example Projects (auto-discovered from <install>/examples/ if present)
    if (auto *frame = ui->frameExampleProjects)
    {
        auto *layout = new QVBoxLayout(frame);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(2);

        QString examplesDir;
        const QStringList candidates = {
            QCoreApplication::applicationDirPath() + QStringLiteral("/../share/openswmmgui/examples"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/examples"),
            QCoreApplication::applicationDirPath() + QStringLiteral("/examples"),
        };
        for (const QString &c : candidates) {
            if (QFileInfo(c).isDir()) { examplesDir = c; break; }
        }

        if (examplesDir.isEmpty())
        {
            auto *label = new QLabel(tr("(No bundled examples found.)"), frame);
            label->setStyleSheet("color: gray;");
            layout->addWidget(label);
        }
        else
        {
            QDir d(examplesDir);
            const QFileInfoList entries = d.entryInfoList({QStringLiteral("*.inp")}, QDir::Files, QDir::Name);
            for (const QFileInfo &fi : entries) {
                auto *btn = new QCommandLinkButton(fi.baseName(), frame);
                btn->setDescription(tr("Open %1 as a read-only copy").arg(fi.fileName()));
                btn->setIcon(QIcon(QStringLiteral(":/swmmvis/Open")));
                const QString path = fi.absoluteFilePath();
                connect(btn, &QCommandLinkButton::clicked, this, [this, path]{ onOpenProject(path); });
                layout->addWidget(btn);
            }
        }
        layout->addStretch(1);
    }
}

void SWMMVis::initializeToolBars()
{
    initializeAnimationToolBar();
}

void SWMMVis::initializeAnimationToolBar()
{
    mSliderAnimationTime = new QSlider(Qt::Horizontal, this);
    mSliderAnimationTime->setSingleStep(1);
    mSliderAnimationTime->setPageStep(10);
    mSliderAnimationTime->setTickInterval(10);
    mSliderAnimationTime->setTickPosition(QSlider::TicksBelow);
    mSliderAnimationTime->setToolTip("Animation Time");
    mSliderAnimationTime->setStatusTip("Animation Time");
    mSliderAnimationTime->setMinimumWidth(300);

    mDateTimeEditAnimationTime = new QDateTimeEdit(this);
    mDateTimeEditAnimationTime->setDisplayFormat("MM/dd/yyyy hh:mm");
    mDateTimeEditAnimationTime->setCalendarPopup(true);
    mDateTimeEditAnimationTime->setToolTip("Animation Time");
    mDateTimeEditAnimationTime->setStatusTip("Animation Time");

    ui->toolBarAnimation->insertWidget(ui->actionSkipForward, mSliderAnimationTime);
    ui->toolBarAnimation->insertWidget(ui->actionSkipForward, mDateTimeEditAnimationTime);
}

void SWMMVis::initializeMapTools()
{
    // Tools live on each SWMMVisProjectWindow; toolbar actions delegate to the active window.
    connect(ui->actionPan,    &QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activatePanTool();
    });
    connect(ui->actionZoomIn, &QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activateZoomInTool();
    });
    connect(ui->actionZoomOut,&QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activateZoomOutTool();
    });
    connect(ui->actionSelect, &QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activateSelectTool();
    });
    connect(ui->actionMeasure,&QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activateMeasureTool();
    });
    connect(ui->actionZoomExtent, &QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->zoomToFullExtent();
    });
    connect(ui->actionZoomToSelection, &QAction::triggered, this, [this]() {
        MapCanvas *c = activeCanvas();
        if (!c) return;
        QRectF sel;
        for (QGraphicsItem *item : c->mapScene()->selectedItems())
        {
            QRectF ir = item->mapToScene(item->boundingRect()).boundingRect();
            sel = sel.isNull() ? ir : sel.united(ir);
        }
        if (!sel.isNull() && sel.width() > 0 && sel.height() > 0)
        {
            MapExtent ext(sel.left(), -sel.bottom(), sel.right(), -sel.top());
            if (ext.isValid()) c->setExtent(ext);
        }
    });
    connect(ui->actionSelectUpstream, &QAction::triggered, this, [this]() {
        onLogMessage("Select Upstream: not yet implemented", OpenSWMMVisLogMessage::Information);
    });
    connect(ui->actionSelectDownstream, &QAction::triggered, this, [this]() {
        onLogMessage("Select Downstream: not yet implemented", OpenSWMMVisLogMessage::Information);
    });

    connect(ui->mdiAreaCentral, &QMdiArea::subWindowActivated,
            this, &SWMMVis::onActiveSubWindowChanged);
}

void SWMMVis::initializeStatusBar()
{
    auto addSep = [this]() {
        QFrame *sep = new QFrame(ui->statusBar);
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);
        ui->statusBar->addPermanentWidget(sep);
    };

    // Flow units
    ui->statusBar->addPermanentWidget(new QLabel("Flow Units:", ui->statusBar));
    mComboBoxFlowUnits = new QComboBox(ui->statusBar);
    mComboBoxFlowUnits->addItem("CFS", static_cast<int>(swmm_CFS));
    mComboBoxFlowUnits->addItem("GPM", static_cast<int>(swmm_GPM));
    mComboBoxFlowUnits->addItem("MGD", static_cast<int>(swmm_MGD));
    mComboBoxFlowUnits->addItem("CMS", static_cast<int>(swmm_CMS));
    mComboBoxFlowUnits->addItem("LPS", static_cast<int>(swmm_LPS));
    mComboBoxFlowUnits->addItem("MLD", static_cast<int>(swmm_MLD));
    mComboBoxFlowUnits->setMinimumWidth(80);
    connect(mComboBoxFlowUnits, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SWMMVis::onFlowUnitsChanged);
    connect(UnitSystem::instance(), &UnitSystem::unitsChanged,
            this, [this](swmm_FlowUnitsProperty u) {
        // Reflect engine-driven changes in the combo without re-firing the slot
        const int idx = mComboBoxFlowUnits->findData(static_cast<int>(u));
        if (idx >= 0 && idx != mComboBoxFlowUnits->currentIndex())
        {
            QSignalBlocker block(mComboBoxFlowUnits);
            mComboBoxFlowUnits->setCurrentIndex(idx);
        }
    });
    ui->statusBar->addPermanentWidget(mComboBoxFlowUnits);
    addSep();

    // Progress bar
    mProgressBar = new QProgressBar(ui->statusBar);
    mProgressBar->setRange(0, 0);
    mProgressBar->setValue(0);
    mProgressBar->setToolTip("Progress");
    mProgressBar->setMaximumWidth(100);
    mProgressBar->setVisible(false);
    ui->statusBar->addPermanentWidget(mProgressBar);
    addSep();

    // Offset mode (LINK_OFFSETS option). Disabled until a project is active;
    // toggling rebinds via activeProjectWindow().
    ui->statusBar->addPermanentWidget(new QLabel("Offset Mode:", ui->statusBar));
    mCheckBoxLevelOffsetMode = new QCheckBox("Elevation", ui->statusBar);
    mCheckBoxLevelOffsetMode->setStyleSheet(
        "QCheckBox::indicator:checked   {image: url(:/swmmvis/ToggleOn);}"
        "QCheckBox::indicator:unchecked {image: url(:/swmmvis/ToggleOff);}");
    mCheckBoxLevelOffsetMode->setEnabled(false);
    connect(mCheckBoxLevelOffsetMode, &QCheckBox::toggled, this, [this](bool on) {
        if (auto *pw = activeProjectWindow())
            pw->setElevationOffsetMode(on);
        mCheckBoxLevelOffsetMode->setText(on ? QStringLiteral("Elevation")
                                             : QStringLiteral("Depth"));
    });
    ui->statusBar->addPermanentWidget(mCheckBoxLevelOffsetMode);
    addSep();

    // Auto-length toggle (Phase 2). Conduit length recalculates from the
    // polyline on every node move / vertex edit when enabled. Disabled
    // until a project is active; rebinds in onActiveSubWindowChanged.
    ui->statusBar->addPermanentWidget(new QLabel("Auto-Length:", ui->statusBar));
    mCheckBoxAutoLength = new QCheckBox("Off", ui->statusBar);
    mCheckBoxAutoLength->setStyleSheet(
        "QCheckBox::indicator:checked   {image: url(:/swmmvis/ToggleOn);}"
        "QCheckBox::indicator:unchecked {image: url(:/swmmvis/ToggleOff);}");
    mCheckBoxAutoLength->setEnabled(false);
    connect(mCheckBoxAutoLength, &QCheckBox::toggled, this, [this](bool on) {
        if (auto *pw = activeProjectWindow())
            pw->setAutoLengthEnabled(on);
        mCheckBoxAutoLength->setText(on ? QStringLiteral("On")
                                        : QStringLiteral("Off"));
    });
    ui->statusBar->addPermanentWidget(mCheckBoxAutoLength);
    addSep();

    // Coordinates
    ui->statusBar->addPermanentWidget(new QLabel("Coordinates:", ui->statusBar));
    mLineEditCoordinates = new QLineEdit("0,0", ui->statusBar);
    mLineEditCoordinates->setReadOnly(true);
    mLineEditCoordinates->setMaximumWidth(250);
    ui->statusBar->addPermanentWidget(mLineEditCoordinates);
    addSep();

    // Map scale
    ui->statusBar->addPermanentWidget(new QLabel("Map Scale:", ui->statusBar));
    mComboBoxMapScale = new QComboBox(ui->statusBar);
    mComboBoxMapScale->addItem("1:1");
    mComboBoxMapScale->setMinimumWidth(150);
    ui->statusBar->addPermanentWidget(mComboBoxMapScale);
    addSep();

    // CRS button
    ui->statusBar->addPermanentWidget(new QLabel("Coordinate Reference System:", ui->statusBar));
    mToolButtonCoordinateReferenceSystem = new QToolButton(ui->statusBar);
    mToolButtonCoordinateReferenceSystem->setIcon(QIcon(":/swmmvis/Globe"));
    mToolButtonCoordinateReferenceSystem->setText(QStringLiteral("EPSG:4326"));
    mToolButtonCoordinateReferenceSystem->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(mToolButtonCoordinateReferenceSystem, &QToolButton::clicked,
            this, &SWMMVis::onCRSButtonClicked);
    ui->statusBar->addPermanentWidget(mToolButtonCoordinateReferenceSystem);
}

void SWMMVis::initializeDockWidgets()
{
    initializeLayersDockWidget();
    initializeObjectBrowserDockWidget();
    initializeAttributePanelDockWidget();
    initializeSimulationStatusDockWidget();
    initializeMessageLogDockWidget();
}

void SWMMVis::initializeLayersDockWidget()
{
    // The .ui defines `dockWidgetSWMMLayers` with a placeholder QTreeView. Replace
    // the dock's central content with the LayerTreePanel widget — the panel
    // brings its own toolbar, tree, model, and context menu. The canvas binding
    // is set later by onActiveSubWindowChanged() each time the active project
    // changes (initially null → empty tree).
    mLayerTreePanel = new LayerTreePanel(nullptr, this);

    auto *contents = new QWidget(ui->dockWidgetSWMMLayers);
    auto *lay = new QVBoxLayout(contents);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(mLayerTreePanel);
    ui->dockWidgetSWMMLayers->setWidget(contents);

    // Right-click "Properties…" / double-click on a layer row → open dialog.
    connect(mLayerTreePanel, &LayerTreePanel::layerPropertiesRequested,
            this, [this](OpenSWMMVisLayer *layer) {
                if (!layer) return;
                LayerPropertiesDialog dlg(layer, this);
                dlg.exec();
                // Apply may have mutated visibility (Scene channel) or
                // opacity on a raster layer (Raster channel). Cheap to flag
                // both — the unused one is a no-op.
                if (auto *c = activeCanvas())
                    c->invalidate(MapCanvas::Raster | MapCanvas::Scene,
                                  QStringLiteral("layer-properties-apply"));
            });
}

void SWMMVis::initializeObjectBrowserDockWidget()
{
    mObjectBrowserPanel = new ObjectBrowserPanel(this);

    auto *dock = new QDockWidget(tr("Object Browser"), this);
    dock->setObjectName(QStringLiteral("dockWidgetObjectBrowser"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setWidget(mObjectBrowserPanel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    // Tabify with the Layers dock so they share screen real estate by default.
    if (ui->dockWidgetSWMMLayers)
        tabifyDockWidget(ui->dockWidgetSWMMLayers, dock);

    // Right-click "Plot Time Series…" → open the chart dialog with the
    // active project's results .out (auto-loaded after Run Simulation).
    connect(mObjectBrowserPanel, &ObjectBrowserPanel::plotTimeSeriesRequested,
            this, [this](const SWMMObjectRef &obj) {
                auto *pw = activeProjectWindow();
                if (!pw || !pw->canvas()) return;

                // Find the first SWMMResultsLayer attached to the canvas;
                // its file path is the .out we plot from.
                QString outPath;
                for (OpenSWMMVisLayer *l : pw->canvas()->layers())
                {
                    if (l->layerType() == OpenSWMMVisLayer::SWMMResultsLayer)
                    {
                        if (auto *rl = qobject_cast<SWMMResultsLayer *>(l))
                        {
                            outPath = rl->resultsFilePath();
                            break;
                        }
                    }
                }
                if (outPath.isEmpty())
                {
                    QMessageBox::information(this, tr("No results loaded"),
                        tr("Run a simulation first (toolbar's Execute button) "
                           "or add a SWMM Output (.out) layer."));
                    return;
                }

                auto *dlg = new TimeSeriesPlotDialog(outPath, obj, this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            });

    // Slice O — per-object visibility. Parent-header toggles propagate
    // down to all their children via objectsVisibilityChanged (one batch
    // call per group toggle = one canvas repaint, regardless of count).
    // Individual leaf toggles go through objectVisibilityChanged and
    // never back-propagate to the parent header.
    connect(mObjectBrowserPanel, &ObjectBrowserPanel::objectVisibilityChanged,
            this, [this](const SWMMObjectRef &obj, bool visible) {
                if (auto *pw = activeProjectWindow())
                {
                    if (auto *layer = pw->modelLayer())
                        layer->setObjectVisible(obj.name, visible);
                }
            });
    connect(mObjectBrowserPanel, &ObjectBrowserPanel::objectsVisibilityChanged,
            this, [this](const QStringList &names, bool visible) {
                if (auto *pw = activeProjectWindow())
                {
                    if (auto *layer = pw->modelLayer())
                        layer->setObjectsVisible(names, visible);
                }
            });
}

void SWMMVis::initializeAttributePanelDockWidget()
{
    mAttributePanel = new AttributePanel(this);
    // QMainWindow::saveState() requires every QDockWidget to have a stable
    // objectName so its position can be persisted across sessions.
    mAttributePanel->setObjectName(QStringLiteral("dockWidgetAttributePanel"));
    addDockWidget(Qt::RightDockWidgetArea, mAttributePanel);
}

void SWMMVis::initializeSimulationStatusDockWidget()
{
    mSimStatusModel = new SimulationStatusModel(this);

    auto *view = ui->treeViewSimulationStatus;
    view->setModel(mSimStatusModel);
    view->setUniformRowHeights(true);
    view->setAlternatingRowColors(true);
    view->header()->setStretchLastSection(false);

    // Give Name and Status sensible widths; let Duration stretch.
    view->setColumnWidth(SimulationStatusModel::ColName,       200);
    view->setColumnWidth(SimulationStatusModel::ColStatus,      80);
    view->setColumnWidth(SimulationStatusModel::ColProgress,    70);
    view->setColumnWidth(SimulationStatusModel::ColSimTime,     80);
    view->setColumnWidth(SimulationStatusModel::ColRunoffErr,   90);
    view->setColumnWidth(SimulationStatusModel::ColRoutingErr,  90);
    view->header()->setSectionResizeMode(SimulationStatusModel::ColDuration,
                                         QHeaderView::Stretch);
}

void SWMMVis::initializeMessageLogDockWidget()
{
    mLogMessagesModel = new QStandardItemModel(0, 3, this);
    mLogMessagesModel->setHorizontalHeaderLabels({"Time", "Type", "Message"});
    ui->treeViewMessageLogs->setModel(mLogMessagesModel);
}

void SWMMVis::initializeMenus()
{
    connect(ui->actionNew,    &QAction::triggered, this, &SWMMVis::onNewProject);
    connect(ui->actionOpen,   &QAction::triggered, this, [this]{ onOpenProject(); });
    connect(ui->actionSave,   &QAction::triggered, this, &SWMMVis::onSaveProject);
    connect(ui->actionSaveAs, &QAction::triggered, this, &SWMMVis::onSaveProjectAs);
    connect(ui->actionAbout,  &QAction::triggered, this, &SWMMVis::onAbout);

    connect(ui->menuOpenRecent, &QMenu::triggered, this, &SWMMVis::onOpenRecentFile);

    connect(ui->actionShowWelcome, &QAction::triggered, this, &SWMMVis::onShowWelcomeScreen);

    connect(ui->actionAddWMSData,    &QAction::triggered, this, &SWMMVis::onAddWMSLayer);
    connect(ui->actionAddBasemap,    &QAction::triggered, this, &SWMMVis::onAddWMTSLayer);
    connect(ui->actionAddVectorData, &QAction::triggered, this, &SWMMVis::onAddVectorLayer);
    connect(ui->actionAddRasterData, &QAction::triggered, this, &SWMMVis::onAddRasterLayer);
    connect(ui->actionAddSWMMOutput, &QAction::triggered, this, &SWMMVis::onAddSWMMResultsLayer);
    connect(ui->actionExecute,       &QAction::triggered, this, &SWMMVis::onRunSimulation);
    connect(ui->actionOptions,       &QAction::triggered, this, &SWMMVis::onSimulationOptions);

    // Tools → Simulation Options… / Set Project CRS… (added programmatically
    // — the .ui's menuTools is empty by default; this avoids touching the
    // .ui resource for these entries).
    if (ui->menuTools)
    {
        auto *actSimOpts = ui->menuTools->addAction(tr("Simulation Options…"));
        actSimOpts->setToolTip(tr("Edit per-project SWMM simulation options"));
        connect(actSimOpts, &QAction::triggered, this, &SWMMVis::onSimulationOptions);

        auto *actSetCRS = ui->menuTools->addAction(
            QIcon(QStringLiteral(":/swmmvis/Globe")),
            tr("Set Project CRS…"));
        actSetCRS->setToolTip(tr("Choose or change the project's coordinate reference system"));
        connect(actSetCRS, &QAction::triggered, this, &SWMMVis::onCRSButtonClicked);
    }



    // Wire the .ui's pre-existing Add* actions to the Add-node tools. The
    // node types match SWMM_NodeType (0=Junction, 1=Outfall, 2=Storage,
    // 3=Divider). actionAddPipe / polyline / polygon remain unwired until
    // MapToolAddLink / AddSubcatch ship.
    if (ui->actionAddJunction)
        connect(ui->actionAddJunction, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddJunctionTool();
        });
    if (ui->actionAddOutfall)
        connect(ui->actionAddOutfall, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddOutfallTool();
        });
    if (ui->actionAddStorage)
        connect(ui->actionAddStorage, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddStorageTool();
        });
    if (ui->actionAddFlowDivider)
        connect(ui->actionAddFlowDivider, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddDividerTool();
        });

    // ---- Window menu (programmatic — .ui has no menuWindow) ----
    // Standard macOS layout: Minimize / Zoom / separator / dynamic list of
    // open project windows (checkmark on the active one) / separator /
    // Bring All to Front.
    mMenuWindow = new QMenu(tr("&Window"), this);

    mActionWindowMinimize = mMenuWindow->addAction(tr("&Minimize"));
    mActionWindowMinimize->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    // Minimize always targets the main window — MDI sub-windows (Welcome
    // + project documents) are deliberately not minimizable.
    connect(mActionWindowMinimize, &QAction::triggered, this, [this]() {
        showMinimized();
    });

    mActionWindowZoom = mMenuWindow->addAction(tr("&Zoom"));
    // Zoom targets the main window too — MDI sub-windows don't have a
    // maximize affordance in this app (their sizing is tabbed-view
    // driven, not user-resizable).
    connect(mActionWindowZoom, &QAction::triggered, this, [this]() {
        if (isMaximized()) showNormal();
        else               showMaximized();
    });

    mMenuWindow->addSeparator();
    // The dynamic window list is appended by rebuildWindowMenu(). The
    // separator + Bring All to Front are appended there too so the whole
    // tail-of-menu can be cleared + rebuilt in one pass.

    if (ui->menuHelp)
        ui->menubarMain->insertMenu(ui->menuHelp->menuAction(), mMenuWindow);
    else
        ui->menubarMain->addMenu(mMenuWindow);

    // Keep the menu in sync with the MDI area. subWindowActivated fires on
    // open / close / focus-change so a single connection covers all cases.
    connect(ui->mdiAreaCentral, &QMdiArea::subWindowActivated,
            this, &SWMMVis::rebuildWindowMenu);

    rebuildWindowMenu();

    // Canvas signals are wired per-window in onActiveSubWindowChanged()
}

void SWMMVis::initializeSettings()
{
    onLogMessage("Reading settings");
    onSetProgressBarBusy(true);

    mSettings.beginGroup("SWMMVis::MainWindow");
    restoreState(mSettings.value("SWMMVis::WindowState",    saveState()).toByteArray());
    setWindowState(static_cast<Qt::WindowState>(
        mSettings.value("SWMMVis::WindowStateEnum", static_cast<int>(windowState())).toInt()));
    setGeometry(mSettings.value("SWMMVis::Geometry", geometry()).toRect());
    mRecentFiles = mSettings.value("SWMMVis::RecentFiles", mRecentFiles).toStringList();
    mSettings.endGroup();

    onRecentFilesSizeChanged();
    onLogMessage("Finished reading settings");
    onSetProgressBarBusy(false);
}

void SWMMVis::saveSettings()
{
    mSettings.beginGroup("SWMMVis::MainWindow");
    mSettings.setValue("SWMMVis::WindowState",     saveState());
    mSettings.setValue("SWMMVis::WindowStateEnum", static_cast<int>(windowState()));
    mSettings.setValue("SWMMVis::Geometry",        geometry());
    mSettings.setValue("SWMMVis::RecentFiles",     mRecentFiles);
    mSettings.endGroup();
}

void SWMMVis::clearPreviousWelcomeScreenElements()
{
    // The .ui authors a QWidget viewport ("widgetContentsRecentFiles") with
    // a QVBoxLayout and a trailing stretch inside scrollAreaRecentFiles, so
    // all we need here is to strip any previously-generated recent-file
    // buttons. onRecentFilesSizeChanged re-populates the layout from
    // mRecentFiles and re-appends the trailing stretch.
    if (auto *area = ui->frameRecentFiles->findChild<QScrollArea *>())
    {
        if (QWidget *content = area->widget(); content && content->layout())
        {
            QLayout *lay = content->layout();
            while (QLayoutItem *item = lay->takeAt(0))
            {
                if (QWidget *w = item->widget()) w->deleteLater();
                delete item;
            }
        }
    }
}

// ── Close event ───────────────────────────────────────────────────────────

void SWMMVis::closeEvent(QCloseEvent *event)
{
    // Ask each dirty project sub-window to confirm. SWMMVisProjectWindow::closeEvent
    // shows the Save? prompt and ignores the event if the user cancels.
    const QList<QMdiSubWindow*> windows = ui->mdiAreaCentral->subWindowList();
    for (QMdiSubWindow *w : windows)
    {
        if (auto *pw = qobject_cast<SWMMVisProjectWindow*>(w))
        {
            if (pw->hasChanges())
            {
                if (!pw->close())
                {
                    event->ignore();
                    return;
                }
            }
        }
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}

bool SWMMVis::eventFilter(QObject *watched, QEvent *event)
{
    // Intercept the welcome sub-window's close so the tab is torn down
    // entirely rather than leaving an empty tab shell. The inner
    // welcomeWidget is preserved for later re-open via
    // Help → Show Welcome Screen.
    if (event->type() == QEvent::Close)
    {
        if (auto *sub = qobject_cast<QMdiSubWindow *>(watched);
            sub && ui && ui->welcomeWidget && sub->widget() == ui->welcomeWidget)
        {
            qDebug() << "[welcome] eventFilter: Close event on welcome sub";
            removeWelcomeSubWindow();
            event->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Welcome sub-window lifecycle helpers
// ---------------------------------------------------------------------------

QMdiSubWindow *SWMMVis::welcomeSubWindow() const
{
    if (!ui || !ui->welcomeWidget || !ui->mdiAreaCentral) return nullptr;
    for (QMdiSubWindow *sub : ui->mdiAreaCentral->subWindowList())
        if (sub->widget() == ui->welcomeWidget)
            return sub;
    return nullptr;
}

void SWMMVis::removeWelcomeSubWindow()
{
    QMdiSubWindow *sub = welcomeSubWindow();
    if (!sub) return;

    // removeSubWindow() reparents the inner widget to nullptr — if we don't
    // immediately re-parent it somewhere, it briefly becomes a top-level
    // window and may flash on screen. Stashing it as a hidden child of the
    // main window keeps it alive and invisible until re-added.
    ui->mdiAreaCentral->removeSubWindow(sub);
    if (ui->welcomeWidget)
    {
        ui->welcomeWidget->setParent(this);
        ui->welcomeWidget->hide();
    }
    sub->deleteLater();
}

// ── Slots ─────────────────────────────────────────────────────────────────

void SWMMVis::onNewProject()
{
    // For now: bring the Welcome tab to the front so the user can choose Open or
    // an example. A true blank-project workflow lands with the engine model-builder
    // integration in a later slice.
    onShowWelcomeScreen();
    onLogMessage(tr("New Project: opens the Welcome tab. A blank-project workflow ships in a later slice."));
}

void SWMMVis::onSaveProject()
{
    auto *pw = activeProjectWindow();
    if (!pw)
    {
        onLogMessage(tr("Save: no active project."), OpenSWMMVisLogMessage::LogMessageType::Warning);
        return;
    }
    QString err;
    if (!pw->save(&err))
    {
        // No path yet → fall through to Save As
        onSaveProjectAs();
        if (!err.isEmpty())
            onLogMessage(err, OpenSWMMVisLogMessage::LogMessageType::Information);
        return;
    }
    onLogMessage(tr("Saved: %1").arg(pw->modelLayer()->modelFilePath()));
}

void SWMMVis::onSaveProjectAs()
{
    auto *pw = activeProjectWindow();
    if (!pw)
    {
        onLogMessage(tr("Save As: no active project."), OpenSWMMVisLogMessage::LogMessageType::Warning);
        return;
    }
    const QString suggested = pw->modelLayer() ? pw->modelLayer()->modelFilePath() : QString();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save SWMM Model As"),
        suggested.isEmpty() ? QDir::homePath() : suggested,
        tr("SWMM Input Files (*.inp)"));
    if (path.isEmpty()) return;

    QString err;
    if (!pw->saveAs(path, &err))
    {
        QMessageBox::critical(this, tr("Save As failed"), err);
        return;
    }
    mRecentFiles.removeAll(path);
    mRecentFiles.prepend(path);
    onRecentFilesSizeChanged();
    saveSettings();
    onLogMessage(tr("Saved As: %1").arg(path));
}

void SWMMVis::onClearRecentFiles()
{
    mRecentFiles.clear();
    onRecentFilesSizeChanged();
    saveSettings();
    onLogMessage(tr("Recent files cleared."));
}

void SWMMVis::onShowWelcomeScreen()
{
    if (!ui->welcomeWidget || !ui->mdiAreaCentral) return;

    // Re-add the welcome widget as an MDI sub-window if it was previously
    // closed (we reparented it to this main window on close to keep it
    // alive). The new sub-window is wired the same way as the one setupUi()
    // originally created — close tears it down again cleanly.
    QMdiSubWindow *sub = welcomeSubWindow();
    if (!sub)
    {
        ui->welcomeWidget->show();
        auto *ws = new WelcomeSubWindow();
        ws->setWidget(ui->welcomeWidget);
        ws->onClose = [this](QCloseEvent *) { removeWelcomeSubWindow(); };
        ui->mdiAreaCentral->addSubWindow(ws);
        sub = ws;
    }
    sub->show();
    ui->mdiAreaCentral->setActiveSubWindow(sub);
}
void SWMMVis::onClose() {}

void SWMMVis::onFlowUnitsChanged(int comboIndex)
{
    if (comboIndex < 0) return;
    auto *pw = activeProjectWindow();
    if (!pw || !pw->unitSystem())
        return;
    const auto units = static_cast<swmm_FlowUnitsProperty>(
        mComboBoxFlowUnits->itemData(comboIndex).toInt());
    SWMM_Engine engine = pw->modelLayer() ? pw->modelLayer()->engine() : nullptr;
    pw->unitSystem()->setFlowUnits(units, engine);
    pw->setHasChanges(true);
}

void SWMMVis::onOpenProject(const QString &path)
{
    QString filePath = path;
    if (filePath.isEmpty())
    {
        filePath = QFileDialog::getOpenFileName(
            this,
            tr("Open SWMM Model or Project"),
            mRecentFiles.isEmpty() ? QDir::homePath()
                                   : QFileInfo(mRecentFiles.first()).absolutePath(),
            tr("SWMM Files (*.inp *.oswp);;SWMM Input Files (*.inp);;SWMM Project Files (*.oswp);;All Files (*)"));
    }
    if (filePath.isEmpty())
        return;

    if (filePath.endsWith(QStringLiteral(".oswp"), Qt::CaseInsensitive))
        openProjectFile(filePath);
    else
        openSingleINP(filePath);
}

void SWMMVis::openSingleINP(const QString &filePath)
{
    // Create a new project window for this INP file
    auto *window = new SWMMVisProjectWindow(mProject, filePath, ui->mdiAreaCentral);
    connect(window, &SWMMVisProjectWindow::modelLoaded,
            this, &SWMMVis::onModelLoaded);
    connect(window, &SWMMVisProjectWindow::modelLoadError,
            this, &SWMMVis::onModelLoadError);

    // Clear the cached active-window pointer if this window is the one being
    // destroyed — otherwise activeProjectWindow() would return a dangling ptr
    // and the next focus event would try to rebind to dead memory. Routes
    // through onActiveSubWindowChanged so the "all closed" branch fires
    // exactly once when the last project window goes away.
    connect(window, &QObject::destroyed, this, [this](QObject *obj) {
        if (mActiveProjectWindow == static_cast<SWMMVisProjectWindow *>(obj))
        {
            mActiveProjectWindow = nullptr;
            onActiveSubWindowChanged(ui->mdiAreaCentral->activeSubWindow());
        }
    });

    // Keep the Window menu label in sync with the dirty `*` marker.
    connect(window, &QWidget::windowTitleChanged,
            this, &SWMMVis::rebuildWindowMenu);

    ui->mdiAreaCentral->addSubWindow(window);

    QList<QString> warnings, errors;
    onSetProgressBarBusy(true);
    const bool ok = window->loadModel(warnings, errors);
    onSetProgressBarBusy(false);

    for (const QString &w : warnings)
        onLogMessage(w, OpenSWMMVisLogMessage::LogMessageType::Warning);
    for (const QString &e : errors)
        onLogMessage(e, OpenSWMMVisLogMessage::LogMessageType::Error);

    if (ok)
    {
        mRecentFiles.removeAll(filePath);
        mRecentFiles.prepend(filePath);
        onRecentFilesSizeChanged();
        saveSettings();

        setWindowTitle(QStringLiteral("OpenSWMM — %1").arg(QFileInfo(filePath).baseName()));
        window->show();
        ui->mdiAreaCentral->setActiveSubWindow(window);

        // Re-run the activation handler now that the engine is loaded.
        // addSubWindow() can fire subWindowActivated before loadModel(),
        // leaving status-bar widgets showing pre-load defaults.
        onActiveSubWindowChanged(window);
    }
    else
    {
        window->close();
    }
}

void SWMMVis::openProjectFile(const QString &oswpPath)
{
    QFile f(oswpPath);
    if (!f.open(QIODevice::ReadOnly))
    {
        onLogMessage(QStringLiteral("Cannot open project file: %1").arg(oswpPath),
                     OpenSWMMVisLogMessage::LogMessageType::Error);
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull())
    {
        onLogMessage(QStringLiteral("Invalid project file: %1").arg(err.errorString()),
                     OpenSWMMVisLogMessage::LogMessageType::Error);
        return;
    }

    mRecentFiles.removeAll(oswpPath);
    mRecentFiles.prepend(oswpPath);
    onRecentFilesSizeChanged();
    saveSettings();

    const QJsonArray layers = doc[QStringLiteral("layers")].toArray();
    for (const QJsonValue &v : layers)
    {
        QString inpPath = v[QStringLiteral("path")].toString();
        if (!inpPath.isEmpty())
            openSingleINP(inpPath);
    }
}

void SWMMVis::onOpenRecentFile()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) return;
    const QString path = action->data().toString();
    // Skip non-recent-file entries in the same menu (separator-then-Clear
    // has its own connection and empty data).
    if (path.isEmpty()) return;
    onOpenProject(path);
}

void SWMMVis::onActiveSubWindowChanged(QMdiSubWindow *window)
{
    auto *pw = qobject_cast<SWMMVisProjectWindow *>(window);

    // QMdiArea fires subWindowActivated(nullptr) any time focus moves outside
    // the MDI area (e.g. clicking a dock widget) — not just when projects
    // close. Treat null as "all project windows really gone" only if there
    // are no project windows left in the area; otherwise keep the current
    // bindings intact so the Layers dock and status-bar widgets persist
    // through transient focus changes.
    if (!pw)
    {
        bool anyProjectStillOpen = false;
        for (QMdiSubWindow *sw : ui->mdiAreaCentral->subWindowList())
        {
            if (qobject_cast<SWMMVisProjectWindow *>(sw))
            {
                anyProjectStillOpen = true;
                break;
            }
        }
        if (anyProjectStillOpen)
            return;   // spurious null — keep existing bindings

        // Real "all projects closed" — clear bindings.
        UnitSystem::setActiveProject(nullptr);
        mCheckBoxLevelOffsetMode->setEnabled(false);
        if (mCheckBoxAutoLength) mCheckBoxAutoLength->setEnabled(false);
        mComboBoxFlowUnits->setEnabled(false);
        if (mLayerTreePanel)     mLayerTreePanel->setCanvas(nullptr);
        if (mObjectBrowserPanel) mObjectBrowserPanel->setProject(nullptr, nullptr, nullptr);
        if (mAttributePanel)     mAttributePanel->clear();
        mActiveProjectWindow = nullptr;
        return;
    }

    // Same project as before → no rebind needed (avoids redundant model
    // resets that flash the Layers dock empty for a frame).
    if (pw == mActiveProjectWindow)
        return;
    mActiveProjectWindow = pw;

    // Rebind canvas signals to the newly active canvas. UniqueConnection avoids
    // duplicating on repeated activation of the same window.
    connect(pw->canvas(), &MapCanvas::cursorPositionChanged,
            this, &SWMMVis::onCursorPositionChanged, Qt::UniqueConnection);
    connect(pw->canvas(), &MapCanvas::canvasSRSChanged,
            this, &SWMMVis::onCanvasSRSChanged, Qt::UniqueConnection);

    // Rebind the UnitSystem facade. The facade re-emits unitsChanged on
    // rebind, which refreshes label-bearing dialogs without per-callsite work.
    UnitSystem::setActiveProject(pw->unitSystem());

    // Sync flow units combo to the active project (block signals so this
    // doesn't fire onFlowUnitsChanged and write back to the engine).
    {
        QSignalBlocker b(mComboBoxFlowUnits);
        const int idx = mComboBoxFlowUnits->findData(
            static_cast<int>(pw->unitSystem()->flowUnits()));
        if (idx >= 0)
            mComboBoxFlowUnits->setCurrentIndex(idx);
        mComboBoxFlowUnits->setEnabled(true);
    }

    // Sync offset mode checkbox to the active project's LINK_OFFSETS.
    {
        QSignalBlocker b(mCheckBoxLevelOffsetMode);
        const bool elev = pw->isElevationOffsetMode();
        mCheckBoxLevelOffsetMode->setChecked(elev);
        mCheckBoxLevelOffsetMode->setText(elev ? QStringLiteral("Elevation")
                                               : QStringLiteral("Depth"));
        mCheckBoxLevelOffsetMode->setEnabled(true);
    }

    // Sync auto-length checkbox to the active project.
    if (mCheckBoxAutoLength)
    {
        QSignalBlocker b(mCheckBoxAutoLength);
        const bool on = pw->isAutoLengthEnabled();
        mCheckBoxAutoLength->setChecked(on);
        mCheckBoxAutoLength->setText(on ? QStringLiteral("On") : QStringLiteral("Off"));
        mCheckBoxAutoLength->setEnabled(true);
    }

    SpatialReferenceSystem *srs = pw->canvas()->canvasSRS();
    mToolButtonCoordinateReferenceSystem->setText(
        srs ? srs->toAuthority() : QStringLiteral("EPSG:4326"));

    // Rebind the Layers dock to this project's canvas so visibility toggles,
    // ordering, and layer additions reflect the focused tab.
    if (mLayerTreePanel)
        mLayerTreePanel->setCanvas(pw->canvas());

    // Rebind the Object Browser + Attribute Panel to this project's model
    // layer + selection bus + canvas (canvas powers Slice O's zoom-to-object).
    if (mObjectBrowserPanel)
        mObjectBrowserPanel->setProject(pw->modelLayer(),
                                        pw->selectionManager(),
                                        pw->canvas());
    if (mAttributePanel && pw->selectionManager())
    {
        // The attribute panel listens to selectionChanged via a per-tab
        // connection; re-wire on every tab switch using UniqueConnection so
        // bouncing between tabs doesn't stack duplicate connections.
        connect(pw->selectionManager(), &SelectionManager::selectionChanged,
                this,
                [this, pw](const QSet<SWMMObjectRef> &current,
                           const QSet<SWMMObjectRef> &,
                           const QSet<SWMMObjectRef> &) {
                    // Guard against the lambda firing for a now-stale project
                    // (re-tab during async work). mActiveProjectWindow is the
                    // canonical "current focus" pointer.
                    if (!mAttributePanel || pw != mActiveProjectWindow) return;
                    if (current.isEmpty())
                    {
                        mAttributePanel->clear();
                        return;
                    }
                    // Render the first selected object's identify-style
                    // attributes. Phase 3's per-type Property Editors are a
                    // future slice; for now this gives users immediate
                    // read-only visibility into the picked object.
                    auto *layer = pw->modelLayer();
                    if (!layer) return;
                    const SWMMObjectRef first = *current.constBegin();
                    const QVariantMap attrs = layer->identifyByName(first.name);
                    if (!attrs.isEmpty())
                    {
                        IdentifyResult r;
                        r.layerName = layer->name();
                        r.features  = { attrs };
                        mAttributePanel->showIdentifyResults({r});
                    }
                    else
                    {
                        mAttributePanel->clear();
                    }
                },
                Qt::UniqueConnection);
    }
}

void SWMMVis::onModelLoaded()
{
    auto *pw = qobject_cast<SWMMVisProjectWindow *>(sender()->parent());
    const QString path = pw ? pw->modelLayer()->modelFilePath() : QString();
    onLogMessage(path.isEmpty() ? QStringLiteral("Model loaded successfully.")
                                : QStringLiteral("Model loaded: %1").arg(path));

    // If this is the active project, populate the Object Browser now that
    // the engine has objects to enumerate. (setProject was called earlier
    // before the engine was ready, so the tree is empty.)
    if (pw && pw == mActiveProjectWindow && mObjectBrowserPanel)
        mObjectBrowserPanel->setProject(pw->modelLayer(),
                                        pw->selectionManager(),
                                        pw->canvas());
}

void SWMMVis::onModelLoadError(const QString &msg)
{
    onLogMessage(msg, OpenSWMMVisLogMessage::LogMessageType::Error);
    QMessageBox::warning(this, tr("Model Load Error"), msg);
}

void SWMMVis::rebuildWindowMenu()
{
    if (!mMenuWindow) return;

    // Strip the dynamic tail (every action after the 3rd — Minimize / Zoom /
    // separator are permanent).
    const QList<QAction *> all = mMenuWindow->actions();
    for (int i = 3; i < all.size(); ++i)
        mMenuWindow->removeAction(all[i]);

    QMdiSubWindow *active = ui->mdiAreaCentral->activeSubWindow();
    const QList<QMdiSubWindow *> subs = ui->mdiAreaCentral->subWindowList();

    // Window list entries (project sub-windows only — hides the welcome
    // backdrop, which isn't a project).
    int listedCount = 0;
    for (QMdiSubWindow *sub : subs)
    {
        auto *pw = qobject_cast<SWMMVisProjectWindow *>(sub);
        if (!pw) continue;

        QAction *act = mMenuWindow->addAction(sub->windowTitle());
        act->setCheckable(true);
        act->setChecked(sub == active);
        connect(act, &QAction::triggered, this, [this, pw]() {
            ui->mdiAreaCentral->setActiveSubWindow(pw);
            pw->raise();
            pw->setFocus(Qt::ActiveWindowFocusReason);
        });
        ++listedCount;
    }

    if (listedCount > 0)
        mMenuWindow->addSeparator();

    mActionWindowBringAllToFront = mMenuWindow->addAction(tr("Bring All to Front"));
    connect(mActionWindowBringAllToFront, &QAction::triggered, this, [this]() {
        // Raise every project sub-window, then the main window last so it
        // stays on top and the app takes focus.
        for (QMdiSubWindow *sub : ui->mdiAreaCentral->subWindowList())
            if (qobject_cast<SWMMVisProjectWindow *>(sub))
                sub->raise();
        this->raise();
        this->activateWindow();
    });
}

void SWMMVis::onRecentFilesSizeChanged()
{
    while (mRecentFiles.size() > 20)
        mRecentFiles.removeLast();

    ui->menuOpenRecent->clear();
    const bool hasFiles = !mRecentFiles.isEmpty();
    ui->menuOpenRecent->setEnabled(hasFiles);
    ui->commandLinkButtonClearRecentFiles->setEnabled(hasFiles);

    // Menu entries — label is the short file name for readability, full path
    // lives on the tooltip + the action's data() payload that the dispatcher
    // reads to reopen the file.
    for (int i = 0; i < mRecentFiles.size(); ++i)
    {
        QFileInfo fi(mRecentFiles[i]);
        QAction *action = new QAction(fi.fileName(), ui->menuOpenRecent);
        action->setToolTip(fi.absoluteFilePath());
        action->setData(fi.absoluteFilePath());
        ui->menuOpenRecent->addAction(action);
    }

    // Standard high-quality-software pattern: separator + Clear at the
    // bottom of the recent-files submenu so the user can purge the list
    // without leaving the menu. The Clear entry has empty data so the
    // generic onOpenRecentFile slot ignores it; we connect it directly
    // to onClearRecentFiles.
    if (hasFiles)
    {
        ui->menuOpenRecent->addSeparator();
        QAction *clearAct = new QAction(
            QIcon(QStringLiteral(":/swmmvis/Clear")),
            tr("Clear Recent Files"), ui->menuOpenRecent);
        clearAct->setToolTip(tr("Remove all entries from the recent files list"));
        // Empty data so the menu-level onOpenRecentFile handler skips it
        // (it tries to open whatever is in action->data().toString()).
        connect(clearAct, &QAction::triggered, this, &SWMMVis::onClearRecentFiles);
        ui->menuOpenRecent->addAction(clearAct);
    }

    // Visual separator above the welcome page's Clear Recent Files button
    // (high-quality-software pattern: divider between the recent-files list
    // and the destructive Clear action so the user can't accidentally hit
    // Clear thinking it's another file). Inserted programmatically in the
    // parent layout that holds frameRecentFiles + commandLinkButtonClearRecentFiles
    // because the .ui's QFrame::HLine-on-demand isn't easy to add inline.
    if (auto *clearBtn = ui->commandLinkButtonClearRecentFiles)
    {
        if (auto *parentLay = qobject_cast<QVBoxLayout *>(
                clearBtn->parentWidget() ? clearBtn->parentWidget()->layout() : nullptr))
        {
            // Skip if a separator already exists (re-init safety).
            const int clearIdx = parentLay->indexOf(clearBtn);
            const QLayoutItem *prevItem = clearIdx > 0 ? parentLay->itemAt(clearIdx - 1) : nullptr;
            const auto *prevFrame = prevItem ? qobject_cast<QFrame *>(prevItem->widget()) : nullptr;
            const bool alreadyHasSeparator =
                prevFrame && prevFrame->frameShape() == QFrame::HLine;
            if (!alreadyHasSeparator && clearIdx >= 0)
            {
                auto *sep = new QFrame(clearBtn->parentWidget());
                sep->setFrameShape(QFrame::HLine);
                sep->setFrameShadow(QFrame::Sunken);
                parentLay->insertWidget(clearIdx, sep);
            }
        }
    }

    // Welcome panel — replace existing buttons with current list
    if (auto *area = ui->frameRecentFiles->findChild<QScrollArea *>())
    {
        QWidget *content = area->widget();
        if (content && content->layout())
        {
            // Remove all children except a trailing stretch
            QLayout *lay = content->layout();
            while (QLayoutItem *item = lay->takeAt(0))
            {
                if (QWidget *w = item->widget()) w->deleteLater();
                delete item;
            }
            for (const QString &path : mRecentFiles)
            {
                QFileInfo fi(path);
                auto *btn = new QCommandLinkButton(fi.fileName(), content);
                btn->setDescription(fi.absolutePath());
                btn->setIcon(QIcon(QStringLiteral(":/swmmvis/Open")));
                btn->setMaximumHeight(50);
                connect(btn, &QCommandLinkButton::clicked, this, [this, path]{ onOpenProject(path); });
                lay->addWidget(btn);
            }
            qobject_cast<QVBoxLayout*>(lay)->addStretch(1);
        }
    }
}

void SWMMVis::onSetProgressBarBusy(bool busy)
{
    mProgressBar->setRange(0, 0);
    mProgressBar->setValue(0);
    mProgressBar->setVisible(busy);
}

void SWMMVis::onAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void SWMMVis::onSimulationOptions()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine())
    {
        onLogMessage(tr("Open a SWMM project first to edit simulation options."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }

    SimulationOptionsDialog dlg(pw->modelLayer()->engine(), pw->modelLayer(), this);
    if (dlg.exec() == QDialog::Accepted && dlg.wroteAnyChanges())
    {
        pw->setHasChanges(true);
        // Some option changes (e.g. FLOW_UNITS via this dialog later) want
        // status-bar widgets re-synced — re-run the activate handler.
        onActiveSubWindowChanged(pw);
    }
}

void SWMMVis::onRunSimulation()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine())
    {
        onLogMessage(tr("Open a SWMM project first to run a simulation."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }

    QString inpPath = pw->modelLayer()->modelFilePath();
    if (inpPath.isEmpty())
    {
        onLogMessage(tr("Save the project before running — Run uses the .inp on disk."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }

    // Auto-save dirty edits so the engine sees the latest state.
    if (pw->hasChanges())
    {
        QString err;
        if (!pw->save(&err))
        {
            QMessageBox::critical(this, tr("Run failed"),
                tr("Could not save the project before running:\n%1").arg(err));
            return;
        }
        onLogMessage(tr("Auto-saved before running."));
    }

    // Derive sibling .rpt and .out paths.
    QFileInfo fi(inpPath);
    QString base    = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
    QString rptPath = base + QStringLiteral(".rpt");
    QString outPath = base + QStringLiteral(".out");

    // Register the job in the status model and show the dock.
    const QString instanceName = fi.fileName();
    const int jobId = mSimStatusModel->addJob(instanceName, inpPath);
    ui->dockWidgetSimulationStatus->show();
    ui->dockWidgetSimulationStatus->raise();

    onLogMessage(tr("Running simulation: %1").arg(instanceName));
    onSetProgressBarBusy(true);

    // Create runner; wire signals → model; runner deletes itself after finish.
    auto *runner = new SimulationRunner(jobId, instanceName, inpPath, rptPath, outPath, this);

    connect(runner, &SimulationRunner::progressChanged,
            mSimStatusModel, &SimulationStatusModel::updateProgress);

    connect(runner, &SimulationRunner::warningReceived,
            mSimStatusModel, &SimulationStatusModel::addWarning);

    QPointer<SWMMVis> self(this);
    QPointer<SWMMVisProjectWindow> pwGuard(pw);
    QString outPathCopy  = outPath;

    connect(runner, &SimulationRunner::finished, this,
            [self, runner, pwGuard, outPathCopy, instanceName]
            (int finishedJobId, bool success, int errCode, QString errMsg,
             double runoffFrac, double routingFrac) {
                if (!self) return;
                self->mSimStatusModel->finishJob(finishedJobId, success, errCode,
                                                 errMsg, runoffFrac, routingFrac);
                self->onSetProgressBarBusy(false);

                if (!success) {
                    self->onLogMessage(
                        tr("Simulation failed (code %1): %2 — %3")
                            .arg(errCode).arg(instanceName).arg(errMsg),
                        OpenSWMMVisLogMessage::Error);
                } else {
                    self->onLogMessage(
                        tr("Simulation finished. Results: %1").arg(outPathCopy));
                    // Auto-load the .out as a SWMMResultsLayer attached to this
                    // project's canvas so subsequent plot/inspect actions find it.
                    if (pwGuard && pwGuard->canvas() && pwGuard->modelLayer()) {
                        auto *rl = new SWMMResultsLayer(outPathCopy,
                                                        pwGuard->modelLayer());
                        pwGuard->canvas()->addLayer(rl, true);
                    }
                }
                runner->deleteLater();
            });

    runner->start();
}

void SWMMVis::onPlotTimeSeries()
{
    // Wired in Slice M-2 below.
}

void SWMMVis::onCursorPositionChanged(double mapX, double mapY)
{
    mLineEditCoordinates->setText(
        QStringLiteral("%1, %2").arg(mapX, 0, 'f', 6).arg(mapY, 0, 'f', 6));
}

void SWMMVis::onCanvasSRSChanged(SpatialReferenceSystem *srs)
{
    mToolButtonCoordinateReferenceSystem->setText(
        srs ? srs->toAuthority() : QStringLiteral("Unknown"));
}

void SWMMVis::onCRSButtonClicked()
{
    MapCanvas *c = activeCanvas();
    if (!c) return;

    SpatialReferenceSystem *currentSRS = c->canvasSRS();

    CRSSelectionDialog picker(this);
    picker.setCurrentCRS(currentSRS);
    if (picker.exec() != QDialog::Accepted)
        return;

    SpatialReferenceSystem *newSRS = picker.selectedSRS();
    if (!newSRS)
        return;

    // No project loaded: just swap the canvas display CRS — there are no
    // stored coordinates to reproject.
    auto *pw = activeProjectWindow();
    SWMMModelLayer *layer = pw ? pw->modelLayer() : nullptr;
    const bool hasModel = layer && layer->engine();

    if (!hasModel)
    {
        c->setCanvasSRS(newSRS, true);
        return;
    }

    // Same CRS as current → no-op.
    if (currentSRS && newSRS->toAuthority() == currentSRS->toAuthority()
        && !currentSRS->toAuthority().isEmpty()
        && currentSRS->toAuthority() != QStringLiteral("Local"))
    {
        // Still rebind in case the user picked the same authority deliberately.
        c->setCanvasSRS(newSRS, true);
        return;
    }

    // Prompt: Reproject vs Re-render vs Cancel.
    SpatialReferenceSystem *modelSRS = layer->srs();
    const bool sourceIsLocal = !modelSRS
        || modelSRS->toAuthority() == QStringLiteral("Local")
        || modelSRS->toAuthority().isEmpty();

    CRSChangeDialog dlg(modelSRS ? modelSRS->toAuthority() : QStringLiteral("(none)"),
                        newSRS->toAuthority(), sourceIsLocal, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    switch (dlg.choice())
    {
        case CRSChangeDialog::RenderOnly:
            // No mutation — canvas does on-the-fly reprojection.
            c->setCanvasSRS(newSRS, true);
            break;

        case CRSChangeDialog::Reproject:
        {
            CRSReproject::Stats stats;
            QString err;
            const bool ok = CRSReproject::reprojectModel(
                layer->engine(), *modelSRS, *newSRS, &stats, &err);
            if (!ok)
            {
                QMessageBox::warning(this, tr("Reproject failed"),
                    err.isEmpty() ? tr("No coordinates were transformed.") : err);
                return;
            }

            // Adopt the new CRS as both the layer's stored CRS and the canvas CRS.
            // The layer takes ownership via setSRS(srs, true). Canvas needs its
            // own copy because each MapCanvas owns its SRS.
            layer->setSRS(new SpatialReferenceSystem(*newSRS, layer), true);
            c->setCanvasSRS(new SpatialReferenceSystem(*newSRS, c), true);

            // Force a geometry refresh — the cached vertices in SWMMModelLayer
            // are now stale relative to the engine's transformed coordinates.
            // Reproject touches every channel: vector geometry (Scene), basemap
            // tile composite (Raster), and the implicit extent rebuild from the
            // following zoomToFullExtent.
            layer->reloadGeometry();
            c->invalidate(MapCanvas::Raster | MapCanvas::Scene,
                          QStringLiteral("crs-reproject"));
            c->zoomToFullExtent();

            pw->setHasChanges(true);
            onLogMessage(tr("Reprojected model: %1 nodes, %2 link vertices, "
                            "%3 polygon vertices.")
                              .arg(stats.nodes)
                              .arg(stats.linkVertices)
                              .arg(stats.polygonVerts));
            break;
        }

        case CRSChangeDialog::Cancel:
        default:
            break;
    }
}

void SWMMVis::onSetLayerCRS()
{
    auto *pw = activeProjectWindow();
    if (!pw) return;
    SWMMModelLayer *layer = pw->modelLayer();
    if (!layer) return;

    CRSSelectionDialog dlg(this);
    dlg.setCurrentCRS(layer->srs());
    if (dlg.exec() != QDialog::Accepted)
        return;

    SpatialReferenceSystem *srs = dlg.selectedSRS();
    if (!srs) return;

    layer->setSRS(srs, true);
    // Rebuild the reprojection transform and redraw — vector items need
    // re-positioning under the new transform; raster basemaps do too.
    layer->onCanvasCRSChanged(pw->canvas()->canvasSRS());
    pw->canvas()->invalidate(MapCanvas::Raster | MapCanvas::Scene,
                             QStringLiteral("set-layer-crs"));
}

void SWMMVis::onAddWMSLayer()
{
    WMSConnectionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    WMSLayer *layer = dlg.createLayer(nullptr);
    if (!layer) return;

    if (MapCanvas *c = activeCanvas())
        c->addLayer(layer, true);
    onLogMessage(QStringLiteral("Added WMS layer: %1").arg(layer->name()));
}

void SWMMVis::onAddWMTSLayer()
{
    WMTSConnectionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    WMTSLayer *layer = dlg.createLayer(nullptr);
    if (!layer) return;

    if (MapCanvas *c = activeCanvas())
        c->addLayer(layer, true);
    onLogMessage(QStringLiteral("Added WMTS layer: %1").arg(layer->name()));
}

void SWMMVis::onAddVectorLayer()
{
    MapCanvas *c = activeCanvas();
    if (!c)
    {
        onLogMessage(tr("Open a project first to add layers."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add Vector Layer"),
        mRecentFiles.isEmpty() ? QDir::homePath()
                               : QFileInfo(mRecentFiles.first()).absolutePath(),
        tr("Vector files (*.shp *.geojson *.gpkg *.kml *.gml *.json);;All files (*)"));
    if (path.isEmpty()) return;

    auto *layer = new GISVectorLayer(path);
    c->addLayer(layer, true);
    onLogMessage(tr("Added vector layer: %1").arg(QFileInfo(path).fileName()));
}

void SWMMVis::onAddRasterLayer()
{
    MapCanvas *c = activeCanvas();
    if (!c)
    {
        onLogMessage(tr("Open a project first to add layers."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add Raster Layer"),
        mRecentFiles.isEmpty() ? QDir::homePath()
                               : QFileInfo(mRecentFiles.first()).absolutePath(),
        tr("Raster files (*.tif *.tiff *.img *.asc *.nc *.hdf *.h5);;All files (*)"));
    if (path.isEmpty()) return;

    auto *layer = new GISRasterLayer(path);
    c->addLayer(layer, true);
    onLogMessage(tr("Added raster layer: %1").arg(QFileInfo(path).fileName()));
}

void SWMMVis::onAddSWMMResultsLayer()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer())
    {
        onLogMessage(tr("Open a SWMM project first; results layers attach to a model."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add SWMM Results"),
        mRecentFiles.isEmpty() ? QDir::homePath()
                               : QFileInfo(mRecentFiles.first()).absolutePath(),
        tr("SWMM results (*.out);;All files (*)"));
    if (path.isEmpty()) return;

    auto *layer = new SWMMResultsLayer(path, pw->modelLayer());
    pw->canvas()->addLayer(layer, true);
    onLogMessage(tr("Added results layer: %1").arg(QFileInfo(path).fileName()));
}
