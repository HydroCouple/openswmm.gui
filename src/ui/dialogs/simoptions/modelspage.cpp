/*!
 * \file   modelspage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/modelspage.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/simulationoptionsdialog.h"

namespace openswmmvis::ui
{

namespace {

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

/*! Per-project key holding the module toggle's UI intent. */
QString module2DSettingsKey(const SWMMModelLayer *layer)
{
    return QStringLiteral("SWMMVis/Project/%1/Module2DEnabled")
        .arg(layer->modelFilePath());
}

} // namespace

ModelsPage::ModelsPage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
    tagWidgets();
}

QString ModelsPage::title() const
{
    return tr("Models / Processes");
}

bool ModelsPage::module2DEnabled() const
{
    return m_module2DBox && m_module2DBox->isChecked();
}

void ModelsPage::setModule2DEnabled(bool on)
{
    // Goes through toggled(), so this counts as a real intent — creating a
    // mesh is a deliberate act of turning the module on.
    if (m_module2DBox && m_module2DBox->isChecked() != on)
        m_module2DBox->setChecked(on);
}

void ModelsPage::setFlowRoutingText(const QString &routing)
{
    if (!m_routingMirror) return;
    m_routingMirror->setText(
        tr("<b>%1</b> — <a href=\"#\">change on Routing &amp; Hydraulics</a>")
            .arg(routing.isEmpty() ? tr("(unset)") : routing));
}

void ModelsPage::setTransport2DHooks(std::function<bool(int)> getter,
                                     std::function<void(int, bool)> setter)
{
    m_transport2DGet = std::move(getter);
    m_transport2DSet = std::move(setter);
}

void ModelsPage::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("modelsTabs"));
    root->addWidget(m_tabs, 1);

    auto *domTab = new QWidget(m_tabs); auto *domLay = new QVBoxLayout(domTab);
    auto *modTab = new QWidget(m_tabs); auto *modLay = new QVBoxLayout(modTab);
    auto *flgTab = new QWidget(m_tabs); auto *flgLay = new QVBoxLayout(flgTab);

    // ── Domains & Processes ────────────────────────────────────────────
    auto *procGroup = new QGroupBox(tr("Process models"), domTab);
    auto *procForm  = new QFormLayout(procGroup);

    m_infiltrationCombo = new QComboBox(procGroup);
    m_infiltrationCombo->addItem(tr("Horton"),                        QStringLiteral("HORTON"));
    m_infiltrationCombo->addItem(tr("Modified Horton"),               QStringLiteral("MOD_HORTON"));
    m_infiltrationCombo->addItem(tr("Green-Ampt"),                    QStringLiteral("GREEN_AMPT"));
    m_infiltrationCombo->addItem(tr("Modified Green-Ampt"),           QStringLiteral("MOD_GREEN_AMPT"));
    m_infiltrationCombo->addItem(tr("Curve Number"),                  QStringLiteral("CURVE_NUMBER"));
    m_infiltrationCombo->setToolTip(
        tr("Infiltration model used on every subcatchment (option INFILTRATION)."));
    procForm->addRow(tr("Infiltr&ation model:"), m_infiltrationCombo);

    // FLOW_ROUTING moved to the Routing & Hydraulics page header, where it
    // gates that page's tabs (PLAN §2.1). A read-only mirror keeps it visible
    // in its old home, with a link across.
    m_routingMirror = new QLabel(procGroup);
    m_routingMirror->setObjectName(QStringLiteral("routingMirrorLabel"));
    m_routingMirror->setTextFormat(Qt::RichText);
    connect(m_routingMirror, &QLabel::linkActivated, this,
            [this](const QString &) { emit showFlowRoutingPageRequested(); });
    procForm->addRow(tr("Flow routing:"), m_routingMirror);

    domLay->addWidget(procGroup);

    // Process activation. Checked = process runs (engine writes IGNORE_X NO);
    // unchecked = engine ignores the process (writes IGNORE_X YES). The
    // .inp surface keeps the legacy IGNORE_* keys — only the UI flips.
    auto *ignoreGroup = new QGroupBox(tr("Active processes"), domTab);
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
    domLay->addWidget(ignoreGroup);

    // U1 (2026-09-07) — the Domain × Species transport matrix, computed by
    // the engine (swmm_get_transport_matrix; the .rpt prints the same
    // table). Cells show on(n) / off:KEY / n/a with the reason as tooltip.
    // The 2D column is the one editable column: its cells mirror the 2D
    // page's TRANSPORT_* boxes (one model, two views). The other columns
    // follow this page's Water quality box, WATER_AGE / HEAT_TRANSPORT on
    // the Quality page and the reactions component — they re-evaluate on
    // Apply.
    auto *matrixGroup = new QGroupBox(tr("Transport by domain"), domTab);
    auto *matrixLay   = new QVBoxLayout(matrixGroup);
    m_transportMatrixTable = new QTableWidget(SWMM_TRANSPORT_DOMAIN_COUNT,
                                              SWMM_TRANSPORT_CLASS_COUNT, matrixGroup);
    m_transportMatrixTable->setObjectName(QStringLiteral("transportMatrixTable"));
    {
        QStringList cols, rows;
        for (int c = 0; c < SWMM_TRANSPORT_CLASS_COUNT; ++c)
            cols << QString::fromUtf8(swmm_transport_class_name(c));
        for (int d = 0; d < SWMM_TRANSPORT_DOMAIN_COUNT; ++d)
            rows << QString::fromUtf8(swmm_transport_domain_name(d));
        m_transportMatrixTable->setHorizontalHeaderLabels(cols);
        m_transportMatrixTable->setVerticalHeaderLabels(rows);
    }
    m_transportMatrixTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_transportMatrixTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_transportMatrixTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_transportMatrixTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_transportMatrixTable->setToolTip(
        tr("Which species classes the engine will carry in each domain for "
           "the options as last applied. on(n): n rows carried; off:KEY: "
           "switched off by that option; n/a: nothing to carry or the solver "
           "lacks it (hover a cell for the reason). Tick or untick a 2D cell "
           "to set the [2D_OPTIONS] TRANSPORT_* keys."));
    m_transportMatrixTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_transportMatrixTable->setMaximumHeight(200);
    matrixLay->addWidget(m_transportMatrixTable);
    auto *matrixNote = new QLabel(
        tr("Inherit-on: a class enabled for the project runs in every domain "
           "whose solver carries it. Groundwater has no transported quality "
           "(legacy parity); MSX needs a reactions component."),
        matrixGroup);
    matrixNote->setWordWrap(true);
    matrixLay->addWidget(matrixNote);
    domLay->addWidget(matrixGroup);
    connect(m_transportMatrixTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *item) {
                if (m_matrixSyncing || !item || !m_transport2DSet) return;
                if (item->row() != SWMM_TRANSPORT_DOMAIN_SURFACE_2D) return;
                m_matrixSyncing = true;
                m_transport2DSet(item->column(), item->checkState() == Qt::Checked);
                m_matrixSyncing = false;
                refreshTransportMatrix();
            });
    connect(m_ignoreQualityBox, &QCheckBox::toggled, this,
            [this](bool) { refreshTransportMatrix(); });

    // ── Modules ────────────────────────────────────────────────────────
    // 1D is the always-on core. 2D Surface Routing is an optional module
    // gated by a project-level toggle here. When enabled, the dedicated
    // "2D Surface Routing" page becomes interactive; when disabled, the
    // row stays in place but is greyed out so users can still see what
    // the parameters would look like. Toggle persists per-.inp under
    // QSettings so reopening a project remembers the choice.
    auto *modulesGroup = new QGroupBox(tr("Modules"), modTab);
    auto *modulesLay   = new QVBoxLayout(modulesGroup);

    m_module1DBox = new QCheckBox(tr("1D Hydraulics (always on)"), modulesGroup);
    m_module1DBox->setChecked(true);
    m_module1DBox->setEnabled(false);
    m_module1DBox->setToolTip(
        tr("The 1D pipe-network solver is the SWMM core and cannot be disabled."));
    modulesLay->addWidget(m_module1DBox);

    m_module2DBox = new QCheckBox(tr("2D Surface Routing"), modulesGroup);
    m_module2DBox->setObjectName(QStringLiteral("module2DBox"));
    // Module toggle is a project-level flag (QSettings-backed) — always
    // editable so the user can prepare meshes / configurations even when
    // the engine 2D solver isn't compiled in. The engine-side gate at
    // compile time only affects the parameter knobs on the 2D Surface
    // Routing page; the Mesh page + this toggle are GUI concerns.
#ifdef OPENSWMM_HAS_2D
    m_module2DBox->setToolTip(
        tr("Enable the optional 2D surface-routing module. When on, the "
           "Mesh + 2D Surface Routing pages become editable and the engine "
           "runs the 2D solver coupled to the 1D network."));
#else
    m_module2DBox->setToolTip(
        tr("Project-level 2D module flag. Mesh selection page becomes "
           "interactive when checked. The engine 2D solver itself is not "
           "compiled in this binary — rebuild with -DOPENSWMM_BUILD_2D=ON "
           "(requires SUNDIALS) for end-to-end coupled runs; mesh "
           "generation works regardless."));
#endif
    modulesLay->addWidget(m_module2DBox);
    modLay->addWidget(modulesGroup);

    // A toggle from here on is a real user intent worth writing to IGNORE_2D
    // — read() seeds the box with the signal blocked, so seeding does not
    // count (see m_module2DIntentKnown).
    connect(m_module2DBox, &QCheckBox::toggled, this, [this](bool) {
        m_module2DIntentKnown = true;
        emit gateInputsChanged();
    });

    // ── Flags ──────────────────────────────────────────────────────────
    auto *flagsGroup = new QGroupBox(tr("Options / flags"), flgTab);
    auto *flagsLay   = new QVBoxLayout(flagsGroup);

    m_allowPondingBox  = new QCheckBox(tr("Allow ponding at nodes (ALLOW_PONDING)"), flagsGroup);
    flagsLay->addWidget(m_allowPondingBox);

    flgLay->addWidget(flagsGroup);

    domLay->addStretch();
    modLay->addStretch();
    flgLay->addStretch();

    m_tabs->addTab(domTab, tr("Domains & Processes"));
    m_tabs->addTab(modTab, tr("Modules"));
    m_tabs->addTab(flgTab, tr("Flags"));

    // PLAN §1.3 reserves room below the tab bar for the matrix preview the
    // next round adds; the empty slot keeps that a layout insert rather than
    // a restructure.
    auto *matrixPreviewSlot = new QVBoxLayout;
    matrixPreviewSlot->setObjectName(QStringLiteral("matrixPreviewSlot"));
    root->addLayout(matrixPreviewSlot);
}

void ModelsPage::tagWidgets()
{
    tagOption(m_infiltrationCombo, "INFILTRATION");
    tagOption(m_allowPondingBox, "ALLOW_PONDING");
    tagOption(m_ignoreRainfallBox, "IGNORE_RAINFALL");
    tagOption(m_ignoreSnowmeltBox, "IGNORE_SNOWMELT");
    tagOption(m_ignoreGroundwaterBox, "IGNORE_GROUNDWATER");
    tagOption(m_ignoreRDIIBox, "IGNORE_RDII");
    tagOption(m_ignoreQualityBox, "IGNORE_QUALITY");
    tagOption(m_ignoreRoutingBox, "IGNORE_ROUTING");
    tagOption(m_module2DBox, "IGNORE_2D");
}

void ModelsPage::refreshTransportMatrix()
{
    if (!m_transportMatrixTable) return;
    m_matrixSyncing = true;
    SWMM_Engine e = ctx_.engine();
    SWMM_TransportMatrix m{};
    const bool ok = e && swmm_get_transport_matrix(e, &m) == SWMM_OK;
    for (int d = 0; d < SWMM_TRANSPORT_DOMAIN_COUNT; ++d) {
        for (int c = 0; c < SWMM_TRANSPORT_CLASS_COUNT; ++c) {
            auto *item = m_transportMatrixTable->item(d, c);
            if (!item) {
                item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignCenter);
                m_transportMatrixTable->setItem(d, c, item);
            }
            item->setFlags(Qt::ItemIsEnabled);
            if (!ok) { item->setText(QStringLiteral("—")); item->setToolTip(QString()); continue; }
            const SWMM_TransportCell &cell = m.cell[d][c];
            QString text, tip;
            switch (cell.state) {
            case SWMM_TRANSPORT_ENABLED:
                text = tr("on (%1)").arg(cell.count);
                tip  = cell.reason[0] ? QString::fromUtf8(cell.reason)
                                      : tr("%1 row(s) carried").arg(cell.count);
                break;
            case SWMM_TRANSPORT_DISABLED_BY_USER:
                text = tr("off");
                tip  = tr("Switched off by %1").arg(QString::fromUtf8(cell.reason));
                break;
            default:
                text = tr("n/a");
                tip  = QString::fromUtf8(cell.reason);
                break;
            }
            item->setText(text);
            item->setToolTip(tip);
            item->setForeground(cell.state == SWMM_TRANSPORT_UNAVAILABLE
                                    ? QBrush(Qt::gray) : QBrush());
            // The 2D column is editable whenever the class is available on
            // the surface at all: the checkbox IS the TRANSPORT_* key, and
            // the dialog value (not the engine's last-applied one) drives
            // its check state.
            if (d == SWMM_TRANSPORT_DOMAIN_SURFACE_2D && m_transport2DGet &&
                cell.state != SWMM_TRANSPORT_UNAVAILABLE) {
                const bool on = m_transport2DGet(c);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
                if (!on) {
                    item->setText(tr("off"));
                    item->setToolTip(tr("Switched off by %1 (pending Apply)")
                                         .arg(QLatin1String(transport2DKey(c))));
                }
            }
        }
    }
    m_matrixSyncing = false;
}

void ModelsPage::read()
{
    using SOD = SimulationOptionsDialog;
    auto selectComboByData = [](QComboBox *c, const QString &data) {
        const int idx = c->findData(data, Qt::UserRole, Qt::MatchFixedString);
        if (idx >= 0) c->setCurrentIndex(idx);
    };
    // Source every fallback from PreferencesManager so the dialog shows the
    // user-preferred default whenever the engine has no value for a key.
    const auto sim = PreferencesManager::instance()->simulationDefaults();
    const auto ynStr = [](bool v) {
        return v ? QStringLiteral("YES") : QStringLiteral("NO");
    };

    selectComboByData(m_infiltrationCombo,
                      ctx_.option("INFILTRATION", sim.infiltrationModel));

    m_allowPondingBox->setChecked(
        SOD::parseEngineBool(ctx_.option("ALLOW_PONDING", ynStr(sim.allowPonding)))
            == Qt::Checked);
    // Inverted UI: checked = process active = engine IGNORE_X is NO.
    m_ignoreRainfallBox->setChecked(   SOD::parseEngineBool(ctx_.option("IGNORE_RAINFALL",    ynStr(sim.ignoreRainfall)))    != Qt::Checked);
    m_ignoreSnowmeltBox->setChecked(   SOD::parseEngineBool(ctx_.option("IGNORE_SNOWMELT",    ynStr(sim.ignoreSnowmelt)))    != Qt::Checked);
    m_ignoreGroundwaterBox->setChecked(SOD::parseEngineBool(ctx_.option("IGNORE_GROUNDWATER", ynStr(sim.ignoreGroundwater))) != Qt::Checked);
    m_ignoreRDIIBox->setChecked(       SOD::parseEngineBool(ctx_.option("IGNORE_RDII",        ynStr(sim.ignoreRdii)))        != Qt::Checked);
    m_ignoreQualityBox->setChecked(    SOD::parseEngineBool(ctx_.option("IGNORE_QUALITY",     ynStr(sim.ignoreQuality)))     != Qt::Checked);
    m_ignoreRoutingBox->setChecked(    SOD::parseEngineBool(ctx_.option("IGNORE_ROUTING",     ynStr(sim.ignoreRouting)))     != Qt::Checked);

    // Context-sensitive availability (mirrors the legacy Delphi Analysis Options
    // form, Doptions.pas:314-332): a process toggle is disabled when the model
    // has no objects of the class it controls. The box still shows its stored
    // value; it just cannot be edited. Counts come from the engine C API.
    if (SWMM_Engine e = ctx_.engine()) {
        const int nGages     = swmm_gage_count(e);
        const int nSnowpacks = swmm_snowpack_count(e);
        const int nAquifers  = swmm_aquifer_count(e);
        const int nLinks     = swmm_link_count(e);
        const int nPolluts   = swmm_pollutant_count(e);
        const int nHydros    = swmm_hydrograph_count(e);
        m_ignoreRainfallBox   ->setEnabled(nGages > 0);
        m_ignoreSnowmeltBox   ->setEnabled(nSnowpacks > 0);
        m_ignoreGroundwaterBox->setEnabled(nAquifers > 0);
        m_ignoreRDIIBox       ->setEnabled(nGages > 0 && nHydros > 0);
        m_ignoreQualityBox    ->setEnabled(nPolluts > 0);
        m_ignoreRoutingBox    ->setEnabled(nLinks > 0);
    }

    // ---- 2D module toggle ----------------------------------------------
    // Persisted per-.inp under QSettings since the engine has no native
    // option for "module enabled" — module activation is implicit in the
    // presence of [2D_VERTICES]/[2D_TRIANGLES] sections. When there's no
    // stored preference the default is inferred from the .inp itself: a
    // file that already carries 2D sections (pre-built demos, externally
    // authored models) shows the module ON; a fresh blank .inp shows OFF.
    if (m_module2DBox && ctx_.modelLayer())
    {
        QSettings s;
        const QString key = module2DSettingsKey(ctx_.modelLayer());
        // The engine's IGNORE_2D flag is authoritative when set — it is what
        // actually keeps the 2D solver from running. QSettings/.inp sections
        // only seed the intent when the model has never been toggled.
        //
        // The engine cannot tell us whether IGNORE_2D was *set*: it reports the
        // default "NO" for a deck that never mentions the key, so a non-empty
        // read proves nothing. Intent therefore comes only from a stored
        // per-project preference, or from the user toggling the box.
        // Everything else is inference from the .inp — and materialising
        // IGNORE_2D from an inference dirtied the project on an edit-free
        // Apply.
        m_module2DIntentKnown = s.contains(key);
        const bool ignored2d =
            SOD::parseEngineBool(ctx_.option("IGNORE_2D", QStringLiteral("NO")))
                == Qt::Checked;
        const bool enabled = !ignored2d &&
            (s.contains(key)
                 ? s.value(key).toBool()
                 : inpCarries2DSections(ctx_.modelLayer()->modelFilePath()));
        QSignalBlocker blk(m_module2DBox);   // seeding is not an intent
        m_module2DBox->setChecked(enabled);
    }

    refreshTransportMatrix();
    emit gateInputsChanged();
}

int ModelsPage::write()
{
    using SOD = SimulationOptionsDialog;
    int n = 0;

    n += ctx_.writeIfChanged("INFILTRATION",
                             m_infiltrationCombo->currentData().toString());
    n += ctx_.writeIfChanged("ALLOW_PONDING",
                             SOD::engineBoolString(m_allowPondingBox->isChecked()));
    // Inverted UI: checked = active = IGNORE_X NO. Unchecked = ignore.
    n += ctx_.writeIfChanged("IGNORE_RAINFALL",
                             SOD::engineBoolString(!m_ignoreRainfallBox->isChecked()));
    n += ctx_.writeIfChanged("IGNORE_SNOWMELT",
                             SOD::engineBoolString(!m_ignoreSnowmeltBox->isChecked()));
    n += ctx_.writeIfChanged("IGNORE_GROUNDWATER",
                             SOD::engineBoolString(!m_ignoreGroundwaterBox->isChecked()));
    n += ctx_.writeIfChanged("IGNORE_RDII",
                             SOD::engineBoolString(!m_ignoreRDIIBox->isChecked()));
    n += ctx_.writeIfChanged("IGNORE_QUALITY",
                             SOD::engineBoolString(!m_ignoreQualityBox->isChecked()));
    n += ctx_.writeIfChanged("IGNORE_ROUTING",
                             SOD::engineBoolString(!m_ignoreRoutingBox->isChecked()));

    // 2D module toggle — IGNORE_2D is the engine-honored gate (unchecked ⇒
    // IGNORE_2D YES ⇒ the solver never activates, mesh or no mesh); QSettings
    // keeps the per-project UI intent for models without the key.
    //
    // Written only once the box carries a real intent (see
    // m_module2DIntentKnown): on a 1D deck that has never been toggled the
    // unchecked box merely describes the .inp, and materialising IGNORE_2D
    // from it made every no-edit Apply dirty the project.
    if (m_module2DBox && m_module2DIntentKnown) {
        n += ctx_.writeIfChanged("IGNORE_2D",
                                 SOD::engineBoolString(!m_module2DBox->isChecked()));
        if (SWMMModelLayer *layer = ctx_.modelLayer()) {
            // Same condition as the IGNORE_2D write above: persisting an
            // inferred state would make s.contains(key) true on the next open,
            // promoting the inference to an "intent" and writing IGNORE_2D
            // after all — the bug would simply reappear on the second Apply.
            QSettings s;
            s.setValue(module2DSettingsKey(layer), m_module2DBox->isChecked());
        }
    }
    return n;
}

} // namespace openswmmvis::ui
