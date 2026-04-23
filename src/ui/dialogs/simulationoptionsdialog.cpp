/*!
 * \file   simulationoptionsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "layers/swmmmodellayer.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef HAVE_OPENSWMMCORE
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_spatial.h>
#endif

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
    root->addWidget(tabs, 1);

    buildModelsTab(tabs);
    buildDatesTab(tabs);
    buildHydraulicsTab(tabs);
    buildPerformanceTab(tabs);
    buildSpatialTab(tabs);
#ifdef OPENSWMM_HAS_2D
    build2DTab(tabs);
#endif

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
#ifdef HAVE_OPENSWMMCORE
        if (!m_engine) return fallback;
        char buf[256] = {};
        if (swmm_options_get_ext(m_engine, key, buf, sizeof(buf)) == 0)
            return QString::fromUtf8(buf).trimmed();
#endif
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
#ifdef HAVE_OPENSWMMCORE
        if (!m_engine) return {};
        char buf[256] = {};
        if (swmm_options_get_ext(m_engine, key, buf, sizeof(buf)) == 0)
            return QString::fromUtf8(buf).trimmed();
#endif
        return {};
    };
    auto setExt = [&](const char *key, const QString &v) -> bool {
#ifdef HAVE_OPENSWMMCORE
        if (!m_engine) return false;
        return swmm_options_set_ext(m_engine, key, v.toUtf8().constData()) == 0;
#else
        Q_UNUSED(key) Q_UNUSED(v) return false;
#endif
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
#ifdef HAVE_OPENSWMMCORE
    if (!m_engine) return fallback;
    char buf[256] = {};
    if (swmm_options_get(m_engine, key, buf, sizeof(buf)) == 0)
        return QString::fromUtf8(buf).trimmed();
    return fallback;
#else
    Q_UNUSED(key) Q_UNUSED(fallback)
    return fallback;
#endif
}

bool SimulationOptionsDialog::setOption(const char *key, const QString &value)
{
#ifdef HAVE_OPENSWMMCORE
    if (!m_engine) return false;
    const QByteArray v = value.toUtf8();
    return swmm_options_set(m_engine, key, v.constData()) == 0;
#else
    Q_UNUSED(key) Q_UNUSED(value)
    return false;
#endif
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

    // ---- Tab 6 (2D) — only present when compiled in --------------------
#ifdef OPENSWMM_HAS_2D
    read2DFromEngine();
#endif
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

    // Tab 6 — 2D (only when compiled in)
#ifdef OPENSWMM_HAS_2D
    write2DToEngine(n);
#endif

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
