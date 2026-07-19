/*!
 * \file   simulationoptionsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simulationoptionsdialog.h"

#include "ui/uiscrollhelpers.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "ui/dialogs/hotstartsavesmodel.h"
#include "ui/widgets/relativepathpicker.h"
#include "ui/dialogs/pathbrowsedelegate.h"
#include "ui/dialogs/pluginstablemodel.h"
#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "mesh/inpmeshwriter.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "plugins/filefilterregistry.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"

#include <openswmm/plugin_sdk/PluginDiscovery.hpp>

#include <qcustomeditors.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QTimeEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QStandardItemModel>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>             // Slice RC.4 — built-in plugin-id lookup
#include <QSettings>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableView>
#include <QTableWidget>
#include <QAbstractItemView>
#include <QBrush>
#include <QButtonGroup>
#include <QColor>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QRadioButton>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextList>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QAction>

#include <openswmm/engine/openswmm_hotstart.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_spatial.h>

#include <algorithm>
#include <functional>

// ---------------------------------------------------------------------------
// Pure-helpers (parseEngineBool / engineBoolString / format/parse DateTime)
// live in simulationoptionshelpers.cpp so leaf QtTests can compile them
// without dragging in GDAL/OGR via the spatial-tab CRS code below.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SimulationOptionsDialog::SimulationOptionsDialog(SWMM_Engine engine,
                                                 SWMMModelLayer *layer,
                                                 const QString &engineVersion,
                                                 SWMMVisProjectWindow *projectWindow,
                                                 QWidget *parent)
    : QDialog(parent),
      m_engine(engine),
      m_layer(layer),
      m_projectWindow(projectWindow),
      m_engineVersion(engineVersion)
{
    setWindowTitle(tr("Simulation Options"));
    resize(620, 600);
    buildUi();
    readFromEngine();
    refreshSpatialSummary();
    applyEngineConstraints();
}

// Destructor is now inline in the header to keep the moc vtable self-contained.

// ---------------------------------------------------------------------------
// Engine constraints
// ---------------------------------------------------------------------------

namespace {

// Mesh-list item classification (stored under Qt::UserRole). The inline row
// is synthetic — it has no .2dm file on disk — so the Set Active / Remove
// handlers must branch on it.
constexpr int kMeshKindRole = Qt::UserRole;
constexpr int kMeshExternal = 0;   ///< sibling *.2dm file
constexpr int kMeshInline   = 1;   ///< mesh embedded in the project .inp

/*!
 * \brief True when the .inp already carries 2D content.
 *
 * The engine activates the 2D solver from the presence of mesh sections,
 * not from any module key — so a file with [2D_OPTIONS] / [2D_VERTICES] /
 * [2D_TRIANGLES] / [2D_MESH_FILE] has the module enabled by construction.
 * Used as the module-checkbox default when the per-project QSettings flag
 * has never been written (pre-built demos, files authored outside this
 * GUI's mesh-generation flow).
 */
bool inpCarries2DSections(const QString &inpPath)
{
    if (inpPath.isEmpty()) return false;
    QFile f(inpPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString text = QString::fromUtf8(f.readAll());
    for (const auto *sect : { "[2D_OPTIONS]", "[2D_VERTICES]",
                              "[2D_TRIANGLES]", "[2D_MESH_FILE]" }) {
        if (text.indexOf(QLatin1String(sect), 0, Qt::CaseInsensitive) >= 0)
            return true;
    }
    return false;
}

} // namespace

/*!
 * Disables widgets / tabs that the currently-selected engine does not support.
 *
 * Legacy SWMM 5.x limits:
 *   - No DYNAMIC_SLOT surcharge method
 *   - No SEMI_IMPLICIT node-continuity scheme
 *   - No Anderson acceleration
 *   - No plugin writers / containers
 *   - No [PLUGINS] section
 *
 * The 2D module checkbox + Mesh tab + 2D Surface Routing tab stay editable
 * even on legacy SWMM 5: mesh preparation is a pure GUI concern and the
 * engine simply ignores 2D inputs at run time. New engine (6.x) supports
 * all of the above, so all controls stay enabled.
 */
void SimulationOptionsDialog::applyEngineConstraints()
{
    const bool legacy = m_engineVersion.startsWith(QLatin1String("5."));
    if (!legacy)
        return;   // new engine: everything already enabled

    const QString tip = tr("Not available in SWMM 5 (legacy engine).");

    // ── Hydraulics tab: DYNAMIC_SLOT surcharge ─────────────────────────────
    // EXTRAN and SLOT exist in SWMM 5.x; DYNAMIC_SLOT is new-engine-only.
    if (m_surchargeCombo) {
        auto *model = qobject_cast<QStandardItemModel *>(m_surchargeCombo->model());
        if (model) {
            for (int i = 0; i < m_surchargeCombo->count(); ++i) {
                if (m_surchargeCombo->itemData(i).toString() == QLatin1String("DYNAMIC_SLOT")) {
                    model->item(i)->setEnabled(false);
                    model->item(i)->setToolTip(tip);
                    if (m_surchargeCombo->currentIndex() == i)
                        m_surchargeCombo->setCurrentIndex(0); // fall back to EXTRAN
                    break;
                }
            }
        }
        updateSurchargeFieldsEnabled(); // ensure DPS_* spins follow
    }

    // ── Hydraulics tab: node-continuity and Anderson acceleration ──────────
    if (m_nodeContinuityCombo) {
        auto *model = qobject_cast<QStandardItemModel *>(m_nodeContinuityCombo->model());
        if (model) {
            for (int i = 0; i < m_nodeContinuityCombo->count(); ++i) {
                if (m_nodeContinuityCombo->itemData(i).toString() == QLatin1String("SEMI_IMPLICIT")) {
                    model->item(i)->setEnabled(false);
                    model->item(i)->setToolTip(tip);
                    if (m_nodeContinuityCombo->currentIndex() == i)
                        m_nodeContinuityCombo->setCurrentIndex(0); // fall back to EXPLICIT
                    break;
                }
            }
        }
    }
    if (m_andersonAccelBox) {
        m_andersonAccelBox->setEnabled(false);
        m_andersonAccelBox->setToolTip(tip);
    }

    // ── Files tab: plugin writers and [PLUGINS] table ──────────────────────
    if (m_writersGroup) {
        m_writersGroup->setEnabled(false);
        m_writersGroup->setToolTip(tip);
    }
    for (QWidget *w : {(QWidget *)m_pluginsView,
                       (QWidget *)m_pluginsAddBtn,
                       (QWidget *)m_pluginsRemoveBtn}) {
        if (w) {
            w->setEnabled(false);
            w->setToolTip(tip);
        }
    }
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // Settings-style navigation — a category list on the left switching a
    // stacked page on the right (mirrors PreferencesDialog), replacing the
    // former wide tab bar. Each build*Tab() returns its page; addCategory
    // wraps it in a scroll area (so tall pages don't force the dialog tall)
    // and registers the sidebar row.
    auto *split = new QHBoxLayout();
    split->setSpacing(8);
    root->addLayout(split, 1);

    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(190);
    split->addWidget(m_categoryList);

    m_pages = new QStackedWidget(this);
    split->addWidget(m_pages, 1);

    connect(m_categoryList, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);

    addCategory(tr("Title / Notes"),          buildTitleNotesTab());
    addCategory(tr("Models / Processes"),     buildModelsTab());
    addCategory(tr("Dates & Times"),          buildDatesTab());
    addCategory(tr("Routing & Hydraulics"),   buildHydraulicsTab());
    addCategory(tr("System / Performance"),   buildPerformanceTab());
    addCategory(tr("Spatial & CRS"),          buildSpatialTab());

    // Mesh configurations — file-management UI, lives outside any
    // OPENSWMM_HAS_2D guard because picking a *.2dm reference is a pure
    // GUI concern (the engine 2D solver isn't required to organise mesh
    // candidates). Always editable: creating/selecting a mesh is what
    // turns on the 2D module, not the other way around.
    addCategory(tr("Mesh"), buildMeshTab());
    m_meshRow = m_categoryList->count() - 1;

#ifdef OPENSWMM_HAS_2D
    addCategory(tr("2D Surface Routing"), build2DTab());
    m_2DRow = m_categoryList->count() - 1;  // sidebar row for the 2D page.
    if (m_module2DBox)
        set2DRowEnabled(m_module2DBox->isChecked());
#endif

    // Slice AA-3.5 — Files / Plugins page (its own inner sub-tabs). Lives at
    // the end so existing ordering is preserved.
    addCategory(tr("Files / Output / Plugins"), buildFilesTab());

    m_categoryList->setCurrentRow(0);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &SimulationOptionsDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SimulationOptionsDialog::onApply);
}

void SimulationOptionsDialog::addCategory(const QString &title, QWidget *page)
{
    m_categoryList->addItem(title);
    m_pages->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_pages));
}

void SimulationOptionsDialog::set2DRowEnabled(bool enabled)
{
    if (m_2DRow < 0) return;
    QListWidgetItem *item = m_categoryList->item(m_2DRow);
    if (!item) return;
    // A QStackedWidget has no per-page "enabled tab", so gate at the sidebar
    // row instead: keep it visible but non-selectable when 2D is off.
    item->setFlags(enabled
                       ? (Qt::ItemIsSelectable | Qt::ItemIsEnabled)
                       : Qt::ItemFlags(Qt::NoItemFlags));
    // If the disabled row was current, move focus off it.
    if (!enabled && m_categoryList->currentRow() == m_2DRow)
        m_categoryList->setCurrentRow(0);
}

QWidget *SimulationOptionsDialog::buildTitleNotesTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *toolbar = new QToolBar(page);
    toolbar->setIconSize(QSize(16, 16));

    m_titleBoldAction = toolbar->addAction(tr("Bold"));
    m_titleBoldAction->setShortcut(QKeySequence::Bold);
    m_titleBoldAction->setCheckable(true);
    m_titleBoldAction->setToolTip(tr("Bold (Ctrl+B)"));

    m_titleItalicAction = toolbar->addAction(tr("Italic"));
    m_titleItalicAction->setShortcut(QKeySequence::Italic);
    m_titleItalicAction->setCheckable(true);
    m_titleItalicAction->setToolTip(tr("Italic (Ctrl+I)"));

    m_titleUnderlineAction = toolbar->addAction(tr("Underline"));
    m_titleUnderlineAction->setShortcut(QKeySequence::Underline);
    m_titleUnderlineAction->setCheckable(true);
    m_titleUnderlineAction->setToolTip(tr("Underline (Ctrl+U)"));

    toolbar->addSeparator();
    auto *bulletAction = toolbar->addAction(tr("Bulleted list"));
    bulletAction->setToolTip(tr("Insert bulleted list"));
    auto *numberedAction = toolbar->addAction(tr("Numbered list"));
    numberedAction->setToolTip(tr("Insert numbered list"));

    vlay->addWidget(toolbar);

    m_titleNotesEdit = new QTextEdit(page);
    m_titleNotesEdit->setAcceptRichText(true);
    m_titleNotesEdit->setPlaceholderText(
        tr("Enter project title and notes (mirrors the SWMM [TITLE] section)."));
    vlay->addWidget(m_titleNotesEdit, 1);

    connect(m_titleBoldAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_titleNotesEdit) return;
        QTextCharFormat fmt;
        fmt.setFontWeight(checked ? QFont::Bold : QFont::Normal);
        m_titleNotesEdit->mergeCurrentCharFormat(fmt);
    });
    connect(m_titleItalicAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_titleNotesEdit) return;
        QTextCharFormat fmt;
        fmt.setFontItalic(checked);
        m_titleNotesEdit->mergeCurrentCharFormat(fmt);
    });
    connect(m_titleUnderlineAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_titleNotesEdit) return;
        QTextCharFormat fmt;
        fmt.setFontUnderline(checked);
        m_titleNotesEdit->mergeCurrentCharFormat(fmt);
    });
    auto applyListStyle = [this](QTextListFormat::Style style) {
        if (!m_titleNotesEdit) return;
        QTextCursor c = m_titleNotesEdit->textCursor();
        c.createList(style);
    };
    connect(bulletAction,   &QAction::triggered, this,
            [applyListStyle]() { applyListStyle(QTextListFormat::ListDisc); });
    connect(numberedAction, &QAction::triggered, this,
            [applyListStyle]() { applyListStyle(QTextListFormat::ListDecimal); });

    connect(m_titleNotesEdit, &QTextEdit::currentCharFormatChanged, this,
            [this](const QTextCharFormat &fmt) {
                if (m_titleBoldAction)
                    m_titleBoldAction->setChecked(fmt.fontWeight() >= QFont::Bold);
                if (m_titleItalicAction)
                    m_titleItalicAction->setChecked(fmt.fontItalic());
                if (m_titleUnderlineAction)
                    m_titleUnderlineAction->setChecked(fmt.fontUnderline());
            });

    return page;
}

QWidget *SimulationOptionsDialog::buildModelsTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *procGroup = new QGroupBox(tr("Process models"), page);
    auto *procForm  = new QFormLayout(procGroup);

    m_infiltrationCombo = new QComboBox(procGroup);
    m_infiltrationCombo->addItem(tr("Horton"),                        QStringLiteral("HORTON"));
    m_infiltrationCombo->addItem(tr("Modified Horton"),               QStringLiteral("MOD_HORTON"));
    m_infiltrationCombo->addItem(tr("Green-Ampt"),                    QStringLiteral("GREEN_AMPT"));
    m_infiltrationCombo->addItem(tr("Modified Green-Ampt"),           QStringLiteral("MOD_GREEN_AMPT"));
    m_infiltrationCombo->addItem(tr("Curve Number"),                  QStringLiteral("CURVE_NUMBER"));
    m_infiltrationCombo->setToolTip(
        tr("Infiltration model used on every subcatchment (option INFILTRATION)."));
    procForm->addRow(tr("Infiltration model:"), m_infiltrationCombo);

    m_routingCombo = new QComboBox(procGroup);
    m_routingCombo->addItem(tr("Steady"),            QStringLiteral("STEADY"));
    m_routingCombo->addItem(tr("Kinematic Wave"),    QStringLiteral("KINWAVE"));
    m_routingCombo->addItem(tr("Dynamic Wave"),      QStringLiteral("DYNWAVE"));
    m_routingCombo->setToolTip(
        tr("Flow-routing method for conduits (option FLOW_ROUTING)."));
    procForm->addRow(tr("Flow routing:"), m_routingCombo);

    vlay->addWidget(procGroup);

    // ── Modules ────────────────────────────────────────────────────────
    // 1D is the always-on core. 2D Surface Routing is an optional module
    // gated by a project-level toggle here. When enabled, the dedicated
    // "2D Surface Routing" tab becomes interactive; when disabled, the
    // tab stays in place but is greyed out so users can still see what
    // the parameters would look like. Toggle persists per-.inp under
    // QSettings so reopening a project remembers the choice.
    auto *modulesGroup = new QGroupBox(tr("Modules"), page);
    auto *modulesLay   = new QVBoxLayout(modulesGroup);

    m_module1DBox = new QCheckBox(tr("1D Hydraulics (always on)"), modulesGroup);
    m_module1DBox->setChecked(true);
    m_module1DBox->setEnabled(false);
    m_module1DBox->setToolTip(
        tr("The 1D pipe-network solver is the SWMM core and cannot be disabled."));
    modulesLay->addWidget(m_module1DBox);

    m_module2DBox = new QCheckBox(tr("2D Surface Routing (CVODE)"), modulesGroup);
    // Module toggle is a project-level flag (QSettings-backed) — always
    // editable so the user can prepare meshes / configurations even when
    // the engine 2D solver isn't compiled in. The engine-side gate at
    // compile time only affects the parameter knobs on the 2D Surface
    // Routing tab; the Mesh tab + this toggle are GUI concerns.
#ifdef OPENSWMM_HAS_2D
    m_module2DBox->setToolTip(
        tr("Enable the optional 2D surface-routing module. When on, the "
           "Mesh + 2D Surface Routing tabs become editable and the engine "
           "runs the 2D solver coupled to the 1D network."));
#else
    m_module2DBox->setToolTip(
        tr("Project-level 2D module flag. Mesh selection tab becomes "
           "interactive when checked. The engine 2D solver itself is not "
           "compiled in this binary — rebuild with -DOPENSWMM_BUILD_2D=ON "
           "(requires SUNDIALS) for end-to-end coupled runs; mesh "
           "generation works regardless."));
#endif
    modulesLay->addWidget(m_module2DBox);

    vlay->addWidget(modulesGroup);

    connect(m_module2DBox, &QCheckBox::toggled,
            this, &SimulationOptionsDialog::on2DModuleToggled);

    auto *flagsGroup = new QGroupBox(tr("Options / flags"), page);
    auto *flagsLay   = new QVBoxLayout(flagsGroup);

    m_allowPondingBox  = new QCheckBox(tr("Allow ponding at nodes (ALLOW_PONDING)"), flagsGroup);
    flagsLay->addWidget(m_allowPondingBox);

    vlay->addWidget(flagsGroup);

    // Process activation. Checked = process runs (engine writes IGNORE_X NO);
    // unchecked = engine ignores the process (writes IGNORE_X YES). The
    // .inp surface keeps the legacy IGNORE_* keys — only the UI flips.
    auto *ignoreGroup = new QGroupBox(tr("Active processes"), page);
    auto *ignoreLay   = new QVBoxLayout(ignoreGroup);
    m_ignoreRainfallBox    = new QCheckBox(tr("Rainfall / runoff"),  ignoreGroup);
    m_ignoreSnowmeltBox    = new QCheckBox(tr("Snowmelt"),           ignoreGroup);
    m_ignoreGroundwaterBox = new QCheckBox(tr("Groundwater"),        ignoreGroup);
    m_ignoreRDIIBox        = new QCheckBox(tr("RDII"),               ignoreGroup);
    m_ignoreQualityBox     = new QCheckBox(tr("Water quality"),      ignoreGroup);
    m_ignoreRoutingBox     = new QCheckBox(tr("Flow routing"),       ignoreGroup);
    m_ignoreRainfallBox   ->setToolTip(tr("Unchecking writes IGNORE_RAINFALL YES — engine skips runoff entirely."));
    m_ignoreSnowmeltBox   ->setToolTip(tr("Unchecking writes IGNORE_SNOWMELT YES — engine skips snowmelt."));
    m_ignoreGroundwaterBox->setToolTip(tr("Unchecking writes IGNORE_GROUNDWATER YES — engine skips groundwater."));
    m_ignoreRDIIBox       ->setToolTip(tr("Unchecking writes IGNORE_RDII YES — engine skips RDII."));
    m_ignoreQualityBox    ->setToolTip(tr("Unchecking writes IGNORE_QUALITY YES — engine skips water-quality routing."));
    m_ignoreRoutingBox    ->setToolTip(tr("Unchecking writes IGNORE_ROUTING YES — engine skips flow routing."));
    ignoreLay->addWidget(m_ignoreRainfallBox);
    ignoreLay->addWidget(m_ignoreSnowmeltBox);
    ignoreLay->addWidget(m_ignoreGroundwaterBox);
    ignoreLay->addWidget(m_ignoreRDIIBox);
    ignoreLay->addWidget(m_ignoreQualityBox);
    ignoreLay->addWidget(m_ignoreRoutingBox);
    vlay->addWidget(ignoreGroup);

    vlay->addStretch();

    return page;
}

QWidget *SimulationOptionsDialog::buildDatesTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    // ── Simulation window ──────────────────────────────────────────────
    auto *winGroup = new QGroupBox(tr("Simulation window"), page);
    auto *winForm  = new QFormLayout(winGroup);

    m_startEdit = new QDateTimeEdit(winGroup);
    m_startEdit->setCalendarPopup(true);
    m_startEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    winForm->addRow(tr("Start:"), m_startEdit);

    m_endEdit = new QDateTimeEdit(winGroup);
    m_endEdit->setCalendarPopup(true);
    m_endEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    winForm->addRow(tr("End:"), m_endEdit);

    m_durationLabel = new QLabel(QStringLiteral("—"), winGroup);
    m_durationLabel->setToolTip(tr("Simulation timespan (End − Start)."));
    winForm->addRow(tr("Duration:"), m_durationLabel);

    m_reportStartEdit = new QDateTimeEdit(winGroup);
    m_reportStartEdit->setCalendarPopup(true);
    m_reportStartEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    winForm->addRow(tr("Report start:"), m_reportStartEdit);

    vlay->addWidget(winGroup);

    // Live sync: clamp report-start ≥ start, refresh duration label.
    connect(m_startEdit, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime &s) {
                m_reportStartEdit->setMinimumDateTime(s);
                if (m_reportStartEdit->dateTime() < s)
                    m_reportStartEdit->setDateTime(s);
                updateDurationLabel();
            });
    connect(m_endEdit, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime &) { updateDurationLabel(); });

    // ── Time steps ─────────────────────────────────────────────────────
    auto *stepGroup = new QGroupBox(tr("Time steps"), page);
    auto *stepForm  = new QFormLayout(stepGroup);

    m_reportStepEdit = new QCustomTimespanEdit(stepGroup);
    m_reportStepEdit->setToolTip(tr("Reporting step (REPORT_STEP)."));
    stepForm->addRow(tr("Reporting step:"), m_reportStepEdit);

    m_dryStepEdit = new QCustomTimespanEdit(stepGroup);
    m_dryStepEdit->setToolTip(tr("Runoff dry-weather step (DRY_STEP)."));
    stepForm->addRow(tr("Dry-weather step:"), m_dryStepEdit);

    m_wetStepEdit = new QCustomTimespanEdit(stepGroup);
    m_wetStepEdit->setToolTip(tr("Runoff wet-weather step (WET_STEP)."));
    stepForm->addRow(tr("Wet-weather step:"), m_wetStepEdit);

    m_ruleStepEdit = new QTimeEdit(stepGroup);
    m_ruleStepEdit->setDisplayFormat(QStringLiteral("HH:mm:ss"));
    m_ruleStepEdit->setTime(QTime(0, 0, 0));
    m_ruleStepEdit->setToolTip(tr("Control-rule evaluation step (RULE_STEP). "
                                   "0 means rules are evaluated every routing step."));
    stepForm->addRow(tr("Control rule step:"), m_ruleStepEdit);

    // Routing step — plain floating-point text box (no spin buttons),
    // displayed in seconds. Engine accepts decimal seconds via ROUTING_STEP.
    m_routingStepEdit = new QLineEdit(stepGroup);
    auto *routingValidator = new QDoubleValidator(0.001, 3600.0, 6, m_routingStepEdit);
    routingValidator->setNotation(QDoubleValidator::StandardNotation);
    m_routingStepEdit->setValidator(routingValidator);
    m_routingStepEdit->setPlaceholderText(QStringLiteral("seconds"));
    m_routingStepEdit->setToolTip(tr("Routing step in seconds (ROUTING_STEP)."));
    stepForm->addRow(tr("Routing step:"), m_routingStepEdit);

    vlay->addWidget(stepGroup);

    // ── Skip steady state ──────────────────────────────────────────────
    // LAT_FLOW_TOL / SYS_FLOW_TOL only matter when SKIP_STEADY_STATE is on
    // — engine treats them as the change thresholds for declaring a period
    // "steady". Grouping the three together makes the dependency clear.
    auto *skipGroup = new QGroupBox(tr("Skip steady state"), page);
    auto *skipForm  = new QFormLayout(skipGroup);

    m_skipSteadyBox = new QCheckBox(tr("Skip steady-periods (SKIP_STEADY_STATE)"),
                                     skipGroup);
    skipForm->addRow(QString(), m_skipSteadyBox);

    m_latFlowTolSpin = new QDoubleSpinBox(skipGroup);
    m_latFlowTolSpin->setRange(0.0, 100.0);
    m_latFlowTolSpin->setSuffix(QStringLiteral(" %"));
    m_latFlowTolSpin->setToolTip(tr("Lateral flow tolerance — engine stores as fraction (LAT_FLOW_TOL)."));
    skipForm->addRow(tr("Lateral flow tol:"), m_latFlowTolSpin);

    m_sysFlowTolSpin = new QDoubleSpinBox(skipGroup);
    m_sysFlowTolSpin->setRange(0.0, 100.0);
    m_sysFlowTolSpin->setSuffix(QStringLiteral(" %"));
    m_sysFlowTolSpin->setToolTip(tr("System flow tolerance (SYS_FLOW_TOL)."));
    skipForm->addRow(tr("System flow tol:"), m_sysFlowTolSpin);

    // Grey out the tolerance spins when skip-steady is off — they remain
    // serialised either way so toggling back on restores the prior values.
    auto syncSkipEnabled = [this]() {
        const bool on = m_skipSteadyBox->isChecked();
        m_latFlowTolSpin->setEnabled(on);
        m_sysFlowTolSpin->setEnabled(on);
    };
    connect(m_skipSteadyBox, &QCheckBox::toggled,
            this, [syncSkipEnabled](bool) { syncSkipEnabled(); });
    syncSkipEnabled();

    vlay->addWidget(skipGroup);

    // ── Sweep / antecedent ─────────────────────────────────────────────
    auto *sweepGroup = new QGroupBox(tr("Sweep / antecedent"), page);
    auto *sweepForm  = new QFormLayout(sweepGroup);

    // SWEEP_START / SWEEP_END are MM/DD only — use a fixed year (2000, a
    // leap year so 02/29 stays selectable) internally and strip it on
    // write. Matches the legacy SWMM-GUI Delphi convention.
    m_sweepStartEdit = new QDateEdit(QDate(2000, 1, 1), sweepGroup);
    m_sweepStartEdit->setDisplayFormat(QStringLiteral("MM/dd"));
    m_sweepStartEdit->setCalendarPopup(true);
    m_sweepStartEdit->setToolTip(tr("Street-sweeping season start (SWEEP_START, MM/DD)."));
    sweepForm->addRow(tr("Start sweeping on:"), m_sweepStartEdit);

    m_sweepEndEdit = new QDateEdit(QDate(2000, 12, 31), sweepGroup);
    m_sweepEndEdit->setDisplayFormat(QStringLiteral("MM/dd"));
    m_sweepEndEdit->setCalendarPopup(true);
    m_sweepEndEdit->setToolTip(tr("Street-sweeping season end (SWEEP_END, MM/DD)."));
    sweepForm->addRow(tr("End sweeping on:"), m_sweepEndEdit);

    m_dryDaysSpin = new QDoubleSpinBox(sweepGroup);
    m_dryDaysSpin->setRange(0.0, 3650.0);
    m_dryDaysSpin->setDecimals(2);
    m_dryDaysSpin->setSuffix(QStringLiteral(" d"));
    sweepForm->addRow(tr("Antecedent dry days:"), m_dryDaysSpin);

    vlay->addWidget(sweepGroup);

    // ── Events ([EVENTS] section editor, Slice CW) ─────────────────────
    // Mirrors SWMM 5.2's [EVENTS] block: a list of {start, end} windows the
    // engine treats as routing-active periods when SKIP_STEADY_STATE is YES
    // (see swmm_is_between_events in openswmm_engine.h:301).  Two columns:
    // Start and End.  Cells use a QDateTimeEdit inline editor.  HH:MM
    // precision (legacy SWMM 5 parity, decided 2026-05-21).
    auto *evGroup = new QGroupBox(tr("Events ([EVENTS])"), page);
    auto *evLay   = new QVBoxLayout(evGroup);
    evGroup->setToolTip(tr(
        "Optional list of routing-active time windows. When Skip Steady State "
        "is on the engine routes full dynamic-wave hydraulics only inside "
        "these windows."));

    m_eventsTable = new QTableWidget(0, 2, evGroup);
    m_eventsTable->setHorizontalHeaderLabels(
        {tr("Start (MM/DD/YYYY HH:MM)"), tr("End (MM/DD/YYYY HH:MM)")});
    m_eventsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_eventsTable->verticalHeader()->setVisible(false);
    m_eventsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_eventsTable->setEditTriggers(QAbstractItemView::AllEditTriggers);
    evLay->addWidget(m_eventsTable);

    auto *evBtnRow = new QHBoxLayout();
    m_eventsAddBtn    = new QPushButton(tr("Add row"),         evGroup);
    m_eventsRemoveBtn = new QPushButton(tr("Remove selected"), evGroup);
    m_eventsRemoveBtn->setEnabled(false);
    evBtnRow->addWidget(m_eventsAddBtn);
    evBtnRow->addWidget(m_eventsRemoveBtn);
    evBtnRow->addStretch();
    evLay->addLayout(evBtnRow);

    connect(m_eventsAddBtn,    &QPushButton::clicked,
            this, &SimulationOptionsDialog::addEventRow);
    connect(m_eventsRemoveBtn, &QPushButton::clicked,
            this, &SimulationOptionsDialog::removeSelectedEventRows);
    connect(m_eventsTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_eventsRemoveBtn->setEnabled(
            !m_eventsTable->selectedItems().isEmpty());
    });

    vlay->addWidget(evGroup);
    vlay->addStretch();

    return page;
}

QWidget *SimulationOptionsDialog::buildHydraulicsTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    // ── Surcharge group ────────────────────────────────────────────────
    auto *surGroup = new QGroupBox(tr("Surcharge handling"), page);
    auto *surForm  = new QFormLayout(surGroup);

    m_surchargeCombo = new QComboBox(surGroup);
    m_surchargeCombo->addItem(tr("EXTRAN (legacy)"),  QStringLiteral("EXTRAN"));
    m_surchargeCombo->addItem(tr("SLOT (Preissmann)"), QStringLiteral("SLOT"));
    m_surchargeCombo->addItem(tr("DYNAMIC_SLOT"),     QStringLiteral("DYNAMIC_SLOT"));
    m_surchargeCombo->setToolTip(
        tr("Method for handling surcharged conduits (option SURCHARGE_METHOD)."));
    surForm->addRow(tr("Method:"), m_surchargeCombo);

    // DPS_* parameters — only meaningful for DYNAMIC_SLOT.
    m_dpsCelerSpin = new QDoubleSpinBox(surGroup);
    m_dpsCelerSpin->setRange(0.1, 1000.0);
    m_dpsCelerSpin->setDecimals(2);
    m_dpsCelerSpin->setSuffix(QStringLiteral(" m/s"));
    m_dpsCelerSpin->setToolTip(tr("DYNAMIC_SLOT target wave celerity (DPS_CELERITY)."));
    surForm->addRow(tr("DPS celerity:"), m_dpsCelerSpin);

    m_dpsAlphaSpin = new QDoubleSpinBox(surGroup);
    m_dpsAlphaSpin->setRange(2.0, 100.0);
    m_dpsAlphaSpin->setDecimals(3);
    m_dpsAlphaSpin->setToolTip(tr("DYNAMIC_SLOT alpha exponent (DPS_ALPHA, ≥ 2)."));
    surForm->addRow(tr("DPS alpha:"), m_dpsAlphaSpin);

    m_dpsDecaySpin = new QDoubleSpinBox(surGroup);
    m_dpsDecaySpin->setRange(0.0, 60.0);
    m_dpsDecaySpin->setDecimals(3);
    m_dpsDecaySpin->setSuffix(QStringLiteral(" s"));
    m_dpsDecaySpin->setToolTip(tr("DYNAMIC_SLOT decay time (DPS_DECAY_TIME)."));
    surForm->addRow(tr("DPS decay:"), m_dpsDecaySpin);

    vlay->addWidget(surGroup);
    connect(m_surchargeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ updateSurchargeFieldsEnabled(); });

    // ── Solver group ───────────────────────────────────────────────────
    auto *solGroup = new QGroupBox(tr("Solver"), page);
    auto *solForm  = new QFormLayout(solGroup);

    m_nodeContinuityCombo = new QComboBox(solGroup);
    m_nodeContinuityCombo->addItem(tr("Explicit (legacy)"),       QStringLiteral("EXPLICIT"));
    m_nodeContinuityCombo->addItem(tr("Semi-implicit (new)"),     QStringLiteral("SEMI_IMPLICIT"));
    m_nodeContinuityCombo->setToolTip(
        tr("Node continuity scheme (option NODE_CONTINUITY)."));
    solForm->addRow(tr("Node continuity:"), m_nodeContinuityCombo);

    m_andersonAccelBox = new QCheckBox(tr("Anderson acceleration (ANDERSON_ACCEL)"), solGroup);
    m_andersonAccelBox->setToolTip(
        tr("Anderson acceleration of the iterative solver — typical 25–50% iteration reduction."));
    solForm->addRow(QString(), m_andersonAccelBox);

    m_maxTrialsSpin = new QSpinBox(solGroup);
    m_maxTrialsSpin->setRange(1, 100);
    m_maxTrialsSpin->setToolTip(tr("Max iterations per routing step (MAX_TRIALS)."));
    solForm->addRow(tr("Max trials:"), m_maxTrialsSpin);

    m_headTolSpin = new QDoubleSpinBox(solGroup);
    m_headTolSpin->setRange(0.000001, 1.0);
    m_headTolSpin->setDecimals(6);
    m_headTolSpin->setToolTip(tr("Head convergence tolerance (HEAD_TOLERANCE)."));
    solForm->addRow(tr("Head tolerance:"), m_headTolSpin);

    m_lengtheningSpin = new QDoubleSpinBox(solGroup);
    m_lengtheningSpin->setRange(0.0, 3600.0);
    m_lengtheningSpin->setDecimals(2);
    m_lengtheningSpin->setSuffix(QStringLiteral(" s"));
    m_lengtheningSpin->setToolTip(tr("Conduit lengthening time step (LENGTHENING_STEP)."));
    solForm->addRow(tr("Lengthening step:"), m_lengtheningSpin);

    m_variableStepSpin = new QDoubleSpinBox(solGroup);
    m_variableStepSpin->setRange(0.0, 1.0);
    m_variableStepSpin->setSingleStep(0.05);
    m_variableStepSpin->setDecimals(3);
    m_variableStepSpin->setToolTip(
        tr("Variable timestep Courant safety fraction (VARIABLE_STEP, 0 disables)."));
    solForm->addRow(tr("Variable step factor:"), m_variableStepSpin);

    vlay->addWidget(solGroup);

    // ── Conduit / channel group ────────────────────────────────────────
    auto *condGroup = new QGroupBox(tr("Conduit / channel"), page);
    auto *condForm  = new QFormLayout(condGroup);

    m_forceMainCombo = new QComboBox(condGroup);
    m_forceMainCombo->addItem(tr("Hazen-Williams (H-W)"), QStringLiteral("H-W"));
    m_forceMainCombo->addItem(tr("Darcy-Weisbach (D-W)"), QStringLiteral("D-W"));
    m_forceMainCombo->setToolTip(tr("Force-main friction equation (FORCE_MAIN_EQUATION)."));
    condForm->addRow(tr("Force-main equation:"), m_forceMainCombo);

    m_normalFlowCombo = new QComboBox(condGroup);
    m_normalFlowCombo->addItem(tr("Slope"),   QStringLiteral("SLOPE"));
    m_normalFlowCombo->addItem(tr("Froude"),  QStringLiteral("FROUDE"));
    m_normalFlowCombo->addItem(tr("Both"),    QStringLiteral("BOTH"));
    m_normalFlowCombo->addItem(tr("Neither"), QStringLiteral("NEITHER"));
    m_normalFlowCombo->setToolTip(tr("Normal-flow limiter criterion (NORMAL_FLOW_LIMITED)."));
    condForm->addRow(tr("Normal-flow criterion:"), m_normalFlowCombo);

    m_inertialDampCombo = new QComboBox(condGroup);
    m_inertialDampCombo->addItem(tr("None"),    QStringLiteral("NONE"));
    m_inertialDampCombo->addItem(tr("Partial"), QStringLiteral("PARTIAL"));
    m_inertialDampCombo->addItem(tr("Full"),    QStringLiteral("FULL"));
    m_inertialDampCombo->setToolTip(tr("Inertial-term damping in dynamic-wave routing (INERTIAL_DAMPING)."));
    condForm->addRow(tr("Inertial damping:"), m_inertialDampCombo);

    m_minSurfAreaSpin = new QDoubleSpinBox(condGroup);
    m_minSurfAreaSpin->setRange(0.0, 1.0e6);
    m_minSurfAreaSpin->setDecimals(4);
    m_minSurfAreaSpin->setToolTip(
        tr("Minimum nodal surface area used in dynamic-wave routing (MIN_SURFAREA)."));
    condForm->addRow(tr("Min surface area:"), m_minSurfAreaSpin);

    m_minSlopeSpin = new QDoubleSpinBox(condGroup);
    m_minSlopeSpin->setRange(0.0, 100.0);
    m_minSlopeSpin->setDecimals(4);
    m_minSlopeSpin->setSuffix(QStringLiteral(" %"));
    m_minSlopeSpin->setToolTip(tr("Minimum conduit slope (MIN_SLOPE)."));
    condForm->addRow(tr("Min conduit slope:"), m_minSlopeSpin);

    vlay->addWidget(condGroup);
    vlay->addStretch();

    return page;
}

QWidget *SimulationOptionsDialog::buildPerformanceTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *threadsGroup = new QGroupBox(tr("Parallelisation"), page);
    auto *threadsForm  = new QFormLayout(threadsGroup);

    m_threadsSpin = new QSpinBox(threadsGroup);
    m_threadsSpin->setRange(0, 256);
    m_threadsSpin->setSpecialValueText(tr("auto"));
    m_threadsSpin->setToolTip(tr(
        "Number of OpenMP worker threads for hydraulics + 2D solvers.\n"
        "0 = auto (engine picks based on conduit count).\n"
        "1 = serial. Higher values cap the OMP team."));
    threadsForm->addRow(tr("Worker threads:"), m_threadsSpin);

    auto *note = new QLabel(
        tr("<i>The IGNORE_* skip-process flags live on the Models / Processes tab. "
           "Future slices add more performance knobs here.</i>"),
        threadsGroup);
    note->setWordWrap(true);
    threadsForm->addRow(note);

    vlay->addWidget(threadsGroup);
    vlay->addStretch();

    return page;
}

void SimulationOptionsDialog::updateSurchargeFieldsEnabled()
{
    if (!m_surchargeCombo) return;
    const bool dyn = m_surchargeCombo->currentData().toString()
                        == QStringLiteral("DYNAMIC_SLOT");
    if (m_dpsCelerSpin) m_dpsCelerSpin->setEnabled(dyn);
    if (m_dpsAlphaSpin) m_dpsAlphaSpin->setEnabled(dyn);
    if (m_dpsDecaySpin) m_dpsDecaySpin->setEnabled(dyn);
}

void SimulationOptionsDialog::updateDurationLabel()
{
    if (!m_durationLabel || !m_startEdit || !m_endEdit) return;
    const qint64 secs = m_startEdit->dateTime().secsTo(m_endEdit->dateTime());
    if (secs <= 0) {
        m_durationLabel->setText(QStringLiteral("—"));
        return;
    }
    const qint64 days  = secs / 86400;
    const qint64 hours = (secs % 86400) / 3600;
    const qint64 mins  = (secs % 3600) / 60;
    const qint64 ss    = secs % 60;
    m_durationLabel->setText(
        QString::asprintf("%lldd %02lld:%02lld:%02lld",
                          static_cast<long long>(days),
                          static_cast<long long>(hours),
                          static_cast<long long>(mins),
                          static_cast<long long>(ss)));
}

// ---------------------------------------------------------------------------
// Tab 5 — Spatial & CRS
// ---------------------------------------------------------------------------

QWidget *SimulationOptionsDialog::buildSpatialTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *crsGroup = new QGroupBox(tr("Coordinate reference system"), page);
    auto *crsForm  = new QFormLayout(crsGroup);

    auto *crsRow = new QWidget(crsGroup);
    auto *crsRowLay = new QHBoxLayout(crsRow);
    crsRowLay->setContentsMargins(0, 0, 0, 0);
    m_crsLabel = new QLabel(tr("(unknown)"), crsRow);
    m_crsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_crsChangeButton = new QToolButton(crsRow);
    m_crsChangeButton->setText(tr("Change…"));
    m_crsChangeButton->setToolTip(tr("Open the CRS picker (writes to swmm_spatial_set_crs)."));
    m_crsDetectButton = new QToolButton(crsRow);
    m_crsDetectButton->setText(tr("Detect from coordinates"));
    m_crsDetectButton->setToolTip(tr(
        "Inspect the model's coordinate ranges and suggest EPSG:4326 if all\n"
        "coordinates fall within geographic bounds (±180° lon, ±85° lat)."));
    crsRowLay->addWidget(m_crsLabel, 1);
    crsRowLay->addWidget(m_crsChangeButton);
    crsRowLay->addWidget(m_crsDetectButton);
    crsForm->addRow(tr("Layer CRS:"), crsRow);

    m_extentLabel = new QLabel(tr("(extent unavailable)"), crsGroup);
    m_extentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_extentLabel->setWordWrap(true);
    crsForm->addRow(tr("Model extent:"), m_extentLabel);

    auto *note = new QLabel(
        tr("<i>Changing the CRS here updates the layer's stored CRS only. "
           "To permanently transform stored coordinates, use the canvas-CRS "
           "button on the status bar (Phase 0.7 reproject prompt).</i>"),
        crsGroup);
    note->setWordWrap(true);
    crsForm->addRow(note);

    vlay->addWidget(crsGroup);
    vlay->addStretch();

    return page;

    connect(m_crsChangeButton, &QToolButton::clicked,
            this, &SimulationOptionsDialog::onSpatialPickCRS);
    connect(m_crsDetectButton, &QToolButton::clicked,
            this, &SimulationOptionsDialog::onSpatialDetectCRS);

    if (!m_layer)
    {
        m_crsChangeButton->setEnabled(false);
        m_crsDetectButton->setEnabled(false);
    }
}

void SimulationOptionsDialog::refreshSpatialSummary()
{
    if (!m_crsLabel) return;

    if (m_layer)
    {
        if (auto *srs = m_layer->srs())
        {
            const QString auth = srs->toAuthority();
            m_crsLabel->setText(auth.isEmpty() ? tr("(local)") : auth);
        }
        else
        {
            m_crsLabel->setText(tr("(none)"));
        }

        const MapExtent ext = m_layer->extent();
        if (ext.isValid())
        {
            m_extentLabel->setText(
                tr("X: [%1, %2]   Y: [%3, %4]")
                    .arg(ext.xMin(), 0, 'g', 8)
                    .arg(ext.xMax(), 0, 'g', 8)
                    .arg(ext.yMin(), 0, 'g', 8)
                    .arg(ext.yMax(), 0, 'g', 8));
        }
        else
        {
            m_extentLabel->setText(tr("(extent invalid / not yet computed)"));
        }
    }
    else
    {
        m_crsLabel->setText(tr("(no layer)"));
        m_extentLabel->setText(tr("(no layer)"));
    }
}

void SimulationOptionsDialog::onSpatialPickCRS()
{
    if (!m_layer) return;
    CRSSelectionDialog dlg(this);
    dlg.setCurrentCRS(m_layer->srs());
    if (dlg.exec() != QDialog::Accepted) return;
    SpatialReferenceSystem *srs = dlg.selectedSRS();
    if (!srs) return;

    m_layer->setSRS(srs, true);
    // Also write the engine's CRS option so it round-trips through .inp save.
    setOption("CRS", srs->toAuthority().isEmpty() ? srs->toWkt() : srs->toAuthority());
    m_wroteChanges = true;
    refreshSpatialSummary();
}

void SimulationOptionsDialog::onSpatialDetectCRS()
{
    if (!m_layer) return;
    const MapExtent ext = m_layer->extent();
    if (!ext.isValid())
        return;

    const bool inGeographic =
        ext.xMin() >= -180.0 && ext.xMax() <=  180.0 &&
        ext.yMin() >=  -90.0 && ext.yMax() <=   90.0;

    if (inGeographic)
    {
        // Suggest EPSG:4326. Update the label as a hint; the user must press
        // Change… to actually apply (so detection is non-destructive).
        m_crsLabel->setText(tr("(suggested: EPSG:4326 — press Change… to apply)"));
    }
    else
    {
        m_crsLabel->setText(tr(
            "(coordinates exceed geographic bounds — pick a projected CRS)"));
    }
}

// ---------------------------------------------------------------------------
// Mesh tab — Slice AU file-management for 2D mesh configurations
// ---------------------------------------------------------------------------

QWidget *SimulationOptionsDialog::buildMeshTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *header = new QLabel(tr(
        "Pick which 2D mesh configuration the engine reads: an external "
        "mesh file (.2dm, referenced via [2D_MESH_FILE]) or the inline mesh "
        "embedded in the project .inp. New meshes are generated from the "
        "editing toolbar's Generate Mesh tool — this tab is purely a "
        "selector for existing configurations."), page);
    header->setWordWrap(true);
    vlay->addWidget(header);

    m_meshDirLabel = new QLabel(page);
    m_meshDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_meshDirLabel->setStyleSheet(QStringLiteral("color: gray;"));
    vlay->addWidget(m_meshDirLabel);

    m_meshList = new QListWidget(page);
    m_meshList->setSelectionMode(QAbstractItemView::SingleSelection);
    vlay->addWidget(m_meshList, 1);

    auto *btnRow = new QHBoxLayout;
    auto *btnSetActive = new QPushButton(tr("Set Active"), page);
    btnSetActive->setToolTip(tr("Patch [2D_MESH_FILE] to point at the "
                                 "selected configuration."));
    auto *btnRemove    = new QPushButton(tr("Remove"), page);
    btnRemove->setToolTip(tr("Delete the selected .2dm from disk."));
    auto *btnRefresh   = new QPushButton(tr("Refresh"), page);
    btnRow->addWidget(btnSetActive);
    btnRow->addWidget(btnRemove);
    btnRow->addStretch();
    btnRow->addWidget(btnRefresh);
    vlay->addLayout(btnRow);

    m_meshActiveLabel = new QLabel(page);
    m_meshActiveLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vlay->addWidget(m_meshActiveLabel);

    connect(btnRefresh, &QPushButton::clicked, this,
            &SimulationOptionsDialog::refreshMeshList);
    connect(btnSetActive, &QPushButton::clicked, this,
            &SimulationOptionsDialog::onMeshSetActive);
    connect(btnRemove, &QPushButton::clicked, this,
            &SimulationOptionsDialog::onMeshRemove);

    refreshMeshList();
    return page;
}

void SimulationOptionsDialog::refreshMeshList()
{
    if (!m_meshList || !m_meshDirLabel || !m_meshActiveLabel) return;
    m_meshList->clear();

    // Search the directory next to the active model (.inp). Without a
    // layer (e.g. dialog opened against a synthesized blank project)
    // we silently no-op — Generate New will create the first mesh.
    QString modelPath;
    if (m_layer) modelPath = m_layer->modelFilePath();
    const QFileInfo modelFi(modelPath);
    const QDir dir = modelPath.isEmpty() ? QDir() : modelFi.absoluteDir();

    m_meshDirLabel->setText(modelPath.isEmpty()
        ? tr("Search directory: <none — save the project first>")
        : tr("Search directory: %1").arg(dir.absolutePath()));

    // Read the .inp once: needed both to discover an inline mesh (embedded
    // [2D_*] sections, no sibling .2dm) and to read the current
    // [2D_MESH_FILE] reference.
    QString inpText;
    if (!modelPath.isEmpty())
    {
        QFile f(modelPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            inpText = QString::fromUtf8(f.readAll());
    }

    // External configurations: one row per sibling *.2dm file.
    if (!modelPath.isEmpty())
    {
        const QStringList meshes = dir.entryList(
            QStringList{QStringLiteral("*.2dm")},
            QDir::Files | QDir::Readable, QDir::Name);
        for (const QString &name : meshes)
        {
            auto *item = new QListWidgetItem(name);
            item->setData(kMeshKindRole, kMeshExternal);
            m_meshList->addItem(item);
        }
    }

    // Inline configuration: the engine reads mesh geometry straight from the
    // .inp when [2D_VERTICES] + [2D_TRIANGLES] are present. Surface it as a
    // selectable row (pinned to the top) so a freshly-generated inline mesh
    // appears here and the user can switch back to it from an external file.
    const bool hasInline =
        inpText.indexOf(QStringLiteral("[2D_VERTICES]"),  0, Qt::CaseInsensitive) >= 0 &&
        inpText.indexOf(QStringLiteral("[2D_TRIANGLES]"), 0, Qt::CaseInsensitive) >= 0;
    if (hasInline)
    {
        auto *item = new QListWidgetItem(
            tr("(Inline mesh — embedded in project .inp)"));
        item->setData(kMeshKindRole, kMeshInline);
        m_meshList->insertItem(0, item);
    }

    // Probe the .inp for a current [2D_MESH_FILE] reference. Tolerant — the
    // section may be absent (engine reads the inline mesh, if any).
    QString active;
    {
        const int sectIdx = inpText.indexOf(QStringLiteral("[2D_MESH_FILE]"),
                                             0, Qt::CaseInsensitive);
        if (sectIdx >= 0)
        {
            // Walk forward to the first non-comment, non-blank line and pull
            // the FILE token.
            int p = inpText.indexOf(QChar('\n'), sectIdx);
            while (p > 0 && p < inpText.size())
            {
                const int nl = inpText.indexOf(QChar('\n'), p + 1);
                const QString line = inpText.mid(p + 1, (nl < 0 ? inpText.size() : nl) - p - 1).trimmed();
                if (!line.isEmpty() && !line.startsWith(QStringLiteral(";"))
                    && !line.startsWith(QChar('[')))
                {
                    // Format: "FILE  <path>".
                    const auto parts = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                  Qt::SkipEmptyParts);
                    if (parts.size() >= 2 && parts.first().compare(
                            QStringLiteral("FILE"), Qt::CaseInsensitive) == 0)
                        active = parts.mid(1).join(QChar(' '));
                    break;
                }
                if (line.startsWith(QChar('['))) break;  // next section
                if (nl < 0) break;
                p = nl;
            }
        }
    }

    // The active configuration is the external file when [2D_MESH_FILE] is
    // present, otherwise the inline mesh (if any). Reflect that in the label
    // and pre-select the matching row.
    if (!active.isEmpty())
    {
        m_meshActiveLabel->setText(tr("Active mesh reference: %1").arg(active));
        const QString activeName = QFileInfo(active).fileName();
        for (int i = 0; i < m_meshList->count(); ++i)
            if (m_meshList->item(i)->data(kMeshKindRole).toInt() == kMeshExternal
                && m_meshList->item(i)->text() == activeName)
                m_meshList->setCurrentRow(i);
    }
    else if (hasInline)
    {
        m_meshActiveLabel->setText(
            tr("Active mesh: inline mesh embedded in project .inp"));
        for (int i = 0; i < m_meshList->count(); ++i)
            if (m_meshList->item(i)->data(kMeshKindRole).toInt() == kMeshInline)
                m_meshList->setCurrentRow(i);
    }
    else
    {
        m_meshActiveLabel->setText(
            tr("Active mesh reference: <none — generate a 2D mesh first>"));
    }
}

void SimulationOptionsDialog::onMeshSetActive()
{
    if (!m_meshList || !m_layer) return;

    QListWidgetItem *item = m_meshList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Set Active Mesh"),
            tr("Select a mesh (.2dm) from the list first."));
        return;
    }

    const QString modelPath = m_layer->modelFilePath();
    if (modelPath.isEmpty()) {
        QMessageBox::warning(this, tr("Set Active Mesh"),
            tr("Save the project first — the [2D_MESH_FILE] reference is "
               "written into the .inp on disk."));
        return;
    }

    QString err;
    if (item->data(kMeshKindRole).toInt() == kMeshInline)
    {
        // Inline mesh: drop any [2D_MESH_FILE] reference so the engine reads
        // the mesh sections embedded directly in the .inp.
        if (!mesh::InpMeshWriter::clearMeshFileRef(modelPath, &err)) {
            QMessageBox::critical(this, tr("Set Active Mesh"),
                tr("Could not switch to the inline mesh:\n%1").arg(err));
            return;
        }
        // Mirror into the engine's in-memory model so a save doesn't re-add a
        // stale reference. Empty clears it (engine reverts to inline mesh).
        if (m_engine)
            swmm_options_set_ext(m_engine, "MESH_FILE", "");
    }
    else
    {
        // External mesh: point [2D_MESH_FILE] at the selected .2dm.
        const QString meshPath =
            QFileInfo(modelPath).absoluteDir().absoluteFilePath(item->text());
        if (!mesh::InpMeshWriter::writeMeshFileRef(modelPath, meshPath, &err)) {
            QMessageBox::critical(this, tr("Set Active Mesh"),
                tr("Could not update [2D_MESH_FILE]:\n%1").arg(err));
            return;
        }
        // Mirror the reference into the engine's in-memory model. Without this
        // the engine re-serialises the .inp on the next save with mesh_file
        // empty and drops [2D_MESH_FILE] — the model silently reverts to 1D.
        if (m_engine)
            swmm_options_set_ext(m_engine, "MESH_FILE",
                                 item->text().toUtf8().constData());
    }

    // Selecting an active mesh implies the user wants 2D on. Flip the
    // module checkbox so the corresponding tab + persistence follow.
    if (m_module2DBox && !m_module2DBox->isChecked())
        m_module2DBox->setChecked(true);

    refreshMeshList();
}

void SimulationOptionsDialog::onMeshRemove()
{
    if (!m_meshList || !m_layer) return;

    QListWidgetItem *item = m_meshList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Remove Mesh"),
            tr("Select a mesh (.2dm) from the list first."));
        return;
    }

    if (item->data(kMeshKindRole).toInt() == kMeshInline) {
        QMessageBox::information(this, tr("Remove Mesh"),
            tr("The inline mesh is embedded in the project .inp — it can't be "
               "deleted from here. Re-generate the mesh, or set an external "
               ".2dm active, to replace it."));
        return;
    }

    const QString modelPath = m_layer->modelFilePath();
    if (modelPath.isEmpty()) return;

    const QString name     = item->text();
    const QString meshPath =
        QFileInfo(modelPath).absoluteDir().absoluteFilePath(name);

    // Warn (but allow) when deleting the file the .inp currently references —
    // the [2D_MESH_FILE] block survives and would dangle until retargeted.
    const bool isActive = m_meshActiveLabel &&
        m_meshActiveLabel->text().contains(name);
    const QString question = isActive
        ? tr("\"%1\" is the active [2D_MESH_FILE] reference. Deleting it will "
             "leave the .inp pointing at a missing file until you set a new "
             "active mesh.\n\nDelete it anyway?").arg(name)
        : tr("Delete \"%1\" from disk? This cannot be undone.").arg(name);

    if (QMessageBox::question(this, tr("Remove Mesh"), question,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;

    QFile f(meshPath);
    if (f.exists() && !f.remove()) {
        QMessageBox::critical(this, tr("Remove Mesh"),
            tr("Could not delete %1:\n%2").arg(meshPath, f.errorString()));
        return;
    }

    refreshMeshList();
}

void SimulationOptionsDialog::on2DModuleToggled(bool enabled)
{
    // Only the 2D Surface Routing solver-parameter page follows the module
    // toggle. The Mesh page is always interactive — mesh creation is what
    // flips the module on, so gating it here would be circular.
#ifdef OPENSWMM_HAS_2D
    set2DRowEnabled(enabled);
#else
    Q_UNUSED(enabled);
#endif
}

#ifdef OPENSWMM_HAS_2D

// ---------------------------------------------------------------------------
// Tab 6 — 2D Surface Routing  (only present when the engine ships the 2D module)
// ---------------------------------------------------------------------------

QWidget *SimulationOptionsDialog::build2DTab()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    auto *cvodeGroup = new QGroupBox(tr("CVODE solver"), page);
    auto *cvodeForm  = new QFormLayout(cvodeGroup);

    m_cvodeMaxStepSpin = new QDoubleSpinBox(cvodeGroup);
    m_cvodeMaxStepSpin->setRange(0.001, 3600.0);
    m_cvodeMaxStepSpin->setDecimals(4);
    m_cvodeMaxStepSpin->setSuffix(QStringLiteral(" s"));
    cvodeForm->addRow(tr("Max timestep:"), m_cvodeMaxStepSpin);

    m_cvodeMinStepSpin = new QDoubleSpinBox(cvodeGroup);
    m_cvodeMinStepSpin->setRange(1e-9, 60.0);
    m_cvodeMinStepSpin->setDecimals(9);
    m_cvodeMinStepSpin->setSuffix(QStringLiteral(" s"));
    cvodeForm->addRow(tr("Min timestep:"), m_cvodeMinStepSpin);

    m_cvodeRelTolSpin = new QDoubleSpinBox(cvodeGroup);
    m_cvodeRelTolSpin->setRange(1e-12, 1.0);
    m_cvodeRelTolSpin->setDecimals(12);
    cvodeForm->addRow(tr("Relative tolerance:"), m_cvodeRelTolSpin);

    m_cvodeAbsTolSpin = new QDoubleSpinBox(cvodeGroup);
    m_cvodeAbsTolSpin->setRange(1e-12, 1.0);
    m_cvodeAbsTolSpin->setDecimals(12);
    cvodeForm->addRow(tr("Absolute tolerance:"), m_cvodeAbsTolSpin);

    m_cvodeMaxStepsSpin = new QSpinBox(cvodeGroup);
    m_cvodeMaxStepsSpin->setRange(1, 100000);
    cvodeForm->addRow(tr("Max CVODE steps:"), m_cvodeMaxStepsSpin);

    vlay->addWidget(cvodeGroup);

    auto *meshGroup = new QGroupBox(tr("Mesh"), page);
    auto *meshForm  = new QFormLayout(meshGroup);

    m_dryDepthSpin = new QDoubleSpinBox(meshGroup);
    m_dryDepthSpin->setRange(0.0, 1.0);
    m_dryDepthSpin->setDecimals(6);
    m_dryDepthSpin->setSuffix(QStringLiteral(" m"));
    meshForm->addRow(tr("Dry depth threshold:"), m_dryDepthSpin);

    m_limiterEpsSpin = new QDoubleSpinBox(meshGroup);
    m_limiterEpsSpin->setRange(0.0, 1.0);
    m_limiterEpsSpin->setDecimals(9);
    meshForm->addRow(tr("Limiter epsilon:"), m_limiterEpsSpin);

    m_fluxDhEpsSpin = new QDoubleSpinBox(meshGroup);
    m_fluxDhEpsSpin->setRange(0.0, 1.0);
    m_fluxDhEpsSpin->setDecimals(6);
    m_fluxDhEpsSpin->setSuffix(QStringLiteral(" m"));
    m_fluxDhEpsSpin->setToolTip(
        tr("Head-difference regularization for the 2D diffusive-wave flux. "
           "0.004 m is the current recommended default from the road/weir "
           "performance trials."));
    meshForm->addRow(tr("Flux head epsilon:"), m_fluxDhEpsSpin);

    vlay->addWidget(meshGroup);

    auto *closureGroup = new QGroupBox(tr("Cell closure (wetting / drying)"), page);
    auto *closureForm  = new QFormLayout(closureGroup);

    m_cellClosureCombo = new QComboBox(closureGroup);
    m_cellClosureCombo->addItem(tr("Flat (η = z̄ + V/A, legacy)"), QStringLiteral("FLAT"));
    m_cellClosureCombo->addItem(tr("VFR (planar-bed volume/free-surface)"),
                                QStringLiteral("VFR"));
    m_cellClosureCombo->setToolTip(
        tr("How the free-surface elevation of a partially wet cell is reconstructed "
           "from its stored volume. Flat overstates the surface on slope/step cells "
           "(water can climb uphill and strand on slopes); VFR (Begnudelli & Sanders) "
           "uses the exact planar-bed relation so a lake at rest stays at rest. "
           "CPU solvers only — the GPU backend falls back to Flat."));
    closureForm->addRow(tr("Cell closure:"), m_cellClosureCombo);

    m_faceReconCombo = new QComboBox(closureGroup);
    m_faceReconCombo->addItem(tr("Mean (upwind cell depth, legacy)"),
                              QStringLiteral("MEAN"));
    m_faceReconCombo->addItem(tr("VFR face (edge depth + wetting gate)"),
                              QStringLiteral("VFR_FACE"));
    m_faceReconCombo->setToolTip(
        tr("Effective conveyance depth at a shared edge. Mean uses the upwind cell's "
           "mean depth; VFR face reconstructs the depth at the edge from the upwind "
           "surface and the edge's bed elevations, blocking flow across an edge whose "
           "bed is above the water (kills uphill creep and slope stranding). "
           "Best paired with the VFR cell closure."));
    closureForm->addRow(tr("Face reconstruction:"), m_faceReconCombo);

    m_vfrMinWetFracSpin = new QDoubleSpinBox(closureGroup);
    m_vfrMinWetFracSpin->setRange(0.0001, 0.5);
    m_vfrMinWetFracSpin->setDecimals(4);
    m_vfrMinWetFracSpin->setSingleStep(0.01);
    m_vfrMinWetFracSpin->setToolTip(
        tr("Wetted-area-fraction floor ε that regularizes the VFR closure for the "
           "implicit solver as a cell dries (bounds dη/dV). Only used when Cell "
           "closure = VFR. Default 0.01; raise (0.02–0.05) if the 2D solver reports "
           "convergence failures on strongly wetting/drying models."));
    closureForm->addRow(tr("VFR min wet fraction:"), m_vfrMinWetFracSpin);

    vlay->addWidget(closureGroup);

    auto *coupGroup = new QGroupBox(tr("1D ↔ 2D coupling"), page);
    auto *coupForm  = new QFormLayout(coupGroup);

    m_couplingCdSpin = new QDoubleSpinBox(coupGroup);
    m_couplingCdSpin->setRange(0.0, 1.0);
    m_couplingCdSpin->setDecimals(4);
    coupForm->addRow(tr("Coupling Cd:"), m_couplingCdSpin);

    // COUPLING_INTERVAL is an integer count of routing steps, not a time.
    // 0 and 1 both mean "advance the 2D solver every routing step"; values >= 2
    // defer the advance into an N-step macro-window (experimental). Editable so
    // an arbitrary N can still be typed while 0/1 read as plain text.
    m_couplingIntervalCombo = new QComboBox(coupGroup);
    m_couplingIntervalCombo->setEditable(true);
    m_couplingIntervalCombo->setInsertPolicy(QComboBox::NoInsert);
    m_couplingIntervalCombo->addItem(tr("Every routing step"),          0);
    m_couplingIntervalCombo->addItem(tr("Every 2 steps (macro-window)"), 2);
    m_couplingIntervalCombo->addItem(tr("Every 5 steps (macro-window)"), 5);
    m_couplingIntervalCombo->addItem(tr("Every 10 steps (macro-window)"), 10);
    if (auto *le = m_couplingIntervalCombo->lineEdit())
        le->setValidator(new QIntValidator(0, 3600, m_couplingIntervalCombo));
    m_couplingIntervalCombo->setToolTip(
        tr("How often the 2D surface solver is advanced, in 1D routing steps. "
           "0 or 1 = every step (recommended). Values ≥ 2 defer the advance "
           "into an N-step macro-window for speed; this is experimental and "
           "CFL-limited — verify the 2D continuity error in the report."));
    coupForm->addRow(tr("Coupling interval:"), m_couplingIntervalCombo);

    vlay->addWidget(coupGroup);

    auto *solverGroup = new QGroupBox(tr("Linear solver"), page);
    auto *solverForm  = new QFormLayout(solverGroup);

    m_linearSolverCombo = new QComboBox(solverGroup);
    m_linearSolverCombo->addItem(tr("GMRES"),    QStringLiteral("GMRES"));
    m_linearSolverCombo->addItem(tr("BICGSTAB"), QStringLiteral("BICGSTAB"));
    m_linearSolverCombo->addItem(tr("TFQMR"),    QStringLiteral("TFQMR"));
    solverForm->addRow(tr("Solver:"), m_linearSolverCombo);

    m_preconditionerCombo = new QComboBox(solverGroup);
    m_preconditionerCombo->addItem(tr("None"),            QStringLiteral("NONE"));
    m_preconditionerCombo->addItem(tr("Jacobi"),          QStringLiteral("JACOBI"));
    m_preconditionerCombo->addItem(tr("ILU"),             QStringLiteral("ILU"));
    m_preconditionerCombo->addItem(tr("AMG (BoomerAMG)"), QStringLiteral("AMG"));
    solverForm->addRow(tr("Preconditioner:"), m_preconditionerCombo);

    m_maxKrylovDimSpin = new QSpinBox(solverGroup);
    m_maxKrylovDimSpin->setRange(1, 1000);
    solverForm->addRow(tr("Max Krylov dim:"), m_maxKrylovDimSpin);

    vlay->addWidget(solverGroup);

    auto *rainfallGroup = new QGroupBox(tr("Rainfall"), page);
    auto *rainfallForm  = new QFormLayout(rainfallGroup);

    m_rainfall2DModeCombo = new QComboBox(rainfallGroup);
    m_rainfall2DModeCombo->addItem(tr("Natural neighbour (all gages)"),
                                   QStringLiteral("NATURAL_NEIGHBOUR"));
    m_rainfall2DModeCombo->addItem(tr("System (uniform gage mean)"),
                                   QStringLiteral("SYSTEM"));
    m_rainfall2DModeCombo->setToolTip(
        tr("How raingage rainfall drives the 2D mesh. Natural neighbour spatially "
           "interpolates all located gages onto each cell (inverse-distance "
           "outside the gage hull); System applies one uniform value — the mean "
           "of all gages."));
    rainfallForm->addRow(tr("Rainfall mode:"), m_rainfall2DModeCombo);

    vlay->addWidget(rainfallGroup);

    m_report2DBox = new QCheckBox(tr("Write 2D results to output (REPORT_2D)"), page);
    vlay->addWidget(m_report2DBox);

    vlay->addStretch();

    return page;
}

void SimulationOptionsDialog::read2DFromEngine()
{
    bool ok = false;
    auto getExt = [&](const char *key, const QString &fallback) -> QString {
        if (!m_engine) return fallback;
        char buf[256] = {};
        if (swmm_options_get_ext(m_engine, key, buf, sizeof(buf)) == 0)
            return QString::fromUtf8(buf).trimmed();
        return fallback;
    };

    m_cvodeMaxStepSpin ->setValue(getExt("MAX_TIMESTEP",      "10").toDouble(&ok));
    m_cvodeMinStepSpin ->setValue(getExt("MIN_TIMESTEP",      "0.001").toDouble(&ok));
    m_cvodeRelTolSpin  ->setValue(getExt("REL_TOLERANCE",     "1e-4").toDouble(&ok));
    m_cvodeAbsTolSpin  ->setValue(getExt("ABS_TOLERANCE",     "1e-6").toDouble(&ok));
    m_cvodeMaxStepsSpin->setValue(getExt("MAX_CVODE_STEPS",   "500").toInt(&ok));
    m_dryDepthSpin     ->setValue(getExt("DRY_DEPTH",         "0.001").toDouble(&ok));
    m_limiterEpsSpin   ->setValue(getExt("LIMITER_EPSILON",   "1e-6").toDouble(&ok));
    m_fluxDhEpsSpin    ->setValue(getExt("FLUX_DH_EPS",       "0.004").toDouble(&ok));
    m_vfrMinWetFracSpin->setValue(getExt("VFR_MIN_WET_FRAC",  "0.01").toDouble(&ok));
    m_couplingCdSpin   ->setValue(getExt("COUPLING_CD",       "0.65").toDouble(&ok));
    m_couplingIntervalRaw = getExt("COUPLING_INTERVAL", "0").toInt(&ok);
    if (!ok || m_couplingIntervalRaw < 0) m_couplingIntervalRaw = 0;
    if (m_couplingIntervalRaw <= 1) {
        m_couplingIntervalCombo->setCurrentIndex(0);            // "Every routing step"
    } else if (int idx = m_couplingIntervalCombo->findData(m_couplingIntervalRaw); idx >= 0) {
        m_couplingIntervalCombo->setCurrentIndex(idx);          // matched a preset
    } else {
        m_couplingIntervalCombo->setEditText(QString::number(m_couplingIntervalRaw));
    }

    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0) c->setCurrentIndex(idx);
    };
    selectComboByData(m_cellClosureCombo,    getExt("CELL_CLOSURE",        "FLAT"));
    selectComboByData(m_faceReconCombo,      getExt("FACE_RECONSTRUCTION", "MEAN"));
    selectComboByData(m_linearSolverCombo,   getExt("LINEAR_SOLVER",   "GMRES"));
    selectComboByData(m_preconditionerCombo, getExt("PRECONDITIONER",  "AMG"));
    m_maxKrylovDimSpin->setValue(getExt("MAX_KRYLOV_DIM", "30").toInt(&ok));
    selectComboByData(m_rainfall2DModeCombo, getExt("RAINFALL_MODE", "NATURAL_NEIGHBOUR"));
    m_report2DBox->setChecked(parseEngineBool(getExt("REPORT_2D", "NO")) == Qt::Checked);
}

int SimulationOptionsDialog::write2DToEngine(int &n)
{
    auto getExt = [&](const char *key) -> QString {
        if (!m_engine) return {};
        char buf[256] = {};
        if (swmm_options_get_ext(m_engine, key, buf, sizeof(buf)) == 0)
            return QString::fromUtf8(buf).trimmed();
        return {};
    };
    auto setExt = [&](const char *key, const QString &v) -> bool {
        if (!m_engine) return false;
        return swmm_options_set_ext(m_engine, key, v.toUtf8().constData()) == 0;
    };
    auto writeIfChanged = [&](const char *key, const QString &cur, const QString &nv) {
        if (cur == nv) return;
        if (setExt(key, nv)) ++n;
    };

    writeIfChanged("MAX_TIMESTEP",      getExt("MAX_TIMESTEP"),
                   QString::number(m_cvodeMaxStepSpin->value(), 'g', 8));
    writeIfChanged("MIN_TIMESTEP",      getExt("MIN_TIMESTEP"),
                   QString::number(m_cvodeMinStepSpin->value(), 'g', 8));
    writeIfChanged("REL_TOLERANCE",     getExt("REL_TOLERANCE"),
                   QString::number(m_cvodeRelTolSpin->value(), 'g', 8));
    writeIfChanged("ABS_TOLERANCE",     getExt("ABS_TOLERANCE"),
                   QString::number(m_cvodeAbsTolSpin->value(), 'g', 8));
    writeIfChanged("MAX_CVODE_STEPS",   getExt("MAX_CVODE_STEPS"),
                   QString::number(m_cvodeMaxStepsSpin->value()));
    writeIfChanged("DRY_DEPTH",         getExt("DRY_DEPTH"),
                   QString::number(m_dryDepthSpin->value(), 'g', 8));
    writeIfChanged("LIMITER_EPSILON",   getExt("LIMITER_EPSILON"),
                   QString::number(m_limiterEpsSpin->value(), 'g', 8));
    writeIfChanged("FLUX_DH_EPS",       getExt("FLUX_DH_EPS"),
                   QString::number(m_fluxDhEpsSpin->value(), 'g', 8));
    writeIfChanged("CELL_CLOSURE",        getExt("CELL_CLOSURE"),
                   m_cellClosureCombo->currentData().toString());
    writeIfChanged("FACE_RECONSTRUCTION", getExt("FACE_RECONSTRUCTION"),
                   m_faceReconCombo->currentData().toString());
    writeIfChanged("VFR_MIN_WET_FRAC",  getExt("VFR_MIN_WET_FRAC"),
                   QString::number(m_vfrMinWetFracSpin->value(), 'g', 6));
    writeIfChanged("COUPLING_CD",       getExt("COUPLING_CD"),
                   QString::number(m_couplingCdSpin->value(), 'f', 4));
    // Resolve the combo (preset or typed) back to an integer step count.
    int couplingInterval = 0;
    {
        const QString text = m_couplingIntervalCombo->currentText().trimmed();
        const int found = m_couplingIntervalCombo->findText(text);
        if (found >= 0) {
            couplingInterval = m_couplingIntervalCombo->itemData(found).toInt();
        } else {
            bool intOk = false;
            couplingInterval = text.toInt(&intOk);
            if (!intOk || couplingInterval < 0) couplingInterval = 0;
        }
        // 0 and 1 are behaviourally identical; keep whichever the model had so an
        // unchanged dialog doesn't rewrite "1" -> "0".
        if (couplingInterval <= 1 && (m_couplingIntervalRaw == 0 || m_couplingIntervalRaw == 1))
            couplingInterval = m_couplingIntervalRaw;
    }
    writeIfChanged("COUPLING_INTERVAL", getExt("COUPLING_INTERVAL"),
                   QString::number(couplingInterval));
    writeIfChanged("LINEAR_SOLVER",     getExt("LINEAR_SOLVER"),
                   m_linearSolverCombo->currentData().toString());
    writeIfChanged("PRECONDITIONER",    getExt("PRECONDITIONER"),
                   m_preconditionerCombo->currentData().toString());
    writeIfChanged("MAX_KRYLOV_DIM",    getExt("MAX_KRYLOV_DIM"),
                   QString::number(m_maxKrylovDimSpin->value()));
    writeIfChanged("RAINFALL_MODE",     getExt("RAINFALL_MODE"),
                   m_rainfall2DModeCombo->currentData().toString());
    writeIfChanged("REPORT_2D",         getExt("REPORT_2D"),
                   engineBoolString(m_report2DBox->isChecked()));
    return n;
}

#endif // OPENSWMM_HAS_2D

// ---------------------------------------------------------------------------
// Engine helpers
// ---------------------------------------------------------------------------

QString SimulationOptionsDialog::getOption(const char *key,
                                           const QString &fallback) const
{
    if (!m_engine) return fallback;
    char buf[256] = {};
    if (swmm_options_get(m_engine, key, buf, sizeof(buf)) == 0)
        return QString::fromUtf8(buf).trimmed();
    return fallback;
}

// ---------------------------------------------------------------------------
// [EVENTS] section helpers (Slice CW — 2026-05-21)
// ---------------------------------------------------------------------------
// oaDateFromQDateTime / qDateTimeFromOaDate are static methods on
// SimulationOptionsDialog defined in simulationoptionshelpers.cpp so the
// leaf QtTest can link them without dragging the spatial-tab OGR cascade.

namespace {

// Wrap a QDateTimeEdit inside a QTableWidget cell.  Centralised so every
// row uses the same display format / calendar policy.  HH:MM precision
// (legacy SWMM 5 parity, decided 2026-05-21).
QDateTimeEdit *makeEventCellEditor(const QDateTime &dt, QWidget *parent)
{
    auto *edit = new QDateTimeEdit(dt, parent);
    edit->setCalendarPopup(true);
    edit->setDisplayFormat(QStringLiteral("MM/dd/yyyy HH:mm"));
    edit->setFrame(false);
    return edit;
}

} // namespace

void SimulationOptionsDialog::addEventRow()
{
    if (!m_eventsTable) return;
    const int row = m_eventsTable->rowCount();
    m_eventsTable->insertRow(row);
    // Default both columns to (project start, project end) so the user only
    // edits the deltas.  Fall back to "now" when the Dates tab edits haven't
    // been populated yet (shouldn't happen — buildDatesTab seeds them).
    const QDateTime defStart = m_startEdit ? m_startEdit->dateTime()
                                           : QDateTime::currentDateTime();
    const QDateTime defEnd   = m_endEdit   ? m_endEdit->dateTime()
                                           : defStart.addDays(1);
    m_eventsTable->setCellWidget(row, 0, makeEventCellEditor(defStart, m_eventsTable));
    m_eventsTable->setCellWidget(row, 1, makeEventCellEditor(defEnd,   m_eventsTable));
}

void SimulationOptionsDialog::removeSelectedEventRows()
{
    if (!m_eventsTable) return;
    // Collect distinct row indices.  selectedRows() returns one QModelIndex
    // per selected row regardless of which column the click landed on.
    // Sort descending so removeRow() doesn't shift the indices we still
    // need to delete.
    QList<int> rows;
    const auto idxs = m_eventsTable->selectionModel()->selectedRows();
    rows.reserve(idxs.size());
    for (const auto &idx : idxs)
        rows.append(idx.row());
    // Fallback: if no full-row selection (user clicked a single cell),
    // fall back to selectedItems() which covers cell selection.
    if (rows.isEmpty()) {
        const auto items = m_eventsTable->selectedItems();
        for (auto *it : items)
            if (!rows.contains(it->row()))
                rows.append(it->row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows)
        m_eventsTable->removeRow(r);
}

void SimulationOptionsDialog::readEventsFromEngine()
{
    if (!m_eventsTable) return;

    // Wipe before refilling — readFromEngine is also called after writeApply
    // to surface engine-normalised values.
    m_eventsTable->setRowCount(0);
    m_eventsSnapshot.clear();

    if (!m_engine) return;

    int count = 0;
    if (swmm_events_count(m_engine, &count) != 0) return;

    for (int i = 0; i < count; ++i) {
        double start = 0.0, end = 0.0;
        if (swmm_events_get(m_engine, i, &start, &end) != 0) continue;
        const QDateTime qs = qDateTimeFromOaDate(start);   // static helper
        const QDateTime qe = qDateTimeFromOaDate(end);     // static helper

        const int row = m_eventsTable->rowCount();
        m_eventsTable->insertRow(row);
        m_eventsTable->setCellWidget(row, 0, makeEventCellEditor(qs, m_eventsTable));
        m_eventsTable->setCellWidget(row, 1, makeEventCellEditor(qe, m_eventsTable));
        m_eventsSnapshot.append(qMakePair(qs, qe));
    }
}

bool SimulationOptionsDialog::validateEvents(QString *warn)
{
    if (!m_eventsTable) return true;

    bool anyInvalid = false;
    QList<QPair<QDateTime, QDateTime>> rows;
    const int n = m_eventsTable->rowCount();
    rows.reserve(n);
    const QString badStyle =
        QStringLiteral("QDateTimeEdit { background-color: #ffc8c8; }");
    for (int r = 0; r < n; ++r) {
        auto *startEdit = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 0));
        auto *endEdit   = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 1));
        if (!startEdit || !endEdit) { anyInvalid = true; continue; }
        const QDateTime s = startEdit->dateTime();
        const QDateTime e = endEdit->dateTime();
        rows.append(qMakePair(s, e));

        const bool bad = !(s < e);
        startEdit->setStyleSheet(bad ? badStyle : QString());
        endEdit  ->setStyleSheet(bad ? badStyle : QString());
        const QString tip = bad ? tr("Start must be earlier than End.")
                                : QString();
        startEdit->setToolTip(tip);
        endEdit  ->setToolTip(tip);
        if (bad) anyInvalid = true;
    }

    if (warn) {
        // Out-of-range check against the simulation window.
        const QDateTime simStart = m_startEdit ? m_startEdit->dateTime() : QDateTime();
        const QDateTime simEnd   = m_endEdit   ? m_endEdit->dateTime()   : QDateTime();
        for (int r = 0; r < rows.size(); ++r) {
            const auto &p = rows[r];
            if (simStart.isValid() && simEnd.isValid()
                && (p.second <= simStart || p.first >= simEnd))
            {
                *warn += tr("Row %1 lies entirely outside the simulation window.\n")
                            .arg(r + 1);
            }
        }
        // Overlap detection: O(n^2) — n is small (typically << 20).
        for (int i = 0; i < rows.size(); ++i)
            for (int j = i + 1; j < rows.size(); ++j)
                if (rows[i].first < rows[j].second &&
                    rows[j].first < rows[i].second)
                {
                    *warn += tr("Rows %1 and %2 overlap.\n")
                                .arg(i + 1).arg(j + 1);
                }
    }

    return !anyInvalid;
}

int SimulationOptionsDialog::writeEventsToEngine()
{
    if (!m_eventsTable || !m_engine) return 0;

    // Snapshot the table into a flat list for diffing against m_eventsSnapshot.
    QList<QPair<QDateTime, QDateTime>> current;
    const int n = m_eventsTable->rowCount();
    current.reserve(n);
    for (int r = 0; r < n; ++r) {
        auto *startEdit = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 0));
        auto *endEdit   = qobject_cast<QDateTimeEdit *>(
            m_eventsTable->cellWidget(r, 1));
        if (!startEdit || !endEdit) continue;
        current.append(qMakePair(startEdit->dateTime(), endEdit->dateTime()));
    }

    if (current == m_eventsSnapshot)
        return 0;   // no change → no write, no dirty flag

    if (swmm_events_clear(m_engine) != 0)
        return 0;

    int written = 0;
    for (const auto &p : current) {
        const double start = oaDateFromQDateTime(p.first);
        const double end   = oaDateFromQDateTime(p.second);
        if (!(start < end)) continue;   // skip invalid rows defensively
        if (swmm_events_add(m_engine, start, end, nullptr) == 0)
            ++written;
    }

    m_eventsSnapshot = current;
    return written;
}

// ---------------------------------------------------------------------------
// Files / Output / Plugins sub-tab validation (Phase 3.10.4 — 2026-05-22)
// ---------------------------------------------------------------------------
//
// Blocking errors:
//   • [PLUGINS] row with empty plugin id (column 0).
//   • Scheduled hot-start save row with empty path (column 0).
//
// Non-blocking warnings (appended to @p warn, surfaced as Yes/No to user):
//   • Report selector with "Selected" radio + empty list → silently
//     collapses to NONE on write; warn the user to make it explicit.
//   • .rpt / .out parent directory missing (typo in path, etc).
//
bool SimulationOptionsDialog::validateFilesTab(QString *warn)
{
    bool anyInvalid = false;

    // ── [PLUGINS] table: empty id rejected ─────────────────────────────
    //
    // Phase 3.10.6 — model-backed.  The blocking error is surfaced via
    // the @p warn channel (previously a per-cell red background, which
    // QAbstractTableModel doesn't expose without an extra role round-
    // trip); anyInvalid still stops Apply.
    if (m_pluginsModel) {
        const int n = m_pluginsModel->rowCount();
        for (int r = 0; r < n; ++r) {
            const QString id = m_pluginsModel->pathAt(r).trimmed();
            if (id.isEmpty()) {
                anyInvalid = true;
                if (warn)
                    warn->append(tr("[PLUGINS] row %1: plugin id / path is required.\n")
                                     .arg(r + 1));
            }
        }
    }

    // ── Hot-start saves table: empty path rejected ─────────────────────
    //
    // Phase 3.10.5 — table is now MVC-backed.  We can't paint per-cell
    // background through the model without an extra role round-trip, so
    // the inline error affordance is the row's row-header text and a
    // tool-tipped warning surfaced via the @p warn channel; the blocking
    // anyInvalid flag still stops Apply.
    if (m_hotstartSavesModel) {
        const int n = m_hotstartSavesModel->rowCount();
        for (int r = 0; r < n; ++r) {
            const QString p = m_hotstartSavesModel->pathAt(r).trimmed();
            if (p.isEmpty()) {
                anyInvalid = true;
                if (warn)
                    warn->append(tr("Hot-start save row %1: path is required.\n")
                                     .arg(r + 1));
            }
        }
    }

    // ── Non-blocking warnings ──────────────────────────────────────────
    if (warn) {
        auto checkSelector = [warn](QRadioButton *some, QLineEdit *list,
                                    const QString &label) {
            if (!some || !list) return;
            if (some->isChecked() && list->text().trimmed().isEmpty()) {
                *warn += SimulationOptionsDialog::tr(
                    "%1: \"Selected\" is chosen but the name list is empty "
                    "(will be written as NONE).\n").arg(label);
            }
        };
        checkSelector(m_rptSubcatchSomeRadio, m_rptSubcatchListEdit,
                      tr("Subcatchments"));
        checkSelector(m_rptNodeSomeRadio,     m_rptNodeListEdit,
                      tr("Nodes"));
        checkSelector(m_rptLinkSomeRadio,     m_rptLinkListEdit,
                      tr("Links"));

        auto checkParentDir = [warn](QLineEdit *edit, const QString &label) {
            if (!edit) return;
            const QString path = edit->text().trimmed();
            if (path.isEmpty()) return;
            const QFileInfo fi(path);
            const QDir parent = fi.absoluteDir();
            if (!parent.exists()) {
                *warn += SimulationOptionsDialog::tr(
                    "%1: parent directory does not exist (%2).\n")
                    .arg(label, QDir::toNativeSeparators(parent.absolutePath()));
            }
        };
        checkParentDir(m_reportFilePathEdit, tr("Report file"));
        checkParentDir(m_outputFilePathEdit, tr("Output file"));
    }

    return !anyInvalid;
}

// ---------------------------------------------------------------------------
// [REPORT] contents editor (Slice BV.1 — 2026-05-22)
// ---------------------------------------------------------------------------
//
// Engine surface: RPT_DISABLED / RPT_INPUT / RPT_CONTINUITY / RPT_FLOWSTATS /
// RPT_CONTROLS / RPT_AVERAGES (booleans, YES/NO) plus RPT_SUBCATCHMENTS /
// RPT_NODES / RPT_LINKS (selectors, "ALL" / "NONE" / "name1,name2,...").

void SimulationOptionsDialog::buildReportContentsGroup(QVBoxLayout *parentLayout,
                                                       QWidget *page)
{
    auto *grp = new QGroupBox(tr("Report contents ([REPORT])"), page);
    auto *vlay = new QVBoxLayout(grp);
    grp->setToolTip(tr(
        "Controls which summary sections and which objects appear in the "
        "simulation report (.rpt) and binary output (.out) files."));

    // ---- bool flags row ------------------------------------------------
    auto *flagsGroup = new QGroupBox(tr("Summary sections"), grp);
    auto *flagsForm  = new QFormLayout(flagsGroup);

    m_rptDisabledBox   = new QCheckBox(tr("Disable all reporting (DISABLED)"),  flagsGroup);
    m_rptInputBox      = new QCheckBox(tr("Echo input summary (INPUT)"),         flagsGroup);
    m_rptContinuityBox = new QCheckBox(tr("Continuity errors (CONTINUITY)"),     flagsGroup);
    m_rptFlowstatsBox  = new QCheckBox(tr("Flow statistics (FLOWSTATS)"),        flagsGroup);
    m_rptControlsBox   = new QCheckBox(tr("Control rule actions (CONTROLS)"),    flagsGroup);
    m_rptAveragesBox   = new QCheckBox(tr("Time-averaged results (AVERAGES)"),   flagsGroup);

    flagsForm->addRow(QString(), m_rptDisabledBox);
    flagsForm->addRow(QString(), m_rptInputBox);
    flagsForm->addRow(QString(), m_rptContinuityBox);
    flagsForm->addRow(QString(), m_rptFlowstatsBox);
    flagsForm->addRow(QString(), m_rptControlsBox);
    flagsForm->addRow(QString(), m_rptAveragesBox);

    // DISABLED short-circuits everything else — grey out the dependent
    // controls when it's on so the user knows nothing else matters.
    auto syncDisabledShortCircuit = [this, flagsGroup]() {
        const bool disabled = m_rptDisabledBox && m_rptDisabledBox->isChecked();
        for (QCheckBox *cb : { m_rptInputBox, m_rptContinuityBox,
                               m_rptFlowstatsBox, m_rptControlsBox,
                               m_rptAveragesBox })
            if (cb) cb->setEnabled(!disabled);
        Q_UNUSED(flagsGroup);
    };
    connect(m_rptDisabledBox, &QCheckBox::toggled,
            this, [syncDisabledShortCircuit](bool) { syncDisabledShortCircuit(); });

    vlay->addWidget(flagsGroup);

    // ---- selectors -----------------------------------------------------
    // Per-kind: a row with [○ None] [○ All] [○ Selected] [name list edit].
    // The line-edit greys out unless Selected is chosen.
    auto buildSelector = [this, grp](const QString &label,
                                      QRadioButton **noneR,
                                      QRadioButton **allR,
                                      QRadioButton **someR,
                                      QLineEdit    **listE)
    {
        auto *row = new QGroupBox(label, grp);
        auto *h = new QHBoxLayout(row);

        *noneR = new QRadioButton(tr("None"),     row);
        *allR  = new QRadioButton(tr("All"),      row);
        *someR = new QRadioButton(tr("Selected:"), row);

        auto *bg = new QButtonGroup(row);
        bg->addButton(*noneR, 0);
        bg->addButton(*allR,  1);
        bg->addButton(*someR, 2);

        *listE = new QLineEdit(row);
        (*listE)->setPlaceholderText(
            tr("comma- or space-separated object names"));
        (*listE)->setEnabled(false);

        h->addWidget(*noneR);
        h->addWidget(*allR);
        h->addWidget(*someR);
        h->addWidget(*listE, 1);

        // Edit field follows the Selected radio.
        connect(*someR, &QRadioButton::toggled, *listE, &QLineEdit::setEnabled);

        return row;
    };

    vlay->addWidget(buildSelector(tr("Subcatchments"),
        &m_rptSubcatchNoneRadio, &m_rptSubcatchAllRadio,
        &m_rptSubcatchSomeRadio, &m_rptSubcatchListEdit));
    vlay->addWidget(buildSelector(tr("Nodes"),
        &m_rptNodeNoneRadio, &m_rptNodeAllRadio,
        &m_rptNodeSomeRadio, &m_rptNodeListEdit));
    vlay->addWidget(buildSelector(tr("Links"),
        &m_rptLinkNoneRadio, &m_rptLinkAllRadio,
        &m_rptLinkSomeRadio, &m_rptLinkListEdit));

    parentLayout->addWidget(grp);
}

void SimulationOptionsDialog::readReportContentsFromEngine()
{
    if (!m_engine) return;

    // Bool keys ---------------------------------------------------------
    auto setBox = [this](QCheckBox *box, const char *key, bool fallback) {
        if (!box) return;
        const QString v = getOption(key, fallback ? QStringLiteral("YES")
                                                  : QStringLiteral("NO"));
        QSignalBlocker blk(box);
        box->setChecked(parseEngineBool(v) == Qt::Checked);
    };
    setBox(m_rptDisabledBox,   "RPT_DISABLED",   false);
    setBox(m_rptInputBox,      "RPT_INPUT",      false);
    setBox(m_rptContinuityBox, "RPT_CONTINUITY", true);
    setBox(m_rptFlowstatsBox,  "RPT_FLOWSTATS",  true);
    setBox(m_rptControlsBox,   "RPT_CONTROLS",   false);
    setBox(m_rptAveragesBox,   "RPT_AVERAGES",   false);

    // Sync the disabled-short-circuit state once after the initial read.
    if (m_rptDisabledBox) {
        const bool disabled = m_rptDisabledBox->isChecked();
        for (QCheckBox *cb : { m_rptInputBox, m_rptContinuityBox,
                               m_rptFlowstatsBox, m_rptControlsBox,
                               m_rptAveragesBox })
            if (cb) cb->setEnabled(!disabled);
    }

    // Selector keys -----------------------------------------------------
    auto setSelector = [this](QRadioButton *noneR, QRadioButton *allR,
                              QRadioButton *someR, QLineEdit *listE,
                              const char *key) {
        if (!noneR || !allR || !someR || !listE) return;
        const QString v = getOption(key, QStringLiteral("ALL")).trimmed();
        QSignalBlocker b1(noneR), b2(allR), b3(someR), b4(listE);
        if (v.compare(QStringLiteral("NONE"), Qt::CaseInsensitive) == 0) {
            noneR->setChecked(true);
            listE->clear();
            listE->setEnabled(false);
        } else if (v.isEmpty()
                || v.compare(QStringLiteral("ALL"), Qt::CaseInsensitive) == 0) {
            allR->setChecked(true);
            listE->clear();
            listE->setEnabled(false);
        } else {
            someR->setChecked(true);
            listE->setText(v);
            listE->setEnabled(true);
        }
    };
    setSelector(m_rptSubcatchNoneRadio, m_rptSubcatchAllRadio,
                m_rptSubcatchSomeRadio, m_rptSubcatchListEdit,
                "RPT_SUBCATCHMENTS");
    setSelector(m_rptNodeNoneRadio, m_rptNodeAllRadio,
                m_rptNodeSomeRadio, m_rptNodeListEdit,
                "RPT_NODES");
    setSelector(m_rptLinkNoneRadio, m_rptLinkAllRadio,
                m_rptLinkSomeRadio, m_rptLinkListEdit,
                "RPT_LINKS");
}

int SimulationOptionsDialog::writeReportContentsToEngine()
{
    if (!m_engine) return 0;
    int n = 0;
    auto writeIfChanged = [this, &n](const char *key, const QString &newVal) {
        if (getOption(key) == newVal) return;
        if (setOption(key, newVal)) ++n;
    };

    auto boolStr = [](QCheckBox *box, bool def) {
        return box ? engineBoolString(box->isChecked()) : engineBoolString(def);
    };
    writeIfChanged("RPT_DISABLED",   boolStr(m_rptDisabledBox,   false));
    writeIfChanged("RPT_INPUT",      boolStr(m_rptInputBox,      false));
    writeIfChanged("RPT_CONTINUITY", boolStr(m_rptContinuityBox, true));
    writeIfChanged("RPT_FLOWSTATS",  boolStr(m_rptFlowstatsBox,  true));
    writeIfChanged("RPT_CONTROLS",   boolStr(m_rptControlsBox,   false));
    writeIfChanged("RPT_AVERAGES",   boolStr(m_rptAveragesBox,   false));

    auto selectorStr = [](QRadioButton *noneR, QRadioButton *allR,
                          QRadioButton *someR, QLineEdit *listE) {
        if (!noneR || !allR || !someR || !listE) return QStringLiteral("ALL");
        if (noneR->isChecked()) return QStringLiteral("NONE");
        if (allR->isChecked())  return QStringLiteral("ALL");
        // Selected — but empty list collapses to NONE so we don't push
        // an invalid empty SOME state through the engine.
        const QString text = listE->text().trimmed();
        return text.isEmpty() ? QStringLiteral("NONE") : text;
    };
    writeIfChanged("RPT_SUBCATCHMENTS",
        selectorStr(m_rptSubcatchNoneRadio, m_rptSubcatchAllRadio,
                    m_rptSubcatchSomeRadio, m_rptSubcatchListEdit));
    writeIfChanged("RPT_NODES",
        selectorStr(m_rptNodeNoneRadio, m_rptNodeAllRadio,
                    m_rptNodeSomeRadio, m_rptNodeListEdit));
    writeIfChanged("RPT_LINKS",
        selectorStr(m_rptLinkNoneRadio, m_rptLinkAllRadio,
                    m_rptLinkSomeRadio, m_rptLinkListEdit));

    return n;
}

bool SimulationOptionsDialog::setOption(const char *key, const QString &value)
{
    // Prefer the layer's setOption when available — it emits
    // optionsChanged() which the main window + status bar listen to, so
    // per-key writes do the live-sync automatically instead of
    // depending on a post-hoc refresh from the caller. Fall back to the
    // raw engine API when no layer is bound (e.g. dialog used in
    // engine-only tests).
    if (m_layer) {
        return m_layer->setOption(QByteArray(key), value);
    }
    if (!m_engine) return false;
    const QByteArray v = value.toUtf8();
    return swmm_options_set(m_engine, key, v.constData()) == 0;
}

// ---------------------------------------------------------------------------
// Engine ↔ widgets
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::readFromEngine()
{
    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0) c->setCurrentIndex(idx);
    };

    // Source every getOption() fallback from PreferencesManager so the dialog
    // shows the user-preferred default whenever the engine has no value for
    // a key — keeps the new-project synthesis path and the missing-key path
    // in lockstep and avoids hardcoded magic-number drift.
    const auto sim = PreferencesManager::instance()->simulationDefaults();
    const auto ynStr = [](bool v) {
        return v ? QStringLiteral("YES") : QStringLiteral("NO");
    };

    // ---- Tab 0 — Title / Notes ----------------------------------------
    if (m_titleNotesEdit) {
        QSignalBlocker blk(m_titleNotesEdit);
        // Prefer the .oswp-persisted rich HTML when available — it preserves
        // formatting that the engine's plain-text [TITLE] cannot.
        const QString persistedHtml = m_projectWindow ? m_projectWindow->notesHtml()
                                                      : QString();
        if (!persistedHtml.isEmpty()) {
            m_titleNotesEdit->setHtml(persistedHtml);
        } else if (m_engine) {
            int count = 0;
            QStringList lines;
            if (swmm_title_get_count(m_engine, &count) == 0 && count > 0) {
                lines.reserve(count);
                for (int i = 0; i < count; ++i) {
                    char buf[1024] = {0};
                    if (swmm_title_get_line(m_engine, i, buf, sizeof(buf)) == 0)
                        lines << QString::fromUtf8(buf);
                }
            }
            m_titleNotesEdit->setPlainText(lines.join(QChar('\n')));
        } else {
            m_titleNotesEdit->clear();
        }
        m_initialNotesHtml = m_titleNotesEdit->toHtml();
    }

    // ---- Tab 1 ---------------------------------------------------------
    selectComboByData(m_infiltrationCombo, getOption("INFILTRATION", sim.infiltrationModel));
    selectComboByData(m_routingCombo,      getOption("FLOW_ROUTING", sim.flowRouting));

    m_allowPondingBox->setChecked(parseEngineBool(getOption("ALLOW_PONDING",     ynStr(sim.allowPonding)))    == Qt::Checked);
    m_skipSteadyBox->setChecked(  parseEngineBool(getOption("SKIP_STEADY_STATE", ynStr(sim.skipSteadyState))) == Qt::Checked);
    // Inverted UI: checked = process active = engine IGNORE_X is NO.
    m_ignoreRainfallBox->setChecked(   parseEngineBool(getOption("IGNORE_RAINFALL",    ynStr(sim.ignoreRainfall)))    != Qt::Checked);
    m_ignoreSnowmeltBox->setChecked(   parseEngineBool(getOption("IGNORE_SNOWMELT",    ynStr(sim.ignoreSnowmelt)))    != Qt::Checked);
    m_ignoreGroundwaterBox->setChecked(parseEngineBool(getOption("IGNORE_GROUNDWATER", ynStr(sim.ignoreGroundwater))) != Qt::Checked);
    m_ignoreRDIIBox->setChecked(       parseEngineBool(getOption("IGNORE_RDII",        ynStr(sim.ignoreRdii)))        != Qt::Checked);
    m_ignoreQualityBox->setChecked(    parseEngineBool(getOption("IGNORE_QUALITY",     ynStr(sim.ignoreQuality)))     != Qt::Checked);
    m_ignoreRoutingBox->setChecked(    parseEngineBool(getOption("IGNORE_ROUTING",     ynStr(sim.ignoreRouting)))     != Qt::Checked);

    // Context-sensitive availability (mirrors the legacy Delphi Analysis Options
    // form, Doptions.pas:314-332): a process toggle is disabled when the model
    // has no objects of the class it controls. The box still shows its stored
    // value; it just cannot be edited. Counts come from the engine C API.
    if (m_engine) {
        const int nGages     = swmm_gage_count(m_engine);
        const int nSnowpacks = swmm_snowpack_count(m_engine);
        const int nAquifers  = swmm_aquifer_count(m_engine);
        const int nLinks     = swmm_link_count(m_engine);
        const int nPolluts   = swmm_pollutant_count(m_engine);
        const int nHydros    = swmm_hydrograph_count(m_engine);
        m_ignoreRainfallBox   ->setEnabled(nGages > 0);
        m_ignoreSnowmeltBox   ->setEnabled(nSnowpacks > 0);
        m_ignoreGroundwaterBox->setEnabled(nAquifers > 0);
        m_ignoreRDIIBox       ->setEnabled(nGages > 0 && nHydros > 0);
        m_ignoreQualityBox    ->setEnabled(nPolluts > 0);
        m_ignoreRoutingBox    ->setEnabled(nLinks > 0);
    }

    // ---- Tab 2 ---------------------------------------------------------
    // Block signals on Start/Report-start during seeding so the clamp
    // connection doesn't bump report-start prematurely between the two
    // reads. Seed the minimum + duration label explicitly at the end.
    {
        QSignalBlocker bs(m_startEdit);
        QSignalBlocker br(m_reportStartEdit);

        QDateTime start = parseEngineDateTime(
            getOption("START_DATE"), getOption("START_TIME", "00:00:00"));
        if (start.isValid()) m_startEdit->setDateTime(start);

        QDateTime end = parseEngineDateTime(
            getOption("END_DATE"),   getOption("END_TIME",   "00:00:00"));
        if (end.isValid()) m_endEdit->setDateTime(end);

        QDateTime rpt = parseEngineDateTime(
            getOption("REPORT_START_DATE"),
            getOption("REPORT_START_TIME", "00:00:00"));
        if (rpt.isValid()) m_reportStartEdit->setDateTime(rpt);
    }
    m_reportStartEdit->setMinimumDateTime(m_startEdit->dateTime());
    updateDurationLabel();

    bool ok = false;

    // Engine round-trip for step values is loose: a step may come back as
    // plain seconds ("900") or as HH:MM:SS ("00:15:00", "48:00:00"). Try
    // integer first, fall through to colon-separated parse if that fails.
    auto parseStepSeconds = [](const QString &s, qint64 fallback) -> qint64 {
        const QString t = s.trimmed();
        bool ok = false;
        const qint64 asInt = t.toLongLong(&ok);
        if (ok) return asInt;
        const QStringList parts = t.split(QLatin1Char(':'));
        if (parts.size() < 1 || parts.size() > 3) return fallback;
        qint64 secs = 0;
        for (const QString &p : parts) {
            bool ok2 = false;
            const qint64 v = p.toLongLong(&ok2);
            if (!ok2) return fallback;
            secs = secs * 60 + v;
        }
        return secs;
    };

    m_reportStepEdit->setTotalSeconds(
        parseStepSeconds(getOption("REPORT_STEP", QString::number(sim.reportStepSec)),
                         sim.reportStepSec));
    m_dryStepEdit->setTotalSeconds(
        parseStepSeconds(getOption("DRY_STEP", QString::number(sim.dryStepSec)),
                         sim.dryStepSec));
    m_wetStepEdit->setTotalSeconds(
        parseStepSeconds(getOption("WET_STEP", QString::number(sim.wetStepSec)),
                         sim.wetStepSec));

    {
        const qint64 ruleSecs = parseStepSeconds(
            getOption("RULE_STEP", QString::number(sim.ruleStepSec)), sim.ruleStepSec);
        const qint64 maxRule  = qint64(23) * 3600 + qint64(59) * 60 + 59;
        const qint64 clamped  = qBound(qint64(0), ruleSecs, maxRule);
        m_ruleStepEdit->setTime(QTime(static_cast<int>(clamped / 3600),
                                       static_cast<int>((clamped % 3600) / 60),
                                       static_cast<int>(clamped % 60)));
    }

    const double routeStep = getOption("ROUTING_STEP",
                                       QString::number(sim.routingStepSec, 'g', 6))
                                .toDouble(&ok);
    m_routingStepEdit->setText(
        QString::number(ok ? routeStep : sim.routingStepSec, 'g', 6));

    const double dryDays = getOption("DRY_DAYS",
                                     QString::number(sim.dryDays, 'g', 6))
                              .toDouble(&ok);
    m_dryDaysSpin->setValue(ok ? dryDays : sim.dryDays);

    // Sweep window — engine stores "MM/DD"; map into a fixed-year QDate
    // (2000 is a leap year so 02/29 stays selectable).
    auto parseSweep = [](const QString &s, QDate fallback) {
        const QStringList parts = s.split(QLatin1Char('/'));
        if (parts.size() != 2) return fallback;
        bool okM = false, okD = false;
        const int m = parts[0].toInt(&okM);
        const int d = parts[1].toInt(&okD);
        if (!okM || !okD) return fallback;
        const QDate q(2000, m, d);
        return q.isValid() ? q : fallback;
    };
    const QDate sweepStartPref = parseSweep(sim.sweepStart, QDate(2000, 1, 1));
    const QDate sweepEndPref   = parseSweep(sim.sweepEnd,   QDate(2000, 12, 31));
    m_sweepStartEdit->setDate(parseSweep(getOption("SWEEP_START", sim.sweepStart),
                                         sweepStartPref));
    m_sweepEndEdit->setDate(parseSweep(getOption("SWEEP_END", sim.sweepEnd),
                                       sweepEndPref));

    // ---- Tab 2 — [EVENTS] (Slice CW) -----------------------------------
    readEventsFromEngine();

    // ---- Tab 3 ---------------------------------------------------------
    selectComboByData(m_surchargeCombo,      getOption("SURCHARGE_METHOD",    sim.surchargeMethod));
    selectComboByData(m_nodeContinuityCombo, getOption("NODE_CONTINUITY",     sim.nodeContinuity));
    selectComboByData(m_forceMainCombo,      getOption("FORCE_MAIN_EQUATION", sim.forceMainEquation));
    selectComboByData(m_normalFlowCombo,     getOption("NORMAL_FLOW_LIMITED", sim.normalFlowLimited));
    selectComboByData(m_inertialDampCombo,   getOption("INERTIAL_DAMPING",    sim.inertialDamping));
    m_andersonAccelBox->setChecked(parseEngineBool(getOption("ANDERSON_ACCEL",
                                                              ynStr(sim.andersonAccel))) == Qt::Checked);

    // DPS_* knobs are dynamic-slot specific and not surfaced in
    // PreferencesManager — keep engine-side defaults.
    m_dpsCelerSpin->setValue(getOption("DPS_CELERITY",   "25").toDouble(&ok));
    m_dpsAlphaSpin->setValue(getOption("DPS_ALPHA",      "3.0").toDouble(&ok));
    m_dpsDecaySpin->setValue(getOption("DPS_DECAY_TIME", "0.5").toDouble(&ok));

    m_lengtheningSpin->setValue(
        getOption("LENGTHENING_STEP", QString::number(sim.lengtheningStepSec, 'g', 6))
            .toDouble(&ok));
    // VARIABLE_STEP toggle in prefs zeroes the Courant factor when off.
    const double variablePref = sim.variableStepOn ? sim.variableStepFactor : 0.0;
    m_variableStepSpin->setValue(
        getOption("VARIABLE_STEP", QString::number(variablePref, 'g', 6))
            .toDouble(&ok));

    m_maxTrialsSpin->setValue(
        getOption("MAX_TRIALS", QString::number(sim.maxTrials)).toInt(&ok));
    m_headTolSpin->setValue(
        getOption("HEAD_TOLERANCE", QString::number(sim.headTolerance, 'g', 6))
            .toDouble(&ok));
    // The engine stores LAT_FLOW_TOL / SYS_FLOW_TOL as fractions but the
    // .inp surface uses percent. Prefs hold percent; convert to fraction
    // for the fallback string and back to percent for the spin display.
    m_latFlowTolSpin->setValue(
        getOption("LAT_FLOW_TOL", QString::number(sim.latFlowTolPct / 100.0, 'g', 6))
            .toDouble(&ok) * 100.0);
    m_sysFlowTolSpin->setValue(
        getOption("SYS_FLOW_TOL", QString::number(sim.sysFlowTolPct / 100.0, 'g', 6))
            .toDouble(&ok) * 100.0);
    // MIN_SURFAREA isn't in prefs; engine default is 0.
    m_minSurfAreaSpin->setValue(getOption("MIN_SURFAREA", "0").toDouble(&ok));
    m_minSlopeSpin->setValue(
        getOption("MIN_SLOPE", QString::number(sim.minSlopePct, 'g', 6)).toDouble(&ok));

    updateSurchargeFieldsEnabled();

    // ---- Tab 4 ---------------------------------------------------------
    m_threadsSpin->setValue(
        getOption("THREADS", QString::number(sim.threads)).toInt(&ok));

    // ---- 2D module toggle (Tab 1 → Modules group) ----------------------
    // Persisted per-.inp under QSettings since the engine has no native
    // option for "module enabled" — module activation is implicit in the
    // presence of [2D_VERTICES]/[2D_TRIANGLES] sections. When there's no
    // stored preference the default is inferred from the .inp itself: a
    // file that already carries 2D sections (pre-built demos, externally
    // authored models) shows the module ON; a fresh blank .inp shows OFF.
    if (m_module2DBox && m_layer)
    {
        QSettings s;
        const QString key = QStringLiteral("SWMMVis/Project/%1/Module2DEnabled")
                                .arg(m_layer->modelFilePath());
        const bool enabled = s.contains(key)
            ? s.value(key).toBool()
            : inpCarries2DSections(m_layer->modelFilePath());
        QSignalBlocker blk(m_module2DBox);  // avoid wiring through on2DModuleToggled twice
        m_module2DBox->setChecked(enabled);
        on2DModuleToggled(enabled);  // explicit: sync tab-enabled state.
    }

    // ---- Tab 6 (2D) — only present when compiled in --------------------
#ifdef OPENSWMM_HAS_2D
    read2DFromEngine();
#endif

    // ---- Tab 7 (Files / Plugins) ----------------------------------------
    readPluginsFromEngine();
    readFilesSectionFromEngine();
    readWriterCombosFromEngine();
    readReportContentsFromEngine();
    readOutputPathsFromSettings();
}

// ---------------------------------------------------------------------------
// Tab 7 — Files / Plugins (Slice AA-3.5)
// ---------------------------------------------------------------------------

QWidget *SimulationOptionsDialog::buildFilesTab()
{
    // Phase 3.10 (2026-05-22) — the legacy single "Files" page is split
    // into three sub-tabs (Files / Output / Plugins) via a nested
    // QTabWidget.  Existing widgets keep their member identities; this
    // method just regroups them by concern:
    //   • Files   — [FILES] secondary refs + scheduled hot-start saves
    //   • Output  — writer combos + [REPORT] flags + .rpt / .out paths
    //   • Plugins — [PLUGINS] table editor (Phase 3.10.3)
    auto *page = new QWidget(this);
    auto *outerLay = new QVBoxLayout(page);
    outerLay->setContentsMargins(0, 0, 0, 0);

    auto *subTabs = new QTabWidget(page);
    subTabs->setDocumentMode(true);
    outerLay->addWidget(subTabs);

    // =====================================================================
    // Sub-tab "Files" — [FILES] secondary refs + scheduled hot-start saves
    // =====================================================================
    auto *filesPage = new QWidget(subTabs);
    auto *vlay = new QVBoxLayout(filesPage);

    // ── [FILES] secondary references group ─────────────────────────────
    auto *secondary = new QGroupBox(
        tr("Secondary file references (.inp [FILES] section)"), filesPage);
    auto *secForm = new QFormLayout(secondary);

    auto makeModeCombo = [secondary] {
        auto *c = new QComboBox(secondary);
        c->addItem(tr("(off)"), QString());
        c->addItem(tr("USE"),   QStringLiteral("USE"));
        c->addItem(tr("SAVE"),  QStringLiteral("SAVE"));
        return c;
    };
    // Slice IO-11a — every [FILES] row is a RelativePathPicker so the
    // line edit shows the path relative to the project anchor (defaulting
    // to the .inp directory) and the embedded "…" button opens the file
    // dialog. The picker re-routes the browse outcome through its own
    // resolveAgainst-anchor logic; we no longer build a separate
    // QPushButton + QFileDialog branch here.
    //
    // The mode combo (USE/SAVE) drives the picker's acceptMode so SAVE
    // rows pick the "Save as" dialog flavour. Filters are coarse — most
    // legacy SWMM secondary refs are plain text/CSV; the hot-start USE
    // row gets its own *.hsf filter.
    const QString projectAnchor =
        (m_layer && !m_layer->modelFilePath().isEmpty())
            ? QFileInfo(m_layer->modelFilePath()).absolutePath()
            : QString();

    auto makePathRow = [secondary, secForm, this, projectAnchor](
                            const QString &label,
                            openswmmvis::ui::RelativePathPicker **edit,
                            QComboBox *modeCombo,
                            bool defaultSave,
                            const QString &dialogTitle,
                            const QString &filter) {
        auto *picker = new openswmmvis::ui::RelativePathPicker(secondary);
        picker->setProjectAnchor(projectAnchor);
        picker->setFileFilter(filter);
        picker->setDialogCaption(dialogTitle);
        picker->setAcceptMode(defaultSave ? QFileDialog::AcceptSave
                                          : QFileDialog::AcceptOpen);
        *edit = picker;

        auto *row = new QHBoxLayout();
        row->addWidget(picker, 1);
        if (modeCombo) {
            row->addWidget(new QLabel(QObject::tr("Mode:"), secondary));
            row->addWidget(modeCombo);

            // Mode change → pick the right file-dialog flavour on next browse.
            connect(modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                    picker, [picker, modeCombo, defaultSave] {
                bool save = defaultSave;
                const QString m = modeCombo->currentData().toString();
                if (!m.isEmpty())
                    save = (m.compare(QLatin1String("SAVE"),
                                       Qt::CaseInsensitive) == 0);
                picker->setAcceptMode(save ? QFileDialog::AcceptSave
                                            : QFileDialog::AcceptOpen);
            });
        }
        secForm->addRow(label, row);
    };

    const QString textFilter = tr("Text files (*.txt *.dat);;All Files (*)");
    const QString csvFilter  = tr("Text / CSV (*.txt *.dat *.csv);;All Files (*)");
    const QString hsfFilter  = tr("Hot-start files (*.hsf);;All Files (*)");

    m_rainfallModeCombo = makeModeCombo();
    makePathRow(tr("Rainfall:"), &m_rainfallPathEdit, m_rainfallModeCombo,
                false, tr("Choose Rainfall File"), textFilter);

    m_runoffModeCombo = makeModeCombo();
    makePathRow(tr("Runoff:"),   &m_runoffPathEdit,   m_runoffModeCombo,
                false, tr("Choose Runoff File"), textFilter);

    m_rdiiModeCombo = makeModeCombo();
    makePathRow(tr("RDII:"),     &m_rdiiPathEdit,     m_rdiiModeCombo,
                false, tr("Choose RDII File"), textFilter);

    makePathRow(tr("Inflows (USE only):"),   &m_inflowsPathEdit,  nullptr,
                false, tr("Choose Inflows File"),  csvFilter);
    makePathRow(tr("Outflows (SAVE only):"), &m_outflowsPathEdit, nullptr,
                true,  tr("Choose Outflows File"), csvFilter);
    makePathRow(tr("Hot-start file (USE):"), &m_hotstartUseEdit,  nullptr,
                false, tr("Choose Hot-Start File"), hsfFilter);

    vlay->addWidget(secondary);

    // ── Scheduled hot-start saves (Slice BV-01, 2026-05-21) ─────────────
    // Multi-row uncapped table replaces the legacy single SAVE field.
    // Backed by the new swmm_hotstart_saves_* engine C API.
    //
    // Phase 3.10.5 (2026-05-22): true MVC — a QTableView bound to a
    // HotstartSavesModel.  HotstartSavesPathDelegate renders each path
    // cell as a [QLineEdit][…] composite so the user can browse for the
    // save path inline; HotstartSavesDateTimeDelegate renders each
    // datetime cell as a QDateTimeEdit with the "(end of run)" sentinel.
    // Persistent editors keep both widgets visible without click-to-edit.
    auto *hsSaves = new QGroupBox(
        tr("Scheduled hot-start saves (.inp [FILES] SAVE HOTSTART)"), filesPage);
    auto *hsSavesLay = new QVBoxLayout(hsSaves);

    m_hotstartSavesModel   = new HotstartSavesModel(this);
    m_hotstartSavesPathDel = new PathBrowseDelegate(
        PathBrowseDelegate::SaveFile,
        tr("Choose Hot-Start Save File"),
        tr("Hot-start files (*.hsf);;All Files (*)"),
        tr("path relative to the .inp directory"),
        this);
    // Slice IO-11c — feed the same project anchor into the delegate so
    // file picks under the .inp directory commit as relative tokens,
    // matching the [FILES] picker behaviour set up immediately above.
    m_hotstartSavesPathDel->setProjectAnchor(projectAnchor);
    m_hotstartSavesDtDel   = new HotstartSavesDateTimeDelegate(this);

    m_hotstartSavesView = new QTableView(hsSaves);
    m_hotstartSavesView->setModel(m_hotstartSavesModel);
    m_hotstartSavesView->setItemDelegateForColumn(
        HotstartSavesModel::ColPath, m_hotstartSavesPathDel);
    m_hotstartSavesView->setItemDelegateForColumn(
        HotstartSavesModel::ColDateTime, m_hotstartSavesDtDel);
    // Interactive resize: both columns are user-draggable.  Set sensible
    // defaults — path column takes the leftover space initially, datetime
    // is sized to fit the picker — but neither is locked.
    {
        auto *hdr = m_hotstartSavesView->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setStretchLastSection(false);
        hdr->setSectionsMovable(false);
        hdr->setMinimumSectionSize(60);
        // Width hint: ~2/3 path, ~1/3 datetime — refined once the view is
        // shown (Qt will clamp later if the dialog is resized).
        hdr->resizeSection(HotstartSavesModel::ColPath,     360);
        hdr->resizeSection(HotstartSavesModel::ColDateTime, 200);
    }
    m_hotstartSavesView->verticalHeader()->setVisible(false);
    m_hotstartSavesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_hotstartSavesView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_hotstartSavesView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_hotstartSavesView->setToolTip(tr(
        "Each row schedules one hot-start save during the run.\n"
        "Leave Datetime empty (\"(end of run)\") to save at the end of the\n"
        "simulation; otherwise the engine writes the file when the sim\n"
        "clock crosses the chosen datetime."));
    hsSavesLay->addWidget(m_hotstartSavesView);

    auto openPersistentForRow = [this](int row) {
        if (!m_hotstartSavesView || !m_hotstartSavesModel) return;
        m_hotstartSavesView->openPersistentEditor(
            m_hotstartSavesModel->index(row, HotstartSavesModel::ColPath));
        m_hotstartSavesView->openPersistentEditor(
            m_hotstartSavesModel->index(row, HotstartSavesModel::ColDateTime));
    };
    // Open persistent editors on every newly-inserted row so the path
    // line-edit + browse button and the QDateTimeEdit are visible by
    // default without click-to-edit.
    connect(m_hotstartSavesModel, &QAbstractItemModel::rowsInserted,
            this, [openPersistentForRow](const QModelIndex &, int first, int last) {
        for (int r = first; r <= last; ++r) openPersistentForRow(r);
    });
    connect(m_hotstartSavesModel, &QAbstractItemModel::modelReset, this,
            [this, openPersistentForRow] {
        if (!m_hotstartSavesModel) return;
        for (int r = 0; r < m_hotstartSavesModel->rowCount(); ++r)
            openPersistentForRow(r);
    });

    auto *hsBtnRow = new QHBoxLayout();
    m_hotstartSavesAddBtn    = new QPushButton(tr("Add…"),     hsSaves);
    m_hotstartSavesBrowseBtn = new QPushButton(tr("Browse…"),  hsSaves);
    m_hotstartSavesRemoveBtn = new QPushButton(tr("Remove"),   hsSaves);
    m_hotstartSavesUpBtn     = new QPushButton(tr("Move up"),  hsSaves);
    m_hotstartSavesDownBtn   = new QPushButton(tr("Move down"),hsSaves);
    m_hotstartSavesBrowseBtn->setToolTip(
        tr("Choose a save-as path for the selected row"));
    hsBtnRow->addWidget(m_hotstartSavesAddBtn);
    hsBtnRow->addWidget(m_hotstartSavesBrowseBtn);
    hsBtnRow->addWidget(m_hotstartSavesRemoveBtn);
    hsBtnRow->addWidget(m_hotstartSavesUpBtn);
    hsBtnRow->addWidget(m_hotstartSavesDownBtn);
    hsBtnRow->addStretch(1);
    hsSavesLay->addLayout(hsBtnRow);
    vlay->addWidget(hsSaves);

    auto updateHsButtons = [this] {
        const int row = (m_hotstartSavesView && m_hotstartSavesView->currentIndex().isValid())
                            ? m_hotstartSavesView->currentIndex().row() : -1;
        const int n   = m_hotstartSavesModel ? m_hotstartSavesModel->rowCount() : 0;
        const bool hasSel = row >= 0;
        if (m_hotstartSavesBrowseBtn) m_hotstartSavesBrowseBtn->setEnabled(hasSel);
        if (m_hotstartSavesRemoveBtn) m_hotstartSavesRemoveBtn->setEnabled(hasSel);
        if (m_hotstartSavesUpBtn)     m_hotstartSavesUpBtn->setEnabled(hasSel && row > 0);
        if (m_hotstartSavesDownBtn)   m_hotstartSavesDownBtn->setEnabled(hasSel && row < n - 1);
    };
    updateHsButtons();
    connect(m_hotstartSavesView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, [updateHsButtons](const QModelIndex &, const QModelIndex &) {
        updateHsButtons();
    });
    connect(m_hotstartSavesModel, &QAbstractItemModel::rowsInserted,
            this, updateHsButtons);
    connect(m_hotstartSavesModel, &QAbstractItemModel::rowsRemoved,
            this, updateHsButtons);
    connect(m_hotstartSavesModel, &QAbstractItemModel::modelReset,
            this, updateHsButtons);

    connect(m_hotstartSavesAddBtn,    &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveAddRow);
    connect(m_hotstartSavesBrowseBtn, &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveBrowseRow);
    connect(m_hotstartSavesRemoveBtn, &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveRemoveRow);
    connect(m_hotstartSavesUpBtn,     &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveMoveRowUp);
    connect(m_hotstartSavesDownBtn,   &QPushButton::clicked,
            this, &SimulationOptionsDialog::onHotstartSaveMoveRowDown);

    vlay->addStretch(1);
    subTabs->addTab(filesPage, tr("Files"));

    // =====================================================================
    // Sub-tab "Output" — writer combos + [REPORT] flags + .rpt / .out paths
    // (Phase 3.10.2, 2026-05-22)
    // =====================================================================
    auto *outputPage = new QWidget(subTabs);
    auto *outVlay = new QVBoxLayout(outputPage);

    // ── Writer / Container group ────────────────────────────────────────
    // Three combos let the user pick the plugin driving each role.  The
    // combo's hidden `data()` is the plugin id (empty string for the
    // built-in `.inp` / `.out` / `.rpt` writer).  Picking a non-default
    // entry adds the corresponding [PLUGINS] row on Apply.
    m_writersGroup = new QGroupBox(tr("Writer / Container"), outputPage);
    auto *writerGroup = m_writersGroup;
    auto *writerForm  = new QFormLayout(writerGroup);

    auto populateWriterCombo = [](QComboBox *combo, openswmmvis::FilterKind k) {
        // First entry: built-in (empty plugin id = engine default).
        const char *defaultLabel =
            (k == openswmmvis::FilterKind::InputRead)
                ? "(built-in: .inp writer)"
                : (k == openswmmvis::FilterKind::ResultsWrite)
                      ? "(built-in: .out writer)"
                      : "(built-in: .rpt writer)";
        combo->addItem(QObject::tr(defaultLabel), QString());
        auto *registry = openswmmvis::FileFilterRegistry::instance();
        QStringList seen;  // dedupe by pluginId
        // Phase 3.10.6 (2026-05-22): the engine SDK has no INPUT_WRITE
        // role — plugins that handle the model input file advertise
        // INPUT_READ only, and the GUI registry sets `canWrite=false`
        // on those entries.  The previous gate `if (!canWrite) continue;`
        // therefore silently dropped every engine-provided input plugin
        // from the Input writer combo.  All three combos now show every
        // plugin id advertising the matching role, deduped.
        for (const auto &entry : registry->entriesFor(k)) {
            if (entry.pluginId.isEmpty()) continue;
            if (seen.contains(entry.pluginId)) continue;
            seen << entry.pluginId;
            const QString label = entry.description.isEmpty()
                ? entry.pluginId
                : QStringLiteral("%1 (%2)").arg(entry.description, entry.pluginId);
            combo->addItem(label, entry.pluginId);
        }
    };

    m_inputWriterCombo  = new QComboBox(writerGroup);
    populateWriterCombo(m_inputWriterCombo, openswmmvis::FilterKind::InputRead);
    writerForm->addRow(tr("Input writer:"),  m_inputWriterCombo);

    m_outputWriterCombo = new QComboBox(writerGroup);
    populateWriterCombo(m_outputWriterCombo, openswmmvis::FilterKind::ResultsWrite);
    writerForm->addRow(tr("Output writer:"), m_outputWriterCombo);

    m_reportWriterCombo = new QComboBox(writerGroup);
    populateWriterCombo(m_reportWriterCombo, openswmmvis::FilterKind::ReportWrite);
    writerForm->addRow(tr("Report writer:"), m_reportWriterCombo);

    m_singleContainerBox = new QCheckBox(
        tr("Single container (write input, output, and report to one file)"),
        writerGroup);
    m_singleContainerBox->setToolTip(
        tr("Enabled when the chosen Input writer plugin advertises input, "
           "output, and report roles for the same extension (e.g., GeoPackage). "
           "When checked, the Output and Report writer combos lock to the "
           "Input writer's plugin id."));
    m_singleContainerBox->setEnabled(false);
    writerForm->addRow(QString(), m_singleContainerBox);

    connect(m_inputWriterCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateSingleContainerEnabled(); });
    connect(m_singleContainerBox, &QCheckBox::toggled,
            this, &SimulationOptionsDialog::onSingleContainerToggled);

    outVlay->addWidget(writerGroup);

    // ── Report contents ([REPORT] section — Slice BV.1, 2026-05-22) ───
    buildReportContentsGroup(outVlay, outputPage);

    // ── Report file path (Slice AA-4) ────────────────────────────────────
    auto *rptGroup = new QGroupBox(tr("Report file"), outputPage);
    auto *rptForm  = new QFormLayout(rptGroup);

    auto *rptPathRow = new QHBoxLayout();
    m_reportFilePathEdit = new QLineEdit(rptGroup);
    m_reportFilePathEdit->setPlaceholderText(tr("(auto — sibling of input file with .rpt extension)"));
    m_reportFilePathEdit->setToolTip(tr(
        "Override path for the simulation report file. Leave blank to "
        "derive the path automatically from the input file location. "
        "The format is determined by the Report writer combo above."));
    rptPathRow->addWidget(m_reportFilePathEdit, 1);
    auto *rptBrowse = new QPushButton(tr("Browse…"), rptGroup);
    connect(rptBrowse, &QPushButton::clicked,
            this, &SimulationOptionsDialog::browseForReportFile);
    rptPathRow->addWidget(rptBrowse);
    rptForm->addRow(tr("Report file path:"), rptPathRow);
    outVlay->addWidget(rptGroup);

    // ── Output (results) file path (Slice AA-4) ──────────────────────────
    auto *outGroup = new QGroupBox(tr("Results output file"), outputPage);
    auto *outForm  = new QFormLayout(outGroup);

    auto *outPathRow = new QHBoxLayout();
    m_outputFilePathEdit = new QLineEdit(outGroup);
    m_outputFilePathEdit->setPlaceholderText(tr("(auto — sibling of input file with .out extension)"));
    m_outputFilePathEdit->setToolTip(tr(
        "Override path for the binary results output file. Leave blank to "
        "derive the path automatically from the input file location. "
        "The format is determined by the Output writer combo above."));
    outPathRow->addWidget(m_outputFilePathEdit, 1);
    auto *outBrowse = new QPushButton(tr("Browse…"), outGroup);
    connect(outBrowse, &QPushButton::clicked,
            this, &SimulationOptionsDialog::browseForOutputFile);
    outPathRow->addWidget(outBrowse);
    outForm->addRow(tr("Output file path:"), outPathRow);
    outVlay->addWidget(outGroup);

    outVlay->addStretch(1);
    subTabs->addTab(outputPage, tr("Output"));

    // =====================================================================
    // Sub-tab "Plugins" — [PLUGINS] table (Phase 3.10.3, 2026-05-22)
    // =====================================================================
    auto *pluginsPage = new QWidget(subTabs);
    auto *plVlay = new QVBoxLayout(pluginsPage);

    auto *intro = new QLabel(
        tr("Plugins listed in the model's <b>[PLUGINS]</b> section.  Each row "
           "names a writer / output / report plugin (by id, <i>id:version</i>, "
           "or shared-library path) and any free-form arguments to pass to "
           "its initialize() call.  The first input-capable row is also used "
           "by File → Save As when picking a non-<code>.inp</code> "
           "extension."),
        pluginsPage);
    intro->setWordWrap(true);
    plVlay->addWidget(intro);

    // Phase 3.10.6 (2026-05-22) — MVC: model + QTableView with the
    // reusable PathBrowseDelegate on column 0 so each row's plugin
    // path field carries an inline "…" browse button targeting the
    // platform's shared-library extensions.  Column 1 (arguments)
    // uses the default QLineEdit delegate (free-form text).
    m_pluginsModel   = new PluginsTableModel(this);
    m_pluginsPathDel = new PathBrowseDelegate(
        PathBrowseDelegate::OpenFile,
        tr("Choose Plugin Library"),
#if defined(Q_OS_WIN)
        tr("Plugin libraries (*.dll);;All Files (*)"),
#elif defined(Q_OS_MACOS)
        tr("Plugin libraries (*.dylib *.so *.bundle);;All Files (*)"),
#else
        tr("Plugin libraries (*.so);;All Files (*)"),
#endif
        tr("plugin id, id:version, or library path"),
        this);

    m_pluginsView = new QTableView(pluginsPage);
    m_pluginsView->setModel(m_pluginsModel);
    m_pluginsView->setItemDelegateForColumn(
        PluginsTableModel::ColPath, m_pluginsPathDel);
    m_pluginsView->verticalHeader()->setVisible(false);
    m_pluginsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pluginsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pluginsView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    {
        auto *hdr = m_pluginsView->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setStretchLastSection(true);
        hdr->setMinimumSectionSize(60);
        hdr->resizeSection(PluginsTableModel::ColPath, 360);
        hdr->resizeSection(PluginsTableModel::ColArgs, 220);
    }
    plVlay->addWidget(m_pluginsView, 1);

    // Open the path-cell persistent editor for every row so the browse
    // "…" button is visible without click-to-edit.  Args column keeps
    // the default click-to-edit behaviour.
    auto openPersistentPluginPath = [this](int row) {
        if (!m_pluginsView || !m_pluginsModel) return;
        m_pluginsView->openPersistentEditor(
            m_pluginsModel->index(row, PluginsTableModel::ColPath));
    };
    connect(m_pluginsModel, &QAbstractItemModel::rowsInserted, this,
            [openPersistentPluginPath](const QModelIndex &, int first, int last) {
        for (int r = first; r <= last; ++r) openPersistentPluginPath(r);
    });
    connect(m_pluginsModel, &QAbstractItemModel::modelReset, this,
            [this, openPersistentPluginPath] {
        if (!m_pluginsModel) return;
        for (int r = 0; r < m_pluginsModel->rowCount(); ++r)
            openPersistentPluginPath(r);
    });

    auto *btnRow = new QHBoxLayout();
    m_pluginsAddBtn    = new QPushButton(tr("Add"), pluginsPage);
    m_pluginsRemoveBtn = new QPushButton(tr("Remove"), pluginsPage);
    m_pluginsRemoveBtn->setEnabled(false);
    btnRow->addWidget(m_pluginsAddBtn);
    btnRow->addWidget(m_pluginsRemoveBtn);
    btnRow->addStretch();
    plVlay->addLayout(btnRow);

    connect(m_pluginsAddBtn, &QPushButton::clicked, this, [this] {
        if (!m_pluginsModel || !m_pluginsView) return;
        const int row = m_pluginsModel->appendRow(QString(), QString());
        m_pluginsView->setCurrentIndex(
            m_pluginsModel->index(row, PluginsTableModel::ColPath));
    });
    connect(m_pluginsRemoveBtn, &QPushButton::clicked, this, [this] {
        if (!m_pluginsModel || !m_pluginsView) return;
        const QModelIndex cur = m_pluginsView->currentIndex();
        if (cur.isValid()) m_pluginsModel->removeRows(cur.row(), 1);
    });
    // Slice RC.4 — Plugins tab honours `is_builtin` from the engine's
    // plugin discovery API. Built-ins (today: Default Input / Output /
    // Report / StateIO + GeoPackage when OPENSWMM_HAS_GEOPACKAGE) are
    // statically linked into the engine, so a user attempting to
    // "Remove" the row from the .inp's [PLUGINS] section cannot
    // actually unload them — the singleton stays alive in-process.
    // Greying the Remove button when the selected row's path matches a
    // built-in plugin_id makes that constraint visible.
    auto builtInPluginIds = [] {
        QSet<QString> ids;
        for (const auto &p : openswmm::discover_plugins_by_id()) {
            if (p.is_builtin)
                ids.insert(QString::fromStdString(p.plugin_id));
        }
        return ids;
    }();
    connect(m_pluginsView->selectionModel(),
            &QItemSelectionModel::currentChanged, this,
            [this, builtInPluginIds](const QModelIndex &cur, const QModelIndex &) {
        if (!cur.isValid()) {
            m_pluginsRemoveBtn->setEnabled(false);
            m_pluginsRemoveBtn->setToolTip(QString());
            return;
        }
        const QString rowPath = m_pluginsModel
            ? m_pluginsModel->pathAt(cur.row()).trimmed()
            : QString();
        const bool isBuiltIn = builtInPluginIds.contains(rowPath);
        m_pluginsRemoveBtn->setEnabled(!isBuiltIn);
        m_pluginsRemoveBtn->setToolTip(
            isBuiltIn
              ? tr("\"%1\" is a built-in plugin statically linked into "
                   "the engine — removing the row would have no effect "
                   "(the plugin stays loaded in-process).").arg(rowPath)
              : QString());
    });

    subTabs->addTab(pluginsPage, tr("Plugins"));

    return page;
}

void SimulationOptionsDialog::readPluginsFromEngine()
{
    if (!m_pluginsModel) return;
    m_pluginsModel->clearRows();
    if (!m_engine) return;

    int count = 0;
    if (swmm_plugins_count(m_engine, &count) != 0) return;

    char path_buf[1024];
    char args_buf[2048];
    for (int i = 0; i < count; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(m_engine, i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        m_pluginsModel->appendRow(QString::fromUtf8(path_buf),
                                  QString::fromUtf8(args_buf));
    }
}

// ---------------------------------------------------------------------------
// Writer / Container combos (Slice AA-3.5 full design)
//
// The combos drive a derived view onto the [PLUGINS] section: the user
// picks which plugin handles each role (input writer, results output,
// report).  Apply collects the selected plugin ids and ensures each
// non-empty id has a matching [PLUGINS] row — without disturbing any
// rows the user may have added manually via the table below.
// ---------------------------------------------------------------------------

namespace {

// Find the first plugin id in the current [PLUGINS] section that advertises
// @p role per the engine's grouped discovery.  Returns empty string when
// no such plugin is loaded (caller treats empty as "built-in default").
QString findActivePluginForRole(SWMM_Engine engine, openswmm::PluginRole role)
{
    if (!engine) return {};
    int count = 0;
    if (swmm_plugins_count(engine, &count) != 0 || count == 0) return {};

    // Build the role index once: plugin_id → roles-bitset.
    auto plugins = openswmm::discover_plugins_by_id();

    char path_buf[1024];
    char args_buf[512];  // ignored
    for (int i = 0; i < count; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(engine, i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        const QString rowId = QString::fromUtf8(path_buf);
        for (const auto &p : plugins) {
            if (QString::fromStdString(p.plugin_id) != rowId) continue;
            for (auto r : p.roles) {
                if (r == role) return rowId;
            }
        }
    }
    return {};
}

// True when the plugin advertises all three writer roles (INPUT_READ,
// OUTPUT_WRITE, REPORT_WRITE) — the "Single container" precondition.
bool isTriRolePlugin(const QString &pluginId)
{
    if (pluginId.isEmpty()) return false;
    for (const auto &p : openswmm::discover_plugins_by_id()) {
        if (QString::fromStdString(p.plugin_id) != pluginId) continue;
        bool hasIn = false, hasOut = false, hasRpt = false;
        for (auto r : p.roles) {
            if      (r == openswmm::PluginRole::INPUT_READ)   hasIn  = true;
            else if (r == openswmm::PluginRole::OUTPUT_WRITE) hasOut = true;
            else if (r == openswmm::PluginRole::REPORT_WRITE) hasRpt = true;
        }
        return hasIn && hasOut && hasRpt;
    }
    return false;
}

void selectComboByPluginId(QComboBox *c, const QString &pluginId)
{
    if (!c) return;
    const int idx = c->findData(pluginId);
    c->setCurrentIndex(idx >= 0 ? idx : 0);
}

} // anonymous

void SimulationOptionsDialog::readWriterCombosFromEngine()
{
    selectComboByPluginId(m_inputWriterCombo,
        findActivePluginForRole(m_engine, openswmm::PluginRole::INPUT_READ));
    selectComboByPluginId(m_outputWriterCombo,
        findActivePluginForRole(m_engine, openswmm::PluginRole::OUTPUT_WRITE));
    selectComboByPluginId(m_reportWriterCombo,
        findActivePluginForRole(m_engine, openswmm::PluginRole::REPORT_WRITE));

    updateSingleContainerEnabled();
    if (m_singleContainerBox && m_singleContainerBox->isEnabled()) {
        // Auto-check when all three combos already resolve to the same id
        // — the simulation was previously set up as a single-container.
        const QString in  = m_inputWriterCombo  ? m_inputWriterCombo ->currentData().toString() : QString();
        const QString out = m_outputWriterCombo ? m_outputWriterCombo->currentData().toString() : QString();
        const QString rpt = m_reportWriterCombo ? m_reportWriterCombo->currentData().toString() : QString();
        if (!in.isEmpty() && in == out && in == rpt) {
            QSignalBlocker block(m_singleContainerBox);
            m_singleContainerBox->setChecked(true);
            // Also disable the locked combos so the UI is consistent.
            if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(false);
            if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(false);
        }
    }
}

int SimulationOptionsDialog::writeWriterCombosToEngine()
{
    if (!m_engine) return 0;
    QStringList wanted;
    auto addWanted = [&](QComboBox *c) {
        if (!c) return;
        const QString id = c->currentData().toString();
        if (!id.isEmpty() && !wanted.contains(id)) wanted << id;
    };
    addWanted(m_inputWriterCombo);
    addWanted(m_outputWriterCombo);
    addWanted(m_reportWriterCombo);

    // Collect existing [PLUGINS] row keys to check before inserting.  We
    // never auto-remove rows — the user manages those via the table so
    // any args they added stay intact.
    QSet<QString> existing;
    int count = 0;
    swmm_plugins_count(m_engine, &count);
    char path_buf[1024];
    char args_buf[512];
    for (int i = 0; i < count; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(m_engine, i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        existing.insert(QString::fromUtf8(path_buf));
    }

    int added = 0;
    for (const QString &id : wanted) {
        if (existing.contains(id)) continue;
        const QByteArray utf = id.toUtf8();
        if (swmm_plugin_set(m_engine, utf.constData(), nullptr) == SWMM_OK)
            ++added;
    }
    return added;
}

// ---------------------------------------------------------------------------
// Output / Report file path helpers (Slice AA-4)
// Paths are per-project, stored in QSettings keyed by model file path.
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::readOutputPathsFromSettings()
{
    if (!m_layer || !m_reportFilePathEdit || !m_outputFilePathEdit) return;
    QSettings s;
    const QString base = QStringLiteral("SWMMVis/Project/%1/")
                             .arg(m_layer->modelFilePath());
    m_reportFilePathEdit->setText(s.value(base + QStringLiteral("ReportFilePath")).toString());
    m_outputFilePathEdit->setText(s.value(base + QStringLiteral("OutputFilePath")).toString());
}

void SimulationOptionsDialog::writeOutputPathsToSettings()
{
    if (!m_layer || !m_reportFilePathEdit || !m_outputFilePathEdit) return;
    QSettings s;
    const QString base = QStringLiteral("SWMMVis/Project/%1/")
                             .arg(m_layer->modelFilePath());
    s.setValue(base + QStringLiteral("ReportFilePath"),
               m_reportFilePathEdit->text().trimmed());
    s.setValue(base + QStringLiteral("OutputFilePath"),
               m_outputFilePathEdit->text().trimmed());
}

void SimulationOptionsDialog::browseForReportFile()
{
    using openswmmvis::FileFilterRegistry;
    using openswmmvis::FilterKind;
    auto *reg = FileFilterRegistry::instance();
    const QString filter = reg->filterFor(FilterKind::ReportWrite);
    const QString current = m_reportFilePathEdit ? m_reportFilePathEdit->text().trimmed() : QString();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Choose Report File"),
        current.isEmpty() ? QString() : current,
        filter.isEmpty() ? tr("All Files (*)") : filter);
    if (!path.isEmpty() && m_reportFilePathEdit)
        m_reportFilePathEdit->setText(path);
}

void SimulationOptionsDialog::browseForOutputFile()
{
    using openswmmvis::FileFilterRegistry;
    using openswmmvis::FilterKind;
    auto *reg = FileFilterRegistry::instance();
    const QString filter = reg->filterFor(FilterKind::ResultsWrite);
    const QString current = m_outputFilePathEdit ? m_outputFilePathEdit->text().trimmed() : QString();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Choose Output File"),
        current.isEmpty() ? QString() : current,
        filter.isEmpty() ? tr("All Files (*)") : filter);
    if (!path.isEmpty() && m_outputFilePathEdit)
        m_outputFilePathEdit->setText(path);
}

// ---------------------------------------------------------------------------
// Multi-row SAVE HOTSTART slots (Slice BV-01, 2026-05-21).
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::onHotstartSaveAddRow()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const int row = m_hotstartSavesModel->appendRow(QString{}, 0.0);
    m_hotstartSavesView->setCurrentIndex(
        m_hotstartSavesModel->index(row, HotstartSavesModel::ColPath));
    onHotstartSaveBrowseRow();   // prompt for the save-as path immediately
}

void SimulationOptionsDialog::onHotstartSaveBrowseRow()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row < 0 || row >= m_hotstartSavesModel->rowCount()) return;

    const QString current = m_hotstartSavesModel->pathAt(row).trimmed();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Choose Hot-Start Save File"),
        current.isEmpty() ? QString() : current,
        tr("Hot-start files (*.hsf);;All Files (*)"));
    if (path.isEmpty()) return;
    m_hotstartSavesModel->setData(
        m_hotstartSavesModel->index(row, HotstartSavesModel::ColPath),
        path, Qt::EditRole);
}

void SimulationOptionsDialog::onHotstartSaveRemoveRow()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row < 0) return;
    m_hotstartSavesModel->removeRows(row, 1);
}

void SimulationOptionsDialog::onHotstartSaveMoveRowUp()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row <= 0) return;
    moveHotstartSaveRow(row, row - 1);
}

void SimulationOptionsDialog::onHotstartSaveMoveRowDown()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row < 0 || row >= m_hotstartSavesModel->rowCount() - 1) return;
    moveHotstartSaveRow(row, row + 1);
}

void SimulationOptionsDialog::moveHotstartSaveRow(int from, int to)
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    if (!m_hotstartSavesModel->swapRows(from, to)) return;
    m_hotstartSavesView->setCurrentIndex(
        m_hotstartSavesModel->index(to, HotstartSavesModel::ColPath));
}

void SimulationOptionsDialog::updateSingleContainerEnabled()
{
    if (!m_singleContainerBox || !m_inputWriterCombo) return;
    const QString id = m_inputWriterCombo->currentData().toString();
    const bool eligible = isTriRolePlugin(id);
    m_singleContainerBox->setEnabled(eligible);
    if (!eligible && m_singleContainerBox->isChecked()) {
        QSignalBlocker b(m_singleContainerBox);
        m_singleContainerBox->setChecked(false);
        if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(true);
        if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(true);
    }
}

void SimulationOptionsDialog::onSingleContainerToggled(bool on)
{
    if (!m_inputWriterCombo) return;
    const QString id = m_inputWriterCombo->currentData().toString();

    if (on) {
        selectComboByPluginId(m_outputWriterCombo, id);
        selectComboByPluginId(m_reportWriterCombo, id);
        if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(false);
        if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(false);
    } else {
        if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(true);
        if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(true);
    }
}

void SimulationOptionsDialog::readFilesSectionFromEngine()
{
    if (!m_engine) return;
    char buf[1024];

    auto getStr = [&](const char *key) -> QString {
        buf[0] = '\0';
        if (swmm_files_get(m_engine, key, buf, sizeof(buf)) != SWMM_OK) return {};
        return QString::fromUtf8(buf);
    };
    auto setMode = [&](QComboBox *c, const QString &mode) {
        if (!c) return;
        const int idx = c->findData(mode, Qt::UserRole, Qt::MatchFixedString);
        c->setCurrentIndex(idx >= 0 ? idx : 0);
    };

    if (m_rainfallPathEdit) m_rainfallPathEdit->setPath(getStr("RAINFALL_PATH"));
    setMode(m_rainfallModeCombo, getStr("RAINFALL_MODE"));
    if (m_runoffPathEdit)   m_runoffPathEdit->setPath(getStr("RUNOFF_PATH"));
    setMode(m_runoffModeCombo,   getStr("RUNOFF_MODE"));
    if (m_rdiiPathEdit)     m_rdiiPathEdit->setPath(getStr("RDII_PATH"));
    setMode(m_rdiiModeCombo,     getStr("RDII_MODE"));
    if (m_inflowsPathEdit)  m_inflowsPathEdit->setPath(getStr("INFLOWS_PATH"));
    if (m_outflowsPathEdit) m_outflowsPathEdit->setPath(getStr("OUTFLOWS_PATH"));
    if (m_hotstartUseEdit)  m_hotstartUseEdit->setPath(getStr("HOTSTART_USE_PATH"));

    // Multi-row SAVE HOTSTART table (Slice BV-01).  Phase 3.10.5 — pull
    // each entry from the engine's hotstart_saves vector into the
    // HotstartSavesModel.  Persistent editors get re-opened by the
    // rowsInserted hook installed in buildFilesTab().
    if (m_hotstartSavesModel) {
        m_hotstartSavesModel->clearRows();
        int count = 0;
        if (swmm_hotstart_saves_count(m_engine, &count) != SWMM_OK) count = 0;
        for (int i = 0; i < count; ++i) {
            char pbuf[1024] = {0};
            double dt = 0.0;
            swmm_hotstart_saves_get_path(m_engine, i, pbuf, sizeof(pbuf));
            swmm_hotstart_saves_get_datetime(m_engine, i, &dt);
            m_hotstartSavesModel->appendRow(QString::fromUtf8(pbuf),
                                            dt > 0.0 ? dt : 0.0);
        }
    }
}

int SimulationOptionsDialog::writeFilesSectionToEngine()
{
    if (!m_engine) return 0;
    int written = 0;
    char buf[1024];

    auto getCurrent = [&](const char *key) -> QString {
        buf[0] = '\0';
        swmm_files_get(m_engine, key, buf, sizeof(buf));
        return QString::fromUtf8(buf);
    };

    // Convert absolute paths to paths relative to the .inp directory
    // so the project folder stays portable.  Relative paths pass
    // through unchanged — the engine resolves them against the .inp
    // directory at run time (legacy SWMM5 behaviour).
    const QString inpPath = m_layer ? m_layer->modelFilePath() : QString();
    auto toRelative = [&](const QString &raw) -> QString {
        if (raw.isEmpty() || inpPath.isEmpty()) return raw;
        if (!QDir::isAbsolutePath(raw)) return raw;
        return ProjectSerializer::toRelativePath(raw, inpPath);
    };

    auto writeIfChanged = [&](const char *key, const QString &newVal) {
        if (getCurrent(key) == newVal) return;
        const QByteArray utf = newVal.toUtf8();
        if (swmm_files_set(m_engine, key, utf.constData()) == SWMM_OK)
            ++written;
    };
    auto writePathIfChanged = [&](const char *key, const QString &rawText) {
        writeIfChanged(key, toRelative(rawText.trimmed()));
    };

    if (m_rainfallPathEdit)
        writePathIfChanged("RAINFALL_PATH", m_rainfallPathEdit->absolutePath());
    if (m_rainfallModeCombo)
        writeIfChanged("RAINFALL_MODE",
                       m_rainfallModeCombo->currentData().toString());
    if (m_runoffPathEdit)
        writePathIfChanged("RUNOFF_PATH",   m_runoffPathEdit->absolutePath());
    if (m_runoffModeCombo)
        writeIfChanged("RUNOFF_MODE",
                       m_runoffModeCombo->currentData().toString());
    if (m_rdiiPathEdit)
        writePathIfChanged("RDII_PATH",     m_rdiiPathEdit->absolutePath());
    if (m_rdiiModeCombo)
        writeIfChanged("RDII_MODE",
                       m_rdiiModeCombo->currentData().toString());
    if (m_inflowsPathEdit)
        writePathIfChanged("INFLOWS_PATH",  m_inflowsPathEdit->absolutePath());
    if (m_outflowsPathEdit)
        writePathIfChanged("OUTFLOWS_PATH", m_outflowsPathEdit->absolutePath());
    if (m_hotstartUseEdit)
        writePathIfChanged("HOTSTART_USE_PATH",  m_hotstartUseEdit->absolutePath());

    // Multi-row SAVE HOTSTART (Slice BV-01).  The vector API is simpler
    // to drive than per-slot diffing: snapshot the engine's current
    // entries, compare against the model contents (path + datetime),
    // and only rebuild the vector when something actually changed.
    if (m_hotstartSavesModel) {
        struct HsRow { QString path; double dt; };
        QList<HsRow> desired;
        const int nRows = m_hotstartSavesModel->rowCount();
        for (int row = 0; row < nRows; ++row) {
            const QString rawPath = m_hotstartSavesModel->pathAt(row).trimmed();
            const QString relPath = toRelative(rawPath);
            const double dt       = m_hotstartSavesModel->oaDateAt(row);

            // Skip empty rows entirely — an empty path with no datetime
            // is a stub the user never filled in.
            if (relPath.isEmpty() && dt == 0.0) continue;
            desired.push_back({relPath, dt});
        }

        // Compare against current engine state.
        int curCount = 0;
        swmm_hotstart_saves_count(m_engine, &curCount);
        bool changed = (curCount != desired.size());
        for (int i = 0; !changed && i < curCount; ++i) {
            char pbuf[1024] = {0};
            double curDt = 0.0;
            swmm_hotstart_saves_get_path(m_engine, i, pbuf, sizeof(pbuf));
            swmm_hotstart_saves_get_datetime(m_engine, i, &curDt);
            if (QString::fromUtf8(pbuf) != desired[i].path) changed = true;
            else if (curDt != desired[i].dt)                changed = true;
        }
        if (changed) {
            swmm_hotstart_saves_clear(m_engine);
            for (const auto &r : desired) {
                const QByteArray utf = r.path.toUtf8();
                swmm_hotstart_saves_add(m_engine, utf.constData(), r.dt);
            }
            ++written;
        }
    }
    return written;
}

int SimulationOptionsDialog::writePluginsToEngine()
{
    if (!m_pluginsModel || !m_engine) return 0;

    // Snapshot existing engine rows by key so we can compute the
    // additions, replacements, and removals that the table represents.
    int existingCount = 0;
    swmm_plugins_count(m_engine, &existingCount);

    QHash<QString, QString> existing;
    char path_buf[1024];
    char args_buf[2048];
    for (int i = 0; i < existingCount; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(m_engine, i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        existing.insert(QString::fromUtf8(path_buf),
                        QString::fromUtf8(args_buf));
    }

    int written = 0;
    QSet<QString> seen;
    const int nRows = m_pluginsModel->rowCount();
    for (int row = 0; row < nRows; ++row) {
        const QString key  = m_pluginsModel->pathAt(row).trimmed();
        const QString args = m_pluginsModel->argsAt(row).trimmed();
        if (key.isEmpty()) continue;
        seen.insert(key);

        // Only call set when the row is new or its args changed —
        // avoids spurious dirty-marker bumps in the engine.
        const auto it = existing.constFind(key);
        if (it == existing.constEnd() || it.value() != args) {
            const QByteArray k = key.toUtf8();
            const QByteArray a = args.toUtf8();
            if (swmm_plugin_set(m_engine, k.constData(),
                                args.isEmpty() ? nullptr : a.constData()) == 0)
                ++written;
        }
    }

    // Remove engine rows whose keys are no longer in the table.
    for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
        if (!seen.contains(it.key())) {
            const QByteArray k = it.key().toUtf8();
            if (swmm_plugin_remove(m_engine, k.constData()) == 0)
                ++written;
        }
    }
    return written;
}

int SimulationOptionsDialog::writeToEngine()
{
    int n = 0;
    auto writeIfChanged = [&](const char *key, const QString &current,
                              const QString &newVal) {
        if (current == newVal) return;
        if (setOption(key, newVal)) ++n;
    };

    // Tab 0 — Title / Notes
    if (m_titleNotesEdit) {
        const QString currentHtml = m_titleNotesEdit->toHtml();
        if (currentHtml != m_initialNotesHtml) {
            if (m_engine) {
                const QString plain = m_titleNotesEdit->toPlainText();
                if (swmm_title_clear(m_engine) == 0) {
                    const QByteArray utf8 = plain.toUtf8();
                    if (swmm_title_set(m_engine, utf8.constData()) == 0)
                        ++n;
                }
            }
            if (m_projectWindow) {
                const QString plain = m_titleNotesEdit->toPlainText();
                // Drop the HTML if the document only carries plain text — keeps
                // .oswp tidy and matches the "no notes" empty case.
                m_projectWindow->setNotesHtml(plain.isEmpty() ? QString()
                                                              : currentHtml);
            }
            m_initialNotesHtml = currentHtml;
        }
    }

    // Tab 1
    writeIfChanged("INFILTRATION",       getOption("INFILTRATION"),
                   m_infiltrationCombo->currentData().toString());
    writeIfChanged("FLOW_ROUTING",       getOption("FLOW_ROUTING"),
                   m_routingCombo->currentData().toString());
    writeIfChanged("ALLOW_PONDING",      getOption("ALLOW_PONDING"),
                   engineBoolString(m_allowPondingBox->isChecked()));
    writeIfChanged("SKIP_STEADY_STATE",  getOption("SKIP_STEADY_STATE"),
                   engineBoolString(m_skipSteadyBox->isChecked()));
    // Inverted UI: checked = active = IGNORE_X NO. Unchecked = ignore.
    writeIfChanged("IGNORE_RAINFALL",    getOption("IGNORE_RAINFALL"),
                   engineBoolString(!m_ignoreRainfallBox->isChecked()));
    writeIfChanged("IGNORE_SNOWMELT",    getOption("IGNORE_SNOWMELT"),
                   engineBoolString(!m_ignoreSnowmeltBox->isChecked()));
    writeIfChanged("IGNORE_GROUNDWATER", getOption("IGNORE_GROUNDWATER"),
                   engineBoolString(!m_ignoreGroundwaterBox->isChecked()));
    writeIfChanged("IGNORE_RDII",        getOption("IGNORE_RDII"),
                   engineBoolString(!m_ignoreRDIIBox->isChecked()));
    writeIfChanged("IGNORE_QUALITY",     getOption("IGNORE_QUALITY"),
                   engineBoolString(!m_ignoreQualityBox->isChecked()));
    writeIfChanged("IGNORE_ROUTING",     getOption("IGNORE_ROUTING"),
                   engineBoolString(!m_ignoreRoutingBox->isChecked()));

    // Tab 2 — dates & times
    QString d, t;
    formatEngineDateTime(m_startEdit->dateTime(),       d, t);
    writeIfChanged("START_DATE", getOption("START_DATE"), d);
    writeIfChanged("START_TIME", getOption("START_TIME"), t);

    formatEngineDateTime(m_endEdit->dateTime(),         d, t);
    writeIfChanged("END_DATE",   getOption("END_DATE"),   d);
    writeIfChanged("END_TIME",   getOption("END_TIME"),   t);

    formatEngineDateTime(m_reportStartEdit->dateTime(), d, t);
    writeIfChanged("REPORT_START_DATE", getOption("REPORT_START_DATE"), d);
    writeIfChanged("REPORT_START_TIME", getOption("REPORT_START_TIME"), t);

    writeIfChanged("REPORT_STEP",  getOption("REPORT_STEP"),
                   QString::number(m_reportStepEdit->totalSeconds()));
    writeIfChanged("DRY_STEP",     getOption("DRY_STEP"),
                   QString::number(m_dryStepEdit->totalSeconds()));
    writeIfChanged("WET_STEP",     getOption("WET_STEP"),
                   QString::number(m_wetStepEdit->totalSeconds()));
    {
        const QTime rt = m_ruleStepEdit->time();
        const qint64 ruleSecs = rt.hour() * 3600 + rt.minute() * 60 + rt.second();
        writeIfChanged("RULE_STEP", getOption("RULE_STEP"),
                       QString::number(ruleSecs));
    }
    {
        // Routing step is a plain text box; preserve the user's typed
        // decimal precision but normalise to a canonical %g rendering.
        bool ok = false;
        const double v = m_routingStepEdit->text().trimmed().toDouble(&ok);
        if (ok)
            writeIfChanged("ROUTING_STEP", getOption("ROUTING_STEP"),
                           QString::number(v, 'g', 6));
    }
    writeIfChanged("DRY_DAYS",     getOption("DRY_DAYS"),
                   QString::number(m_dryDaysSpin->value(), 'f', 2));
    writeIfChanged("SWEEP_START",  getOption("SWEEP_START"),
                   m_sweepStartEdit->date().toString(QStringLiteral("MM/dd")));
    writeIfChanged("SWEEP_END",    getOption("SWEEP_END"),
                   m_sweepEndEdit->date().toString(QStringLiteral("MM/dd")));

    // Tab 2 — [EVENTS] (Slice CW). writeEventsToEngine() returns the number
    // of rows it actually pushed; folded into n so wroteChanges flips.
    n += writeEventsToEngine();

    // Tab 3 — Routing & Hydraulics
    writeIfChanged("SURCHARGE_METHOD",    getOption("SURCHARGE_METHOD"),
                   m_surchargeCombo->currentData().toString());
    writeIfChanged("DPS_CELERITY",        getOption("DPS_CELERITY"),
                   QString::number(m_dpsCelerSpin->value(), 'f', 4));
    writeIfChanged("DPS_ALPHA",           getOption("DPS_ALPHA"),
                   QString::number(m_dpsAlphaSpin->value(), 'f', 4));
    writeIfChanged("DPS_DECAY_TIME",      getOption("DPS_DECAY_TIME"),
                   QString::number(m_dpsDecaySpin->value(), 'f', 4));
    writeIfChanged("NODE_CONTINUITY",     getOption("NODE_CONTINUITY"),
                   m_nodeContinuityCombo->currentData().toString());
    writeIfChanged("ANDERSON_ACCEL",      getOption("ANDERSON_ACCEL"),
                   engineBoolString(m_andersonAccelBox->isChecked()));
    writeIfChanged("FORCE_MAIN_EQUATION", getOption("FORCE_MAIN_EQUATION"),
                   m_forceMainCombo->currentData().toString());
    writeIfChanged("NORMAL_FLOW_LIMITED", getOption("NORMAL_FLOW_LIMITED"),
                   m_normalFlowCombo->currentData().toString());
    writeIfChanged("INERTIAL_DAMPING",    getOption("INERTIAL_DAMPING"),
                   m_inertialDampCombo->currentData().toString());
    writeIfChanged("LENGTHENING_STEP",    getOption("LENGTHENING_STEP"),
                   QString::number(m_lengtheningSpin->value(), 'f', 2));
    writeIfChanged("VARIABLE_STEP",       getOption("VARIABLE_STEP"),
                   QString::number(m_variableStepSpin->value(), 'f', 3));
    writeIfChanged("MAX_TRIALS",          getOption("MAX_TRIALS"),
                   QString::number(m_maxTrialsSpin->value()));
    writeIfChanged("HEAD_TOLERANCE",      getOption("HEAD_TOLERANCE"),
                   QString::number(m_headTolSpin->value(), 'f', 6));
    // Engine stores LAT/SYS_FLOW_TOL as fractions; spin shows percent.
    writeIfChanged("LAT_FLOW_TOL",        getOption("LAT_FLOW_TOL"),
                   QString::number(m_latFlowTolSpin->value() / 100.0, 'f', 6));
    writeIfChanged("SYS_FLOW_TOL",        getOption("SYS_FLOW_TOL"),
                   QString::number(m_sysFlowTolSpin->value() / 100.0, 'f', 6));
    writeIfChanged("MIN_SURFAREA",        getOption("MIN_SURFAREA"),
                   QString::number(m_minSurfAreaSpin->value(), 'f', 4));
    writeIfChanged("MIN_SLOPE",           getOption("MIN_SLOPE"),
                   QString::number(m_minSlopeSpin->value(), 'f', 4));

    // Tab 4 — System / Performance
    writeIfChanged("THREADS",             getOption("THREADS"),
                   QString::number(m_threadsSpin->value()));

    // 2D module toggle — QSettings persistence (engine has no native key)
    if (m_module2DBox && m_layer)
    {
        QSettings s;
        const QString key = QStringLiteral("SWMMVis/Project/%1/Module2DEnabled")
                                .arg(m_layer->modelFilePath());
        s.setValue(key, m_module2DBox->isChecked());
    }

    // Tab 6 — 2D (only when compiled in)
#ifdef OPENSWMM_HAS_2D
    write2DToEngine(n);
#endif

    // Tab 7 — Files / Plugins (Slice AA-3.5)
    // Order matters: the table editor reconciles existing rows first
    // (its diff pass can REMOVE rows the user deleted), then the combos
    // run as an additive-only "ensure these are present" pass.  If a
    // combo's pluginId was just removed by the table edit, the combo
    // re-adds it — combos win on conflict, by design.
    n += writePluginsToEngine();
    n += writeFilesSectionToEngine();
    n += writeWriterCombosToEngine();
    n += writeReportContentsToEngine();
    writeOutputPathsToSettings();

    if (n > 0)
        m_wroteChanges = true;
    return n;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::onApply()
{
    // [EVENTS] validation gate (Slice CW).  Block Apply when any row has
    // Start >= End; warn (non-blocking) on overlap / out-of-range so users
    // can still proceed when they know what they're doing.
    QString warn;
    if (!validateEvents(&warn)) {
        QMessageBox::warning(this, tr("Invalid event row"),
            tr("One or more events have Start ≥ End.  Fix the highlighted "
               "rows before applying."));
        return;
    }
    // Files / Output / Plugins validation gate (Phase 3.10.4).  Block on
    // empty plugin id or empty hot-start save path; warn (non-blocking)
    // on softer checks (empty Selected list, missing parent dirs).
    if (!validateFilesTab(&warn)) {
        QMessageBox::warning(this, tr("Invalid Files / Plugins row"),
            tr("One or more rows on the Files or Plugins sub-tab are "
               "missing a required value.  Fix the highlighted rows "
               "before applying."));
        return;
    }
    if (!warn.isEmpty()) {
        const auto choice = QMessageBox::warning(this, tr("Warnings"),
            warn + tr("\nApply anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice != QMessageBox::Yes) return;
    }

    writeToEngine();
    // Re-read after write so the controls reflect whatever the engine
    // actually accepted (some keys may be clamped or normalised).
    readFromEngine();
}

void SimulationOptionsDialog::onAccept()
{
    QString warn;
    if (!validateEvents(&warn)) {
        QMessageBox::warning(this, tr("Invalid event row"),
            tr("One or more events have Start ≥ End.  Fix the highlighted "
               "rows before clicking OK."));
        return;
    }
    if (!validateFilesTab(&warn)) {
        QMessageBox::warning(this, tr("Invalid Files / Plugins row"),
            tr("One or more rows on the Files or Plugins sub-tab are "
               "missing a required value.  Fix the highlighted rows "
               "before clicking OK."));
        return;
    }
    if (!warn.isEmpty()) {
        const auto choice = QMessageBox::warning(this, tr("Warnings"),
            warn + tr("\nApply anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice != QMessageBox::Yes) return;
    }

    writeToEngine();
    accept();
}
