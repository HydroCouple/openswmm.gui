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
#include <QMetaEnum>
#include <QFileInfo>
#include <QCloseEvent>
#include <QGraphicsItem>
#include <QFileDialog>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

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
#include "map/tools/maptoolidentify.h"   // IdentifyResult

#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
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

void SWMMVis::initializeWelcomeScreen()
{
    clearPreviousWelcomeScreenElements();

    // Closeability: rather than wrap welcomeWidget in a QMdiSubWindow
    // (which destabilises the inner QScrollArea's layout and infinite-loops
    // through updateScrollBars ↔ showChildren on startup), keep
    // welcomeWidget as the QMdiArea's backdrop child as the .ui authored
    // it, and add a small Close button to the welcome page itself. The
    // page hides on close; Help → Show Welcome Screen brings it back.
    if (ui->welcomeWidget && !mWelcomeCloseButton)
    {
        mWelcomeCloseButton = new QToolButton(ui->welcomeWidget);
        mWelcomeCloseButton->setIcon(style()->standardIcon(QStyle::SP_DockWidgetCloseButton));
        mWelcomeCloseButton->setToolTip(tr("Close (re-open via Help → Show Welcome Screen)"));
        mWelcomeCloseButton->setAutoRaise(true);
        mWelcomeCloseButton->setCursor(Qt::PointingHandCursor);
        connect(mWelcomeCloseButton, &QToolButton::clicked,
                this, [this]() { if (ui->welcomeWidget) ui->welcomeWidget->hide(); });
        // Float in the top-right corner of the welcome widget.
        const auto reposition = [this]() {
            if (!ui->welcomeWidget || !mWelcomeCloseButton) return;
            const int sz = 20;
            mWelcomeCloseButton->resize(sz, sz);
            mWelcomeCloseButton->move(ui->welcomeWidget->width() - sz - 6, 6);
            mWelcomeCloseButton->raise();
        };
        ui->welcomeWidget->installEventFilter(this);   // for resize → reposition
        // Also reposition once after the initial layout pass.
        QTimer::singleShot(0, this, reposition);
        mWelcomeRepositionFn = reposition;             // captured by eventFilter
    }

    // Wire welcome buttons
    connect(ui->commandLinkButtonNew,  &QCommandLinkButton::clicked, this, &SWMMVis::onNewProject);
    connect(ui->commandLinkButtonOpen, &QCommandLinkButton::clicked, this, [this]{ onOpenProject(); });
    connect(ui->commandLinkButtonClearRecentFiles, &QCommandLinkButton::clicked,
            this, &SWMMVis::onClearRecentFiles);

    // Persist "show welcome on startup" toggle
    mShowWelcomeScreenOnStartUp = mSettings.value(
        QStringLiteral("SWMMVis::ShowWelcomeOnStartup"), true).toBool();
    ui->checkBoxShowWelcomeOnStartUp->setChecked(mShowWelcomeScreenOnStartUp);
    connect(ui->checkBoxShowWelcomeOnStartUp, &QCheckBox::toggled, this, [this](bool on) {
        mShowWelcomeScreenOnStartUp = on;
        mSettings.setValue(QStringLiteral("SWMMVis::ShowWelcomeOnStartup"), on);
    });

    // Populate Learn SWMM links
    if (auto *frame = ui->frameLearnSWMM)
    {
        auto *layout = new QVBoxLayout(frame);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(2);
        struct Link { const char *text; const char *url; };
        const Link links[] = {
            { "User Manual",          "https://www.openswmm.org/gui/manual/" },
            { "EPA SWMM Reference",   "https://www.epa.gov/water-research/storm-water-management-model-swmm" },
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
}

void SWMMVis::initializeAttributePanelDockWidget()
{
    mAttributePanel = new AttributePanel(this);
    // QMainWindow::saveState() requires every QDockWidget to have a stable
    // objectName so its position can be persisted across sessions.
    mAttributePanel->setObjectName(QStringLiteral("dockWidgetAttributePanel"));
    addDockWidget(Qt::RightDockWidgetArea, mAttributePanel);
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

    // Help → Show Welcome Screen (added programmatically — the .ui's
    // menuHelp only has Help + About by default).
    if (ui->menuHelp)
    {
        QAction *first = ui->menuHelp->actions().value(0, nullptr);
        auto *actShowWelcome = new QAction(
            QIcon(QStringLiteral(":/swmmvis/Help")),
            tr("Show Welcome Screen"), this);
        actShowWelcome->setToolTip(tr("Re-open the Welcome page"));
        connect(actShowWelcome, &QAction::triggered, this, &SWMMVis::onShowWelcomeScreen);
        if (first) ui->menuHelp->insertAction(first, actShowWelcome);
        else       ui->menuHelp->addAction(actShowWelcome);
    }

    connect(ui->actionAddWMSData,    &QAction::triggered, this, &SWMMVis::onAddWMSLayer);
    connect(ui->actionAddBasemap,    &QAction::triggered, this, &SWMMVis::onAddWMTSLayer);
    connect(ui->actionAddVectorData, &QAction::triggered, this, &SWMMVis::onAddVectorLayer);
    connect(ui->actionAddRasterData, &QAction::triggered, this, &SWMMVis::onAddRasterLayer);
    connect(ui->actionAddSWMMOutput, &QAction::triggered, this, &SWMMVis::onAddSWMMResultsLayer);
    connect(ui->actionExecute,       &QAction::triggered, this, &SWMMVis::onRunSimulation);

    // Tools → Simulation Options… (added programmatically — the .ui's menuTools
    // is empty by default; this avoids touching the .ui resource for one entry).
    if (ui->menuTools)
    {
        auto *actSimOpts = ui->menuTools->addAction(tr("Simulation Options…"));
        actSimOpts->setToolTip(tr("Edit per-project SWMM simulation options"));
        connect(actSimOpts, &QAction::triggered, this, &SWMMVis::onSimulationOptions);
    }

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
    // Remove the Designer-placed placeholder + any previously generated buttons.
    for (QCommandLinkButton *btn : ui->frameRecentFiles->findChildren<QCommandLinkButton *>())
        btn->deleteLater();

    // Ensure the scroll area has a viewport widget with a vertical layout we can append to.
    if (auto *area = ui->frameRecentFiles->findChild<QScrollArea *>())
    {
        QWidget *content = area->widget();
        if (!content)
        {
            content = new QWidget(area);
            area->setWidget(content);
        }
        if (!content->layout())
        {
            auto *lay = new QVBoxLayout(content);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->setSpacing(2);
            lay->addStretch(1);
        }
        area->setWidgetResizable(true);
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
    // Reposition the welcome page's floating Close button whenever the
    // welcome widget is resized (or first shown).
    if (ui && watched == ui->welcomeWidget
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    {
        if (mWelcomeRepositionFn) mWelcomeRepositionFn();
    }
    return QMainWindow::eventFilter(watched, event);
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
    if (!ui->welcomeWidget) return;
    // The welcome widget is the QMdiArea's backdrop child — bring it back
    // by clearing the active subwindow so it's not occluded.
    ui->mdiAreaCentral->setActiveSubWindow(nullptr);
    ui->welcomeWidget->show();
    ui->welcomeWidget->raise();
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
        mComboBoxFlowUnits->setEnabled(false);
        if (mLayerTreePanel)     mLayerTreePanel->setCanvas(nullptr);
        if (mObjectBrowserPanel) mObjectBrowserPanel->setProject(nullptr, nullptr);
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

    SpatialReferenceSystem *srs = pw->canvas()->canvasSRS();
    mToolButtonCoordinateReferenceSystem->setText(
        srs ? srs->toAuthority() : QStringLiteral("EPSG:4326"));

    // Rebind the Layers dock to this project's canvas so visibility toggles,
    // ordering, and layer additions reflect the focused tab.
    if (mLayerTreePanel)
        mLayerTreePanel->setCanvas(pw->canvas());

    // Rebind the Object Browser + Attribute Panel to this project's model
    // layer + selection bus.
    if (mObjectBrowserPanel)
        mObjectBrowserPanel->setProject(pw->modelLayer(), pw->selectionManager());
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
        mObjectBrowserPanel->setProject(pw->modelLayer(), pw->selectionManager());
}

void SWMMVis::onModelLoadError(const QString &msg)
{
    onLogMessage(msg, OpenSWMMVisLogMessage::LogMessageType::Error);
    QMessageBox::warning(this, tr("Model Load Error"), msg);
}

void SWMMVis::onRecentFilesSizeChanged()
{
    while (mRecentFiles.size() > 20)
        mRecentFiles.removeLast();

    ui->menuOpenRecent->clear();
    const bool hasFiles = !mRecentFiles.isEmpty();
    ui->menuOpenRecent->setEnabled(hasFiles);
    ui->commandLinkButtonClearRecentFiles->setEnabled(hasFiles);

    // Menu entries
    for (int i = 0; i < mRecentFiles.size(); ++i)
    {
        QFileInfo fi(mRecentFiles[i]);
        QAction *action = new QAction(fi.filePath(), ui->menuOpenRecent);
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
    QString base = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
    QString rptPath = base + QStringLiteral(".rpt");
    QString outPath = base + QStringLiteral(".out");

    onSetProgressBarBusy(true);
    onLogMessage(tr("Running simulation: %1").arg(fi.fileName()));

    // Run the simulation on a worker thread. swmm_engine_run is a
    // standalone call that opens its own engine handle from the .inp on
    // disk — leaves the GUI's open engine untouched.
#ifdef HAVE_OPENSWMMCORE
    QPointer<SWMMVis> self(this);
    QPointer<SWMMVisProjectWindow> pwGuard(pw);
    QString outPathCopy = outPath;
    QString fileName    = fi.fileName();

    auto *watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcher<int>::finished, this,
            [self, watcher, pwGuard, outPathCopy, fileName]() {
                const int rc = watcher->result();
                watcher->deleteLater();
                if (!self) return;
                self->onSetProgressBarBusy(false);
                if (rc != 0)
                {
                    self->onLogMessage(
                        tr("Simulation failed (engine code %1) for %2.")
                            .arg(rc).arg(fileName),
                        OpenSWMMVisLogMessage::Error);
                    return;
                }
                self->onLogMessage(
                    tr("Simulation finished. Results: %1").arg(outPathCopy));
                // Auto-load the .out as a SWMMResultsLayer attached to this
                // project's model layer, so subsequent plot/inspect actions
                // can read it.
                if (pwGuard && pwGuard->canvas() && pwGuard->modelLayer())
                {
                    auto *rl = new SWMMResultsLayer(outPathCopy,
                                                    pwGuard->modelLayer());
                    pwGuard->canvas()->addLayer(rl, true);
                }
            });
    watcher->setFuture(QtConcurrent::run([inpPath, rptPath, outPath]() -> int {
        return swmm_engine_run(inpPath.toUtf8().constData(),
                               rptPath.toUtf8().constData(),
                               outPath.toUtf8().constData(),
                               nullptr);
    }));
#else
    Q_UNUSED(outPath) Q_UNUSED(rptPath)
    onSetProgressBarBusy(false);
    onLogMessage(tr("OpenSWMMCore not available; cannot run."),
                 OpenSWMMVisLogMessage::Error);
#endif
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
