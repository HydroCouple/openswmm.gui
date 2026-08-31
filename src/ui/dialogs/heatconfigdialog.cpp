/*!
 * \file heatconfigdialog.cpp
 * \brief G4g — the heat configuration editor. See the header for scope and
 *        the recorded timeseries-name API gap.
 *
 * \author  Caleb Buahin <caleb.buahin@gmail.com>
 * \copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * \license Apache-2.0
 */

#include "ui/dialogs/heatconfigdialog.h"

#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVector>

namespace OpenSWMMVis
{

namespace
{

struct SourceRow { int code; const char *label; };

const SourceRow kSources[] = {
    { SWMM_HEAT_SRC_RAINFALL,        QT_TRANSLATE_NOOP("HeatCfg", "Rainfall / runoff") },
    { SWMM_HEAT_SRC_DWF,             QT_TRANSLATE_NOOP("HeatCfg", "Dry weather flow") },
    { SWMM_HEAT_SRC_GW,              QT_TRANSLATE_NOOP("HeatCfg", "Groundwater") },
    { SWMM_HEAT_SRC_RDII,            QT_TRANSLATE_NOOP("HeatCfg", "RDII") },
    { SWMM_HEAT_SRC_EXTERNAL_INFLOW, QT_TRANSLATE_NOOP("HeatCfg", "External inflow") },
    { SWMM_HEAT_SRC_IFACE,           QT_TRANSLATE_NOOP("HeatCfg", "Routing interface file") },
    { SWMM_HEAT_SRC_INITIAL_STATE,   QT_TRANSLATE_NOOP("HeatCfg", "Initial network state") },
};
constexpr int kSourceCount = int(sizeof(kSources) / sizeof(kSources[0]));

/*! The H1 scope rule: only these two take per-node overrides. */
bool nodeScoped(int code)
{
    return code == SWMM_HEAT_SRC_DWF || code == SWMM_HEAT_SRC_EXTERNAL_INFLOW;
}

/*! °C spin over the parser's own accepted range ([-50, 100], refused not
 *  clamped engine-side — the spin simply cannot author a refusal). */
QDoubleSpinBox *makeTempSpin(QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(-50.0, 100.0);
    s->setDecimals(2);
    s->setValue(20.0);
    s->setSuffix(QStringLiteral(" \302\260C"));
    return s;
}

QDoubleSpinBox *makeFractionSpin(QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(0.0, 1.0);
    s->setDecimals(4);
    s->setSingleStep(0.01);
    return s;
}

struct ParamRow { int code; const char *label; };

const ParamRow kRadScalars[] = {
    { SWMM_HEAT_RAD_ALBEDO,          QT_TRANSLATE_NOOP("HeatCfg", "Water albedo Rs") },
    { SWMM_HEAT_RAD_SHADE_FACTOR,    QT_TRANSLATE_NOOP("HeatCfg", "Shade factor fs") },
    { SWMM_HEAT_RAD_SKY_VIEW,        QT_TRANSLATE_NOOP("HeatCfg", "Sky view fsky") },
    { SWMM_HEAT_RAD_EMISS_WATER,     QT_TRANSLATE_NOOP("HeatCfg", "Water emissivity") },
    { SWMM_HEAT_RAD_EMISS_LANDCOVER, QT_TRANSLATE_NOOP("HeatCfg", "Land-cover emissivity") },
    { SWMM_HEAT_RAD_ATM_EMISS_COEFF, QT_TRANSLATE_NOOP("HeatCfg", "Brunt atmospheric coeff.") },
    { SWMM_HEAT_RAD_LW_REFLECTION,   QT_TRANSLATE_NOOP("HeatCfg", "Longwave reflection RL") },
};

const ParamRow kSolarSite[] = {
    { SWMM_HEAT_SOLAR_LATITUDE,  QT_TRANSLATE_NOOP("HeatCfg", "Latitude (\302\260, +N)") },
    { SWMM_HEAT_SOLAR_LONGITUDE, QT_TRANSLATE_NOOP("HeatCfg", "Longitude (\302\260, +E)") },
    { SWMM_HEAT_SOLAR_TIMEZONE,  QT_TRANSLATE_NOOP("HeatCfg", "Timezone (h from UTC)") },
    { SWMM_HEAT_SOLAR_ELEVATION, QT_TRANSLATE_NOOP("HeatCfg", "Elevation (m)") },
};

const ParamRow kSolarAtmos[] = {
    { SWMM_HEAT_SOLAR_TURBIDITY_380, QT_TRANSLATE_NOOP("HeatCfg", "Aerosol depth at 380 nm") },
    { SWMM_HEAT_SOLAR_TURBIDITY_500, QT_TRANSLATE_NOOP("HeatCfg", "Aerosol depth at 500 nm") },
    { SWMM_HEAT_SOLAR_PRECIP_WATER,  QT_TRANSLATE_NOOP("HeatCfg", "Precipitable water (cm)") },
    { SWMM_HEAT_SOLAR_OZONE,         QT_TRANSLATE_NOOP("HeatCfg", "Ozone column (cm)") },
    { SWMM_HEAT_SOLAR_GROUND_ALBEDO, QT_TRANSLATE_NOOP("HeatCfg", "Ground albedo (land)") },
};

const ParamRow kCloudCoeffs[] = {
    { SWMM_HEAT_CLOUD_SW_ATTEN_K, QT_TRANSLATE_NOOP("HeatCfg", "Shortwave atten. k") },
    { SWMM_HEAT_CLOUD_SW_ATTEN_N, QT_TRANSLATE_NOOP("HeatCfg", "Shortwave atten. n") },
    { SWMM_HEAT_CLOUD_LW_CLOUD_K, QT_TRANSLATE_NOOP("HeatCfg", "Longwave cloud k") },
};

bool changed(double a, double b)
{
    return !qFuzzyCompare(1.0 + a, 1.0 + b);
}

} // namespace

HeatConfigDialog::HeatConfigDialog(SWMM_Engine engine, QWidget *parent)
    : QDialog(parent), m_engine(engine)
{
    setWindowTitle(tr("Heat Configuration"));
    setObjectName(QStringLiteral("heatConfigDialog"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    buildUi();
    readFromEngine();
}

void HeatConfigDialog::buildUi()
{
    auto *vlay = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("hc_tabs"));
    vlay->addWidget(tabs);

    // ── Sources ─────────────────────────────────────────────────────────
    auto *srcPage = new QWidget(tabs);
    {
        auto *lay = new QVBoxLayout(srcPage);
        auto *hint = new QLabel(
            tr("Inlet temperature of the water entering by each pathway. An "
               "unchecked source takes the 20 \302\260C default and writes "
               "no [HEAT_SOURCES] row."),
            srcPage);
        hint->setWordWrap(true);
        lay->addWidget(hint);

        m_sourceTable = new QTableWidget(kSourceCount, 3, srcPage);
        m_sourceTable->setObjectName(QStringLiteral("hc_sourceTable"));
        m_sourceTable->setHorizontalHeaderLabels(
            { tr("Source"), tr("Set"), tr("Temperature") });
        m_sourceTable->verticalHeader()->setVisible(false);
        m_sourceTable->horizontalHeader()->setStretchLastSection(true);
        m_sourceTable->setSelectionMode(QAbstractItemView::NoSelection);
        for (int r = 0; r < kSourceCount; ++r) {
            auto *nameItem = new QTableWidgetItem(
                QCoreApplication::translate("HeatCfg", kSources[r].label));
            nameItem->setFlags(Qt::ItemIsEnabled);
            nameItem->setData(Qt::UserRole, kSources[r].code);
            m_sourceTable->setItem(r, 0, nameItem);

            auto *check = new QCheckBox(m_sourceTable);
            check->setObjectName(QStringLiteral("hc_srcCheck_%1")
                                     .arg(kSources[r].code));
            m_sourceTable->setCellWidget(r, 1, check);

            auto *spin = makeTempSpin(m_sourceTable);
            spin->setObjectName(QStringLiteral("hc_srcSpin_%1")
                                    .arg(kSources[r].code));
            spin->setEnabled(false);
            connect(check, &QCheckBox::toggled, spin,
                    &QWidget::setEnabled);
            m_sourceTable->setCellWidget(r, 2, spin);
        }
        lay->addWidget(m_sourceTable);

        lay->addWidget(new QLabel(
            tr("Per-node overrides (dry weather flow and external inflow "
               "only):"),
            srcPage));
        m_overrideTable = new QTableWidget(0, 3, srcPage);
        m_overrideTable->setObjectName(QStringLiteral("hc_overrideTable"));
        m_overrideTable->setHorizontalHeaderLabels(
            { tr("Source"), tr("Node"), tr("Temperature") });
        m_overrideTable->verticalHeader()->setVisible(false);
        m_overrideTable->horizontalHeader()->setStretchLastSection(true);
        lay->addWidget(m_overrideTable);

        auto *btns = new QHBoxLayout;
        auto *add = new QPushButton(tr("Add"), srcPage);
        add->setObjectName(QStringLiteral("hc_addOverride"));
        auto *rem = new QPushButton(tr("Remove"), srcPage);
        rem->setObjectName(QStringLiteral("hc_removeOverride"));
        connect(add, &QPushButton::clicked, this,
                &HeatConfigDialog::onAddOverride);
        connect(rem, &QPushButton::clicked, this,
                &HeatConfigDialog::onRemoveOverride);
        btns->addWidget(add);
        btns->addWidget(rem);
        btns->addStretch(1);
        lay->addLayout(btns);
    }
    tabs->addTab(srcPage, tr("Sources"));

    // ── Fluxes ──────────────────────────────────────────────────────────
    auto *fluxPage = new QWidget(tabs);
    {
        auto *lay = new QVBoxLayout(fluxPage);
        const char *labels[3] = {
            QT_TRANSLATE_NOOP("HeatCfg",
                              "Surface exchange (latent + sensible)"),
            QT_TRANSLATE_NOOP("HeatCfg",
                              "Radiative exchange (shortwave + longwave)"),
            QT_TRANSLATE_NOOP("HeatCfg", "LID layer conduction"),
        };
        for (int m = 0; m < 3; ++m) {
            m_modules[m] = new QCheckBox(
                QCoreApplication::translate("HeatCfg", labels[m]), fluxPage);
            m_modules[m]->setObjectName(QStringLiteral("hc_module_%1").arg(m));
            lay->addWidget(m_modules[m]);
        }
        lay->addStretch(1);
    }
    tabs->addTab(fluxPage, tr("Fluxes"));

    // ── Radiative ───────────────────────────────────────────────────────
    auto *radPage = new QWidget(tabs);
    {
        auto *lay = new QVBoxLayout(radPage);
        auto *swBox = new QGroupBox(tr("Incoming shortwave"), radPage);
        auto *swLay = new QFormLayout(swBox);

        m_swConstant = new QRadioButton(tr("Constant"), swBox);
        m_swConstant->setObjectName(QStringLiteral("hc_swConstant"));
        m_swConstSpin = new QDoubleSpinBox(swBox);
        m_swConstSpin->setObjectName(QStringLiteral("hc_swConstSpin"));
        m_swConstSpin->setRange(0.0, 1500.0);
        m_swConstSpin->setDecimals(2);
        m_swConstSpin->setSuffix(QStringLiteral(" W/m\302\262"));
        swLay->addRow(m_swConstant, m_swConstSpin);

        m_swTimeseries = new QRadioButton(tr("Timeseries"), swBox);
        m_swTimeseries->setObjectName(QStringLiteral("hc_swTimeseries"));
        m_swTsCombo = new QComboBox(swBox);
        m_swTsCombo->setObjectName(QStringLiteral("hc_swTsCombo"));
        swLay->addRow(m_swTimeseries, m_swTsCombo);

        m_swComputed = new QRadioButton(
            tr("Computed (solar position + Bird clear sky)"), swBox);
        m_swComputed->setObjectName(QStringLiteral("hc_swComputed"));
        swLay->addRow(m_swComputed);
        lay->addWidget(swBox);

        auto *scalars = new QGroupBox(tr("Radiative parameters"), radPage);
        auto *form = new QFormLayout(scalars);
        for (const auto &p : kRadScalars) {
            auto *spin = makeFractionSpin(scalars);
            spin->setObjectName(QStringLiteral("hc_rad_%1").arg(p.code));
            m_radSpin[p.code] = spin;
            form->addRow(QCoreApplication::translate("HeatCfg", p.label),
                         spin);
        }
        lay->addWidget(scalars);
        lay->addStretch(1);
    }
    tabs->addTab(radPage, tr("Radiative"));

    // ── Solar ───────────────────────────────────────────────────────────
    auto *solarPage = new QWidget(tabs);
    {
        auto *lay = new QVBoxLayout(solarPage);
        auto *site = new QGroupBox(tr("Site (needed for COMPUTED shortwave)"),
                                   solarPage);
        auto *sform = new QFormLayout(site);
        for (const auto &p : kSolarSite) {
            auto *spin = new QDoubleSpinBox(site);
            spin->setObjectName(QStringLiteral("hc_solar_%1").arg(p.code));
            spin->setDecimals(4);
            switch (p.code) {
            case SWMM_HEAT_SOLAR_LATITUDE:  spin->setRange(-90.0, 90.0); break;
            case SWMM_HEAT_SOLAR_LONGITUDE: spin->setRange(-180.0, 180.0); break;
            case SWMM_HEAT_SOLAR_TIMEZONE:  spin->setRange(-12.0, 14.0); break;
            case SWMM_HEAT_SOLAR_ELEVATION: spin->setRange(-500.0, 9000.0); break;
            default: break;
            }
            m_solarSpin[p.code] = spin;
            sform->addRow(QCoreApplication::translate("HeatCfg", p.label),
                          spin);
        }
        lay->addWidget(site);

        auto *atm = new QGroupBox(tr("Atmosphere (Bird clear-sky model)"),
                                  solarPage);
        auto *aform = new QFormLayout(atm);
        for (const auto &p : kSolarAtmos) {
            auto *spin = new QDoubleSpinBox(atm);
            spin->setObjectName(QStringLiteral("hc_solar_%1").arg(p.code));
            spin->setDecimals(4);
            spin->setRange(0.0,
                           p.code == SWMM_HEAT_SOLAR_GROUND_ALBEDO ? 1.0
                                                                   : 15.0);
            spin->setSingleStep(0.01);
            m_solarSpin[p.code] = spin;
            aform->addRow(QCoreApplication::translate("HeatCfg", p.label),
                          spin);
        }
        lay->addWidget(atm);
        lay->addStretch(1);
    }
    tabs->addTab(solarPage, tr("Solar"));

    // ── Cloud ───────────────────────────────────────────────────────────
    auto *cloudPage = new QWidget(tabs);
    {
        auto *lay = new QVBoxLayout(cloudPage);
        m_cloudEnable = new QCheckBox(tr("Cloud cover configured"),
                                      cloudPage);
        m_cloudEnable->setObjectName(QStringLiteral("hc_cloudEnable"));
        lay->addWidget(m_cloudEnable);

        auto *form = new QFormLayout;
        auto *frac = makeFractionSpin(cloudPage);
        frac->setObjectName(
            QStringLiteral("hc_cloud_%1").arg(SWMM_HEAT_CLOUD_FRACTION));
        m_cloudSpin[SWMM_HEAT_CLOUD_FRACTION] = frac;
        form->addRow(tr("Fraction [0..1]"), frac);
        for (const auto &p : kCloudCoeffs) {
            auto *spin = new QDoubleSpinBox(cloudPage);
            spin->setObjectName(QStringLiteral("hc_cloud_%1").arg(p.code));
            spin->setDecimals(4);
            spin->setRange(0.0, 10.0);
            spin->setSingleStep(0.01);
            m_cloudSpin[p.code] = spin;
            form->addRow(QCoreApplication::translate("HeatCfg", p.label),
                         spin);
        }
        m_cloudTsCombo = new QComboBox(cloudPage);
        m_cloudTsCombo->setObjectName(QStringLiteral("hc_cloudTsCombo"));
        form->addRow(tr("Fraction timeseries"), m_cloudTsCombo);
        lay->addLayout(form);
        lay->addStretch(1);

        // Enabled-state follows the check box; hydration sets the box.
        auto enableAll = [this](bool on) {
            for (auto *s : m_cloudSpin)
                if (s) s->setEnabled(on);
            m_cloudTsCombo->setEnabled(on);
        };
        connect(m_cloudEnable, &QCheckBox::toggled, this, enableAll);
        enableAll(false);
    }
    tabs->addTab(cloudPage, tr("Cloud"));

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this,
            &HeatConfigDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    vlay->addWidget(bb);
}

void HeatConfigDialog::onAddOverride()
{
    const int r = m_overrideTable->rowCount();
    m_overrideTable->insertRow(r);

    auto *srcCombo = new QComboBox(m_overrideTable);
    for (const auto &s : kSources)
        if (nodeScoped(s.code))
            srcCombo->addItem(
                QCoreApplication::translate("HeatCfg", s.label), s.code);
    m_overrideTable->setCellWidget(r, 0, srcCombo);

    auto *nodeCombo = new QComboBox(m_overrideTable);
    const int n = m_engine ? swmm_node_count(m_engine) : 0;
    for (int i = 0; i < n; ++i) {
        const char *id = swmm_node_id(m_engine, i);
        nodeCombo->addItem(id ? QString::fromUtf8(id)
                              : QStringLiteral("#%1").arg(i), i);
    }
    m_overrideTable->setCellWidget(r, 1, nodeCombo);

    m_overrideTable->setCellWidget(r, 2, makeTempSpin(m_overrideTable));
}

void HeatConfigDialog::onRemoveOverride()
{
    const int r = m_overrideTable->currentRow();
    if (r >= 0) m_overrideTable->removeRow(r);
}

void HeatConfigDialog::readFromEngine()
{
    if (!m_engine) return;

    // Timeseries combos: the model's table names, behind a "keep" row —
    // the engine has no getter for the bound series NAME (recorded gap),
    // so selecting a name REBINDS and the placeholder is the no-op.
    const QString keep = tr("(keep current series)");
    m_swTsCombo->addItem(keep, QString());
    m_cloudTsCombo->addItem(keep, QString());
    const int nt = swmm_table_count(m_engine);
    for (int i = 0; i < nt; ++i) {
        const char *id = swmm_table_id(m_engine, i);
        if (!id) continue;
        m_swTsCombo->addItem(QString::fromUtf8(id), QString::fromUtf8(id));
        m_cloudTsCombo->addItem(QString::fromUtf8(id),
                                QString::fromUtf8(id));
    }

    // Sources
    for (int r = 0; r < kSourceCount; ++r) {
        const int code = kSources[r].code;
        int configured = 0;
        double t = 20.0;
        swmm_heat_get_source_configured(m_engine, code, &configured);
        swmm_heat_get_source_temp(m_engine, code, &t);
        if (auto *c = qobject_cast<QCheckBox *>(
                m_sourceTable->cellWidget(r, 1)))
            c->setChecked(configured != 0);
        if (auto *s = qobject_cast<QDoubleSpinBox *>(
                m_sourceTable->cellWidget(r, 2)))
            s->setValue(t);
    }
    int count = 0;
    if (swmm_heat_node_override_count(m_engine, &count) == SWMM_OK) {
        for (int i = 0; i < count; ++i) {
            int src = 0, node = 0;
            double t = 20.0;
            if (swmm_heat_get_node_override(m_engine, i, &src, &node, &t)
                    != SWMM_OK)
                continue;
            onAddOverride();
            const int r = m_overrideTable->rowCount() - 1;
            if (auto *c = qobject_cast<QComboBox *>(
                    m_overrideTable->cellWidget(r, 0))) {
                const int idx = c->findData(src);
                if (idx >= 0) c->setCurrentIndex(idx);
            }
            if (auto *c = qobject_cast<QComboBox *>(
                    m_overrideTable->cellWidget(r, 1))) {
                const int idx = c->findData(node);
                if (idx >= 0) c->setCurrentIndex(idx);
            }
            if (auto *s = qobject_cast<QDoubleSpinBox *>(
                    m_overrideTable->cellWidget(r, 2)))
                s->setValue(t);
        }
    }

    // Fluxes
    for (int m = 0; m < 3; ++m) {
        int on = 0;
        if (swmm_heat_get_module(m_engine, m, &on) == SWMM_OK)
            m_modules[m]->setChecked(on != 0);
    }

    // Radiative
    int mode = SWMM_HEAT_SW_CONSTANT;
    swmm_heat_get_shortwave_mode(m_engine, &mode);
    m_swConstant->setChecked(mode == SWMM_HEAT_SW_CONSTANT);
    m_swTimeseries->setChecked(mode == SWMM_HEAT_SW_TIMESERIES);
    m_swComputed->setChecked(mode == SWMM_HEAT_SW_COMPUTED);
    double v = 0.0;
    if (swmm_heat_get_radiative(m_engine, SWMM_HEAT_RAD_SHORTWAVE, &v)
            == SWMM_OK)
        m_swConstSpin->setValue(v);
    for (const auto &p : kRadScalars)
        if (swmm_heat_get_radiative(m_engine, p.code, &v) == SWMM_OK)
            m_radSpin[p.code]->setValue(v);

    // COMPUTED needs an explicit site — gate the radio, don't discover the
    // refusal after the fact (the header's own guidance).
    int sited = 0;
    swmm_heat_get_solar_sited(m_engine, &sited);
    if (!sited && mode != SWMM_HEAT_SW_COMPUTED) {
        m_swComputed->setEnabled(false);
        m_swComputed->setToolTip(
            tr("Set latitude and longitude on the Solar tab first."));
    }

    // Solar
    for (int p = 0; p < 9; ++p)
        if (m_solarSpin[p] &&
            swmm_heat_get_solar(m_engine, p, &v) == SWMM_OK)
            m_solarSpin[p]->setValue(v);

    // Cloud
    int cfgd = 0;
    swmm_heat_get_cloud_configured(m_engine, &cfgd);
    m_cloudEnable->setChecked(cfgd != 0);
    for (int p = 0; p < 4; ++p)
        if (m_cloudSpin[p] &&
            swmm_heat_get_cloud(m_engine, p, &v) == SWMM_OK)
            m_cloudSpin[p]->setValue(v);
}

int HeatConfigDialog::writeSources()
{
    int writes = 0;
    for (int r = 0; r < kSourceCount; ++r) {
        const int code = kSources[r].code;
        auto *check = qobject_cast<QCheckBox *>(
            m_sourceTable->cellWidget(r, 1));
        auto *spin = qobject_cast<QDoubleSpinBox *>(
            m_sourceTable->cellWidget(r, 2));
        if (!check || !spin) continue;
        int configured = 0;
        double current = 20.0;
        swmm_heat_get_source_configured(m_engine, code, &configured);
        swmm_heat_get_source_temp(m_engine, code, &current);
        if (check->isChecked()) {
            if (!configured || changed(current, spin->value())) {
                if (swmm_heat_set_source_temp(m_engine, code, spin->value())
                        == SWMM_OK)
                    ++writes;
            }
        } else if (configured) {
            if (swmm_heat_clear_source_temp(m_engine, code) == SWMM_OK)
                ++writes;
        }
    }

    // Overrides: engine keeps a keyed list; remove rows the table dropped,
    // then set (add-or-update) every table row that changed.
    int count = 0;
    if (swmm_heat_node_override_count(m_engine, &count) == SWMM_OK) {
        QVector<QPair<int, int>> tableKeys;
        for (int r = 0; r < m_overrideTable->rowCount(); ++r) {
            auto *sc = qobject_cast<QComboBox *>(
                m_overrideTable->cellWidget(r, 0));
            auto *nc = qobject_cast<QComboBox *>(
                m_overrideTable->cellWidget(r, 1));
            if (sc && nc)
                tableKeys.append({ sc->currentData().toInt(),
                                   nc->currentData().toInt() });
        }
        // Removal is BY INDEX and later rows shift down — walk backwards.
        for (int i = count - 1; i >= 0; --i) {
            int src = 0, node = 0;
            if (swmm_heat_get_node_override(m_engine, i, &src, &node,
                                            nullptr) != SWMM_OK)
                continue;
            if (!tableKeys.contains({ src, node }) &&
                swmm_heat_remove_node_override(m_engine, i) == SWMM_OK)
                ++writes;
        }
    }
    for (int r = 0; r < m_overrideTable->rowCount(); ++r) {
        auto *sc = qobject_cast<QComboBox *>(
            m_overrideTable->cellWidget(r, 0));
        auto *nc = qobject_cast<QComboBox *>(
            m_overrideTable->cellWidget(r, 1));
        auto *ts = qobject_cast<QDoubleSpinBox *>(
            m_overrideTable->cellWidget(r, 2));
        if (!sc || !nc || !ts) continue;
        const int src = sc->currentData().toInt();
        const int node = nc->currentData().toInt();
        double cur = 0.0;
        bool found = false;
        int n2 = 0;
        if (swmm_heat_node_override_count(m_engine, &n2) == SWMM_OK) {
            for (int i = 0; i < n2; ++i) {
                int s2 = 0, nd2 = 0;
                double t2 = 0.0;
                if (swmm_heat_get_node_override(m_engine, i, &s2, &nd2, &t2)
                        == SWMM_OK && s2 == src && nd2 == node) {
                    found = true;
                    cur = t2;
                    break;
                }
            }
        }
        if (found && !changed(cur, ts->value())) continue;
        if (swmm_heat_set_node_override(m_engine, src, node, ts->value())
                == SWMM_OK)
            ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeModules()
{
    int writes = 0;
    for (int m = 0; m < 3; ++m) {
        int on = 0;
        if (swmm_heat_get_module(m_engine, m, &on) != SWMM_OK) continue;
        const int want = m_modules[m]->isChecked() ? 1 : 0;
        if (want != on && swmm_heat_set_module(m_engine, m, want) == SWMM_OK)
            ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeSolar()
{
    int writes = 0;
    double v = 0.0;
    for (int p = 0; p < 9; ++p) {
        if (!m_solarSpin[p]) continue;
        if (swmm_heat_get_solar(m_engine, p, &v) != SWMM_OK) continue;
        if (!changed(v, m_solarSpin[p]->value())) continue;
        if (swmm_heat_set_solar(m_engine, p, m_solarSpin[p]->value())
                == SWMM_OK)
            ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeRadiative()
{
    int writes = 0;
    double v = 0.0;

    // Mode first: a CONSTANT value only writes in CONSTANT mode (the API
    // refuses it elsewhere), and TIMESERIES binds via its own setter.
    int mode = SWMM_HEAT_SW_CONSTANT;
    swmm_heat_get_shortwave_mode(m_engine, &mode);
    const QString pickedTs = m_swTsCombo->currentData().toString();

    if (m_swTimeseries->isChecked()) {
        if (!pickedTs.isEmpty()) {
            if (swmm_heat_set_shortwave_timeseries(
                    m_engine, pickedTs.toUtf8().constData()) == SWMM_OK)
                ++writes;
        }
        // No pick + already TIMESERIES = keep; no pick + other mode = the
        // dialog cannot bind an unknown series, leave the mode alone.
    } else if (m_swComputed->isChecked()) {
        if (mode != SWMM_HEAT_SW_COMPUTED &&
            swmm_heat_set_shortwave_mode(m_engine, SWMM_HEAT_SW_COMPUTED)
                == SWMM_OK)
            ++writes;
    } else {
        if (mode != SWMM_HEAT_SW_CONSTANT &&
            swmm_heat_set_shortwave_mode(m_engine, SWMM_HEAT_SW_CONSTANT)
                == SWMM_OK)
            ++writes;
        if (swmm_heat_get_radiative(m_engine, SWMM_HEAT_RAD_SHORTWAVE, &v)
                == SWMM_OK &&
            changed(v, m_swConstSpin->value()) &&
            swmm_heat_set_radiative(m_engine, SWMM_HEAT_RAD_SHORTWAVE,
                                    m_swConstSpin->value()) == SWMM_OK)
            ++writes;
    }

    for (const auto &p : kRadScalars) {
        if (swmm_heat_get_radiative(m_engine, p.code, &v) != SWMM_OK)
            continue;
        if (!changed(v, m_radSpin[p.code]->value())) continue;
        if (swmm_heat_set_radiative(m_engine, p.code,
                                    m_radSpin[p.code]->value()) == SWMM_OK)
            ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeCloud()
{
    int writes = 0;
    int cfgd = 0;
    swmm_heat_get_cloud_configured(m_engine, &cfgd);

    if (!m_cloudEnable->isChecked()) {
        if (cfgd && swmm_heat_clear_cloud(m_engine) == SWMM_OK) ++writes;
        return writes;
    }

    double v = 0.0;
    for (int p = 0; p < 4; ++p) {
        if (!m_cloudSpin[p]) continue;
        if (swmm_heat_get_cloud(m_engine, p, &v) != SWMM_OK) continue;
        // Writing any parameter marks cloud configured, so an enable with
        // untouched values still needs ONE write to take effect.
        const bool mustTouch = !cfgd && p == SWMM_HEAT_CLOUD_FRACTION;
        if (!mustTouch && !changed(v, m_cloudSpin[p]->value())) continue;
        if (swmm_heat_set_cloud(m_engine, p, m_cloudSpin[p]->value())
                == SWMM_OK)
            ++writes;
    }
    const QString pickedTs = m_cloudTsCombo->currentData().toString();
    if (!pickedTs.isEmpty() &&
        swmm_heat_set_cloud_timeseries(m_engine,
                                       pickedTs.toUtf8().constData())
            == SWMM_OK)
        ++writes;
    return writes;
}

int HeatConfigDialog::writeToEngine()
{
    if (!m_engine) return 0;
    // Solar before radiative: COMPUTED requires the site to be set, and the
    // user may have entered both in one visit.
    int writes = 0;
    writes += writeSolar();
    writes += writeSources();
    writes += writeModules();
    writes += writeRadiative();
    writes += writeCloud();
    return writes;
}

void HeatConfigDialog::onAccept()
{
    m_lastWriteCount = writeToEngine();
    m_wroteAnyChanges = m_lastWriteCount > 0;
    accept();
}

} // namespace OpenSWMMVis
