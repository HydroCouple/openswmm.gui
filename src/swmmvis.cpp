/*!
 * \file   swmmvis.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */

#include <QCommandLinkButton>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
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
#include <QInputDialog>
#include <QJsonDocument>
#include <QCursor>      // Slice PT.1 — exec menu at pointer
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QJsonArray>
#include <QJsonObject>
#include <QProxyStyle>
#include <QSettings>
#include <QDateTimeEdit>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionProgressBar>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QThread>

#include <cmath>

#include "swmmvis.h"
#include "ui_swmmvis.h"
#include "version.h"
#include "legacy_version.h"

#include "core/unitsystem.h"
#include "ui/widgets/attributepickermenu.h"  // Slice PT.1 — picker for plotTimeSeries
#include "core/preferencesmanager.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "map/openswmmvisscene.h"
#include "map/mapextent.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "layers/swmmmodellayer.h"

// Slice BI-MK.LT — renderer classes used by onLayerKindStyleRequested.
#include "render/colorramp.h"
#include "render/ifeaturerenderer.h"
#include "render/intervalbinner.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
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
#include "ui/dialogs/profileplotdialog.h"
#include "ui/dialogs/comparisonplotdialog.h"
#include "plot/comparisonplotmodel.h"
#include "ui/dialogs/addbasemapdialog.h"
#include "ui/dialogs/symbologydialog.h"
#include "ui/widgets/legendoverlay.h"
#include "ui/widgets/perattributethemingwidget.h"
#include "ui/panels/layertreepanel.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/attributepanel.h"
#include "ui/panels/attributetablepanel.h"
#include "plugins/filefilterregistry.h"
#include "selection/selectionmanager.h"
#include "simulation/simulationrunner.h"
#include "simulation/simulationstatusmodel.h"

#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_engine.h>
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
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "mesh/inpmeshreader.h"
#include "animation/animationcontroller.h"
#include "ui/toolbars/terraintoolbar.h"
#include "map/tools/maptoolselect.h"
#include "map/tools/maptoolplotpick.h"
#include "map/tools/maptoolselectprofile.h"

#include <QDesktopServices>
#include <QDockWidget>
#include <QFile>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>
#include <QCommandLinkButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QValidator>
#include <QRegularExpression>

namespace {

// QGIS / ArcGIS-style preset scale list (largest scale → smallest, i.e.
// most-zoomed-in to most-zoomed-out).  Plain numerator-1 denominators.
const QVector<double> &kMapScalePresets()
{
    static const QVector<double> presets = {
        100, 200, 500,
        1000, 2000, 2500, 5000,
        10000, 25000, 50000, 100000,
        250000, 500000, 1000000,
        5000000, 10000000, 25000000
    };
    return presets;
}

// Format a denominator as "1:12,345" using grouping separators that match
// the user's locale (falls back to comma if the locale has none).
QString formatScaleText(double denom)
{
    if (denom <= 0.0 || !std::isfinite(denom))
        return QStringLiteral("1:1");
    // Round to nearest integer for display — fractional N would be confusing.
    const qlonglong n = static_cast<qlonglong>(std::llround(denom));
    return QStringLiteral("1:%1").arg(QLocale().toString(n));
}

// Parse "1:N", "N", "1 : N" etc. with grouping characters into a positive
// denominator.  Returns 0.0 if the string is not a valid scale.
double parseScaleText(const QString &raw)
{
    QString s = raw.trimmed();
    if (s.isEmpty()) return 0.0;

    // Drop the optional "1:" / "1 :" prefix.
    static const QRegularExpression prefixRe(QStringLiteral("^\\s*1\\s*:\\s*"));
    s.remove(prefixRe);

    // Strip grouping characters (commas, spaces, narrow-no-break, apostrophes).
    s.remove(QChar(','));
    s.remove(QChar(' '));
    s.remove(QChar(0x00A0));    // non-breaking space
    s.remove(QChar(0x202F));    // narrow no-break space
    s.remove(QChar('\''));

    bool ok = false;
    const double n = s.toDouble(&ok);
    return (ok && n > 0.0 && std::isfinite(n)) ? n : 0.0;
}

// QValidator that allows partial typing of "[1[ :]] <digits>[,. <digits>]…".
class MapScaleValidator : public QValidator {
public:
    explicit MapScaleValidator(QObject *parent = nullptr) : QValidator(parent) {}
    State validate(QString &input, int & /*pos*/) const override
    {
        if (input.trimmed().isEmpty()) return Intermediate;
        static const QRegularExpression re(
            QStringLiteral("^\\s*(?:1\\s*:?\\s*)?[\\d ,'\\xA0\\x{202F}]*\\.?\\d*\\s*$"));
        return re.match(input).hasMatch() ? Acceptable : Invalid;
    }
};

} // namespace

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

/**
 * @brief Pick a "nice" tick step from the 1 / 2 / 5 × 10^n ladder so that
 *        a slider of range [0, total] shows at most ~targetTicks ticks.
 *
 * Returns 0 when total < 2 (caller hides ticks entirely). Otherwise the
 * smallest step from {1, 2, 5, 10, 20, 50, ...} such that
 * total / step <= targetTicks.
 */
int niceTickStep(int total, int targetTicks = 50)
{
    if (total < 2) return 0;
    static constexpr int kMantissas[] = {1, 2, 5};
    int pow10 = 1;
    int idx   = 0;
    while (true) {
        const int step = kMantissas[idx] * pow10;
        if (step <= 0) return total;  // overflow guard
        if (total / step <= targetTicks) return step;
        if (++idx == 3) { idx = 0; pow10 *= 10; }
    }
}

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
    initializeTerrainToolBar();
}

void SWMMVis::applyEditSessionToActions(bool active)
{
    // Gated on the "Edit Existing" toggle: operations that mutate features
    // that already exist in the model. Adding new features is governed by
    // applyProjectOpenToActions and is available whenever a project is open.
    static const QStringList kEditOnlyActions = {
        QStringLiteral("actionMoveNode"),      QStringLiteral("actionEditVertex"),
        QStringLiteral("actionDeleteSelected"),
    };
    for (const QString &name : kEditOnlyActions)
        if (auto *act = findChild<QAction *>(name))
            act->setEnabled(active);
}

void SWMMVis::applyProjectOpenToActions(bool open)
{
    // Add-feature actions are available whenever a project is open; they
    // do not require an edit session. actionGenerateMesh is intentionally
    // omitted — it produces a separate .2dm artefact and is reachable
    // even before a project is bound.
    static const QStringList kProjectOnlyActions = {
        QStringLiteral("actionAddJunction"),   QStringLiteral("actionAddOutfall"),
        QStringLiteral("actionAddStorage"),    QStringLiteral("actionAddFlowDivider"),
        QStringLiteral("actionAddPipe"),       QStringLiteral("actionAddPump"),
        QStringLiteral("actionAddOrifice"),    QStringLiteral("actionAddWeir"),
        QStringLiteral("actionAddOutlet"),
        QStringLiteral("actionAddSubcatchment"),
        QStringLiteral("actionRainGauge"),
        QStringLiteral("actionAddNode"),       QStringLiteral("actionAddPolyline"),
        QStringLiteral("actionAddPolygon"),    QStringLiteral("actionAddText"),
    };
    for (const QString &name : kProjectOnlyActions)
        if (auto *act = findChild<QAction *>(name))
            act->setEnabled(open);
}

void SWMMVis::initializeTerrainToolBar()
{
    mTerrainToolbar = new TerrainToolbar(tr("Terrain"), this);
    addToolBar(Qt::TopToolBarArea, mTerrainToolbar);
}

void SWMMVis::initializeAnimationToolBar()
{
    mAnimationController = new AnimationController(this);

    mSliderAnimationTime = new QSlider(Qt::Horizontal, this);
    mSliderAnimationTime->setSingleStep(1);
    mSliderAnimationTime->setPageStep(1);
    mSliderAnimationTime->setTickPosition(QSlider::NoTicks);  // ticks appear once a run loads
    mSliderAnimationTime->setToolTip(tr("Animation Time"));
    mSliderAnimationTime->setStatusTip(tr("Animation Time"));
    mSliderAnimationTime->setMinimumWidth(300);

    mDateTimeEditAnimationTime = new QDateTimeEdit(this);
    mDateTimeEditAnimationTime->setDisplayFormat(QStringLiteral("MM/dd/yyyy hh:mm"));
    mDateTimeEditAnimationTime->setCalendarPopup(true);
    mDateTimeEditAnimationTime->setToolTip(tr("Animation Time"));
    mDateTimeEditAnimationTime->setStatusTip(tr("Animation Time"));

    ui->toolBarAnimation->insertWidget(ui->actionSkipForward, mSliderAnimationTime);
    ui->toolBarAnimation->insertWidget(ui->actionSkipForward, mDateTimeEditAnimationTime);

    // Speed selector — sits after Stop, before SkipForward (separator already
    // ends the play/pause/stop transport group via the .ui file).
    mLabelAnimationSpeed = new QLabel(tr("Speed:"), this);
    mLabelAnimationSpeed->setContentsMargins(6, 0, 4, 0);
    mComboAnimationSpeed = new QComboBox(this);
    mComboAnimationSpeed->setToolTip(tr("Animation playback speed multiplier"));
    mComboAnimationSpeed->setStatusTip(tr("Animation playback speed multiplier"));
    struct SpeedEntry { double value; const char *label; };
    static constexpr SpeedEntry kSpeeds[] = {
        {0.25, "0.25×"}, {0.5, "0.5×"}, {1.0, "1×"},
        {2.0,  "2×"},    {4.0, "4×"},   {8.0, "8×"},
    };
    for (const auto &e : kSpeeds)
        mComboAnimationSpeed->addItem(tr(e.label), e.value);

    ui->toolBarAnimation->insertWidget(ui->actionSkipForward, mLabelAnimationSpeed);
    ui->toolBarAnimation->insertWidget(ui->actionSkipForward, mComboAnimationSpeed);
    ui->toolBarAnimation->insertSeparator(ui->actionSkipForward);

    // Restore cross-launch default speed from PreferencesManager and push it
    // into the controller before the first play().
    const double prefSpeed = PreferencesManager::instance()->animationSpeed();
    int idx = mComboAnimationSpeed->findData(prefSpeed);
    if (idx < 0) idx = mComboAnimationSpeed->findData(1.0);
    mComboAnimationSpeed->setCurrentIndex(idx);
    mAnimationController->setSpeed(mComboAnimationSpeed->currentData().toDouble());

    connect(mComboAnimationSpeed, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int i) {
        const double speed = mComboAnimationSpeed->itemData(i).toDouble();
        if (speed <= 0.0) return;
        mAnimationController->setSpeed(speed);
        PreferencesManager::instance()->setAnimationSpeed(speed);
    });

    // Wire toolbar actions to controller.
    // actionPlay is checkable (toggle); use triggered(bool) so both check and
    // uncheck transitions are caught. Checked = playing, unchecked = paused.
    connect(ui->actionPlay, &QAction::triggered, this, [this](bool checked) {
        if (checked)
            mAnimationController->play();
        else
            mAnimationController->pause();
    });
    // Dedicated Pause button — pauses playback regardless of actionPlay
    // state, and stays checked while the controller is paused (so the
    // user can see "paused" at a glance). actionPlay's check state is
    // updated by the playStateChanged handler below, keeping the two
    // toggles consistent.
    connect(ui->actionPause, &QAction::triggered, this, [this]() {
        mAnimationController->pause();
    });
    connect(ui->actionStop,        &QAction::triggered,
            mAnimationController,  &AnimationController::stop);
    connect(ui->actionSkipBack,    &QAction::triggered,
            mAnimationController,  &AnimationController::stepBackward);
    connect(ui->actionSkipForward, &QAction::triggered,
            mAnimationController,  &AnimationController::stepForward);

    // Keep actionPlay / actionPause check states in sync with the
    // controller so stop() / seek-to-end / a Pause click each leave the
    // toolbar reflecting reality without a feedback loop.
    connect(mAnimationController, &AnimationController::playStateChanged,
            this, [this](bool playing) {
        QSignalBlocker bPlay(ui->actionPlay);
        QSignalBlocker bPause(ui->actionPause);
        ui->actionPlay->setChecked(playing);
        ui->actionPause->setChecked(!playing);
    });

    // Slider ↔ controller (bidirectional, guarded against feedback loops).
    connect(mAnimationController, &AnimationController::currentPeriodChanged,
            this, [this](int period) {
        QSignalBlocker b(mSliderAnimationTime);
        mSliderAnimationTime->setValue(period);
    });
    connect(mSliderAnimationTime, &QSlider::valueChanged,
            mAnimationController, &AnimationController::seekToPeriod);

    // DateTime display (read-only — controller drives it). Also fans the
    // time scrub out to any 2D results layer on the active canvas so the
    // single slider drives both 1D and 2D playback in lockstep.
    connect(mAnimationController, &AnimationController::currentTimeChanged,
            this, [this](const QDateTime &dt) {
        QSignalBlocker b(mDateTimeEditAnimationTime);
        mDateTimeEditAnimationTime->setDateTime(dt);

        if (auto *pw = activeProjectWindow()) {
            if (auto *canvas = pw->canvas()) {
                for (OpenSWMMVisLayer *l : canvas->layers()) {
                    if (auto *r2d = qobject_cast<SWMM2DResultsLayer *>(l)) {
                        r2d->setCurrentSimTime(dt);
                    }
                }
            }
        }
    });

    // Slider range tracks total periods. Tick density is recomputed from a
    // 1/2/5×10ⁿ ladder so long runs (10³–10⁵ periods) don't smear the rail.
    connect(mAnimationController, &AnimationController::totalPeriodsChanged,
            this, [this](int total) {
        mSliderAnimationTime->setRange(0, qMax(0, total - 1));
        const int step = niceTickStep(total);
        if (step <= 0) {
            mSliderAnimationTime->setTickPosition(QSlider::NoTicks);
            mSliderAnimationTime->setTickInterval(0);
            mSliderAnimationTime->setPageStep(1);
        } else {
            mSliderAnimationTime->setTickPosition(QSlider::TicksBelow);
            mSliderAnimationTime->setTickInterval(step);
            mSliderAnimationTime->setPageStep(step);
        }
    });
}

void SWMMVis::initializeMapTools()
{
    // All tool actions are checkable — the activeToolChanged handler below
    // keeps exactly one checked at a time to show which tool is active.
    const QStringList toolActionNames = {
        QStringLiteral("actionPan"),     QStringLiteral("actionZoomIn"),
        QStringLiteral("actionZoomOut"), QStringLiteral("actionSelect"),
        QStringLiteral("actionMeasure"), QStringLiteral("actionPlotProfile"),
        QStringLiteral("actionAddJunction"), QStringLiteral("actionAddOutfall"),
        QStringLiteral("actionAddStorage"), QStringLiteral("actionAddFlowDivider"),
        QStringLiteral("actionAddPipe"),  QStringLiteral("actionAddPump"),
        QStringLiteral("actionAddOrifice"), QStringLiteral("actionAddWeir"),
        QStringLiteral("actionAddOutlet"),
        QStringLiteral("actionRainGauge"), QStringLiteral("actionAddSubcatchment"),
    };
    for (const QString &name : toolActionNames) {
        if (auto *act = findChild<QAction *>(name))
            act->setCheckable(true);
    }

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
    // "Edit Existing" toggle — must be the first button in the editing
    // toolbar. Governs operations that modify already-placed features
    // (move/edit-vertex/delete); adding new features is governed by
    // applyProjectOpenToActions and stays available whenever a project
    // is open.
    ui->actionEditExisting->setEnabled(false);  // disabled until a project opens
    connect(ui->actionEditExisting, &QAction::toggled, this, [this](bool active) {
        if (auto *pw = activeProjectWindow())
            pw->setEditSessionActive(active);
        applyEditSessionToActions(active);
    });

    connect(ui->actionSelect, &QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activateSelectTool();
    });
    connect(ui->actionMeasure,&QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activateMeasureTool();
    });
    connect(ui->actionPlotProfile, &QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activateSelectProfileTool();
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

    // Disable all edit-session-gated actions at startup (no project open yet).
    applyEditSessionToActions(false);
}

void SWMMVis::initializeStatusBar()
{
    auto addSep = [this]() {
        QFrame *sep = new QFrame(ui->statusBar);
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);
        ui->statusBar->addPermanentWidget(sep);
    };

    // Engine version — first permanent widget so it appears leftmost on the right side.
    ui->statusBar->addPermanentWidget(new QLabel("Engine:", ui->statusBar));
    mComboBoxEngineVersion = new QComboBox(ui->statusBar);
    mComboBoxEngineVersion->addItem(
        tr("OpenSWMM %1").arg(QLatin1String(SWMM_VERSION_FULL)),
        QLatin1String(SWMM_VERSION));
    mComboBoxEngineVersion->addItem(
        tr("SWMM %1 (Legacy)").arg(QLatin1String(OPENSWMM_LEGACY_FULL_VERSION)),
        QLatin1String(LEGACY_SWMM_VERSION));
    mComboBoxEngineVersion->setToolTip(tr("Select which SWMM engine version to use when running simulations"));
    mComboBoxEngineVersion->setEnabled(false);
    connect(mComboBoxEngineVersion, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (auto *pw = activeProjectWindow())
            pw->setEngineVersion(mComboBoxEngineVersion->currentData().toString());
    });
    ui->statusBar->addPermanentWidget(mComboBoxEngineVersion);
    addSep();

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

    // Coordinates — wide enough for "X: 12345678.123456, Y: 3456789.654321  Z: 1234.567"
    ui->statusBar->addPermanentWidget(new QLabel("Coordinates:", ui->statusBar));
    mLineEditCoordinates = new QLineEdit("0,0", ui->statusBar);
    mLineEditCoordinates->setReadOnly(true);
    mLineEditCoordinates->setMinimumWidth(420);
    mLineEditCoordinates->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->statusBar->addPermanentWidget(mLineEditCoordinates);
    addSep();

    // Map scale — editable combo with GIS-style presets.  The combo is wired
    // bidirectionally in onActiveSubWindowChanged() once an active canvas
    // exists: typing/picking calls MapCanvas::setScaleDenominator(); the
    // canvas's scaleChanged() signal repopulates the edit text.
    ui->statusBar->addPermanentWidget(new QLabel("Map Scale:", ui->statusBar));
    mComboBoxMapScale = new QComboBox(ui->statusBar);
    mComboBoxMapScale->setEditable(true);
    mComboBoxMapScale->setInsertPolicy(QComboBox::NoInsert);
    mComboBoxMapScale->setMinimumWidth(150);
    mComboBoxMapScale->setToolTip(tr("Map scale — pick a preset or type 1:N"));
    mComboBoxMapScale->setEnabled(false);   // enabled once a canvas binds
    for (double d : kMapScalePresets())
        mComboBoxMapScale->addItem(formatScaleText(d), d);
    mComboBoxMapScale->setEditText(QString());
    mComboBoxMapScale->lineEdit()->setValidator(new MapScaleValidator(mComboBoxMapScale));
    // Preset click → activated(QString); typed entry → editingFinished().
    // Both route through the same parse-and-apply slot.
    connect(mComboBoxMapScale, QOverload<int>::of(&QComboBox::activated),
            this, [this](int idx) {
                if (idx < 0) return;
                onMapScaleEntered(mComboBoxMapScale->itemText(idx));
            });
    connect(mComboBoxMapScale->lineEdit(), &QLineEdit::editingFinished,
            this, [this]() {
                onMapScaleEntered(mComboBoxMapScale->currentText());
            });
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

    // Per-attribute theming combos (Node / Link / Subcatchment). Subscribes
    // to AnimationController::primaryLayerChanged so the combos always
    // reflect the active SWMMResultsLayer's current variable.
    addSep();
    mThemingWidget = new openswmmvis::ui::PerAttributeThemingWidget(ui->statusBar);
    mThemingWidget->setAnimationController(mAnimationController);
    ui->statusBar->addPermanentWidget(mThemingWidget);
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

    // Right-click "Set Style…" → open the symbology dialog for that layer.
    // Mirrors the animation-toolbar's actionSetStyle but skips the
    // "find the active layer" fallback because the menu already knows
    // which layer was clicked.
    connect(mLayerTreePanel, &LayerTreePanel::layerStyleRequested,
            this, [this](OpenSWMMVisLayer *layer) {
                if (!layer) return;
                openswmmvis::ui::SymbologyDialog dlg(layer, this);
                if (dlg.exec() == QDialog::Accepted) {
                    if (auto *c = activeCanvas())
                        c->invalidate(MapCanvas::Scene,
                                      QStringLiteral("symbology-apply"));
                }
            });

    // Slice BI-MK.LT — right-click on a kind sub-row's Style submenu. The
    // signal carries (layer, kindOrdinal, rendererId) where rendererId is
    // "single" / "graduated" / "categorized" (or empty for "open dialog").
    // BI-MK.1 dialog isn't shipped yet, so for v1 we install a default-
    // configured renderer of the matching class and let canvas paint /
    // legend pick it up; the user can later tune attribute / ramp via
    // the dialog when BI-MK.1 lands.
    connect(mLayerTreePanel, &LayerTreePanel::layerKindStyleRequested,
            this, &SWMMVis::onLayerKindStyleRequested);

    // Slice PT.1 — kind-row "Plot timeseries…" → build a SWMMObjectRef
    // from kindOrdinal + name and route to the existing AT.2 picker
    // (openTimeSeriesPlotFor pops the variable menu + opens Comparison
    // Plot Dialog).
    connect(mLayerTreePanel, &LayerTreePanel::plotKindObjectRequested,
            this, [this](int kindOrd, const QString &name) {
                if (name.isEmpty()) return;
                SWMMObjectRef ref;
                ref.name = name;
                const auto cat = static_cast<SWMMModelLayer::Category>(kindOrd);
                switch (cat) {
                case SWMMModelLayer::CatJunctions:
                case SWMMModelLayer::CatOutfalls:
                case SWMMModelLayer::CatStorage:
                case SWMMModelLayer::CatDividers:
                    ref.objectType = SWMMObjectRef::Node;     break;
                case SWMMModelLayer::CatConduits:
                case SWMMModelLayer::CatPumps:
                case SWMMModelLayer::CatOrifices:
                case SWMMModelLayer::CatWeirs:
                case SWMMModelLayer::CatOutlets:
                    ref.objectType = SWMMObjectRef::Link;     break;
                case SWMMModelLayer::CatSubcatchments:
                    ref.objectType = SWMMObjectRef::Subcatchment; break;
                case SWMMModelLayer::CatRainGages:
                    ref.objectType = SWMMObjectRef::RainGage; break;
                default:
                    ref.objectType = SWMMObjectRef::Unknown;  break;
                }
                openTimeSeriesPlotFor(ref);
            });
}

void SWMMVis::onLayerKindStyleRequested(OpenSWMMVisLayer *layer,
                                        int kindOrdinal,
                                        const QString &rendererId)
{
    if (kindOrdinal < 0 || kindOrdinal >= SWMMModelLayer::NumCategories) return;
    const auto cat = static_cast<SWMMModelLayer::Category>(kindOrdinal);

    auto *swmm    = qobject_cast<SWMMModelLayer   *>(layer);
    auto *results = qobject_cast<SWMMResultsLayer *>(layer);
    if (!swmm && !results) return;

    using namespace OpenSWMM::Render;

    // Slice CTX.2 — smart Style submenu. Whether the user picked
    // Single/Graduated/Categorized, the rule is:
    //   1. If the kind's CURRENT renderer matches the requested class,
    //      keep its tuning intact and just open the dialog scoped to
    //      that kind + tab. Picking the same class twice no longer
    //      destroys the user's settings.
    //   2. If the class differs (or no class was requested), install
    //      sensible defaults for the new class, THEN open the dialog.
    // Slice OUT.3 — same logic now applies to SWMMResultsLayer kind
    // rows; default attribute candidates come from the output-layer
    // candidate helper (NodeDepth / LinkFlow / SubcatchRunoff).
    IFeatureRenderer *cur = swmm ? swmm->kindRenderer(cat)
                                 : results->kindRenderer(cat);
    const QString currentClass = cur ? cur->rendererId() : QString();

    if (!rendererId.isEmpty() && rendererId != currentClass) {
        // Sensible-defaults config for the new renderer class.
        std::unique_ptr<IFeatureRenderer> next;
        if (rendererId == QStringLiteral("single")) {
            if (swmm) swmm->resetKindRendererToDefaults(cat);
            else      results->resetKindRendererToDefaults(cat);
            if (auto *cv = activeCanvas())
                cv->invalidate(MapCanvas::Scene,
                               QStringLiteral("symbology-apply"));
            // Fall through to open the dialog scoped to the kind.
        } else if (rendererId == QStringLiteral("graduated")) {
            auto g = std::make_unique<GraduatedRenderer>();
            if (swmm) {
                switch (cat) {
                case SWMMModelLayer::CatJunctions:
                case SWMMModelLayer::CatOutfalls:
                case SWMMModelLayer::CatStorage:
                case SWMMModelLayer::CatDividers:
                    g->setClassifyAttribute(QStringLiteral("maxDepth")); break;
                case SWMMModelLayer::CatConduits:
                    g->setClassifyAttribute(QStringLiteral("geom1")); break;
                case SWMMModelLayer::CatPumps:
                case SWMMModelLayer::CatOrifices:
                case SWMMModelLayer::CatWeirs:
                case SWMMModelLayer::CatOutlets:
                    g->setClassifyAttribute(QStringLiteral("maxFlow")); break;
                case SWMMModelLayer::CatSubcatchments:
                    g->setClassifyAttribute(QStringLiteral("area")); break;
                case SWMMModelLayer::CatRainGages:
                    g->setClassifyAttribute(QStringLiteral("recordingInterval")); break;
                default: break;
                }
            } else {
                // Output layer: pick the scope's primary result variable
                // (matches the per-kind renderer defaults in OUT.1).
                switch (cat) {
                case SWMMModelLayer::CatJunctions:
                case SWMMModelLayer::CatOutfalls:
                case SWMMModelLayer::CatStorage:
                case SWMMModelLayer::CatDividers:
                    g->setClassifyAttribute(QStringLiteral("NodeDepth")); break;
                case SWMMModelLayer::CatConduits:
                case SWMMModelLayer::CatPumps:
                case SWMMModelLayer::CatOrifices:
                case SWMMModelLayer::CatWeirs:
                case SWMMModelLayer::CatOutlets:
                    g->setClassifyAttribute(QStringLiteral("LinkFlow")); break;
                case SWMMModelLayer::CatSubcatchments:
                    g->setClassifyAttribute(QStringLiteral("SubcatchRunoff")); break;
                default: break;
                }
            }
            g->setRamp(RasterColorRamp::viridis(0.0, 1.0));
            IntervalBinner b;
            b.setMethod(BinMethod::EqualInterval);
            b.setBinCount(5);
            g->setBinner(b);
            next = std::move(g);
        } else if (rendererId == QStringLiteral("categorized")) {
            auto c = std::make_unique<CategorizedRenderer>();
            c->setClassifyAttribute(QStringLiteral("tag"));
            next = std::move(c);
        }

        if (next) {
            if (swmm) swmm->setKindRenderer(cat, std::move(next));
            else      results->setKindRenderer(cat, std::move(next));
            if (auto *cv = activeCanvas())
                cv->invalidate(MapCanvas::Scene,
                               QStringLiteral("symbology-apply"));
        }
    }
    // If rendererId matches currentClass, we preserve the tuning and
    // fall through directly to the dialog — that's the CTX.2 fix.

    // Open the kind-scoped dialog with the right tab pre-selected.
    openswmmvis::ui::SymbologyDialog dlg(layer, kindOrdinal,
                                          rendererId, this);
    if (dlg.exec() == QDialog::Accepted) {
        if (auto *cv = activeCanvas())
            cv->invalidate(MapCanvas::Scene,
                           QStringLiteral("symbology-apply"));
    }
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
    // Slice PT.1 — was: forward to openComparisonPlotFor (default
    // attribute only). Now: pop AttributePickerMenu first so the user
    // picks WHICH variable to plot. Used by both ObjectBrowserPanel
    // and MapToolSelect's right-click → "Plot Time Series" entry.

    using openswmmvis::plot::ObjectRef;
    using openswmmvis::plot::PlotAttribute;

    // Map SWMMObjectRef kind → plot::ObjectRef::Kind.
    ObjectRef::Kind kind = ObjectRef::Kind::Unknown;
    switch (ref.objectType) {
    case SWMMObjectRef::Node:         kind = ObjectRef::Kind::Node;     break;
    case SWMMObjectRef::Link:         kind = ObjectRef::Kind::Link;     break;
    case SWMMObjectRef::Subcatchment: kind = ObjectRef::Kind::Subcatch; break;
    default:                          break;
    }
    if (kind == ObjectRef::Kind::Unknown) {
        // Non-plottable kind — fall back to the existing path which
        // shows a friendly "no plot available" tooltip via the dialog.
        openComparisonPlotFor(ref);
        return;
    }

    const auto units = UnitSystem::instance() && UnitSystem::instance()->isSI()
        ? openswmmvis::plot::UnitSystem::SI
        : openswmmvis::plot::UnitSystem::US;
    QMenu *menu = openswmmvis::ui::AttributePickerMenu::createForObjectKind(
        kind, units, this);
    if (!menu) {
        openComparisonPlotFor(ref);
        return;
    }

    menu->setTitle(tr("Plot %1 …").arg(ref.name));
    // Pop at the cursor so the menu lands on the user's pointer
    // (works for ObjectBrowserPanel right-click + MapToolSelect both).
    QAction *picked = menu->exec(QCursor::pos());
    const PlotAttribute attr = picked
        ? openswmmvis::ui::AttributePickerMenu::attributeFrom(picked)
        : PlotAttribute::Unknown;
    menu->deleteLater();

    if (!picked) return;   // user cancelled

    if (attr == PlotAttribute::Unknown) {
        // "All attributes" sentinel — fall back to the default-only path
        // (the dialog handles multi-series itself).
        openComparisonPlotFor(ref);
    } else {
        openComparisonPlotForAttribute(ref, attr);
    }
}

void SWMMVis::openComparisonPlotFor(const SWMMObjectRef &ref)
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas()) return;

    // Locate the first SWMMResultsLayer on the active canvas.
    SWMMResultsLayer *resultsLayer = nullptr;
    for (OpenSWMMVisLayer *l : pw->canvas()->layers())
    {
        if (l->layerType() == OpenSWMMVisLayer::SWMMResultsLayer)
        {
            resultsLayer = qobject_cast<SWMMResultsLayer *>(l);
            if (resultsLayer) break;
        }
    }
    if (!resultsLayer)
    {
        QMessageBox::information(this, tr("No results loaded"),
            tr("Run a simulation first (toolbar's Execute button) "
               "or add a SWMM Output (.out) layer."));
        return;
    }

    // Find-or-create the dialog; reuse across calls.
    auto *dlg = findChild<openswmmvis::ui::ComparisonPlotDialog *>();
    if (!dlg) {
        dlg = new openswmmvis::ui::ComparisonPlotDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &openswmmvis::ui::ComparisonPlotDialog::addFromMapToggled,
                this, &SWMMVis::onAddFromMapToggled);
        // Slice AT.3 polish — drive the dialog's animation cursor from the
        // global AnimationController so the dashed vertical line moves
        // during sim playback. Connection auto-disconnects when the dialog
        // dies via WA_DeleteOnClose.
        if (mAnimationController) {
            connect(mAnimationController, &AnimationController::currentTimeChanged,
                    dlg->model(), &openswmmvis::plot::ComparisonPlotModel::setAnimationTime);
        }
    }

    const int runIdx = dlg->ensureRunSourceForLayer(resultsLayer);

    // Map SWMMObjectRef::ObjectType → ObjectRef::Kind + pick a default attribute.
    using PA = openswmmvis::plot::PlotAttribute;
    openswmmvis::plot::ObjectRef::Kind kind = openswmmvis::plot::ObjectRef::Kind::Unknown;
    PA defaultAttr = PA::Unknown;
    switch (ref.objectType) {
    case SWMMObjectRef::Node:
        kind = openswmmvis::plot::ObjectRef::Kind::Node;
        defaultAttr = PA::NodeDepth;
        break;
    case SWMMObjectRef::Link:
        kind = openswmmvis::plot::ObjectRef::Kind::Link;
        defaultAttr = PA::LinkFlow;
        break;
    case SWMMObjectRef::Subcatchment:
        kind = openswmmvis::plot::ObjectRef::Kind::Subcatch;
        defaultAttr = PA::SubcatchRunoff;
        break;
    default:
        // Other object kinds aren't plottable through .out.
        break;
    }

    if (kind != openswmmvis::plot::ObjectRef::Kind::Unknown &&
        defaultAttr != PA::Unknown)
    {
        openswmmvis::plot::ObjectRef objRef(kind, ref.name);
        dlg->addSeries(runIdx, objRef, defaultAttr);
    }

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void SWMMVis::openComparisonPlotForAttribute(const SWMMObjectRef &ref,
                                              openswmmvis::plot::PlotAttribute attribute)
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas()) return;

    SWMMResultsLayer *resultsLayer = nullptr;
    for (OpenSWMMVisLayer *l : pw->canvas()->layers()) {
        if (l->layerType() == OpenSWMMVisLayer::SWMMResultsLayer) {
            resultsLayer = qobject_cast<SWMMResultsLayer *>(l);
            if (resultsLayer) break;
        }
    }
    if (!resultsLayer) {
        QMessageBox::information(this, tr("No results loaded"),
            tr("Run a simulation first or add a SWMM Output (.out) layer."));
        return;
    }

    auto *dlg = findChild<openswmmvis::ui::ComparisonPlotDialog *>();
    if (!dlg) {
        dlg = new openswmmvis::ui::ComparisonPlotDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &openswmmvis::ui::ComparisonPlotDialog::addFromMapToggled,
                this, &SWMMVis::onAddFromMapToggled);
        // Slice AT.3 polish — drive the dialog's animation cursor from the
        // global AnimationController so the dashed vertical line moves
        // during sim playback. Connection auto-disconnects when the dialog
        // dies via WA_DeleteOnClose.
        if (mAnimationController) {
            connect(mAnimationController, &AnimationController::currentTimeChanged,
                    dlg->model(), &openswmmvis::plot::ComparisonPlotModel::setAnimationTime);
        }
    }
    const int runIdx = dlg->ensureRunSourceForLayer(resultsLayer);

    using PA = openswmmvis::plot::PlotAttribute;
    using PKind = openswmmvis::plot::ObjectRef::Kind;
    PKind kind = PKind::Unknown;
    switch (ref.objectType) {
    case SWMMObjectRef::Node:         kind = PKind::Node;     break;
    case SWMMObjectRef::Link:         kind = PKind::Link;     break;
    case SWMMObjectRef::Subcatchment: kind = PKind::Subcatch; break;
    default: break;
    }
    if (kind == PKind::Unknown) { dlg->show(); dlg->raise(); dlg->activateWindow(); return; }

    openswmmvis::plot::ObjectRef objRef(kind, ref.name);

    if (attribute == PA::Unknown) {
        // "All attributes" sentinel — fan out across every attribute valid
        // for the object kind. Mirrors AttributePickerMenu's enumeration.
        const PA nodeAttrs[]   = {PA::NodeDepth, PA::NodeHead, PA::NodeVolume,
                                  PA::NodeLateralInflow, PA::NodeTotalInflow,
                                  PA::NodeOverflow};
        const PA linkAttrs[]   = {PA::LinkFlow, PA::LinkDepth, PA::LinkVelocity,
                                  PA::LinkVolume, PA::LinkCapacity};
        const PA subAttrs[]    = {PA::SubcatchRainfall, PA::SubcatchSnowDepth,
                                  PA::SubcatchEvap, PA::SubcatchInfil,
                                  PA::SubcatchRunoff};
        switch (kind) {
        case PKind::Node:     for (PA a : nodeAttrs) dlg->addSeries(runIdx, objRef, a); break;
        case PKind::Link:     for (PA a : linkAttrs) dlg->addSeries(runIdx, objRef, a); break;
        case PKind::Subcatch: for (PA a : subAttrs)  dlg->addSeries(runIdx, objRef, a); break;
        default: break;
        }
    } else {
        dlg->addSeries(runIdx, objRef, attribute);
    }

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void SWMMVis::openComparisonPlotForSystemAttribute(openswmmvis::plot::PlotAttribute attribute)
{
    if (attribute == openswmmvis::plot::PlotAttribute::Unknown) return;
    if (!openswmmvis::plot::isSystemAttribute(attribute)) return;

    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas()) return;

    SWMMResultsLayer *resultsLayer = nullptr;
    for (OpenSWMMVisLayer *l : pw->canvas()->layers()) {
        if (l->layerType() == OpenSWMMVisLayer::SWMMResultsLayer) {
            resultsLayer = qobject_cast<SWMMResultsLayer *>(l);
            if (resultsLayer) break;
        }
    }
    if (!resultsLayer) {
        QMessageBox::information(this, tr("No results loaded"),
            tr("Run a simulation first or add a SWMM Output (.out) layer."));
        return;
    }

    auto *dlg = findChild<openswmmvis::ui::ComparisonPlotDialog *>();
    if (!dlg) {
        dlg = new openswmmvis::ui::ComparisonPlotDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &openswmmvis::ui::ComparisonPlotDialog::addFromMapToggled,
                this, &SWMMVis::onAddFromMapToggled);
        // Slice AT.3 polish — drive the dialog's animation cursor from the
        // global AnimationController so the dashed vertical line moves
        // during sim playback. Connection auto-disconnects when the dialog
        // dies via WA_DeleteOnClose.
        if (mAnimationController) {
            connect(mAnimationController, &AnimationController::currentTimeChanged,
                    dlg->model(), &openswmmvis::plot::ComparisonPlotModel::setAnimationTime);
        }
    }
    const int runIdx = dlg->ensureRunSourceForLayer(resultsLayer);
    dlg->addSeries(runIdx, openswmmvis::plot::ObjectRef::forSystem(), attribute);

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void SWMMVis::onAddFromMapToggled(bool active)
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas()) return;
    auto *dlg = findChild<openswmmvis::ui::ComparisonPlotDialog *>();
    if (!dlg) return;
    MapCanvas *canvas = pw->canvas();

    if (active) {
        // Avoid double-push (dialog re-toggles during programmatic setChecked).
        if (mPlotPickTool) return;

        mPrevMapTool   = canvas->activeTool();
        mPlotPickTool  = new OpenSWMMVisMapToolPlotPick(canvas, this);

        connect(mPlotPickTool, &OpenSWMMVisMapToolPlotPick::objectPicked,
                this, &SWMMVis::openComparisonPlotForAttribute);
        connect(mPlotPickTool, &OpenSWMMVisMapToolPlotPick::plotSystemRequested,
                this, &SWMMVis::openComparisonPlotForSystemAttribute);
        connect(mPlotPickTool, &OpenSWMMVisMapToolPlotPick::cancelled,
                this, [this, dlg]() {
                    // Bouncing through the dialog's action keeps the
                    // toolbar button in sync; setAddFromMapChecked(false)
                    // re-fires addFromMapToggled(false) which lands us
                    // back here with active=false.
                    dlg->setAddFromMapChecked(false);
                });

        canvas->setActiveTool(mPlotPickTool);
        statusBar()->showMessage(
            tr("Click an object on the map to plot. Esc cancels."), 5000);
    } else {
        if (!mPlotPickTool) return;
        // Restore the previously active tool (which may be a Select
        // tool that's been since destroyed — guard with the QPointer).
        canvas->setActiveTool(mPrevMapTool ? mPrevMapTool.data() : nullptr);
        mPlotPickTool->deleteLater();
        mPlotPickTool.clear();
        mPrevMapTool.clear();
        statusBar()->showMessage(tr("Add-from-Map cancelled."), 3000);
    }
}

void SWMMVis::openComparisonPlotForCells(SWMM2DResultsLayer *layer,
                                          const QVector<int> &triIdxList)
{
    if (!layer || triIdxList.isEmpty())
        return;

    auto *pw = activeProjectWindow();
    if (!pw) return;

    // Find-or-create the comparison dialog.
    auto *dlg = findChild<openswmmvis::ui::ComparisonPlotDialog *>();
    if (!dlg) {
        dlg = new openswmmvis::ui::ComparisonPlotDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &openswmmvis::ui::ComparisonPlotDialog::addFromMapToggled,
                this, &SWMMVis::onAddFromMapToggled);
        // Slice AT.3 polish — drive the dialog's animation cursor from the
        // global AnimationController so the dashed vertical line moves
        // during sim playback. Connection auto-disconnects when the dialog
        // dies via WA_DeleteOnClose.
        if (mAnimationController) {
            connect(mAnimationController, &AnimationController::currentTimeChanged,
                    dlg->model(), &openswmmvis::plot::ComparisonPlotModel::setAnimationTime);
        }
    }

    const int runIdx = dlg->ensureRunSourceForMeshLayer(layer);
    if (runIdx < 0) {
        QMessageBox::warning(this, tr("Mesh layer unavailable"),
            tr("Couldn't attach the 2D mesh layer to the comparison plot."));
        return;
    }

    // Ask the user which attributes to plot — small modal popover. Defaults
    // to Depth only (cheapest path; velocity reconstruction is per-tick).
    QDialog popover(this);
    popover.setWindowTitle(tr("Plot cell time series"));
    auto *vbox = new QVBoxLayout(&popover);
    vbox->addWidget(new QLabel(
        tr("Plot %1 selected cell(s) — choose attributes:").arg(triIdxList.size()),
        &popover));
    auto *cbDepth = new QCheckBox(tr("Depth"),              &popover);  cbDepth->setChecked(true);
    auto *cbHGL   = new QCheckBox(tr("HGL / water surface"),&popover);
    auto *cbVMag  = new QCheckBox(tr("|V| (velocity magnitude)"), &popover);
    auto *cbVx    = new QCheckBox(tr("Vx (velocity east)"), &popover);
    auto *cbVy    = new QCheckBox(tr("Vy (velocity north)"),&popover);
    cbVMag->setEnabled(layer->hasVelocityData());
    cbVx  ->setEnabled(layer->hasVelocityData());
    cbVy  ->setEnabled(layer->hasVelocityData());
    if (!layer->hasVelocityData()) {
        const QString tip = tr("Edge-flux data not present — re-run with "
                               "current engine to enable velocity series.");
        cbVMag->setToolTip(tip);
        cbVx  ->setToolTip(tip);
        cbVy  ->setToolTip(tip);
    }
    vbox->addWidget(cbDepth);
    vbox->addWidget(cbHGL);
    vbox->addWidget(cbVMag);
    vbox->addWidget(cbVx);
    vbox->addWidget(cbVy);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &popover);
    QObject::connect(bb, &QDialogButtonBox::accepted, &popover, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &popover, &QDialog::reject);
    vbox->addWidget(bb);

    if (popover.exec() != QDialog::Accepted)
        return;

    using PA = openswmmvis::plot::PlotAttribute;
    QVector<PA> attrs;
    if (cbDepth->isChecked()) attrs.push_back(PA::Mesh2DDepth);
    if (cbHGL  ->isChecked()) attrs.push_back(PA::Mesh2DHGL);
    if (cbVMag ->isChecked()) attrs.push_back(PA::Mesh2DVelocityMag);
    if (cbVx   ->isChecked()) attrs.push_back(PA::Mesh2DVelocityX);
    if (cbVy   ->isChecked()) attrs.push_back(PA::Mesh2DVelocityY);

    if (attrs.isEmpty())
        return;

    // Warn on large selections (per CF.3 edge case: 500-cell threshold).
    const int total = triIdxList.size() * attrs.size();
    if (total > 500) {
        const auto choice = QMessageBox::question(this,
            tr("Many series"),
            tr("This will create %1 series (%2 cells × %3 attributes). "
               "Continue?")
                .arg(total).arg(triIdxList.size()).arg(attrs.size()));
        if (choice != QMessageBox::Yes) return;
    }

    dlg->addCellSeries(runIdx, triIdxList, attrs);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void SWMMVis::openProfilePlotFor(const ProfileRouter::Path &path)
{
    auto *pw = activeProjectWindow();
    if (!pw) return;
    SWMMModelLayer *model = pw->modelLayer();
    if (!model) {
        QMessageBox::information(this, tr("No model loaded"),
            tr("Open a SWMM model before plotting a profile."));
        return;
    }
    if (path.nodes.size() < 2) return;

    // Parent = nullptr → fully independent top-level window with its own
    // dock icon on macOS.  WA_DeleteOnClose owns the lifetime.  The
    // dialog queries the project window dynamically for the active
    // terrain raster so loading a terrain *after* the dialog is open
    // still updates the ground line.
    auto *dlg = new ProfilePlotDialog(model, mAnimationController, path,
                                      pw, /*parent=*/nullptr);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Route the profile dialog's attribute-picker right-click into the
    // same ComparisonPlotDialog path as the map view, so the user picks
    // the variable to plot (depth, flow, …) directly from the submenu
    // rather than getting whatever the dialog defaults to.
    connect(dlg, &ProfilePlotDialog::plotAttributeRequested,
            this, &SWMMVis::openComparisonPlotForAttribute);
    dlg->show();
}

void SWMMVis::initializeAttributePanelDockWidget()
{
    // Property browser (single-object detail view) — right dock.
    mAttributePanel = new AttributePanel(this);
    mAttributePanel->setObjectName(QStringLiteral("dockWidgetAttributePanel"));
    addDockWidget(Qt::RightDockWidgetArea, mAttributePanel);

    // Attribute table (all objects, tabular grid) — bottom dock.
    mAttributeTablePanel = new AttributeTablePanel(this);
    auto *tableDock = new QDockWidget(tr("Attribute Table"), this);
    tableDock->setObjectName(QStringLiteral("dockWidgetAttributeTable"));
    tableDock->setWidget(mAttributeTablePanel);
    addDockWidget(Qt::BottomDockWidgetArea, tableDock);

    // Two-way sync between property browser and attribute table so an edit
    // in either view immediately reflects in the other without a full refresh.
    connect(mAttributePanel, &AttributePanel::objectEdited,
            mAttributeTablePanel, &AttributeTablePanel::onObjectEditedExternally);
    connect(mAttributeTablePanel, &AttributeTablePanel::objectEdited,
            mAttributePanel, &AttributePanel::onObjectEditedExternally);
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
    view->setColumnWidth(SimulationStatusModel::ColVersion,      90);
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
    connect(ui->actionSave,    &QAction::triggered, this, &SWMMVis::onSaveProject);
    connect(ui->actionSaveAs,  &QAction::triggered, this, &SWMMVis::onSaveAs);
    connect(ui->actionExportMap, &QAction::triggered, this, &SWMMVis::onExportMap);
    connect(ui->actionAbout,  &QAction::triggered, this, &SWMMVis::onAbout);

    connect(ui->menuOpenRecent, &QMenu::triggered, this, &SWMMVis::onOpenRecentFile);

    connect(ui->actionShowWelcome, &QAction::triggered, this, &SWMMVis::onShowWelcomeScreen);

    connect(ui->actionAddWMSData,    &QAction::triggered, this, &SWMMVis::onAddWMSLayer);
    connect(ui->actionAddBasemap,    &QAction::triggered, this, &SWMMVis::onAddBasemapLayer);
    connect(ui->actionAddVectorData, &QAction::triggered, this, &SWMMVis::onAddVectorLayer);
    connect(ui->actionAddRasterData, &QAction::triggered, this, &SWMMVis::onAddRasterLayer);
    connect(ui->actionAddSWMMOutput, &QAction::triggered, this, &SWMMVis::onAddSWMMResultsLayer);
    connect(ui->actionExecute,       &QAction::triggered, this, &SWMMVis::onRunSimulation);

    // Helper: resolve the runner the user wants to act on.  If the
    // Simulation Status dock has a selected row, use that job's runner;
    // otherwise fall back to the single active runner when there's
    // exactly one in flight.  Returns nullptr (and logs / messageboxes
    // an explanation) when no unambiguous target can be chosen.
    auto resolveTargetRunner = [this](const QString &actionLabel) -> SimulationRunner * {
        if (mActiveRunners.isEmpty()) {
            onLogMessage(tr("%1: no simulation is running.").arg(actionLabel),
                         OpenSWMMVisLogMessage::LogMessageType::Information);
            return nullptr;
        }
        // Try the selection in the dock first.
        if (auto *view = ui->treeViewSimulationStatus) {
            const auto rows = view->selectionModel()
                                  ? view->selectionModel()->selectedRows()
                                  : QModelIndexList{};
            for (const QModelIndex &idx : rows) {
                const int row = idx.parent().isValid() ? idx.parent().row()
                                                        : idx.row();
                const int jobId = mSimStatusModel->jobIdForRow(row);
                if (auto *r = mActiveRunners.value(jobId, nullptr))
                    return r;
            }
        }
        // No (active) selection — only proceed when exactly one runner
        // is in flight so the user can't accidentally hit the wrong one.
        if (mActiveRunners.size() == 1)
            return *mActiveRunners.begin();
        QMessageBox::information(this, tr("%1 simulation").arg(actionLabel),
            tr("Multiple simulations are running.\n"
               "Select the one you want to %1 in the Simulation Status "
               "panel first.").arg(actionLabel.toLower()));
        return nullptr;
    };

    // Pause: toggle the paused flag on the *selected* runner. The action
    // is a checkable toggle in the toolbar — its state follows the
    // resolved runner's paused flag.
    ui->actionPauseExecution->setCheckable(true);
    connect(ui->actionPauseExecution, &QAction::toggled, this,
            [this, resolveTargetRunner](bool paused) {
        SimulationRunner *runner = resolveTargetRunner(tr("Pause"));
        if (!runner) {
            // Bounce the checked state back so the toolbar reflects reality.
            QSignalBlocker b(ui->actionPauseExecution);
            ui->actionPauseExecution->setChecked(false);
            return;
        }
        runner->setPaused(paused);
        onLogMessage(paused
            ? tr("Simulation paused — toggle again to resume (%1).")
                  .arg(QFileInfo(runner->inpPath()).fileName())
            : tr("Simulation resumed (%1).")
                  .arg(QFileInfo(runner->inpPath()).fileName()));
    });

    // Stop: pause first, confirm with the user, then either cancel
    // (which still flushes .out / .rpt so partial results are saved
    // and auto-loaded by the finished handler) or resume.
    connect(ui->actionCancelExecution, &QAction::triggered, this,
            [this, resolveTargetRunner]() {
        SimulationRunner *runner = resolveTargetRunner(tr("Stop"));
        if (!runner) return;
        const bool wasPaused = runner->isPaused();
        if (!wasPaused) runner->setPaused(true);

        const QString name = QFileInfo(runner->inpPath()).fileName();
        const auto reply = QMessageBox::question(
            this, tr("Stop simulation"),
            tr("Stop \"%1\" now?\n\n"
               "The simulation is paused.  Choosing Yes flushes whatever "
               "the engine has produced up to this point to the .out / "
               ".rpt files and loads the partial results.  Choosing No "
               "resumes the run.").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            runner->setPaused(false);     // un-pause so the step loop can exit
            runner->cancel();
            onLogMessage(tr("Stop requested for %1 — partial results will "
                            "be saved to the .out file.").arg(name));
        } else if (!wasPaused) {
            // User aborted the stop — resume since we paused implicitly.
            runner->setPaused(false);
        }
    });
    connect(ui->actionOptions,       &QAction::triggered, this, &SWMMVis::onSimulationOptions);

    // Animation toolbar — Show Legend toggles the on-canvas legend
    // overlay for the active project window's canvas. The overlay is
    // a draggable child widget of the canvas; one overlay per canvas
    // (lazily created here and reused on every show / hide cycle).
    ui->actionShowLegend->setCheckable(true);
    connect(ui->actionShowLegend, &QAction::triggered, this, [this](bool checked) {
        auto *c = activeCanvas();
        if (!c) {
            // No active canvas — silently revert the toggle so the
            // toolbar state never lies about what's actually shown.
            QSignalBlocker b(ui->actionShowLegend);
            ui->actionShowLegend->setChecked(false);
            return;
        }
        auto *overlay = c->findChild<openswmmvis::ui::LegendOverlay *>(
            QString(), Qt::FindDirectChildrenOnly);
        if (!overlay)
            overlay = new openswmmvis::ui::LegendOverlay(c);
        overlay->setVisible(checked);
        if (checked) overlay->raise();
    });

    // Animation toolbar — Set Style opens the SymbologyDialog for the
    // currently selected layer (LayerTreePanel selection). When nothing
    // is selected, fall back to the active canvas's top visible layer so
    // a single click on the toolbar still does something useful.
    connect(ui->actionSetStyle, &QAction::triggered, this, [this]() {
        OpenSWMMVisLayer *target = mLayerTreePanel ? mLayerTreePanel->selectedLayer()
                                                   : nullptr;
        if (!target) {
            if (auto *c = activeCanvas()) {
                for (int i = c->layerCount() - 1; i >= 0; --i) {
                    if (OpenSWMMVisLayer *l = c->layerAt(i); l && l->isVisible()) {
                        target = l; break;
                    }
                }
            }
        }
        if (!target) {
            QMessageBox::information(this, tr("Set Style"),
                tr("Select a layer in the Layers panel to edit its style."));
            return;
        }
        openswmmvis::ui::SymbologyDialog dlg(target, this);
        if (dlg.exec() == QDialog::Accepted) {
            if (auto *c = activeCanvas())
                c->invalidate(MapCanvas::Scene, QStringLiteral("symbology-apply"));
        }
    });

    // Tools → Set Project CRS… (added programmatically).
    if (ui->menuTools)
    {
        auto *actSetCRS = ui->menuTools->addAction(
            QIcon(QStringLiteral(":/swmmvis/Globe")),
            tr("Set Project CRS…"));
        actSetCRS->setToolTip(tr("Choose or change the project's coordinate reference system"));
        connect(actSetCRS, &QAction::triggered, this, &SWMMVis::onCRSButtonClicked);

        // Slice CF.3 — Tools → Pick 2D Cells (added programmatically).
        // Checkable so it participates in the existing toolbar tool-sync
        // (toolActionKeys → onActiveSubWindowChanged updates checked state
        // when the active map tool changes).
        auto *actPick2DCells = ui->menuTools->addAction(tr("Pick 2D Cells…"));
        actPick2DCells->setObjectName(QStringLiteral("actionPick2DCells"));
        actPick2DCells->setCheckable(true);
        actPick2DCells->setIcon(QIcon(QStringLiteral(":/swmmvis/Crosshair")));
        actPick2DCells->setToolTip(tr(
            "Box-select (default) or lasso-select cells on the 2D mesh "
            "results layer to plot their depth / HGL / velocity time "
            "series. Press B or L while active to swap modes; Esc cancels."));
        connect(actPick2DCells, &QAction::toggled, this,
                [this, actPick2DCells](bool checked) {
            auto *pw = activeProjectWindow();
            if (!pw) {
                actPick2DCells->setChecked(false);
                return;
            }
            if (checked) {
                pw->activatePick2DCellsTool();
            } else {
                // Revert to the default Select tool when the user un-checks
                // — symmetric with the Add Node / Add Link toggle behaviour.
                pw->activateSelectTool();
            }
        });

        // Pin it to the Analysis toolbar (its natural home — results-oriented).
        // Fall back to the main toolbar if Analysis isn't present.
        if (ui->toolBarAnalysis) {
            ui->toolBarAnalysis->addSeparator();
            ui->toolBarAnalysis->addAction(actPick2DCells);
        } else if (ui->toolBarMain) {
            ui->toolBarMain->addAction(actPick2DCells);
        }
    }

    // ── Slice BM.0 / DA.3 — Data menu + Add-New shortcut wiring ─────────────
    //
    // Inserts a new top-level "Data" menu between "View" and "Tools" so the
    // user has a keyboard-accelerated path to "Add New <Type>…" without
    // hunting through the Object Browser context menu. Per Slice
    // BM.0-Add-New (2026-05-24), each menu item dispatches through
    // `ObjectBrowserPanel::launchAddNewEditor` — which launches the
    // category's complex MVC editor in create mode (Time Series / Unit
    // Hydrographs) or is disabled for gap categories with a tooltip
    // naming the future editor slice. The legacy `NewDataObjectDialog`
    // is removed.
    //
    // The toolbar strip (BM.0.4) lives at the right end of the existing
    // Edit toolbar with the five most-used types: Time Series, Curve,
    // Time Pattern, LID Control, Pollutant.
    {
        // Helper closure capturing `this`; reused by both the menu and the
        // toolbar so the dispatch path stays consistent.
        auto launchAddNew = [this](SWMMModelLayer::DataCategory dc) {
            if (mObjectBrowserPanel) mObjectBrowserPanel->launchAddNewEditor(dc);
        };

        auto *menuData = new QMenu(tr("&Data"), this);
        menuData->setObjectName(QStringLiteral("menuData"));

        struct DataEntry {
            SWMMModelLayer::DataCategory dc;
            const char                  *menuLabel;
            bool                         separatorAfter;
        };
        // DA.3 / BM.0.3 ordering: tables first, then quality, then
        // subsurface, then dynamic-behaviour (rules + RDII + climate),
        // and finally streets/inlets. nullptr label means "separator
        // here, no menu item".
        static const DataEntry kEntries[] = {
            {SWMMModelLayer::DataTimeSeries,  QT_TR_NOOP("New Time &Series…"),  false},
            {SWMMModelLayer::DataCurves,      QT_TR_NOOP("New &Curve…"),        false},
            {SWMMModelLayer::DataPatterns,    QT_TR_NOOP("New Time &Pattern…"), true},
            {SWMMModelLayer::DataLIDControls, QT_TR_NOOP("New &LID Control…"),  false},
            {SWMMModelLayer::DataPollutants,  QT_TR_NOOP("New Po&llutant…"),    false},
            {SWMMModelLayer::DataLandUses,    QT_TR_NOOP("New L&and Use…"),     true},
            {SWMMModelLayer::DataAquifers,    QT_TR_NOOP("New A&quifer…"),      false},
            {SWMMModelLayer::DataSnowpacks,   QT_TR_NOOP("New S&nowpack…"),     true},
            // DA.3 — Control Rules + Unit Hydrographs land in the menu.
            {SWMMModelLayer::DataControls,    QT_TR_NOOP("New Control &Rule…"), false},
            {SWMMModelLayer::DataTransects,   QT_TR_NOOP("New &Transect…"),     false},
            {SWMMModelLayer::DataHydrographs, QT_TR_NOOP("New Unit &Hydrograph…"), true},
            {SWMMModelLayer::DataStreets,     QT_TR_NOOP("New St&reet…"),       false},
            {SWMMModelLayer::DataInlets,      QT_TR_NOOP("New &Inlet…"),        false},
        };
        for (const auto &e : kEntries) {
            auto *act = menuData->addAction(tr(e.menuLabel));
            // Slice BM.0-Add-New — disable gap categories with a tooltip
            // naming the future editor slice (mirrors the Object Browser
            // context-menu behaviour).
            if (!ObjectBrowserPanel::hasComplexEditor(e.dc)) {
                act->setEnabled(false);
                act->setToolTip(ObjectBrowserPanel::gapTooltipFor(e.dc));
            }
            connect(act, &QAction::triggered, this,
                    [launchAddNew, dc = e.dc] {
                launchAddNew(dc);
            });
            if (e.separatorAfter) menuData->addSeparator();
        }

        // Insert before "Tools" so the menu order reads File / Edit /
        // View / Data / Tools / Help.
        if (ui->menuTools) {
            menuBar()->insertMenu(ui->menuTools->menuAction(), menuData);
        } else {
            menuBar()->addMenu(menuData);
        }

        // ── BM.0.4 — Data toolbar strip on the Edit toolbar ─────────────
        QToolBar *editBar = ui->toolBarEdit ? ui->toolBarEdit : ui->toolBarMain;
        if (editBar) {
            editBar->addSeparator();
            struct ToolEntry {
                SWMMModelLayer::DataCategory dc;
                const char                  *tooltip;
            };
            static const ToolEntry kToolEntries[] = {
                {SWMMModelLayer::DataTimeSeries,  QT_TR_NOOP("New Time Series…")},
                {SWMMModelLayer::DataCurves,      QT_TR_NOOP("New Curve…")},
                {SWMMModelLayer::DataPatterns,    QT_TR_NOOP("New Time Pattern…")},
                {SWMMModelLayer::DataLIDControls, QT_TR_NOOP("New LID Control…")},
                {SWMMModelLayer::DataPollutants,  QT_TR_NOOP("New Pollutant…")},
            };
            for (const auto &e : kToolEntries) {
                auto *act = editBar->addAction(
                    QIcon(QStringLiteral(":/swmmvis/Layers")),
                    tr(e.tooltip));
                // Slice BM.0-Add-New — gap categories greyed out with a
                // tooltip naming the future editor slice. Today only Time
                // Series is live among the 5 toolbar entries; the other 4
                // (Curve / Pattern / LID / Pollutant) wait on BO/BP/BQ.
                if (!ObjectBrowserPanel::hasComplexEditor(e.dc)) {
                    act->setEnabled(false);
                    act->setToolTip(ObjectBrowserPanel::gapTooltipFor(e.dc));
                } else {
                    act->setToolTip(tr(e.tooltip));
                }
                connect(act, &QAction::triggered, this,
                        [launchAddNew, dc = e.dc] {
                    launchAddNew(dc);
                });
            }
        }
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



    // Node add tools.
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

    // Link add tools (Slice AE).
    if (ui->actionAddPipe)
        connect(ui->actionAddPipe, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddConduitTool();
        });
    if (ui->actionAddPump)
        connect(ui->actionAddPump, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddPumpTool();
        });
    if (ui->actionAddOrifice)
        connect(ui->actionAddOrifice, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddOrificeTool();
        });
    if (ui->actionAddWeir)
        connect(ui->actionAddWeir, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddWeirTool();
        });
    if (ui->actionAddOutlet)
        connect(ui->actionAddOutlet, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddOutletTool();
        });

    // Rain gage + subcatchment tools (Slice AE/AF).
    if (ui->actionRainGauge)
        connect(ui->actionRainGauge, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddGageTool();
        });
    if (ui->actionAddSubcatchment)
        connect(ui->actionAddSubcatchment, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddSubcatchmentTool();
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
            {
                onLogMessage(tr("2D mesh generated and written."));

                // Creating a mesh is what activates the 2D module. The
                // SimulationOptionsDialog persists the same flag under this
                // key; writing it here means the next open of that dialog
                // will see the module already checked.
                if (auto *ml = pw->modelLayer())
                {
                    QSettings s;
                    const QString key =
                        QStringLiteral("SWMMVis/Project/%1/Module2DEnabled")
                            .arg(ml->modelFilePath());
                    s.setValue(key, true);
                }
            }
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
    // Close any open profile-plot dialogs.  They're top-level windows
    // parented to `nullptr` (so they get their own dock icon on macOS)
    // and therefore don't die automatically with the main window.
    // `WA_DeleteOnClose` handles the destruction.
    for (QWidget *top : QApplication::topLevelWidgets()) {
        if (auto *dlg = qobject_cast<ProfilePlotDialog *>(top))
            dlg->close();
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

// Format an integer second count as HH:MM:SS — required by SWMM's
// [OPTIONS] step keys (REPORT_STEP, WET_STEP, …). 0 → "00:00:00".
QString secondsToHms(int seconds)
{
    seconds = std::max(0, seconds);
    const int h = seconds / 3600;
    const int m = (seconds % 3600) / 60;
    const int s = seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

// Emit a routing-step value with sub-second precision when needed
// (engine accepts "0:00:05" as well as "5" — keep the HH:MM:SS form
// for stylistic consistency with the other step keys; fall back to a
// raw decimal-seconds value when the user wants < 1 s granularity).
QString routingStepToken(double seconds)
{
    if (seconds < 1.0 || std::fabs(seconds - std::round(seconds)) > 1e-6)
        return QString::number(seconds, 'g', 6);
    return secondsToHms(static_cast<int>(std::round(seconds)));
}

// Build a minimum-viable SWMM .inp from the New-Project dialog inputs.
// SWMM tolerates many missing sections; we keep [TITLE], [OPTIONS], and
// [REPORT] explicit so the engine has stable defaults to read back.
//
// Engine-aware: emits modern-engine keys (NODE_CONTINUITY, ANDERSON_ACCEL)
// only when `forNewEngine` is true. Legacy SWMM 5.x silently accepts most
// other modern keys (SURCHARGE_METHOD, INERTIAL_DAMPING, LAT_FLOW_TOL, …)
// because they have legacy precedents; only the two new-engine-only knobs
// are gated.
QString synthesizeBlankInp(const NewProjectDialog::Result &r,
                            const PreferencesManager::SimulationDefaults &d,
                            bool forNewEngine)
{
    const QString startDate = r.startDateTime.toString(QStringLiteral("MM/dd/yyyy"));
    const QString startTime = r.startDateTime.toString(QStringLiteral("HH:mm:ss"));
    const QString endDate   = r.endDateTime.toString(QStringLiteral("MM/dd/yyyy"));
    const QString endTime   = r.endDateTime.toString(QStringLiteral("HH:mm:ss"));

    const QString yesNo[2] = {QStringLiteral("NO"), QStringLiteral("YES")};
    const auto yn = [&](bool v) { return yesNo[v ? 1 : 0]; };

    // VARIABLE_STEP is a Courant fraction in [0, 1]; the engine treats 0
    // as "disabled" and the GUI exposes that as a separate toggle. When
    // the toggle is off, force the factor to 0 regardless of the spin.
    const double variableStep = d.variableStepOn ? d.variableStepFactor : 0.0;

    // Tolerances surfaced as percent in the GUI, stored as fractions in
    // the engine internals; the .inp ingests percent.
    QString out;
    out.reserve(2048);
    out += QStringLiteral("[TITLE]\n%1\n\n").arg(r.name);
    out += QStringLiteral("[OPTIONS]\n");

    auto kv = [&](const char *key, const QString &value) {
        out += QStringLiteral("%1 %2\n")
            .arg(QString::fromLatin1(key), -20).arg(value);
    };

    kv("FLOW_UNITS",          d.flowUnits);
    kv("INFILTRATION",        d.infiltrationModel);
    kv("FLOW_ROUTING",        d.flowRouting);
    kv("LINK_OFFSETS",        QStringLiteral("DEPTH"));
    kv("MIN_SLOPE",           QString::number(d.minSlopePct, 'g', 6));
    kv("ALLOW_PONDING",       yn(d.allowPonding));
    kv("SKIP_STEADY_STATE",   yn(d.skipSteadyState));

    kv("START_DATE",          startDate);
    kv("START_TIME",          startTime);
    kv("END_DATE",            endDate);
    kv("END_TIME",            endTime);
    kv("REPORT_START_DATE",   startDate);
    kv("REPORT_START_TIME",   startTime);
    kv("SWEEP_START",         d.sweepStart);
    kv("SWEEP_END",           d.sweepEnd);
    kv("DRY_DAYS",            QString::number(d.dryDays, 'g', 6));
    kv("REPORT_STEP",         secondsToHms(d.reportStepSec));
    kv("WET_STEP",            secondsToHms(d.wetStepSec));
    kv("DRY_STEP",            secondsToHms(d.dryStepSec));
    kv("ROUTING_STEP",        routingStepToken(d.routingStepSec));
    kv("RULE_STEP",           secondsToHms(d.ruleStepSec));

    // Process-module toggles (refactored engine supports IGNORE_* across
    // the board; legacy honours all of these too — gated only on the
    // engine-aware emit further down).
    kv("IGNORE_RAINFALL",     yn(d.ignoreRainfall));
    kv("IGNORE_SNOWMELT",     yn(d.ignoreSnowmelt));
    kv("IGNORE_GROUNDWATER",  yn(d.ignoreGroundwater));
    kv("IGNORE_RDII",         yn(d.ignoreRdii));
    kv("IGNORE_QUALITY",      yn(d.ignoreQuality));
    kv("IGNORE_ROUTING",      yn(d.ignoreRouting));

    // Hydraulics — DW knobs go in regardless of routing method; the
    // engine ignores them under STEADY/KINWAVE.
    kv("INERTIAL_DAMPING",    d.inertialDamping);
    kv("NORMAL_FLOW_LIMITED", d.normalFlowLimited);
    kv("FORCE_MAIN_EQUATION", d.forceMainEquation);
    kv("SURCHARGE_METHOD",    d.surchargeMethod);
    kv("VARIABLE_STEP",       QString::number(variableStep, 'g', 6));
    kv("LENGTHENING_STEP",    QString::number(d.lengtheningStepSec, 'g', 6));
    kv("MINIMUM_STEP",        QString::number(d.minRoutingStepSec, 'g', 6));
    kv("MAX_TRIALS",          QString::number(d.maxTrials));
    kv("HEAD_TOLERANCE",      QString::number(d.headTolerance, 'g', 6));
    kv("SYS_FLOW_TOL",        QString::number(d.sysFlowTolPct, 'g', 6));
    kv("LAT_FLOW_TOL",        QString::number(d.latFlowTolPct, 'g', 6));
    kv("THREADS",             QString::number(d.threads));

    if (forNewEngine) {
        kv("NODE_CONTINUITY", d.nodeContinuity);
        kv("ANDERSON_ACCEL",  yn(d.andersonAccel));
    }

    out += QStringLiteral("\n[REPORT]\n"
"INPUT      NO\n"
"CONTROLS   NO\n"
"SUBCATCHMENTS ALL\n"
"NODES      ALL\n"
"LINKS      ALL\n");
    return out;
}

} // namespace

void SWMMVis::onNewProject()
{
    // File → New creates a blank untitled project immediately — no
    // dialog interruption — using defaults persisted through
    // PreferencesManager (edit them in Preferences → "Simulation
    // Defaults" / "Dynamic Wave Defaults"). Templates / explicit CRS
    // choice live in a future "New From Template…" entry; NewProjectDialog
    // stays available for that.
    auto *prefs  = PreferencesManager::instance();
    auto         sim = prefs->simulationDefaults();

    // THREADS persisted default is 0 (engine auto). On File→New we max
    // to the machine's logical-processor count so a fresh project starts
    // saturated; the user's persisted choice still overrides when non-zero.
    if (sim.threads <= 0) {
        const int hw = QThread::idealThreadCount();
        sim.threads = hw > 0 ? hw : 1;
    }

    NewProjectDialog::Result r;
    r.name              = QStringLiteral("Untitled");
    r.flowUnits         = sim.flowUnits;
    r.infiltrationModel = sim.infiltrationModel;
    r.flowRouting       = sim.flowRouting;
    // Start at today's midnight; end 24 h later. The legacy hard-coded
    // 2002-01-01 + 6 h window was a SWMM5 echo; modern projects almost
    // always re-set this anyway, and "current day at midnight" makes the
    // synthetic .inp self-describing in the timeline.
    const QDate today = QDate::currentDate();
    r.startDateTime    = QDateTime(today, QTime(0, 0));
    r.endDateTime      = r.startDateTime.addSecs(24 * 3600);
    // crsAuthCode intentionally left empty — SWMMModelLayer::loadModel
    // pulls the default CRS from PreferencesManager when the .inp carries
    // none, and the project window propagates that to the canvas. No
    // post-load override here keeps layer/canvas in lockstep.

    // Engine-aware emit: NODE_CONTINUITY + ANDERSON_ACCEL are gated on the
    // refactored engine being the active default.
    QString defaultEngine = prefs->defaultEngineMode();
    if (defaultEngine.isEmpty()) defaultEngine = QLatin1String(SWMM_VERSION);
    const bool forNewEngine = (defaultEngine == QLatin1String(SWMM_VERSION));

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
        f.write(synthesizeBlankInp(r, sim, forNewEngine).toUtf8());
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
        onSaveAs();
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

void SWMMVis::onSaveAs()
{
    auto *pw = activeProjectWindow();
    if (!pw) {
        onLogMessage(tr("Save As: no active project."),
                     OpenSWMMVisLogMessage::LogMessageType::Warning);
        return;
    }

    const QString suggested = pw->modelLayer()
                                ? pw->modelLayer()->modelFilePath()
                                : QString();
    auto *kFilters = openswmmvis::FileFilterRegistry::instance();

    // Use a QFileDialog instance (not the static helper) so the left-sidebar
    // panel remains interactive under macOS and MDI parent windows.
    QFileDialog dlg(this, tr("Save As"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    // Combined filter: ProjectWrite (.oswp) first, then all writable InputRead
    // formats (.inp, .gpkg, …), then All Files. The user picks the target
    // format from one dropdown — no separate submenu actions needed.
    dlg.setNameFilter(kFilters->saveAsFilter());
    dlg.setDirectory(suggested.isEmpty() ? QDir::homePath()
                                         : QFileInfo(suggested).absolutePath());
    if (!suggested.isEmpty())
        dlg.selectFile(QFileInfo(suggested).fileName());

    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString path = dlg.selectedFiles().value(0);
    if (path.isEmpty())
        return;

    const QString ext = QFileInfo(path).suffix().toLower();

    // When saving as .oswp the user is expressing intent to save the full
    // project.  Derive the sibling .inp path (same dir, same base name) and
    // write the SWMM input there, then write the sidecar to the chosen path.
    const bool isProject = (ext == QStringLiteral("oswp"));
    const QString inpPath = isProject
        ? QFileInfo(path).absolutePath() + QChar('/')
              + QFileInfo(path).completeBaseName() + QStringLiteral(".inp")
        : path;

    QString err;
    if (!pw->saveAs(inpPath, &err)) {
        QMessageBox::critical(this, tr("Save As failed"), err);
        return;
    }

    // Write the .oswp sidecar for native input (.inp) or explicit project
    // (.oswp) saves. Plugin-backed export formats (.gpkg, …) are standalone
    // — they carry no project sidecar.
    const bool writeSidecar = isProject || (ext == QStringLiteral("inp"));
    if (writeSidecar) {
        const QString oswp = isProject ? path
                                       : ProjectSerializer::sidecarPathFor(path);
        QString sidecarErr;
        if (!oswp.isEmpty()
            && !ProjectSerializer::saveToFile(oswp, pw, &sidecarErr)) {
            onLogMessage(tr("Sidecar save failed: %1").arg(sidecarErr),
                         OpenSWMMVisLogMessage::LogMessageType::Warning);
        }
    }

    mRecentFiles.removeAll(inpPath);
    mRecentFiles.prepend(inpPath);
    onRecentFilesSizeChanged();
    saveSettings();
    onLogMessage(tr("Saved As: %1").arg(path));
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
    // Prevent duplicate windows — if this .inp is already open, just focus it.
    const QFileInfo inpFi(filePath);
    for (QMdiSubWindow *sw : ui->mdiAreaCentral->subWindowList()) {
        auto *existing = qobject_cast<SWMMVisProjectWindow *>(sw);
        if (existing && existing->modelLayer()) {
            const QFileInfo existFi(existing->modelLayer()->modelFilePath());
            if (existFi == inpFi) {
                ui->mdiAreaCentral->setActiveSubWindow(sw);
                return;
            }
        }
    }

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
        // hidden objects, canvas extent, result layers) that can't
        // round-trip through the .inp itself. Missing sidecar is not an error.
        const QString oswp = ProjectSerializer::sidecarPathFor(filePath);
        if (!oswp.isEmpty() && QFile::exists(oswp)) {
            QString sidecarErr;
            if (!ProjectSerializer::applyFromFile(oswp, window, &sidecarErr)) {
                onLogMessage(tr("Sidecar load failed: %1").arg(sidecarErr),
                             OpenSWMMVisLogMessage::LogMessageType::Warning);
            }
        }

        // Auto-discover sibling output files if the sidecar didn't restore any.
        // Check ResultsRead patterns (*.out) and any plugin-registered extensions.
        bool hasResultLayer = false;
        if (window->canvas()) {
            for (OpenSWMMVisLayer *l : window->canvas()->layers())
                if (qobject_cast<SWMMResultsLayer *>(l)) { hasResultLayer = true; break; }
        }
        if (!hasResultLayer && window->canvas() && window->modelLayer()) {
            auto *reg = openswmmvis::FileFilterRegistry::instance();
            // Collect all ResultsRead and ResultsWrite patterns (e.g. "*.out").
            QStringList patterns = reg->patternsFor(openswmmvis::FilterKind::ResultsRead);
            patterns += reg->patternsFor(openswmmvis::FilterKind::ResultsWrite);

            const QFileInfo fi(filePath);
            const QString base = fi.completeBaseName();
            const QDir dir = fi.absoluteDir();

            for (const QString &pat : patterns) {
                // Pattern is "*.ext" — build "basename.ext" and check if it exists.
                const QString ext = QString(pat).remove(QStringLiteral("*."));
                const QString candidate = dir.filePath(base + QStringLiteral(".") + ext);
                if (!QFileInfo::exists(candidate)) continue;

                auto *rl = new SWMMResultsLayer(candidate, window->modelLayer());
                rl->setName(QFileInfo(candidate).fileName());
                window->canvas()->addLayer(rl, /*pushUndo=*/false);
                QList<QString> rlW, rlE;
                if (rl->openResults(rlW, rlE)) {
                    rl->autoStretchColorRamp();
                    if (mAnimationController)
                        mAnimationController->setPrimaryLayer(rl);
                    onLogMessage(tr("Auto-loaded results: %1").arg(QFileInfo(candidate).fileName()));
                }
                for (const QString &e : rlE)
                    onLogMessage(e, OpenSWMMVisLogMessage::LogMessageType::Warning);
                break; // load first match only
            }
        }

        // ── 2D mesh + previous-run HDF5 results auto-load ─────────────────────
        // Pull the static mesh geometry out of the .inp (or the linked .2dm)
        // so the user sees the surface routing domain immediately after open,
        // without having to start a simulation. Then check [2D_OPTIONS]
        // OUTPUT_FILE — if the engine has already written an HDF5 from a prior
        // run, attach a scrub-ready SWMM2DResultsLayer over the mesh.
        if (window->canvas() && window->modelLayer())
        {
            const mesh::InpMeshReadResult meshRead = mesh::InpMeshReader::read(filePath);
            if (!meshRead.errorMsg.isEmpty()) {
                onLogMessage(meshRead.errorMsg,
                             OpenSWMMVisLogMessage::LogMessageType::Warning);
            }
            if (meshRead.hasMesh) {
                auto *meshLayer = new SWMM2DMeshLayer(meshRead.mesh, meshRead.sourcePath);
                meshLayer->setActiveMesh(meshRead.isExternal);
                meshLayer->setName(meshRead.sourcePath.isEmpty()
                                       ? QStringLiteral("Mesh (inline)")
                                       : QFileInfo(meshRead.sourcePath).fileName());
                // Mesh coordinates are in the model CRS; the layer reprojects
                // to canvas CRS when populating the scene.
                if (window->modelLayer()->srs())
                    meshLayer->setSRS(
                        new SpatialReferenceSystem(*window->modelLayer()->srs(), meshLayer),
                        /*ownsSRS=*/true);
                window->canvas()->addLayer(meshLayer, /*pushUndo=*/false);
                onLogMessage(tr("Loaded 2D mesh: %1 vertices, %2 triangles (%3)")
                                 .arg(meshRead.mesh.vertices.size())
                                 .arg(meshRead.mesh.triangles.size())
                                 .arg(meshRead.isExternal
                                          ? QFileInfo(meshRead.sourcePath).fileName()
                                          : tr("inline")));

                // Surface previous-run HDF5 depth results so the user can
                // scrub without re-running. Skip when there's no [2D_OPTIONS]
                // OUTPUT_FILE entry or when the referenced file is missing.
                const QString h5Path = SimulationRunner::parseTwoDOutputFile(filePath);
                if (h5Path.isEmpty()) {
                    onLogMessage(tr("2D results auto-load skipped: no [2D_OPTIONS] "
                                     "OUTPUT_FILE in %1").arg(QFileInfo(filePath).fileName()));
                } else if (!QFileInfo::exists(h5Path)) {
                    onLogMessage(tr("2D results auto-load skipped: file does not "
                                     "exist (resolved: %1)").arg(h5Path));
                } else {
                    auto h5Src = std::make_unique<HDF5Mesh2DSource>();
                    if (h5Src->open(h5Path))
                    {
                        // Anchor the HDF5 source's time axis to the engine's
                        // simulation start so the global animation slider's
                        // QDateTime ticks map to 2D frame indices. Without
                        // this, source->simTimeAt() returns invalid for every
                        // frame and SWMM2DResultsLayer::setCurrentSimTime
                        // silently no-ops on every slider scrub.
                        if (window->modelLayer() && window->modelLayer()->engine()) {
                            double startOA = 0.0;
                            if (swmm_get_start_time(window->modelLayer()->engine(),
                                                    &startOA) == SWMM_OK &&
                                startOA > 0.0)
                            {
                                static const QDateTime kSwmmEpoch(
                                    QDate(1899, 12, 30),
                                    QTime(0, 0), QTimeZone::LocalTime);
                                const QDateTime simStart =
                                    kSwmmEpoch.addMSecs(static_cast<qint64>(
                                        startOA * 86400.0 * 1000.0));
                                if (simStart.isValid()) {
                                    h5Src->setSimulationStart(simStart);
                                    onLogMessage(tr("2D layer time anchor: %1")
                                                     .arg(simStart.toString(
                                                         Qt::ISODate)));
                                }
                            }
                        }

                        // Cache the bare source pointer for the post-set
                        // peak-frame scan (setSource swallows ownership).
                        IMesh2DSource* srcRaw = h5Src.get();
                        auto *resLayer = new SWMM2DResultsLayer(
                            QStringLiteral("2D Results"), nullptr);
                        resLayer->setSource(std::move(h5Src));
                        window->canvas()->addLayer(resLayer, /*pushUndo=*/false);

                        // Scan all frames to find (a) the peak-inundation
                        // frame to seek to and (b) the actual depth range so
                        // we can auto-tune the ramp + dry-cell threshold.
                        // setSource() defaults to the LAST frame, which for
                        // many runs is fully drained.
                        const int nFrames = srcRaw->timeCount();
                        int   peakFrame = 0;
                        float peakDepth = 0.0f;
                        std::vector<float> probe;
                        for (int t = 0; t < nFrames; ++t) {
                            if (!srcRaw->readDepthsAt(t, probe)) continue;
                            if (probe.empty()) continue;
                            const float m = *std::max_element(probe.begin(),
                                                                probe.end());
                            if (m > peakDepth) { peakDepth = m; peakFrame = t; }
                        }

                        // Honour the engine's DRY_DEPTH (via the live engine
                        // handle) so the GUI render threshold matches what
                        // the solver considers wet — without this, the
                        // GUI default (5 mm) clips shallow runs invisibly.
                        // Falls back to a small data-derived floor when the
                        // engine doesn't yet have 2D options live.
                        double engineDry = 0.0;
                        if (window->modelLayer() && window->modelLayer()->engine() &&
                            swmm_2d_get_dry_depth(window->modelLayer()->engine(),
                                                   &engineDry) == SWMM_OK &&
                            engineDry > 0.0)
                        {
                            resLayer->setDryDepth(engineDry);
                        } else if (peakDepth > 0.0f) {
                            resLayer->setDryDepth(std::max(1e-5, 0.05 * peakDepth));
                        }

                        // Tune the inundation colour ramp's upper bound to the
                        // actual data peak; default (0.5 m) is way too loose
                        // for typical overland-flow demos.
                        if (peakDepth > 0.0f) {
                            resLayer->setMaxDepth(peakDepth);
                            resLayer->setCurrentTimeIndex(peakFrame);
                        }

                        // CF.MVP-fix.1 — register the 2D layer as the
                        // animation controller's fallback driver so the
                        // play/pause/slider toolbar ticks 2D frames even
                        // when no 1D .out results are loaded.
                        if (mAnimationController && !mAnimationController->primaryLayer()) {
                            mAnimationController->setFallback2DLayer(resLayer);
                        }

                        onLogMessage(tr("Loaded 2D results: %1 (%2 frames, "
                                         "peak depth %3 mm at frame %4, "
                                         "peak velocity %5 mm/s)")
                                         .arg(QFileInfo(h5Path).fileName())
                                         .arg(nFrames)
                                         .arg(double(peakDepth) * 1000.0, 0, 'f', 2)
                                         .arg(peakFrame)
                                         .arg(resLayer->maxVelocity() * 1000.0,
                                              0, 'f', 3));
                    } else {
                        onLogMessage(tr("Found 2D output file but failed to open: %1")
                                         .arg(h5Path),
                                     OpenSWMMVisLogMessage::LogMessageType::Warning);
                    }
                }
            }
        }

        setWindowTitle(QStringLiteral("OpenSWMM — %1").arg(QFileInfo(filePath).baseName()));
        window->show();
        ui->mdiAreaCentral->setActiveSubWindow(window);

        // Re-run the activation handler now that the engine is loaded.
        // addSubWindow() / setActiveSubWindow() can fire subWindowActivated
        // before loadModel() finishes, which sets mActiveProjectWindow and
        // makes the explicit re-call below trip the same-project guard in
        // onActiveSubWindowChanged, leaving status-bar widgets showing
        // pre-load defaults. Null the cache so the guard does not fire and
        // the hydration block (Flow Units / Offset Mode / Map Scale / CRS /
        // Engine Version) actually runs against the now-loaded engine.
        // Required by §M.1 trigger 1 (project open) — see Slice CX.
        mActiveProjectWindow = nullptr;
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
        if (mComboBoxMapScale) {
            QSignalBlocker block(mComboBoxMapScale);
            mComboBoxMapScale->setEditText(QString());
            mComboBoxMapScale->setEnabled(false);
        }
        mComboBoxFlowUnits->setEnabled(false);
        if (mComboBoxEngineVersion) mComboBoxEngineVersion->setEnabled(false);
        if (mLayerTreePanel)        mLayerTreePanel->setCanvas(nullptr);
        if (mObjectBrowserPanel)    mObjectBrowserPanel->setProject(nullptr, nullptr, nullptr);
        if (mAttributePanel)      { mAttributePanel->setProject(nullptr); mAttributePanel->clear(); }
        if (mAttributeTablePanel)   mAttributeTablePanel->setProject(nullptr, nullptr, nullptr);
        if (mTerrainToolbar)        mTerrainToolbar->rebindCanvas(nullptr);
        ui->actionEditExisting->setChecked(false);
        ui->actionEditExisting->setEnabled(false);
        applyEditSessionToActions(false);
        applyProjectOpenToActions(false);
        mActiveProjectWindow = nullptr;
        return;
    }

    // Same project as before → no rebind needed (avoids redundant model
    // resets that flash the Layers dock empty for a frame).
    if (pw == mActiveProjectWindow)
        return;
    mActiveProjectWindow = pw;

    // Animation toolbar follows the active project tab — point the
    // shared controller at this tab's first SWMMResultsLayer so the
    // slider range, datetime edit, and play/pause/skip actions all
    // operate on the data the user is looking at. Tabs without a
    // results layer reset the controller to nullptr, which collapses
    // the slider range to 0 and disables the transport.
    {
        SWMMResultsLayer *primary = nullptr;
        if (pw->canvas()) {
            for (OpenSWMMVisLayer *l : pw->canvas()->layers()) {
                if (auto *rl = qobject_cast<SWMMResultsLayer *>(l)) {
                    primary = rl;
                    break;
                }
            }
        }
        if (mAnimationController) mAnimationController->setPrimaryLayer(primary);
    }

    // Rebind canvas signals to the newly active canvas. UniqueConnection avoids
    // duplicating on repeated activation of the same window.
    connect(pw->canvas(), &MapCanvas::cursorPositionChanged,
            this, &SWMMVis::onCursorPositionChanged, Qt::UniqueConnection);
    connect(pw->canvas(), &MapCanvas::canvasSRSChanged,
            this, &SWMMVis::onCanvasSRSChanged, Qt::UniqueConnection);
    connect(pw->canvas(), &MapCanvas::scaleChanged,
            this, &SWMMVis::onCanvasScaleChanged, Qt::UniqueConnection);

    // Prime the map-scale combo with the canvas's current scale so the
    // statusbar isn't blank until the user's first zoom/pan.
    if (mComboBoxMapScale) {
        mComboBoxMapScale->setEnabled(true);
        onCanvasScaleChanged(pw->canvas()->scaleDenominator());
    }

    // Sync toolbar checked state when the active tool changes.
    // Build mapping once per window connection; lambda captures pw by value so
    // it remains valid after a window switch (pw stays alive in the MDI area).
    QObject::disconnect(pw->canvas(), &MapCanvas::activeToolChanged, this, nullptr);
    connect(pw->canvas(), &MapCanvas::activeToolChanged, this,
            [this, pw](OpenSWMMVisMapTool *tool) {
                if (pw != mActiveProjectWindow) return;
                const QHash<OpenSWMMVisMapTool *, QString> keys = pw->toolActionKeys();
                // Uncheck all tool actions, then check the active one.
                for (const QString &name : keys.values()) {
                    if (auto *act = findChild<QAction *>(name))
                        act->setChecked(false);
                }
                if (tool) {
                    const QString name = keys.value(tool);
                    if (!name.isEmpty())
                        if (auto *act = findChild<QAction *>(name))
                            act->setChecked(true);
                }
            });

    // Sync immediately to the current active tool of the newly focused window.
    {
        const QHash<OpenSWMMVisMapTool *, QString> keys = pw->toolActionKeys();
        for (const QString &name : keys.values())
            if (auto *act = findChild<QAction *>(name))
                act->setChecked(false);
        if (OpenSWMMVisMapTool *cur = pw->canvas()->activeTool()) {
            const QString name = keys.value(cur);
            if (!name.isEmpty())
                if (auto *act = findChild<QAction *>(name))
                    act->setChecked(true);
        }
    }

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

    // Sync edit session toggle to the incoming project window's state.
    // Disconnect any prior editSessionChanged connection from this window
    // so repeated tab-switches don't stack handlers.
    QObject::disconnect(pw, &SWMMVisProjectWindow::editSessionChanged, this, nullptr);
    connect(pw, &SWMMVisProjectWindow::editSessionChanged, this,
            [this, pw](bool active) {
                if (pw != mActiveProjectWindow) return;
                QSignalBlocker b(ui->actionEditExisting);
                ui->actionEditExisting->setChecked(active);
                applyEditSessionToActions(active);
            }, Qt::UniqueConnection);
    {
        const bool active = pw->isEditSessionActive();
        QSignalBlocker b(ui->actionEditExisting);
        ui->actionEditExisting->setChecked(active);
        ui->actionEditExisting->setEnabled(true);
        applyEditSessionToActions(active);
        applyProjectOpenToActions(true);
    }

    // Sync engine version combo to the active project.
    if (mComboBoxEngineVersion)
    {
        QSignalBlocker b(mComboBoxEngineVersion);
        const int idx = mComboBoxEngineVersion->findData(pw->engineVersion());
        mComboBoxEngineVersion->setCurrentIndex(idx >= 0 ? idx : 0);
        mComboBoxEngineVersion->setEnabled(true);
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
        // Slice AT.2 — attribute submenu picks (Node/Link/Subcatch) and
        // background-hit system-variable picks.
        connect(st, &OpenSWMMVisMapToolSelect::plotAttributeRequested,
                this, &SWMMVis::openComparisonPlotForAttribute,
                Qt::UniqueConnection);
        connect(st, &OpenSWMMVisMapToolSelect::plotSystemRequested,
                this, &SWMMVis::openComparisonPlotForSystemAttribute,
                Qt::UniqueConnection);
    }

    // Slice BC — when MapToolSelectProfile finalizes a path, open the
    // profile plot dialog bound to the active model + animation controller.
    if (auto *pt = pw->selectProfileTool()) {
        connect(pt, &OpenSWMMVisMapToolSelectProfile::profilePathSelected,
                this, &SWMMVis::openProfilePlotFor,
                Qt::UniqueConnection);
        connect(pt, &OpenSWMMVisMapToolSelectProfile::statusMessageChanged,
                statusBar(), [this](const QString &msg) {
                    statusBar()->showMessage(msg, 5000);
                }, Qt::UniqueConnection);
        // Async routing busy → status-bar progress spinner.  The spinner
        // is the same one used by simulation runs / .inp loads; the
        // accompanying status message tells the user what's running.
        connect(pt, &OpenSWMMVisMapToolSelectProfile::routingBusyChanged,
                this, &SWMMVis::onSetProgressBarBusy,
                Qt::UniqueConnection);
    }

    // Slice CF.3 — Pick 2D Cells tool: project window forwards cellsPicked
    // here, and we open / focus the Comparison Plot Dialog seeded with the
    // selected cells.
    connect(pw, &SWMMVisProjectWindow::pick2DCellsPicked,
            this, &SWMMVis::openComparisonPlotForCells,
            Qt::UniqueConnection);

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

    // Bind the property browser to the engine layer so typed adapters
    // (SWMMJunctionPropertyAdapter, etc.) can be constructed on identify.
    if (mAttributePanel)
        mAttributePanel->setProject(pw->modelLayer());

    // Rebind the Attribute Table to this project.
    if (mAttributeTablePanel)
        mAttributeTablePanel->setProject(pw->modelLayer(),
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

                    // Slice DA.2 — non-spatial Data Object kinds bypass
                    // identifyByName (which only handles spatial features
                    // with map coordinates) and dispatch straight to the
                    // typed property adapter via showDataObject.
                    using K = SWMMObjectRef::ObjectType;
                    switch (first.objectType) {
                    case K::Curve:        case K::TimeSeries:
                    case K::TimePattern:  case K::LIDControl:
                    case K::Pollutant:    case K::LandUse:
                    case K::Aquifer:      case K::Snowpack:
                    case K::Control:      case K::Transect:
                    case K::Hydrograph:   case K::Street:
                    case K::Inlet:        case K::RainGage:
                        mAttributePanel->showDataObject(
                            layer, static_cast<int>(first.objectType),
                            first.name);
                        return;
                    default:
                        break;
                    }

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

    // Rebind the terrain toolbar to the new project's canvas and restore its
    // per-project state.  Disconnect prior signal connections first to avoid
    // stacking lambdas on every tab switch.
    if (mTerrainToolbar) {
        mTerrainToolbar->rebindCanvas(pw->canvas());
        mTerrainToolbar->restoreState(pw->activeTerrainLayerPath(),
                                      pw->terrainNodeOffset(),
                                      pw->terrainLinkOffset(),
                                      pw->terrainVerticalUnit());

        QObject::disconnect(mTerrainToolbar, &TerrainToolbar::activeTerrainChanged,
                            this, nullptr);
        QObject::disconnect(mTerrainToolbar, &TerrainToolbar::nodeOffsetChanged,
                            this, nullptr);
        QObject::disconnect(mTerrainToolbar, &TerrainToolbar::linkOffsetChanged,
                            this, nullptr);
        QObject::disconnect(mTerrainToolbar, &TerrainToolbar::verticalUnitChanged,
                            this, nullptr);

        connect(mTerrainToolbar, &TerrainToolbar::activeTerrainChanged, this,
                [this, pw](GISRasterLayer *layer) {
                    if (pw != mActiveProjectWindow) return;
                    pw->setActiveTerrain(layer);
                    // Auto-detected unit was already set in onComboIndexChanged;
                    // propagate it to the project window.
                    pw->setTerrainVerticalUnit(mTerrainToolbar->verticalUnit());
                });
        connect(mTerrainToolbar, &TerrainToolbar::nodeOffsetChanged, this,
                [this, pw](double offset) {
                    if (pw != mActiveProjectWindow) return;
                    pw->setTerrainNodeOffset(offset);
                });
        connect(mTerrainToolbar, &TerrainToolbar::linkOffsetChanged, this,
                [this, pw](double offset) {
                    if (pw != mActiveProjectWindow) return;
                    pw->setTerrainLinkOffset(offset);
                });
        connect(mTerrainToolbar, &TerrainToolbar::verticalUnitChanged, this,
                [this, pw](const QString &unit) {
                    if (pw != mActiveProjectWindow) return;
                    pw->setTerrainVerticalUnit(unit);
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

    SimulationOptionsDialog dlg(pw->modelLayer()->engine(), pw->modelLayer(),
                                pw->engineVersion(), pw, this);
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

    // Also resolve the 2D HDF5 OUTPUT_FILE (when present in [2D_OPTIONS]) so
    // we can detect, prompt-on, and clear stale 2D results in lockstep with
    // the 1D .out — see CF.MVP-fix.2.
    const QString h5Path = SimulationRunner::parseTwoDOutputFile(inpPath);

    // ── Output-path collision guard ──────────────────────────────────
    // 1) An in-flight simulation is already writing to this .out — hard
    //    abort, even an overwrite would corrupt a running engine.
    const QString outCanon = QFileInfo(outPath).absoluteFilePath();
    for (SimulationRunner *runner : std::as_const(mActiveRunners)) {
        if (QFileInfo(runner->outPath()).absoluteFilePath() == outCanon) {
            QMessageBox::warning(this, tr("Output file in use"),
                tr("A simulation is already writing to:\n%1\n\n"
                   "Stop or wait for that run to finish before starting "
                   "another with the same output file.").arg(outPath));
            return;
        }
    }
    // 2) Concluded run / pre-existing .out (or .h5) / already-loaded
    //    results layer — overwriting is OK but warn the user first.
    //    Inspect every project's canvas, not just this one's, since a
    //    foreign project may have loaded the same .out / .h5 as a
    //    comparison source.
    {
        const QString h5Canon = h5Path.isEmpty()
                                  ? QString()
                                  : QFileInfo(h5Path).absoluteFilePath();

        bool resultLayerOpen   = false;
        bool result2DLayerOpen = false;
        for (QMdiSubWindow *sw : ui->mdiAreaCentral->subWindowList()) {
            auto *otherPw = qobject_cast<SWMMVisProjectWindow *>(sw);
            if (!otherPw || !otherPw->canvas()) continue;
            for (OpenSWMMVisLayer *l : otherPw->canvas()->layers()) {
                if (auto *rl = qobject_cast<SWMMResultsLayer *>(l)) {
                    if (QFileInfo(rl->resultsFilePath()).absoluteFilePath() == outCanon) {
                        resultLayerOpen = true;
                    }
                }
                if (!h5Canon.isEmpty()) {
                    if (auto *r2d = qobject_cast<SWMM2DResultsLayer *>(l)) {
                        if (auto *h5Src = dynamic_cast<HDF5Mesh2DSource *>(r2d->source());
                            h5Src && QFileInfo(h5Src->path()).absoluteFilePath() == h5Canon)
                        {
                            result2DLayerOpen = true;
                        }
                    }
                }
            }
        }
        const bool outOnDisk = QFileInfo(outPath).exists() && QFileInfo(outPath).size() > 0;
        const bool h5OnDisk  = !h5Canon.isEmpty() &&
                                QFileInfo(h5Canon).exists() &&
                                QFileInfo(h5Canon).size() > 0;

        if (resultLayerOpen || outOnDisk || result2DLayerOpen || h5OnDisk) {
            // Build a "which streams will be overwritten" sentence for the
            // dialog. Both / either / neither — only mention streams that
            // actually exist so the prompt stays accurate.
            QStringList existing;
            if (outOnDisk || resultLayerOpen)
                existing << tr("1D results (.out)");
            if (h5OnDisk || result2DLayerOpen)
                existing << tr("2D inundation results (.h5)");
            const QString streams = existing.join(tr(" and "));

            QStringList paths;
            if (outOnDisk || resultLayerOpen) paths << outPath;
            if (h5OnDisk  || result2DLayerOpen) paths << h5Path;
            const QString pathBlock = paths.join(QStringLiteral("\n"));

            const QString loadedNote = (resultLayerOpen || result2DLayerOpen)
                ? tr("They are currently loaded as a layer and will be "
                     "reloaded after the run.\n\n")
                : QString();

            const auto reply = QMessageBox::question(this,
                tr("Overwrite output?"),
                tr("%1 already exist for this model:\n%2\n\n"
                   "%3Running the simulation will overwrite them.  Continue?")
                    .arg(streams, pathBlock, loadedNote),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                onLogMessage(tr("Run cancelled — output overwrite declined."),
                             OpenSWMMVisLogMessage::LogMessageType::Information);
                return;
            }

            // Close any already-open .out handles so the engine can
            // truncate / rewrite without a sharing violation, and remove
            // any stale 2D-results layer pointing at the doomed .h5 (its
            // source is replaced by the upcoming run's twoDInitialized
            // handler, but the old layer must release its HDF5 handle so
            // the engine can recreate the file).
            for (QMdiSubWindow *sw : ui->mdiAreaCentral->subWindowList()) {
                auto *otherPw = qobject_cast<SWMMVisProjectWindow *>(sw);
                if (!otherPw || !otherPw->canvas()) continue;
                auto *canvas = otherPw->canvas();

                // First pass — close 1D results in place (the layer object
                // stays; only its file handle is dropped).
                for (OpenSWMMVisLayer *l : canvas->layers()) {
                    if (auto *rl = qobject_cast<SWMMResultsLayer *>(l)) {
                        if (QFileInfo(rl->resultsFilePath()).absoluteFilePath() == outCanon)
                            rl->closeResults();
                    }
                }

                // Second pass — collect indices of stale 2D layers and
                // remove them (takeLayer mutates the layers list, so we
                // can't iterate while removing).
                if (!h5Canon.isEmpty()) {
                    QList<int> stale2DIdx;
                    const auto layerList = canvas->layers();
                    for (int i = 0; i < layerList.size(); ++i) {
                        auto *r2d = qobject_cast<SWMM2DResultsLayer *>(layerList.at(i));
                        if (!r2d) continue;
                        auto *h5Src = dynamic_cast<HDF5Mesh2DSource *>(r2d->source());
                        if (h5Src && QFileInfo(h5Src->path()).absoluteFilePath() == h5Canon)
                            stale2DIdx.prepend(i);   // prepend → descending order for safe take
                    }
                    for (int idx : stale2DIdx) {
                        // closeSource first so the HDF5 handle is gone before
                        // we delete the file; then physically remove the
                        // layer (no undo — overwrite-confirm already accepted).
                        if (auto *r2d = qobject_cast<SWMM2DResultsLayer *>(
                                canvas->layers().at(idx)))
                            r2d->closeSource();
                        OpenSWMMVisLayer *taken = canvas->takeLayer(idx, /*pushUndo=*/false);
                        // If this layer was the animation controller's
                        // fallback, drop the dangling pointer.
                        if (taken == mAnimationController->fallback2DLayer())
                            mAnimationController->setFallback2DLayer(nullptr);
                        delete taken;
                    }
                }
            }
            if (h5OnDisk) {
                QFile::remove(h5Canon);
            }
        }
    }

    // Register the job in the status model and show the dock.
    // Reuse the existing row for this project window so re-running the same
    // model doesn't accumulate duplicate rows.
    const QString instanceName = fi.fileName();
    const QString engineVer    = pw ? pw->engineVersion() : QStringLiteral("6.0.0");
    const int jobId = mSimStatusModel->addOrReuseJobForModel(pw, instanceName, inpPath, engineVer);

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
    auto *runner = new SimulationRunner(jobId, instanceName, inpPath, rptPath, outPath,
                                        engineVer, this);
    mActiveRunners.insert(jobId, runner);

    // Pause / Cancel execution start out disabled in the .ui — flip them
    // on the moment a runner is registered so the toolbar buttons (and
    // the equivalent menu items) become clickable while a sim is in
    // flight. The finished handler below turns them back off when the
    // last runner finishes.
    ui->actionPauseExecution->setEnabled(true);
    ui->actionCancelExecution->setEnabled(true);

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

    // Capture the start time per job so the 2D HDF5 source can map its
    // /time (days-since-start) back to QDateTime for the global slider.
    connect(runner, &SimulationRunner::simulationDatesKnown, this,
            [this](int datesJobId, QDateTime start, QDateTime /*end*/) {
                mSimulationStarts.insert(datesJobId, start);
            });

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
                // launch with pause pre-engaged. Also disable the
                // Pause / Cancel actions so they only light up while a
                // run is actually in progress.
                if (self->mActiveRunners.isEmpty()) {
                    if (self->ui->actionPauseExecution->isChecked()) {
                        QSignalBlocker b(self->ui->actionPauseExecution);
                        self->ui->actionPauseExecution->setChecked(false);
                    }
                    self->ui->actionPauseExecution->setEnabled(false);
                    self->ui->actionCancelExecution->setEnabled(false);
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
                    // Reuse any existing layer pointing at this .out
                    // (typical case: same model re-run, or the
                    // overwrite-confirm path closed an open layer above
                    // — reopen its handle now that the engine has
                    // finished writing).  Otherwise create a fresh one.
                    const QString outCanon = QFileInfo(outPathCopy).absoluteFilePath();
                    SWMMResultsLayer *rl = nullptr;
                    for (OpenSWMMVisLayer *l : pwGuard->canvas()->layers()) {
                        if (auto *existing = qobject_cast<SWMMResultsLayer *>(l)) {
                            if (QFileInfo(existing->resultsFilePath()).absoluteFilePath() == outCanon) {
                                rl = existing;
                                rl->closeResults();
                                break;
                            }
                        }
                    }
                    const bool freshlyCreated = (rl == nullptr);
                    if (freshlyCreated) {
                        rl = new SWMMResultsLayer(outPathCopy,
                                                  pwGuard->modelLayer());
                        rl->setName(QFileInfo(outPathCopy).fileName());
                        pwGuard->canvas()->addLayer(rl, true);
                    }

                    QList<QString> rlWarnings, rlErrors;
                    if (rl->openResults(rlWarnings, rlErrors))
                    {
                        rl->autoStretchColorRamp();
                        self->mAnimationController->setPrimaryLayer(rl);
                    }
                    else
                    {
                        for (const QString &e : rlErrors)
                            self->onLogMessage(e, OpenSWMMVisLogMessage::Error);
                    }
                }

                runner->deleteLater();
            });

    // ── Slice CF.MVP — 2D inundation viz hookup ─────────────────────────────
    // twoDInitialized fires once after the engine reports the 2D solver is
    // active. Create an EngineMesh2DSource from the queried mesh geometry,
    // attach a SWMM2DResultsLayer to the canvas, and remember the layer by
    // jobId so per-tick depth slices can be routed to it.
    connect(runner, &SimulationRunner::twoDInitialized, this,
            [self, pwGuard]
            (int twoDJobId, QString h5Path,
             QVector<double> vx, QVector<double> vy, QVector<double> vz,
             QVector<int> triFlat) {
                if (!self || !pwGuard || !pwGuard->canvas()) return;
                const int nTri = triFlat.size() / 3;
                std::vector<std::array<int, 3>> tris(nTri);
                for (int t = 0; t < nTri; ++t) {
                    tris[t] = { triFlat[t*3+0], triFlat[t*3+1], triFlat[t*3+2] };
                }
                auto source = std::make_unique<EngineMesh2DSource>(
                    std::vector<double>(vx.begin(), vx.end()),
                    std::vector<double>(vy.begin(), vy.end()),
                    std::vector<double>(vz.begin(), vz.end()),
                    std::move(tris));

                auto *layer = new SWMM2DResultsLayer(
                    QStringLiteral("2D Results (live)"), nullptr);
                layer->setSource(std::move(source));
                // Stash h5 path on object property so the finished handler
                // can pick it up without an extra connect-time capture.
                layer->setProperty("snoopy_h5_path", h5Path);

                // Honour the engine's [2D_OPTIONS] DRY_DEPTH so the GUI
                // threshold matches what the solver considers wet — without
                // this, the layer's default (0.1 mm) clips below the engine
                // dry depth and leaves a fringe of "should-be-wet" cells
                // black at the wet/dry interface.
                if (pwGuard->modelLayer() && pwGuard->modelLayer()->engine()) {
                    double engineDry = 0.0;
                    if (swmm_2d_get_dry_depth(pwGuard->modelLayer()->engine(),
                                               &engineDry) == SWMM_OK &&
                        engineDry > 0.0)
                    {
                        layer->setDryDepth(engineDry);
                    }
                }

                pwGuard->canvas()->addLayer(layer, false);
                self->mActive2DResultsLayers.insert(twoDJobId, layer);

                // CF.MVP-fix.1 — register the live 2D layer as the animation
                // controller's fallback so play/scrub works even when no 1D
                // .out is loaded as the primary results layer.
                if (self->mAnimationController &&
                    !self->mAnimationController->primaryLayer())
                {
                    self->mAnimationController->setFallback2DLayer(layer);
                }

                self->onLogMessage(tr("2D surface routing active: %1 vertices, "
                                       "%2 triangles. Output → %3")
                                       .arg(vx.size()).arg(nTri)
                                       .arg(h5Path.isEmpty()
                                            ? tr("(no HDF5 path set)")
                                            : QFileInfo(h5Path).fileName()));
            });

    // Per-tick depth slice — push into the live EngineMesh2DSource.
    connect(runner, &SimulationRunner::twoDDepthsAvailable, this,
            [self](int twoDJobId, QVector<float> depths,
                   QDateTime simTime, double elapsedSec) {
                if (!self) return;
                auto it = self->mActive2DResultsLayers.constFind(twoDJobId);
                if (it == self->mActive2DResultsLayers.constEnd() || !it.value())
                    return;
                SWMM2DResultsLayer *layer = it.value();
                auto *engineSrc = dynamic_cast<EngineMesh2DSource *>(layer->source());
                if (!engineSrc) return;
                engineSrc->pushDepths(
                    std::vector<float>(depths.begin(), depths.end()),
                    simTime, elapsedSec);
                layer->refreshTimeRange();
            });

    // CF.2.4 — one-shot edge geometry handoff so the velocity overlay has
    // length + outward unit normal cached before any flux ticks arrive.
    connect(runner, &SimulationRunner::twoDEdgeGeometryAvailable, this,
            [self](int twoDJobId, QVector<float> length,
                   QVector<float> nx, QVector<float> ny) {
                if (!self) return;
                auto it = self->mActive2DResultsLayers.constFind(twoDJobId);
                if (it == self->mActive2DResultsLayers.constEnd() || !it.value())
                    return;
                auto *engineSrc =
                    dynamic_cast<EngineMesh2DSource *>(it.value()->source());
                if (!engineSrc) return;
                engineSrc->setEdgeGeometry(
                    std::vector<float>(length.begin(), length.end()),
                    std::vector<float>(nx.begin(),     nx.end()),
                    std::vector<float>(ny.begin(),     ny.end()));
            });

    // CF.2.4 — per-tick edge flux: paired with the matching depth tick on
    // the source side by elapsedSec, so a single tick yields a single
    // history frame regardless of queue ordering.
    connect(runner, &SimulationRunner::twoDFluxAvailable, this,
            [self](int twoDJobId, QVector<float> flux,
                   QDateTime simTime, double elapsedSec) {
                if (!self) return;
                auto it = self->mActive2DResultsLayers.constFind(twoDJobId);
                if (it == self->mActive2DResultsLayers.constEnd() || !it.value())
                    return;
                SWMM2DResultsLayer *layer = it.value();
                auto *engineSrc =
                    dynamic_cast<EngineMesh2DSource *>(layer->source());
                if (!engineSrc) return;
                engineSrc->pushFlux(
                    std::vector<float>(flux.begin(), flux.end()),
                    simTime, elapsedSec);
                layer->refreshTimeRange();
            });

    // On finished (success path), swap the layer's source from the live
    // EngineMesh2DSource to an HDF5Mesh2DSource so the user can scrub back.
    // The .h5 path was stashed on the layer in the twoDInitialized handler.
    connect(runner, &SimulationRunner::finished, this,
            [self](int finishedJobId, bool success, int /*errCode*/,
                   QString /*errMsg*/, double /*runoff*/, double /*routing*/) {
                if (!self) return;
                auto it = self->mActive2DResultsLayers.find(finishedJobId);
                if (it == self->mActive2DResultsLayers.end()) return;
                if (success && it.value()) {
                    SWMM2DResultsLayer *layer = it.value();
                    const QString h5Path =
                        layer->property("snoopy_h5_path").toString();
                    if (!h5Path.isEmpty() && QFileInfo::exists(h5Path)) {
                        auto h5Src = std::make_unique<HDF5Mesh2DSource>();
                        // Anchor the source's time axis to wall-clock so the
                        // global animation slider's QDateTime ticks map to
                        // 2D frame indices via SWMM2DResultsLayer::setCurrentSimTime.
                        const QDateTime simStart =
                            self->mSimulationStarts.value(finishedJobId);
                        if (simStart.isValid())
                            h5Src->setSimulationStart(simStart);
                        if (h5Src->open(h5Path)) {
                            const int nFrames = h5Src->timeCount();
                            // Scan all frames for the run's actual peak so
                            // the colour ramp + dry-cell threshold match
                            // the data range. Without this, setSource()
                            // defaults to the LAST frame (often fully
                            // drained) AND the layer's auto-grown
                            // max_depth_ may still be wider than the
                            // actual peak, leaving everything dim.
                            IMesh2DSource* srcRaw = h5Src.get();
                            int   peakFrame = 0;
                            float peakDepth = 0.0f;
                            std::vector<float> probe;
                            for (int t = 0; t < nFrames; ++t) {
                                if (!srcRaw->readDepthsAt(t, probe)) continue;
                                if (probe.empty()) continue;
                                const float m = *std::max_element(probe.begin(),
                                                                    probe.end());
                                if (m > peakDepth) { peakDepth = m; peakFrame = t; }
                            }

                            layer->setName(QStringLiteral("2D Results"));
                            layer->setSource(std::move(h5Src));

                            // Auto-tune the ramp + dry threshold to the
                            // actual data range. setMaxDepth pins the
                            // upper end (disables further auto-grow);
                            // dry_depth is biased to the floor so very
                            // shallow runs still produce visible cells.
                            if (peakDepth > 0.0f) {
                                layer->setMaxDepth(peakDepth);
                                layer->setDryDepth(
                                    std::max(1e-5, 0.05 * double(peakDepth)));
                                layer->setCurrentTimeIndex(peakFrame);
                            }

                            self->onLogMessage(tr("2D scrub ready: %1 frames from %2. "
                                                   "Peak depth %3 m at cell %4, frame %5.")
                                                   .arg(nFrames)
                                                   .arg(QFileInfo(h5Path).fileName())
                                                   .arg(double(peakDepth), 0, 'f', 4)
                                                   .arg(layer->currentPeak().second)
                                                   .arg(peakFrame));
                        }
                    }
                }
                self->mActive2DResultsLayers.erase(it);
                self->mSimulationStarts.remove(finishedJobId);
            });

    runner->start();
}

void SWMMVis::onPlotTimeSeries()
{
    // Wired in Slice M-2 below.
}

void SWMMVis::onCursorPositionChanged(double mapX, double mapY)
{
    // mapX/mapY arrive in canvas CRS. When the active model layer's native
    // CRS differs (e.g. geographic data displayed via Web Mercator on-the-fly
    // reprojection), reverse-transform so the read-out shows the cursor
    // position in the user's data CRS rather than the display CRS.
    double dispX = mapX, dispY = mapY;
    SpatialReferenceSystem *displaySRS = nullptr;
    if (auto *pw = activeProjectWindow()) {
        if (SWMMModelLayer *layer = pw->modelLayer()) {
            layer->transformCanvasToLayer(mapX, mapY, dispX, dispY);
            displaySRS = layer->srs();
        }
    }
    if (!displaySRS) {
        if (MapCanvas *c = activeCanvas()) displaySRS = c->canvasSRS();
    }

    // Degrees need more decimals than meters/feet to be useful (1e-5°
    // ≈ 1 m at the equator).
    const int decimals = (displaySRS && displaySRS->isGeographic()) ? 7 : 4;
    QString text = QStringLiteral("%1, %2")
                       .arg(dispX, 0, 'f', decimals)
                       .arg(dispY, 0, 'f', decimals);

    // Append terrain Z with the unit assigned to the canvas by the project window.
    if (MapCanvas *c = activeCanvas()) {
        const auto z = c->terrainZ();
        if (z.has_value()) {
            const QString unit = c->terrainUnit();
            text += unit.isEmpty()
                ? QStringLiteral("  Z: %1").arg(*z, 0, 'f', 3)
                : QStringLiteral("  Z: %1 %2").arg(*z, 0, 'f', 3).arg(unit);
        }
    }

    mLineEditCoordinates->setText(text);
}

void SWMMVis::onCanvasSRSChanged(SpatialReferenceSystem *srs)
{
    mToolButtonCoordinateReferenceSystem->setText(
        srs ? srs->toAuthority() : QStringLiteral("Unknown"));
}

void SWMMVis::onMapScaleEntered(const QString &text)
{
    MapCanvas *c = activeCanvas();
    if (!c) return;

    const double denom = parseScaleText(text);
    if (denom <= 0.0) {
        // Invalid entry — restore the current scale so the user sees a
        // sensible value rather than their unparseable input.
        onCanvasScaleChanged(c->scaleDenominator());
        return;
    }

    c->setScaleDenominator(denom);
    // setScaleDenominator → setExtent → scaleChanged → onCanvasScaleChanged,
    // which will overwrite the edit text with the canonical formatted form.
}

void SWMMVis::onCanvasScaleChanged(double denominator)
{
    if (!mComboBoxMapScale) return;
    // Block signals so updating the edit text doesn't re-trigger
    // editingFinished and feed back into onMapScaleEntered.
    QSignalBlocker block(mComboBoxMapScale);
    mComboBoxMapScale->setEditText(formatScaleText(denominator));
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

void SWMMVis::onAddBasemapLayer()
{
    AddBasemapDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    OpenSWMMVisLayer *layer = dlg.createLayer(nullptr);
    if (!layer) return;

    if (MapCanvas *c = activeCanvas())
        c->addLayer(layer, true);
    onLogMessage(tr("Added basemap layer: %1").arg(layer->name()));
}

void SWMMVis::onAddWMSLayer()
{
    AddBasemapDialog dlg(this);
    dlg.setInitialTab(1); // WMS / WMTS tab
    if (dlg.exec() != QDialog::Accepted) return;

    OpenSWMMVisLayer *layer = dlg.createLayer(nullptr);
    if (!layer) return;

    if (MapCanvas *c = activeCanvas())
        c->addLayer(layer, true);
    onLogMessage(tr("Added WMS/WMTS layer: %1").arg(layer->name()));
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

    // Dedup — every output file is only loaded once per project canvas.
    // If a layer already points at this path, focus it as the primary
    // and reload its handle rather than appending a duplicate.
    const QString canon = QFileInfo(path).absoluteFilePath();
    SWMMResultsLayer *layer = nullptr;
    for (OpenSWMMVisLayer *l : pw->canvas()->layers()) {
        if (auto *existing = qobject_cast<SWMMResultsLayer *>(l)) {
            if (QFileInfo(existing->resultsFilePath()).absoluteFilePath() == canon) {
                layer = existing;
                layer->closeResults();   // reopen below so re-loads pick up new data
                break;
            }
        }
    }
    if (!layer) {
        layer = new SWMMResultsLayer(path, pw->modelLayer());
        layer->setName(QFileInfo(path).fileName());
        pw->canvas()->addLayer(layer, true);
    }

    QList<QString> warnings, errors;
    if (layer->openResults(warnings, errors))
    {
        layer->autoStretchColorRamp();
        mAnimationController->setPrimaryLayer(layer);
        onLogMessage(tr("Added results layer: %1").arg(QFileInfo(path).fileName()));
    }
    else
    {
        for (const QString &e : errors)
            onLogMessage(e, OpenSWMMVisLogMessage::Error);
    }
}
