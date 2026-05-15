/*!
 * \file   simulationoptionsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "layers/swmmmodellayer.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "plugins/filefilterregistry.h"
#include "project/projectserializer.h"

#include <openswmm/plugin_sdk/PluginDiscovery.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_spatial.h>

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
                                                 QWidget *parent)
    : QDialog(parent),
      m_engine(engine),
      m_layer(layer)
{
    setWindowTitle(tr("Simulation Options"));
    resize(620, 600);
    buildUi();
    readFromEngine();
    refreshSpatialSummary();
}

// Destructor is now inline in the header to keep the moc vtable self-contained.

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    m_tabs = tabs;  // captured so the 2D-module toggle can flip the tab.
    root->addWidget(tabs, 1);

    buildModelsTab(tabs);
    buildDatesTab(tabs);
    buildHydraulicsTab(tabs);
    buildPerformanceTab(tabs);
    buildSpatialTab(tabs);

    // Mesh configurations — file-management UI, lives outside any
    // OPENSWMM_HAS_2D guard because picking a *.2dm reference is a pure
    // GUI concern (the engine 2D solver isn't required to organise mesh
    // candidates). Tab is enabled/disabled in lockstep with the 2D module
    // toggle on the Models tab.
    buildMeshTab(tabs);
    m_meshTabIndex = tabs->count() - 1;
    if (m_module2DBox)
        tabs->setTabEnabled(m_meshTabIndex, m_module2DBox->isChecked());

#ifdef OPENSWMM_HAS_2D
    build2DTab(tabs);
    m_2DTabIndex = tabs->count() - 1;  // index of the just-added 2D tab.
    if (m_module2DBox)
        tabs->setTabEnabled(m_2DTabIndex, m_module2DBox->isChecked());
#endif

    // Slice AA-3.5 — Files / Plugins tab.  Lives at the end of the tab
    // bar so existing tab numbering is preserved.
    buildFilesTab(tabs);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &SimulationOptionsDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SimulationOptionsDialog::onApply);
}

void SimulationOptionsDialog::buildModelsTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
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
    m_skipSteadyBox    = new QCheckBox(tr("Skip steady-periods (SKIP_STEADY_STATE)"), flagsGroup);
    flagsLay->addWidget(m_allowPondingBox);
    flagsLay->addWidget(m_skipSteadyBox);

    vlay->addWidget(flagsGroup);

    auto *ignoreGroup = new QGroupBox(tr("Ignore processes"), page);
    auto *ignoreLay   = new QVBoxLayout(ignoreGroup);
    m_ignoreRainfallBox    = new QCheckBox(tr("Ignore rainfall (IGNORE_RAINFALL)"),     ignoreGroup);
    m_ignoreSnowmeltBox    = new QCheckBox(tr("Ignore snowmelt (IGNORE_SNOWMELT)"),     ignoreGroup);
    m_ignoreGroundwaterBox = new QCheckBox(tr("Ignore groundwater (IGNORE_GROUNDWATER)"), ignoreGroup);
    m_ignoreRDIIBox        = new QCheckBox(tr("Ignore RDII (IGNORE_RDII)"),              ignoreGroup);
    m_ignoreQualityBox     = new QCheckBox(tr("Ignore water quality (IGNORE_QUALITY)"),  ignoreGroup);
    m_ignoreRoutingBox     = new QCheckBox(tr("Ignore routing (IGNORE_ROUTING)"),        ignoreGroup);
    ignoreLay->addWidget(m_ignoreRainfallBox);
    ignoreLay->addWidget(m_ignoreSnowmeltBox);
    ignoreLay->addWidget(m_ignoreGroundwaterBox);
    ignoreLay->addWidget(m_ignoreRDIIBox);
    ignoreLay->addWidget(m_ignoreQualityBox);
    ignoreLay->addWidget(m_ignoreRoutingBox);
    vlay->addWidget(ignoreGroup);

    vlay->addStretch();

    tabs->addTab(page, tr("Models / Processes"));
}

void SimulationOptionsDialog::buildDatesTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
    auto *form = new QFormLayout(page);

    m_startEdit = new QDateTimeEdit(page);
    m_startEdit->setCalendarPopup(true);
    m_startEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    form->addRow(tr("Start:"), m_startEdit);

    m_endEdit = new QDateTimeEdit(page);
    m_endEdit->setCalendarPopup(true);
    m_endEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    form->addRow(tr("End:"), m_endEdit);

    m_reportStartEdit = new QDateTimeEdit(page);
    m_reportStartEdit->setCalendarPopup(true);
    m_reportStartEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    form->addRow(tr("Report start:"), m_reportStartEdit);

    // Time-step spin boxes — engine stores these in seconds.
    m_reportStepSpin = new QSpinBox(page);
    m_reportStepSpin->setRange(1, 3600 * 24);
    m_reportStepSpin->setSuffix(QStringLiteral(" s"));
    form->addRow(tr("Reporting step:"), m_reportStepSpin);

    m_dryStepSpin = new QSpinBox(page);
    m_dryStepSpin->setRange(1, 3600 * 24);
    m_dryStepSpin->setSuffix(QStringLiteral(" s"));
    form->addRow(tr("Dry-weather step:"), m_dryStepSpin);

    m_wetStepSpin = new QSpinBox(page);
    m_wetStepSpin->setRange(1, 3600 * 24);
    m_wetStepSpin->setSuffix(QStringLiteral(" s"));
    form->addRow(tr("Wet-weather step:"), m_wetStepSpin);

    m_routingStepSpin = new QDoubleSpinBox(page);
    m_routingStepSpin->setRange(0.1, 3600.0);
    m_routingStepSpin->setDecimals(2);
    m_routingStepSpin->setSuffix(QStringLiteral(" s"));
    form->addRow(tr("Routing step:"), m_routingStepSpin);

    m_dryDaysSpin = new QDoubleSpinBox(page);
    m_dryDaysSpin->setRange(0.0, 3650.0);
    m_dryDaysSpin->setDecimals(2);
    m_dryDaysSpin->setSuffix(QStringLiteral(" d"));
    form->addRow(tr("Antecedent dry days:"), m_dryDaysSpin);

    tabs->addTab(page, tr("Dates & Times"));
}

void SimulationOptionsDialog::buildHydraulicsTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
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

    m_latFlowTolSpin = new QDoubleSpinBox(solGroup);
    m_latFlowTolSpin->setRange(0.0, 100.0);
    m_latFlowTolSpin->setSuffix(QStringLiteral(" %"));
    m_latFlowTolSpin->setToolTip(tr("Lateral flow tolerance — engine stores as fraction (LAT_FLOW_TOL)."));
    solForm->addRow(tr("Lateral flow tol:"), m_latFlowTolSpin);

    m_sysFlowTolSpin = new QDoubleSpinBox(solGroup);
    m_sysFlowTolSpin->setRange(0.0, 100.0);
    m_sysFlowTolSpin->setSuffix(QStringLiteral(" %"));
    m_sysFlowTolSpin->setToolTip(tr("System flow tolerance (SYS_FLOW_TOL)."));
    solForm->addRow(tr("System flow tol:"), m_sysFlowTolSpin);

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

    tabs->addTab(page, tr("Routing & Hydraulics"));
}

void SimulationOptionsDialog::buildPerformanceTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
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

    tabs->addTab(page, tr("System / Performance"));
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

// ---------------------------------------------------------------------------
// Tab 5 — Spatial & CRS
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::buildSpatialTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
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

    tabs->addTab(page, tr("Spatial & CRS"));

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

void SimulationOptionsDialog::buildMeshTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
    auto *vlay = new QVBoxLayout(page);

    auto *header = new QLabel(tr(
        "Pick which 2D mesh configuration (.2dm) the engine reads via "
        "[2D_MESH_FILE]. New meshes are generated from the editing "
        "toolbar's Generate Mesh tool — this tab is purely a selector "
        "for existing configurations."), page);
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

    // For the demo cut, Refresh + listing are live. Set Active / Remove
    // become live alongside the MeshGenerationDialog (Slice AU.4) so
    // the .inp [2D_MESH_FILE] retarget logic ships in one place.
    connect(btnRefresh, &QPushButton::clicked, this,
            &SimulationOptionsDialog::refreshMeshList);
    connect(btnSetActive, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, tr("Set Active Mesh"),
            tr("[2D_MESH_FILE] re-targeting lands alongside the "
               "Generate Mesh dialog (Slice AU.4)."));
    });
    connect(btnRemove, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, tr("Remove Mesh"),
            tr("Removal lands alongside the Generate Mesh dialog."));
    });

    refreshMeshList();
    tabs->addTab(page, tr("Mesh"));
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

    if (!modelPath.isEmpty())
    {
        const QStringList meshes = dir.entryList(
            QStringList{QStringLiteral("*.2dm")},
            QDir::Files | QDir::Readable, QDir::Name);
        for (const QString &name : meshes)
            m_meshList->addItem(name);
    }

    // Probe the .inp for a current [2D_MESH_FILE] reference. Keep this
    // tolerant — the section may be absent (engine reads inline mesh).
    QString active;
    if (!modelPath.isEmpty())
    {
        QFile f(modelPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            const QString text = QString::fromUtf8(f.readAll());
            const int sectIdx = text.indexOf(QStringLiteral("[2D_MESH_FILE]"),
                                              0, Qt::CaseInsensitive);
            if (sectIdx >= 0)
            {
                // Walk forward to the first non-comment, non-blank line
                // and pull the FILE token.
                int p = text.indexOf(QChar('\n'), sectIdx);
                while (p > 0 && p < text.size())
                {
                    const int nl = text.indexOf(QChar('\n'), p + 1);
                    const QString line = text.mid(p + 1, (nl < 0 ? text.size() : nl) - p - 1).trimmed();
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
    }
    m_meshActiveLabel->setText(active.isEmpty()
        ? tr("Active mesh reference: <none — engine reads inline mesh, if any>")
        : tr("Active mesh reference: %1").arg(active));
}

void SimulationOptionsDialog::on2DModuleToggled(bool enabled)
{
    // Drive the Mesh + (engine-gated) 2D Surface Routing tabs together.
    // Tabs stay visible either way so the user can preview the layout
    // when 2D is off; only interactive state flips.
    if (m_tabs && m_meshTabIndex >= 0)
        m_tabs->setTabEnabled(m_meshTabIndex, enabled);
#ifdef OPENSWMM_HAS_2D
    if (m_tabs && m_2DTabIndex >= 0)
        m_tabs->setTabEnabled(m_2DTabIndex, enabled);
#endif
}

#ifdef OPENSWMM_HAS_2D

// ---------------------------------------------------------------------------
// Tab 6 — 2D Surface Routing  (only present when the engine ships the 2D module)
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::build2DTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
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

    vlay->addWidget(meshGroup);

    auto *coupGroup = new QGroupBox(tr("1D ↔ 2D coupling"), page);
    auto *coupForm  = new QFormLayout(coupGroup);

    m_couplingCdSpin = new QDoubleSpinBox(coupGroup);
    m_couplingCdSpin->setRange(0.0, 1.0);
    m_couplingCdSpin->setDecimals(4);
    coupForm->addRow(tr("Coupling Cd:"), m_couplingCdSpin);

    m_couplingIntervalSpin = new QDoubleSpinBox(coupGroup);
    m_couplingIntervalSpin->setRange(0.0, 3600.0);
    m_couplingIntervalSpin->setDecimals(2);
    m_couplingIntervalSpin->setSuffix(QStringLiteral(" s"));
    m_couplingIntervalSpin->setSpecialValueText(tr("every step"));
    coupForm->addRow(tr("Coupling interval:"), m_couplingIntervalSpin);

    vlay->addWidget(coupGroup);

    auto *solverGroup = new QGroupBox(tr("Linear solver"), page);
    auto *solverForm  = new QFormLayout(solverGroup);

    m_linearSolverCombo = new QComboBox(solverGroup);
    m_linearSolverCombo->addItem(tr("GMRES"),    QStringLiteral("GMRES"));
    m_linearSolverCombo->addItem(tr("BICGSTAB"), QStringLiteral("BICGSTAB"));
    m_linearSolverCombo->addItem(tr("TFQMR"),    QStringLiteral("TFQMR"));
    solverForm->addRow(tr("Solver:"), m_linearSolverCombo);

    m_preconditionerCombo = new QComboBox(solverGroup);
    m_preconditionerCombo->addItem(tr("None"),   QStringLiteral("NONE"));
    m_preconditionerCombo->addItem(tr("Jacobi"), QStringLiteral("JACOBI"));
    m_preconditionerCombo->addItem(tr("ILU"),    QStringLiteral("ILU"));
    solverForm->addRow(tr("Preconditioner:"), m_preconditionerCombo);

    m_maxKrylovDimSpin = new QSpinBox(solverGroup);
    m_maxKrylovDimSpin->setRange(1, 1000);
    solverForm->addRow(tr("Max Krylov dim:"), m_maxKrylovDimSpin);

    vlay->addWidget(solverGroup);

    m_report2DBox = new QCheckBox(tr("Write 2D results to output (REPORT_2D)"), page);
    vlay->addWidget(m_report2DBox);

    vlay->addStretch();

    tabs->addTab(page, tr("2D Surface Routing"));
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
    m_couplingCdSpin   ->setValue(getExt("COUPLING_CD",       "0.65").toDouble(&ok));
    m_couplingIntervalSpin->setValue(getExt("COUPLING_INTERVAL", "0").toDouble(&ok));

    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0) c->setCurrentIndex(idx);
    };
    selectComboByData(m_linearSolverCombo,   getExt("LINEAR_SOLVER",   "GMRES"));
    selectComboByData(m_preconditionerCombo, getExt("PRECONDITIONER",  "JACOBI"));
    m_maxKrylovDimSpin->setValue(getExt("MAX_KRYLOV_DIM", "30").toInt(&ok));
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
    writeIfChanged("COUPLING_CD",       getExt("COUPLING_CD"),
                   QString::number(m_couplingCdSpin->value(), 'f', 4));
    writeIfChanged("COUPLING_INTERVAL", getExt("COUPLING_INTERVAL"),
                   QString::number(m_couplingIntervalSpin->value(), 'f', 2));
    writeIfChanged("LINEAR_SOLVER",     getExt("LINEAR_SOLVER"),
                   m_linearSolverCombo->currentData().toString());
    writeIfChanged("PRECONDITIONER",    getExt("PRECONDITIONER"),
                   m_preconditionerCombo->currentData().toString());
    writeIfChanged("MAX_KRYLOV_DIM",    getExt("MAX_KRYLOV_DIM"),
                   QString::number(m_maxKrylovDimSpin->value()));
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

    // ---- Tab 1 ---------------------------------------------------------
    selectComboByData(m_infiltrationCombo, getOption("INFILTRATION", QStringLiteral("HORTON")));
    selectComboByData(m_routingCombo,      getOption("FLOW_ROUTING", QStringLiteral("DYNWAVE")));

    m_allowPondingBox->setChecked(parseEngineBool(getOption("ALLOW_PONDING", "NO"))      == Qt::Checked);
    m_skipSteadyBox->setChecked(  parseEngineBool(getOption("SKIP_STEADY_STATE", "NO"))   == Qt::Checked);
    m_ignoreRainfallBox->setChecked(   parseEngineBool(getOption("IGNORE_RAINFALL",    "NO")) == Qt::Checked);
    m_ignoreSnowmeltBox->setChecked(   parseEngineBool(getOption("IGNORE_SNOWMELT",    "NO")) == Qt::Checked);
    m_ignoreGroundwaterBox->setChecked(parseEngineBool(getOption("IGNORE_GROUNDWATER", "NO")) == Qt::Checked);
    m_ignoreRDIIBox->setChecked(       parseEngineBool(getOption("IGNORE_RDII",        "NO")) == Qt::Checked);
    m_ignoreQualityBox->setChecked(    parseEngineBool(getOption("IGNORE_QUALITY",     "NO")) == Qt::Checked);
    m_ignoreRoutingBox->setChecked(    parseEngineBool(getOption("IGNORE_ROUTING",     "NO")) == Qt::Checked);

    // ---- Tab 2 ---------------------------------------------------------
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

    bool ok = false;
    int  rs = getOption("REPORT_STEP", "900").toInt(&ok);
    m_reportStepSpin->setValue(ok ? rs : 900);

    int  ds = getOption("DRY_STEP",    "3600").toInt(&ok);
    m_dryStepSpin->setValue(ok ? ds : 3600);

    int  ws = getOption("WET_STEP",    "300").toInt(&ok);
    m_wetStepSpin->setValue(ok ? ws : 300);

    double routeStep = getOption("ROUTING_STEP", "30").toDouble(&ok);
    m_routingStepSpin->setValue(ok ? routeStep : 30.0);

    double dryDays = getOption("DRY_DAYS", "0").toDouble(&ok);
    m_dryDaysSpin->setValue(ok ? dryDays : 0.0);

    // ---- Tab 3 ---------------------------------------------------------
    selectComboByData(m_surchargeCombo,    getOption("SURCHARGE_METHOD", QStringLiteral("EXTRAN")));
    selectComboByData(m_nodeContinuityCombo, getOption("NODE_CONTINUITY", QStringLiteral("EXPLICIT")));
    selectComboByData(m_forceMainCombo,    getOption("FORCE_MAIN_EQUATION", QStringLiteral("H-W")));
    selectComboByData(m_normalFlowCombo,   getOption("NORMAL_FLOW_LIMITED", QStringLiteral("BOTH")));
    selectComboByData(m_inertialDampCombo, getOption("INERTIAL_DAMPING", QStringLiteral("PARTIAL")));
    m_andersonAccelBox->setChecked(parseEngineBool(getOption("ANDERSON_ACCEL", "NO")) == Qt::Checked);

    m_dpsCelerSpin->setValue(getOption("DPS_CELERITY",    "25").toDouble(&ok));
    m_dpsAlphaSpin->setValue(getOption("DPS_ALPHA",       "3.0").toDouble(&ok));
    m_dpsDecaySpin->setValue(getOption("DPS_DECAY_TIME",  "0.5").toDouble(&ok));
    m_lengtheningSpin->setValue(getOption("LENGTHENING_STEP", "0").toDouble(&ok));
    m_variableStepSpin->setValue(getOption("VARIABLE_STEP",   "0").toDouble(&ok));

    m_maxTrialsSpin->setValue(getOption("MAX_TRIALS", "8").toInt(&ok));
    m_headTolSpin->setValue(getOption("HEAD_TOLERANCE", "0.0015").toDouble(&ok));
    // The engine stores LAT_FLOW_TOL / SYS_FLOW_TOL as fractions but the
    // .inp surface uses percent. swmm_options_get returns the engine's
    // internal value, so multiply by 100 for the spin and divide back on
    // write. Default percent values match the engine's own defaults.
    m_latFlowTolSpin->setValue(getOption("LAT_FLOW_TOL", "0.05").toDouble(&ok) * 100.0);
    m_sysFlowTolSpin->setValue(getOption("SYS_FLOW_TOL", "0.05").toDouble(&ok) * 100.0);
    m_minSurfAreaSpin->setValue(getOption("MIN_SURFAREA", "0").toDouble(&ok));
    m_minSlopeSpin->setValue(getOption("MIN_SLOPE",       "0").toDouble(&ok));

    updateSurchargeFieldsEnabled();

    // ---- Tab 4 ---------------------------------------------------------
    m_threadsSpin->setValue(getOption("THREADS", "0").toInt(&ok));

    // ---- 2D module toggle (Tab 1 → Modules group) ----------------------
    // Persisted per-.inp under QSettings since the engine has no native
    // option for "module enabled" — module activation is implicit in the
    // presence of [2D_VERTICES]/[2D_TRIANGLES] sections. Default OFF when
    // there's no stored preference (matches a fresh project's blank .inp).
    if (m_module2DBox && m_layer)
    {
        QSettings s;
        const QString key = QStringLiteral("SWMMVis/Project/%1/Module2DEnabled")
                                .arg(m_layer->modelFilePath());
        const bool enabled = s.value(key, false).toBool();
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
    readOutputPathsFromSettings();
}

// ---------------------------------------------------------------------------
// Tab 7 — Files / Plugins (Slice AA-3.5)
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::buildFilesTab(QTabWidget *tabs)
{
    auto *page = new QWidget(tabs);
    auto *vlay = new QVBoxLayout(page);

    // ── Writer / Container group ────────────────────────────────────────
    // Three combos let the user pick the plugin driving each role.  The
    // combo's hidden `data()` is the plugin id (empty string for the
    // built-in `.inp` / `.out` / `.rpt` writer).  Picking a non-default
    // entry adds the corresponding [PLUGINS] row on Apply.
    auto *writerGroup = new QGroupBox(tr("Writer / Container"), page);
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
        for (const auto &entry : registry->entriesFor(k)) {
            if (entry.pluginId.isEmpty()) continue;
            if (k != openswmmvis::FilterKind::InputRead) {
                /* OUTPUT_WRITE / REPORT_WRITE are write-only roles */
            } else if (!entry.canWrite) {
                continue;
            }
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

    vlay->addWidget(writerGroup);

    auto *intro = new QLabel(
        tr("Plugins listed in the model's <b>[PLUGINS]</b> section.  Each row "
           "names a writer / output / report plugin (by id, <i>id:version</i>, "
           "or shared-library path) and any free-form arguments to pass to "
           "its initialize() call.  The first input-capable row is also used "
           "by File → Save As when picking a non-<code>.inp</code> "
           "extension."),
        page);
    intro->setWordWrap(true);
    vlay->addWidget(intro);

    m_pluginsTable = new QTableWidget(0, 2, page);
    m_pluginsTable->setHorizontalHeaderLabels(
        {tr("Plugin (path / id / id:version)"), tr("Arguments")});
    m_pluginsTable->horizontalHeader()->setStretchLastSection(true);
    m_pluginsTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    m_pluginsTable->verticalHeader()->setVisible(false);
    m_pluginsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pluginsTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                    | QAbstractItemView::EditKeyPressed
                                    | QAbstractItemView::AnyKeyPressed);
    vlay->addWidget(m_pluginsTable, 1);

    auto *btnRow = new QHBoxLayout();
    m_pluginsAddBtn    = new QPushButton(tr("Add"), page);
    m_pluginsRemoveBtn = new QPushButton(tr("Remove"), page);
    m_pluginsRemoveBtn->setEnabled(false);
    btnRow->addWidget(m_pluginsAddBtn);
    btnRow->addWidget(m_pluginsRemoveBtn);
    btnRow->addStretch();
    vlay->addLayout(btnRow);

    connect(m_pluginsAddBtn, &QPushButton::clicked, this, [this] {
        const int row = m_pluginsTable->rowCount();
        m_pluginsTable->insertRow(row);
        m_pluginsTable->setItem(row, 0, new QTableWidgetItem(QString()));
        m_pluginsTable->setItem(row, 1, new QTableWidgetItem(QString()));
        m_pluginsTable->editItem(m_pluginsTable->item(row, 0));
    });
    connect(m_pluginsRemoveBtn, &QPushButton::clicked, this, [this] {
        const int row = m_pluginsTable->currentRow();
        if (row >= 0) m_pluginsTable->removeRow(row);
    });
    connect(m_pluginsTable, &QTableWidget::itemSelectionChanged, this, [this] {
        m_pluginsRemoveBtn->setEnabled(m_pluginsTable->currentRow() >= 0);
    });

    // ── [FILES] secondary references group ─────────────────────────────
    auto *secondary = new QGroupBox(
        tr("Secondary file references (.inp [FILES] section)"), page);
    auto *secForm = new QFormLayout(secondary);

    auto makeModeCombo = [secondary] {
        auto *c = new QComboBox(secondary);
        c->addItem(tr("(off)"), QString());
        c->addItem(tr("USE"),   QStringLiteral("USE"));
        c->addItem(tr("SAVE"),  QStringLiteral("SAVE"));
        return c;
    };
    auto makePathRow = [secondary, secForm](const QString &label,
                                              QLineEdit **edit,
                                              QComboBox *modeCombo) {
        *edit = new QLineEdit(secondary);
        (*edit)->setPlaceholderText(
            QObject::tr("path relative to the .inp directory"));
        if (modeCombo) {
            auto *row = new QHBoxLayout();
            row->addWidget(*edit, 1);
            row->addWidget(new QLabel(QObject::tr("Mode:"), secondary));
            row->addWidget(modeCombo);
            secForm->addRow(label, row);
        } else {
            secForm->addRow(label, *edit);
        }
    };

    m_rainfallModeCombo = makeModeCombo();
    makePathRow(tr("Rainfall:"), &m_rainfallPathEdit, m_rainfallModeCombo);

    m_runoffModeCombo = makeModeCombo();
    makePathRow(tr("Runoff:"),   &m_runoffPathEdit,   m_runoffModeCombo);

    m_rdiiModeCombo = makeModeCombo();
    makePathRow(tr("RDII:"),     &m_rdiiPathEdit,     m_rdiiModeCombo);

    makePathRow(tr("Inflows (USE only):"),  &m_inflowsPathEdit,  nullptr);
    makePathRow(tr("Outflows (SAVE only):"), &m_outflowsPathEdit, nullptr);
    makePathRow(tr("Hot-start file (USE):"), &m_hotstartUseEdit,  nullptr);
    makePathRow(tr("Hot-start file (SAVE):"), &m_hotstartSaveEdit, nullptr);

    vlay->addWidget(secondary);

    // ── Report file path (Slice AA-4) ────────────────────────────────────
    auto *rptGroup = new QGroupBox(tr("Report file"), page);
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
    vlay->addWidget(rptGroup);

    // ── Output (results) file path (Slice AA-4) ──────────────────────────
    auto *outGroup = new QGroupBox(tr("Results output file"), page);
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
    vlay->addWidget(outGroup);

    tabs->addTab(page, tr("Files"));
}

void SimulationOptionsDialog::readPluginsFromEngine()
{
    if (!m_pluginsTable) return;
    m_pluginsTable->setRowCount(0);
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
        const int row = m_pluginsTable->rowCount();
        m_pluginsTable->insertRow(row);
        m_pluginsTable->setItem(row, 0,
            new QTableWidgetItem(QString::fromUtf8(path_buf)));
        m_pluginsTable->setItem(row, 1,
            new QTableWidgetItem(QString::fromUtf8(args_buf)));
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

    if (m_rainfallPathEdit) m_rainfallPathEdit->setText(getStr("RAINFALL_PATH"));
    setMode(m_rainfallModeCombo, getStr("RAINFALL_MODE"));
    if (m_runoffPathEdit)   m_runoffPathEdit->setText(getStr("RUNOFF_PATH"));
    setMode(m_runoffModeCombo,   getStr("RUNOFF_MODE"));
    if (m_rdiiPathEdit)     m_rdiiPathEdit->setText(getStr("RDII_PATH"));
    setMode(m_rdiiModeCombo,     getStr("RDII_MODE"));
    if (m_inflowsPathEdit)  m_inflowsPathEdit->setText(getStr("INFLOWS_PATH"));
    if (m_outflowsPathEdit) m_outflowsPathEdit->setText(getStr("OUTFLOWS_PATH"));
    if (m_hotstartUseEdit)  m_hotstartUseEdit->setText(getStr("HOTSTART_USE_PATH"));
    if (m_hotstartSaveEdit) m_hotstartSaveEdit->setText(getStr("HOTSTART_SAVE_PATH"));
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
        writePathIfChanged("RAINFALL_PATH", m_rainfallPathEdit->text());
    if (m_rainfallModeCombo)
        writeIfChanged("RAINFALL_MODE",
                       m_rainfallModeCombo->currentData().toString());
    if (m_runoffPathEdit)
        writePathIfChanged("RUNOFF_PATH",   m_runoffPathEdit->text());
    if (m_runoffModeCombo)
        writeIfChanged("RUNOFF_MODE",
                       m_runoffModeCombo->currentData().toString());
    if (m_rdiiPathEdit)
        writePathIfChanged("RDII_PATH",     m_rdiiPathEdit->text());
    if (m_rdiiModeCombo)
        writeIfChanged("RDII_MODE",
                       m_rdiiModeCombo->currentData().toString());
    if (m_inflowsPathEdit)
        writePathIfChanged("INFLOWS_PATH",  m_inflowsPathEdit->text());
    if (m_outflowsPathEdit)
        writePathIfChanged("OUTFLOWS_PATH", m_outflowsPathEdit->text());
    if (m_hotstartUseEdit)
        writePathIfChanged("HOTSTART_USE_PATH",  m_hotstartUseEdit->text());
    if (m_hotstartSaveEdit)
        writePathIfChanged("HOTSTART_SAVE_PATH", m_hotstartSaveEdit->text());
    return written;
}

int SimulationOptionsDialog::writePluginsToEngine()
{
    if (!m_pluginsTable || !m_engine) return 0;

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
    for (int row = 0; row < m_pluginsTable->rowCount(); ++row) {
        auto *pathItem = m_pluginsTable->item(row, 0);
        auto *argsItem = m_pluginsTable->item(row, 1);
        const QString key  = pathItem ? pathItem->text().trimmed() : QString();
        const QString args = argsItem ? argsItem->text().trimmed() : QString();
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

    // Tab 1
    writeIfChanged("INFILTRATION",       getOption("INFILTRATION"),
                   m_infiltrationCombo->currentData().toString());
    writeIfChanged("FLOW_ROUTING",       getOption("FLOW_ROUTING"),
                   m_routingCombo->currentData().toString());
    writeIfChanged("ALLOW_PONDING",      getOption("ALLOW_PONDING"),
                   engineBoolString(m_allowPondingBox->isChecked()));
    writeIfChanged("SKIP_STEADY_STATE",  getOption("SKIP_STEADY_STATE"),
                   engineBoolString(m_skipSteadyBox->isChecked()));
    writeIfChanged("IGNORE_RAINFALL",    getOption("IGNORE_RAINFALL"),
                   engineBoolString(m_ignoreRainfallBox->isChecked()));
    writeIfChanged("IGNORE_SNOWMELT",    getOption("IGNORE_SNOWMELT"),
                   engineBoolString(m_ignoreSnowmeltBox->isChecked()));
    writeIfChanged("IGNORE_GROUNDWATER", getOption("IGNORE_GROUNDWATER"),
                   engineBoolString(m_ignoreGroundwaterBox->isChecked()));
    writeIfChanged("IGNORE_RDII",        getOption("IGNORE_RDII"),
                   engineBoolString(m_ignoreRDIIBox->isChecked()));
    writeIfChanged("IGNORE_QUALITY",     getOption("IGNORE_QUALITY"),
                   engineBoolString(m_ignoreQualityBox->isChecked()));
    writeIfChanged("IGNORE_ROUTING",     getOption("IGNORE_ROUTING"),
                   engineBoolString(m_ignoreRoutingBox->isChecked()));

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
                   QString::number(m_reportStepSpin->value()));
    writeIfChanged("DRY_STEP",     getOption("DRY_STEP"),
                   QString::number(m_dryStepSpin->value()));
    writeIfChanged("WET_STEP",     getOption("WET_STEP"),
                   QString::number(m_wetStepSpin->value()));
    writeIfChanged("ROUTING_STEP", getOption("ROUTING_STEP"),
                   QString::number(m_routingStepSpin->value(), 'f', 2));
    writeIfChanged("DRY_DAYS",     getOption("DRY_DAYS"),
                   QString::number(m_dryDaysSpin->value(), 'f', 2));

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
    writeToEngine();
    // Re-read after write so the controls reflect whatever the engine
    // actually accepted (some keys may be clamped or normalised).
    readFromEngine();
}

void SimulationOptionsDialog::onAccept()
{
    writeToEngine();
    accept();
}
