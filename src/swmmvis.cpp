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
#include <QProxyStyle>
#include <QDateTimeEdit>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionProgressBar>
#include <QStyledItemDelegate>
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
#include "project/projectserializer.h"
#include "layers/swmmmodellayer.h"
#include "swmmvisprojectwindow.h"
#include "core/crsreproject.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "ui/dialogs/crschangedialog.h"
#include "ui/dialogs/aboutdialog.h"
#include "ui/dialogs/layerpropertiesdialog.h"
#include "ui/dialogs/meshgenerationdialog.h"
#include "ui/dialogs/newprojectdialog.h"
#include "ui/dialogs/pluginsdialog.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/timeseriesplotdialog.h"
#include "ui/dialogs/wmsconnectiondialog.h"
#include "ui/dialogs/wmtsconnectiondialog.h"
#include "ui/panels/layertreepanel.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/attributepanel.h"
#include "plugins/filefilterregistry.h"
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
#include "map/tools/maptoolselect.h"
#include "layers/wmslayer.h"
#include "layers/wmtslayer.h"

#include <QDesktopServices>
#include <QFile>
#include <QStandardPaths>
#include <QUuid>
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
    // (Workspace-init chatter removed — Message Log is for modelling
    // events only, not GUI-lifecycle noise.)
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
 * @brief Proxy style that forces tab-close buttons to render on the
 *        RIGHT side of the tab. macOS's native Qt style defaults to
 *        LEFT (Safari convention); the SWMM GUI preference is RIGHT so
 *        the close affordance matches the platform-neutral / Windows
 *        feel the rest of the app uses.
 */
class TabCloseRightStyle : public QProxyStyle
{
public:
    explicit TabCloseRightStyle(QStyle *base) : QProxyStyle(base) {}
    int styleHint(QStyle::StyleHint hint, const QStyleOption *opt,
                  const QWidget *w, QStyleHintReturn *r) const override
    {
        if (hint == QStyle::SH_TabBar_CloseButtonPosition)
            return QTabBar::RightSide;
        return QProxyStyle::styleHint(hint, opt, w, r);
    }
};

/**
 * @brief Item delegate that paints a QProgressBar in the Progress
 *        column of the Simulation Status tree view. Reads the percent
 *        value from Qt::UserRole (an integer 0–100 carried by the
 *        model) and falls back to a blank cell if that role isn't set
 *        (e.g. for the warning child rows).
 */
class ProgressBarDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        // Paint the default cell background first (selection colour,
        // alternating rows, grid lines) using the real index so the
        // styling the view expects is applied.
        QStyleOptionViewItem bgOpt = option;
        bgOpt.text.clear();                       // don't draw any text
        QStyledItemDelegate::paint(painter, bgOpt, index);

        // Child (warning) rows don't carry a progress value; skip.
        const QVariant pctVar = index.data(Qt::UserRole);
        if (!pctVar.isValid() || !pctVar.canConvert<int>())
            return;
        const int pct = qBound(0, pctVar.toInt(), 100);

        QStyleOptionProgressBar bar;
        // Constrain to a horizontal band — on macOS the native QProgressBar
        // minimum height can push the cell to render tall/square, which
        // looked "vertical". Clamping to 14 px tall keeps the bar horizontal
        // and sits comfortably inside a normal tree-view row.
        QRect r = option.rect.adjusted(4, 0, -4, 0);
        const int barH = qMin(r.height() - 4, 14);
        const int yOff = (r.height() - barH) / 2;
        bar.rect          = QRect(r.left(), r.top() + yOff, r.width(), barH);
        bar.minimum       = 0;
        bar.maximum       = 100;
        bar.progress      = pct;
        bar.text          = QStringLiteral("%1%").arg(pct);
        bar.textVisible   = true;
        bar.textAlignment = Qt::AlignCenter;
        bar.palette       = option.palette;
        bar.direction     = option.direction;
        bar.fontMetrics   = option.fontMetrics;
        // State_Horizontal tells the style to orient the chunk horizontally
        // (some platform styles otherwise default to vertical for tall rects).
        bar.state         = QStyle::State_Enabled | QStyle::State_Active
                          | QStyle::State_Horizontal;

        // Use the Fusion style to paint — its progress-chunk is the standard
        // blue used by the bottom status-bar QProgressBar, and it renders
        // the same way on every platform (macOS native would otherwise draw
        // an aqua/teal gradient that doesn't match).
        static QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"));
        QStyle *style = fusion ? fusion : QApplication::style();
        style->drawControl(QStyle::CE_ProgressBar, &bar, painter);
    }
};

/**
 * @brief Item delegate that renders Sim Start / Sim Current / Sim End date
 *        columns as a read-only QDateTimeEdit (persistent editor). The
 *        model carries raw QDateTime in DisplayRole; this delegate pushes
 *        it into the widget and keeps the widget visible across the row's
 *        lifetime so the user sees an actual datetime control, not a flat
 *        locale string.
 */
class DateTimeEditDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem & /*opt*/,
                          const QModelIndex & /*idx*/) const override
    {
        auto *w = new QDateTimeEdit(parent);
        w->setReadOnly(true);
        w->setButtonSymbols(QAbstractSpinBox::NoButtons);
        w->setFrame(false);
        w->setCalendarPopup(false);
        w->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        w->setFocusPolicy(Qt::NoFocus);
        w->setAlignment(Qt::AlignCenter);
        w->setAttribute(Qt::WA_TransparentForMouseEvents);
        return w;
    }

    void setEditorData(QWidget *editor, const QModelIndex &idx) const override
    {
        auto *w = qobject_cast<QDateTimeEdit *>(editor);
        if (!w) return;
        const QVariant v = idx.data(Qt::DisplayRole);
        if (v.canConvert<QDateTime>() && v.toDateTime().isValid())
            w->setDateTime(v.toDateTime());
    }
    // No setModelData — readonly.
};

} // anonymous namespace

void SWMMVis::initializeWelcomeScreen()
{
    clearPreviousWelcomeScreenElements();

    // Force the MDI's internal tab bar to render close-X on the RIGHT
    // (macOS defaults to LEFT via the native style hint). Apply before
    // tabs are populated so every tab picks up the new position.
    if (auto *tabBar = ui->mdiAreaCentral->findChild<QTabBar *>())
    {
        tabBar->setStyle(new TabCloseRightStyle(tabBar->style()));
    }

    // Welcome sub-window lifecycle (simplified from the previous
    // delete-on-close + reparent scheme, which was brittle — reopen via
    // Help → Show Welcome sometimes failed the tab-close button because
    // the reparent path left welcomeWidget in an ambiguous parent
    // state). New scheme: the sub-window is KEPT alive (not destroyed
    // on close) and we just toggle its visibility. Closing via the tab
    // X hides the sub (Qt's default for close() when WA_DeleteOnClose
    // is false) — QMdiArea's TabbedView drops the tab for a hidden
    // sub, so the user sees "the welcome closed." Reopen = show().
    if (QMdiSubWindow *sub = welcomeSubWindow())
    {
        sub->setAttribute(Qt::WA_DeleteOnClose, false);
        sub->installEventFilter(this);
        sub->setWindowTitle(tr("Welcome"));
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
    {
        if (QMdiSubWindow *sub = welcomeSubWindow())
            sub->hide();  // hide without triggering the close event
    }

    // Populate Learn SWMM links
    if (auto *frame = ui->frameLearnSWMM)
    {
        auto *layout = new QVBoxLayout(frame);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(2);
        struct Link { const char *text; const char *url; };
        const Link links[] = {
            { "User Manual",          "https://www.hydrocouple.org/openswmm.engine/d3/dae/manuals.html" },
            { "Engine API Reference", "https://www.hydrocouple.org/openswmm.engine" },
            { "Report an Issue",      "https://github.com/HydroCouple/openswmm.engine/issues" },
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

        // Union the layer-CRS bbox of every selected SWMM object across
        // every visible SWMMModelLayer. Slice R Phase 3 retired the
        // per-object QGraphicsItems so `mapScene()->selectedItems()` no
        // longer reports SWMM features — selection lives on the layer
        // itself via `selectedElementNames` + `objectExtent`.
        double xMin = std::numeric_limits<double>::infinity();
        double yMin = std::numeric_limits<double>::infinity();
        double xMax = -std::numeric_limits<double>::infinity();
        double yMax = -std::numeric_limits<double>::infinity();
        bool any = false;

        for (OpenSWMMVisLayer *l : c->layers()) {
            if (!l->isVisible()) continue;
            auto *sl = qobject_cast<SWMMModelLayer *>(l);
            if (!sl) continue;
            for (const QString &name : sl->selectedElementNames()) {
                const MapExtent e = sl->objectExtent(name);
                if (!std::isfinite(e.xMin())) continue;
                xMin = std::min(xMin, e.xMin());
                yMin = std::min(yMin, e.yMin());
                xMax = std::max(xMax, e.xMax());
                yMax = std::max(yMax, e.yMax());
                any = true;
            }
        }
        if (!any) return;

        // Pad: 25 % of the union's span for a comfortable framing
        // around extents that aren't degenerate; for a single-point
        // selection (node / gage) fall back to a layer-relative
        // buffer so the zoom stays at a reasonable scale.
        double padX = (xMax - xMin) * 0.25;
        double padY = (yMax - yMin) * 0.25;
        if (padX <= 0.0 && padY <= 0.0) {
            // Single-point selection — borrow ObjectBrowserPanel's
            // logic: 0.5 % of the layer extent's diagonal, floor 25.
            double buffer = 100.0;
            for (OpenSWMMVisLayer *l : c->layers()) {
                if (!l->isVisible()) continue;
                if (auto *sl = qobject_cast<SWMMModelLayer *>(l)) {
                    const MapExtent &le = sl->extent();
                    if (!le.isValid()) continue;
                    const double dx = le.xMax() - le.xMin();
                    const double dy = le.yMax() - le.yMin();
                    buffer = std::max(buffer, std::max(25.0, 0.005 * std::max(dx, dy)));
                }
            }
            padX = buffer; padY = buffer;
        }
        MapExtent zoom(xMin - padX, yMin - padY, xMax + padX, yMax + padY);
        if (zoom.isValid()) c->setExtent(zoom);
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

    // Right-click "Plot Time Series…" — shared by Object Browser and the
    // map canvas's Select-tool context menu. Both routes funnel through
    // openTimeSeriesPlotFor().
    connect(mObjectBrowserPanel, &ObjectBrowserPanel::plotTimeSeriesRequested,
            this, &SWMMVis::openTimeSeriesPlotFor);

    // Slice S — per-object visibility no longer goes through the panel's
    // signals. The virtualised SWMMObjectTreeModel's setData() calls the
    // layer's setObjectVisibleAt / setCategoryVisible directly, so the
    // previous objectVisibilityChanged / objectsVisibilityChanged bridges
    // are gone.
}

void SWMMVis::openTimeSeriesPlotFor(const SWMMObjectRef &ref)
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas()) return;

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

    auto *dlg = new TimeSeriesPlotDialog(outPath, ref, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
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
    view->setColumnWidth(SimulationStatusModel::ColName,        200);
    view->setColumnWidth(SimulationStatusModel::ColStatus,       80);
    view->setColumnWidth(SimulationStatusModel::ColProgress,    120);
    view->setColumnWidth(SimulationStatusModel::ColStartDate,   140);
    view->setColumnWidth(SimulationStatusModel::ColCurrentDate, 140);
    view->setColumnWidth(SimulationStatusModel::ColEndDate,     140);
    view->setColumnWidth(SimulationStatusModel::ColRunoffErr,    90);
    view->setColumnWidth(SimulationStatusModel::ColRoutingErr,   90);
    view->header()->setSectionResizeMode(SimulationStatusModel::ColDuration,
                                         QHeaderView::Stretch);

    // Embed a progress bar in the Progress column via a delegate that
    // paints QStyle::CE_ProgressBar keyed off the model's UserRole int.
    view->setItemDelegateForColumn(SimulationStatusModel::ColProgress,
                                   new ProgressBarDelegate(view));

    // Render Sim Start / Sim Current / Sim End as read-only QDateTimeEdit
    // widgets. Uses persistent editors so the widget stays visible the
    // whole time the row exists; dataChanged emissions from the model
    // automatically re-call setEditorData.
    auto *dtDelegate = new DateTimeEditDelegate(view);
    view->setItemDelegateForColumn(SimulationStatusModel::ColStartDate,   dtDelegate);
    view->setItemDelegateForColumn(SimulationStatusModel::ColCurrentDate, dtDelegate);
    view->setItemDelegateForColumn(SimulationStatusModel::ColEndDate,     dtDelegate);

    connect(mSimStatusModel, &QAbstractItemModel::rowsInserted, view,
            [view, this](const QModelIndex &parent, int first, int last) {
                if (parent.isValid()) return;              // job rows only
                for (int r = first; r <= last; ++r) {
                    view->openPersistentEditor(
                        mSimStatusModel->index(r, SimulationStatusModel::ColStartDate));
                    view->openPersistentEditor(
                        mSimStatusModel->index(r, SimulationStatusModel::ColCurrentDate));
                    view->openPersistentEditor(
                        mSimStatusModel->index(r, SimulationStatusModel::ColEndDate));
                }
            });
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
    connect(ui->actionSave,            &QAction::triggered, this, &SWMMVis::onSaveProject);
    connect(ui->actionSaveAsProject,   &QAction::triggered, this, &SWMMVis::onSaveProjectAs);
    connect(ui->actionSaveAsInput,     &QAction::triggered, this, &SWMMVis::onSaveAsInputFile);
    connect(ui->actionExportMap,       &QAction::triggered, this, &SWMMVis::onExportMap);
    connect(ui->actionAbout,  &QAction::triggered, this, &SWMMVis::onAbout);

    connect(ui->menuOpenRecent, &QMenu::triggered, this, &SWMMVis::onOpenRecentFile);

    connect(ui->actionShowWelcome, &QAction::triggered, this, &SWMMVis::onShowWelcomeScreen);

    connect(ui->actionAddWMSData,    &QAction::triggered, this, &SWMMVis::onAddWMSLayer);
    connect(ui->actionAddBasemap,    &QAction::triggered, this, &SWMMVis::onAddWMTSLayer);
    connect(ui->actionAddVectorData, &QAction::triggered, this, &SWMMVis::onAddVectorLayer);
    connect(ui->actionAddRasterData, &QAction::triggered, this, &SWMMVis::onAddRasterLayer);
    connect(ui->actionAddSWMMOutput, &QAction::triggered, this, &SWMMVis::onAddSWMMResultsLayer);
    connect(ui->actionExecute,       &QAction::triggered, this, &SWMMVis::onRunSimulation);

    // Pause: toggle the paused flag on every active runner. The pause
    // action itself is a checkable toggle in the toolbar — its state
    // follows the paused flag.
    ui->actionPauseExecution->setCheckable(true);
    connect(ui->actionPauseExecution, &QAction::toggled, this, [this](bool paused) {
        for (SimulationRunner *runner : std::as_const(mActiveRunners))
            runner->setPaused(paused);
        onLogMessage(paused
            ? tr("Simulation paused — toggle again to resume.")
            : tr("Simulation resumed."));
    });

    // Cancel: request an early stop. The runner still flushes the .out
    // and .rpt via swmm_engine_end / _report / _close, so partial
    // results are preserved. Finished-handler auto-loads them.
    connect(ui->actionCancelExecution, &QAction::triggered, this, [this]() {
        if (mActiveRunners.isEmpty()) {
            onLogMessage(tr("Cancel: no simulation is running."),
                         OpenSWMMVisLogMessage::LogMessageType::Information);
            return;
        }
        for (SimulationRunner *runner : std::as_const(mActiveRunners))
            runner->cancel();
        onLogMessage(tr("Simulation cancel requested; partial results will "
                        "be saved to the .out file."));
    });
    connect(ui->actionOptions,       &QAction::triggered, this, &SWMMVis::onSimulationOptions);

    // Tools → Set Project CRS… (added programmatically).
    if (ui->menuTools)
    {
        auto *actSetCRS = ui->menuTools->addAction(
            QIcon(QStringLiteral(":/swmmvis/Globe")),
            tr("Set Project CRS…"));
        actSetCRS->setToolTip(tr("Choose or change the project's coordinate reference system"));
        connect(actSetCRS, &QAction::triggered, this, &SWMMVis::onCRSButtonClicked);

    }

    // Tools → Preferences… (Slice V). The .ui defines `actionSettings`
    // with PreferencesRole — on macOS, Qt automatically reparents it into
    // the Application menu ("App Name → Preferences"), matching native
    // platform conventions. On Linux / Windows it stays in Tools.
    if (ui->actionSettings) {
        ui->actionSettings->setToolTip(tr("Configure tolerances, default tool, "
                                          "CRS, rendering LOD, selection "
                                          "colours, simulation tick rate, "
                                          "and other application preferences"));
        connect(ui->actionSettings, &QAction::triggered, this, [this]() {
            PreferencesDialog dlg(this);
            dlg.exec();
        });
    }

    // Tools → Plugins… (Slice AA). Read-only listing of every filter
    // the FileFilterRegistry knows about (built-in + engine-discovered).
    if (ui->actionPlugins)
    {
        connect(ui->actionPlugins, &QAction::triggered, this, [this]() {
            PluginsDialog dlg(this);
            dlg.exec();
        });
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

    // Slice AU.4 — Generate Mesh tool launches MeshGenerationDialog.
    if (ui->actionGenerateMesh)
        connect(ui->actionGenerateMesh, &QAction::triggered, this, [this]() {
            auto *pw = activeProjectWindow();
            if (!pw)
            {
                onLogMessage(tr("Generate Mesh: open a SWMM project first."),
                             OpenSWMMVisLogMessage::LogMessageType::Warning);
                return;
            }
            MeshGenerationDialog dlg(pw, this);
            if (dlg.exec() == QDialog::Accepted)
                onLogMessage(tr("2D mesh generated and written."));
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
    // (Settings-I/O chatter removed — Message Log is for modelling
    // events only.)
    onSetProgressBarBusy(true);

    mSettings.beginGroup("SWMMVis::MainWindow");
    restoreState(mSettings.value("SWMMVis::WindowState",    saveState()).toByteArray());
    setWindowState(static_cast<Qt::WindowState>(
        mSettings.value("SWMMVis::WindowStateEnum", static_cast<int>(windowState())).toInt()));
    setGeometry(mSettings.value("SWMMVis::Geometry", geometry()).toRect());
    mRecentFiles = mSettings.value("SWMMVis::RecentFiles", mRecentFiles).toStringList();
    mSettings.endGroup();

    onRecentFilesSizeChanged();
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
    // Welcome tab close: accept the close, let Qt hide the sub-window
    // (WA_DeleteOnClose is false so nothing is destroyed). QMdiArea's
    // TabbedView hides the corresponding tab when the sub becomes
    // invisible, so the user sees the welcome disappear.
    //
    // After hiding, promote the next visible sub-window to active if
    // any exist — matches the user's expectation that closing the
    // welcome leaves the next project document in front. If no other
    // subs are visible we just leave the MDI blank (nothing to do —
    // Qt handles that automatically).
    if (event->type() == QEvent::Close)
    {
        if (auto *sub = qobject_cast<QMdiSubWindow *>(watched);
            sub && ui && ui->welcomeWidget && sub->widget() == ui->welcomeWidget)
        {
            // Eat the close event and hide the sub-window in place.
            // QMdiArea::TabbedView automatically removes the tab for any
            // hidden sub-window, so the user sees the tab disappear.
            // The widget and its content are fully preserved for reuse.
            QMdiSubWindow *next = nullptr;
            for (QMdiSubWindow *other : ui->mdiAreaCentral->subWindowList())
            {
                if (other == sub) continue;
                if (other->isVisible()) { next = other; break; }
            }
            sub->hide();
            if (next) ui->mdiAreaCentral->setActiveSubWindow(next);
            return true;  // eat the event — sub-window stays in MDI list
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
    // Closing the sub-window triggers WelcomeSubWindow::closeEvent which
    // nulls ui->welcomeWidget, then WA_DeleteOnClose destroys everything.
    if (QMdiSubWindow *sub = welcomeSubWindow())
        sub->close();
}

// ── Slots ─────────────────────────────────────────────────────────────────

namespace {

// Build a minimum-viable SWMM .inp from the New-Project dialog inputs.
// SWMM tolerates many missing sections; we keep [TITLE], [OPTIONS], and
// [REPORT] explicit so the engine has stable defaults to read back.
QString synthesizeBlankInp(const NewProjectDialog::Result &r)
{
    const QString startDate = r.startDateTime.toString(QStringLiteral("MM/dd/yyyy"));
    const QString startTime = r.startDateTime.toString(QStringLiteral("HH:mm:ss"));
    const QString endDate   = r.endDateTime.toString(QStringLiteral("MM/dd/yyyy"));
    const QString endTime   = r.endDateTime.toString(QStringLiteral("HH:mm:ss"));
    return QStringLiteral(
"[TITLE]\n"
"%1\n\n"
"[OPTIONS]\n"
"FLOW_UNITS           %2\n"
"INFILTRATION         %3\n"
"FLOW_ROUTING         %4\n"
"START_DATE           %5\n"
"START_TIME           %6\n"
"END_DATE             %7\n"
"END_TIME             %8\n"
"REPORT_START_DATE    %5\n"
"REPORT_START_TIME    %6\n"
"SWEEP_START          01/01\n"
"SWEEP_END            12/31\n"
"DRY_DAYS             0\n"
"REPORT_STEP          00:15:00\n"
"WET_STEP             00:05:00\n"
"DRY_STEP             01:00:00\n"
"ROUTING_STEP         0:00:30\n\n"
"[REPORT]\n"
"INPUT      NO\n"
"CONTROLS   NO\n"
"SUBCATCHMENTS ALL\n"
"NODES      ALL\n"
"LINKS      ALL\n")
        .arg(r.name, r.flowUnits, r.infiltrationModel, r.flowRouting,
             startDate, startTime, endDate, endTime);
}

} // namespace

void SWMMVis::onNewProject()
{
    // Match legacy SWMM5: File → New creates a blank untitled project
    // immediately with hard-coded defaults — no dialog interruption.
    // Templates / explicit CRS choice live in a future "New From
    // Template…" entry; NewProjectDialog stays available for that.
    NewProjectDialog::Result r;
    r.name              = QStringLiteral("Untitled");
    r.flowUnits         = QStringLiteral("CFS");
    r.infiltrationModel = QStringLiteral("HORTON");
    r.flowRouting       = QStringLiteral("DYNWAVE");
    r.startDateTime     = QDateTime(QDate(2002, 1, 1), QTime(0, 0));
    r.endDateTime       = QDateTime(QDate(2002, 1, 1), QTime(6, 0));
    // crsAuthCode intentionally left empty — SWMMModelLayer::loadModel
    // pulls the default CRS from PreferencesManager when the .inp carries
    // none, and the project window propagates that to the canvas. No
    // post-load override here keeps layer/canvas in lockstep.

    // Write a synthetic .inp into the system temp dir with a unique name.
    // The path is owned by the resulting project window and deleted on
    // first Save As (engine no longer needs it) or on close-without-save.
    const QString tempDir = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation);
    const QString tempPath = QStringLiteral("%1/swmmvis-untitled-%2.inp")
        .arg(tempDir, QUuid::createUuid().toString(QUuid::WithoutBraces));

    {
        QFile f(tempPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::critical(this, tr("New Project failed"),
                tr("Could not write temp file at %1").arg(tempPath));
            return;
        }
        f.write(synthesizeBlankInp(r).toUtf8());
    }

    // Reuse the standard load path. After it returns, undo the
    // recent-files registration the temp path picked up — the user has
    // not yet given this project a real name.
    openSingleINP(tempPath);
    mRecentFiles.removeAll(tempPath);
    onRecentFilesSizeChanged();
    saveSettings();

    auto *pw = activeProjectWindow();
    if (!pw) return;  // load failed; openSingleINP already logged

    pw->markUntitled(tempPath);
    setWindowTitle(QStringLiteral("OpenSWMM — Untitled"));

    // CRS handling intentionally left to the standard load path:
    //   SWMMModelLayer::loadModel falls back to PreferencesManager's default
    //   when the .inp carries no CRS (synthetic blank .inp doesn't), and
    //   SWMMVisProjectWindow::loadModel propagates that to the canvas.
    //   The Slice-Y-removed dialog used to surface a CRS picker; with the
    //   no-dialog flow, the only correct answer is "use the configured
    //   default" — which loadModel already does. No post-load CRS override
    //   here, since doing so creates a layer/canvas drift when the override
    //   doesn't match what loadModel adopted.

    onLogMessage(tr("New project created (untitled). Save As to give it a path."));
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

    // Slice X — write the .oswp sidecar next to the .inp so GUI-only
    // state (layer CRS, category/object order, hidden set, canvas
    // extent) round-trips on reopen. Serializer failures log at
    // Warning — the .inp itself already saved so the project is
    // recoverable even if the sidecar couldn't be written.
    const QString oswp = ProjectSerializer::sidecarPathFor(
        pw->modelLayer()->modelFilePath());
    if (!oswp.isEmpty()) {
        QString sidecarErr;
        if (!ProjectSerializer::saveToFile(oswp, pw, &sidecarErr)) {
            onLogMessage(tr("Sidecar save failed: %1").arg(sidecarErr),
                         OpenSWMMVisLogMessage::LogMessageType::Warning);
        }
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
    auto *kFilters = openswmmvis::FileFilterRegistry::instance();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save SWMM Model As"),
        suggested.isEmpty() ? QDir::homePath() : suggested,
        kFilters->filterFor(openswmmvis::FilterKind::InputRead));
    if (path.isEmpty()) return;

    QString err;
    if (!pw->saveAs(path, &err))
    {
        QMessageBox::critical(this, tr("Save As failed"), err);
        return;
    }
    // Mirror the .oswp sidecar to the new path — Save As is the only
    // place a single project can take on a new basename, so the old
    // sidecar stays next to the old .inp. Explicit delete of the old
    // sidecar is deferred; Phase 12 polish.
    {
        const QString oswp = ProjectSerializer::sidecarPathFor(path);
        QString sidecarErr;
        if (!oswp.isEmpty()
            && !ProjectSerializer::saveToFile(oswp, pw, &sidecarErr))
        {
            onLogMessage(tr("Sidecar save failed: %1").arg(sidecarErr),
                         OpenSWMMVisLogMessage::LogMessageType::Warning);
        }
    }
    mRecentFiles.removeAll(path);
    mRecentFiles.prepend(path);
    onRecentFilesSizeChanged();
    saveSettings();
    onLogMessage(tr("Saved As: %1").arg(path));
}

void SWMMVis::onSaveAsInputFile()
{
    auto *pw = activeProjectWindow();
    if (!pw)
    {
        onLogMessage(tr("Save Input As: no active project."),
                     OpenSWMMVisLogMessage::LogMessageType::Warning);
        return;
    }
    const QString suggested = pw->modelLayer() ? pw->modelLayer()->modelFilePath() : QString();
    auto *kFilters = openswmmvis::FileFilterRegistry::instance();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save SWMM Input As"),
        suggested.isEmpty() ? QDir::homePath() : suggested,
        kFilters->filterFor(openswmmvis::FilterKind::InputRead));
    if (path.isEmpty()) return;

    QString err;
    if (!pw->saveAs(path, &err))
    {
        QMessageBox::critical(this, tr("Save Input As failed"), err);
        return;
    }
    // Distinct from onSaveProjectAs: no .oswp sidecar is written here. The
    // user is exporting the SWMM input independently, not relocating the
    // whole project.
    mRecentFiles.removeAll(path);
    mRecentFiles.prepend(path);
    onRecentFilesSizeChanged();
    saveSettings();
    onLogMessage(tr("Saved Input As: %1").arg(path));
}

void SWMMVis::onExportMap()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas())
    {
        onLogMessage(tr("Export Map: no active project."),
                     OpenSWMMVisLogMessage::LogMessageType::Warning);
        return;
    }
    auto *kFilters = openswmmvis::FileFilterRegistry::instance();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Map"),
        QDir::homePath(),
        kFilters->filterFor(openswmmvis::FilterKind::MapExportWrite));
    if (path.isEmpty()) return;

    // First-cut: PNG via QWidget::grab(). SVG / DXF / EMF / .map are
    // surfaced in the filter list but routed to Slice AP (Print/Export)
    // for a proper renderer; user gets a clear log message in the
    // meantime.
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("png"))
    {
        const QPixmap pm = pw->canvas()->grab();
        if (!pm.save(path, "PNG"))
        {
            QMessageBox::critical(this, tr("Export Map failed"),
                tr("Could not write PNG to %1").arg(path));
            return;
        }
        onLogMessage(tr("Exported map: %1").arg(path));
    }
    else
    {
        QMessageBox::information(this, tr("Export Map"),
            tr("Export to %1 is not implemented yet (planned for the "
               "Print / Export slice). PNG is supported now.")
                .arg(suffix.toUpper()));
    }
}

void SWMMVis::onClearRecentFiles()
{
    mRecentFiles.clear();
    onRecentFilesSizeChanged();
    saveSettings();
}

void SWMMVis::onShowWelcomeScreen()
{
    if (!ui->mdiAreaCentral) return;

    // The welcome sub-window is kept in the MDI list always (never removed).
    // Hide-on-close preserves all widget content; show() restores the tab.
    if (QMdiSubWindow *sub = welcomeSubWindow())
    {
        sub->show();
        ui->mdiAreaCentral->setActiveSubWindow(sub);
    }
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
        // Engine input plugins (e.g., DefaultInputPlugin → *.inp,
        // GeoPackagePluginInfo → *.gpkg) contribute filters via the
        // registry's InputRead kind. ProjectRead supplies the GUI-only
        // *.oswp filter. We concatenate the two filter strings, dropping
        // the trailing "All Files (*)" from the first so it appears only
        // once at the end.
        auto *kFilters = openswmmvis::FileFilterRegistry::instance();
        const QString allFiles = QStringLiteral(";;") + tr("All Files (*)");
        QString inputs = kFilters->filterFor(openswmmvis::FilterKind::InputRead);
        inputs.chop(allFiles.size());
        QString projects = kFilters->filterFor(openswmmvis::FilterKind::ProjectWrite);
        projects.chop(allFiles.size());
        const QString combined = inputs + QStringLiteral(";;") + projects + allFiles;
        filePath = QFileDialog::getOpenFileName(
            this,
            tr("Open SWMM Model or Project"),
            mRecentFiles.isEmpty() ? QDir::homePath()
                                   : QFileInfo(mRecentFiles.first()).absolutePath(),
            combined);
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

    // Main-window title tracks the active project — re-sync when that
    // project renames itself (Save As) or flips its dirty `*` marker.
    connect(window, &QWidget::windowTitleChanged, this,
            [this, window](const QString &t) {
                if (window == mActiveProjectWindow)
                    setWindowTitle(QStringLiteral("OpenSWMM — %1").arg(t));
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

        // Slice X — apply the co-located .oswp sidecar if one exists.
        // Hydrates GUI-only state (layer CRS, category / object order,
        // hidden objects, canvas extent) that can't round-trip through
        // the .inp itself. Missing sidecar is not an error.
        const QString oswp = ProjectSerializer::sidecarPathFor(filePath);
        if (!oswp.isEmpty() && QFile::exists(oswp)) {
            QString sidecarErr;
            if (!ProjectSerializer::applyFromFile(oswp, window, &sidecarErr)) {
                onLogMessage(tr("Sidecar load failed: %1").arg(sidecarErr),
                             OpenSWMMVisLogMessage::LogMessageType::Warning);
            }
        }

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

void SWMMVis::onOpenRecentFile(QAction *action)
{
    // QMenu::triggered emits the triggered QAction* directly; relying on
    // sender() gives us the menu, not the action, so the path lookup was
    // coming back empty and nothing opened. Take the parameter instead.
    if (!action) return;
    const QString path = action->data().toString();
    // Skip non-recent-file entries in the same menu (separator + Clear
    // has its own connection and empty data).
    if (path.isEmpty()) return;
    onOpenProject(path);
}

void SWMMVis::onActiveSubWindowChanged(QMdiSubWindow *window)
{
    auto *pw = qobject_cast<SWMMVisProjectWindow *>(window);

    // Main-window title follows the active MDI tab so macOS / Linux
    // window managers and the Window menu's app-name slot reflect
    // what the user's looking at. Welcome and "no project" both show
    // the bare app name. Project windows show "OpenSWMM — <title>"
    // where <title> is the sub-window's title (carries the dirty `*`
    // from Slice A, so the user can see unsaved-changes state at a
    // glance).
    if (pw) {
        setWindowTitle(QStringLiteral("OpenSWMM — %1").arg(pw->windowTitle()));
    } else if (window) {
        // Welcome or other non-project sub-window active.
        setWindowTitle(QStringLiteral("OpenSWMM"));
    }

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
        setWindowTitle(QStringLiteral("OpenSWMM"));
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

    // Bridge the Select-tool's right-click context menu to the same
    // Time Series plot handler used by Object Browser. UniqueConnection
    // keeps repeated tab-switches from stacking duplicates.
    if (auto *st = pw->selectTool()) {
        connect(st, &OpenSWMMVisMapToolSelect::plotTimeSeriesRequested,
                this, &SWMMVis::openTimeSeriesPlotFor,
                Qt::UniqueConnection);
    }

    // MVC live-sync: when the Simulation Options dialog writes through the
    // layer's setOption(), the layer emits optionsChanged({keys}). Refresh
    // the status-bar widgets (Flow Units combo, Offset-Mode checkbox)
    // from the project's mirror of the engine state.
    //
    // Qt::UniqueConnection does NOT work with lambdas — Qt can't compare
    // functor identities and silently stacks duplicates on every
    // tab-switch. Disconnect all prior handlers on this layer from `this`
    // first, then connect exactly one fresh lambda. Without this guard,
    // every tab switch would accumulate another handler and a single
    // FLOW_UNITS change would trigger N refreshes.
    if (auto *layer = pw->modelLayer())
    {
        QObject::disconnect(layer, &SWMMModelLayer::optionsChanged,
                            this, nullptr);
        connect(layer, &SWMMModelLayer::optionsChanged, this,
                [this, pw](const QStringList &keys) {
                    if (pw != mActiveProjectWindow) return;
                    if (keys.contains(QStringLiteral("FLOW_UNITS"))) {
                        pw->unitSystem()->syncFromEngine(pw->modelLayer()->engine());
                        QSignalBlocker b(mComboBoxFlowUnits);
                        const int idx = mComboBoxFlowUnits->findData(
                            static_cast<int>(pw->unitSystem()->flowUnits()));
                        if (idx >= 0) mComboBoxFlowUnits->setCurrentIndex(idx);
                    }
                    if (keys.contains(QStringLiteral("LINK_OFFSETS"))) {
                        pw->reloadElevationOffsetModeFromEngine();
                        QSignalBlocker b(mCheckBoxLevelOffsetMode);
                        const bool elev = pw->isElevationOffsetMode();
                        mCheckBoxLevelOffsetMode->setChecked(elev);
                        mCheckBoxLevelOffsetMode->setText(
                            elev ? QStringLiteral("Elevation")
                                 : QStringLiteral("Depth"));
                    }
                });
    }

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
        // connection. Qt::UniqueConnection can't de-dupe lambdas, so
        // disconnect our prior handler from this manager first — every
        // tab switch gets exactly one live connection.
        QObject::disconnect(pw->selectionManager(),
                            &SelectionManager::selectionChanged,
                            this, nullptr);
        connect(pw->selectionManager(), &SelectionManager::selectionChanged,
                this,
                [this, pw](const QSet<SWMMObjectRef> &current,
                           const QSet<SWMMObjectRef> &,
                           const QSet<SWMMObjectRef> &) {
                    if (!mAttributePanel || pw != mActiveProjectWindow) return;
                    if (current.isEmpty())
                    {
                        mAttributePanel->clear();
                        return;
                    }
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
                });
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
    if (ui->commandLinkButtonClearRecentFiles)
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

    // Visual separator above the welcome page's Clear Recent Files button.
    // Only valid while the welcome screen is still alive.
    if (ui->welcomeWidget)
    {
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
    }

    // Welcome panel — replace existing buttons with current list (only
    // while the welcome screen is still alive).
    if (ui->frameRecentFiles)
    {
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
}

void SWMMVis::onSetProgressBarBusy(bool busy)
{
    // Indeterminate "busy" spinner — reserved for operations with no
    // measurable progress (e.g. loading a large .inp). Simulation runs
    // use updateSimulationProgressBar() which shows real percentage.
    mProgressBar->setRange(0, 0);
    mProgressBar->setValue(0);
    mProgressBar->setVisible(busy);
}

void SWMMVis::updateSimulationProgressBar()
{
    // No sims running → hide the bar.
    if (mRunningSimProgress.isEmpty())
    {
        mProgressBar->setVisible(false);
        mProgressBar->setRange(0, 0);
        return;
    }

    // Track the SLOWEST simulation so the bar reflects the overall
    // "will-all-sims-finish" estimate rather than racing ahead on the
    // fastest one.
    double minFrac = 1.0;
    for (double frac : std::as_const(mRunningSimProgress))
        minFrac = std::min(minFrac, frac);

    mProgressBar->setRange(0, 100);
    mProgressBar->setValue(qBound(0, int(minFrac * 100.0 + 0.5), 100));
    mProgressBar->setToolTip(
        mRunningSimProgress.size() == 1
            ? tr("Simulation: %1%").arg(int(minFrac * 100.0 + 0.5))
            : tr("%1 simulations running — slowest at %2%")
                  .arg(mRunningSimProgress.size())
                  .arg(int(minFrac * 100.0 + 0.5)));
    mProgressBar->setVisible(true);
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

    // Seed this sim's entry in the running-progress map at 0 so the
    // bottom bar starts showing 0% immediately (rather than busy
    // indeterminate). Progress ticks update the entry; finished()
    // removes it.
    mRunningSimProgress[jobId] = 0.0;
    updateSimulationProgressBar();

    // Create runner; wire signals → model; runner deletes itself after finish.
    auto *runner = new SimulationRunner(jobId, instanceName, inpPath, rptPath, outPath, this);
    mActiveRunners.insert(jobId, runner);

    connect(runner, &SimulationRunner::progressChanged,
            mSimStatusModel, &SimulationStatusModel::updateProgress);

    // Also feed the bottom status-bar progress bar (show min across
    // running sims as a real percent, not busy spinner).
    connect(runner, &SimulationRunner::progressChanged, this,
            [this](int runnerJobId, double frac,
                   const QDateTime & /*curSimDate*/,
                   double /*runoffErr*/, double /*routingErr*/) {
                mRunningSimProgress[runnerJobId] = frac;
                updateSimulationProgressBar();
            });

    connect(runner, &SimulationRunner::simulationDatesKnown,
            mSimStatusModel, &SimulationStatusModel::setSimulationDates);

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
                // Drop this job from the running-progress + runner maps;
                // the bottom progress bar hides automatically when the
                // last sim finishes.
                self->mRunningSimProgress.remove(finishedJobId);
                self->mActiveRunners.remove(finishedJobId);
                self->updateSimulationProgressBar();
                // Always drop the pause-toggle back to unchecked when the
                // last runner finishes — otherwise the next Run will
                // launch with pause pre-engaged.
                if (self->mActiveRunners.isEmpty()
                    && self->ui->actionPauseExecution->isChecked())
                {
                    QSignalBlocker b(self->ui->actionPauseExecution);
                    self->ui->actionPauseExecution->setChecked(false);
                }

                // Success path OR cancelled path — both preserve the .out
                // on disk (engine_end + engine_report run regardless). Log
                // different text and auto-load the partial results so the
                // user can still inspect whatever was written.
                const bool cancelled = !success && errCode == 0;
                if (cancelled) {
                    self->onLogMessage(
                        tr("Simulation cancelled. Partial results: %1")
                            .arg(outPathCopy),
                        OpenSWMMVisLogMessage::Warning);
                } else if (!success) {
                    self->onLogMessage(
                        tr("Simulation failed (code %1): %2 — %3")
                            .arg(errCode).arg(instanceName).arg(errMsg),
                        OpenSWMMVisLogMessage::Error);
                } else {
                    self->onLogMessage(
                        tr("Simulation finished. Results: %1").arg(outPathCopy));
                }

                // Auto-load the .out as a SWMMResultsLayer regardless of
                // cancel/success — the engine flushed partial output
                // either way, and the user explicitly asked for Cancel to
                // save results. Only skip on engine-error with no output.
                const bool hasResults = (success || cancelled)
                    && QFileInfo(outPathCopy).exists()
                    && QFileInfo(outPathCopy).size() > 0;
                if (hasResults && pwGuard && pwGuard->canvas() && pwGuard->modelLayer()) {
                    auto *rl = new SWMMResultsLayer(outPathCopy,
                                                    pwGuard->modelLayer());
                    rl->setName(QFileInfo(outPathCopy).fileName());
                    pwGuard->canvas()->addLayer(rl, true);
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
    // Auto-fit so the user sees what they just loaded — the layer may sit
    // far from the SWMM model in coordinate space and the canvas otherwise
    // keeps its prior viewport.
    c->zoomToFullExtent();
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
    c->zoomToFullExtent();
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

    auto *kFilters = openswmmvis::FileFilterRegistry::instance();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add SWMM Results"),
        mRecentFiles.isEmpty() ? QDir::homePath()
                               : QFileInfo(mRecentFiles.first()).absolutePath(),
        kFilters->filterFor(openswmmvis::FilterKind::ResultsRead));
    if (path.isEmpty()) return;

    auto *layer = new SWMMResultsLayer(path, pw->modelLayer());
    layer->setName(QFileInfo(path).fileName());
    pw->canvas()->addLayer(layer, true);
    onLogMessage(tr("Added results layer: %1").arg(QFileInfo(path).fileName()));
}
