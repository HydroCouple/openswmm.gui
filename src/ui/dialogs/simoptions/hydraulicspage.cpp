/*!
 * \file   hydraulicspage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/hydraulicspage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>

#include "core/preferencesmanager.h"
#include "ui/dialogs/simulationoptionsdialog.h"

namespace openswmmvis::ui
{

HydraulicsPage::HydraulicsPage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
    tagWidgets();
}

QString HydraulicsPage::title() const
{
    return tr("Routing & Hydraulics");
}

QString HydraulicsPage::flowRouting() const
{
    return m_routingCombo ? m_routingCombo->currentData().toString() : QString();
}

void HydraulicsPage::setMinimumStep(double seconds)
{
    if (m_minStepSpin) m_minStepSpin->setValue(seconds);
}

void HydraulicsPage::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // ── Page header: FLOW_ROUTING, always visible above the tab bar ──────
    // It gates three of the four tabs below, so it cannot live inside one of
    // them (PLAN §2.1). Moved here from Models / Processes.
    auto *headerForm = new QFormLayout;
    m_routingCombo = new QComboBox(this);
    m_routingCombo->setObjectName(QStringLiteral("flowRoutingCombo"));
    m_routingCombo->addItem(tr("Steady"),            QStringLiteral("STEADY"));
    m_routingCombo->addItem(tr("Kinematic Wave"),    QStringLiteral("KINWAVE"));
    m_routingCombo->addItem(tr("Dynamic Wave"),      QStringLiteral("DYNWAVE"));
    m_routingCombo->addItem(tr("Finite Volume"),     QStringLiteral("FV"));
    m_routingCombo->setToolTip(
        tr("Flow-routing method for conduits (option FLOW_ROUTING).\n"
           "Finite Volume is the explicit Godunov solver; the tabs below grey "
           "themselves out when they do not apply to the selected method."));
    connect(m_routingCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emit gateInputsChanged(); });
    headerForm->addRow(tr("Flow routing:"), m_routingCombo);
    root->addLayout(headerForm);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("hydraulicsTabs"));
    root->addWidget(m_tabs, 1);

    auto *rtTab = new QWidget(m_tabs); auto *rtLay = new QVBoxLayout(rtTab);
    auto *dwTab = new QWidget(m_tabs); auto *dwLay = new QVBoxLayout(dwTab);
    auto *fvTab = new QWidget(m_tabs); auto *fvLay = new QVBoxLayout(fvTab);
    auto *ufTab = new QWidget(m_tabs); auto *ufLay = new QVBoxLayout(ufTab);


    // ── Surcharge group ────────────────────────────────────────────────
    auto *surGroup = new QGroupBox(tr("Surcharge handling"), this);
    auto *surForm  = new QFormLayout(surGroup);

    m_surchargeCombo = new QComboBox(surGroup);
    m_surchargeCombo->addItem(tr("EXTRAN (legacy)"),  QStringLiteral("EXTRAN"));
    m_surchargeCombo->addItem(tr("SLOT (Preissmann)"), QStringLiteral("SLOT"));
    m_surchargeCombo->addItem(tr("DYNAMIC_SLOT"),     QStringLiteral("DYNAMIC_SLOT"));
    m_surchargeCombo->addItem(tr("TPA (two-component pressure, experimental)"),
                              QStringLiteral("TPA"));
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

    // TPA_CELERITY — only meaningful for the TPA surcharge method
    // (engine issue #156; GUI issue #10).
    m_tpaCeleritySpin = new QDoubleSpinBox(surGroup);
    m_tpaCeleritySpin->setRange(1.0, 5000.0);
    m_tpaCeleritySpin->setDecimals(1);
    m_tpaCeleritySpin->setToolTip(
        tr("Acoustic (pressure-wave) celerity a for the TPA surcharge "
           "method, in project length units per second (TPA_CELERITY). "
           "Sets the pressurized wall compliance w = g·A_full/a²."));
    surForm->addRow(tr("TPA celerity:"), m_tpaCeleritySpin);

    dwLay->addWidget(surGroup);
    connect(m_surchargeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ emit gateInputsChanged(); });

    // ── Solver group ───────────────────────────────────────────────────
    auto *solGroup = new QGroupBox(tr("Solver"), this);
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
    solForm->addRow(tr("Ma&x trials:"), m_maxTrialsSpin);

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
    solForm->addRow(tr("Varia&ble step factor:"), m_variableStepSpin);

    m_minStepSpin = new QDoubleSpinBox(solGroup);
    m_minStepSpin->setRange(0.01, 60.0);
    m_minStepSpin->setSingleStep(0.1);
    m_minStepSpin->setDecimals(3);
    m_minStepSpin->setSuffix(QStringLiteral(" s"));
    m_minStepSpin->setToolTip(
        tr("Smallest routing step the adaptive solver may take (MINIMUM_STEP).\n"
           "In 1D/2D-coupled runs the coupling collapses the 1D step toward this "
           "floor; raising it (e.g. 1.0–1.5 s) recovers most of the runtime with a "
           "small accuracy trade. See the Performance tab's Fast preset."));
    solForm->addRow(tr("Minimum step:"), m_minStepSpin);

    dwLay->addWidget(solGroup);

    // ── Finite-volume solver groups (FLOW_ROUTING FV) ──────────────────
    // Knobs for the explicit Godunov FV routing solver. Both groups are
    // enabled only while the Models / Processes tab's flow-routing combo
    // says FV; the engine accepts FV_* keys as inert under any other
    // routing model, so a greyed-out group never invalidates the project.
    m_fvGroup = new QGroupBox(tr("Finite volume solver"), this);
    auto *fvForm = new QFormLayout(m_fvGroup);

    m_fvCellLengthSpin = new QDoubleSpinBox(m_fvGroup);
    m_fvCellLengthSpin->setRange(0.0, 100000.0);
    m_fvCellLengthSpin->setDecimals(2);
    m_fvCellLengthSpin->setSpecialValueText(tr("whole conduit"));
    m_fvCellLengthSpin->setToolTip(
        tr("Target cell length for conduit discretisation, in project length "
           "units (FV_CELL_LENGTH). 0 = one cell per conduit."));
    fvForm->addRow(tr("Cell length:"), m_fvCellLengthSpin);

    m_fvMinCellsSpin = new QSpinBox(m_fvGroup);
    m_fvMinCellsSpin->setRange(1, 1000);
    m_fvMinCellsSpin->setToolTip(
        tr("Minimum number of cells per conduit (FV_MIN_CELLS)."));
    fvForm->addRow(tr("Min cells per conduit:"), m_fvMinCellsSpin);

    m_fvCflSpin = new QDoubleSpinBox(m_fvGroup);
    m_fvCflSpin->setRange(0.05, 1.0);
    m_fvCflSpin->setSingleStep(0.05);
    m_fvCflSpin->setDecimals(2);
    m_fvCflSpin->setToolTip(
        tr("Courant number the explicit substep targets (FV_CFL)."));
    fvForm->addRow(tr("CFL number:"), m_fvCflSpin);

    m_fvRiemannCombo = new QComboBox(m_fvGroup);
    m_fvRiemannCombo->addItem(tr("HLLC"), QStringLiteral("HLLC"));
    m_fvRiemannCombo->addItem(tr("HLL"),  QStringLiteral("HLL"));
    m_fvRiemannCombo->setToolTip(tr("Approximate Riemann solver for face fluxes (FV_RIEMANN)."));
    fvForm->addRow(tr("Riemann solver:"), m_fvRiemannCombo);

    m_fvOrderCombo = new QComboBox(m_fvGroup);
    m_fvOrderCombo->addItem(tr("1st order"),                  QStringLiteral("1"));
    m_fvOrderCombo->addItem(tr("2nd order (MUSCL-Hancock)"),  QStringLiteral("2"));
    m_fvOrderCombo->setToolTip(tr("Spatial reconstruction order (FV_ORDER)."));
    fvForm->addRow(tr("Spatial order:"), m_fvOrderCombo);

    m_fvLimiterCombo = new QComboBox(m_fvGroup);
    m_fvLimiterCombo->addItem(tr("Minmod"),   QStringLiteral("MINMOD"));
    m_fvLimiterCombo->addItem(tr("van Leer"), QStringLiteral("VANLEER"));
    m_fvLimiterCombo->addItem(tr("Superbee"), QStringLiteral("SUPERBEE"));
    m_fvLimiterCombo->setToolTip(
        tr("Slope limiter for 2nd-order reconstruction (FV_LIMITER)."));
    fvForm->addRow(tr("Slope limiter:"), m_fvLimiterCombo);

    m_fvTimeIntCombo = new QComboBox(m_fvGroup);
    m_fvTimeIntCombo->addItem(tr("Euler"), QStringLiteral("EULER"));
    m_fvTimeIntCombo->addItem(tr("RK2"),   QStringLiteral("RK2"));
    m_fvTimeIntCombo->setToolTip(tr("Substep time integrator (FV_TIME_INTEGRATION)."));
    fvForm->addRow(tr("Time integration:"), m_fvTimeIntCombo);

    m_fvSlotCeleritySpin = new QDoubleSpinBox(m_fvGroup);
    m_fvSlotCeleritySpin->setRange(1.0, 10000.0);
    m_fvSlotCeleritySpin->setDecimals(1);
    m_fvSlotCeleritySpin->setToolTip(
        tr("Preissmann-slot pressure-wave celerity, in project length units "
           "per second (FV_SLOT_CELERITY)."));
    fvForm->addRow(tr("Slot celerity:"), m_fvSlotCeleritySpin);

    // FV_PRESSURE_CLOSURE (engine issue #156; GUI issue #10). FV-only key;
    // the engine accepts it as inert under other routing, matching the FV_*
    // posture above.
    m_fvPressureClosureCombo = new QComboBox(m_fvGroup);
    m_fvPressureClosureCombo->addItem(tr("SLOT (Preissmann)"), QStringLiteral("SLOT"));
    m_fvPressureClosureCombo->addItem(tr("TPA (two-component pressure)"),
                                      QStringLiteral("TPA"));
    m_fvPressureClosureCombo->setToolTip(
        tr("Pressure closure for surcharged FV cells (FV_PRESSURE_CLOSURE). "
           "TPA carries a signed pressure head so sub-atmospheric "
           "full-pipe flow is representable; SLOT is the one-sided "
           "Preissmann slot."));
    fvForm->addRow(tr("Pressure closure:"), m_fvPressureClosureCombo);

    // Surfaced as experimental by explicit decision (2026-08-29): the solve
    // cannot yet compose with local time stepping (tiering stands down on
    // any substep where it engages) and slot program R2b is expected to
    // revise it, but it is fully functional and gated, and needed to
    // experiment with pressurized transmission mains from the GUI.
    m_fvPressImplicitBox = new QCheckBox(
        tr("Implicit pressurized head solve (FV_PRESSURIZED_IMPLICIT, experimental)"),
        m_fvGroup);
    m_fvPressImplicitBox->setToolTip(
        tr("Experimental. Solve surcharged-cell heads implicitly so "
           "pressurized reaches stop binding the CFL substep and full-bore "
           "head loss is Manning-exact regardless of slot celerity "
           "(FV_PRESSURIZED_IMPLICIT). CPU backend only; local time "
           "stepping stands down while the solve engages; free-to-"
           "pressurized transition faces stay explicit. Subject to change "
           "in slot program R2b."));
    fvForm->addRow(QString(), m_fvPressImplicitBox);

    // Not surfaced here on purpose: FV_DISPERSION (inert on every path — the
    // engine warns at open),
    // and the retired FV_NODE_COUPLING / FV_NODE_DT / FV_NODE_PICARD, which
    // the engine hardwires to their former defaults. FV_SCALAR_SCHEME lives
    // on the Quality & Transport page: its only live consumer is the
    // Eulerian ARD engine.

    m_fvStructCouplingCombo = new QComboBox(m_fvGroup);
    m_fvStructCouplingCombo->addItem(tr("Every substep"),      QStringLiteral("SUBSTEP"));
    m_fvStructCouplingCombo->addItem(tr("Every routing step"), QStringLiteral("ROUTING_STEP"));
    m_fvStructCouplingCombo->setToolTip(
        tr("How often weir/orifice/pump flows are re-evaluated "
           "(FV_STRUCTURE_COUPLING)."));
    fvForm->addRow(tr("Structure coupling:"), m_fvStructCouplingCombo);

    fvLay->addWidget(m_fvGroup);

    m_fvPerfGroup = new QGroupBox(tr("Finite volume performance"), this);
    auto *fvPerfForm = new QFormLayout(m_fvPerfGroup);

    m_fvBackendCombo = new QComboBox(m_fvPerfGroup);
    m_fvBackendCombo->addItem(tr("Auto"),  QStringLiteral("AUTO"));
    m_fvBackendCombo->addItem(tr("CPU (serial)"), QStringLiteral("CPU"));
    m_fvBackendCombo->addItem(tr("OpenMP"), QStringLiteral("OMP"));
    m_fvBackendCombo->addItem(tr("CUDA"),  QStringLiteral("CUDA"));
    m_fvBackendCombo->addItem(tr("HIP"),   QStringLiteral("HIP"));
    m_fvBackendCombo->addItem(tr("SYCL"),  QStringLiteral("SYCL"));
    m_fvBackendCombo->setToolTip(
        tr("Compute backend for the FV kernels (FV_BACKEND). Auto picks "
           "based on mesh size and available plugins."));
    fvPerfForm->addRow(tr("Backend:"), m_fvBackendCombo);

    m_fvMinParallelSpin = new QSpinBox(m_fvPerfGroup);
    m_fvMinParallelSpin->setRange(0, 100000000);
    m_fvMinParallelSpin->setSingleStep(1000);
    m_fvMinParallelSpin->setToolTip(
        tr("Cell count below which the solver stays serial "
           "(FV_MIN_PARALLEL_CELLS)."));
    fvPerfForm->addRow(tr("Min parallel cells:"), m_fvMinParallelSpin);

    m_fvCompactionBox = new QCheckBox(tr("Compact dry-cell storage (FV_COMPACTION)"),
                                      m_fvPerfGroup);
    m_fvCompactionBox->setToolTip(
        tr("Skip fully dry reaches in the substep loop (FV_COMPACTION)."));
    fvPerfForm->addRow(QString(), m_fvCompactionBox);

    m_fvLtsBox = new QCheckBox(tr("Local time stepping (FV_LTS)"), m_fvPerfGroup);
    m_fvLtsBox->setToolTip(
        tr("Advance slow cells with larger substeps grouped in tiers "
           "(FV_LTS)."));
    fvPerfForm->addRow(QString(), m_fvLtsBox);

    m_fvLtsTiersSpin = new QSpinBox(m_fvPerfGroup);
    m_fvLtsTiersSpin->setRange(1, 8);
    m_fvLtsTiersSpin->setToolTip(
        tr("Maximum number of local-time-stepping tiers (FV_LTS_MAX_TIERS)."));
    fvPerfForm->addRow(tr("LTS max tiers:"), m_fvLtsTiersSpin);

    m_fvCflCensusSpin = new QSpinBox(m_fvPerfGroup);
    m_fvCflCensusSpin->setRange(1, 10000);
    m_fvCflCensusSpin->setToolTip(
        tr("Substeps between full CFL re-surveys of the mesh "
           "(FV_CFL_CENSUS_INTERVAL). 1 = every substep (exact)."));
    fvPerfForm->addRow(tr("CFL census interval:"), m_fvCflCensusSpin);

    fvLay->addWidget(m_fvPerfGroup);

    // ── Unsteady friction (engine issue #156; GUI issue #10) ───────────
    // Consumed by BOTH the dynamic-wave and FV solvers, so it is its own
    // group gated on FLOW_ROUTING ∈ {DYNWAVE, FV} by the gate table.
    m_ufGroup = new QGroupBox(tr("Unsteady friction"), this);
    auto *ufForm = new QFormLayout(m_ufGroup);

    m_ufMethodCombo = new QComboBox(m_ufGroup);
    m_ufMethodCombo->addItem(tr("None"),      QStringLiteral("NONE"));
    m_ufMethodCombo->addItem(tr("Vitkovsky"), QStringLiteral("VITKOVSKY"));
    m_ufMethodCombo->setToolTip(
        tr("Unsteady (transient) friction model added to the steady friction "
           "slope during rapid transients (UNSTEADY_FRICTION). Applies to "
           "dynamic-wave and finite-volume routing. Vitkovsky "
           "instantaneous-acceleration model per Pinto, Vasconcelos & "
           "Soares (2025)."));
    ufForm->addRow(tr("Method:"), m_ufMethodCombo);

    m_ufK3Spin = new QDoubleSpinBox(m_ufGroup);
    m_ufK3Spin->setRange(0.0, 0.05);
    m_ufK3Spin->setDecimals(3);
    m_ufK3Spin->setSingleStep(0.005);
    m_ufK3Spin->setToolTip(
        tr("Vitkovsky (Brunone-type) coefficient k3 (UF_K3). Used only when "
           "an unsteady-friction method is selected; paper range "
           "0.005–0.020."));
    ufForm->addRow(tr("Coefficient k3:"), m_ufK3Spin);

    ufLay->addWidget(m_ufGroup);

    connect(m_routingCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ emit gateInputsChanged(); });
    connect(m_fvOrderCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ emit gateInputsChanged(); });
    connect(m_fvLtsBox, &QCheckBox::toggled,
            this, [this](bool){ emit gateInputsChanged(); });
    connect(m_ufMethodCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ emit gateInputsChanged(); });

    // ── Conduit / channel group ────────────────────────────────────────
    auto *condGroup = new QGroupBox(tr("Conduit / channel"), this);
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

    rtLay->addWidget(condGroup);
    // ── Skip steady state ───────────────────────────────────────────────
    // On the shared Routing tab, not Dynamic Wave: isInSteadyState() is called
    // from the main routing step with no routing-model guard, so the option is
    // honoured under every method (PLAN §7 Q1).
    // LAT_FLOW_TOL / SYS_FLOW_TOL only matter when SKIP_STEADY_STATE is on
    // — engine treats them as the change thresholds for declaring a period
    // "steady". Grouping the three together makes the dependency clear.
    auto *skipGroup = new QGroupBox(tr("Skip steady state"), this);
    auto *skipForm  = new QFormLayout(skipGroup);

    m_skipSteadyBox = new QCheckBox(tr("Skip steady-periods (SKIP_STEADY_STATE)"),
                                     skipGroup);
    skipForm->addRow(QString(), m_skipSteadyBox);

    m_latFlowTolSpin = new QDoubleSpinBox(skipGroup);
    m_latFlowTolSpin->setRange(0.0, 100.0);
    m_latFlowTolSpin->setSuffix(QStringLiteral(" %"));
    m_latFlowTolSpin->setToolTip(tr("Lateral flow tolerance in percent (LAT_FLOW_TOL)."));
    skipForm->addRow(tr("Lateral flow tol:"), m_latFlowTolSpin);

    m_sysFlowTolSpin = new QDoubleSpinBox(skipGroup);
    m_sysFlowTolSpin->setRange(0.0, 100.0);
    m_sysFlowTolSpin->setSuffix(QStringLiteral(" %"));
    m_sysFlowTolSpin->setToolTip(tr("System flow tolerance (SYS_FLOW_TOL)."));
    skipForm->addRow(tr("System flow tol:"), m_sysFlowTolSpin);

    // Grey out the tolerance spins when skip-steady is off — they remain
    // serialised either way so toggling back on restores the prior values.
    connect(m_skipSteadyBox, &QCheckBox::toggled,
            this, [this](bool){ emit gateInputsChanged(); });

    rtLay->addWidget(skipGroup);

    rtLay->addStretch();
    dwLay->addStretch();
    fvLay->addStretch();
    ufLay->addStretch();

    m_tabs->addTab(rtTab, tr("Routing"));
    m_tabs->addTab(dwTab, tr("Dynamic Wave"));
    m_tabs->addTab(fvTab, tr("Finite Volume"));
    m_tabs->addTab(ufTab, tr("Unsteady Friction"));
}

void HydraulicsPage::tagWidgets()
{
    tagOption(m_routingCombo, "FLOW_ROUTING");
    tagOption(m_skipSteadyBox, "SKIP_STEADY_STATE");
    tagOption(m_surchargeCombo, "SURCHARGE_METHOD");
    tagOption(m_dpsCelerSpin, "DPS_CELERITY");
    tagOption(m_dpsAlphaSpin, "DPS_ALPHA");
    tagOption(m_dpsDecaySpin, "DPS_DECAY_TIME");
    tagOption(m_tpaCeleritySpin, "TPA_CELERITY");
    tagOption(m_ufMethodCombo, "UNSTEADY_FRICTION");
    tagOption(m_ufK3Spin, "UF_K3");
    tagOption(m_nodeContinuityCombo, "NODE_CONTINUITY");
    tagOption(m_andersonAccelBox, "ANDERSON_ACCEL");
    tagOption(m_forceMainCombo, "FORCE_MAIN_EQUATION");
    tagOption(m_normalFlowCombo, "NORMAL_FLOW_LIMITED");
    tagOption(m_inertialDampCombo, "INERTIAL_DAMPING");
    tagOption(m_lengtheningSpin, "LENGTHENING_STEP");
    tagOption(m_variableStepSpin, "VARIABLE_STEP");
    tagOption(m_minStepSpin, "MINIMUM_STEP");
    tagOption(m_maxTrialsSpin, "MAX_TRIALS");
    tagOption(m_headTolSpin, "HEAD_TOLERANCE");
    tagOption(m_latFlowTolSpin, "LAT_FLOW_TOL");
    tagOption(m_sysFlowTolSpin, "SYS_FLOW_TOL");
    tagOption(m_minSurfAreaSpin, "MIN_SURFAREA");
    tagOption(m_minSlopeSpin, "MIN_SLOPE");
    tagOption(m_fvCellLengthSpin, "FV_CELL_LENGTH");
    tagOption(m_fvMinCellsSpin, "FV_MIN_CELLS");
    tagOption(m_fvCflSpin, "FV_CFL");
    tagOption(m_fvRiemannCombo, "FV_RIEMANN");
    tagOption(m_fvOrderCombo, "FV_ORDER");
    tagOption(m_fvLimiterCombo, "FV_LIMITER");
    tagOption(m_fvTimeIntCombo, "FV_TIME_INTEGRATION");
    tagOption(m_fvSlotCeleritySpin, "FV_SLOT_CELERITY");
    tagOption(m_fvPressureClosureCombo, "FV_PRESSURE_CLOSURE");
    tagOption(m_fvPressImplicitBox, "FV_PRESSURIZED_IMPLICIT");
    tagOption(m_fvStructCouplingCombo, "FV_STRUCTURE_COUPLING");
    tagOption(m_fvCompactionBox, "FV_COMPACTION");
    tagOption(m_fvBackendCombo, "FV_BACKEND");
    tagOption(m_fvMinParallelSpin, "FV_MIN_PARALLEL_CELLS");
    tagOption(m_fvLtsBox, "FV_LTS");
    tagOption(m_fvLtsTiersSpin, "FV_LTS_MAX_TIERS");
    tagOption(m_fvCflCensusSpin, "FV_CFL_CENSUS_INTERVAL");
}

void HydraulicsPage::read()
{
    const auto sim = PreferencesManager::instance()->simulationDefaults();
    const auto ynStr = [](bool v) {
        return v ? QStringLiteral("YES") : QStringLiteral("NO");
    };
    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0) c->setCurrentIndex(idx);
    };
    // Numeric option reads: keep the fallback when the engine string fails
    // to parse instead of silently seeding 0 into the spin box (which
    // write() would then persist as a real edit).
    auto optDouble = [this](const char *key, double fallback) {
        bool okNum = false;
        const double v = ctx_.option(key, QString::number(fallback, 'g', 6))
                             .toDouble(&okNum);
        return okNum ? v : fallback;
    };
    auto optInt = [this](const char *key, int fallback) {
        bool okNum = false;
        const int v = ctx_.option(key, QString::number(fallback)).toInt(&okNum);
        return okNum ? v : fallback;
    };
    Q_UNUSED(ynStr)

    selectComboByData(m_routingCombo,      ctx_.option("FLOW_ROUTING", sim.flowRouting));
    m_skipSteadyBox->setChecked(  SimulationOptionsDialog::parseEngineBool(ctx_.option("SKIP_STEADY_STATE", ynStr(sim.skipSteadyState))) == Qt::Checked);
    // ---- Tab 3 ---------------------------------------------------------
    selectComboByData(m_surchargeCombo,      ctx_.option("SURCHARGE_METHOD",    sim.surchargeMethod));
    selectComboByData(m_nodeContinuityCombo, ctx_.option("NODE_CONTINUITY",     sim.nodeContinuity));
    selectComboByData(m_forceMainCombo,      ctx_.option("FORCE_MAIN_EQUATION", sim.forceMainEquation));
    selectComboByData(m_normalFlowCombo,     ctx_.option("NORMAL_FLOW_LIMITED", sim.normalFlowLimited));
    selectComboByData(m_inertialDampCombo,   ctx_.option("INERTIAL_DAMPING",    sim.inertialDamping));
    m_andersonAccelBox->setChecked(SimulationOptionsDialog::parseEngineBool(ctx_.option("ANDERSON_ACCEL",
                                                              ynStr(sim.andersonAccel))) == Qt::Checked);

    // DPS_* knobs are dynamic-slot specific and not surfaced in
    // PreferencesManager — keep engine-side defaults.
    m_dpsCelerSpin->setValue(optDouble("DPS_CELERITY",   25.0));
    m_dpsAlphaSpin->setValue(optDouble("DPS_ALPHA",      3.0));
    m_dpsDecaySpin->setValue(optDouble("DPS_DECAY_TIME", 0.5));
    // TPA_CELERITY follows the DPS_* rule: method-specific, not surfaced in
    // PreferencesManager — fallback is the engine-side default.
    m_tpaCeleritySpin->setValue(optDouble("TPA_CELERITY", 100.0));

    // Unsteady friction (engine issue #156) — prefs-backed like the other
    // method combos on this tab.
    selectComboByData(m_ufMethodCombo,
                      ctx_.option("UNSTEADY_FRICTION", sim.unsteadyFriction));
    m_ufK3Spin->setValue(optDouble("UF_K3", sim.ufK3));

    m_lengtheningSpin->setValue(
        optDouble("LENGTHENING_STEP", sim.lengtheningStepSec));
    // VARIABLE_STEP toggle in prefs zeroes the Courant factor when off.
    const double variablePref = sim.variableStepOn ? sim.variableStepFactor : 0.0;
    m_variableStepSpin->setValue(optDouble("VARIABLE_STEP", variablePref));
    m_minStepSpin->setValue(optDouble("MINIMUM_STEP", sim.minRoutingStepSec));

    m_maxTrialsSpin->setValue(optInt("MAX_TRIALS", sim.maxTrials));
    m_headTolSpin->setValue(optDouble("HEAD_TOLERANCE", sim.headTolerance));
    // LAT_FLOW_TOL / SYS_FLOW_TOL speak percent through the options API on
    // both get and set, mirroring the .inp surface; prefs hold percent too.
    m_latFlowTolSpin->setValue(optDouble("LAT_FLOW_TOL", sim.latFlowTolPct));
    m_sysFlowTolSpin->setValue(optDouble("SYS_FLOW_TOL", sim.sysFlowTolPct));
    // MIN_SURFAREA isn't in prefs; engine default is 0.
    m_minSurfAreaSpin->setValue(optDouble("MIN_SURFAREA", 0.0));
    m_minSlopeSpin->setValue(optDouble("MIN_SLOPE", sim.minSlopePct));

    // FV_* knobs are FV-routing specific and not surfaced in
    // PreferencesManager — fallbacks are the engine-side defaults.
    m_fvCellLengthSpin->setValue(optDouble("FV_CELL_LENGTH", 0.0));
    m_fvMinCellsSpin->setValue(optInt("FV_MIN_CELLS", 4));
    m_fvCflSpin->setValue(optDouble("FV_CFL", 0.5));
    selectComboByData(m_fvRiemannCombo,  ctx_.option("FV_RIEMANN",  QStringLiteral("HLLC")));
    selectComboByData(m_fvOrderCombo,    ctx_.option("FV_ORDER",    QStringLiteral("1")));
    selectComboByData(m_fvLimiterCombo,  ctx_.option("FV_LIMITER",  QStringLiteral("MINMOD")));
    selectComboByData(m_fvTimeIntCombo,  ctx_.option("FV_TIME_INTEGRATION", QStringLiteral("EULER")));
    m_fvSlotCeleritySpin->setValue(optDouble("FV_SLOT_CELERITY", 100.0));
    selectComboByData(m_fvPressureClosureCombo,
                      ctx_.option("FV_PRESSURE_CLOSURE", QStringLiteral("SLOT")));
    m_fvPressImplicitBox->setChecked(
        SimulationOptionsDialog::parseEngineBool(ctx_.option("FV_PRESSURIZED_IMPLICIT",
                                  QStringLiteral("NO"))) == Qt::Checked);
    // FV_SCALAR_SCHEME is NOT here: it stays on Quality & Transport >
    // Eulerian ARD (PLAN §1.4), even though it is an FV_* key.
    selectComboByData(m_fvStructCouplingCombo,
                      ctx_.option("FV_STRUCTURE_COUPLING", QStringLiteral("SUBSTEP")));
    m_fvCompactionBox->setChecked(
        SimulationOptionsDialog::parseEngineBool(ctx_.option("FV_COMPACTION", QStringLiteral("YES"))) == Qt::Checked);
    selectComboByData(m_fvBackendCombo,  ctx_.option("FV_BACKEND",  QStringLiteral("AUTO")));
    m_fvMinParallelSpin->setValue(optInt("FV_MIN_PARALLEL_CELLS", 20000));
    m_fvLtsBox->setChecked(
        SimulationOptionsDialog::parseEngineBool(ctx_.option("FV_LTS", QStringLiteral("YES"))) == Qt::Checked);
    m_fvLtsTiersSpin->setValue(optInt("FV_LTS_MAX_TIERS", 6));
    m_fvCflCensusSpin->setValue(optInt("FV_CFL_CENSUS_INTERVAL", 1));

}

int HydraulicsPage::write()
{
    int n = 0;
    // Tab 3 — Routing & Hydraulics
    n += ctx_.writeIfChanged("SURCHARGE_METHOD",
                   m_surchargeCombo->currentData().toString());
    n += ctx_.writeIfChanged("DPS_CELERITY",
                   QString::number(m_dpsCelerSpin->value(), 'f', 4));
    n += ctx_.writeIfChanged("DPS_ALPHA",
                   QString::number(m_dpsAlphaSpin->value(), 'f', 4));
    n += ctx_.writeIfChanged("DPS_DECAY_TIME",
                   QString::number(m_dpsDecaySpin->value(), 'f', 4));
    n += ctx_.writeIfChanged("TPA_CELERITY",
                   QString::number(m_tpaCeleritySpin->value(), 'f', 1));
    // Unsteady friction (engine issue #156) — consumed by DW and FV; the
    // engine accepts the keys under any routing model.
    n += ctx_.writeIfChanged("UNSTEADY_FRICTION",
                   m_ufMethodCombo->currentData().toString());
    n += ctx_.writeIfChanged("UF_K3",
                   QString::number(m_ufK3Spin->value(), 'f', 3));
    n += ctx_.writeIfChanged("NODE_CONTINUITY",
                   m_nodeContinuityCombo->currentData().toString());
    n += ctx_.writeIfChanged("ANDERSON_ACCEL",
                   SimulationOptionsDialog::engineBoolString(m_andersonAccelBox->isChecked()));
    n += ctx_.writeIfChanged("FORCE_MAIN_EQUATION",
                   m_forceMainCombo->currentData().toString());
    n += ctx_.writeIfChanged("NORMAL_FLOW_LIMITED",
                   m_normalFlowCombo->currentData().toString());
    n += ctx_.writeIfChanged("INERTIAL_DAMPING",
                   m_inertialDampCombo->currentData().toString());
    n += ctx_.writeIfChanged("LENGTHENING_STEP",
                   QString::number(m_lengtheningSpin->value(), 'f', 2));
    n += ctx_.writeIfChanged("VARIABLE_STEP",
                   QString::number(m_variableStepSpin->value(), 'f', 3));
    n += ctx_.writeIfChanged("MINIMUM_STEP",
                   QString::number(m_minStepSpin->value(), 'f', 3));
    n += ctx_.writeIfChanged("MAX_TRIALS",
                   QString::number(m_maxTrialsSpin->value()));
    n += ctx_.writeIfChanged("HEAD_TOLERANCE",
                   QString::number(m_headTolSpin->value(), 'f', 6));
    // LAT/SYS_FLOW_TOL speak percent through the options API, matching the
    // spin display and the .inp surface.
    n += ctx_.writeIfChanged("LAT_FLOW_TOL",
                   QString::number(m_latFlowTolSpin->value(), 'f', 2));
    n += ctx_.writeIfChanged("SYS_FLOW_TOL",
                   QString::number(m_sysFlowTolSpin->value(), 'f', 2));
    n += ctx_.writeIfChanged("MIN_SURFAREA",
                   QString::number(m_minSurfAreaSpin->value(), 'f', 4));
    n += ctx_.writeIfChanged("MIN_SLOPE",
                   QString::number(m_minSlopeSpin->value(), 'f', 4));

    // Tab 3 — Finite volume solver. Written regardless of the routing
    // selection: the engine accepts FV_* keys as inert under non-FV routing
    // and its InpWriter only persists them when FLOW_ROUTING is FV.
    n += ctx_.writeIfChanged("FV_CELL_LENGTH",
                   QString::number(m_fvCellLengthSpin->value(), 'f', 2));
    n += ctx_.writeIfChanged("FV_MIN_CELLS",
                   QString::number(m_fvMinCellsSpin->value()));
    n += ctx_.writeIfChanged("FV_CFL",
                   QString::number(m_fvCflSpin->value(), 'f', 2));
    n += ctx_.writeIfChanged("FV_RIEMANN",
                   m_fvRiemannCombo->currentData().toString());
    n += ctx_.writeIfChanged("FV_ORDER",
                   m_fvOrderCombo->currentData().toString());
    n += ctx_.writeIfChanged("FV_LIMITER",
                   m_fvLimiterCombo->currentData().toString());
    n += ctx_.writeIfChanged("FV_TIME_INTEGRATION",
                   m_fvTimeIntCombo->currentData().toString());
    n += ctx_.writeIfChanged("FV_SLOT_CELERITY",
                   QString::number(m_fvSlotCeleritySpin->value(), 'f', 1));
    n += ctx_.writeIfChanged("FV_PRESSURE_CLOSURE",
                   m_fvPressureClosureCombo->currentData().toString());
    n += ctx_.writeIfChanged("FV_PRESSURIZED_IMPLICIT",
                   SimulationOptionsDialog::engineBoolString(m_fvPressImplicitBox->isChecked()));
    // FV_SCALAR_SCHEME is edited on the Quality & Transport page (ARD group)
    // but is an FV_* key; written with its siblings so the engine sees one
    // coherent FV block.
    n += ctx_.writeIfChanged("FV_STRUCTURE_COUPLING",
                   m_fvStructCouplingCombo->currentData().toString());
    n += ctx_.writeIfChanged("FV_COMPACTION",
                   SimulationOptionsDialog::engineBoolString(m_fvCompactionBox->isChecked()));
    n += ctx_.writeIfChanged("FV_BACKEND",
                   m_fvBackendCombo->currentData().toString());
    n += ctx_.writeIfChanged("FV_MIN_PARALLEL_CELLS",
                   QString::number(m_fvMinParallelSpin->value()));
    n += ctx_.writeIfChanged("FV_LTS",
                   SimulationOptionsDialog::engineBoolString(m_fvLtsBox->isChecked()));
    n += ctx_.writeIfChanged("FV_LTS_MAX_TIERS",
                   QString::number(m_fvLtsTiersSpin->value()));
    n += ctx_.writeIfChanged("FV_CFL_CENSUS_INTERVAL",
                   QString::number(m_fvCflCensusSpin->value()));

    return n;
}

void HydraulicsPage::refreshGates()
{
    // Intra-page widget gates. The dialog owns rows and tabs; a page owns the
    // widgets inside it, so these never leave this class (PLAN §4.3 "widget"
    // rows). Setting a control on a disabled group is harmless — Qt ANDs the
    // enabled state down the tree.
    const QString surcharge =
        m_surchargeCombo ? m_surchargeCombo->currentData().toString() : QString();
    const bool dyn = surcharge == QLatin1String("DYNAMIC_SLOT");
    const bool tpa = surcharge == QLatin1String("TPA");
    if (m_dpsCelerSpin)    m_dpsCelerSpin->setEnabled(dyn);
    if (m_dpsAlphaSpin)    m_dpsAlphaSpin->setEnabled(dyn);
    if (m_dpsDecaySpin)    m_dpsDecaySpin->setEnabled(dyn);
    if (m_tpaCeleritySpin) m_tpaCeleritySpin->setEnabled(tpa && ctx_.caps().tpa);

    if (m_fvLimiterCombo && m_fvOrderCombo)
        m_fvLimiterCombo->setEnabled(
            m_fvOrderCombo->currentData().toString() == QLatin1String("2"));
    if (m_fvLtsTiersSpin && m_fvLtsBox)
        m_fvLtsTiersSpin->setEnabled(m_fvLtsBox->isChecked());
    if (m_ufK3Spin && m_ufMethodCombo)
        m_ufK3Spin->setEnabled(m_ufMethodCombo->currentData().toString()
                                   != QLatin1String("NONE"));

    // The tolerances are serialised either way, so toggling skip-steady back
    // on restores the prior values.
    const bool skip = m_skipSteadyBox && m_skipSteadyBox->isChecked();
    if (m_latFlowTolSpin) m_latFlowTolSpin->setEnabled(skip);
    if (m_sysFlowTolSpin) m_sysFlowTolSpin->setEnabled(skip);
}

void HydraulicsPage::applyCapabilities()
{
    const EngineCapabilities &c = ctx_.caps();
    const QString legacyTip = tr("Not available in SWMM 5 (legacy engine).");
    const QString mixedFlowTip =
        c.legacy ? legacyTip
                 : tr("This engine build predates the mixed-flow "
                      "(TPA / unsteady-friction) option surface.");

    // Disable one item of a combo the way the original did: the VALUE cannot
    // exist on this engine, so the item goes grey and the selection falls back.
    const auto disableItem = [](QComboBox *combo, const char *data,
                                const QString &tip, int fallbackIdx) {
        if (!combo) return;
        auto *model = qobject_cast<QStandardItemModel *>(combo->model());
        if (!model) return;
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toString() == QLatin1String(data)) {
                model->item(i)->setEnabled(false);
                model->item(i)->setToolTip(tip);
                if (combo->currentIndex() == i) combo->setCurrentIndex(fallbackIdx);
                break;
            }
        }
    };

    if (!c.fv) {
        const int dyn = m_routingCombo
            ? m_routingCombo->findData(QStringLiteral("DYNWAVE")) : 0;
        disableItem(m_routingCombo, "FV",
                    c.legacy ? legacyTip
                             : tr("This engine build predates the "
                                  "finite-volume solver."),
                    dyn >= 0 ? dyn : 0);
    }
    if (!c.tpa) {
        disableItem(m_surchargeCombo, "TPA", mixedFlowTip, 0);   // fall back to EXTRAN
        if (m_tpaCeleritySpin) {
            m_tpaCeleritySpin->setEnabled(false);
            m_tpaCeleritySpin->setToolTip(mixedFlowTip);
        }
    }
    if (!c.fvPressure && m_fvPressureClosureCombo) {
        // Explicit child disable is sticky in Qt, so the FV tab gate
        // re-enabling its group cannot resurrect this one.
        m_fvPressureClosureCombo->setEnabled(false);
        m_fvPressureClosureCombo->setToolTip(mixedFlowTip);
    }
    if (!c.uf && m_ufGroup)
        m_ufGroup->setToolTip(mixedFlowTip);
    if (!c.dynSlot)
        disableItem(m_surchargeCombo, "DYNAMIC_SLOT", legacyTip, 0);
    if (!c.semiImplicit)
        disableItem(m_nodeContinuityCombo, "SEMI_IMPLICIT", legacyTip, 0);
    if (!c.andersonAccel && m_andersonAccelBox) {
        m_andersonAccelBox->setEnabled(false);
        m_andersonAccelBox->setToolTip(legacyTip);
    }
}

} // namespace openswmmvis::ui
