/*!
 * \file   twodpage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/twodpage.h"

#ifdef OPENSWMM_HAS_2D

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <qcustomeditors.h>

#include <openswmm/engine/openswmm_gw_transport.h>   // U5: [GW_*] row count
#include <openswmm/engine/openswmm_infil2d.h>        // U1: INFILTRATION AUTO label

#include <algorithm>
#include <vector>

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/initialqualitydialog.h"
#include "ui/dialogs/simulationoptionsdialog.h"

namespace openswmmvis::ui
{

TwoDPage::TwoDPage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
    tagWidgets();
}

QString TwoDPage::title() const
{
    return tr("2D Surface Routing");
}

void TwoDPage::setScheduleHooks(ScheduleHooks hooks)
{
    m_schedule = std::move(hooks);
}

bool TwoDPage::transport2DEnabled(int speciesClass) const
{
    return speciesClass >= 0 && speciesClass < 4 && m_transport2DBox[speciesClass]
        && m_transport2DBox[speciesClass]->isChecked();
}

void TwoDPage::setTransport2DEnabled(int speciesClass, bool on)
{
    if (speciesClass < 0 || speciesClass >= 4) return;
    if (QCheckBox *box = m_transport2DBox[speciesClass]) {
        if (!box->isEnabled()) return;
        QSignalBlocker b(box);   // the caller already knows; don't echo back
        box->setChecked(on);
    }
}

void TwoDPage::scheduleChanged()
{
    // Mirror the two project steps this page can follow, then re-estimate.
    if (m_report2DStepSameBox && m_report2DStepSameBox->isChecked() &&
        m_report2DStepEdit && m_schedule.reportStepSeconds)
        m_report2DStepEdit->setTotalSeconds(m_schedule.reportStepSeconds());
    if (m_infil2DStepSameBox && m_infil2DStepSameBox->isChecked() &&
        m_infil2DStepEdit && m_schedule.wetStepSeconds)
        m_infil2DStepEdit->setTotalSeconds(m_schedule.wetStepSeconds());
    updateOutputSizeEstimate();
}

void TwoDPage::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // T5 — eight stacked groups became five tabs (PLAN §2). The Processes tab
    // is the one §1.3 reserved "after Coupling": its content (the 2D process
    // switches and the subsurface groundwater enables) landed after the plan
    // was written, so the reservation is now real rather than a placeholder.
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("twoDTabs"));
    root->addWidget(m_tabs, 1);

    auto *t2Hyd = new QWidget(m_tabs); auto *t2HydLay = new QVBoxLayout(t2Hyd);
    auto *t2Msh = new QWidget(m_tabs); auto *t2MshLay = new QVBoxLayout(t2Msh);
    auto *t2Cpl = new QWidget(m_tabs); auto *t2CplLay = new QVBoxLayout(t2Cpl);
    auto *t2Prc = new QWidget(m_tabs); auto *t2PrcLay = new QVBoxLayout(t2Prc);
    auto *t2Out = new QWidget(m_tabs); auto *t2OutLay = new QVBoxLayout(t2Out);

    // The explicit local-inertial marcher is the only 2D integrator (D2
    // retirement of the CVODE/ARKODE stack, 2026-07-29) — no selector, and
    // the marcher settings are always live.
    auto *stepGroup = new QGroupBox(tr("Time stepping"), t2Hyd);
    auto *stepForm  = new QFormLayout(stepGroup);

    m_maxTimestepSpin = new QDoubleSpinBox(stepGroup);
    m_maxTimestepSpin->setRange(0.001, 3600.0);
    m_maxTimestepSpin->setDecimals(4);
    m_maxTimestepSpin->setSuffix(QStringLiteral(" s"));
    m_maxTimestepSpin->setToolTip(
        tr("Upper bound on the marcher's CFL substeps and on the 1D↔2D "
           "co-advance sync batch (default 10 s)."));
    stepForm->addRow(tr("Max timestep:"), m_maxTimestepSpin);

    t2HydLay->addWidget(stepGroup);

    m_marcherGroup = new QGroupBox(tr("Explicit marcher"), t2Hyd);
    auto *marchForm = new QFormLayout(m_marcherGroup);

    m_thetaSpin = new QDoubleSpinBox(m_marcherGroup);
    m_thetaSpin->setRange(0.05, 1.0);
    m_thetaSpin->setDecimals(2);
    m_thetaSpin->setSingleStep(0.05);
    m_thetaSpin->setToolTip(
        tr("θ-weighting of the face discharge in the momentum update. 1.0 is "
           "the classic Bates (2010) scheme; values below 1 blend in the "
           "neighbour discharges, damping thin-film checkerboarding on steep "
           "faces (default 0.8)."));
    marchForm->addRow(tr("Momentum &θ:"), m_thetaSpin);

    m_cflNumberSpin = new QDoubleSpinBox(m_marcherGroup);
    m_cflNumberSpin->setRange(0.05, 1.0);
    m_cflNumberSpin->setDecimals(2);
    m_cflNumberSpin->setSingleStep(0.05);
    m_cflNumberSpin->setToolTip(
        tr("Courant safety factor α on each cell's stable step "
           "dt = α·L/√(g·h) (default 0.7)."));
    marchForm->addRow(tr("CFL number:"), m_cflNumberSpin);

    m_ltsTiersSpin = new QSpinBox(m_marcherGroup);
    m_ltsTiersSpin->setRange(1, 8);
    m_ltsTiersSpin->setToolTip(
        tr("Local-timestepping tiers K: cells march at power-of-two multiples "
           "of the finest step (tier k fires every 2^k substeps). 1 = global "
           "timestep (debug/equivalence mode); raise toward 7–8 for meshes "
           "with extreme cell-size disparity (default 4)."));
    marchForm->addRow(tr("LTS tiers:"), m_ltsTiersSpin);

    m_hMoveSpin = new QDoubleSpinBox(m_marcherGroup);
    m_hMoveSpin->setRange(0.0, 1.0);
    m_hMoveSpin->setDecimals(4);
    m_hMoveSpin->setSuffix(QStringLiteral(" m"));
    m_hMoveSpin->setToolTip(
        tr("Movement threshold: cells shallower than this stay in the lazy "
           "(source-only) set — rain over thin films costs nothing until "
           "water must move. Raising it shrinks the active set and speeds "
           "up drainage tails (default 0.003 m)."));
    marchForm->addRow(tr("Movement threshold:"), m_hMoveSpin);

    // MOMENTUM_EQUATION (engine 2026-09-06): the marcher's momentum closure.
    m_momentum2DCombo = new QComboBox(m_marcherGroup);
    m_momentum2DCombo->addItem(tr("Local inertial (default)"),
                               QStringLiteral("LOCAL_INERTIAL"));
    m_momentum2DCombo->addItem(tr("Full shallow-water (HLLC, shock capturing)"),
                               QStringLiteral("FULL_SWE"));
    m_momentum2DCombo->addItem(tr("Diffusive wave"),
                               QStringLiteral("DIFFUSIVE_WAVE"));
    m_momentum2DCombo->setToolTip(
        tr("Momentum closure of the explicit 2D marcher. Local inertial: "
           "de Almeida–Bates face update (valid Fr < ~0.5). Full shallow-water: "
           "conservative Godunov/HLLC with the convective term — transcritical "
           "flow, hydraulic jumps and dam breaks; 2–3× the cost. Diffusive "
           "wave: Manning quasi-steady flux, no inertia (Δx²-bound steps)."));
    marchForm->addRow(tr("Momentum equation:"), m_momentum2DCombo);

    m_reconOrder2DSpin = new QSpinBox(m_marcherGroup);
    m_reconOrder2DSpin->setRange(1, 2);
    m_reconOrder2DSpin->setToolTip(
        tr("Full shallow-water only: 1 = first-order Godunov, 2 = MUSCL "
           "(Barth–Jespersen) + SSP-RK2 (runs in global-dt mode)."));
    marchForm->addRow(tr("Reconstruction order:"), m_reconOrder2DSpin);

    m_froudeMaxSpin = new QDoubleSpinBox(m_marcherGroup);
    m_froudeMaxSpin->setRange(0.1, 5.0);
    m_froudeMaxSpin->setDecimals(2);
    m_froudeMaxSpin->setSingleStep(0.1);
    m_froudeMaxSpin->setToolTip(
        tr("Froude-number cap on face discharge — the supercritical guard for "
           "the local-inertial scheme (default 1.5)."));
    marchForm->addRow(tr("Max Froude number:"), m_froudeMaxSpin);

    m_advection2DBox = new QCheckBox(
        tr("Convective momentum flux (ADVECTION)"), m_marcherGroup);
    m_advection2DBox->setToolTip(
        tr("Include the convective momentum flux at interior faces "
           "(Stelling–Duinmeijer staggered upwind form). Restores velocity "
           "head on transcritical reaches and correct bore states; off "
           "reproduces the established pure local-inertial results."));
    marchForm->addRow(QString(), m_advection2DBox);

    t2HydLay->addWidget(m_marcherGroup);

    // Same shape as the FV tab's "Finite volume performance" group: the
    // model's own backend request, persisted as [2D_OPTIONS] BACKEND.
    auto *perfGroup = new QGroupBox(tr("Performance"), t2Out);
    auto *perfForm  = new QFormLayout(perfGroup);

    m_backend2DCombo = new QComboBox(perfGroup);
    m_backend2DCombo->addItem(tr("Auto"),                   QStringLiteral("AUTO"));
    m_backend2DCombo->addItem(tr("CPU (built-in marcher)"), QStringLiteral("CPU"));
    m_backend2DCombo->addItem(tr("OpenMP (Kokkos)"),        QStringLiteral("OMP"));
    m_backend2DCombo->addItem(tr("CUDA"),                   QStringLiteral("CUDA"));
    m_backend2DCombo->addItem(tr("HIP"),                    QStringLiteral("HIP"));
    m_backend2DCombo->addItem(tr("SYCL"),                   QStringLiteral("SYCL"));
    m_backend2DCombo->setToolTip(
        tr("Which marcher implementation runs the 2D mesh (BACKEND). Auto "
           "prefers an installed GPU plugin above the device mesh-size floor, "
           "then the OpenMP plugin above its own floor, else the built-in "
           "CPU marcher. A named backend loads that plugin outright, "
           "regardless of mesh size; if it is not installed or has no usable "
           "device the run falls back to CPU with a notice. Pin CPU when a "
           "model measures slower on the accelerator. The OPENSWMM_2D_BACKEND "
           "environment variable overrides this setting when set."));
    perfForm->addRow(tr("Backend:"), m_backend2DCombo);

    t2OutLay->addWidget(perfGroup);

    // Not "Mesh": every knob here is a depth below which the solver stops
    // treating a cell as wet. Nothing in it describes the mesh.
    auto *depthGroup = new QGroupBox(tr("Depth thresholds"), t2Msh);
    auto *meshForm  = new QFormLayout(depthGroup);

    m_dryDepthSpin = new QDoubleSpinBox(depthGroup);
    m_dryDepthSpin->setRange(0.0, 1.0);
    m_dryDepthSpin->setDecimals(6);
    m_dryDepthSpin->setSuffix(QStringLiteral(" m"));
    meshForm->addRow(tr("Dry depth threshold:"), m_dryDepthSpin);

    m_limiterEpsSpin = new QDoubleSpinBox(depthGroup);
    m_limiterEpsSpin->setRange(0.0, 1.0);
    m_limiterEpsSpin->setDecimals(9);
    meshForm->addRow(tr("Limiter epsilon:"), m_limiterEpsSpin);

    m_fluxDhEpsSpin = new QDoubleSpinBox(depthGroup);
    m_fluxDhEpsSpin->setRange(0.0, 1.0);
    m_fluxDhEpsSpin->setDecimals(6);
    m_fluxDhEpsSpin->setSuffix(QStringLiteral(" m"));
    m_fluxDhEpsSpin->setToolTip(
        tr("Head-difference regularization for the 2D diffusive-wave flux. "
           "0.004 m is the current recommended default from the road/weir "
           "performance trials."));
    meshForm->addRow(tr("Flux head epsilon:"), m_fluxDhEpsSpin);

    t2MshLay->addWidget(depthGroup);

    auto *closureGroup = new QGroupBox(tr("Cell closure"), t2Msh);
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
           "Supported on every backend (serial and Kokkos)."));
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
        tr("Wetted-area-fraction floor ε that regularizes the VFR closure as a "
           "cell dries (bounds dη/dV). Only used when Cell closure = VFR "
           "(default 0.01)."));
    closureForm->addRow(tr("VFR min wet fraction:"), m_vfrMinWetFracSpin);

    t2MshLay->addWidget(closureGroup);

    auto *coupGroup = new QGroupBox(tr("1D ↔ 2D coupling"), t2Cpl);
    auto *coupForm  = new QFormLayout(coupGroup);

    m_couplingCdSpin = new QDoubleSpinBox(coupGroup);
    m_couplingCdSpin->setRange(0.0, 1.0);
    m_couplingCdSpin->setDecimals(4);
    coupForm->addRow(tr("Coupling Cd:"), m_couplingCdSpin);

    m_couplingSyncSpin = new QDoubleSpinBox(coupGroup);
    m_couplingSyncSpin->setRange(0.0, 60.0);
    m_couplingSyncSpin->setDecimals(1);
    m_couplingSyncSpin->setSingleStep(5.0);
    m_couplingSyncSpin->setSuffix(QStringLiteral(" s"));
    m_couplingSyncSpin->setSpecialValueText(tr("Every routing step"));
    m_couplingSyncSpin->setToolTip(
        tr("How often 1D↔2D exchange volumes are settled (COUPLING_SYNC). "
           "0 (default) couples every routing step — tightest feedback, "
           "needed for fast fill-and-spill ponds behind weirs/culverts. "
           "A batching interval (clamped to [routing step, 60 s]) advances "
           "the 2D in longer spans and delivers exchange to the 1D spread "
           "over the following span — much faster on large meshes, but the "
           "one-span feedback delay can ring on rapidly filling ponds."));
    coupForm->addRow(tr("Exchange interval:"), m_couplingSyncSpin);

    m_couplingAreaAutoBox = new QCheckBox(
        tr("Derive exchange areas automatically (COUPLING_AREA AUTO)"),
        coupGroup);
    m_couplingAreaAutoBox->setToolTip(
        tr("Override every coupling point's exchange area with "
           "1.25 × the largest connected conduit area (clamped 0.05–2 m²). "
           "Recommended when the mesh authored default areas much larger than "
           "the pipes they feed — oversized areas drive fill-and-spill churn. "
           "Explicit AREA values in the input are replaced while this is on."));
    coupForm->addRow(QString(), m_couplingAreaAutoBox);

    t2CplLay->addWidget(coupGroup);

    // U1 (2026-09-07) — Processes group: the 2D process enables of E2
    // ([2D_OPTIONS] INFILTRATION / INFIL_STEP / INFIL_DEFAULT_METHOD /
    // INFIL_DESTINATION, EVAPORATION, TRANSPORT_*) beside RAINFALL_MODE.
    auto *rainfallGroup = new QGroupBox(tr("Processes"), t2Prc);
    auto *rainfallForm  = new QFormLayout(rainfallGroup);

    m_rainfall2DModeCombo = new QComboBox(rainfallGroup);
    m_rainfall2DModeCombo->addItem(tr("Natural neighbour (all gages)"),
                                   QStringLiteral("NATURAL_NEIGHBOUR"));
    m_rainfall2DModeCombo->addItem(tr("System (uniform gage mean)"),
                                   QStringLiteral("SYSTEM"));
    m_rainfall2DModeCombo->addItem(tr("None (no direct rainfall)"),
                                   QStringLiteral("NONE"));
    m_rainfall2DModeCombo->setToolTip(
        tr("How raingage rainfall drives the 2D mesh. Natural neighbour spatially "
           "interpolates all located gages onto each cell (inverse-distance "
           "outside the gage hull); System applies one uniform value — the mean "
           "of all gages; None applies no direct rainfall to the mesh."));
    rainfallForm->addRow(tr("Rainfall mode:"), m_rainfall2DModeCombo);

    // ── Infiltration ────────────────────────────────────────────────────
    m_infil2DModeCombo = new QComboBox(rainfallGroup);
    m_infil2DModeCombo->setObjectName(QStringLiteral("infil2DModeCombo"));
    m_infil2DModeCombo->addItem(tr("Automatic (on when per-cell rows exist)"),
                                QStringLiteral("AUTO"));
    m_infil2DModeCombo->addItem(tr("On"),  QStringLiteral("YES"));
    m_infil2DModeCombo->addItem(tr("Off (keep rows, run without infiltration)"),
                                QStringLiteral("NO"));
    m_infil2DModeCombo->setToolTip(
        tr("[2D_OPTIONS] INFILTRATION. Automatic is the pre-existing rule — "
           "cells infiltrate when [2D_INFILTRATION_DEFAULTS] / "
           "[2D_INFILTRATION] rows resolve. Off keeps the rows in the model "
           "but books no infiltration this run; On warns when no row "
           "resolves."));
    rainfallForm->addRow(tr("Infiltration:"), m_infil2DModeCombo);

    auto *istepRow = new QWidget(rainfallGroup);
    auto *istepLay = new QHBoxLayout(istepRow);
    istepLay->setContentsMargins(0, 0, 0, 0);
    m_infil2DStepSameBox = new QCheckBox(tr("Same as wet-weather step"), istepRow);
    m_infil2DStepSameBox->setObjectName(QStringLiteral("infil2DStepSameBox"));
    m_infil2DStepSameBox->setToolTip(
        tr("Infiltration rates are recomputed on their own cadence "
           "([2D_OPTIONS] INFIL_STEP, alias of [2D_INFILTRATION_OPTIONS]) "
           "and held between updates. Ticked: the project WET_STEP."));
    m_infil2DStepEdit = new QCustomTimespanEdit(istepRow);
    m_infil2DStepEdit->setObjectName(QStringLiteral("infil2DStepEdit"));
    istepLay->addWidget(m_infil2DStepSameBox);
    istepLay->addWidget(m_infil2DStepEdit, 1);
    rainfallForm->addRow(tr("Infiltration step:"), istepRow);
    connect(m_infil2DStepSameBox, &QCheckBox::toggled, this, [this](bool same) {
        m_infil2DStepEdit->setEnabled(!same);
        if (same && m_schedule.wetStepSeconds)
            m_infil2DStepEdit->setTotalSeconds(m_schedule.wetStepSeconds());
    });

    m_infil2DMethodCombo = new QComboBox(rainfallGroup);
    m_infil2DMethodCombo->setObjectName(QStringLiteral("infil2DMethodCombo"));
    m_infil2DMethodCombo->addItem(tr("None (no mesh-wide default)"), QStringLiteral("NONE"));
    m_infil2DMethodCombo->addItem(tr("Horton"),                   QStringLiteral("HORTON"));
    m_infil2DMethodCombo->addItem(tr("Modified Horton"),          QStringLiteral("MOD_HORTON"));
    m_infil2DMethodCombo->addItem(tr("Green-Ampt"),               QStringLiteral("GREEN_AMPT"));
    m_infil2DMethodCombo->addItem(tr("Modified Green-Ampt"),      QStringLiteral("MOD_GREEN_AMPT"));
    m_infil2DMethodCombo->addItem(tr("Curve Number"),             QStringLiteral("CURVE_NUMBER"));
    m_infil2DMethodCombo->addItem(tr("Constant rate"),            QStringLiteral("CONSTANT"));
    m_infil2DMethodCombo->setToolTip(
        tr("[2D_OPTIONS] INFIL_DEFAULT_METHOD — the method of the '*' row of "
           "[2D_INFILTRATION_DEFAULTS]. The row carries the parameters: pick "
           "None to drop the mesh-wide default for a run; a method must "
           "match the row (edit the row's parameters with the per-cell "
           "editor)."));
    rainfallForm->addRow(tr("Default method:"), m_infil2DMethodCombo);

    m_infil2DDestCombo = new QComboBox(rainfallGroup);
    m_infil2DDestCombo->setObjectName(QStringLiteral("infil2DDestCombo"));
    m_infil2DDestCombo->addItem(tr("Lost (leaves the model)"), QStringLiteral("LOST"));
    m_infil2DDestCombo->addItem(tr("Subcatchment aquifer (containing subcatchment)"),
                                QStringLiteral("SUBCATCH_AQUIFER"));
    m_infil2DDestCombo->addItem(tr("2D aquifer (authoring only until the groundwater kernel)"),
                                QStringLiteral("AQUIFER_2D"));
    m_infil2DDestCombo->setToolTip(
        tr("[2D_OPTIONS] INFIL_DESTINATION — where infiltrated water goes "
           "for every row that does not spell its own DEST column. "
           "Subcatchment aquifer recharges the legacy aquifer of the "
           "subcatchment containing each cell; 2D aquifer is accepted in "
           "the file but a run refuses it until the integrated groundwater "
           "kernel lands."));
    rainfallForm->addRow(tr("Destination:"), m_infil2DDestCombo);

    m_editInfilCellsBtn = new QPushButton(tr("Edit per-cell infiltration…"), rainfallGroup);
    m_editInfilCellsBtn->setObjectName(QStringLiteral("editInfilCellsBtn"));
    m_editInfilCellsBtn->setToolTip(
        tr("Opens Model ▸ Mesh ▸ Assign Infiltration to Selection for the "
           "cells selected on the map (methods, parameters and destination "
           "per cell or per region tag)."));
    connect(m_editInfilCellsBtn, &QPushButton::clicked, this, [this]() {
        QAction *act = nullptr;
        SWMMVisProjectWindow *pw = ctx_.projectWindow();
        if (QWidget *top = pw ? pw->window() : nullptr)
            act = top->findChild<QAction *>(QStringLiteral("actionMeshAssignInfilToSelection"));
        if (!act)
            for (QWidget *w : QApplication::topLevelWidgets())
                if ((act = w->findChild<QAction *>(
                         QStringLiteral("actionMeshAssignInfilToSelection"))))
                    break;
        if (act) {
            act->trigger();
        } else {
            QMessageBox::information(
                this, tr("Per-cell infiltration"),
                tr("Select cells on the map, then use Model ▸ Mesh ▸ Assign "
                   "Infiltration to Selection…"));
        }
    });
    rainfallForm->addRow(QString(), m_editInfilCellsBtn);

    // ── Evaporation ─────────────────────────────────────────────────────
    m_evap2DCombo = new QComboBox(rainfallGroup);
    m_evap2DCombo->setObjectName(QStringLiteral("evap2DCombo"));
    m_evap2DCombo->addItem(tr("Forced only (API / control forcing)"), QStringLiteral("YES"));
    m_evap2DCombo->addItem(tr("Project climate rate ([EVAPORATION]) on unforced cells"),
                           QStringLiteral("CLIMATE"));
    m_evap2DCombo->addItem(tr("Off"), QStringLiteral("NO"));
    m_evap2DCombo->setToolTip(
        tr("[2D_OPTIONS] EVAPORATION. Forced only is the pre-existing sink: "
           "cells evaporate only where swmm_2d_force_evap* prescribed a "
           "rate. Project climate rate applies the [EVAPORATION] rate the "
           "1D side uses (Climatology dialog) to every unforced cell. Off "
           "books no evaporation."));
    rainfallForm->addRow(tr("Evaporation:"), m_evap2DCombo);

    // ── Transport (the 2D column of the Models page matrix) ─────────────
    auto *trBox = new QWidget(rainfallGroup);
    auto *trLay = new QHBoxLayout(trBox);
    trLay->setContentsMargins(0, 0, 0, 0);
    const char *trLabels[4] = {"Pollutants", "MSX species", "Water age", "Temperature"};
    for (int c = 0; c < 4; ++c) {
        m_transport2DBox[c] = new QCheckBox(tr(trLabels[c]), trBox);
        m_transport2DBox[c]->setObjectName(QStringLiteral("transport2DBox_") +
                                           QLatin1String(transport2DKey(c)));
        m_transport2DBox[c]->setToolTip(
            tr("[2D_OPTIONS] %1 — carry this class on the 2D mesh when the "
               "project enables it. Unticking is a real performance lever "
               "(each row costs per-cell memory and a reaction stage); the "
               "1D side is unaffected.").arg(QLatin1String(transport2DKey(c))));
        trLay->addWidget(m_transport2DBox[c]);
        connect(m_transport2DBox[c], &QCheckBox::toggled, this,
                [this](bool) { emit transportSelectionChanged(); });
    }
    trLay->addStretch();
    rainfallForm->addRow(tr("Transport on the mesh:"), trBox);

    t2PrcLay->addWidget(rainfallGroup);

    // U5 (2026-09-07, rewired the same day once the G1 two-zone kernel
    // landed) — Groundwater group. This page holds the PROCESS ENABLES; the
    // parameters live in Mesh 2D > Groundwater (2D) > Aquifer Parameters,
    // which edits the same [2D_AQUIFER_OPTIONS] values (CLAUDE.md §5.1 — one
    // model, two views).
    m_gw2DGroup = new QGroupBox(tr("Groundwater (subsurface)"), t2Prc);
    auto *gwForm = new QFormLayout(m_gw2DGroup);

    // Tri-state, exactly like Infiltration above: Automatic is the default and
    // is never written, so opening this dialog and clicking Apply on a deck
    // that never mentioned the key stays the identity.
    m_gw2DEnableCombo = new QComboBox(m_gw2DGroup);
    m_gw2DEnableCombo->setObjectName(QStringLiteral("gw2DEnableCombo"));
    m_gw2DEnableCombo->addItem(tr("Automatic (on when aquifer rows exist)"),
                               QStringLiteral("AUTO"));
    m_gw2DEnableCombo->addItem(tr("On"), QStringLiteral("YES"));
    m_gw2DEnableCombo->addItem(tr("Off (keep rows, run without groundwater)"),
                               QStringLiteral("NO"));
    m_gw2DEnableCombo->setToolTip(
        tr("[2D_OPTIONS] GROUNDWATER. Turns on the two-zone subsurface column "
           "under every mesh cell: unsaturated + saturated storage, lateral "
           "flow and (with the sections below) subsurface transport. "
           "Automatic is the pre-existing rule — the subsurface runs when "
           "[2D_AQUIFER_OPTIONS] / [2D_AQUIFER] rows are authored. Off keeps "
           "those rows in the model but runs without groundwater; On warns "
           "when no row was authored."));
    gwForm->addRow(tr("Subsurface:"), m_gw2DEnableCombo);

    m_gw2DEtCombo = new QComboBox(m_gw2DGroup);
    m_gw2DEtCombo->setObjectName(QStringLiteral("gw2DEtCombo"));
    m_gw2DEtCombo->addItem(tr("None"), QStringLiteral("NONE"));
    m_gw2DEtCombo->addItem(tr("Capillary rise (from the water table)"),
                           QStringLiteral("CAPILLARY_RISE"));
    m_gw2DEtCombo->addItem(tr("Boundary ET (from the unsaturated column)"),
                           QStringLiteral("BOUNDARY_ET"));
    m_gw2DEtCombo->addItem(tr("Both"), QStringLiteral("BOTH"));
    m_gw2DEtCombo->setToolTip(
        tr("GW_ET: evapotranspiration from the subsurface, at the same "
           "climate PET the 1D side uses. Separate from the surface "
           "Evaporation setting above: that removes ponded water, this removes "
           "soil water, and each books its own ledger row.\n\n"
           "This is the same setting as Groundwater ET in Mesh 2D > "
           "Groundwater (2D) > Aquifer Parameters — it is stored once, in "
           "[2D_AQUIFER_OPTIONS], and editing it in either place changes the "
           "same value."));
    gwForm->addRow(tr("Evapotranspiration:"), m_gw2DEtCombo);

    m_gw2DStatusLabel = new QLabel(m_gw2DGroup);
    m_gw2DStatusLabel->setObjectName(QStringLiteral("gw2DStatusLabel"));
    m_gw2DStatusLabel->setWordWrap(true);
    gwForm->addRow(tr("Transport:"), m_gw2DStatusLabel);

    m_gw2DEditBtn = new QPushButton(tr("Subsurface initial quality..."), m_gw2DGroup);
    m_gw2DEditBtn->setObjectName(QStringLiteral("gw2DEditBtn"));
    m_gw2DEditBtn->setToolTip(
        tr("Open the Initial Quality editor: per-element initial "
           "concentrations, temperatures and ages. Subsurface rows "
           "([GW_INITIAL_QUALITY]) are authored there and saved with the "
           "model."));
    connect(m_gw2DEditBtn, &QPushButton::clicked, this, [this]() {
        OpenSWMMVis::InitialQualityDialog dlg(ctx_.engine(), this);
        if (dlg.exec() == QDialog::Accepted && dlg.wroteAnyChanges())
            emit engineEditedDirectly();
        refreshGates();
    });
    gwForm->addRow(QString(), m_gw2DEditBtn);

    connect(m_gw2DEnableCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshGates(); });
    t2PrcLay->addWidget(m_gw2DGroup);

    auto *outGroup = new QGroupBox(tr("Output"), t2Out);
    auto *outForm  = new QFormLayout(outGroup);

    m_report2DBox = new QCheckBox(tr("Write 2D results to output (REPORT_2D)"),
                                  outGroup);
    outForm->addRow(QString(), m_report2DBox);

    auto *outRow = new QWidget(outGroup);
    auto *outRowLay = new QHBoxLayout(outRow);
    outRowLay->setContentsMargins(0, 0, 0, 0);
    m_output2DFileEdit = new QLineEdit(outRow);
    m_output2DFileEdit->setPlaceholderText(tr("<model>.2d.h5 (automatic)"));
    m_output2DFileEdit->setToolTip(
        tr("HDF5 file receiving 2D results ([2D_OPTIONS] OUTPUT_FILE). "
           "Relative paths resolve against the input file's folder. Leave "
           "blank to use <model>.2d.h5 next to the input file."));
    auto *outBrowse = new QPushButton(tr("Browse…"), outRow);
    connect(outBrowse, &QPushButton::clicked, this, [this]() {
        QString start = m_output2DFileEdit->text().trimmed();
        if (start.isEmpty() && ctx_.modelLayer())
            start = QFileInfo(ctx_.modelLayer()->modelFilePath()).absolutePath();
        const QString f = QFileDialog::getSaveFileName(
            this, tr("2D results file"), start,
            tr("HDF5 results (*.h5);;All files (*)"));
        if (!f.isEmpty()) m_output2DFileEdit->setText(f);
    });
    outRowLay->addWidget(m_output2DFileEdit, 1);
    outRowLay->addWidget(outBrowse);
    outForm->addRow(tr("2D results file:"), outRow);

    // ── E1 — storage precision / compression / cadence / variable selection ──
    m_output2DPrecisionCombo = new QComboBox(outGroup);
    m_output2DPrecisionCombo->setObjectName(QStringLiteral("output2DPrecisionCombo"));
    m_output2DPrecisionCombo->addItem(tr("Single (float32, half the size)"),
                                      QStringLiteral("FLOAT32"));
    m_output2DPrecisionCombo->addItem(tr("Double (float64, bit-exact)"),
                                      QStringLiteral("FLOAT64"));
    m_output2DPrecisionCombo->setToolTip(
        tr("Storage type of every time-varying dataset in the 2D results file "
           "([2D_OPTIONS] OUTPUT_PRECISION). The solver always runs in double; "
           "float32 storage keeps ~7 significant digits, ample for depth, head "
           "and velocity rendering, and halves the file. Choose float64 for "
           "bit-exact regression comparisons."));
    outForm->addRow(tr("Precision:"), m_output2DPrecisionCombo);

    m_output2DCompressionSpin = new QSpinBox(outGroup);
    m_output2DCompressionSpin->setObjectName(QStringLiteral("output2DCompressionSpin"));
    m_output2DCompressionSpin->setRange(0, 9);
    m_output2DCompressionSpin->setSpecialValueText(tr("0 (off)"));
    m_output2DCompressionSpin->setToolTip(
        tr("HDF5 deflate level for the results datasets ([2D_OPTIONS] "
           "OUTPUT_COMPRESSION, 0–9). Levels above 0 also enable the shuffle "
           "filter, which typically halves float32 depth fields again on "
           "mostly-dry meshes. 4 is the default; 0 writes uncompressed chunks."));
    outForm->addRow(tr("Compression:"), m_output2DCompressionSpin);

    auto *stepRow = new QWidget(outGroup);
    auto *stepRowLay = new QHBoxLayout(stepRow);
    stepRowLay->setContentsMargins(0, 0, 0, 0);
    m_report2DStepSameBox = new QCheckBox(tr("Same as reporting step"), stepRow);
    m_report2DStepSameBox->setObjectName(QStringLiteral("report2DStepSameBox"));
    m_report2DStepSameBox->setToolTip(
        tr("Write a 2D frame every REPORT_STEP (the 1D reporting step). "
           "Untick to thin the 2D time axis to a longer multiple of it "
           "([2D_OPTIONS] REPORT_2D_STEP) — the 1D output keeps its own step."));
    m_report2DStepEdit = new QCustomTimespanEdit(stepRow);
    m_report2DStepEdit->setObjectName(QStringLiteral("report2DStepEdit"));
    m_report2DStepEdit->setToolTip(
        tr("2D frame interval ([2D_OPTIONS] REPORT_2D_STEP). Must be a whole "
           "multiple of REPORT_STEP; the engine refuses other values."));
    stepRowLay->addWidget(m_report2DStepSameBox);
    stepRowLay->addWidget(m_report2DStepEdit, 1);
    outForm->addRow(tr("2D report step:"), stepRow);
    connect(m_report2DStepSameBox, &QCheckBox::toggled, this, [this](bool same) {
        m_report2DStepEdit->setEnabled(!same);
        if (same && m_schedule.reportStepSeconds)
            m_report2DStepEdit->setTotalSeconds(m_schedule.reportStepSeconds());
        updateOutputSizeEstimate();
    });
    connect(m_report2DStepEdit, &QCustomTimespanEdit::totalSecondsChanged, this,
            [this](qint64) { updateOutputSizeEstimate(); });

    // Variables checklist — one row per engine dataset group, in bit order.
    // The tooltip names the GUI consumers so a user knows what greys out.
    auto *varsBox = new QWidget(outGroup);
    auto *varsLay = new QVBoxLayout(varsBox);
    varsLay->setContentsMargins(0, 0, 0, 0);
    m_report2DVarsList = new QListWidget(varsBox);
    m_report2DVarsList->setObjectName(QStringLiteral("report2DVarsList"));
    m_report2DVarsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_report2DVarsList->setToolTip(
        tr("Dataset groups written to the 2D results file ([2D_OPTIONS] "
           "REPORT_2D_VARIABLES). Unticked groups are absent from the file; "
           "plots and symbology that need them are greyed out when the run "
           "is loaded. Depth is always written."));
    struct VarDoc { const char *token; const char *label; const char *tip; };
    static const VarDoc kVarDocs[] = {
        {"DEPTH",        "Depth & head (per cell)",
         "Mesh2_face_depth / Mesh2_face_head — depth fill, contour bands and "
         "lines, cell depth / HGL plots. Always written."},
        {"VELOCITY",     "Velocity (per cell)",
         "Mesh2_face_vx / Mesh2_face_vy — cell-centred velocity components "
         "(exported for external post-processing; the GUI reconstructs velocity "
         "from edge fluxes)."},
        {"EDGE_FLUX",    "Edge fluxes (per edge)",
         "Mesh2_edge_flux — velocity vectors and velocity magnitude symbology, "
         "edge flow / flux plots, velocity plots."},
        {"NODE_HEAD",    "Vertex head & depth (per vertex)",
         "Mesh2_node_head / Mesh2_node_depth — smooth (vertex-interpolated) "
         "depth rendering and flood-extent isolines; without them the GUI "
         "falls back to a coarser cell-average reconstruction."},
        {"SPECIES",      "Water quality (per cell × species)",
         "Mesh2_face_species_conc — concentration rendering and plots for "
         "pollutants, MSX species, water age and temperature. Use the species "
         "list below to keep only some rows."},
        {"RAINFALL",     "Rainfall (per cell)",
         "Mesh2_face_rainfall / Mesh2_face_rain_cum — cell rainfall intensity "
         "and cumulative rain-volume plots."},
        {"INFILTRATION", "Infiltration (per cell)",
         "Mesh2_face_infil_rate / Mesh2_face_infil_cum — infiltration-rate "
         "and cumulative-loss plots and colour ramps."},
        {"COUPLING",     "1D↔2D exchange (per cell)",
         "Mesh2_face_coupling_flux / Mesh2_face_net_source — diagnostic "
         "exchange and net-source fields (no GUI consumer today)."},
        {"GRADIENTS",    "Reconstruction gradients (diagnostic)",
         "Mesh2_face_grad_* (4 fields) — solver diagnostics only."},
        {"CONTINUITY",   "Continuity error (diagnostic)",
         "Mesh2_face_continuity_err (+ its envelope) — solver diagnostics only."},
        {"ENVELOPES",    "Run maxima (once per run)",
         "Mesh2_face_max_depth / Mesh2_face_max_velocity — maximum-depth and "
         "maximum-velocity symbology and the flood-extent envelope; written "
         "once at the end of the run, negligible size."},
    };
    const int nVars = swmm_2d_output_variable_count();
    for (int i = 0; i < nVars; ++i) {
        const QString token = QString::fromLatin1(swmm_2d_output_variable_name(i));
        const VarDoc *doc = nullptr;
        for (const VarDoc &d : kVarDocs)
            if (token.compare(QLatin1String(d.token), Qt::CaseInsensitive) == 0) { doc = &d; break; }
        auto *item = new QListWidgetItem(
            doc ? tr("%1  [%2]").arg(tr(doc->label), token) : token, m_report2DVarsList);
        item->setData(Qt::UserRole, i);
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        item->setCheckState(Qt::Unchecked);
        if (doc) item->setToolTip(tr(doc->tip));
        if (token.compare(QLatin1String("DEPTH"), Qt::CaseInsensitive) == 0) {
            item->setCheckState(Qt::Checked);
            item->setFlags(Qt::ItemIsUserCheckable);   // always on, not editable
        }
    }
    m_report2DVarsList->setFixedHeight(
        m_report2DVarsList->sizeHintForRow(0) * std::max(1, nVars) + 6);

    auto *presetRow = new QWidget(varsBox);
    auto *presetLay = new QHBoxLayout(presetRow);
    presetLay->setContentsMargins(0, 0, 0, 0);
    auto addPreset = [&](const QString &label, const char *preset, const QString &tip) {
        auto *b = new QPushButton(label, presetRow);
        b->setObjectName(QStringLiteral("report2DPreset_") + QLatin1String(preset));
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, [this, preset]() {
            setReportVarsMask(swmm_2d_output_variable_mask(preset));
        });
        presetLay->addWidget(b);
    };
    addPreset(tr("Default"), "DEFAULT",
              tr("Everything the GUI renders or plots; drops the solver "
                 "diagnostics (exchange, gradients, continuity)."));
    addPreset(tr("Minimal"), "MINIMAL",
              tr("Depth, vertex head and run maxima only — smallest file that "
                 "still animates depth."));
    addPreset(tr("All"), "ALL", tr("Every dataset group, including diagnostics."));
    presetLay->addStretch();
    varsLay->addWidget(m_report2DVarsList);
    varsLay->addWidget(presetRow);
    outForm->addRow(tr("Variables:"), varsBox);
    connect(m_report2DVarsList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *) { updateOutputSizeEstimate(); });

    // Species sub-list — pollutants + MSX species; "All" keeps every row the
    // 2D transport solver carries (including age / temperature when enabled).
    auto *spBox = new QWidget(outGroup);
    auto *spLay = new QVBoxLayout(spBox);
    spLay->setContentsMargins(0, 0, 0, 0);
    m_report2DAllSpeciesBox = new QCheckBox(tr("All transported species"), spBox);
    m_report2DAllSpeciesBox->setObjectName(QStringLiteral("report2DAllSpeciesBox"));
    m_report2DAllSpeciesBox->setToolTip(
        tr("Write every species row the 2D transport solver carries "
           "([2D_OPTIONS] REPORT_2D_SPECIES ALL). Untick to keep only the "
           "names ticked below."));
    m_report2DSpeciesList = new QListWidget(spBox);
    m_report2DSpeciesList->setObjectName(QStringLiteral("report2DSpeciesList"));
    m_report2DSpeciesList->setSelectionMode(QAbstractItemView::NoSelection);
    m_report2DSpeciesList->setToolTip(
        tr("Species rows kept in Mesh2_face_species_conc "
           "([2D_OPTIONS] REPORT_2D_SPECIES). Names not in the model are "
           "ignored by the engine."));
    if (SWMM_Engine e = ctx_.engine()) {
        const int nPol = swmm_pollutant_count(e);
        for (int i = 0; i < nPol; ++i) {
            const char *id = swmm_pollutant_id(e, i);
            if (!id || !*id) continue;
            auto *it = new QListWidgetItem(QString::fromUtf8(id), m_report2DSpeciesList);
            it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            it->setCheckState(Qt::Unchecked);
            it->setToolTip(tr("[POLLUTANTS] %1").arg(QString::fromUtf8(id)));
        }
        const int nSp = swmm_reaction_species_count(e);
        for (int i = 0; i < nSp; ++i) {
            char name[128] = {}; char units[32] = {};
            int isWall = 0; double atol = 0.0, rtol = 0.0;
            if (swmm_reaction_species_get(e, i, name, sizeof(name), &isWall,
                                          units, sizeof(units), &atol, &rtol) != 0)
                continue;
            if (!name[0]) continue;
            const QString nm = QString::fromUtf8(name);
            if (!m_report2DSpeciesList->findItems(nm, Qt::MatchFixedString).isEmpty())
                continue;
            auto *it = new QListWidgetItem(nm, m_report2DSpeciesList);
            it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            it->setCheckState(Qt::Unchecked);
            it->setToolTip(tr("[REACTION_SPECIES] %1").arg(nm));
        }
    }
    m_report2DSpeciesList->setFixedHeight(
        m_report2DSpeciesList->sizeHintForRow(0) *
            std::clamp(m_report2DSpeciesList->count(), 1, 6) + 6);
    spLay->addWidget(m_report2DAllSpeciesBox);
    spLay->addWidget(m_report2DSpeciesList);
    outForm->addRow(tr("Species:"), spBox);
    connect(m_report2DAllSpeciesBox, &QCheckBox::toggled, this, [this](bool all) {
        m_report2DSpeciesList->setEnabled(!all);
        updateOutputSizeEstimate();
    });
    connect(m_report2DSpeciesList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *) { updateOutputSizeEstimate(); });

    m_output2DSizeLabel = new QLabel(QStringLiteral("—"), outGroup);
    m_output2DSizeLabel->setObjectName(QStringLiteral("output2DSizeLabel"));
    m_output2DSizeLabel->setWordWrap(true);
    m_output2DSizeLabel->setToolTip(
        tr("Rough uncompressed size of the time-varying datasets: frames × "
           "(selected fields × cells / vertices / edges) × bytes per value. "
           "Compression typically shrinks it 2–5×; mesh geometry is not counted."));
    outForm->addRow(tr("Estimated size:"), m_output2DSizeLabel);
    connect(m_output2DPrecisionCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { updateOutputSizeEstimate(); });
    connect(m_output2DCompressionSpin, &QSpinBox::valueChanged, this,
            [this](int) { updateOutputSizeEstimate(); });

    t2OutLay->addWidget(outGroup);

    t2HydLay->addStretch();
    t2MshLay->addStretch();
    t2CplLay->addStretch();
    t2PrcLay->addStretch();
    t2OutLay->addStretch();

    m_tabs->addTab(t2Hyd, tr("Hydrodynamics"));
    m_tabs->addTab(t2Msh, tr("Wetting & Drying"));
    m_tabs->addTab(t2Cpl, tr("Coupling"));
    m_tabs->addTab(t2Prc, tr("Processes"));
    m_tabs->addTab(t2Out, tr("Performance & Output"));
}

void TwoDPage::tagWidgets()
{
    for (int c = 0; c < 4; ++c)
        tagOption(m_transport2DBox[c], transport2DKey(c));
    tagOption(m_maxTimestepSpin, "MAX_TIMESTEP");
    tagOption(m_dryDepthSpin, "DRY_DEPTH");
    tagOption(m_limiterEpsSpin, "LIMITER_EPSILON");
    tagOption(m_fluxDhEpsSpin, "FLUX_DH_EPS");
    tagOption(m_cellClosureCombo, "CELL_CLOSURE");
    tagOption(m_faceReconCombo, "FACE_RECONSTRUCTION");
    tagOption(m_vfrMinWetFracSpin, "VFR_MIN_WET_FRAC");
    tagOption(m_couplingCdSpin, "COUPLING_CD");
    tagOption(m_couplingSyncSpin, "COUPLING_SYNC");
    tagOption(m_rainfall2DModeCombo, "RAINFALL_MODE");
    tagOption(m_report2DBox, "REPORT_2D");
    tagOption(m_thetaSpin, "THETA");
    tagOption(m_cflNumberSpin, "CFL_NUMBER");
    tagOption(m_ltsTiersSpin, "LTS_TIERS");
    tagOption(m_hMoveSpin, "H_MOVE");
    tagOption(m_froudeMaxSpin, "FROUDE_MAX");
    tagOption(m_momentum2DCombo, "MOMENTUM_EQUATION");
    tagOption(m_reconOrder2DSpin, "RECONSTRUCTION_ORDER");
    tagOption(m_advection2DBox, "ADVECTION");
    tagOption(m_backend2DCombo, "BACKEND");
    tagOption(m_couplingAreaAutoBox, "COUPLING_AREA");
    tagOption(m_output2DFileEdit, "OUTPUT_FILE");
    tagOption(m_output2DPrecisionCombo, "OUTPUT_PRECISION");
    tagOption(m_output2DCompressionSpin, "OUTPUT_COMPRESSION");
    tagOption(m_report2DStepEdit, "REPORT_2D_STEP");
    tagOption(m_report2DVarsList, "REPORT_2D_VARIABLES");
    tagOption(m_report2DSpeciesList, "REPORT_2D_SPECIES");
    tagOption(m_infil2DStepEdit, "INFIL_STEP");
    tagOption(m_infil2DModeCombo, "INFILTRATION");
    tagOption(m_infil2DMethodCombo, "INFIL_DEFAULT_METHOD");
    tagOption(m_infil2DDestCombo, "INFIL_DESTINATION");
    tagOption(m_evap2DCombo, "EVAPORATION");
    tagOption(m_gw2DEnableCombo, "GROUNDWATER");
    tagOption(m_gw2DEtCombo, "GW_ET");
}

void TwoDPage::refreshGates()
{
    if (!m_gw2DGroup) return;
    // Tri-state: only an explicit "On" is a definite yes. Under Automatic the
    // answer depends on whether aquifer rows exist, which this dialog does not
    // count — so it leaves the destination free and lets the engine validate,
    // rather than guessing and locking a control the user cannot then change.
    const QString mode = m_gw2DEnableCombo->currentData().toString();
    const bool on  = (mode == QLatin1String("YES"));
    const bool off = (mode == QLatin1String("NO"));
    m_gw2DEtCombo->setEnabled(!off);
    // The surface Infiltration destination and the subsurface are the two
    // ends of the same water: with the 2D aquifer on, infiltration belongs
    // to it, and the mutual exclusion the engine validates shows here as a
    // state change rather than a hidden group (OPT plan section 7).
    if (m_infil2DDestCombo) {
        const int aq = m_infil2DDestCombo->findData(QStringLiteral("AQUIFER_2D"));
        if (aq >= 0 && on && m_infil2DDestCombo->currentIndex() != aq)
            m_infil2DDestCombo->setCurrentIndex(aq);
        m_infil2DDestCombo->setEnabled(!on);
        m_infil2DDestCombo->setToolTip(
            on ? tr("Locked to the 2D aquifer while Groundwater is enabled: "
                    "infiltrated water enters the subsurface column under the "
                    "cell. Turn Groundwater off to route it elsewhere.")
               : tr("[2D_OPTIONS] INFIL_DESTINATION: where infiltrated water "
                    "goes for every row that does not spell its own DEST "
                    "column. Subcatchment aquifer recharges the legacy aquifer "
                    "of the subcatchment containing each cell; 2D aquifer is "
                    "accepted in the file but a run refuses it until the "
                    "integrated groundwater kernel lands."));
        if (m_infil2DMethodCombo)
            m_infil2DMethodCombo->setEnabled(!on);
    }

    // Name the [GW_*] authoring surface and what a run will do with it.
    int rows = 0;
    if (SWMM_Engine e = ctx_.engine()) {
        rows = std::max(0, swmm_gw_params_count(e)) +
               std::max(0, swmm_gw_sorption_count(e)) +
               std::max(0, swmm_gw_init_quality_count(e)) +
               std::max(0, swmm_gw_boundary_quality_count(e)) +
               std::max(0, swmm_gw_source_count(e));
    }
    m_gw2DStatusLabel->setText(
        rows > 0
            ? tr("%1 subsurface transport row(s) authored "
                 "([GW_TRANSPORT_PARAMS], [GW_SORPTION], [GW_INITIAL_QUALITY], "
                 "[GW_BOUNDARY_QUALITY], [GW_SOURCES]). They are saved with "
                 "the model; a run reports them as authored-but-inert until "
                 "the integrated groundwater component ships.").arg(rows)
            : tr("No subsurface transport rows authored yet. They are saved "
                 "with the model and run when the integrated groundwater "
                 "component ships."));
}

unsigned TwoDPage::reportVarsMask() const
{
    unsigned mask = 0u;
    for (int r = 0; r < m_report2DVarsList->count(); ++r) {
        const QListWidgetItem *it = m_report2DVarsList->item(r);
        if (it->checkState() == Qt::Checked)
            mask |= 1u << it->data(Qt::UserRole).toInt();
    }
    return mask;
}

void TwoDPage::setReportVarsMask(unsigned mask)
{
    QSignalBlocker b(m_report2DVarsList);
    for (int r = 0; r < m_report2DVarsList->count(); ++r) {
        QListWidgetItem *it = m_report2DVarsList->item(r);
        const int bit = it->data(Qt::UserRole).toInt();
        const bool isDepth = bit == 0;   // DEPTH is bit 0 and always on
        it->setCheckState((isDepth || (mask & (1u << bit))) ? Qt::Checked : Qt::Unchecked);
    }
    updateOutputSizeEstimate();
}

QString TwoDPage::reportSpeciesText() const
{
    if (m_report2DAllSpeciesBox->isChecked()) return QStringLiteral("ALL");
    QStringList names;
    for (int r = 0; r < m_report2DSpeciesList->count(); ++r) {
        const QListWidgetItem *it = m_report2DSpeciesList->item(r);
        if (it->checkState() == Qt::Checked) names << it->text();
    }
    // Nothing ticked means "all" to the engine (empty list = unfiltered);
    // spell it out so the .inp reads as intended.
    return names.isEmpty() ? QStringLiteral("ALL") : names.join(QLatin1Char(' '));
}

void TwoDPage::setReportSpeciesText(const QString &text)
{
    QSignalBlocker b1(m_report2DAllSpeciesBox);
    QSignalBlocker b2(m_report2DSpeciesList);
    const QStringList names = text.split(QRegularExpression(QStringLiteral("[\\s,]+")),
                                         Qt::SkipEmptyParts);
    const bool all = names.isEmpty() ||
        (names.size() == 1 && names.first().compare(QLatin1String("ALL"), Qt::CaseInsensitive) == 0);
    for (int r = 0; r < m_report2DSpeciesList->count(); ++r)
        m_report2DSpeciesList->item(r)->setCheckState(Qt::Unchecked);
    if (!all) {
        for (const QString &nm : names) {
            auto hits = m_report2DSpeciesList->findItems(nm, Qt::MatchFixedString);
            if (hits.isEmpty()) {
                // Name from the file that the model no longer declares — keep it
                // so the round trip does not silently drop it.
                auto *it = new QListWidgetItem(nm, m_report2DSpeciesList);
                it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
                it->setToolTip(tr("Not declared in this model"));
                hits << it;
            }
            hits.first()->setCheckState(Qt::Checked);
        }
    }
    m_report2DAllSpeciesBox->setChecked(all);
    m_report2DSpeciesList->setEnabled(!all);
}

void TwoDPage::updateOutputSizeEstimate()
{
    if (!m_output2DSizeLabel) return;
    SWMM_Engine e = ctx_.engine();
    if (!e) { m_output2DSizeLabel->setText(QStringLiteral("—")); return; }

    int nCells = 0, nVerts = 0, nQuads = 0;
    swmm_2d_cell_count(e, &nCells);
    swmm_2d_vertex_count(e, &nVerts);
    swmm_2d_quad_count(e, &nQuads);
    if (nCells <= 0) {
        m_output2DSizeLabel->setText(tr("No mesh loaded — nothing to estimate."));
        return;
    }
    const int edgeStride = nQuads > 0 ? 4 : 3;

    const qint64 durationSec = m_schedule.durationSeconds
                                   ? m_schedule.durationSeconds() : 0;
    const qint64 stepSec = m_report2DStepSameBox->isChecked()
        ? (m_schedule.reportStepSeconds ? m_schedule.reportStepSeconds() : 0)
        : m_report2DStepEdit->totalSeconds();
    if (stepSec <= 0 || durationSec <= 0) {
        m_output2DSizeLabel->setText(tr("Set a positive duration and report step."));
        return;
    }
    const double frames = static_cast<double>(durationSec) / stepSec + 1.0;

    int nSpecies = 0;
    if (m_report2DAllSpeciesBox->isChecked()) {
        nSpecies = swmm_pollutant_count(e);
        for (int r = 0; r < m_report2DSpeciesList->count(); ++r)
            if (!m_report2DSpeciesList->item(r)->toolTip().startsWith(QLatin1String("[POLLUTANTS]")))
                ++nSpecies;   // MSX species (the list de-duplicates by name)
    } else {
        for (int r = 0; r < m_report2DSpeciesList->count(); ++r)
            if (m_report2DSpeciesList->item(r)->checkState() == Qt::Checked) ++nSpecies;
    }

    // Values per frame by group; bit order matches swmm_2d_output_variable_name.
    const unsigned mask = reportVarsMask();
    auto on = [&](const char *token) {
        return (mask & swmm_2d_output_variable_mask(token)) != 0u;
    };
    double perFrame = 1.0;                                        // time
    if (on("DEPTH"))        perFrame += 2.0 * nCells;
    if (on("VELOCITY"))     perFrame += 2.0 * nCells;
    if (on("EDGE_FLUX"))    perFrame += static_cast<double>(nCells) * edgeStride;
    if (on("NODE_HEAD"))    perFrame += 2.0 * nVerts;
    if (on("SPECIES"))      perFrame += static_cast<double>(nSpecies) * nCells;
    if (on("RAINFALL"))     perFrame += 2.0 * nCells;
    if (on("INFILTRATION")) perFrame += 2.0 * nCells;
    if (on("COUPLING"))     perFrame += 2.0 * nCells;
    if (on("GRADIENTS"))    perFrame += 4.0 * nCells;
    if (on("CONTINUITY"))   perFrame += 1.0 * nCells;
    const double bytesPer = m_output2DPrecisionCombo->currentData().toString()
                                .compare(QLatin1String("FLOAT64"), Qt::CaseInsensitive) == 0
                            ? 8.0 : 4.0;
    const double bytes = frames * perFrame * bytesPer;

    auto human = [](double b) {
        const char *units[] = {"B", "KB", "MB", "GB", "TB"};
        int u = 0;
        while (b >= 1024.0 && u < 4) { b /= 1024.0; ++u; }
        return QStringLiteral("%1 %2").arg(b, 0, 'f', u == 0 ? 0 : 1)
                                      .arg(QLatin1String(units[u]));
    };
    m_output2DSizeLabel->setText(
        tr("≈ %1 uncompressed (%2 frames × %3 values × %4 B)%5")
            .arg(human(bytes))
            .arg(static_cast<qint64>(frames))
            .arg(QLocale().toString(static_cast<qint64>(perFrame)))
            .arg(static_cast<int>(bytesPer))
            .arg(m_output2DCompressionSpin->value() > 0
                     ? tr("; deflate level %1 typically 2–5× smaller")
                           .arg(m_output2DCompressionSpin->value())
                     : QString()));
}

void TwoDPage::read()
{
    SWMM_Engine e = ctx_.engine();
    auto getExt = [&](const char *key, const QString &fallback) -> QString {
        if (!e) return fallback;
        char buf[256] = {};
        if (swmm_options_get_ext(e, key, buf, sizeof(buf)) == 0)
            return QString::fromUtf8(buf).trimmed();
        return fallback;
    };
    // Same fallback-on-garbage semantics as the [OPTIONS] pages: never seed 0
    // into a spin box from an unparseable engine string (write() would then
    // persist it as a real edit).
    auto extDouble = [&](const char *key, double fallback) {
        bool okNum = false;
        const double v = getExt(key, QString::number(fallback, 'g', 8))
                             .toDouble(&okNum);
        return okNum ? v : fallback;
    };
    auto extInt = [&](const char *key, int fallback) {
        bool okNum = false;
        const int v = getExt(key, QString::number(fallback)).toInt(&okNum);
        return okNum ? v : fallback;
    };
    using SOD = SimulationOptionsDialog;

    // Iteration 4 — source every missing-key fallback from the 2D preference
    // tabs (Preferences > Simulation Defaults > 2D ...) (same lockstep idiom as the 1D pages): the dialog shows the
    // user-preferred default whenever the project has no value for a key,
    // matching what File→New would synthesize.
    const auto t = PreferencesManager::instance()->twoDDefaults();

    m_maxTimestepSpin  ->setValue(extDouble("MAX_TIMESTEP",     t.maxTimestepSec));
    m_dryDepthSpin     ->setValue(extDouble("DRY_DEPTH",        t.dryDepth));
    m_limiterEpsSpin   ->setValue(extDouble("LIMITER_EPSILON",  t.limiterEpsilon));
    m_fluxDhEpsSpin    ->setValue(extDouble("FLUX_DH_EPS",      t.fluxDhEps));
    m_vfrMinWetFracSpin->setValue(extDouble("VFR_MIN_WET_FRAC", t.vfrMinWetFrac));
    m_couplingCdSpin   ->setValue(extDouble("COUPLING_CD",      t.couplingCd));
    m_couplingSyncSpin ->setValue(extDouble("COUPLING_SYNC",    t.couplingSync));

    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0) c->setCurrentIndex(idx);
    };
    selectComboByData(m_cellClosureCombo,    getExt("CELL_CLOSURE",        t.cellClosure));
    selectComboByData(m_faceReconCombo,      getExt("FACE_RECONSTRUCTION", t.faceReconstruction));
    selectComboByData(m_rainfall2DModeCombo, getExt("RAINFALL_MODE",       t.rainfallMode));
    m_report2DBox->setChecked(SOD::parseEngineBool(
        getExt("REPORT_2D", t.report2D ? "YES" : "NO")) == Qt::Checked);

    // Explicit-marcher configuration (the only 2D integrator; no INTEGRATOR
    // read/write — the engine default is EXPLICIT).
    m_thetaSpin    ->setValue(extDouble("THETA",      t.theta));
    m_cflNumberSpin->setValue(extDouble("CFL_NUMBER", t.cflNumber));
    m_ltsTiersSpin ->setValue(extInt("LTS_TIERS",     t.ltsTiers));
    m_hMoveSpin    ->setValue(extDouble("H_MOVE",     t.hMove));
    m_froudeMaxSpin->setValue(extDouble("FROUDE_MAX", t.froudeMax));
    selectComboByData(m_momentum2DCombo,
                      getExt("MOMENTUM_EQUATION", QStringLiteral("LOCAL_INERTIAL")));
    m_reconOrder2DSpin->setValue(extInt("RECONSTRUCTION_ORDER", 1));
    m_advection2DBox->setChecked(SOD::parseEngineBool(
        getExt("ADVECTION", t.advection ? "YES" : "NO")) == Qt::Checked);
    // No preferences default: the engine's AUTO is the only sensible seed.
    selectComboByData(m_backend2DCombo, getExt("BACKEND", QStringLiteral("AUTO")));
    m_couplingAreaAutoBox->setChecked(
        getExt("COUPLING_AREA", t.couplingAreaAuto ? "AUTO" : "DEFAULT")
            .compare(QStringLiteral("AUTO"), Qt::CaseInsensitive) == 0);

    // OUTPUT_FILE has no preferences default — blank means "auto-derive
    // <model>.2d.h5 at run time" (see SimulationRunner / run wiring).
    m_output2DFileEdit->setText(getExt("OUTPUT_FILE", QString()));

    // E1 — output precision / compression / cadence / variables / species.
    // A species list can exceed the 256-byte getExt buffer; read it wide.
    auto getExtWide = [&](const char *key) -> QString {
        if (!e) return {};
        std::vector<char> buf(4096, '\0');
        if (swmm_options_get_ext(e, key, buf.data(), static_cast<int>(buf.size())) == 0)
            return QString::fromUtf8(buf.data()).trimmed();
        return {};
    };
    selectComboByData(m_output2DPrecisionCombo,
                      getExt("OUTPUT_PRECISION", QStringLiteral("FLOAT32")));
    m_output2DCompressionSpin->setValue(std::clamp(extInt("OUTPUT_COMPRESSION", 4), 0, 9));
    {
        const qint64 step2d =
            SOD::parseStepSeconds(getExt("REPORT_2D_STEP", QStringLiteral("0")), 0);
        QSignalBlocker b1(m_report2DStepSameBox);
        QSignalBlocker b2(m_report2DStepEdit);
        m_report2DStepSameBox->setChecked(step2d <= 0);
        m_report2DStepEdit->setEnabled(step2d > 0);
        m_report2DStepEdit->setTotalSeconds(
            step2d > 0 ? step2d
                       : (m_schedule.reportStepSeconds ? m_schedule.reportStepSeconds() : 0));
    }
    {
        const QString varsText = getExtWide("REPORT_2D_VARIABLES");
        unsigned mask = varsText.isEmpty() ? 0u
                      : swmm_2d_output_variable_mask(varsText.toUtf8().constData());
        if (mask == 0u) mask = swmm_2d_output_variable_mask("DEFAULT");
        setReportVarsMask(mask);
    }
    setReportSpeciesText(getExtWide("REPORT_2D_SPECIES"));
    updateOutputSizeEstimate();

    // U1 — Processes group (E2 keys). INFILTRATION is AUTO | YES | NO as
    // stored; the Automatic item's text carries the effective state derived
    // from the rows the engine holds.
    selectComboByData(m_infil2DModeCombo, getExt("INFILTRATION", QStringLiteral("AUTO")));
    {
        int nDefaults = 0;
        swmm_infil2d_defaults_count(e, &nDefaults);
        // No per-cell override count in the C API: scan a bounded prefix
        // when there is no default row (a large mesh with only overrides
        // keeps the plain label).
        int nCells = 0;
        swmm_2d_cell_count(e, &nCells);
        const int scanLimit = 50000;
        bool anyCellRow = false;
        for (int i = 0; nDefaults == 0 && i < nCells && i < scanLimit && !anyCellRow; ++i) {
            SWMM_Infil2DRow row{};
            int isOverride = 0;
            if (swmm_infil2d_get_cell(e, i, &row, &isOverride) == SWMM_OK &&
                isOverride && row.has_method)
                anyCellRow = true;
        }
        const bool rows = nDefaults > 0 || anyCellRow;
        m_infil2DModeCombo->setItemText(
            m_infil2DModeCombo->findData(QStringLiteral("AUTO")),
            (nDefaults == 0 && nCells > scanLimit && !anyCellRow)
                ? tr("Automatic (on when per-cell rows exist)")
                : (rows ? tr("Automatic — on (per-cell rows exist)")
                        : tr("Automatic — off (no per-cell rows)")));
    }
    {
        const qint64 step =
            SOD::parseStepSeconds(getExt("INFIL_STEP", QStringLiteral("0")), 0);
        QSignalBlocker b1(m_infil2DStepSameBox);
        QSignalBlocker b2(m_infil2DStepEdit);
        m_infil2DStepSameBox->setChecked(step <= 0);
        m_infil2DStepEdit->setEnabled(step > 0);
        m_infil2DStepEdit->setTotalSeconds(
            step > 0 ? step
                     : (m_schedule.wetStepSeconds ? m_schedule.wetStepSeconds() : 0));
    }
    selectComboByData(m_infil2DMethodCombo, getExt("INFIL_DEFAULT_METHOD", QStringLiteral("NONE")));
    selectComboByData(m_infil2DDestCombo,   getExt("INFIL_DESTINATION",    QStringLiteral("LOST")));
    selectComboByData(m_evap2DCombo,        getExt("EVAPORATION",          QStringLiteral("YES")));
    for (int c = 0; c < 4; ++c) {
        QSignalBlocker b(m_transport2DBox[c]);
        m_transport2DBox[c]->setChecked(
            SOD::parseEngineBool(getExt(transport2DKey(c), QStringLiteral("YES")))
                != Qt::Unchecked);
    }
    emit transportSelectionChanged();

    // U5 — groundwater process enables. GROUNDWATER reports AS STORED, so
    // AUTO seeds AUTO and an unedited Apply writes nothing.
    selectComboByData(m_gw2DEnableCombo,
                      getExt("GROUNDWATER", QStringLiteral("AUTO")));
    selectComboByData(m_gw2DEtCombo, getExt("GW_ET", QStringLiteral("NONE")));
    refreshGates();
}

int TwoDPage::write()
{
    SWMM_Engine e = ctx_.engine();
    int n = 0;
    auto getExt = [&](const char *key) -> QString {
        if (!e) return {};
        char buf[256] = {};
        if (swmm_options_get_ext(e, key, buf, sizeof(buf)) == 0)
            return QString::fromUtf8(buf).trimmed();
        return {};
    };
    auto setExt = [&](const char *key, const QString &v) -> bool {
        if (!e) return false;
        return swmm_options_set_ext(e, key, v.toUtf8().constData()) == 0;
    };
    auto writeIfChanged = [&](const char *key, const QString &nv) {
        // [2D_OPTIONS] keys go through swmm_options_set_ext, not the plain
        // setter the context wraps — but they are still option editors and
        // must appear in the reachability comparison, so the key is recorded
        // through the context either way.
        ctx_.recordWrittenKey(key);
        // Numeric-aware compare, same rationale as the [OPTIONS] pages.
        if (SimulationOptionsDialog::optionValueEquals(getExt(key), nv)) return;
        if (setExt(key, nv)) ++n;
    };
    using SOD = SimulationOptionsDialog;

    writeIfChanged("MAX_TIMESTEP",
                   QString::number(m_maxTimestepSpin->value(), 'g', 8));
    writeIfChanged("DRY_DEPTH",
                   QString::number(m_dryDepthSpin->value(), 'g', 8));
    writeIfChanged("LIMITER_EPSILON",
                   QString::number(m_limiterEpsSpin->value(), 'g', 8));
    writeIfChanged("FLUX_DH_EPS",
                   QString::number(m_fluxDhEpsSpin->value(), 'g', 8));
    writeIfChanged("CELL_CLOSURE",        m_cellClosureCombo->currentData().toString());
    writeIfChanged("FACE_RECONSTRUCTION", m_faceReconCombo->currentData().toString());
    writeIfChanged("VFR_MIN_WET_FRAC",
                   QString::number(m_vfrMinWetFracSpin->value(), 'g', 6));
    writeIfChanged("COUPLING_CD",
                   QString::number(m_couplingCdSpin->value(), 'f', 4));
    writeIfChanged("COUPLING_SYNC",
                   QString::number(m_couplingSyncSpin->value(), 'g', 6));
    writeIfChanged("RAINFALL_MODE",     m_rainfall2DModeCombo->currentData().toString());
    writeIfChanged("REPORT_2D",         SOD::engineBoolString(m_report2DBox->isChecked()));

    // Explicit-marcher configuration (the only 2D integrator).
    writeIfChanged("THETA",         QString::number(m_thetaSpin->value(), 'g', 6));
    writeIfChanged("CFL_NUMBER",    QString::number(m_cflNumberSpin->value(), 'g', 6));
    writeIfChanged("LTS_TIERS",     QString::number(m_ltsTiersSpin->value()));
    writeIfChanged("H_MOVE",        QString::number(m_hMoveSpin->value(), 'g', 6));
    writeIfChanged("FROUDE_MAX",    QString::number(m_froudeMaxSpin->value(), 'g', 6));
    writeIfChanged("MOMENTUM_EQUATION", m_momentum2DCombo->currentData().toString());
    writeIfChanged("RECONSTRUCTION_ORDER",
                   QString::number(m_reconOrder2DSpin->value()));
    writeIfChanged("ADVECTION",     SOD::engineBoolString(m_advection2DBox->isChecked()));
    writeIfChanged("BACKEND",       m_backend2DCombo->currentData().toString());
    writeIfChanged("COUPLING_AREA", m_couplingAreaAutoBox->isChecked()
                                        ? QStringLiteral("AUTO")
                                        : QStringLiteral("DEFAULT"));
    // Empty clears the key — InpWriter omits OUTPUT_FILE when unset and the
    // run wiring falls back to <model>.2d.h5.
    writeIfChanged("OUTPUT_FILE",   m_output2DFileEdit->text().trimmed());

    // E1 — precision / compression / cadence / variables / species.
    auto getExtWide = [&](const char *key) -> QString {
        if (!e) return {};
        std::vector<char> buf(4096, '\0');
        if (swmm_options_get_ext(e, key, buf.data(), static_cast<int>(buf.size())) == 0)
            return QString::fromUtf8(buf.data()).trimmed();
        return {};
    };
    writeIfChanged("OUTPUT_PRECISION",
                   m_output2DPrecisionCombo->currentData().toString());
    writeIfChanged("OUTPUT_COMPRESSION",
                   QString::number(m_output2DCompressionSpin->value()));
    {
        // Engine formats the key as HH:MM:SS; compare in seconds so an
        // untouched value is not re-written.
        ctx_.recordWrittenKey("REPORT_2D_STEP");
        const qint64 cur = SOD::parseStepSeconds(getExt("REPORT_2D_STEP"), 0);
        const qint64 nv  = m_report2DStepSameBox->isChecked()
                               ? 0 : m_report2DStepEdit->totalSeconds();
        if (cur != nv) {
            const qint64 h = nv / 3600, m = (nv % 3600) / 60, s = nv % 60;
            if (setExt("REPORT_2D_STEP",
                       QStringLiteral("%1:%2:%3")
                           .arg(h, 2, 10, QLatin1Char('0'))
                           .arg(m, 2, 10, QLatin1Char('0'))
                           .arg(s, 2, 10, QLatin1Char('0'))))
                ++n;
        }
    }
    {
        // Compare as masks: "DEFAULT" and its expanded token list are equal.
        ctx_.recordWrittenKey("REPORT_2D_VARIABLES");
        const QString curText = getExtWide("REPORT_2D_VARIABLES");
        const unsigned cur = curText.isEmpty()
            ? swmm_2d_output_variable_mask("DEFAULT")
            : swmm_2d_output_variable_mask(curText.toUtf8().constData());
        const unsigned nv = reportVarsMask();
        if (cur != nv && setExt("REPORT_2D_VARIABLES",
                                QString::fromLatin1(swmm_2d_output_variable_text(nv))))
            ++n;
    }
    {
        ctx_.recordWrittenKey("REPORT_2D_SPECIES");
        const QString cur = getExtWide("REPORT_2D_SPECIES");
        const QString nv  = reportSpeciesText();
        const bool curAll = cur.isEmpty() || cur.compare(QLatin1String("ALL"), Qt::CaseInsensitive) == 0;
        const bool nvAll  = nv.compare(QLatin1String("ALL"), Qt::CaseInsensitive) == 0;
        if (!(curAll && nvAll) && cur.compare(nv, Qt::CaseInsensitive) != 0 &&
            setExt("REPORT_2D_SPECIES", nv))
            ++n;
    }

    // U1 — Processes group (E2 keys).
    writeIfChanged("INFILTRATION", m_infil2DModeCombo->currentData().toString());
    {
        // INFIL_STEP is the alias of [2D_INFILTRATION_OPTIONS] INFIL_STEP;
        // compare in seconds (engine spells HH:MM:SS).
        ctx_.recordWrittenKey("INFIL_STEP");
        const qint64 cur = SOD::parseStepSeconds(getExt("INFIL_STEP"), 0);
        const qint64 nv  = m_infil2DStepSameBox->isChecked()
                               ? 0 : m_infil2DStepEdit->totalSeconds();
        if (cur != nv) {
            const qint64 h = nv / 3600, m = (nv % 3600) / 60, sec = nv % 60;
            if (setExt("INFIL_STEP", QStringLiteral("%1:%2:%3")
                                         .arg(h, 2, 10, QLatin1Char('0'))
                                         .arg(m, 2, 10, QLatin1Char('0'))
                                         .arg(sec, 2, 10, QLatin1Char('0'))))
                ++n;
        }
    }
    writeIfChanged("INFIL_DEFAULT_METHOD", m_infil2DMethodCombo->currentData().toString());
    writeIfChanged("INFIL_DESTINATION",    m_infil2DDestCombo->currentData().toString());
    writeIfChanged("EVAPORATION",          m_evap2DCombo->currentData().toString());
    for (int c = 0; c < 4; ++c)
        writeIfChanged(transport2DKey(c),
                       SOD::engineBoolString(m_transport2DBox[c]->isChecked()));

    // U5 — groundwater process enables. GW_ET writes through the alias into
    // [2D_AQUIFER_OPTIONS], so this and the Mesh 2D aquifer editor set the
    // same value.
    writeIfChanged("GROUNDWATER", m_gw2DEnableCombo->currentData().toString());
    writeIfChanged("GW_ET",       m_gw2DEtCombo->currentData().toString());
    return n;
}

} // namespace openswmmvis::ui

#endif // OPENSWMM_HAS_2D
