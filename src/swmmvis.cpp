/*!
 * \file   swmmvis.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */

#include <QCommandLinkButton>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QFont>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QGridLayout>
#include <QComboBox>
#include <QToolButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QDateTimeEdit>

#include "ui/widgets/cursorwindowslider.h"
#include <QProgressBar>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QHeaderView>
#include <QMetaEnum>
#include <QFileInfo>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QGraphicsItem>
#include <QFileDialog>
#include <QDir>
#include <QInputDialog>
#include <QJsonDocument>
#include <algorithm>    // std::sort — Message Log copy actions
#include <QClipboard>   // 2026-06-04 — Message Log copy actions
#include <QCursor>      // Slice PT.1 — exec menu at pointer
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QJsonArray>
#include <QJsonObject>
#include <QProxyStyle>
#include <QSet>           // Slice RA.4 — writable-extensions set for normalizer
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
#include "io/gdaldrivers.h"
#include "ui/dialogs/sublayerselectiondialog.h"
#include "ui/dialogs/dialoglayoutpersistence.h"
#include "ui_swmmvis.h"
#include "version.h"
#include "legacy_version.h"

#include "core/unitsystem.h"
#include "ui/widgets/attributepickermenu.h"  // Slice PT.1 — picker for plotTimeSeries
#include "core/preferencesmanager.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"   // #36 — MapUndoStack* → QUndoStack* upcast at dialog calls
#include "map/spatialreferencesystem.h"
#include "map/openswmmvisscene.h"
#include "map/mapextent.h"
#include "project/ioportabilitynormalizer.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "project/saveaspathnormalizer.h"
#include "simulation/runpathresolver.h"   // Slice QB.3
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
#include "ui/dialogs/layerstyledialog.h"
#include "ui/dialogs/meshgenerationdialog.h"
#include "ui/dialogs/newprojectdialog.h"
#include "ui/dialogs/pluginsdialog.h"
#include "ui/dialogs/preferencesdialog.h"
// Slice Z.17c — Style Manager dialog.
#include "ui/dialogs/stylemanagerdialog.h"
// Slice Z.18 — Layer Styling dock.
#include "ui/panels/layerstylingdock.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/climatologydialog.h"
#include "ui/dialogs/statisticsdashboarddialog.h"
#include "ui/dialogs/userflagsdialog.h"
#include "layers/tabulardatalayer.h"
#include "selection/selectionmanager.h"
#include "plot/profilenetworkadapter.h"
#include "plot/profilerouter.h"
#include "layers/swmmresultslayer.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_output.h>

#include <QClipboard>
#include <QDockWidget>
#include <QGuiApplication>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>

#include <vector>
#include "ui/dialogs/profileplotdialog.h"
#include "ui/dialogs/meshprofileplotdialog.h"
#include "ui/dialogs/comparisonplotdialog.h"
#include "plot/comparisonplotmodel.h"
#include "ui/dialogs/addbasemapdialog.h"
#include "ui/widgets/legendoverlay.h"
#include "ui/widgets/perattributethemingwidget.h"
#include "ui/panels/layertreepanel.h"
#include "ui/panels/legenddock.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/attributetablepanel.h"
#include "plugins/filefilterregistry.h"
#include "selection/selectionmanager.h"
#include "simulation/simulationrunner.h"
#include "ui/dialogs/statusreportdialog.h"           // Slice GUI-2026-05-30 §6
#include "simulation/simulationstatusmodel.h"

#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>   // node id enumeration for coupled-node dropdown
#include <openswmm/engine/openswmm_spatial.h> // node coordinates for Auto-couple
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
#include "output/outputstatsregistry.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "mesh/inpmeshreader.h"
#include "animation/animationcontroller.h"
#include "ui/toolbars/terraintoolbar.h"
#include "ui/toolbars/mesheditingtoolbar.h"

#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/dialogs/curveeditordialog.h"
#include "layers/swmmmodellayer.h"
#include "timeseries/timeseriesregistry.h"
#include "timeseries/timeseriesprovider.h"
#include "curve/curveregistry.h"
#include "curve/curveprovider.h"
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

    // Feature A — accept drops of .inp / .oswp files anywhere on the window.
    setAcceptDrops(true);

    initializeToolBars();
    initializeStatusBar();
    initializeWelcomeScreen();
    initializeDockWidgets();
    initializeMenus();
    initializeMapTools();
    initializeSettings();
    initializeDefaultWorkspaceSession();

    // Allow the window to shrink well below the size implied by the toolbars,
    // dock widgets and central MDI area so it can be snapped/tiled to half the
    // screen. A QMainWindow otherwise derives its minimum from its children;
    // an explicit minimum overrides that layout-computed floor.
    setMinimumSize(480, 360);
}

SWMMVis::~SWMMVis()
{
    delete ui;
}

// ── Drag-and-drop of project / model files (Feature A) ──────────────────────

QStringList SWMMVis::acceptableDropPaths(const QMimeData *mime) const
{
    QStringList paths;
    if (!mime || !mime->hasUrls())
        return paths;

    for (const QUrl &url : mime->urls())
    {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (!QFileInfo(path).isFile())
            continue;
        if (path.endsWith(QStringLiteral(".inp"), Qt::CaseInsensitive) ||
            path.endsWith(QStringLiteral(".oswp"), Qt::CaseInsensitive))
            paths << path;
    }
    return paths;
}

void SWMMVis::dragEnterEvent(QDragEnterEvent *event)
{
    if (!acceptableDropPaths(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void SWMMVis::dragMoveEvent(QDragMoveEvent *event)
{
    if (!acceptableDropPaths(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void SWMMVis::dropEvent(QDropEvent *event)
{
    const QStringList paths = acceptableDropPaths(event->mimeData());
    if (paths.isEmpty())
    {
        event->ignore();
        return;
    }
    event->acceptProposedAction();

    // onOpenProject() routes .oswp vs .inp and de-dups already-open windows.
    for (const QString &path : paths)
        onOpenProject(path);
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
 * @brief Show/hide the MDI tab belonging to a sub-window. QMdiArea only
 *        removes tabs when a sub-window is removed from the area — a
 *        merely *hidden* sub-window keeps its tab on screen — so the
 *        hide-in-place welcome scheme must drive the tab's visibility
 *        itself. Tab index == position in subWindowList(): QMdiArea
 *        appends tabs in child-insertion order and its internal tab bar
 *        is not user-reorderable.
 */
void setSubWindowTabVisible(QMdiArea *area, QMdiSubWindow *sub, bool visible)
{
    if (!area || !sub) return;
    auto *tabBar = area->findChild<QTabBar *>();
    if (!tabBar) return;
    const int idx = area->subWindowList().indexOf(sub);
    if (idx >= 0 && idx < tabBar->count())
        tabBar->setTabVisible(idx, visible);
}

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
    // (macOS defaults to LEFT via the native style hint).
    if (auto *tabBar = ui->mdiAreaCentral->findChild<QTabBar *>())
    {
        tabBar->setStyle(new TabCloseRightStyle(tabBar->style()));
        // Tabs that already exist (the welcome tab is added during
        // setupUi, before this style swap) carry their close button in
        // the LEFT slot. QTabBarPrivate::closeTab() looks the clicked
        // button up only on the side the hint reports at click time —
        // RightSide from here on — so a left-slot button no longer maps
        // to any tab and its clicks are silently dropped (welcome tab
        // X did nothing). Migrate existing buttons to the right slot.
        for (int i = 0; i < tabBar->count(); ++i)
        {
            QWidget *btn = tabBar->tabButton(i, QTabBar::LeftSide);
            if (btn && !tabBar->tabButton(i, QTabBar::RightSide))
            {
                tabBar->setTabButton(i, QTabBar::LeftSide, nullptr);
                tabBar->setTabButton(i, QTabBar::RightSide, btn);
            }
        }
    }

    // Welcome sub-window lifecycle (simplified from the previous
    // delete-on-close + reparent scheme, which was brittle — reopen via
    // Help → Show Welcome sometimes failed the tab-close button because
    // the reparent path left welcomeWidget in an ambiguous parent
    // state). New scheme: the sub-window is KEPT alive (not destroyed
    // on close) and we just toggle its visibility. Closing via the tab
    // X hides the sub (Qt's default for close() when WA_DeleteOnClose
    // is false). NOTE: QMdiArea does NOT drop the tab of a hidden sub
    // (tabs are only removed when a sub leaves the area), so every
    // hide/show of the sub also toggles its tab via
    // setSubWindowTabVisible. Reopen = show() + tab visible.
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
        {
            sub->hide();  // hide without triggering the close event
            setSubWindowTabVisible(ui->mdiAreaCentral, sub, false);
        }
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
    initializeMeshEditingToolBar();
    initializeAnalysisLayerCombos();
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
        QStringLiteral("actionAddText"),
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

// Slice §V.VB — Mesh Editing toolbar peer of TerrainToolbar.  Docked in
// the top toolbar area immediately after Terrain, with insertToolBarBreak
// so it gets its own row (avoids horizontal cramming).  Per Q-V8 user
// recommendation.
void SWMMVis::initializeMeshEditingToolBar()
{
    mMeshEditingToolbar = new MeshEditingToolbar(tr("Mesh Editing"), this);
    addToolBar(Qt::TopToolBarArea, mMeshEditingToolbar);
    insertToolBarBreak(mMeshEditingToolbar);

    connect(mMeshEditingToolbar, &MeshEditingToolbar::editVertexToggled,
            this, [this](bool on) {
        auto *pw = activeProjectWindow();
        if (!pw) return;
        if (on) pw->activateMeshSelectVertexTool();
        else    pw->activateSelectTool();
    });
    connect(mMeshEditingToolbar, &MeshEditingToolbar::editEdgeToggled,
            this, [this](bool on) {
        auto *pw = activeProjectWindow();
        if (!pw) return;
        if (on) pw->activateMeshSelectEdgeTool();
        else    pw->activateSelectTool();
    });

    // Slice §V.VC — picker callbacks dispatch through the rich editor
    // dialogs ([[feedback_data_object_pickers]]). The toolbar stays
    // decoupled from the registries; lambdas close over `this` to
    // resolve the active project window's SWMMModelLayer at click time.
    auto stagePicker = [this](const QString &cur) -> QString {
        auto *pw = activeProjectWindow();
        if (!pw || !pw->modelLayer()) return {};
        auto *reg = qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(
            pw->modelLayer()->ensureTimeseriesRegistry());
        if (!reg) return {};
        return openswmmvis::ui::TimeseriesEditorDialog::pickTimeseries(reg, nullptr, cur, this);
    };
    mMeshEditingToolbar->setStageTimeseriesPicker(stagePicker);
    mMeshEditingToolbar->setFlowTimeseriesPicker(stagePicker);

    mMeshEditingToolbar->setRatingCurvePicker(
        [this](const QString &cur) -> QString {
            auto *pw = activeProjectWindow();
            if (!pw || !pw->modelLayer()) return {};
            auto *reg = qobject_cast<openswmmvis::curve::CurveRegistry *>(
                pw->modelLayer()->ensureCurveRegistry());
            if (!reg) return {};
            return openswmmvis::ui::CurveEditorDialog::pickCurve(reg, nullptr, cur, this);
        });

    // Listers populate the TS / curve comboboxes from the project's
    // registries so the user can pick an existing object without
    // opening the CRUD dialog. Re-queried on rebindCanvas + after
    // each CRUD dialog closes.
    mMeshEditingToolbar->setTimeseriesLister([this]() -> QStringList {
        auto *pw = activeProjectWindow();
        if (!pw || !pw->modelLayer()) return {};
        auto *reg = qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(
            pw->modelLayer()->ensureTimeseriesRegistry());
        if (!reg) return {};
        QStringList names;
        for (auto *p : reg->providers()) if (p) names.append(p->name());
        names.sort(Qt::CaseInsensitive);
        return names;
    });
    // Couplable SWMM node ids for the mesh vertex coupled-node dropdown.
    mMeshEditingToolbar->setNodeLister([this]() -> QStringList {
        auto *pw = activeProjectWindow();
        if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine()) return {};
        SWMM_Engine e = pw->modelLayer()->engine();
        QStringList ids;
        const int nNodes = swmm_node_count(e);
        for (int i = 0; i < nNodes; ++i) {
            const char *id = swmm_node_id(e, i);
            if (id && id[0] != '\0') ids.append(QString::fromUtf8(id));
        }
        ids.sort(Qt::CaseInsensitive);
        return ids;
    });
    // Node ids + [COORDINATES] for the Auto-couple action. Raw map units —
    // the same CRS the mesh vertices carry, so coincidence needs no
    // conversion. Nodes without coordinates are skipped.
    mMeshEditingToolbar->setNodeLocator(
        [this]() -> QVector<QPair<QString, QPointF>> {
            auto *pw = activeProjectWindow();
            if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine()) return {};
            SWMM_Engine e = pw->modelLayer()->engine();
            QVector<QPair<QString, QPointF>> out;
            const int nNodes = swmm_node_count(e);
            out.reserve(nNodes);
            for (int i = 0; i < nNodes; ++i) {
                const char *id = swmm_node_id(e, i);
                if (!id || id[0] == '\0') continue;
                double x = 0.0, y = 0.0;
                if (swmm_spatial_get_node_coord(e, i, &x, &y) != 0) continue;
                out.append({ QString::fromUtf8(id), QPointF(x, y) });
            }
            return out;
        });
    mMeshEditingToolbar->setCurveLister([this]() -> QStringList {
        auto *pw = activeProjectWindow();
        if (!pw || !pw->modelLayer()) return {};
        auto *reg = qobject_cast<openswmmvis::curve::CurveRegistry *>(
            pw->modelLayer()->ensureCurveRegistry());
        if (!reg) return {};
        QStringList names;
        for (auto *p : reg->providers()) if (p) names.append(p->name());
        names.sort(Qt::CaseInsensitive);
        return names;
    });

    // ── 2D-results interactions at the end of the mesh toolbar ──────────────
    // Select 2D Cells (the cell-selector peer of the vertex/edge selectors)
    // and the Trace Profile Path tool, placed after the BC controls (before
    // the expanding spacer) via addToolAction(). Both are checkable and kept
    // OUT of the toolbar's exclusive vertex/edge edit group so they toggle
    // freely. Their objectNames match SWMMVisProjectWindow::toolActionKeys()
    // so the existing active-tool checked-state sync keeps them in step.
    mMeshEditingToolbar->addToolSeparator();

    auto *actPick2DCells = new QAction(QIcon(QStringLiteral(":/swmmvis/SelectCell")),
                                       "", mMeshEditingToolbar);
    actPick2DCells->setObjectName(QStringLiteral("actionPick2DCells"));
    actPick2DCells->setCheckable(true);
    actPick2DCells->setToolTip(tr(
        "Select cells on the 2D mesh results layer. Single-click selects and "
        "highlights a cell (Shift = add, Ctrl = toggle); drag a box or press L "
        "to lasso multiple. Right-click a selection to plot its depth / HGL / "
        "velocity time series. Esc clears."));
    connect(actPick2DCells, &QAction::toggled, this,
            [this, actPick2DCells](bool checked) {
        auto *pw = activeProjectWindow();
        if (!pw) { actPick2DCells->setChecked(false); return; }
        if (checked) pw->activatePick2DCellsTool();
        else         pw->activateSelectTool();
    });
    mMeshEditingToolbar->addToolAction(actPick2DCells);
    // Cell-selection info label right after the Select-2D-Cells tool (like the
    // edge label after Edit Edge), then the per-cell editors (Manning's n +
    // tag) so they sit in the 2D-cell group. The toolbar hides them unless a
    // single cell is selected.
    mMeshEditingToolbar->addToolWidget(mMeshEditingToolbar->cellInfoLabel());
    QAction *cellManningsAct =
        mMeshEditingToolbar->addToolWidget(mMeshEditingToolbar->cellManningsWidget());
    QAction *cellTagAct =
        mMeshEditingToolbar->addToolWidget(mMeshEditingToolbar->cellTagWidget());
    mMeshEditingToolbar->setCellEditorActions(cellManningsAct, cellTagAct);

    // Icon-only on the toolbar — like Edit Vertex / Edit Edge above, the
    // QAction text is left empty so only the icon shows; the descriptive
    // label lives in the tooltip set below.
    auto *actMeshProfile = new QAction(QIcon(QStringLiteral(":/swmmvis/Profile")),
                                       QString(), mMeshEditingToolbar);
    actMeshProfile->setObjectName(QStringLiteral("actionMeshProfile"));
    actMeshProfile->setCheckable(true);
    actMeshProfile->setToolTip(tr(
        "Draw a polyline across the 2D mesh to plot a bed/terrain elevation "
        "profile. For water depth + envelope, use the Analysis toolbar's Plot "
        "Profile. Click to add vertices, double-click or Enter to finish, "
        "right-click to undo, Esc to cancel."));
    connect(actMeshProfile, &QAction::toggled, this,
            [this, actMeshProfile](bool checked) {
        auto *pw = activeProjectWindow();
        if (!pw) { actMeshProfile->setChecked(false); return; }
        if (checked) pw->activateMeshProfileTool();
        else         pw->activateSelectTool();
    });
    mMeshEditingToolbar->addToolAction(actMeshProfile);
}

void SWMMVis::initializeAnalysisLayerCombos()
{
    // Two selectors on the Analysis toolbar: the active 1D results layer and
    // the active 2D results layer. Choosing one makes it the target of every
    // analysis / visualization tool (Comparison plot, Profile, Tabular, color-
    // by-result, animation, 2D cell picking). "— none —" returns to model
    // editing. This replaces the old "first results layer found" guess.
    //
    // The combos lead the toolbar (inserted before the .ui-defined actions)
    // so the run selection reads left-to-right ahead of the analysis tools
    // that act on it.
    QAction *anchor = ui->toolBarAnalysis->actions().isEmpty()
                          ? nullptr
                          : ui->toolBarAnalysis->actions().first();

    mLabelActiveResults1D = new QLabel(tr("1D results:"), this);
    mLabelActiveResults1D->setContentsMargins(6, 0, 4, 0);
    mComboActiveResults1D = new QComboBox(this);
    mComboActiveResults1D->setMinimumWidth(160);
    mComboActiveResults1D->setToolTip(tr(
        "Active 1D results layer for analysis (plots, tables, color-by-result, "
        "animation). Pick a run to analyze its elements; choose \"— none —\" to "
        "return to model editing."));
    ui->toolBarAnalysis->insertWidget(anchor, mLabelActiveResults1D);
    ui->toolBarAnalysis->insertWidget(anchor, mComboActiveResults1D);

    mLabelActiveResults2D = new QLabel(tr("2D results:"), this);
    mLabelActiveResults2D->setContentsMargins(10, 0, 4, 0);
    mComboActiveResults2D = new QComboBox(this);
    mComboActiveResults2D->setMinimumWidth(160);
    mComboActiveResults2D->setToolTip(tr(
        "Active 2D results layer for analysis (mesh-cell depth / velocity plots, "
        "mesh profiles, animation). Pick a run, or \"— none —\" to return to "
        "mesh editing."));
    ui->toolBarAnalysis->insertWidget(anchor, mLabelActiveResults2D);
    ui->toolBarAnalysis->insertWidget(anchor, mComboActiveResults2D);

    // Live-render toggle, immediately right of the 2D dropdown. Off stops 2D
    // rendering AND frame streaming for the active live layer during a run, to
    // save GPU/CPU; on resumes and jumps to the newest frame. Only meaningful
    // for a live/streaming source, so it is disabled otherwise (see
    // refreshActiveResultsCombos()). Default checked.
    mCheckBoxLive2D = new QCheckBox(tr("Live render"), this);
    mCheckBoxLive2D->setChecked(true);
    mCheckBoxLive2D->setContentsMargins(8, 0, 4, 0);
    mCheckBoxLive2D->setToolTip(tr(
        "Render the active 2D results layer live while a simulation runs. "
        "Uncheck to stop 2D rendering and frame streaming during the run to "
        "save GPU/CPU; re-check to resume at the newest frame. Only applies to "
        "a live (streaming) results layer."));
    ui->toolBarAnalysis->insertWidget(anchor, mCheckBoxLive2D);
    ui->toolBarAnalysis->insertSeparator(anchor);

    // User picks → set the active layer on the current project window. The
    // stored item data is the layer pointer (quintptr) or 0 for "— none —".
    connect(mComboActiveResults1D, qOverload<int>(&QComboBox::activated),
            this, [this](int idx) {
        auto *pw = activeProjectWindow();
        if (!pw) return;
        auto *layer = reinterpret_cast<SWMMResultsLayer *>(
            mComboActiveResults1D->itemData(idx).value<quintptr>());
        pw->setActiveResultsLayer(layer);
    });
    connect(mComboActiveResults2D, qOverload<int>(&QComboBox::activated),
            this, [this](int idx) {
        auto *pw = activeProjectWindow();
        if (!pw) return;
        auto *layer = reinterpret_cast<SWMM2DResultsLayer *>(
            mComboActiveResults2D->itemData(idx).value<quintptr>());
        pw->setActive2DResultsLayer(layer);
    });

    // Live-render toggle: gate the active 2D layer's streaming/render work.
    connect(mCheckBoxLive2D, &QCheckBox::toggled, this, [this](bool checked) {
        auto *pw = activeProjectWindow();
        if (!pw) return;
        if (auto *layer = pw->active2DResultsLayer())
            layer->setLiveRenderEnabled(checked);
    });

    refreshActiveResultsCombos();
}

void SWMMVis::refreshActiveResultsCombos()
{
    if (!mComboActiveResults1D || !mComboActiveResults2D)
        return;

    auto *pw = activeProjectWindow();

    // Repopulate without firing the `activated`-driven setters (we use
    // currentIndexChanged-free `activated`, but block anyway for safety).
    QSignalBlocker b1(mComboActiveResults1D);
    QSignalBlocker b2(mComboActiveResults2D);

    mComboActiveResults1D->clear();
    mComboActiveResults2D->clear();
    mComboActiveResults1D->addItem(tr("— none —"), QVariant::fromValue<quintptr>(0));
    mComboActiveResults2D->addItem(tr("— none —"), QVariant::fromValue<quintptr>(0));

    if (!pw) {
        mComboActiveResults1D->setEnabled(false);
        mComboActiveResults2D->setEnabled(false);
        return;
    }

    // 1D: registry is the single source of truth for loaded .out layers.
    int sel1D = 0;
    if (auto *reg = pw->statsRegistry()) {
        const auto ids = reg->identities();
        for (const openswmmvis::OutputIdentity &id : ids) {
            if (!id.layer) continue;
            mComboActiveResults1D->addItem(
                id.shortLabel,
                QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(id.layer)));
            mComboActiveResults1D->setItemData(
                mComboActiveResults1D->count() - 1, id.tooltipPath, Qt::ToolTipRole);
            if (id.layer == pw->activeResultsLayer())
                sel1D = mComboActiveResults1D->count() - 1;
        }
    }
    mComboActiveResults1D->setCurrentIndex(sel1D);
    mComboActiveResults1D->setEnabled(mComboActiveResults1D->count() > 1);

    // 2D: the registry only tracks 1D layers, so enumerate the canvas for
    // SWMM2DResultsLayer instances directly.
    int sel2D = 0;
    if (auto *canvas = pw->canvas()) {
        for (OpenSWMMVisLayer *l : canvas->layers()) {
            auto *r2d = qobject_cast<SWMM2DResultsLayer *>(l);
            if (!r2d) continue;
            mComboActiveResults2D->addItem(
                r2d->name(),
                QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(r2d)));
            if (r2d == pw->active2DResultsLayer())
                sel2D = mComboActiveResults2D->count() - 1;
        }
    }
    mComboActiveResults2D->setCurrentIndex(sel2D);
    mComboActiveResults2D->setEnabled(mComboActiveResults2D->count() > 1);

    // Live-render toggle reflects the active 2D layer's gate, and is only
    // meaningful for a live/streaming source (greyed out otherwise).
    if (mCheckBoxLive2D) {
        SWMM2DResultsLayer *active2D = pw->active2DResultsLayer();
        const bool live = active2D && active2D->source()
                          && active2D->source()->isLive();
        QSignalBlocker bLive(mCheckBoxLive2D);
        mCheckBoxLive2D->setEnabled(live);
        mCheckBoxLive2D->setChecked(active2D ? active2D->liveRenderEnabled()
                                             : true);
    }
}

void SWMMVis::initializeAnimationToolBar()
{
    mAnimationController = new AnimationController(this);

    // Issue 1 — single-thumb scrubber. The thumb is the current time (cursor);
    // the look-back window is a non-interactive band painted ending at the
    // cursor, its width driven solely by the "Window:" spin box below. Dragging
    // the thumb moves only the cursor (no second handle), so there is no
    // two-handle feedback round-trip — the source of the old lag.
    mAnimationSlider = new openswmmvis::ui::CursorWindowSlider(this);
    mAnimationSlider->setMinimumWidth(240);
    // Stretch to fill the toolbar's free width (the cursor + window band read
    // more precisely on a wide track).
    mAnimationSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mAnimationSlider->setToolTip(
        tr("Animation time (thumb). The shaded band is the look-back window — "
           "set its width with the Window box. Drag the thumb or click the "
           "track to scrub."));
    mAnimationSlider->setStatusTip(tr("Animation Time"));

    mLabelAnimationWindow = new QLabel(tr("Window:"), this);
    mLabelAnimationWindow->setContentsMargins(6, 0, 4, 0);
    mSpinAnimationWindow = new QDoubleSpinBox(this);
    mSpinAnimationWindow->setDecimals(1);
    mSpinAnimationWindow->setRange(0.0, 1.0e6);
    mSpinAnimationWindow->setSingleStep(1.0);
    mSpinAnimationWindow->setSuffix(tr(" min"));
    mSpinAnimationWindow->setToolTip(
        tr("Look-back window: each output shows its latest frame within this "
           "span at or before the cursor (0 = latest at-or-before)."));

    mDateTimeEditAnimationTime = new QDateTimeEdit(this);
    mDateTimeEditAnimationTime->setDisplayFormat(QStringLiteral("MM/dd/yyyy hh:mm"));
    mDateTimeEditAnimationTime->setCalendarPopup(true);
    mDateTimeEditAnimationTime->setToolTip(tr("Animation Time"));
    mDateTimeEditAnimationTime->setStatusTip(tr("Animation Time"));

    ui->toolBarAnimation->insertWidget(ui->actionSkipForward, mAnimationSlider);
    ui->toolBarAnimation->insertWidget(ui->actionSkipForward, mLabelAnimationWindow);
    ui->toolBarAnimation->insertWidget(ui->actionSkipForward, mSpinAnimationWindow);
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

    // DateTime display (read-only — controller drives it). Also fans the
    // time scrub out to any 2D results layer on the active canvas so the
    // single slider drives both 1D and 2D playback in lockstep.
    //
    // Slice §Y.1 — gate the fan-out on layer visibility. Pushing
    // setCurrentSimTime into a hidden 2D layer still triggers result fetch
    // + scene work; skipping invisible layers keeps the animation hot path
    // limited to what the user can actually see.
    connect(mAnimationController, &AnimationController::currentTimeChanged,
            this, [this](const QDateTime &dt) {
        QSignalBlocker b(mDateTimeEditAnimationTime);
        mDateTimeEditAnimationTime->setDateTime(dt);

        if (auto *pw = activeProjectWindow()) {
            if (auto *canvas = pw->canvas()) {
                for (OpenSWMMVisLayer *l : canvas->layers()) {
                    auto *r2d = qobject_cast<SWMM2DResultsLayer *>(l);
                    if (r2d && r2d->isVisible())
                        r2d->setCurrentSimTimeAsOf(dt);   // causal: never ahead of cursor
                }
            }
        }
    });


    // ── Sync window: range slider ↔ span box ↔ controller cursor/window ──
    // All controls share the controller's cursor (currentTime) and windowMs as
    // the single source of truth; programmatic updates are guarded against
    // feedback with QSignalBlocker, mirroring the single-slider wiring above.
    auto driverSpanMs = [this]() -> qint64 {
        const QDateTime s = mAnimationController->driverStartTime();
        const QDateTime e = mAnimationController->driverEndTime();
        return (s.isValid() && e.isValid()) ? s.msecsTo(e) : 0;
    };
    auto normTime = [this, driverSpanMs](const QDateTime &dt) -> qreal {
        const qint64 span = driverSpanMs();
        if (span <= 0 || !dt.isValid()) return 0.0;
        return std::clamp(double(mAnimationController->driverStartTime().msecsTo(dt))
                              / double(span), 0.0, 1.0);
    };
    auto denormTime = [this, driverSpanMs](qreal v) -> QDateTime {
        const QDateTime s = mAnimationController->driverStartTime();
        const qint64 span = driverSpanMs();
        if (span <= 0 || !s.isValid()) return s;
        return s.addMSecs(static_cast<qint64>(std::clamp(v, 0.0, 1.0) * double(span)));
    };
    // Push the controller's cursor + window into the single slider (no echo):
    // the thumb goes to the cursor, the band width = window / span. The band is
    // a decoration; only the thumb is interactive.
    auto syncCursorFromState = [this, driverSpanMs, normTime]() {
        if (!mAnimationSlider) return;
        const qint64 spanMs = driverSpanMs();
        if (spanMs <= 0) return;
        const QDateTime t = mAnimationController->currentDateTime();
        if (!t.isValid()) return;
        const qreal wN = std::clamp(double(mAnimationController->windowMs())
                                        / double(spanMs), 0.0, 1.0);
        QSignalBlocker b(mAnimationSlider);
        mAnimationSlider->setWindowNorm(wN);
        mAnimationSlider->setCursorNorm(normTime(t));   // clamped to [0,1] inside
    };

    // Slider (user) → controller: the thumb is the cursor, so a scrub only
    // seeks. The window is owned by the Window spin box; scrubbing never
    // changes it, so there is no two-handle round-trip (Issue 1).
    connect(mAnimationSlider, &openswmmvis::ui::CursorWindowSlider::cursorChanged,
            this, [this, denormTime](qreal cursor) {
        const QDateTime t = denormTime(cursor);
        if (!t.isValid()) return;
        // Block the slider while the controller fans the seek back out through
        // currentTimeChanged (which re-sets the thumb).
        QSignalBlocker b(mAnimationSlider);
        mAnimationController->seekToTime(t);
    });

    // Span box (user) → controller window.
    connect(mSpinAnimationWindow, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double minutes) {
        mAnimationController->setWindowSec(minutes * 60.0);
    });

    // Controller window → span box + slider band (no echo).
    connect(mAnimationController, &AnimationController::windowChanged,
            this, [this, syncCursorFromState](qint64 ms) {
        QSignalBlocker b(mSpinAnimationWindow);
        mSpinAnimationWindow->setValue(double(ms) / 60000.0);
        syncCursorFromState();
    });

    // Controller cursor time → slider thumb.
    connect(mAnimationController, &AnimationController::currentTimeChanged,
            this, [syncCursorFromState](const QDateTime &) { syncCursorFromState(); });

    // Run loaded / range changed → refresh span-box max (run duration) + slider.
    connect(mAnimationController, &AnimationController::totalPeriodsChanged,
            this, [this, driverSpanMs, syncCursorFromState](int) {
        const qint64 span = driverSpanMs();
        QSignalBlocker b(mSpinAnimationWindow);
        mSpinAnimationWindow->setMaximum(span > 0 ? double(span) / 60000.0 : 1.0e6);
        syncCursorFromState();
    });

    // Default look-back window: 10 minutes. Set after the controls are wired so
    // windowChanged fans the value out to the span box + slider band.
    mAnimationController->setWindowSec(600.0);
}

void SWMMVis::initializeMapTools()
{
    // All tool actions are checkable — the activeToolChanged handler below
    // keeps exactly one checked at a time to show which tool is active.
    const QStringList toolActionNames = {
        QStringLiteral("actionPan"),     QStringLiteral("actionZoomIn"),
        QStringLiteral("actionZoomOut"), QStringLiteral("actionSelect"),
        QStringLiteral("actionSelectByPolygon"),
        QStringLiteral("actionMeasure"), QStringLiteral("actionPlotProfile"),
        QStringLiteral("actionAddJunction"), QStringLiteral("actionAddOutfall"),
        QStringLiteral("actionAddStorage"), QStringLiteral("actionAddFlowDivider"),
        QStringLiteral("actionAddPipe"),  QStringLiteral("actionAddPump"),
        QStringLiteral("actionAddOrifice"), QStringLiteral("actionAddWeir"),
        QStringLiteral("actionAddOutlet"),
        QStringLiteral("actionRainGauge"), QStringLiteral("actionAddSubcatchment"),
        QStringLiteral("actionAddText"),
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
    connect(ui->actionSelectByPolygon, &QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activateSelectByPolygonTool();
    });
    connect(ui->actionMeasure,&QAction::triggered, this, [this]() {
        if (auto *w = activeProjectWindow()) w->activateMeasureTool();
    });
    connect(ui->actionPlotProfile, &QAction::triggered, this, [this]() {
        onPlotProfileTriggered(/*forceMode=*/0);
    });
    // US.A2 — explicit override dropdown on the Plot Profile button: lets the
    // user force a Network or 2D-Surface profile even when both are loaded.
    {
        auto *menu = new QMenu(this);
        QAction *net  = menu->addAction(tr("Network Profile…"));
        QAction *surf = menu->addAction(tr("Surface (2D mesh) Profile…"));
        connect(net,  &QAction::triggered, this, [this]() { onPlotProfileTriggered(1); });
        connect(surf, &QAction::triggered, this, [this]() { onPlotProfileTriggered(2); });
        if (auto *btn = qobject_cast<QToolButton *>(
                ui->toolBarAnalysis->widgetForAction(ui->actionPlotProfile))) {
            btn->setMenu(menu);
            btn->setPopupMode(QToolButton::MenuButtonPopup);
        }
    }
    // Slice GUI-2026-05-30 §6 — Analysis toolbar Report action opens the
    // two-panel Report Viewer over the active project's .rpt sibling.
    connect(ui->actionReport, &QAction::triggered, this, &SWMMVis::onShowReport);
    // Slice GUI-2026-05-30 §5 — Plot Timeseries entry point on the Analysis
    // toolbar.  Uses the current canvas selection when one exists; otherwise
    // arms a one-shot map pick + offers a System Variable side path.
    connect(ui->actionPlotTimeSeries, &QAction::triggered,
            this, &SWMMVis::onPlotTimeSeries);
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
    connect(ui->actionSelectUpstream, &QAction::triggered, this, &SWMMVis::onSelectUpstream);
    connect(ui->actionSelectDownstream, &QAction::triggered, this, &SWMMVis::onSelectDownstream);

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
    // Layout: "Offset Mode: Elevation [toggle] Depth". The toggle is checked
    // for ELEVATION (matching isElevationOffsetMode); the flanking labels are
    // static and the active side is bolded via updateOffsetModeLabels().
    ui->statusBar->addPermanentWidget(new QLabel("Offset Mode:", ui->statusBar));
    mLabelOffsetElevation = new QLabel("Elevation", ui->statusBar);
    ui->statusBar->addPermanentWidget(mLabelOffsetElevation);
    mCheckBoxLevelOffsetMode = new QCheckBox(ui->statusBar);
    mCheckBoxLevelOffsetMode->setStyleSheet(
        "QCheckBox::indicator:checked   {image: url(:/swmmvis/ToggleOn);}"
        "QCheckBox::indicator:unchecked {image: url(:/swmmvis/ToggleOff);}");
    mCheckBoxLevelOffsetMode->setEnabled(false);
    connect(mCheckBoxLevelOffsetMode, &QCheckBox::toggled, this, [this](bool on) {
        auto *pw = activeProjectWindow();
        if (!pw) {
            updateOffsetModeLabels(on);
            return;
        }

        // Flip the LINK_OFFSETS option first — the switch happens regardless of
        // the convert choice below (legacy UpdateOffsets parity).
        pw->setElevationOffsetMode(on);
        updateOffsetModeLabels(on);

        // Offer to convert existing link offsets, but only when the model has
        // links to convert (matches EPA SWMM-GUI, which skips the prompt for an
        // empty network).
        auto *layer = pw->modelLayer();
        const int nLinks =
            (layer && layer->engine()) ? swmm_link_count(layer->engine()) : 0;
        if (nLinks <= 0)
            return;

        const QString msg =
            on ? tr("You have switched from using Depth offsets to Elevation "
                    "offsets.\nShould all link offsets be converted to "
                    "elevations now?")
               : tr("You have switched from using Elevation offsets to Depth "
                    "offsets.\nShould all link offsets be converted to depths "
                    "now?");

        const auto choice = QMessageBox::question(
            this, tr("Convert Link Offsets"), msg,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (choice == QMessageBox::Yes)
            pw->convertLinkOffsets(on);   // on == true → convert to Elevation
        // No → leave the stored offset values untouched (switch without changes).
    });
    ui->statusBar->addPermanentWidget(mCheckBoxLevelOffsetMode);
    mLabelOffsetDepth = new QLabel("Depth", ui->statusBar);
    ui->statusBar->addPermanentWidget(mLabelOffsetDepth);
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
    mLineEditCoordinates->setMinimumWidth(120);
    mLineEditCoordinates->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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

void SWMMVis::updateOffsetModeLabels(bool elevation)
{
    if (mLabelOffsetElevation) {
        QFont f = mLabelOffsetElevation->font();
        f.setBold(elevation);
        mLabelOffsetElevation->setFont(f);
    }
    if (mLabelOffsetDepth) {
        QFont f = mLabelOffsetDepth->font();
        f.setBold(!elevation);
        mLabelOffsetDepth->setFont(f);
    }
}

void SWMMVis::initializeDockWidgets()
{
    initializeLayersDockWidget();
    initializeObjectBrowserDockWidget();
    initializePropertiesPanelDockWidget();
    initializeSimulationStatusDockWidget();
    initializeMessageLogDockWidget();
    initializeLegendDockWidget();
}

void SWMMVis::initializeLegendDockWidget()
{
    // Slice BB Phase 8.6.11 / 8.6.16 — dockable Legend / per-class style editor.
    // Created free-standing from code (no .ui placeholder); docks to the
    // right side by default so it lives next to the Attribute panel and
    // leaves the left edge for the Layers / Object Browser panels.
    mLegendDock = new openswmmvis::ui::LegendDock(this);
    addDockWidget(Qt::RightDockWidgetArea, mLegendDock);
    mLegendDock->hide();   // user reveals via the View menu / View toolbar.

    // Slice Z.18 — always-open layer-styling editor. Default-hidden so
    // a first-time launch UX matches the legacy single-modal workflow;
    // the user opens it via View → Layer Styling Dock and from then on
    // the dock state persists across sessions through Qt's saveState/
    // restoreState plumbing further down the ctor (objectName set in
    // the dock ctor is what makes that work).
    mLayerStylingDock = new openswmmvis::ui::LayerStylingDock(this);
    addDockWidget(Qt::RightDockWidgetArea, mLayerStylingDock);
    mLayerStylingDock->hide();
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

    // Right-click "Properties…" / double-click on a layer row → open the
    // unified LayerStyleDialog (Slice U-3). Replaces the legacy
    // LayerPropertiesDialog so every layer gets the same multitab dialog.
    connect(mLayerTreePanel, &LayerTreePanel::layerPropertiesRequested,
            this, [this](OpenSWMMVisLayer *layer) {
                if (!layer) return;
                openswmmvis::ui::LayerStyleDialog dlg(
                    layer, QString(), this,
                    activeCanvas() ? activeCanvas()->undoStack() : nullptr);  // #36
                dlg.exec();
                if (auto *c = activeCanvas())
                    c->invalidate(MapCanvas::Raster | MapCanvas::Scene,
                                  QStringLiteral("layer-style-apply"));
            });

    // Slice S1 — Layer-row "Set Style…" now routes to the unified
    // LayerStyleDialog instead of the legacy SymbologyDialog. Same dialog
    // every other "Properties…" entry point opens.
    connect(mLayerTreePanel, &LayerTreePanel::layerStyleRequested,
            this, [this](OpenSWMMVisLayer *layer) {
                if (!layer) return;
                openswmmvis::ui::LayerStyleDialog dlg(
                    layer, QString(), this,
                    activeCanvas() ? activeCanvas()->undoStack() : nullptr);  // #36
                dlg.exec();
                if (auto *c = activeCanvas())
                    c->invalidate(MapCanvas::Raster | MapCanvas::Scene,
                                  QStringLiteral("layer-style-apply"));
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

    // Right-click on a SWMM Output layer → "Plot Time Series…" — pops an
    // object picker (type + id from the .out itself) then the variable
    // picker, anchored to that specific results layer.
    connect(mLayerTreePanel, &LayerTreePanel::plotTimeSeriesFromOutputLayerRequested,
            this, &SWMMVis::onPlotTimeSeriesFromOutputLayer);

    // Layer-tree "Set as Active Results Layer" → route to the active project
    // window so the chosen layer becomes the analysis target. Connected once
    // (the panel is a shared dock retargeted per tab); the handler resolves the
    // current window at click time.
    connect(mLayerTreePanel, &LayerTreePanel::setActiveResultsLayerRequested,
            this, [this](SWMMResultsLayer *layer) {
        if (auto *pw = activeProjectWindow()) pw->setActiveResultsLayer(layer);
    });
    connect(mLayerTreePanel, &LayerTreePanel::setActive2DResultsLayerRequested,
            this, [this](SWMM2DResultsLayer *layer) {
        if (auto *pw = activeProjectWindow()) pw->setActive2DResultsLayer(layer);
    });
}

void SWMMVis::onPlotTimeSeriesFromOutputLayer(SWMMResultsLayer *layer)
{
    if (!layer) return;
    SWMM_Output out = layer->outputHandle();
    if (!out) {
        // .out not open yet — try to open it transparently. SWMMResultsLayer's
        // openResults() returns warnings/errors lists.
        QList<QString> warnings, errors;
        if (!layer->openResults(warnings, errors) || !(out = layer->outputHandle())) {
            QMessageBox::warning(this, tr("Plot Time Series"),
                tr("Could not open the results file:\n%1").arg(layer->resultsFilePath()));
            return;
        }
    }

    // Object picker: cascading combos for Type → Object id → Variable.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Plot Time Series — %1").arg(layer->name()));
    auto *form = new QFormLayout(&dlg);

    auto *classCombo = new QComboBox(&dlg);
    classCombo->addItem(tr("Node"),         int(SWMMObjectRef::Node));
    classCombo->addItem(tr("Link"),         int(SWMMObjectRef::Link));
    classCombo->addItem(tr("Subcatchment"), int(SWMMObjectRef::Subcatchment));
    form->addRow(tr("Type:"), classCombo);

    auto *idCombo  = new QComboBox(&dlg);
    auto *varCombo = new QComboBox(&dlg);
    form->addRow(tr("Object:"),   idCombo);
    form->addRow(tr("Variable:"), varCombo);

    auto refreshObjects = [&]() {
        idCombo->clear();
        const auto t = static_cast<SWMMObjectRef::ObjectType>(classCombo->currentData().toInt());
        switch (t) {
        case SWMMObjectRef::Node: {
            const int n = swmm_output_get_node_count(out);
            for (int i = 0; i < n; ++i)
                idCombo->addItem(QString::fromUtf8(swmm_output_get_node_id(out, i)));
            break;
        }
        case SWMMObjectRef::Link: {
            const int n = swmm_output_get_link_count(out);
            for (int i = 0; i < n; ++i)
                idCombo->addItem(QString::fromUtf8(swmm_output_get_link_id(out, i)));
            break;
        }
        case SWMMObjectRef::Subcatchment: {
            const int n = swmm_output_get_subcatch_count(out);
            for (int i = 0; i < n; ++i)
                idCombo->addItem(QString::fromUtf8(swmm_output_get_subcatch_id(out, i)));
            break;
        }
        default: break;
        }
    };
    using PA = openswmmvis::plot::PlotAttribute;
    auto refreshVariables = [&]() {
        varCombo->clear();
        const auto t = static_cast<SWMMObjectRef::ObjectType>(classCombo->currentData().toInt());
        switch (t) {
        case SWMMObjectRef::Node:
            varCombo->addItem(tr("Depth"),          int(PA::NodeDepth));
            varCombo->addItem(tr("Head"),           int(PA::NodeHead));
            varCombo->addItem(tr("Volume"),         int(PA::NodeVolume));
            varCombo->addItem(tr("Lateral inflow"), int(PA::NodeLateralInflow));
            varCombo->addItem(tr("Total inflow"),   int(PA::NodeTotalInflow));
            varCombo->addItem(tr("Overflow"),       int(PA::NodeOverflow));
            break;
        case SWMMObjectRef::Link:
            varCombo->addItem(tr("Flow"),     int(PA::LinkFlow));
            varCombo->addItem(tr("Depth"),    int(PA::LinkDepth));
            varCombo->addItem(tr("Velocity"), int(PA::LinkVelocity));
            varCombo->addItem(tr("Volume"),   int(PA::LinkVolume));
            varCombo->addItem(tr("Capacity"), int(PA::LinkCapacity));
            break;
        case SWMMObjectRef::Subcatchment:
            varCombo->addItem(tr("Rainfall"),     int(PA::SubcatchRainfall));
            varCombo->addItem(tr("Snow depth"),   int(PA::SubcatchSnowDepth));
            varCombo->addItem(tr("Evaporation"),  int(PA::SubcatchEvap));
            varCombo->addItem(tr("Infiltration"), int(PA::SubcatchInfil));
            varCombo->addItem(tr("Runoff"),       int(PA::SubcatchRunoff));
            break;
        default: break;
        }
    };
    connect(classCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            &dlg, [&](int) { refreshObjects(); refreshVariables(); });
    refreshObjects();
    refreshVariables();

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);

    if (dlg.exec() != QDialog::Accepted) return;
    if (idCombo->currentText().isEmpty() || !varCombo->currentData().isValid()) return;

    SWMMObjectRef ref;
    ref.objectType = static_cast<SWMMObjectRef::ObjectType>(classCombo->currentData().toInt());
    ref.name       = idCombo->currentText();
    const auto attr = static_cast<PA>(varCombo->currentData().toInt());
    openComparisonPlotForAttributeOnLayer(ref, attr, layer);
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

    // Slice S1 — open the unified LayerStyleDialog scoped to the kind's
    // tab (routingId = "<model|results>.<kindName>"). Replaces the legacy
    // SymbologyDialog so renderer-class swap + per-kind editing share
    // the same UI surface as every other styling entry point.
    auto kindRoutingId = [&]() -> QString {
        const auto cat = static_cast<SWMMModelLayer::Category>(kindOrdinal);
        const QString prefix = swmm ? QStringLiteral("model.")
                                     : QStringLiteral("results.");
        switch (cat) {
            case SWMMModelLayer::CatJunctions:     return prefix + QStringLiteral("junctions");
            case SWMMModelLayer::CatOutfalls:      return prefix + QStringLiteral("outfalls");
            case SWMMModelLayer::CatStorage:       return prefix + QStringLiteral("storage");
            case SWMMModelLayer::CatDividers:      return prefix + QStringLiteral("dividers");
            case SWMMModelLayer::CatConduits:      return prefix + QStringLiteral("conduits");
            case SWMMModelLayer::CatPumps:         return prefix + QStringLiteral("pumps");
            case SWMMModelLayer::CatOrifices:      return prefix + QStringLiteral("orifices");
            case SWMMModelLayer::CatWeirs:         return prefix + QStringLiteral("weirs");
            case SWMMModelLayer::CatOutlets:       return prefix + QStringLiteral("outlets");
            case SWMMModelLayer::CatSubcatchments: return prefix + QStringLiteral("subcatchments");
            case SWMMModelLayer::CatRainGages:     return prefix + QStringLiteral("raingages");
            default:                               return QString();
        }
    }();

    openswmmvis::ui::LayerStyleDialog dlg(
        layer, kindRoutingId, this,
        activeCanvas() ? activeCanvas()->undoStack() : nullptr);  // #36
    dlg.exec();
    if (auto *cv = activeCanvas())
        cv->invalidate(MapCanvas::Scene, QStringLiteral("layer-style-apply"));
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
    // When multiple .out layers are loaded, the Object Browser shows a
    // "Plot Time Series ▸ <layer>" submenu; this signal carries the user's
    // results-layer choice so we plot against that one explicitly.
    connect(mObjectBrowserPanel, &ObjectBrowserPanel::plotTimeSeriesForLayerRequested,
            this, &SWMMVis::openTimeSeriesPlotForOnLayer);

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

void SWMMVis::openTimeSeriesPlotForOnLayer(const SWMMObjectRef &ref,
                                            SWMMResultsLayer *layer)
{
    // Same flow as openTimeSeriesPlotFor() but plots against \p layer
    // specifically (skipping the implicit first-found pick). Used by the
    // Object Browser's "Plot Time Series ▸ <layer>" submenu when more
    // than one .out is loaded.
    using openswmmvis::plot::ObjectRef;
    using openswmmvis::plot::PlotAttribute;

    if (!layer) { openTimeSeriesPlotFor(ref); return; }

    ObjectRef::Kind kind = ObjectRef::Kind::Unknown;
    switch (ref.objectType) {
    case SWMMObjectRef::Node:         kind = ObjectRef::Kind::Node;     break;
    case SWMMObjectRef::Link:         kind = ObjectRef::Kind::Link;     break;
    case SWMMObjectRef::Subcatchment: kind = ObjectRef::Kind::Subcatch; break;
    default:                          break;
    }
    if (kind == ObjectRef::Kind::Unknown) {
        openComparisonPlotForOnLayer(ref, layer);
        return;
    }

    const auto units = UnitSystem::instance() && UnitSystem::instance()->isSI()
        ? openswmmvis::plot::UnitSystem::SI
        : openswmmvis::plot::UnitSystem::US;
    QMenu *menu = openswmmvis::ui::AttributePickerMenu::createForObjectKind(
        kind, units, this);
    if (!menu) { openComparisonPlotForOnLayer(ref, layer); return; }

    menu->setTitle(tr("Plot %1 (%2) …").arg(ref.name, layer->name()));
    QAction *picked = menu->exec(QCursor::pos());
    const PlotAttribute attr = picked
        ? openswmmvis::ui::AttributePickerMenu::attributeFrom(picked)
        : PlotAttribute::Unknown;
    menu->deleteLater();

    if (!picked) return;

    if (attr == PlotAttribute::Unknown) {
        openComparisonPlotForOnLayer(ref, layer);
    } else {
        openComparisonPlotForAttributeOnLayer(ref, attr, layer);
    }
}

void SWMMVis::openComparisonPlotFor(const SWMMObjectRef &ref)
{
    openComparisonPlotForOnLayer(ref, nullptr);
}

void SWMMVis::openComparisonPlotForOnLayer(const SWMMObjectRef &ref,
                                            SWMMResultsLayer *preferred)
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas()) return;

    // Use the caller-supplied layer when given (Object Browser's results
    // submenu route); otherwise target the project window's ACTIVE 1D results
    // layer (chosen via the Analysis-toolbar combo or the layer-tree "Set as
    // Active Results Layer" action) rather than guessing the first one found.
    SWMMResultsLayer *resultsLayer = preferred;
    if (!resultsLayer)
        resultsLayer = pw->activeResultsLayer();
    if (!resultsLayer)
    {
        QMessageBox::information(this, tr("No active results layer"),
            tr("Pick a results layer in the Analysis toolbar's \"1D results\" "
               "selector, or run a simulation / add a SWMM Output (.out) layer."));
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
    openComparisonPlotForAttributeOnLayer(ref, attribute, nullptr);
}

void SWMMVis::openComparisonPlotForAttributeOnLayer(const SWMMObjectRef &ref,
                                                     openswmmvis::plot::PlotAttribute attribute,
                                                     SWMMResultsLayer *preferred)
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas()) return;

    // Honour the caller-supplied results layer when given (e.g. Object
    // Browser's "Plot Time Series ▸ <layer>" submenu); otherwise target the
    // project window's ACTIVE 1D results layer.
    SWMMResultsLayer *resultsLayer = preferred;
    if (!resultsLayer)
        resultsLayer = pw->activeResultsLayer();
    if (!resultsLayer) {
        QMessageBox::information(this, tr("No active results layer"),
            tr("Pick a results layer in the Analysis toolbar's \"1D results\" "
               "selector, or run a simulation / add a SWMM Output (.out) layer."));
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

    // Target the active 1D results layer rather than the first one found.
    SWMMResultsLayer *resultsLayer = pw->activeResultsLayer();
    if (!resultsLayer) {
        QMessageBox::information(this, tr("No active results layer"),
            tr("Pick a results layer in the Analysis toolbar's \"1D results\" "
               "selector, or run a simulation / add a SWMM Output (.out) layer."));
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

void SWMMVis::openMeshEdgeFluxPlotFor(SWMM2DMeshLayer *mesh, int triIdx, int edgeLocal,
                                      openswmmvis::plot::PlotAttribute attr)
{
    Q_UNUSED(mesh);  // triIdx/edgeLocal index the shared engine mesh; the
                     // results layer's source carries the per-edge flux feed.
    auto *pw = activeProjectWindow();
    if (!pw) return;

    SWMM2DResultsLayer *layer = pw->active2DResultsLayer();
    if (!layer || !layer->source()) {
        QMessageBox::information(this, tr("No active 2D results layer"),
            tr("Pick a results layer in the Analysis toolbar's \"2D results\" "
               "selector (run a 2D simulation first if none are loaded)."));
        return;
    }
    if (!layer->hasEdgeFluxData()) {
        QMessageBox::information(this, tr("Edge flow/flux unavailable"),
            tr("This run has no per-edge flux data. Re-run with the current "
               "engine to enable edge flow / flux plotting."));
        return;
    }

    // Find-or-create the comparison dialog (mirrors openComparisonPlotForCells).
    auto *dlg = findChild<openswmmvis::ui::ComparisonPlotDialog *>();
    if (!dlg) {
        dlg = new openswmmvis::ui::ComparisonPlotDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &openswmmvis::ui::ComparisonPlotDialog::addFromMapToggled,
                this, &SWMMVis::onAddFromMapToggled);
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

    // Add the single requested edge series — flow Q (m³/s) or unit-width flux q
    // (m²/s) — for the picked edge; it lands on its own chart row.
    const auto edgeRef = openswmmvis::plot::ObjectRef::forMesh2DEdge(triIdx, edgeLocal);
    dlg->addSeries(runIdx, edgeRef, attr);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void SWMMVis::openMeshVertexSeriesFor(SWMM2DMeshLayer *mesh,
                                       const QVector<int> &vertexIdxList)
{
    Q_UNUSED(mesh);  // vertex indices reference the shared engine mesh; the
                     // results layer's source carries the depth feed.
    auto *pw = activeProjectWindow();
    if (!pw || vertexIdxList.isEmpty()) return;

    SWMM2DResultsLayer *layer = pw->active2DResultsLayer();
    if (!layer || !layer->source()) {
        QMessageBox::information(this, tr("No active 2D results layer"),
            tr("Pick a results layer in the Analysis toolbar's \"2D results\" "
               "selector before plotting vertex time series."));
        return;
    }

    auto *dlg = findChild<openswmmvis::ui::ComparisonPlotDialog *>();
    if (!dlg) {
        dlg = new openswmmvis::ui::ComparisonPlotDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &openswmmvis::ui::ComparisonPlotDialog::addFromMapToggled,
                this, &SWMMVis::onAddFromMapToggled);
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

    // Warn on large multi-selections (2 series per vertex).
    const int total = vertexIdxList.size() * 2;
    if (total > 500) {
        const auto choice = QMessageBox::question(this, tr("Many series"),
            tr("This will create %1 series (%2 vertices × depth + HGL). Continue?")
                .arg(total).arg(vertexIdxList.size()));
        if (choice != QMessageBox::Yes) return;
    }

    using openswmmvis::plot::ObjectRef;
    using openswmmvis::plot::PlotAttribute;
    for (int v : vertexIdxList) {
        dlg->addSeries(runIdx, ObjectRef::forMesh2DVertex(v), PlotAttribute::Mesh2DDepth);
        dlg->addSeries(runIdx, ObjectRef::forMesh2DVertex(v), PlotAttribute::Mesh2DHGL);
    }
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

    // Parent the dialog to its project sub-window so it lives in the Qt
    // object tree: it still floats above the project (Qt::Window |
    // StaysOnTop are set in the ctor — stacking hints, independent of
    // parentage), but it now closes with its document and is destroyed
    // when the app shuts down.  A parentless dialog is a "primary window"
    // and, per QApplication::quitOnLastWindowClosed, keeps the whole app
    // alive after the main window closes.  WA_DeleteOnClose still owns
    // per-close cleanup.  The dialog queries the project window
    // dynamically for the active terrain raster so loading a terrain
    // *after* the dialog is open still updates the ground line.
    auto *dlg = new ProfilePlotDialog(model, mAnimationController, path,
                                      pw, /*parent=*/pw);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // Route the profile dialog's attribute-picker right-click into a
    // ComparisonPlotDialog that floats above the profile (parented to it
    // with Qt::Tool flags) rather than the shared main-window-parented
    // dialog used by the map view.
    connect(dlg, &ProfilePlotDialog::plotAttributeRequested,
            this, [this, dlg](const SWMMObjectRef &ref,
                              openswmmvis::plot::PlotAttribute attribute) {
        openComparisonPlotOverlayForProfile(dlg, ref, attribute);
    });
    dlg->show();
}

void SWMMVis::openMeshProfileDialog(const QVector<QPointF> &scenePolyline,
                                    SWMM2DResultsLayer *results,
                                    const QString &title)
{
    auto *pw = activeProjectWindow();
    if (!pw) return;
    if (scenePolyline.size() < 2) return;

    // Resolve the active 2D mesh (geometry → ground). The mesh is required;
    // results are optional — bed-only (terrain) when null.
    SWMM2DMeshLayer *mesh = mMeshEditingToolbar ? mMeshEditingToolbar->activeMesh()
                                                : nullptr;
    if (!mesh) {
        if (auto *canvas = pw->canvas())
            for (OpenSWMMVisLayer *l : canvas->layers())
                if (auto *m = qobject_cast<SWMM2DMeshLayer *>(l)) { mesh = m; break; }
    }
    if (!mesh) {
        QMessageBox::information(this, tr("No 2D mesh"),
            tr("Load or generate a 2D mesh before tracing a profile path."));
        return;
    }

    auto *dlg = new MeshProfilePlotDialog(mesh, results, mAnimationController,
                                          scenePolyline, pw, /*parent=*/pw);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if (!title.isEmpty())
        dlg->setWindowTitle(title);
    dlg->show();
}

void SWMMVis::openMeshProfilePlotFor(const QVector<QPointF> &scenePolyline)
{
    // Slice US.A1 — Analysis variant: ground + animated depth + envelope from
    // the user's chosen active 2D results layer.
    auto *pw = activeProjectWindow();
    openMeshProfileDialog(scenePolyline,
                          pw ? pw->active2DResultsLayer() : nullptr,
                          tr("2D Mesh Profile"));
}

void SWMMVis::openMeshBedProfilePlotFor(const QVector<QPointF> &scenePolyline)
{
    // Slice US.A1 — mesh-toolbar variant: bed/terrain only (no water surface).
    // The sampler + plot widget already render a ground-only profile when
    // results is null.
    openMeshProfileDialog(scenePolyline, /*results=*/nullptr,
                          tr("2D Mesh Bed Profile"));
}

void SWMMVis::onPlotProfileTriggered(int forceMode)
{
    // Slice US.A2 — context-sensitive dispatch. The single Analysis "Plot
    // Profile" entry plots a network (pipe HGL) profile or a 2D surface
    // profile depending on selection + what's loaded. The toolbar button's
    // dropdown passes forceMode 1 / 2 for an explicit override.
    auto *pw = activeProjectWindow();
    if (!pw) return;

    if (forceMode == 1) { pw->activateSelectProfileTool();        return; }
    if (forceMode == 2) { pw->activateAnalysisMeshProfileTool();  return; }

    const bool hasModel = pw->hasModelLayer();
    const bool hasMesh  = pw->hasMeshLayer();

    if (!hasModel && !hasMesh) {
        QMessageBox::information(this, tr("Nothing to profile"),
            tr("Load a SWMM network or a 2D mesh before plotting a profile."));
        return;
    }

    // Explicit 1D selection wins — the user pointed at pipes/nodes.
    bool oneDSelected = false;
    if (auto *canvas = pw->canvas())
        for (OpenSWMMVisLayer *l : canvas->layers())
            if (auto *m = qobject_cast<SWMMModelLayer *>(l))
                if (!m->selectedElementNames().isEmpty()) { oneDSelected = true; break; }

    if (hasMesh && !hasModel)        pw->activateAnalysisMeshProfileTool();
    else if (hasModel && !hasMesh)   pw->activateSelectProfileTool();
    else if (oneDSelected)           pw->activateSelectProfileTool();
    else                             pw->activateSelectProfileTool(); // both, no 1D pick → network default (use dropdown for surface)
}

void SWMMVis::openComparisonPlotOverlayForProfile(ProfilePlotDialog *profileDlg,
                                                   const SWMMObjectRef &ref,
                                                   openswmmvis::plot::PlotAttribute attribute)
{
    if (!profileDlg) return;
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas()) return;

    SWMMResultsLayer *resultsLayer = pw->activeResultsLayer();
    if (!resultsLayer) {
        QMessageBox::information(profileDlg, tr("No active results layer"),
            tr("Pick a results layer in the Analysis toolbar's \"1D results\" "
               "selector, or run a simulation / add a SWMM Output (.out) layer."));
        return;
    }

    // Find-or-create an overlay CPD as a direct child of the profile dialog
    // so it floats above and is destroyed with it.
    auto *dlg = profileDlg->findChild<openswmmvis::ui::ComparisonPlotDialog *>(
        QString(), Qt::FindDirectChildrenOnly);
    if (!dlg) {
        dlg = new openswmmvis::ui::ComparisonPlotDialog(profileDlg);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        // Qt::Tool ⇒ utility window that stacks above its parent; reinforce
        // with WindowStaysOnTopHint because the profile dialog itself sets
        // that flag, so a plain Tool child can otherwise sink behind it on
        // some platforms.
        dlg->setWindowFlags(openswmmvis::ui::floatingPanelFlags()
                            | Qt::WindowTitleHint
                            | Qt::WindowSystemMenuHint
                            | Qt::WindowMinMaxButtonsHint
                            | Qt::WindowCloseButtonHint);
        connect(dlg, &openswmmvis::ui::ComparisonPlotDialog::addFromMapToggled,
                this, &SWMMVis::onAddFromMapToggled);
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
        // for the object kind.
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

void SWMMVis::initializePropertiesPanelDockWidget()
{
    // Property browser (single-object detail view) — right dock.
    mPropertiesPanel = new PropertiesPanel(this);
    mPropertiesPanel->setObjectName(QStringLiteral("dockWidgetPropertiesPanel"));
    addDockWidget(Qt::RightDockWidgetArea, mPropertiesPanel);

    // Attribute table (all objects, tabular grid) — bottom dock.
    mAttributeTablePanel = new AttributeTablePanel(this);
    auto *tableDock = new QDockWidget(tr("Attribute Table"), this);
    tableDock->setObjectName(QStringLiteral("dockWidgetAttributeTable"));
    tableDock->setWidget(mAttributeTablePanel);
    addDockWidget(Qt::BottomDockWidgetArea, tableDock);

    // Two-way sync between property browser and attribute table so an edit
    // in either view immediately reflects in the other without a full refresh.
    connect(mPropertiesPanel, &PropertiesPanel::objectEdited,
            mAttributeTablePanel, &AttributeTablePanel::onObjectEditedExternally);
    connect(mAttributeTablePanel, &AttributeTablePanel::objectEdited,
            mPropertiesPanel, &PropertiesPanel::onObjectEditedExternally);
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
    view->setColumnWidth(SimulationStatusModel::ColTwoDErr,      90);
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

    // 2026-06-04 — right-click copy (selected rows or the entire log).
    // The view's contextMenuPolicy is Qt::ActionsContextMenu (set in
    // swmmvis.ui), so actions added to the widget surface directly as
    // its context menu — no extra menu plumbing needed.
    auto *view = ui->treeViewMessageLogs;
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);

    // One tab-separated line per row (Time \t Type \t Message) —
    // pastes cleanly into spreadsheets and plain-text editors alike.
    const auto rowText = [this](int r) {
        QStringList cells;
        for (int c = 0; c < mLogMessagesModel->columnCount(); ++c)
            cells << mLogMessagesModel->index(r, c).data().toString();
        return cells.join(QLatin1Char('\t'));
    };

    auto *copySelected = new QAction(tr("Copy"), view);
    copySelected->setShortcut(QKeySequence::Copy);
    copySelected->setShortcutContext(Qt::WidgetShortcut);
    connect(copySelected, &QAction::triggered, this, [view, rowText]() {
        if (!view->selectionModel()) return;
        QModelIndexList rows = view->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(),
                  [](const QModelIndex &a, const QModelIndex &b) {
                      return a.row() < b.row();
                  });
        QStringList lines;
        for (const QModelIndex &idx : rows)
            lines << rowText(idx.row());
        if (!lines.isEmpty())
            QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    });
    view->addAction(copySelected);

    auto *copyAll = new QAction(tr("Copy All"), view);
    connect(copyAll, &QAction::triggered, this, [this, rowText]() {
        QStringList lines;
        lines << QStringLiteral("Time\tType\tMessage");
        for (int r = 0; r < mLogMessagesModel->rowCount(); ++r)
            lines << rowText(r);
        QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    });
    view->addAction(copyAll);
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
    // 2026-06-04 — Add Basemap duplicates the Add WMS/WCS flow; hidden
    // (not removed) so the action, dialog, and connect stay intact for
    // an easy re-enable if the flows diverge again.
    ui->actionAddBasemap->setVisible(false);
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
    connect(ui->actionUserFlags,     &QAction::triggered, this, &SWMMVis::onUserFlags);

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
        if (!overlay) {
            overlay = new openswmmvis::ui::LegendOverlay(c);
            // Slice BB Phase 8.6.16 — sync the toolbar action when the user
            // selects "Hide legend" from the overlay's right-click menu so
            // the action's checked state never drifts from the overlay's
            // visible state. Lambda captures `this` for the action lookup;
            // overlay is the signal source — no dangling reference risk.
            connect(overlay, &openswmmvis::ui::LegendOverlay::hideRequested,
                    this, [this]() {
                QSignalBlocker b(ui->actionShowLegend);
                ui->actionShowLegend->setChecked(false);
            });
        }
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
        // Slice S1 — toolbar/menu fallback also routes to LayerStyleDialog.
        openswmmvis::ui::LayerStyleDialog dlg(
            target, QString(), this,
            activeCanvas() ? activeCanvas()->undoStack() : nullptr);  // #36
        dlg.exec();
        if (auto *c = activeCanvas())
            c->invalidate(MapCanvas::Raster | MapCanvas::Scene,
                          QStringLiteral("layer-style-apply"));
    });

    // Tools → Set Project CRS… (added programmatically).
    if (ui->menuTools)
    {
        auto *actSetCRS = ui->menuTools->addAction(
            QIcon(QStringLiteral(":/swmmvis/Globe")),
            tr("Set Project CRS…"));
        actSetCRS->setToolTip(tr("Choose or change the project's coordinate reference system"));
        connect(actSetCRS, &QAction::triggered, this, &SWMMVis::onCRSButtonClicked);

        // Pick 2D Cells (and the new Trace Profile Path tool) now live on the
        // Mesh Editing toolbar — see initializeMeshEditingToolBar(). They are
        // no longer added to the Tools menu / Analysis toolbar.
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
                const char                  *icon;
            };
            static const ToolEntry kToolEntries[] = {
                {SWMMModelLayer::DataTimeSeries,  QT_TR_NOOP("New Time Series…"),  ":/swmmvis/AddTimeSeries"},
                {SWMMModelLayer::DataCurves,      QT_TR_NOOP("New Curve…"),        ":/swmmvis/AddCurve"},
                {SWMMModelLayer::DataPatterns,    QT_TR_NOOP("New Time Pattern…"), ":/swmmvis/AddPattern"},
                {SWMMModelLayer::DataControls,    QT_TR_NOOP("New Control Rule…"), ":/swmmvis/AddControlRule"},
                {SWMMModelLayer::DataTransects,   QT_TR_NOOP("New Transect…"),     ":/swmmvis/AddTransect"},
                {SWMMModelLayer::DataLIDControls, QT_TR_NOOP("New LID Control…"),  ":/swmmvis/Layers"},
                {SWMMModelLayer::DataPollutants,  QT_TR_NOOP("New Pollutant…"),    ":/swmmvis/Layers"},
            };
            for (const auto &e : kToolEntries) {
                auto *act = editBar->addAction(
                    QIcon(QString::fromLatin1(e.icon)),
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

    // Tools → Style Manager… (Slice Z.17c). Browse the per-user style
    // library, apply Rule Lists to the currently-selected layer, and
    // import / export styles for sharing between projects. The library
    // lives under QStandardPaths::AppLocalDataLocation/styles.
    if (ui->actionStyleManager)
    {
        connect(ui->actionStyleManager, &QAction::triggered, this, [this]() {
            OpenSWMMVisLayer *target =
                mLayerTreePanel ? mLayerTreePanel->selectedLayer() : nullptr;
            openswmmvis::ui::StyleManagerDialog dlg(target, this);
            dlg.exec();
        });
    }

    // View → Layer Styling Dock (Slice Z.18). Two-way binding between
    // the checkable action and the dock's visibility: clicking the
    // action toggles the dock, and the user closing the dock via its
    // titlebar X reflects back into the action's checked state. Also
    // wire the layer-tree's selection → dock.setLayer so the editor
    // always shows the active layer.
    if (ui->actionLayerStylingDock && mLayerStylingDock)
    {
        ui->actionLayerStylingDock->setChecked(mLayerStylingDock->isVisible());
        connect(ui->actionLayerStylingDock, &QAction::toggled, this,
                [this](bool on) {
                    if (!mLayerStylingDock) return;
                    mLayerStylingDock->setVisible(on);
                    if (on && mLayerTreePanel)
                        mLayerStylingDock->setLayer(
                            mLayerTreePanel->selectedLayer());
                });
        connect(mLayerStylingDock, &QDockWidget::visibilityChanged, this,
                [this](bool visible) {
                    if (!ui->actionLayerStylingDock) return;
                    QSignalBlocker b(ui->actionLayerStylingDock);
                    ui->actionLayerStylingDock->setChecked(visible);
                });
        if (mLayerTreePanel) {
            connect(mLayerTreePanel, &LayerTreePanel::layerSelected,
                    this, [this](OpenSWMMVisLayer *layer) {
                        if (mLayerStylingDock && mLayerStylingDock->isVisible())
                            mLayerStylingDock->setLayer(layer);
                    });
        }
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
    if (ui->actionAddText)
        connect(ui->actionAddText, &QAction::triggered, this, [this]() {
            if (auto *pw = activeProjectWindow()) pw->activateAddTextTool();
        });

    // Climatology buttons → tabbed Climatology dialog (Temperature/Evaporation/
    // Wind/Snow Melt/Areal Depletion/Adjustments). Solar Radiation has no SWMM
    // input section; it opens the dialog on the Evaporation tab (solar feeds
    // Hargreaves ET).
    if (ui->actionTemperature)
        connect(ui->actionTemperature, &QAction::triggered, this, [this]() {
            onClimatology(ClimatologyDialog::TabTemperature);
        });
    if (ui->actionEvaporation)
        connect(ui->actionEvaporation, &QAction::triggered, this, [this]() {
            onClimatology(ClimatologyDialog::TabEvaporation);
        });
    if (ui->actionWind)
        connect(ui->actionWind, &QAction::triggered, this, [this]() {
            onClimatology(ClimatologyDialog::TabWind);
        });
    if (ui->actionSnow)
        connect(ui->actionSnow, &QAction::triggered, this, [this]() {
            onClimatology(ClimatologyDialog::TabSnowMelt);
        });
    if (ui->actionSolarRadiation)
        connect(ui->actionSolarRadiation, &QAction::triggered, this, [this]() {
            onClimatology(ClimatologyDialog::TabEvaporation);
        });

    // Toolbar quick-wins (Phase 2).
    if (ui->actionSearch)
        connect(ui->actionSearch, &QAction::triggered, this, &SWMMVis::onSearch);
    if (ui->actionTabularView)
        connect(ui->actionTabularView, &QAction::triggered, this, &SWMMVis::onTabularView);
    if (ui->actionAddDelimeteredData)
        connect(ui->actionAddDelimeteredData, &QAction::triggered, this,
                &SWMMVis::onAddDelimitedData);
    if (ui->actionSummarizeResults)
        connect(ui->actionSummarizeResults, &QAction::triggered, this,
                &SWMMVis::onSummarizeResults);
    if (ui->actionCopy)
        connect(ui->actionCopy, &QAction::triggered, this, &SWMMVis::onCopyActiveView);
    if (ui->actionPrint)
        connect(ui->actionPrint, &QAction::triggered, this, &SWMMVis::onPrintActiveView);
    if (ui->actionInvertSelection)
        connect(ui->actionInvertSelection, &QAction::triggered, this,
                &SWMMVis::onInvertSelection);

    // Network analysis (Phase 3). Mass balance lives in the status report
    // (legacy parity); flow balance / travel time analyze the up/down subnet.
    if (ui->actionShowMassBalance)
        connect(ui->actionShowMassBalance, &QAction::triggered, this, &SWMMVis::onShowReport);
    if (ui->actionFlowBalanceUpstream)
        connect(ui->actionFlowBalanceUpstream, &QAction::triggered, this,
                [this]() { onFlowBalance(/*upstream=*/true); });
    if (ui->actionFlowBalanceDownstream)
        connect(ui->actionFlowBalanceDownstream, &QAction::triggered, this,
                [this]() { onFlowBalance(/*upstream=*/false); });
    if (ui->actionTravelTimeUpstream)
        connect(ui->actionTravelTimeUpstream, &QAction::triggered, this,
                [this]() { onTravelTime(/*upstream=*/true); });
    if (ui->actionTravelTimeDownstream)
        connect(ui->actionTravelTimeDownstream, &QAction::triggered, this,
                [this]() { onTravelTime(/*upstream=*/false); });

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
    // Gracefully close every open top-level dialog so their own closeEvent
    // runs (geometry/state persistence) before teardown. This is required
    // for the application to actually quit: QApplication::quitOnLastWindow
    // Closed (default true) only fires once the last *visible* top-level
    // window closes, and our dialogs carry Qt::Window — so any left open
    // (profile plots, editors, property dialogs, …) keep the otherwise
    // windowless process alive and the Dock/genie icon active. Even
    // dialogs parented to a project sub-window appear in topLevelWidgets()
    // and must be closed here; the Qt object tree would only destroy them
    // at app exit, which never arrives while they hold the app open.
    // Snapshot the list first since close() may schedule deletions
    // (WA_DeleteOnClose) that mutate the live widget list.
    const QList<QWidget *> topLevels = QApplication::topLevelWidgets();
    for (QWidget *top : topLevels) {
        if (top == this) continue;
        if (auto *dlg = qobject_cast<QDialog *>(top))
            dlg->close();
    }

    // Cancel any in-flight simulation jobs before we let the window close.
    // Each runner executes on a QtConcurrent (global QThreadPool) thread that
    // blocks in its step / legacy-worker loop until the run completes or it
    // observes the cancel flag. If we close with jobs still running, the event
    // loop ends and QApplication teardown calls QThreadPool::waitForDone(),
    // which keeps the (now windowless) process alive in the Dock until the
    // simulation finishes on its own — looking like a hang. Cancelling lets
    // each worker flush partial output, kill its legacy-worker subprocess, and
    // exit promptly so teardown returns immediately. Un-pause first: a paused
    // step loop parks in a sleep and would never observe the cancel otherwise
    // (mirrors the Stop action).
    for (SimulationRunner *runner : std::as_const(mActiveRunners)) {
        runner->setPaused(false);
        runner->cancel();
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
            // Eat the close event and hide the sub-window in place; the
            // widget and its content are fully preserved for reuse. The
            // tab must be hidden explicitly — QMdiArea keeps tabs of
            // hidden sub-windows (verified Qt 6.9.3: tabs are only
            // removed when a sub leaves the area).
            QMdiSubWindow *next = nullptr;
            for (QMdiSubWindow *other : ui->mdiAreaCentral->subWindowList())
            {
                if (other == sub) continue;
                if (other->isVisible()) { next = other; break; }
            }
            sub->hide();
            setSubWindowTabVisible(ui->mdiAreaCentral, sub, false);
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

    // Slice RB.3 — .oswp sidecar is now created inside
    // SWMMVisProjectWindow::saveAs (Slice RB.1) so every save path —
    // including auto-save-before-run — keeps the sidecar in sync. The
    // previous duplicate write that lived here has been removed.
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

    // Slice RA.2 — keep the dialog's default suffix in lockstep with the
    // currently-selected filter. macOS's native dialog will otherwise
    // silently append the *first* filter's extension when the user
    // switches filters mid-flow, producing stacks like `model.inp.oswp`.
    // The normalizer in RA.1 is the correction layer; this is prevention.
    auto applyDefaultSuffixForFilter = [&dlg](const QString &filter) {
        static const QRegularExpression re(QStringLiteral(R"(\*\.([A-Za-z0-9]+))"));
        const auto m = re.match(filter);
        if (m.hasMatch()) dlg.setDefaultSuffix(m.captured(1));
    };
    connect(&dlg, &QFileDialog::filterSelected,
            &dlg, applyDefaultSuffixForFilter);

    // Slice RA.3 — restore the user's last Save-As filter for this project
    // so the dialog opens on the format-of-record (per-project memory under
    // SWMMVis/Project/<inpPath>/LastSaveAsFilter). Falls back to the first
    // filter when no preference exists; in that case the default suffix is
    // seeded from `.inp` so existing behaviour is preserved.
    const QString prefBase =
        suggested.isEmpty()
            ? QString()
            : QStringLiteral("SWMMVis/Project/%1/LastSaveAsFilter").arg(suggested);
    {
        QString restoredFilter;
        if (!prefBase.isEmpty()) {
            QSettings s;
            restoredFilter = s.value(prefBase).toString();
        }
        if (!restoredFilter.isEmpty()) {
            dlg.selectNameFilter(restoredFilter);
            applyDefaultSuffixForFilter(restoredFilter);
        } else {
            dlg.setDefaultSuffix(QStringLiteral("inp"));
        }
    }

    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString rawPath = dlg.selectedFiles().value(0);
    if (rawPath.isEmpty())
        return;

    // Slice RA.3 — persist the chosen filter for next time.
    if (!prefBase.isEmpty()) {
        QSettings s;
        s.setValue(prefBase, dlg.selectedNameFilter());
    }

    // Slice RA.1 / RA.4 — collapse any stacked writable extensions
    // (`model.inp.oswp`, `model.inp.inp`, etc.) and compute the canonical
    // inpPath + isProject flag in one place. The normalizer is a pure
    // function over the dialog string + the writable-extensions set, so
    // tests/gui/test_saveaspathnormalizer.cpp can exercise every edge case
    // without spinning up the dialog.
    QSet<QString> writableExts;
    for (const auto &entry :
            kFilters->entriesFor(openswmmvis::FilterKind::InputRead)) {
        if (!entry.enabled || !entry.canWrite) continue;
        for (const QString &pat : entry.patterns) {
            QString patExt = pat;
            if (patExt.startsWith(QStringLiteral("*.")))
                patExt = patExt.mid(2);
            writableExts.insert(patExt.toLower());
        }
    }
    for (const auto &entry :
            kFilters->entriesFor(openswmmvis::FilterKind::ProjectWrite)) {
        if (!entry.enabled) continue;
        for (const QString &pat : entry.patterns) {
            QString patExt = pat;
            if (patExt.startsWith(QStringLiteral("*.")))
                patExt = patExt.mid(2);
            writableExts.insert(patExt.toLower());
        }
    }
    const auto normalized =
        openswmmvis::normalizeSaveAsPath(rawPath, writableExts);
    if (normalized.wasNormalized) {
        onLogMessage(tr("Save As: collapsed duplicate extensions in "
                        "\"%1\" → \"%2\"")
                         .arg(QFileInfo(rawPath).fileName(),
                              QFileInfo(normalized.inpPath).fileName()),
                     OpenSWMMVisLogMessage::LogMessageType::Information);
    }

    // Reconstruct `path` (the dialog-canonical user choice, post-normalize)
    // and `ext` (the user-intended writable kind) for the existing sidecar
    // logic below. For a .oswp save, `path` is `<stem>.oswp` next to the
    // .inp that the engine will actually write.
    const QString path = normalized.isProject
        ? (QFileInfo(normalized.inpPath).absolutePath()
             + QLatin1Char('/')
             + QFileInfo(normalized.inpPath).completeBaseName()
             + QStringLiteral(".oswp"))
        : normalized.inpPath;
    const QString ext = QFileInfo(path).suffix().toLower();
    const bool isProject = normalized.isProject;
    const QString inpPath = normalized.inpPath;

    // Slice IO-12 — pre-flight portability check before the engine writer
    // runs. The engine writer (Slice IO-4) is the authoritative rebase
    // pass; this call gives the GUI an *advance preview* of any
    // cross-volume / missing-file warnings so the user sees them in the
    // log panel alongside the save-success line, instead of having to
    // peek inside the saved file to discover surprises. Non-blocking:
    // warnings are surfaced, but the save proceeds regardless.
    if (pw->modelLayer() && pw->modelLayer()->engine()) {
        SWMM_Engine eng = pw->modelLayer()->engine();
        openswmmvis::project::PreflightResult pf;
        if (ext == QStringLiteral("gpkg")) {
            pf = openswmmvis::project::IoPortabilityNormalizer
                    ::preflightGpkgSave(eng, inpPath);
        } else {
            pf = openswmmvis::project::IoPortabilityNormalizer
                    ::preflightInpSave(eng, inpPath);
        }
        for (const QString &w : pf.warnings) {
            onLogMessage(tr("Portability check: %1").arg(w),
                          OpenSWMMVisLogMessage::Warning);
        }
    }

    QString err;
    if (!pw->saveAs(inpPath, &err)) {
        QMessageBox::critical(this, tr("Save As failed"), err);
        return;
    }

    // Slice RB.3 — .oswp sidecar is now created inside
    // SWMMVisProjectWindow::saveAs (Slice RB.1) for every successful
    // built-in (.inp) write. Plugin-backed export formats (.gpkg, …)
    // are standalone — saveAs skips the sidecar when pluginId is
    // non-empty. `ext` and `isProject` retained for the recent-files
    // / log-message logic below.
    (void)ext;
    (void)isProject;

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
    // Hide-on-close preserves all widget content; restore = show the sub
    // AND its tab (tab visibility is managed by hand — see eventFilter).
    if (QMdiSubWindow *sub = welcomeSubWindow())
    {
        setSubWindowTabVisible(ui->mdiAreaCentral, sub, true);
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

    // Closing the project window invalidates any progress / status rows
    // that referenced it. aboutToClose() fires before Qt teardown, so the
    // canvas + layers are still alive — but we don't need them; we just
    // drop the per-job state keyed on this window.
    connect(window, &SWMMVisProjectWindow::aboutToClose, this,
            [this, window]() { clearSimulationStatusForProject(window); });

    // Same treatment when the user removes the output layer itself — the
    // status row reports continuity errors / sim dates pinned to that
    // .out, so they go stale the moment it leaves the canvas. Bind the
    // connection to the project window as the receiver context so it is
    // auto-disconnected when the window is destroyed (avoids a layered
    // teardown-time emit reaching a dangling lambda capture).
    connect(window->canvas(), &MapCanvas::layerRemoved, window,
            [this, window](OpenSWMMVisLayer *layer) {
                if (qobject_cast<SWMMResultsLayer *>(layer))
                    clearSimulationStatusForProject(window);
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
        // Slice RB.4 — when no sibling .oswp exists, optionally create
        // one capturing the canvas's current default state so the user
        // sees the project file in their file manager immediately on
        // open. Gated by Preferences/AutoCreateOswpOnOpen (default true).
        // The lazy-create path (first save → SWMMVisProjectWindow::saveAs)
        // remains the fallback if the preference is disabled.
        else if (!oswp.isEmpty()) {
            QSettings s;
            const bool autoCreate =
                s.value(QStringLiteral("Preferences/AutoCreateOswpOnOpen"),
                        true).toBool();
            if (autoCreate) {
                QString sidecarErr;
                if (ProjectSerializer::saveToFile(oswp, window, &sidecarErr)) {
                    onLogMessage(tr("Created sibling project file: %1")
                                     .arg(QFileInfo(oswp).fileName()),
                                 OpenSWMMVisLogMessage::LogMessageType::Information);
                } else {
                    onLogMessage(tr("Sidecar auto-create failed: %1")
                                     .arg(sidecarErr),
                                 OpenSWMMVisLogMessage::LogMessageType::Warning);
                }
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
                    // Make the auto-loaded run this tab's active 1D results
                    // layer (default-only) so every analysis tool + the combo
                    // target it; also drive the transport for immediate scrub.
                    if (!window->activeResultsLayer())
                        window->setActiveResultsLayer(rl);
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
            if (!meshRead.warning.isEmpty()) {
                onLogMessage(meshRead.warning,
                             OpenSWMMVisLogMessage::LogMessageType::Warning);
            }
            if (meshRead.hasMesh) {
                // Mesh XY are in project-CRS units (no conversion needed):
                // the engine multiplies by 0.3048 itself in
                // SurfaceRouter2D::initialize when SWMM FLOW_UNITS is US.
                auto *meshLayer = new SWMM2DMeshLayer(meshRead.mesh, meshRead.sourcePath);
                meshLayer->setActiveMesh(meshRead.isExternal);
                // Slice §V.VD.1 — preload any parsed [2D_BOUNDARY_CONDITIONS]
                // into the layer's BC SoA so the toolbar / Property Browser
                // see persisted edits on reload.
                if (!meshRead.edgeBCs.isEmpty()) {
                    auto &bcs = meshLayer->edgeBCsMutable();
                    if (bcs.size() == meshRead.edgeBCs.size())
                        bcs = meshRead.edgeBCs;
                }
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
                        // Outputs inherit the model's CRS so the
                        // Properties window shows a real CRS and any
                        // reprojection logic matches the input.
                        if (window->modelLayer() && window->modelLayer()->srs())
                            resLayer->setSRS(
                                new SpatialReferenceSystem(*window->modelLayer()->srs(),
                                                           resLayer),
                                /*ownsSRS=*/true);
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

                        // Honour the model's DRY_DEPTH so the GUI render
                        // threshold (cells AND velocity/flow vectors) matches
                        // what the solver considers wet — without this, the GUI
                        // default clips shallow runs invisibly. Prefer the live
                        // engine handle; when none is attached (opened a model
                        // without running), read [2D_OPTIONS] DRY_DEPTH straight
                        // from the .inp. Last resort: a small data-derived floor.
                        double engineDry = 0.0;
                        const double inpDry =
                            SimulationRunner::parseTwoDOption(
                                filePath, QStringLiteral("DRY_DEPTH")).toDouble();
                        if (window->modelLayer() && window->modelLayer()->engine() &&
                            swmm_2d_get_dry_depth(window->modelLayer()->engine(),
                                                   &engineDry) == SWMM_OK &&
                            engineDry > 0.0)
                        {
                            resLayer->setDryDepth(engineDry);
                        } else if (inpDry > 0.0) {
                            resLayer->setDryDepth(inpDry);
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

                        // Make this the tab's active 2D results layer
                        // (default-only) so the 2D analysis combo + cell-pick
                        // tool + mesh profile target it.
                        if (!window->active2DResultsLayer())
                            window->setActive2DResultsLayer(resLayer);

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
        {
            // .oswp stores layer paths relative to the project file. Resolve
            // against the .oswp directory before opening: a relative path
            // passed through makes the ENGINE resolve the model's own
            // relative references (rain FILE gages, mesh sidecars, hotstart
            // files) against the process working directory instead of the
            // model directory — silently loading nothing.
            inpPath = ProjectSerializer::resolveStoredPath(inpPath, oswpPath);
            openSingleINP(inpPath);
        }
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
        if (mLegendDock)            mLegendDock->setCanvas(nullptr);
        if (mObjectBrowserPanel)    mObjectBrowserPanel->setProject(nullptr, nullptr, nullptr);
        if (mPropertiesPanel)      { mPropertiesPanel->setProject(nullptr); mPropertiesPanel->clear(); }
        if (mAttributeTablePanel)   mAttributeTablePanel->setProject(nullptr, nullptr, nullptr);
        if (mTerrainToolbar)        mTerrainToolbar->rebindCanvas(nullptr);
        if (mMeshEditingToolbar) {
            mMeshEditingToolbar->rebindSelectionManager(nullptr);
            mMeshEditingToolbar->rebindCanvas(nullptr);
        }
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

    // Animation toolbar + analysis tools follow the active project tab. Each
    // tab keeps its own ACTIVE 1D / 2D results layer (the user's choice from
    // the Analysis-toolbar combos or the layer-tree action). On switch we adopt
    // this tab's stored choice rather than re-guessing "first results layer"
    // (which used to silently overwrite the user's selection). If the tab has
    // no active layer yet, default to the first one found — default only, so
    // an explicit choice on another tab is never stolen.
    {
        if (!pw->activeResultsLayer() && pw->canvas()) {
            for (OpenSWMMVisLayer *l : pw->canvas()->layers())
                if (auto *rl = qobject_cast<SWMMResultsLayer *>(l)) {
                    pw->setActiveResultsLayer(rl);
                    break;
                }
        }
        if (!pw->active2DResultsLayer() && pw->canvas()) {
            for (OpenSWMMVisLayer *l : pw->canvas()->layers())
                if (auto *r2 = qobject_cast<SWMM2DResultsLayer *>(l)) {
                    pw->setActive2DResultsLayer(r2);
                    break;
                }
        }
        if (mAnimationController) {
            mAnimationController->setPrimaryLayer(pw->activeResultsLayer());
            mAnimationController->setFallback2DLayer(pw->active2DResultsLayer());
        }
        if (mLayerTreePanel) {
            mLayerTreePanel->setActiveResultsLayer(pw->activeResultsLayer());
            mLayerTreePanel->setActive2DResultsLayer(pw->active2DResultsLayer());
        }
    }

    // Keep the transport, the Analysis-toolbar combos, and the layer-tree
    // check-state in sync when this tab's active results layer changes. Lambdas
    // can't use Qt::UniqueConnection, so disconnect prior handlers first.
    QObject::disconnect(pw, &SWMMVisProjectWindow::activeResultsLayerChanged,
                        this, nullptr);
    connect(pw, &SWMMVisProjectWindow::activeResultsLayerChanged, this,
            [this, pw](SWMMResultsLayer *layer) {
                if (pw != mActiveProjectWindow) return;
                if (mAnimationController) mAnimationController->setPrimaryLayer(layer);
                if (mLayerTreePanel) mLayerTreePanel->setActiveResultsLayer(layer);
                refreshActiveResultsCombos();
            });
    QObject::disconnect(pw, &SWMMVisProjectWindow::active2DResultsLayerChanged,
                        this, nullptr);
    connect(pw, &SWMMVisProjectWindow::active2DResultsLayerChanged, this,
            [this, pw](SWMM2DResultsLayer *layer) {
                if (pw != mActiveProjectWindow) return;
                if (mAnimationController) mAnimationController->setFallback2DLayer(layer);
                if (mLayerTreePanel) mLayerTreePanel->setActive2DResultsLayer(layer);
                refreshActiveResultsCombos();
            });

    // Loading / closing a 1D results layer changes the available choices and
    // may need an auto-default (first run becomes active; a vanished active
    // layer falls back to another or to null via QPointer).
    if (auto *reg = pw->statsRegistry()) {
        QObject::disconnect(reg, &openswmmvis::OutputStatsRegistry::identitiesChanged,
                            this, nullptr);
        connect(reg, &openswmmvis::OutputStatsRegistry::identitiesChanged, this,
                [this, pw]() {
                    if (pw != mActiveProjectWindow) return;
                    if (!pw->activeResultsLayer()) {
                        const auto ids = pw->statsRegistry()->identities();
                        if (!ids.isEmpty() && ids.first().layer)
                            pw->setActiveResultsLayer(ids.first().layer);
                    }
                    refreshActiveResultsCombos();
                });
    }

    // The 2D results layers aren't tracked by the OutputStatsRegistry, so the
    // 2D combo must refresh when canvas layers are added/removed. (The 1D combo
    // also benefits — a removed .out drops out of the list.)
    if (pw->canvas()) {
        // Lambdas can't use Qt::UniqueConnection — drop prior handlers first so
        // repeated activation of the same tab doesn't stack duplicates.
        QObject::disconnect(pw->canvas(), &MapCanvas::layerAdded, this, nullptr);
        QObject::disconnect(pw->canvas(), &MapCanvas::layerRemoved, this, nullptr);
        connect(pw->canvas(), &MapCanvas::layerAdded, this,
                [this, pw](OpenSWMMVisLayer *) {
                    if (pw == mActiveProjectWindow) refreshActiveResultsCombos();
                });
        connect(pw->canvas(), &MapCanvas::layerRemoved, this,
                [this, pw](OpenSWMMVisLayer *) {
                    if (pw == mActiveProjectWindow) refreshActiveResultsCombos();
                });
    }

    // Repopulate the combos for the newly active tab.
    refreshActiveResultsCombos();

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
                // Block each action's signals: this lambda only MIRRORS the
                // active tool into the toolbar's checked state. The actions'
                // toggled handlers re-activate a tool (see actPick2DCells /
                // Select handlers in initializeMenus), so an unguarded
                // setChecked() here emits toggled → activates a tool →
                // emits activeToolChanged → re-enters this lambda → recurses
                // until the stack overflows (crash loading 2D models, which
                // activate the Pick-2D-Cells tool on load).
                for (const QString &name : keys.values()) {
                    if (auto *act = findChild<QAction *>(name)) {
                        QSignalBlocker b(act);
                        act->setChecked(false);
                    }
                }
                if (tool) {
                    const QString name = keys.value(tool);
                    if (!name.isEmpty())
                        if (auto *act = findChild<QAction *>(name)) {
                            QSignalBlocker b(act);
                            act->setChecked(true);
                        }
                }
            });

    // Sync immediately to the current active tool of the newly focused window.
    {
        const QHash<OpenSWMMVisMapTool *, QString> keys = pw->toolActionKeys();
        // Block signals while mirroring state — same reentrancy hazard as the
        // activeToolChanged lambda above (setChecked → toggled → re-activate).
        for (const QString &name : keys.values())
            if (auto *act = findChild<QAction *>(name)) {
                QSignalBlocker b(act);
                act->setChecked(false);
            }
        if (OpenSWMMVisMapTool *cur = pw->canvas()->activeTool()) {
            const QString name = keys.value(cur);
            if (!name.isEmpty())
                if (auto *act = findChild<QAction *>(name)) {
                    QSignalBlocker b(act);
                    act->setChecked(true);
                }
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
        updateOffsetModeLabels(elev);
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
    // Qt 6 asserts on Qt::UniqueConnection with non-PMF slots; the
    // disconnect above already de-dupes.
    connect(pw, &SWMMVisProjectWindow::editSessionChanged, this,
            [this, pw](bool active) {
                if (pw != mActiveProjectWindow) return;
                QSignalBlocker b(ui->actionEditExisting);
                ui->actionEditExisting->setChecked(active);
                applyEditSessionToActions(active);
            });
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
        // Two-level submenu — fires when the user picks a specific results
        // layer in addition to the variable (≥2 .out layers loaded).
        connect(st, &OpenSWMMVisMapToolSelect::plotAttributeForLayerRequested,
                this, &SWMMVis::openComparisonPlotForAttributeOnLayer,
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
        // Qt 6 asserts on Qt::UniqueConnection with non-PMF slots; disconnect
        // first so repeated tab-switches don't stack duplicate handlers.
        QObject::disconnect(pt, &OpenSWMMVisMapToolSelectProfile::statusMessageChanged,
                            statusBar(), nullptr);
        connect(pt, &OpenSWMMVisMapToolSelectProfile::statusMessageChanged,
                statusBar(), [this](const QString &msg) {
                    statusBar()->showMessage(msg, 5000);
                });
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

    // Trace Profile Path tool: project window forwards the finished polyline.
    // US.A1 — the mesh-toolbar tool draws a BED-ONLY profile; the analysis
    // tool draws ground + animated depth + envelope.
    connect(pw, &SWMMVisProjectWindow::meshProfileTraced,
            this, &SWMMVis::openMeshBedProfilePlotFor,
            Qt::UniqueConnection);
    connect(pw, &SWMMVisProjectWindow::analysisMeshProfileTraced,
            this, &SWMMVis::openMeshProfilePlotFor,
            Qt::UniqueConnection);

    // Mesh edge-select tool: right-click → plot the edge's flux time series.
    connect(pw, &SWMMVisProjectWindow::meshEdgeFluxRequested,
            this, &SWMMVis::openMeshEdgeFluxPlotFor,
            Qt::UniqueConnection);

    // Mesh vertex-select tool: right-click → plot interpolated depth/HGL.
    connect(pw, &SWMMVisProjectWindow::meshVertexSeriesRequested,
            this, &SWMMVis::openMeshVertexSeriesFor,
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
                        updateOffsetModeLabels(elev);
                    }
                });
    }

    // Rebind the Layers dock to this project's canvas so visibility toggles,
    // ordering, and layer additions reflect the focused tab.
    if (mLayerTreePanel)
        mLayerTreePanel->setCanvas(pw->canvas());

    if (mLegendDock)
        mLegendDock->setCanvas(pw->canvas());

    // Rebind the Object Browser + Attribute Panel to this project's model
    // layer + selection bus + canvas (canvas powers Slice O's zoom-to-object).
    if (mObjectBrowserPanel)
        mObjectBrowserPanel->setProject(pw->modelLayer(),
                                        pw->selectionManager(),
                                        pw->canvas());

    // Bind the property browser to the engine layer so typed adapters
    // (SWMMJunctionPropertyAdapter, etc.) can be constructed on identify.
    if (mPropertiesPanel)
        mPropertiesPanel->setProject(pw->modelLayer());

    // Rebind the Attribute Table to this project.
    if (mAttributeTablePanel)
        mAttributeTablePanel->setProject(pw->modelLayer(),
                                         pw->selectionManager(),
                                         pw->canvas());
    if (mPropertiesPanel && pw->selectionManager())
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
                    if (!mPropertiesPanel || pw != mActiveProjectWindow) return;
                    if (current.isEmpty())
                    {
                        mPropertiesPanel->clear();
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
                        mPropertiesPanel->showDataObject(
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
                        mPropertiesPanel->showIdentifyResults({r});
                    }
                    else
                    {
                        mPropertiesPanel->clear();
                    }
                });
    }

    // Slice §V.VB — rebind the Mesh Editing toolbar to the new project's
    // canvas + selection manager. Like TerrainToolbar, this happens on
    // every project-window switch so the toolbar's combo / selection
    // probe always reflect the visible project.
    if (mMeshEditingToolbar) {
        mMeshEditingToolbar->rebindCanvas(pw->canvas());
        mMeshEditingToolbar->rebindSelectionManager(pw->selectionManager());
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

void SWMMVis::clearSimulationStatusForProject(SWMMVisProjectWindow *pw)
{
    if (!pw || !mSimStatusModel) return;

    const QList<int> removed = mSimStatusModel->clearJobsForModel(pw);
    if (removed.isEmpty()) return;

    for (int jobId : removed) {
        // Cancel still-running runners — the user has signaled (by closing
        // the project or removing its output) that the run's results are
        // no longer wanted. The finished() lambda still fires later but
        // its model/progress-map writes no-op against the cleared state.
        if (auto *runner = mActiveRunners.take(jobId))
            runner->cancel();
        mRunningSimProgress.remove(jobId);
        mSimulationStarts.remove(jobId);
        mActive2DResultsLayers.remove(jobId);
    }

    updateSimulationProgressBar();

    // Mirror the finished-handler logic: when the last runner is gone,
    // drop Pause/Cancel back to their default-disabled state.
    if (mActiveRunners.isEmpty()) {
        if (ui->actionPauseExecution->isChecked()) {
            QSignalBlocker b(ui->actionPauseExecution);
            ui->actionPauseExecution->setChecked(false);
        }
        ui->actionPauseExecution->setEnabled(false);
        ui->actionCancelExecution->setEnabled(false);
    }
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

void SWMMVis::onClimatology(int tab)
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine())
    {
        onLogMessage(tr("Open a SWMM project first to edit climatology."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }

    ClimatologyDialog dlg(pw->modelLayer()->engine(), pw->modelLayer(), this);
    dlg.setCurrentTab(tab);
    if (dlg.exec() == QDialog::Accepted && dlg.wroteAnyChanges())
        pw->setHasChanges(true);
}

// ── Toolbar quick-wins (Phase 2) ────────────────────────────────────────────

namespace {
//! Show + raise the QDockWidget ancestor of \p w (if any).
void raiseDockAncestor(QWidget *w)
{
    for (QWidget *p = w; p; p = p->parentWidget())
        if (auto *d = qobject_cast<QDockWidget *>(p)) { d->show(); d->raise(); return; }
}
} // namespace

void SWMMVis::onSearch()
{
    if (!mObjectBrowserPanel) return;
    raiseDockAncestor(mObjectBrowserPanel);
    mObjectBrowserPanel->focusSearch();
}

void SWMMVis::onTabularView()
{
    if (!mAttributeTablePanel) return;
    raiseDockAncestor(mAttributeTablePanel);
    mAttributeTablePanel->setFocus(Qt::ShortcutFocusReason);
}

void SWMMVis::onAddDelimitedData()
{
    MapCanvas *c = activeCanvas();
    if (!c)
    {
        onLogMessage(tr("Open a project first to add delimited data."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add Delimited Data"),
        mRecentFiles.isEmpty() ? QDir::homePath()
                               : QFileInfo(mRecentFiles.first()).absolutePath(),
        tr("Delimited text (*.csv *.tsv *.txt);;All files (*)"));
    if (path.isEmpty()) return;

    auto *layer = new TabularDataLayer(QFileInfo(path).fileName());
    QString err;
    if (!layer->loadFromFile(path, &err))
    {
        delete layer;
        QMessageBox::warning(this, tr("Add Delimited Data"),
            tr("Could not load %1:\n%2").arg(QFileInfo(path).fileName(), err));
        return;
    }
    c->addLayer(layer, true);
    c->zoomToFullExtent();
    onLogMessage(tr("Added delimited data: %1").arg(QFileInfo(path).fileName()));
}

void SWMMVis::onSummarizeResults()
{
    auto *pw = activeProjectWindow();
    if (!pw)
    {
        onLogMessage(tr("Summarize Results: open a project first."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }
    auto *layer = pw->activeResultsLayer();
    if (!layer)
    {
        QMessageBox::information(this, tr("No Results"),
            tr("Run a simulation or load a results (.out) file first."));
        return;
    }

    auto *dlg = new openswmmvis::ui::StatisticsDashboardDialog(layer, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void SWMMVis::onCopyActiveView()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas())
    {
        onLogMessage(tr("Copy: no active map view."), OpenSWMMVisLogMessage::Warning);
        return;
    }
    QGuiApplication::clipboard()->setPixmap(pw->canvas()->grab());
    onLogMessage(tr("Copied map view to clipboard."));
}

void SWMMVis::onPrintActiveView()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->canvas())
    {
        onLogMessage(tr("Print: no active map view."), OpenSWMMVisLogMessage::Warning);
        return;
    }
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QPixmap pm = pw->canvas()->grab();
    QPainter painter(&printer);
    const QRect vp = painter.viewport();
    const QPixmap scaled = pm.scaled(vp.size(), Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
    painter.drawPixmap(vp.topLeft(), scaled);
    onLogMessage(tr("Printed map view."));
}

void SWMMVis::onInvertSelection()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine() || !pw->selectionManager())
    {
        onLogMessage(tr("Invert Selection: open a project first."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }
    SWMM_Engine e = pw->modelLayer()->engine();

    QSet<SWMMObjectRef> all;
    const auto addAll = [&](int count,
                            const char *(*idFn)(SWMM_Engine, int),
                            SWMMObjectRef::ObjectType t) {
        for (int i = 0; i < count; ++i)
        {
            const char *id = idFn(e, i);
            if (id && *id) all.insert(SWMMObjectRef(t, QString::fromUtf8(id)));
        }
    };
    addAll(swmm_node_count(e),     swmm_node_id,     SWMMObjectRef::Node);
    addAll(swmm_link_count(e),     swmm_link_id,     SWMMObjectRef::Link);
    addAll(swmm_subcatch_count(e), swmm_subcatch_id, SWMMObjectRef::Subcatchment);
    addAll(swmm_gage_count(e),     swmm_gage_id,     SWMMObjectRef::RainGage);

    const QSet<SWMMObjectRef> cur = pw->selectionManager()->selection();
    QSet<SWMMObjectRef> inv;
    inv.reserve(all.size());
    for (const auto &r : all)
        if (!cur.contains(r)) inv.insert(r);

    pw->selectionManager()->select(inv, SelectionManager::Replace);
    onLogMessage(tr("Inverted selection (%1 object(s) now selected).").arg(inv.size()));
}

// ── Network analysis (Phase 3) ──────────────────────────────────────────────

namespace {

//! Up/down-stream subnetwork of a set of seed nodes, found by BFS over the
//! routing graph (forward edges for downstream, reversed for upstream).
struct Subnetwork
{
    bool                       ok = false;
    QSet<int>                  nodes;          // engine node indices reached
    QVector<int>               interiorLinks;  // engine link idx, both ends inside
    QVector<QPair<int, bool>>  boundaryLinks;  // (link idx, true=flow enters subnet)
};

Subnetwork buildSubnetwork(SWMMModelLayer *model, const QSet<int> &seeds, bool upstream)
{
    Subnetwork s;
    if (!model || seeds.isEmpty()) return s;
    const ProfileRouter::Graph g = ProfileNetworkAdapter::buildGraphFromModel(model);

    QHash<int, QVector<int>> adj;   // traverse-from node -> outgoing edge indices
    for (int i = 0; i < g.edges.size(); ++i)
        adj[upstream ? g.edges[i].toNode : g.edges[i].fromNode].push_back(i);

    QSet<int> visited = seeds;
    QList<int> queue(seeds.begin(), seeds.end());
    while (!queue.isEmpty())
    {
        const int n = queue.takeFirst();
        for (int ei : adj.value(n))
        {
            const int next = upstream ? g.edges[ei].fromNode : g.edges[ei].toNode;
            if (next >= 0 && !visited.contains(next)) { visited.insert(next); queue.push_back(next); }
        }
    }
    s.nodes = visited;

    for (const auto &e : g.edges)
    {
        const bool a = visited.contains(e.fromNode);
        const bool b = visited.contains(e.toNode);
        if (a && b)
            s.interiorLinks.push_back(e.linkId);
        else if (a != b)
            // Positive flow (from→to) enters the subnet when the TO end is inside.
            s.boundaryLinks.push_back({e.linkId, b});
    }
    s.ok = true;
    return s;
}

//! Seed engine-node indices from the current selection (selected nodes, plus
//! both endpoints of any selected links).
QSet<int> seedNodes(SWMMModelLayer *model, const QSet<SWMMObjectRef> &sel)
{
    QSet<int> seeds;
    SWMM_Engine e = model->engine();
    for (const auto &r : sel)
    {
        if (r.objectType == SWMMObjectRef::Node)
        {
            const int idx = swmm_node_index(e, r.name.toUtf8().constData());
            if (idx >= 0) seeds.insert(idx);
        }
        else if (r.objectType == SWMMObjectRef::Link)
        {
            const int li = swmm_link_index(e, r.name.toUtf8().constData());
            if (li >= 0)
            {
                const int a = model->linkFromNodeIdx(li);
                const int b = model->linkToNodeIdx(li);
                if (a >= 0) seeds.insert(a);
                if (b >= 0) seeds.insert(b);
            }
        }
    }
    return seeds;
}

QSet<SWMMObjectRef> subnetToRefs(SWMM_Engine e, const Subnetwork &sn)
{
    QSet<SWMMObjectRef> refs;
    for (int n : sn.nodes)
    {
        const char *id = swmm_node_id(e, n);
        if (id && *id) refs.insert(SWMMObjectRef(SWMMObjectRef::Node, QString::fromUtf8(id)));
    }
    for (int li : sn.interiorLinks)
    {
        const char *id = swmm_link_id(e, li);
        if (id && *id) refs.insert(SWMMObjectRef(SWMMObjectRef::Link, QString::fromUtf8(id)));
    }
    return refs;
}

} // namespace

void SWMMVis::onSelectUpstream()   { /* implemented via streamSelect below */
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine() || !pw->selectionManager())
    { onLogMessage(tr("Select Upstream: open a project first."), OpenSWMMVisLogMessage::Warning); return; }
    SWMMModelLayer *model = pw->modelLayer();
    const QSet<int> seeds = seedNodes(model, pw->selectionManager()->selection());
    if (seeds.isEmpty())
    { onLogMessage(tr("Select Upstream: select a node or link first."), OpenSWMMVisLogMessage::Information); return; }
    const Subnetwork sn = buildSubnetwork(model, seeds, /*upstream=*/true);
    const QSet<SWMMObjectRef> refs = subnetToRefs(model->engine(), sn);
    pw->selectionManager()->select(refs, SelectionManager::Add);
    onLogMessage(tr("Selected upstream subnetwork: %1 node(s), %2 link(s).")
                 .arg(sn.nodes.size()).arg(sn.interiorLinks.size()));
}

void SWMMVis::onSelectDownstream()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine() || !pw->selectionManager())
    { onLogMessage(tr("Select Downstream: open a project first."), OpenSWMMVisLogMessage::Warning); return; }
    SWMMModelLayer *model = pw->modelLayer();
    const QSet<int> seeds = seedNodes(model, pw->selectionManager()->selection());
    if (seeds.isEmpty())
    { onLogMessage(tr("Select Downstream: select a node or link first."), OpenSWMMVisLogMessage::Information); return; }
    const Subnetwork sn = buildSubnetwork(model, seeds, /*upstream=*/false);
    const QSet<SWMMObjectRef> refs = subnetToRefs(model->engine(), sn);
    pw->selectionManager()->select(refs, SelectionManager::Add);
    onLogMessage(tr("Selected downstream subnetwork: %1 node(s), %2 link(s).")
                 .arg(sn.nodes.size()).arg(sn.interiorLinks.size()));
}

void SWMMVis::onFlowBalance(bool upstream)
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine() || !pw->selectionManager())
    { onLogMessage(tr("Flow Balance: open a project first."), OpenSWMMVisLogMessage::Warning); return; }
    auto *results = pw->activeResultsLayer();
    if (!results || !results->outputHandle())
    { QMessageBox::information(this, tr("Flow Balance"),
          tr("Run a simulation or load a results (.out) file first.")); return; }
    SWMMModelLayer *model = pw->modelLayer();
    SWMM_Engine e = model->engine();
    const QSet<int> seeds = seedNodes(model, pw->selectionManager()->selection());
    if (seeds.isEmpty())
    { QMessageBox::information(this, tr("Flow Balance"),
          tr("Select a node or link first.")); return; }

    const Subnetwork sn = buildSubnetwork(model, seeds, upstream);
    SWMM_Output out = results->outputHandle();
    const int periods = swmm_output_get_period_count(out);
    const int nLinks  = swmm_link_count(e);
    if (periods <= 0 || nLinks <= 0)
    { QMessageBox::information(this, tr("Flow Balance"), tr("No results to summarize.")); return; }

    std::vector<float> flow(static_cast<std::size_t>(nLinks), 0.0f);
    swmm_output_get_link_result(out, periods - 1, SWMM_OUT_LINK_FLOW, flow.data());
    double inflow = 0.0, outflow = 0.0;
    for (const auto &bl : sn.boundaryLinks)
    {
        if (bl.first < 0 || bl.first >= nLinks) continue;
        const double into = bl.second ? flow[bl.first] : -flow[bl.first];
        if (into >= 0.0) inflow += into; else outflow += -into;
    }
    QMessageBox::information(this,
        tr("Flow Balance — %1").arg(upstream ? tr("Upstream") : tr("Downstream")),
        tr("Subnetwork: %1 node(s), %2 boundary link(s)\n\n"
           "Inflow:  %3\nOutflow: %4\nNet:     %5\n\n"
           "(final time-step flows, in project flow units)")
            .arg(sn.nodes.size()).arg(sn.boundaryLinks.size())
            .arg(inflow, 0, 'f', 3).arg(outflow, 0, 'f', 3)
            .arg(inflow - outflow, 0, 'f', 3));
}

void SWMMVis::onTravelTime(bool upstream)
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine() || !pw->selectionManager())
    { onLogMessage(tr("Travel Time: open a project first."), OpenSWMMVisLogMessage::Warning); return; }
    auto *results = pw->activeResultsLayer();
    if (!results || !results->outputHandle())
    { QMessageBox::information(this, tr("Travel Time"),
          tr("Run a simulation or load a results (.out) file first.")); return; }
    SWMMModelLayer *model = pw->modelLayer();
    SWMM_Engine e = model->engine();
    const QSet<int> seeds = seedNodes(model, pw->selectionManager()->selection());
    if (seeds.isEmpty())
    { QMessageBox::information(this, tr("Travel Time"), tr("Select a node or link first.")); return; }

    const Subnetwork sn = buildSubnetwork(model, seeds, upstream);
    SWMM_Output out = results->outputHandle();
    const int periods = swmm_output_get_period_count(out);
    const int nLinks  = swmm_link_count(e);
    if (periods <= 0 || nLinks <= 0)
    { QMessageBox::information(this, tr("Travel Time"), tr("No results to summarize.")); return; }

    std::vector<float> vel(static_cast<std::size_t>(nLinks), 0.0f);
    swmm_output_get_link_result(out, periods - 1, SWMM_OUT_LINK_VELOCITY, vel.data());
    double totalSec = 0.0;
    int counted = 0;
    for (int li : sn.interiorLinks)
    {
        if (li < 0 || li >= nLinks) continue;
        double length = 0.0;
        if (swmm_link_get_length(e, li, &length) != SWMM_OK || length <= 0.0) continue;
        const double v = vel[li];
        if (v > 1e-6) { totalSec += length / v; ++counted; }
    }
    QMessageBox::information(this,
        tr("Travel Time — %1").arg(upstream ? tr("Upstream") : tr("Downstream")),
        tr("Subnetwork: %1 flowing conduit(s)\n\n"
           "Total in-pipe travel time: %2 min\n\n"
           "(sum of length / velocity at the final time-step)")
            .arg(counted).arg(totalSec / 60.0, 0, 'f', 2));
}

void SWMMVis::onUserFlags()
{
    auto *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer() || !pw->modelLayer()->engine())
    {
        onLogMessage(tr("Open a SWMM project first to edit user flags."),
                     OpenSWMMVisLogMessage::Warning);
        return;
    }

    UserFlagsDialog dlg(pw->modelLayer()->ensureUserFlagsModel(), this);
    dlg.exec();
    // Apply may have written even if the dialog was later cancelled.
    if (dlg.wroteAnyChanges())
        pw->setHasChanges(true);
}

namespace {

// True when the engine will actually activate the 2D solver for this .inp:
// either an inline mesh ([2D_VERTICES] + [2D_TRIANGLES]) is embedded, or a
// [2D_MESH_FILE] reference resolves to an existing file. Mirrors the engine's
// activation rule (mesh presence, not a module flag).
bool twoDMeshResolves(const QString &inpPath)
{
    QFile f(inpPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString text = QString::fromUtf8(f.readAll());

    const bool hasInline =
        text.indexOf(QStringLiteral("[2D_VERTICES]"),  0, Qt::CaseInsensitive) >= 0 &&
        text.indexOf(QStringLiteral("[2D_TRIANGLES]"), 0, Qt::CaseInsensitive) >= 0;
    if (hasInline) return true;

    const int sectIdx = text.indexOf(QStringLiteral("[2D_MESH_FILE]"),
                                     0, Qt::CaseInsensitive);
    if (sectIdx < 0) return false;

    // Pull the FILE token and resolve it relative to the .inp directory.
    int p = text.indexOf(QChar('\n'), sectIdx);
    while (p > 0 && p < text.size()) {
        const int nl = text.indexOf(QChar('\n'), p + 1);
        const QString line = text.mid(p + 1, (nl < 0 ? text.size() : nl) - p - 1).trimmed();
        if (!line.isEmpty() && !line.startsWith(QStringLiteral(";"))
            && !line.startsWith(QChar('['))) {
            const auto parts = line.simplified().split(QChar(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2 && parts.first().compare(
                    QStringLiteral("FILE"), Qt::CaseInsensitive) == 0) {
                const QString ref = parts.mid(1).join(QChar(' '));
                QFileInfo refFi(ref);
                if (refFi.isRelative())
                    refFi = QFileInfo(QFileInfo(inpPath).absoluteDir()
                                          .absoluteFilePath(ref));
                return refFi.exists();
            }
            break;
        }
        if (line.startsWith(QChar('['))) break;
        if (nl < 0) break;
        p = nl;
    }
    return false;
}

} // namespace

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

    // Pre-flight: if the user enabled 2D Surface Routing for this model but no
    // mesh resolves (no inline [2D_*] sections and no valid [2D_MESH_FILE]),
    // the engine silently runs 1D-only. Warn and let the user decide rather
    // than producing 1D-only results without explanation.
    {
        const QString key = QStringLiteral("SWMMVis/Project/%1/Module2DEnabled")
                                .arg(inpPath);
        const bool twoDEnabled = QSettings().value(key, false).toBool();
        if (twoDEnabled && !twoDMeshResolves(inpPath)) {
            const auto reply = QMessageBox::warning(this,
                tr("2D mesh not found"),
                tr("2D Surface Routing is enabled for this model, but no 2D "
                   "mesh was found — there is no inline mesh in the .inp and no "
                   "valid [2D_MESH_FILE] reference.\n\nThe simulation will run "
                   "as 1D-only. Generate a mesh (or set one active in "
                   "Simulation Options → Mesh) to run the 2D solver.\n\n"
                   "Continue with a 1D-only run?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                onLogMessage(tr("Run cancelled — 2D enabled but no mesh found."),
                             OpenSWMMVisLogMessage::Information);
                return;
            }
            onLogMessage(tr("2D enabled but no mesh found — running 1D-only."),
                         OpenSWMMVisLogMessage::Warning);
        }
    }

    // Slice QB.3 — honour the Simulation Options → Output tab override
    // (Slice AA-4 QSettings round-trip) when present, falling back to
    // the sibling `<inpStem>.{rpt,out}` default so unchanged projects
    // keep today's behaviour. Strategy C per §Q.5 — GUI-only resolution,
    // no engine OPTIONS surface. Surface a log line naming the resolved
    // paths so the user can see which files the run will write.
    const QString rptPath =
        openswmmvis::resolveRunOutputPathFromSettings(
            inpPath, openswmmvis::RunOutputKind::Rpt);
    const QString outPath =
        openswmmvis::resolveRunOutputPathFromSettings(
            inpPath, openswmmvis::RunOutputKind::Out);
    {
        const QFileInfo inpFi(inpPath);
        const QString siblingBase = inpFi.absoluteDir().filePath(
            inpFi.completeBaseName());
        const QString defaultRpt = siblingBase + QStringLiteral(".rpt");
        const QString defaultOut = siblingBase + QStringLiteral(".out");
        const bool overrodeRpt = (QDir::cleanPath(rptPath) != QDir::cleanPath(defaultRpt));
        const bool overrodeOut = (QDir::cleanPath(outPath) != QDir::cleanPath(defaultOut));
        if (overrodeRpt || overrodeOut) {
            onLogMessage(tr("Using output overrides — rpt: %1 / out: %2")
                             .arg(rptPath, outPath),
                         OpenSWMMVisLogMessage::Information);
        }
    }

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
    const QString instanceName = QFileInfo(inpPath).fileName();
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

    // Mirror every engine warning to the message log as well, not just the
    // Simulation Status tree — engine diagnostics should always be visible
    // in the log regardless of which panel the user has open.
    connect(runner, &SimulationRunner::warningReceived, this,
            [this, instanceName](int /*wJobId*/, int code, const QString &message) {
                onLogMessage(code != 0
                                 ? tr("Simulation warning [%1]: %2 — %3")
                                       .arg(code).arg(instanceName, message)
                                 : tr("Simulation warning: %1 — %2")
                                       .arg(instanceName, message),
                             OpenSWMMVisLogMessage::Warning);
            });

    QPointer<SWMMVis> self(this);
    QPointer<SWMMVisProjectWindow> pwGuard(pw);
    QString outPathCopy  = outPath;
    QString rptPathCopy  = rptPath;

    connect(runner, &SimulationRunner::finished, this,
            [self, runner, pwGuard, outPathCopy, rptPathCopy, instanceName]
            (int finishedJobId, bool success, int errCode, QString errMsg,
             double runoffFrac, double routingFrac, double twoDFrac) {
                if (!self) return;
                self->mSimStatusModel->finishJob(finishedJobId, success, errCode,
                                                 errMsg, runoffFrac, routingFrac,
                                                 twoDFrac);
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
                const bool outHasData = QFileInfo(outPathCopy).exists()
                    && QFileInfo(outPathCopy).size() > 0;
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
                } else if (!outHasData) {
                    // The run reported success but wrote no output. This almost
                    // always means the engine failed during input parsing or
                    // setup and the worker didn't surface it as a non-zero exit.
                    // Don't claim success — point the user at the report file,
                    // which carries the actual ERROR line.
                    self->onLogMessage(
                        tr("Simulation reported success but produced no output: %1\n"
                           "The run likely failed during input parsing or setup — "
                           "see the report file for the cause: %2")
                            .arg(instanceName, rptPathCopy),
                        OpenSWMMVisLogMessage::Error);
                } else {
                    self->onLogMessage(
                        tr("Simulation finished. Results: %1").arg(outPathCopy));
                }

                // Auto-load the .out as a SWMMResultsLayer regardless of
                // cancel/success — the engine flushed partial output
                // either way, and the user explicitly asked for Cancel to
                // save results. Only skip on engine-error with no output.
                const bool hasResults = (success || cancelled) && outHasData;
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
                    // Remember which .rpt this run wrote so the Report
                    // Viewer can list it (persisted in the .oswp sidecar).
                    rl->setReportFilePath(rptPathCopy);

                    QList<QString> rlWarnings, rlErrors;
                    if (rl->openResults(rlWarnings, rlErrors))
                    {
                        rl->autoStretchColorRamp();
                        // A just-finished run is an explicit user action — make
                        // its results the active 1D analysis layer (drives the
                        // combo, layer-tree check, and transport).
                        pwGuard->setActiveResultsLayer(rl);
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
                // Outputs inherit the model's CRS so the Properties
                // window shows a real CRS for the live results layer.
                if (pwGuard->modelLayer() && pwGuard->modelLayer()->srs())
                    layer->setSRS(
                        new SpatialReferenceSystem(*pwGuard->modelLayer()->srs(), layer),
                        /*ownsSRS=*/true);
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

                // The live 2D run is an explicit user action — make it the
                // active 2D analysis layer (drives the 2D combo + cell picks).
                pwGuard->setActive2DResultsLayer(layer);

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
                SWMM2DResultsLayer *layer = it.value();
                auto *engineSrc =
                    dynamic_cast<EngineMesh2DSource *>(layer->source());
                if (!engineSrc) return;
                engineSrc->setEdgeGeometry(
                    std::vector<float>(length.begin(), length.end()),
                    std::vector<float>(nx.begin(),     nx.end()),
                    std::vector<float>(ny.begin(),     ny.end()));
                layer->refreshCurrentFrame();
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
                layer->refreshCurrentFrame();
            });

    // Per-tick reconstructed vertex heads — feeds the smooth (Gouraud) depth
    // fill + contour interpolation. Paired with the matching depth tick by
    // elapsedSec, like flux. Heads do not advance the frame counter, but they
    // can change the current frame's interpolation basis after the depth packet
    // already painted, so re-apply the same frame.
    connect(runner, &SimulationRunner::twoDVertexHeadsAvailable, this,
            [self](int twoDJobId, QVector<double> heads,
                   QDateTime simTime, double elapsedSec) {
                if (!self) return;
                auto it = self->mActive2DResultsLayers.constFind(twoDJobId);
                if (it == self->mActive2DResultsLayers.constEnd() || !it.value())
                    return;
                SWMM2DResultsLayer *layer = it.value();
                auto *engineSrc =
                    dynamic_cast<EngineMesh2DSource *>(layer->source());
                if (!engineSrc) return;
                engineSrc->pushVertexHeads(
                    std::vector<double>(heads.begin(), heads.end()),
                    simTime, elapsedSec);
                layer->refreshCurrentFrame();
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
                    // Run finished — drop the "(live)" qualifier so results read
                    // as final and fully available for visualization. The
                    // file-backed source is installed below when the .h5 is
                    // present; otherwise the in-memory history from the live run
                    // is retained (still a complete, scrubbable source).
                    layer->setName(QStringLiteral("2D Results"));
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

                            layer->setSource(std::move(h5Src));

                            // Auto-tune the ramp + dry threshold to the
                            // actual data range. setMaxDepth pins the
                            // upper end (disables further auto-grow);
                            // dry_depth is biased to the floor so very
                            // shallow runs still produce visible cells.
                            if (peakDepth > 0.0f) {
                                layer->setMaxDepth(peakDepth);
                                // Refine-only: the 5%-of-peak heuristic may
                                // LOWER the wet/dry cutoff (keeps very shallow
                                // runs visible) but must never RAISE it above
                                // the model DRY_DEPTH applied at run init —
                                // raising it culled every cell shallower than
                                // 5% of peak from the post-run scrub view
                                // (0.59 m peak → 3 cm cutoff wiped the
                                // shallow flooding the live view had shown).
                                const double autoDry =
                                    std::max(1e-5, 0.05 * double(peakDepth));
                                if (autoDry < layer->dryDepth())
                                    layer->setDryDepth(autoDry);
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

                    // Run finished — refresh the 2D results selector so the
                    // "(live)" label drops to "2D Results", and re-arm the
                    // animation controller against the now-static (scrubbable)
                    // source so play/scrub operate on the full results. The
                    // detach + re-attach forces a clean state re-sync of the
                    // toolbar range/cursor from the swapped source; it only
                    // runs when this layer is the active 2D driver (no 1D
                    // primary), mirroring the registration guard at run start.
                    self->refreshActiveResultsCombos();
                    // Arm the animation slider against the finished layer
                    // whenever no 1D primary is driving — not only when this
                    // layer was already the registered fallback. The run-start
                    // registration is skipped when a (possibly stale) primary
                    // existed at that moment, which left the controller with
                    // NO driver after the run: seekToTime() bailed, the slider
                    // was dead, and the 2D view froze on the peak frame until
                    // an extent change forced a re-render.
                    if (auto *ac = self->mAnimationController;
                        ac && !ac->primaryLayer()) {
                        ac->setFallback2DLayer(nullptr);   // force clean re-sync
                        ac->setFallback2DLayer(layer);
                    }
                }
                self->mActive2DResultsLayers.erase(it);
                self->mSimulationStarts.remove(finishedJobId);
            });

    runner->start();
}

void SWMMVis::onPlotTimeSeries()
{
    // Slice GUI-2026-05-30 §5 — Analysis-toolbar entry point.
    //
    // Flow:
    //   1. If a SWMM object is currently selected, use it → openTimeSeriesPlotFor.
    //   2. Otherwise arm a one-shot pick flag and activate the select tool.
    //      The next click from MapToolSelect's selectionPicked path will be
    //      caught by the existing plotTimeSeriesRequested wiring; we read
    //      the flag in onPlotTimeSeriesPickComplete to know whether to plot.
    //   3. We also offer a "System Variable…" inline shortcut via a brief
    //      status-bar prompt: the user can press the Plot Timeseries button
    //      a second time while the flag is armed to fall through to the
    //      System Variable picker dialog.
    SWMMVisProjectWindow *pw = activeProjectWindow();
    if (!pw) {
        QMessageBox::information(this, tr("Plot Time Series"),
            tr("Open a project before plotting time series."));
        return;
    }

    // Look for a plottable primary selection.
    auto firstPlottable = [](const QSet<SWMMObjectRef> &set) -> SWMMObjectRef {
        for (const auto &r : set) {
            if (r.objectType == SWMMObjectRef::Node
                || r.objectType == SWMMObjectRef::Link
                || r.objectType == SWMMObjectRef::Subcatchment)
                return r;
        }
        return SWMMObjectRef{};
    };

    if (auto *sm = pw->selectionManager()) {
        const SWMMObjectRef sel = firstPlottable(sm->selection());
        if (sel.isValid()) {
            openTimeSeriesPlotFor(sel);
            return;
        }
    }

    // No current selection — second click on the button activates the
    // System Variable picker; first click arms the one-shot pick: switch
    // to the select tool, watch SelectionManager::selectionChanged once,
    // and route the resulting object into openTimeSeriesPlotFor.
    if (!mPendingPlotTimeseriesPick) {
        mPendingPlotTimeseriesPick = true;
        pw->activateSelectTool();
        statusBar()->showMessage(
            tr("Click a node, link, or subcatchment to plot — "
               "or click Plot Timeseries again for a system variable."),
            10000);
        if (auto *sm = pw->selectionManager()) {
            auto *conn = new QMetaObject::Connection;
            *conn = connect(sm, &SelectionManager::selectionChanged, this,
                [this, conn, sm, firstPlottable](const QSet<SWMMObjectRef> &current,
                                                  const QSet<SWMMObjectRef> &,
                                                  const QSet<SWMMObjectRef> &) {
                    if (!mPendingPlotTimeseriesPick) {
                        QObject::disconnect(*conn); delete conn; return;
                    }
                    const SWMMObjectRef pick = firstPlottable(current);
                    if (!pick.isValid()) return;          // unplottable click — keep listening
                    QObject::disconnect(*conn); delete conn;
                    onPlotTimeSeriesPickComplete(pick);
                });
        }
        return;
    }

    // Second invocation: drop the pending flag and pop the system-variable
    // picker submenu (reuses the existing AttributePickerMenu helper).
    mPendingPlotTimeseriesPick = false;
    QMenu *sysMenu = openswmmvis::ui::AttributePickerMenu::createForSystem(
        (UnitSystem::instance() && UnitSystem::instance()->isSI())
            ? openswmmvis::plot::UnitSystem::SI
            : openswmmvis::plot::UnitSystem::US,
        this);
    if (!sysMenu) return;
    sysMenu->setTitle(tr("Plot System Variable…"));
    QAction *picked = sysMenu->exec(QCursor::pos());
    const auto attr = openswmmvis::ui::AttributePickerMenu::attributeFrom(picked);
    sysMenu->deleteLater();
    if (attr != openswmmvis::plot::PlotAttribute::Unknown)
        openComparisonPlotForSystemAttribute(attr);
}

void SWMMVis::onPlotTimeSeriesPickComplete(const SWMMObjectRef &ref)
{
    if (!mPendingPlotTimeseriesPick) return;   // not our pick
    mPendingPlotTimeseriesPick = false;
    if (ref.isValid())
        openTimeSeriesPlotFor(ref);
}

void SWMMVis::onShowReport()
{
    // Slice GUI-2026-05-30 §6 — open the Report Viewer over the active
    // project's resolved .rpt path.  Falls back to <inpStem>.rpt when no
    // simulation-options override is configured.
    SWMMVisProjectWindow *pw = activeProjectWindow();
    if (!pw || !pw->modelLayer()) {
        QMessageBox::information(this, tr("Report"),
            tr("Open a project before viewing the report."));
        return;
    }
    const QString inpPath = pw->modelLayer()->modelFilePath();
    if (inpPath.isEmpty()) {
        QMessageBox::information(this, tr("Report"),
            tr("Save the project before viewing the report."));
        return;
    }

    // One report source per loaded run (results layer). Each layer carries
    // the .rpt its run wrote (persisted in the .oswp); layers loaded from
    // older projects fall back to the sibling <outStem>.rpt convention.
    QVector<openswmmvis::ui::ReportSource> sources;
    QSet<QString> seen;
    int initialIndex = 0;
    auto addSource = [&sources, &seen](const QString &label,
                                       const QString &path) -> bool {
        if (path.isEmpty() || !QFileInfo::exists(path)) return false;
        const QString canon = QFileInfo(path).absoluteFilePath();
        if (seen.contains(canon)) return false;
        seen.insert(canon);
        sources.append({ label, canon });
        return true;
    };
    if (pw->canvas()) {
        for (OpenSWMMVisLayer *l : pw->canvas()->layers()) {
            auto *rl = qobject_cast<SWMMResultsLayer *>(l);
            if (!rl) continue;
            QString rpt = rl->reportFilePath();
            if (rpt.isEmpty()) {
                const QFileInfo outFi(rl->resultsFilePath());
                rpt = outFi.absoluteDir().filePath(
                    outFi.completeBaseName() + QStringLiteral(".rpt"));
            }
            const QString label = rl->scenarioName().isEmpty()
                                      ? rl->name() : rl->scenarioName();
            if (addSource(label, rpt) && rl == pw->activeResultsLayer())
                initialIndex = sources.size() - 1;
        }
    }
    // Settings-resolved path — covers the no-results-layer case (e.g. a run
    // that failed before writing a .out but still produced a .rpt).
    const QString rptPath = openswmmvis::resolveRunOutputPathFromSettings(
        inpPath, openswmmvis::RunOutputKind::Rpt);
    addSource(QFileInfo(rptPath).fileName(), rptPath);

    if (sources.isEmpty()) {
        QMessageBox::information(this, tr("Report"),
            tr("No report file found at:\n%1\n\n"
               "Run a simulation to generate the report.").arg(rptPath));
        return;
    }

    auto *dlg = new openswmmvis::ui::StatusReportDialog(sources, initialIndex, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
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
        openswmmvis::io::gdalcaps::vectorOpenFilter());
    if (path.isEmpty()) return;

    // Multi-layer datasources (GeoPackage, Esri File GDB, multi-layer GML/KML)
    // get a sublayer picker; single-layer sources load straight through.
    const QList<GISVectorLayer::OgrSublayerInfo> subs =
        GISVectorLayer::enumerateSublayers(path);

    QStringList layerNames;   // empty ⇒ open the default (first) layer
    if (subs.size() > 1)
    {
        SublayerSelectionDialog dlg(path, subs, this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        layerNames = dlg.selectedLayerNames();
        if (layerNames.isEmpty())
        {
            onLogMessage(tr("No layers selected — nothing added."),
                         OpenSWMMVisLogMessage::Warning);
            return;
        }
    }

    int added = 0;
    if (layerNames.isEmpty())
    {
        c->addLayer(new GISVectorLayer(path), true);
        ++added;
    }
    else
    {
        for (const QString &name : layerNames)
        {
            c->addLayer(new GISVectorLayer(path, name), true);
            ++added;
        }
    }

    // Auto-fit so the user sees what they just loaded — the layer may sit
    // far from the SWMM model in coordinate space and the canvas otherwise
    // keeps its prior viewport.
    c->zoomToFullExtent();
    onLogMessage(tr("Added %1 vector layer(s) from %2")
                     .arg(added)
                     .arg(QFileInfo(path).fileName()));
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
        openswmmvis::io::gdalcaps::rasterOpenFilter());
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
        // Explicitly-added results layer → make it the active 1D analysis layer.
        pw->setActiveResultsLayer(layer);
        mAnimationController->setPrimaryLayer(layer);
        onLogMessage(tr("Added results layer: %1").arg(QFileInfo(path).fileName()));
    }
    else
    {
        for (const QString &e : errors)
            onLogMessage(e, OpenSWMMVisLogMessage::Error);
    }
}
