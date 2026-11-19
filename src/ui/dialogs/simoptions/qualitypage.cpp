/**
 * @file      qualitypage.cpp
 * @brief     Simulation Options → Quality & Transport.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/simoptions/qualitypage.h"

#include <QCheckBox>
#include <QLocale>
#include <QLineEdit>
#include <QDoubleValidator>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QTableView>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "ui/dialogs/initialqualitydialog.h"
#include "ui/dialogs/mesh2dsectionroweditor.h"
#include "ui/dialogs/simoptions/ardtransportmodels.h"
#include "ui/util/tableclipboard.h"
#include "ui/util/usernotice.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/wateragesourcesdialog.h"

#include <openswmm/engine/openswmm_process_components.h>
#include <openswmm/engine/openswmm_transport.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace openswmmvis::ui
{

QualityPage::QualityPage(SimOptionsContext &ctx, QWidget *parent) : SimOptionsPage(ctx, parent)
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
    return m_qualitySolverCombo ? m_qualitySolverCombo->currentData().toString() : QString();
}

bool QualityPage::tracksReservedSpecies() const
{
    return (m_waterAgeBox && m_waterAgeBox->isChecked()) ||
           (m_heatTransportBox && m_heatTransportBox->isChecked());
}

void QualityPage::setHeatEnabled(bool enabled)
{
    m_heatTransportBox->setChecked(enabled);
}

bool QualityPage::heatEnabled() const
{
    return m_heatTransportBox && m_heatTransportBox->isChecked();
}
bool QualityPage::ageEnabled() const
{
    return m_waterAgeBox && m_waterAgeBox->isChecked();
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
    m_qualitySolverCombo->addItem(tr("Legacy (complete mix)"), QStringLiteral("LEGACY"));
    m_qualitySolverCombo->addItem(tr("Eulerian ARD (advection–reaction–dispersion)"),
                                  QStringLiteral("EULERIAN_ARD"));
    m_qualitySolverCombo->addItem(tr("Lagrangian (LARD)"), QStringLiteral("LAGRANGIAN"));
    m_qualitySolverCombo->setToolTip(
        tr("Transport engine for pollutants and reserved species "
           "(option QUALITY_SOLVER). The tabs below gray themselves out when "
           "they do not apply to the selected solver."));
    connect(m_qualitySolverCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { emit gateInputsChanged(); });
    headerForm->addRow(tr("Sol&ver:"), m_qualitySolverCombo);
    root->addLayout(headerForm);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("qualityTabs"));
    root->addWidget(m_tabs, 1);

    auto *solTab = new QWidget(m_tabs);
    auto *solTabLay = new QVBoxLayout(solTab);
    auto *ardTab = new QWidget(m_tabs);
    auto *ardTabLay = new QVBoxLayout(ardTab);
    auto *lardTab = new QWidget(m_tabs);
    auto *lardTabLay = new QVBoxLayout(lardTab);
    auto *resTab = new QWidget(m_tabs);
    auto *resTabLay = new QVBoxLayout(resTab);

    // ── General — what applies under EVERY solver ──────────────────────
    auto *solGroup = new QGroupBox(tr("Boundary quality"), solTab);
    auto *solForm = new QFormLayout(solGroup);

    // Boundary re-entry quality at outfalls — solver-independent (both engines
    // honor it), which is exactly what this tab is for.
    m_outfallBackflowCombo = new QComboBox(solGroup);
    m_outfallBackflowCombo->addItem(tr("Hold last concentration (legacy)"), QStringLiteral("LAST"));
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
    // G-02: the engine keeps these settings in the transport component's file; the label says
    // which file (readArd), and Apply registers one named after the model when needed.
    m_ardFileLabel = new QLabel(ardGroup);
    m_ardFileLabel->setObjectName(QStringLiteral("ardFileLabel"));
    m_ardFileLabel->setWordWrap(true);
    ardLay->addWidget(m_ardFileLabel);

    // FV_SCALAR_SCHEME lives here, not on the Routing & Hydraulics page: its
    // only live consumer is the ARD engine, which reads it under any routing
    // model (FV routing itself transports no species).
    auto *ardForm = new QFormLayout();
    m_fvScalarSchemeCombo = new QComboBox(ardGroup);
    m_fvScalarSchemeCombo->addItem(tr("MUSCL"), QStringLiteral("MUSCL"));
    m_fvScalarSchemeCombo->addItem(tr("Upwind"), QStringLiteral("UPWIND"));
    m_fvScalarSchemeCombo->addItem(tr("QUICKEST-ULTIMATE"), QStringLiteral("QUICKEST_ULTIMATE"));
    m_fvScalarSchemeCombo->setToolTip(
        tr("Advection scheme for water-quality scalars on the ARD transport "
           "mesh (FV_SCALAR_SCHEME). Read under any routing model; a bound "
           "component file's [TRANSPORT_OPTIONS] SCALAR_SCHEME overrides it."));
    ardForm->addRow(tr("Scalar scheme:"), m_fvScalarSchemeCombo);

    m_ardDispersionCombo = new QComboBox(ardGroup);
    m_ardDispersionCombo->setObjectName(QStringLiteral("ardDispersionCombo"));
    m_ardDispersionCombo->addItem(tr("Off"), int(SWMM_DISPERSION_OFF));
    m_ardDispersionCombo->addItem(tr("Fischer (from the hydraulics)"), int(SWMM_DISPERSION_FISCHER));
    m_ardDispersionCombo->addItem(tr("Fixed coefficient"), int(SWMM_DISPERSION_VALUE));
    m_ardDispersionCombo->setToolTip(
        tr("Longitudinal dispersion on every conduit ([TRANSPORT_OPTIONS] DISPERSION). Rows in "
           "Conduit dispersion below replace it on their conduits."));
    ardForm->addRow(tr("&Dispersion:"), m_ardDispersionCombo);

    const auto numberField = [ardGroup](const QString &name) {
        auto *edit = new QLineEdit(ardGroup);
        edit->setObjectName(name);
        auto *validator = new QDoubleValidator(0.0, 1e300, 17, edit);
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::ScientificNotation);
        edit->setValidator(validator);
        return edit;
    };
    const auto withUnits = [ardGroup](QWidget *field, QLabel **units) {
        auto *row = new QHBoxLayout;
        row->addWidget(field, 1);
        *units = new QLabel(ardGroup);
        row->addWidget(*units);
        return row;
    };
    m_ardDispersionValue = numberField(QStringLiteral("ardDispersionValue"));
    m_ardDispersionValue->setToolTip(tr("The coefficient used when Dispersion is Fixed."));
    ardForm->addRow(tr("Co&efficient:"), withUnits(m_ardDispersionValue, &m_ardCoefficientUnits));
    m_ardTargetDx = numberField(QStringLiteral("ardTargetDx"));
    m_ardTargetDx->setToolTip(
        tr("Length of the transport cells the conduits are divided into. 0 lets the engine "
           "choose."));
    ardForm->addRow(tr("Transport cell &spacing:"), withUnits(m_ardTargetDx, &m_ardSpacingUnits));

    m_ardDetailedOutput = new QLineEdit(ardGroup);
    m_ardDetailedOutput->setObjectName(QStringLiteral("ardDetailedOutput"));
    m_ardDetailedOutput->setPlaceholderText(tr("Off"));
    m_ardDetailedOutput->setToolTip(
        tr("CSV file for per-cell transport results. Empty turns it off. A relative path is "
           "taken from the folder the program runs in; Browse gives a full path."));
    auto *browse = new QPushButton(tr("Browse…"), ardGroup);
    browse->setAutoDefault(false);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString start = m_ardDetailedOutput->text().trimmed();
        const QString path = QFileDialog::getSaveFileName(this, tr("Detailed transport output"),
                                                          start, tr("CSV files (*.csv)"));
        if (!path.isEmpty())
            m_ardDetailedOutput->setText(path);
    });
    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(m_ardDetailedOutput, 1);
    outputRow->addWidget(browse);
    ardForm->addRow(tr("Detailed &output:"), outputRow);
    connect(m_ardDispersionCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_ardDispersionValue->setEnabled(m_ardDispersionCombo->currentData().toInt() ==
                                         SWMM_DISPERSION_VALUE);
    });
    ardLay->addLayout(ardForm);
    ardTabLay->addWidget(ardGroup);

    // Per-conduit dispersion, boundaries and sources: staged tables, written on Apply.
    m_ardConduits = new ArdConduitDispersionModel(this);
    m_ardConduits->setObjectName(QStringLiteral("ardConduitDispersion"));
    m_ardBoundaries = new ArdTransportRowModel(ArdTransportRowModel::Kind::Boundary, this);
    m_ardBoundaries->setObjectName(QStringLiteral("ardBoundaries"));
    m_ardSources = new ArdTransportRowModel(ArdTransportRowModel::Kind::Source, this);
    m_ardSources->setObjectName(QStringLiteral("ardSources"));
    auto *ardTables = new QTabWidget(ardTab);
    ardTables->setObjectName(QStringLiteral("ardTables"));
    const auto tablePage = [this, ardTables](QAbstractItemModel *model, const char *key,
                                             const QString &accessibleName,
                                             const std::function<void()> &add,
                                             const std::function<void(int)> &remove) {
        auto *page = new QWidget(ardTables);
        auto *layout = new QVBoxLayout(page);
        auto *view = new QTableView(page);
        view->setObjectName(model->objectName() + QStringLiteral("Table"));
        view->setModel(model);
        view->setItemDelegate(new Mesh2DChoiceDelegate(view));
        view->setAccessibleName(accessibleName);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->verticalHeader()->setVisible(false);
        view->horizontalHeader()->setStretchLastSection(true);
        TableClipboard::install(view);
        tagOption(view, key);
        layout->addWidget(view, 1);
        auto *buttons = new QHBoxLayout;
        auto *addButton = new QPushButton(tr("Add"), page);
        addButton->setObjectName(model->objectName() + QStringLiteral("Add"));
        addButton->setAutoDefault(false);
        auto *removeButton = new QPushButton(tr("Remove"), page);
        removeButton->setObjectName(model->objectName() + QStringLiteral("Remove"));
        removeButton->setAutoDefault(false);
        connect(addButton, &QPushButton::clicked, this, [view, model, add] {
            add();
            view->setCurrentIndex(model->index(model->rowCount() - 1, 0));
        });
        connect(removeButton, &QPushButton::clicked, this, [view, remove] {
            if (view->currentIndex().isValid())
                remove(view->currentIndex().row());
        });
        buttons->addWidget(addButton);
        buttons->addWidget(removeButton);
        buttons->addStretch();
        layout->addLayout(buttons);
        return std::make_pair(page, addButton);
    };
    auto conduits = tablePage(
        m_ardConduits, "CONDUIT_DISPERSION", tr("Conduit dispersion"),
        [this] { m_ardConduits->addRow({m_ardConduits->conduits().value(0), 0.0}); },
        [this](int row) { m_ardConduits->removeRowAt(row); });
    auto boundaries = tablePage(
        m_ardBoundaries, "TRANSPORT_BOUNDARIES", tr("Transport boundaries"),
        [this] {
            m_ardBoundaries->addRow({m_ardBoundaries->elements().value(0),
                                     m_ardBoundaries->species().value(0), false, 0.0, {}});
        },
        [this](int row) { m_ardBoundaries->removeRowAt(row); });
    auto sources = tablePage(
        m_ardSources, "TRANSPORT_SOURCES", tr("Transport sources"),
        [this] {
            m_ardSources->addRow({m_ardSources->elements().value(0),
                                  m_ardSources->species().value(0), false, 0.0, {}});
        },
        [this](int row) { m_ardSources->removeRowAt(row); });
    m_ardConduitAdd = conduits.second;
    m_ardBoundaryAdd = boundaries.second;
    m_ardSourceAdd = sources.second;
    ardTables->addTab(conduits.first, tr("Conduit dispersion"));
    ardTables->addTab(boundaries.first, tr("Boundaries"));
    ardTables->addTab(sources.first, tr("Sources"));
    ardTabLay->addWidget(ardTables, 1);
    m_ardSpeciesHint = new QLabel(
        tr("Boundaries and sources apply to the species of the Reaction System, and this model "
           "has none. Add them in the Reaction System editor; pollutants enter through "
           "inflows."),
        ardTab);
    m_ardSpeciesHint->setObjectName(QStringLiteral("ardSpeciesHint"));
    m_ardSpeciesHint->setWordWrap(true);
    ardTabLay->addWidget(m_ardSpeciesHint);

    // ── Lagrangian (LARD) ──────────────────────────────────────────────
    auto *lardGroup = new QGroupBox(tr("Lagrangian (LARD)"), lardTab);
    auto *lardForm = new QFormLayout(lardGroup);

    m_qualityStepSpin = new QDoubleSpinBox(lardGroup);
    m_qualityStepSpin->setRange(0.0, 3600.0);
    m_qualityStepSpin->setDecimals(6);
    m_qualityStepSpin->setSuffix(QStringLiteral(" s"));
    m_qualityStepSpin->setToolTip(
        tr("Transport substep (QUALITY_STEP). 0 follows the routing step; "
           "smaller values refine transport without changing hydraulics."));
    lardForm->addRow(tr("Quality &step:"), m_qualityStepSpin);

    m_maxSegmentsSpin = new QSpinBox(lardGroup);
    m_maxSegmentsSpin->setRange(2, 10000);
    m_maxSegmentsSpin->setToolTip(tr("Segment slab capacity per link (MAX_SEGMENTS_PER_LINK)."));
    lardForm->addRow(tr("Max se&gments per link:"), m_maxSegmentsSpin);

    m_dispersionCombo = new QComboBox(lardGroup);
    m_dispersionCombo->addItem(tr("Off"), QStringLiteral("OFF"));
    m_dispersionCombo->addItem(tr("RWPT (random-walk particle tracking)"), QStringLiteral("RWPT"));
    m_dispersionCombo->setToolTip(tr("Longitudinal dispersion from resolved vertical shear "
                                     "(option DISPERSION)."));
    lardForm->addRow(tr("&Dispersion:"), m_dispersionCombo);

    m_rwptSeedSpin = new QSpinBox(lardGroup);
    m_rwptSeedSpin->setRange(-2147483647, 2147483647);
    m_rwptSeedSpin->setToolTip(tr("Deterministic RWPT seed (RWPT_SEED) — the same seed reproduces "
                                  "a run bit-for-bit at any thread count."));
    lardForm->addRow(tr("RWPT s&eed:"), m_rwptSeedSpin);
    lardTabLay->addWidget(lardGroup);

    connect(m_dispersionCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { refreshGates(); });

    // ── Reserved species ───────────────────────────────────────────────
    auto *resGroup = new QGroupBox(tr("Reserved species"), resTab);
    auto *resLay = new QVBoxLayout(resGroup);
    m_waterAgeBox = new QCheckBox(tr("Track water age"), resGroup);
    m_waterAgeBox->setToolTip(tr("Transported water age as the reserved species __WATER_AGE__, "
                                 "reported in hours (option WATER_AGE)."));
    m_heatTransportBox = new QCheckBox(tr("Simulate heat transport"), resGroup);
    m_heatTransportBox->setToolTip(tr("Transported temperature as the reserved species "
                                      "__TEMPERATURE__, reported in °C (option HEAT_TRANSPORT)."));
    resLay->addWidget(m_waterAgeBox);
    resLay->addWidget(m_heatTransportBox);
    // Constants of the heat budget (CSH Table 4.1 names and defaults). Plain C-locale text:
    // the wind coefficients are ~1e-8, which a spin box cannot show.
    auto *heatForm = new QFormLayout;
    const struct { const char *key; const char *label; const char *tip; } heatKeys[] = {
        {"WATER_DENSITY", QT_TR_NOOP("Water density (kg/m³):"), QT_TR_NOOP("WATER_DENSITY; default 1000.")},
        {"WATER_SPECIFIC_HEAT_CAPACITY", QT_TR_NOOP("Specific heat (J/kg·K):"),
         QT_TR_NOOP("WATER_SPECIFIC_HEAT_CAPACITY; default 4184.")},
        {"WIND_FUNC_COEFF_A", QT_TR_NOOP("Wind function A:"),
         QT_TR_NOOP("WIND_FUNC_COEFF_A of the evaporative wind function; default 1.505e-8.")},
        {"WIND_FUNC_COEFF_B", QT_TR_NOOP("Wind function B:"),
         QT_TR_NOOP("WIND_FUNC_COEFF_B of the evaporative wind function; default 1.6e-8.")},
        {"PRESSURE_RATIO", QT_TR_NOOP("Pressure ratio:"),
         QT_TR_NOOP("PRESSURE_RATIO, local to sea-level air pressure; default 1.")},
    };
    for (const auto &k : heatKeys) {
        auto *edit = new QLineEdit(resGroup);
        auto *validator = new QDoubleValidator(0.0, 1e30, 17, edit);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        QLocale c = QLocale::c();
        c.setNumberOptions(QLocale::RejectGroupSeparator);
        validator->setLocale(c);
        edit->setValidator(validator);
        edit->setToolTip(tr(k.tip));
        heatForm->addRow(tr(k.label), edit);
        m_heatConstants.append({QString::fromLatin1(k.key), edit});
    }
    resLay->addLayout(heatForm);
    // Both feed the dialog's "is there anything to transport?" row gate.
    connect(m_waterAgeBox, &QCheckBox::toggled, this, [this](bool) { emit gateInputsChanged(); });
    connect(m_heatTransportBox, &QCheckBox::toggled, this,
            [this](bool) { emit gateInputsChanged(); });
    auto *resNote =
        new QLabel(tr("Configure source temperatures and heat exchange in Model → Heat. "
                      "Weather and shared radiation are configured in Climatology. "
                      "Temperature transport and local heat exchange are separate settings."),
                   resGroup);
    resNote->setWordWrap(true);
    resLay->addWidget(resNote);
    auto *heatSettings = new QPushButton(tr("Edit heat configuration…"), resGroup);
    heatSettings->setAutoDefault(false);
    heatSettings->setObjectName("qt_editHeatConfigBtn");
    heatSettings->setToolTip(tr(
        "Heat settings join this Simulation Options edit. Apply commits them together as one Undo step; Cancel reverts them."));
    connect(heatSettings, &QPushButton::clicked, this, &QualityPage::showHeatSettingsRequested);
    resLay->addWidget(heatSettings);

    // Y3b: the Water Age Sources editor, reachable from the page that
    // enables the tracking it configures.
    auto *ageSrcBtn = new QPushButton(tr("Edit Source Ages..."), resGroup);
    ageSrcBtn->setObjectName(QStringLiteral("qt_editAgeSourcesBtn"));
    ageSrcBtn->setToolTip(
        tr("Initial age of water entering by each source pathway "
           "(WATER_AGE_SOURCES). Negative ages extract age-volume. "
           "Accepted source-age changes remain applied if Simulation Options is canceled."));
    connect(ageSrcBtn, &QPushButton::clicked, this, [this]() {
        QPointer<SWMMVisProjectWindow> project(ctx_.projectWindow());
        if (!ctx_.engine())
            return;
        OpenSWMMVis::WaterAgeSourcesDialog dlg(ctx_.engine(), this);
        if (auto *layer = ctx_.modelLayer()) {
            connect(layer, &SWMMModelLayer::engineAboutToClose, &dlg,
                    &OpenSWMMVis::WaterAgeSourcesDialog::invalidateEngine);
            connect(layer, &QObject::destroyed, &dlg,
                    &OpenSWMMVis::WaterAgeSourcesDialog::invalidateEngine);
        }
        if (project) {
            connect(project, &SWMMVisProjectWindow::aboutToClose, &dlg,
                    &OpenSWMMVis::WaterAgeSourcesDialog::invalidateEngine);
            connect(&dlg, &OpenSWMMVis::WaterAgeSourcesDialog::changesApplied, project, [project] {
                if (project)
                    project->setHasChanges(true);
            });
        }
        dlg.exec(); // writes straight to the engine; OK/Cancel is its own
        if (project && dlg.wroteAnyChanges())
            project->setHasChanges(true);
    });
    resLay->addWidget(ageSrcBtn);

    // G-A1: the per-element Initial Quality editor, reachable from the
    // page that configures the quality run it seeds.
    auto *initQualBtn = new QPushButton(tr("Edit Initial Quality..."), resGroup);
    initQualBtn->setObjectName(QStringLiteral("qt_editInitialQualityBtn"));
    initQualBtn->setToolTip(tr("Per-node and per-link initial concentrations "
                               "(INITIAL_QUALITY), overriding the global Cinit."));
    connect(initQualBtn, &QPushButton::clicked, this, [this]() {
        if (!ctx_.engine())
            return;
        OpenSWMMVis::InitialQualityDialog dlg(ctx_.engine(), this);
        if (auto *layer = ctx_.modelLayer()) {
            connect(layer, &SWMMModelLayer::engineAboutToClose, &dlg,
                    &OpenSWMMVis::InitialQualityDialog::invalidateEngine);
            connect(layer, &QObject::destroyed, &dlg,
                    &OpenSWMMVis::InitialQualityDialog::invalidateEngine);
        }
        if (auto *project = ctx_.projectWindow()) {
            connect(project, &SWMMVisProjectWindow::aboutToClose, &dlg,
                    &OpenSWMMVis::InitialQualityDialog::invalidateEngine);
            connect(&dlg, &OpenSWMMVis::InitialQualityDialog::changesApplied, project,
                    [project] { project->setHasChanges(true); });
        }
        dlg.exec(); // successful and partial writes survive outer Cancel
    });
    resLay->addWidget(initQualBtn);
    resTabLay->addWidget(resGroup);

    solTabLay->addStretch();
    ardTabLay->addStretch();
    lardTabLay->addStretch();
    resTabLay->addStretch();

    m_tabs->addTab(solTab, tr("General"));
    m_tabs->addTab(ardTab, tr("Eulerian ARD"));
    m_tabs->addTab(lardTab, tr("Lagrangian (LARD)"));
    m_tabs->addTab(resTab, tr("Reserved Species"));
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
    for (const auto &[key, edit] : std::as_const(m_heatConstants))
        tagOption(edit, key.toLatin1().constData());
    tagOption(m_fvScalarSchemeCombo, "FV_SCALAR_SCHEME");
    for (QWidget *w : {static_cast<QWidget *>(m_ardDispersionCombo),
                       static_cast<QWidget *>(m_ardDispersionValue),
                       static_cast<QWidget *>(m_ardTargetDx),
                       static_cast<QWidget *>(m_ardDetailedOutput)})
        tagOption(w, "TRANSPORT_OPTIONS");
}

void QualityPage::refreshGates()
{
    // Intra-page: the RWPT seed only means anything under the RWPT dispersion
    // model. The two solver-driven TAB gates live in the dialog's gate table.
    if (m_rwptSeedSpin && m_dispersionCombo) {
        const bool on = m_dispersionCombo->currentData().toString() == QLatin1String("RWPT");
        m_rwptSeedSpin->setEnabled(on);
        m_rwptSeedSpin->setToolTip(on ? tr("Deterministic RWPT seed (RWPT_SEED) — the same seed "
                                           "reproduces a run bit-for-bit at any thread count.")
                                      : tr("Applies while the RWPT dispersion model is selected"));
    }
}

void QualityPage::applyCapabilities()
{
    // Same capability-probe rule the FV group gives: the C ABI is string-keyed,
    // so an engine built before the transport keys reached swmm_options_get
    // (subplan Y0) is only detectable by asking it.
    if (ctx_.caps().transport)
        return;

    const QString ttip = ctx_.caps().legacy ? tr("Not available in SWMM 5 (legacy engine).")
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
    if (m_waterAgeBox) {
        m_waterAgeBox->setEnabled(false);
        m_waterAgeBox->setToolTip(ttip);
    }
    if (m_heatTransportBox) {
        m_heatTransportBox->setEnabled(false);
        m_heatTransportBox->setToolTip(ttip);
    }
}

void QualityPage::read()
{
    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0)
            c->setCurrentIndex(idx);
    };
    auto optDouble = [this](const char *key, double fallback) {
        bool okNum = false;
        const double v = ctx_.option(key, QString::number(fallback, 'g', 17)).toDouble(&okNum);
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
                      ctx_.option("OUTFALL_BACKFLOW_QUALITY", QStringLiteral("LAST")));
    m_qualityStepSpin->setValue(optDouble("QUALITY_STEP", 0.0));
    m_maxSegmentsSpin->setValue(optInt("MAX_SEGMENTS_PER_LINK", 100));
    selectComboByData(m_dispersionCombo, ctx_.option("DISPERSION", QStringLiteral("OFF")));
    m_rwptSeedSpin->setValue(optInt("RWPT_SEED", 0));
    m_waterAgeBox->setChecked(SimulationOptionsDialog::parseEngineBool(
                                  ctx_.option("WATER_AGE", QStringLiteral("NO"))) == Qt::Checked);
    m_heatTransportBox->setChecked(SimulationOptionsDialog::parseEngineBool(ctx_.option(
                                       "HEAT_TRANSPORT", QStringLiteral("NO"))) == Qt::Checked);
    for (const auto &[key, edit] : std::as_const(m_heatConstants)) {
        bool ok = false;
        const double v = ctx_.option(key.toLatin1().constData(), QString()).toDouble(&ok);
        edit->setText(ok ? QString::number(v, 'g', 17) : QString());
    }
    selectComboByData(m_fvScalarSchemeCombo,
                      ctx_.option("FV_SCALAR_SCHEME", QStringLiteral("MUSCL")));
    readArd();
    ctx_.captureEditors(this);
}

int QualityPage::write()
{
    int n = 0;
    // Every key is written independently of the solver selection — the engine
    // accepts them under any solver (Y0 §2.1), so a user can configure LARD
    // before switching to it and the settings survive. The context's
    // numeric-aware compare keeps QUALITY_STEP from churning against the
    // engine's "0.000000" rendering.
    n += writeOption("QUALITY_SOLVER", m_qualitySolverCombo->currentData().toString());
    n += writeOption("OUTFALL_BACKFLOW_QUALITY",
                             m_outfallBackflowCombo->currentData().toString());
    n += writeOption("QUALITY_STEP", QString::number(m_qualityStepSpin->value(), 'g', 17));
    n += writeOption("MAX_SEGMENTS_PER_LINK", QString::number(m_maxSegmentsSpin->value()));
    n += writeOption("DISPERSION", m_dispersionCombo->currentData().toString());
    n += writeOption("RWPT_SEED", QString::number(m_rwptSeedSpin->value()));
    n += writeOption("WATER_AGE",
                             SimulationOptionsDialog::engineBoolString(m_waterAgeBox->isChecked()));
    n += writeOption("HEAT_TRANSPORT", SimulationOptionsDialog::engineBoolString(
                                                   m_heatTransportBox->isChecked()));
    for (const auto &[key, edit] : std::as_const(m_heatConstants))
        if (edit->hasAcceptableInput())
            n += writeOption(key.toLatin1().constData(), edit->text().trimmed());
    n += writeOption("FV_SCALAR_SCHEME", m_fvScalarSchemeCombo->currentData().toString());
    n += writeArd();
    ctx_.captureEditors(this);
    return n;
}

// ---------------------------------------------------------------------------
// Eulerian ARD transport settings (review G-02)
// ---------------------------------------------------------------------------

namespace
{
constexpr const char *kArdComponentId = "org.hydrocouple.openswmm.transport.ard";

QString ardNumberText(double value)
{
    return QLocale::c().toString(value, 'g', QLocale::FloatingPointShortest);
}
} // namespace

QString QualityPage::transportFileName() const
{
    const SWMMModelLayer *layer = ctx_.modelLayer();
    const QString stem =
        layer ? QFileInfo(layer->modelFilePath()).completeBaseName() : QString();
    return (stem.isEmpty() ? QStringLiteral("model") : stem) + QStringLiteral(".ard");
}

QualityPage::ArdOptions QualityPage::ardDraft(bool *ok) const
{
    ArdOptions draft;
    draft.mode = m_ardDispersionCombo->currentData().toInt();
    bool okValue = true, okDx = true;
    const QString value = m_ardDispersionValue->text().trimmed();
    const QString dx = m_ardTargetDx->text().trimmed();
    draft.value = value.isEmpty() ? 0.0 : QLocale::c().toDouble(value, &okValue);
    draft.dx = dx.isEmpty() ? 0.0 : QLocale::c().toDouble(dx, &okDx);
    draft.detailed = m_ardDetailedOutput->text().trimmed();
    *ok = okValue && okDx && std::isfinite(draft.value) && std::isfinite(draft.dx) &&
          draft.value >= 0.0 && draft.dx >= 0.0;
    return draft;
}

void QualityPage::readArd()
{
    if (!m_ardDispersionCombo)
        return;
    SWMM_Engine engine = ctx_.engine();
    ArdOptions loaded;
    QString file;
    if (engine) {
        swmm_transport_get_dispersion_mode(engine, &loaded.mode);
        swmm_transport_get_dispersion_value(engine, &loaded.value);
        swmm_transport_get_target_dx(engine, &loaded.dx);
        char path[4096]{};
        if (swmm_transport_get_detailed_output(engine, path, sizeof path) == SWMM_OK)
            loaded.detailed = QString::fromUtf8(path);
        const int component = swmm_process_component_find(engine, kArdComponentId);
        char id[256]{}, config[4096]{}, resolved[4096]{};
        if (component >= 0 &&
            swmm_process_component_get(engine, component, id, sizeof id, config, sizeof config,
                                       resolved, sizeof resolved) == SWMM_OK)
            file = QFileInfo(QString::fromUtf8(config[0] ? config : resolved)).fileName();
    }
    m_ardLoaded = loaded;
    {
        const QSignalBlocker block(m_ardDispersionCombo);
        m_ardDispersionCombo->setCurrentIndex(
            std::max(0, m_ardDispersionCombo->findData(loaded.mode)));
    }
    m_ardDispersionValue->setText(ardNumberText(loaded.value));
    m_ardDispersionValue->setEnabled(loaded.mode == SWMM_DISPERSION_VALUE);
    m_ardTargetDx->setText(ardNumberText(loaded.dx));
    m_ardDetailedOutput->setText(loaded.detailed);

    // Coefficient and spacing are stored in the model's units, US or SI by flow units.
    const QString flowUnits = ctx_.option("FLOW_UNITS", QStringLiteral("CFS")).toUpper();
    const bool si = flowUnits == QLatin1String("CMS") || flowUnits == QLatin1String("LPS") ||
                    flowUnits == QLatin1String("MLD");
    const QString area = si ? QStringLiteral("m²/s") : QStringLiteral("ft²/s");
    m_ardCoefficientUnits->setText(area);
    m_ardSpacingUnits->setText(si ? QStringLiteral("m") : QStringLiteral("ft"));
    m_ardConduits->setUnits(area);

    QStringList problems;
    for (const QString &error :
         {m_ardConduits->load(engine), m_ardBoundaries->load(engine), m_ardSources->load(engine)})
        if (!error.isEmpty())
            problems << error;
    QString text = file.isEmpty()
                       ? tr("These settings are saved in a transport file beside the model, %1, "
                            "registered when you apply a change and written when the model is "
                            "saved.")
                             .arg(transportFileName())
                       : tr("These settings are saved in the model's transport file, %1, written "
                            "when the model is saved.")
                             .arg(file);
    if (!problems.isEmpty())
        text += QLatin1Char('\n') + problems.join(QLatin1Char('\n'));
    m_ardFileLabel->setText(text);
    refreshArdButtons();
}

void QualityPage::refreshArdButtons()
{
    const bool species = !m_ardBoundaries->species().isEmpty();
    m_ardConduitAdd->setEnabled(!m_ardConduits->conduits().isEmpty());
    m_ardBoundaryAdd->setEnabled(species && !m_ardBoundaries->elements().isEmpty());
    m_ardSourceAdd->setEnabled(species && !m_ardSources->elements().isEmpty());
    m_ardSpeciesHint->setVisible(!species);
}

bool QualityPage::validate(QString *warn)
{
    if (!m_ardDispersionCombo)
        return true;
    bool ok = false;
    ardDraft(&ok);
    QString problem;
    if (!ok)
        problem = tr("Eulerian ARD: the dispersion coefficient and the transport cell spacing "
                     "must be numbers that are not negative.");
    for (const QString &error : {m_ardConduits->validateCommit(ctx_.engine()),
                                 m_ardBoundaries->validateCommit(ctx_.engine()),
                                 m_ardSources->validateCommit(ctx_.engine())})
        if (problem.isEmpty() && !error.isEmpty())
            problem = tr("Eulerian ARD: %1").arg(error);
    if (problem.isEmpty())
        return true;
    if (warn)
        *warn = problem;
    return false;
}

int QualityPage::writeArd()
{
    m_ardWrote = false;
    SWMM_Engine engine = ctx_.engine();
    if (!engine || !m_ardDispersionCombo)
        return 0;
    int n = 0;
    QStringList refused;
    bool ok = false;
    const ArdOptions draft = ardDraft(&ok);
    if (ok && draft != m_ardLoaded) {
        const QByteArray path = draft.detailed.toUtf8();
        if (draft.value != m_ardLoaded.value &&
            swmm_transport_set_dispersion_value(engine, draft.value) != SWMM_OK)
            refused << tr("the dispersion coefficient");
        if (draft.mode != m_ardLoaded.mode &&
            swmm_transport_set_dispersion_mode(engine, draft.mode) != SWMM_OK)
            refused << tr("the dispersion mode");
        if (draft.dx != m_ardLoaded.dx && swmm_transport_set_target_dx(engine, draft.dx) != SWMM_OK)
            refused << tr("the transport cell spacing");
        if (draft.detailed != m_ardLoaded.detailed &&
            swmm_transport_set_detailed_output(
                engine, draft.detailed.isEmpty() ? nullptr : path.constData()) != SWMM_OK)
            refused << tr("the detailed output file");
        ctx_.recordWrittenKey("TRANSPORT_OPTIONS");
        ++n;
    }
    const auto commitTable = [&](auto *model, const char *key) {
        if (!model->isDirty())
            return;
        const QString error = model->commit(engine);
        if (!error.isEmpty()) {
            refused << error;
            return;
        }
        model->acceptChanges();
        ctx_.recordWrittenKey(key);
        ++n;
    };
    commitTable(m_ardConduits, "CONDUIT_DISPERSION");
    commitTable(m_ardBoundaries, "TRANSPORT_BOUNDARIES");
    commitTable(m_ardSources, "TRANSPORT_SOURCES");
    if (!refused.isEmpty())
        notifyUser(this, NoticeSeverity::Error,
                   tr("Eulerian ARD: the engine refused %1.").arg(refused.join(tr("; "))));
    m_ardWrote = n > 0;
    return n;
}

int QualityPage::registerTransportFile()
{
    SWMM_Engine engine = ctx_.engine();
    const bool wrote = std::exchange(m_ardWrote, false);
    int configured = 0;
    if (!wrote || !engine || swmm_transport_get_configured(engine, &configured) != SWMM_OK ||
        !configured || swmm_process_component_find(engine, kArdComponentId) >= 0)
        return 0;
    if (swmm_process_component_register(engine, kArdComponentId,
                                        transportFileName().toUtf8().constData()) != SWMM_OK) {
        notifyUser(this, NoticeSeverity::Error,
                   tr("Eulerian ARD: the settings were applied, but %1 could not be registered, "
                      "so saving the model will not keep them.")
                       .arg(transportFileName()));
        return 0;
    }
    return 1;
}

} // namespace openswmmvis::ui
