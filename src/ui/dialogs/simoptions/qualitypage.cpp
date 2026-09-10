/*!
 * \file   qualitypage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/qualitypage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "ui/dialogs/initialqualitydialog.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/wateragesourcesdialog.h"

namespace openswmmvis::ui
{

QualityPage::QualityPage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
    tagWidgets();
}

QString QualityPage::title() const
{
    return tr("Quality & Transport");
}

QString QualityPage::qualitySolver() const
{
    return m_qualitySolverCombo ? m_qualitySolverCombo->currentData().toString()
                                : QString();
}

bool QualityPage::tracksReservedSpecies() const
{
    return (m_waterAgeBox && m_waterAgeBox->isChecked())
        || (m_heatTransportBox && m_heatTransportBox->isChecked());
}

void QualityPage::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // ── Page header: QUALITY_SOLVER, always visible above the tab bar ────
    // It gates the Eulerian ARD and Lagrangian tabs, so it cannot live inside
    // one of them (PLAN §2.1).
    auto *headerForm = new QFormLayout;
    m_qualitySolverCombo = new QComboBox(this);
    m_qualitySolverCombo->setObjectName(QStringLiteral("qualitySolverCombo"));
    m_qualitySolverCombo->addItem(tr("Legacy (complete mix)"),
                                  QStringLiteral("LEGACY"));
    m_qualitySolverCombo->addItem(tr("Eulerian ARD (advection–reaction–dispersion)"),
                                  QStringLiteral("EULERIAN_ARD"));
    m_qualitySolverCombo->addItem(tr("Lagrangian (LARD)"),
                                  QStringLiteral("LAGRANGIAN"));
    m_qualitySolverCombo->setToolTip(
        tr("Transport engine for pollutants and reserved species "
           "(option QUALITY_SOLVER). The tabs below grey themselves out when "
           "they do not apply to the selected solver."));
    connect(m_qualitySolverCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emit gateInputsChanged(); });
    headerForm->addRow(tr("Sol&ver:"), m_qualitySolverCombo);
    root->addLayout(headerForm);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("qualityTabs"));
    root->addWidget(m_tabs, 1);

    auto *solTab  = new QWidget(m_tabs); auto *solTabLay  = new QVBoxLayout(solTab);
    auto *ardTab  = new QWidget(m_tabs); auto *ardTabLay  = new QVBoxLayout(ardTab);
    auto *lardTab = new QWidget(m_tabs); auto *lardTabLay = new QVBoxLayout(lardTab);
    auto *resTab  = new QWidget(m_tabs); auto *resTabLay  = new QVBoxLayout(resTab);

    // ── Solver ─────────────────────────────────────────────────────────
    auto *solGroup = new QGroupBox(tr("Water quality solver"), solTab);
    auto *solForm  = new QFormLayout(solGroup);

    // Boundary re-entry quality at outfalls — solver-independent (both
    // engines honor it), so it lives with the solver selection rather
    // than in a per-solver group.
    m_outfallBackflowCombo = new QComboBox(solGroup);
    m_outfallBackflowCombo->addItem(tr("Hold last concentration (legacy)"),
                                    QStringLiteral("LAST"));
    m_outfallBackflowCombo->addItem(tr("Fresh (zero concentration and age)"),
                                    QStringLiteral("ZERO"));
    m_outfallBackflowCombo->setToolTip(
        tr("Quality carried by reverse flow at outfalls (option "
           "OUTFALL_BACKFLOW_QUALITY). \"Hold last\" re-injects the "
           "outfall's held state — under water age that water keeps aging "
           "with the clock, so a permanently supplying outfall grows old "
           "without bound. \"Fresh\" makes a supplying outfall an "
           "EPANET-style reservoir: backflow enters with zero "
           "concentration and zero age."));
    solForm->addRow(tr("Outfall &backflow:"), m_outfallBackflowCombo);
    solTabLay->addWidget(solGroup);

    // ── Eulerian ARD ───────────────────────────────────────────────────
    auto *ardGroup = new QGroupBox(tr("Eulerian ARD"), ardTab);
    auto *ardLay = new QVBoxLayout(ardGroup);
    auto *ardNote = new QLabel(
        tr("Dispersion and transport mesh spacing for the Eulerian ARD "
           "engine are configured in its component file (<i>model.ard</i>: "
           "[TRANSPORT_OPTIONS], [CONDUIT_DISPERSION]), bound on the Files / "
           "Output / Plugins page; a component file also overrides the "
           "scalar scheme below. The Lagrangian solver's dispersion is the "
           "DISPERSION option in its own group."),
        ardGroup);
    ardNote->setWordWrap(true);
    ardLay->addWidget(ardNote);

    // FV_SCALAR_SCHEME lives here, not on the Routing & Hydraulics page: its
    // only live consumer is the ARD engine, which reads it under any routing
    // model (FV routing itself transports no species).
    auto *ardForm = new QFormLayout();
    m_fvScalarSchemeCombo = new QComboBox(ardGroup);
    m_fvScalarSchemeCombo->addItem(tr("MUSCL"),              QStringLiteral("MUSCL"));
    m_fvScalarSchemeCombo->addItem(tr("Upwind"),             QStringLiteral("UPWIND"));
    m_fvScalarSchemeCombo->addItem(tr("QUICKEST-ULTIMATE"),  QStringLiteral("QUICKEST_ULTIMATE"));
    m_fvScalarSchemeCombo->setToolTip(
        tr("Advection scheme for water-quality scalars on the ARD transport "
           "mesh (FV_SCALAR_SCHEME). Read under any routing model; a bound "
           "component file's [TRANSPORT_OPTIONS] SCALAR_SCHEME overrides it."));
    ardForm->addRow(tr("Scalar scheme:"), m_fvScalarSchemeCombo);
    ardLay->addLayout(ardForm);
    ardTabLay->addWidget(ardGroup);

    // ── Lagrangian (LARD) ──────────────────────────────────────────────
    auto *lardGroup = new QGroupBox(tr("Lagrangian (LARD)"), lardTab);
    auto *lardForm = new QFormLayout(lardGroup);

    m_qualityStepSpin = new QDoubleSpinBox(lardGroup);
    m_qualityStepSpin->setRange(0.0, 3600.0);
    m_qualityStepSpin->setDecimals(2);
    m_qualityStepSpin->setSuffix(QStringLiteral(" s"));
    m_qualityStepSpin->setToolTip(
        tr("Transport substep (QUALITY_STEP). 0 follows the routing step; "
           "smaller values refine transport without changing hydraulics."));
    lardForm->addRow(tr("Quality &step:"), m_qualityStepSpin);

    m_maxSegmentsSpin = new QSpinBox(lardGroup);
    m_maxSegmentsSpin->setRange(2, 10000);
    m_maxSegmentsSpin->setToolTip(
        tr("Segment slab capacity per link (MAX_SEGMENTS_PER_LINK)."));
    lardForm->addRow(tr("Max se&gments per link:"), m_maxSegmentsSpin);

    m_dispersionCombo = new QComboBox(lardGroup);
    m_dispersionCombo->addItem(tr("Off"),  QStringLiteral("OFF"));
    m_dispersionCombo->addItem(tr("RWPT (random-walk particle tracking)"),
                               QStringLiteral("RWPT"));
    m_dispersionCombo->setToolTip(
        tr("Longitudinal dispersion from resolved vertical shear "
           "(option DISPERSION)."));
    lardForm->addRow(tr("&Dispersion:"), m_dispersionCombo);

    m_rwptSeedSpin = new QSpinBox(lardGroup);
    m_rwptSeedSpin->setRange(-2147483647, 2147483647);
    m_rwptSeedSpin->setToolTip(
        tr("Deterministic RWPT seed (RWPT_SEED) — the same seed reproduces "
           "a run bit-for-bit at any thread count."));
    lardForm->addRow(tr("RWPT s&eed:"), m_rwptSeedSpin);
    lardTabLay->addWidget(lardGroup);

    connect(m_dispersionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshGates(); });

    // ── Reserved species ───────────────────────────────────────────────
    auto *resGroup = new QGroupBox(tr("Reserved species"), resTab);
    auto *resLay   = new QVBoxLayout(resGroup);
    m_waterAgeBox = new QCheckBox(tr("Track water age"), resGroup);
    m_waterAgeBox->setToolTip(
        tr("Transported water age as the reserved species __WATER_AGE__, "
           "reported in hours (option WATER_AGE)."));
    m_heatTransportBox = new QCheckBox(tr("Simulate heat transport"), resGroup);
    m_heatTransportBox->setToolTip(
        tr("Transported temperature as the reserved species "
           "__TEMPERATURE__, reported in °C (option HEAT_TRANSPORT)."));
    resLay->addWidget(m_waterAgeBox);
    resLay->addWidget(m_heatTransportBox);
    // Both feed the dialog's "is there anything to transport?" row gate.
    connect(m_waterAgeBox, &QCheckBox::toggled,
            this, [this](bool) { emit gateInputsChanged(); });
    connect(m_heatTransportBox, &QCheckBox::toggled,
            this, [this](bool) { emit gateInputsChanged(); });
    auto *resNote = new QLabel(
        tr("Per-source initial ages are edited below; heat meteorology is "
           "configured in its component file until the heat page arrives."),
        resGroup);
    resNote->setWordWrap(true);
    resLay->addWidget(resNote);

    // Y3b: the Water Age Sources editor, reachable from the page that
    // enables the tracking it configures.
    auto *ageSrcBtn = new QPushButton(tr("Edit Source Ages..."), resGroup);
    ageSrcBtn->setObjectName(QStringLiteral("qt_editAgeSourcesBtn"));
    ageSrcBtn->setToolTip(
        tr("Initial age of water entering by each source pathway "
           "(WATER_AGE_SOURCES). Negative ages extract age-volume."));
    connect(ageSrcBtn, &QPushButton::clicked, this, [this]() {
        OpenSWMMVis::WaterAgeSourcesDialog dlg(ctx_.engine(), this);
        dlg.exec();   // writes straight to the engine; OK/Cancel is its own
    });
    resLay->addWidget(ageSrcBtn);

    // G-A1: the per-element Initial Quality editor, reachable from the
    // page that configures the quality run it seeds.
    auto *initQualBtn = new QPushButton(tr("Edit Initial Quality..."),
                                        resGroup);
    initQualBtn->setObjectName(QStringLiteral("qt_editInitialQualityBtn"));
    initQualBtn->setToolTip(
        tr("Per-node and per-link initial concentrations "
           "(INITIAL_QUALITY), overriding the global Cinit."));
    connect(initQualBtn, &QPushButton::clicked, this, [this]() {
        OpenSWMMVis::InitialQualityDialog dlg(ctx_.engine(), this);
        dlg.exec();   // writes straight to the engine; OK/Cancel is its own
    });
    resLay->addWidget(initQualBtn);
    resTabLay->addWidget(resGroup);

    solTabLay->addStretch();
    ardTabLay->addStretch();
    lardTabLay->addStretch();
    resTabLay->addStretch();

    m_tabs->addTab(solTab,  tr("Solver"));
    m_tabs->addTab(ardTab,  tr("Eulerian ARD"));
    m_tabs->addTab(lardTab, tr("Lagrangian (LARD)"));
    m_tabs->addTab(resTab,  tr("Reserved Species"));
}

void QualityPage::tagWidgets()
{
    tagOption(m_qualitySolverCombo, "QUALITY_SOLVER");
    tagOption(m_outfallBackflowCombo, "OUTFALL_BACKFLOW_QUALITY");
    tagOption(m_qualityStepSpin, "QUALITY_STEP");
    tagOption(m_maxSegmentsSpin, "MAX_SEGMENTS_PER_LINK");
    tagOption(m_dispersionCombo, "DISPERSION");
    tagOption(m_rwptSeedSpin, "RWPT_SEED");
    tagOption(m_waterAgeBox, "WATER_AGE");
    tagOption(m_heatTransportBox, "HEAT_TRANSPORT");
    tagOption(m_fvScalarSchemeCombo, "FV_SCALAR_SCHEME");
}

void QualityPage::refreshGates()
{
    // Intra-page: the RWPT seed only means anything under the RWPT dispersion
    // model. The two solver-driven TAB gates live in the dialog's gate table.
    if (m_rwptSeedSpin && m_dispersionCombo) {
        const bool on = m_dispersionCombo->currentData().toString()
                            == QLatin1String("RWPT");
        m_rwptSeedSpin->setEnabled(on);
        m_rwptSeedSpin->setToolTip(
            on ? tr("Deterministic RWPT seed (RWPT_SEED) — the same seed "
                    "reproduces a run bit-for-bit at any thread count.")
               : tr("Applies while the RWPT dispersion model is selected"));
    }
}

void QualityPage::applyCapabilities()
{
    // Same capability-probe rule the FV group gives: the C ABI is string-keyed,
    // so an engine built before the transport keys reached swmm_options_get
    // (subplan Y0) is only detectable by asking it.
    if (ctx_.caps().transport) return;

    const QString ttip =
        ctx_.caps().legacy ? tr("Not available in SWMM 5 (legacy engine).")
                           : tr("This engine build predates the quality/transport "
                                "option surface.");
    if (m_qualitySolverCombo) {
        m_qualitySolverCombo->setEnabled(false);
        m_qualitySolverCombo->setToolTip(ttip);
    }
    // OUTFALL_BACKFLOW_QUALITY exists in BOTH engines, but a legacy build
    // predating it would drop the key on save — same gate as the rest of the
    // quality surface.
    if (m_outfallBackflowCombo) {
        m_outfallBackflowCombo->setEnabled(false);
        m_outfallBackflowCombo->setToolTip(ttip);
    }
    // The ARD / LARD TABS are gated on caps.transport too (dialog gate table),
    // so a frozen solver combo cannot leave one of them reachable.
    if (m_waterAgeBox)      { m_waterAgeBox->setEnabled(false);
                              m_waterAgeBox->setToolTip(ttip); }
    if (m_heatTransportBox) { m_heatTransportBox->setEnabled(false);
                              m_heatTransportBox->setToolTip(ttip); }
}

void QualityPage::read()
{
    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0) c->setCurrentIndex(idx);
    };
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

    // Fallbacks are the ENGINE's documented defaults (Y0's gate 1 pins them);
    // if the engine's defaults drift, that gate flags this block for a resync.
    selectComboByData(m_qualitySolverCombo,
                      ctx_.option("QUALITY_SOLVER", QStringLiteral("LEGACY")));
    selectComboByData(m_outfallBackflowCombo,
                      ctx_.option("OUTFALL_BACKFLOW_QUALITY",
                                  QStringLiteral("LAST")));
    m_qualityStepSpin->setValue(optDouble("QUALITY_STEP", 0.0));
    m_maxSegmentsSpin->setValue(optInt("MAX_SEGMENTS_PER_LINK", 100));
    selectComboByData(m_dispersionCombo,
                      ctx_.option("DISPERSION", QStringLiteral("OFF")));
    m_rwptSeedSpin->setValue(optInt("RWPT_SEED", 0));
    m_waterAgeBox->setChecked(
        SimulationOptionsDialog::parseEngineBool(
            ctx_.option("WATER_AGE", QStringLiteral("NO"))) == Qt::Checked);
    m_heatTransportBox->setChecked(
        SimulationOptionsDialog::parseEngineBool(
            ctx_.option("HEAT_TRANSPORT", QStringLiteral("NO"))) == Qt::Checked);
    selectComboByData(m_fvScalarSchemeCombo,
                      ctx_.option("FV_SCALAR_SCHEME", QStringLiteral("MUSCL")));
}

int QualityPage::write()
{
    int n = 0;
    // Every key is written independently of the solver selection — the engine
    // accepts them under any solver (Y0 §2.1), so a user can configure LARD
    // before switching to it and the settings survive. The context's
    // numeric-aware compare keeps QUALITY_STEP from churning against the
    // engine's "0.000000" rendering.
    n += ctx_.writeIfChanged("QUALITY_SOLVER",
                             m_qualitySolverCombo->currentData().toString());
    n += ctx_.writeIfChanged("OUTFALL_BACKFLOW_QUALITY",
                             m_outfallBackflowCombo->currentData().toString());
    n += ctx_.writeIfChanged("QUALITY_STEP",
                             QString::number(m_qualityStepSpin->value(), 'f', 2));
    n += ctx_.writeIfChanged("MAX_SEGMENTS_PER_LINK",
                             QString::number(m_maxSegmentsSpin->value()));
    n += ctx_.writeIfChanged("DISPERSION",
                             m_dispersionCombo->currentData().toString());
    n += ctx_.writeIfChanged("RWPT_SEED",
                             QString::number(m_rwptSeedSpin->value()));
    n += ctx_.writeIfChanged("WATER_AGE",
                             SimulationOptionsDialog::engineBoolString(
                                 m_waterAgeBox->isChecked()));
    n += ctx_.writeIfChanged("HEAT_TRANSPORT",
                             SimulationOptionsDialog::engineBoolString(
                                 m_heatTransportBox->isChecked()));
    n += ctx_.writeIfChanged("FV_SCALAR_SCHEME",
                             m_fvScalarSchemeCombo->currentData().toString());
    return n;
}

} // namespace openswmmvis::ui
