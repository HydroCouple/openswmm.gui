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
#include "ui/precisenumericvalue.h"

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
#include <QHash>
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

void HeatConfigDialog::invalidateEngine()
{
    m_engine = nullptr;
    setEnabled(false);
    reject();
}

void HeatConfigDialog::setCurrentTab(int idx)
{
    if (m_tabs && idx >= 0 && idx < m_tabs->count())
        m_tabs->setCurrentIndex(idx);
}

void HeatConfigDialog::buildUi()
{
    auto *vlay = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("hc_tabs"));
    m_tabs = tabs;
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
        m_sourceTable->setAccessibleName(tr("Global source temperatures"));
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
            check->setAccessibleName(tr("%1, configured").arg(nameItem->text()));
            spin->setAccessibleName(tr("%1, temperature").arg(nameItem->text()));
            spin->setAccessibleDescription(tr("Temperature in degrees Celsius."));
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
        m_overrideTable->setAccessibleName(tr("Node temperature overrides"));
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
        add->setAutoDefault(false);
        rem->setAutoDefault(false);
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

    m_validationError = new QLabel(this);
    m_validationError->setObjectName(QStringLiteral("hc_validationError"));
    m_validationError->setAccessibleName(tr("Heat configuration error"));
    m_validationError->setTextFormat(Qt::PlainText);
    m_validationError->setWordWrap(true);
    m_validationError->hide();
    vlay->addWidget(m_validationError);
    // Form labels provide buddies; explicit names also cover unlabeled
    // radio-associated fields and platform accessibility implementations.
    for (auto *label : findChildren<QLabel *>()) {
        if (auto *field = label->buddy()) field->setAccessibleName(label->text().remove(QLatin1Char('&')));
    }
    m_swConstSpin->setAccessibleName(tr("Constant shortwave radiation"));
    m_swTsCombo->setAccessibleName(tr("Shortwave time series"));
    m_cloudTsCombo->setAccessibleName(tr("Cloud fraction time series"));

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
    refreshOverrideAccessibility();
}

void HeatConfigDialog::onRemoveOverride()
{
    const int r = m_overrideTable->currentRow();
    if (r >= 0) m_overrideTable->removeRow(r);
    refreshOverrideAccessibility();
}

void HeatConfigDialog::readFromEngine()
{
    if (!m_engine) return;

    // Timeseries combos: the model's table names, DISPLAYING the bound
    // series by name (swmm_heat_get_*_timeseries — the getters that closed
    // G4g's recorded gap). The placeholder row exists only while nothing is
    // bound; with a binding the bound name is preselected, and the OK path
    // writes only when the selection moved off the hydrated value.
    char tsbuf[256] = {0};
    if (swmm_heat_get_shortwave_timeseries(m_engine, tsbuf, sizeof tsbuf)
            == SWMM_OK)
        m_swTsInitial = QString::fromUtf8(tsbuf);
    tsbuf[0] = '\0';
    if (swmm_heat_get_cloud_timeseries(m_engine, tsbuf, sizeof tsbuf)
            == SWMM_OK)
        m_cloudTsInitial = QString::fromUtf8(tsbuf);

    const QString none = tr("(none bound)");
    if (m_swTsInitial.isEmpty()) m_swTsCombo->addItem(none, QString());
    if (m_cloudTsInitial.isEmpty()) m_cloudTsCombo->addItem(none, QString());
    const int nt = swmm_table_count(m_engine);
    for (int i = 0; i < nt; ++i) {
        int type = -1;
        if (swmm_table_get_type(m_engine, i, &type) != SWMM_OK || type != 0) continue;
        const char *id = swmm_table_id(m_engine, i);
        if (!id) continue;
        m_swTsCombo->addItem(QString::fromUtf8(id), QString::fromUtf8(id));
        m_cloudTsCombo->addItem(QString::fromUtf8(id),
                                QString::fromUtf8(id));
    }
    if (!m_swTsInitial.isEmpty()) {
        const int at = m_swTsCombo->findData(m_swTsInitial);
        if (at >= 0) m_swTsCombo->setCurrentIndex(at);
    }
    if (!m_cloudTsInitial.isEmpty()) {
        const int at = m_cloudTsCombo->findData(m_cloudTsInitial);
        if (at >= 0) m_cloudTsCombo->setCurrentIndex(at);
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
            OpenSWMM::Ui::setHydratedValue(s, t);
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
                OpenSWMM::Ui::setHydratedValue(s, t);
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
        OpenSWMM::Ui::setHydratedValue(m_swConstSpin, v);
    for (const auto &p : kRadScalars)
        if (swmm_heat_get_radiative(m_engine, p.code, &v) == SWMM_OK)
            OpenSWMM::Ui::setHydratedValue(m_radSpin[p.code], v);

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
            OpenSWMM::Ui::setHydratedValue(m_solarSpin[p], v);

    // Cloud
    int cfgd = 0;
    swmm_heat_get_cloud_configured(m_engine, &cfgd);
    m_cloudEnable->setChecked(cfgd != 0);
    for (int p = 0; p < 4; ++p)
        if (m_cloudSpin[p] &&
            swmm_heat_get_cloud(m_engine, p, &v) == SWMM_OK)
            OpenSWMM::Ui::setHydratedValue(m_cloudSpin[p], v);
}

void HeatConfigDialog::refreshOverrideAccessibility()
{
    for (int row = 0; row < m_overrideTable->rowCount(); ++row)
        for (int col = 0; col < m_overrideTable->columnCount(); ++col) {
            auto *field = m_overrideTable->cellWidget(row, col);
            if (!field) continue;
            const QString label = tr("Override row %1, %2").arg(row + 1)
                .arg(m_overrideTable->horizontalHeaderItem(col)->text());
            field->setAccessibleName(label);
            field->setAccessibleDescription(tr("%1. %2").arg(label, col == 2
                ? tr("Temperature in degrees Celsius.") : tr("Each source and node pair must be unique.")));
        }
}

bool HeatConfigDialog::reportFailure(QWidget *field, const QString &message)
{
    m_validationError->setText(message);
    m_validationError->show();
    if (field) {
        for (int tab = 0; tab < m_tabs->count(); ++tab)
            if (m_tabs->widget(tab)->isAncestorOf(field)) { m_tabs->setCurrentIndex(tab); break; }
        for (auto *table : {m_sourceTable, m_overrideTable})
            for (int row = 0; row < table->rowCount(); ++row)
                for (int col = 0; col < table->columnCount(); ++col)
                    if (table->cellWidget(row, col) == field) {
                        table->setCurrentCell(row, col);
                        table->scrollTo(table->model()->index(row, col));
                    }
        field->setAccessibleDescription(tr("%1. %2").arg(field->accessibleName(), message));
        field->setFocus(Qt::OtherFocusReason);
    }
    return false;
}

bool HeatConfigDialog::checked(int status, QWidget *field, const QString &action)
{
    if (status == SWMM_OK) return true;
    m_writeFailed = true;
    return reportFailure(field, tr("%1 failed (engine error %2). Earlier successful changes remain applied; correct the draft and try again.")
        .arg(action).arg(status));
}

bool HeatConfigDialog::validateDraft()
{
    m_validationError->clear();
    m_validationError->hide();
    refreshOverrideAccessibility();
    if (!m_engine) return reportFailure(nullptr, tr("Open a model before editing heat configuration."));
    QHash<QPair<int, int>, int> seen;
    for (int row = 0; row < m_overrideTable->rowCount(); ++row) {
        auto *source = qobject_cast<QComboBox *>(m_overrideTable->cellWidget(row, 0));
        auto *node = qobject_cast<QComboBox *>(m_overrideTable->cellWidget(row, 1));
        bool sourceOk = false, nodeOk = false;
        const int sourceCode = source ? source->currentData().toInt(&sourceOk) : -1;
        const int nodeIndex = node ? node->currentData().toInt(&nodeOk) : -1;
        if (!sourceOk || !nodeScoped(sourceCode))
            return reportFailure(source, tr("Row %1: select dry weather flow or external inflow.").arg(row + 1));
        const char *id = nodeOk && nodeIndex >= 0 && nodeIndex < swmm_node_count(m_engine)
            ? swmm_node_id(m_engine, nodeIndex) : nullptr;
        if (!id || node->currentText() != QString::fromUtf8(id))
            return reportFailure(node, tr("Row %1: select an available node. Reopen this dialog if the model's nodes changed.").arg(row + 1));
        const QPair<int, int> key{sourceCode, nodeIndex};
        if (seen.contains(key))
            return reportFailure(source, tr("Row %1 duplicates row %2. Choose a different source or node.").arg(row + 1).arg(seen.value(key) + 1));
        seen.insert(key, row);
    }
    auto validSeries = [this](QComboBox *combo, bool required) {
        const QString name = combo->currentData().toString();
        if (name.isEmpty() && !required) return true;
        const int index = swmm_table_index(m_engine, name.toUtf8().constData());
        int type = -1;
        if (name.isEmpty() || index < 0 || swmm_table_get_type(m_engine, index, &type) != SWMM_OK || type != 0)
            return reportFailure(combo, tr("%1: select an available time series.").arg(combo->accessibleName()));
        return true;
    };
    if (m_swTimeseries->isChecked() && !validSeries(m_swTsCombo, true)) return false;
    if (m_cloudEnable->isChecked() && !validSeries(m_cloudTsCombo, false)) return false;
    return true;
}

int HeatConfigDialog::writeSources()
{
    int writes = 0;
    for (int row = 0; row < kSourceCount; ++row) {
        const int code = kSources[row].code;
        auto *check = qobject_cast<QCheckBox *>(m_sourceTable->cellWidget(row, 1));
        auto *spin = qobject_cast<QDoubleSpinBox *>(m_sourceTable->cellWidget(row, 2));
        int configured = 0;
        double current = 20;
        const QString name = m_sourceTable->item(row, 0)->text();
        if (!checked(swmm_heat_get_source_configured(m_engine, code, &configured), check, tr("Reading %1").arg(name))
            || !checked(swmm_heat_get_source_temp(m_engine, code, &current), spin, tr("Reading %1 temperature").arg(name))) return writes;
        const double desired = OpenSWMM::Ui::preciseValue(spin);
        if (check->isChecked() && (!configured || changed(current, desired))) {
            if (!checked(swmm_heat_set_source_temp(m_engine, code, desired), spin, tr("Saving %1 temperature").arg(name))) return writes;
            ++writes;
        } else if (!check->isChecked() && configured) {
            if (!checked(swmm_heat_clear_source_temp(m_engine, code), check, tr("Clearing %1 temperature").arg(name))) return writes;
            ++writes;
        }
    }
    int count = 0;
    if (!checked(swmm_heat_node_override_count(m_engine, &count), m_overrideTable, tr("Reading overrides"))) return writes;
    QVector<QPair<int, int>> keys;
    for (int row = 0; row < m_overrideTable->rowCount(); ++row)
        keys.append({qobject_cast<QComboBox *>(m_overrideTable->cellWidget(row, 0))->currentData().toInt(),
                     qobject_cast<QComboBox *>(m_overrideTable->cellWidget(row, 1))->currentData().toInt()});
    for (int i = count - 1; i >= 0; --i) {
        int source = 0, node = 0;
        if (!checked(swmm_heat_get_node_override(m_engine, i, &source, &node, nullptr), m_overrideTable, tr("Reading override %1").arg(i + 1))) return writes;
        if (keys.contains({source, node})) continue;
        if (!checked(swmm_heat_remove_node_override(m_engine, i), m_overrideTable, tr("Removing override %1").arg(i + 1))) return writes;
        ++writes;
    }
    for (int row = 0; row < m_overrideTable->rowCount(); ++row) {
        auto *spin = qobject_cast<QDoubleSpinBox *>(m_overrideTable->cellWidget(row, 2));
        const auto key = keys.at(row);
        bool found = false;
        double current = 0;
        if (!checked(swmm_heat_node_override_count(m_engine, &count), spin, tr("Reading overrides"))) return writes;
        for (int i = 0; i < count; ++i) {
            int source = 0, node = 0;
            double value = 0;
            if (!checked(swmm_heat_get_node_override(m_engine, i, &source, &node, &value), spin, tr("Reading override %1").arg(row + 1))) return writes;
            if (source == key.first && node == key.second) { found = true; current = value; break; }
        }
        const double desired = OpenSWMM::Ui::preciseValue(spin);
        if (found && !changed(current, desired)) continue;
        if (!checked(swmm_heat_set_node_override(m_engine, key.first, key.second, desired), spin, tr("Saving override %1 temperature").arg(row + 1))) return writes;
        ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeModules()
{
    int writes = 0;
    for (int module = 0; module < 3; ++module) {
        int current = 0;
        auto *field = m_modules[module];
        if (!checked(swmm_heat_get_module(m_engine, module, &current), field, tr("Reading %1").arg(field->text()))) return writes;
        const int desired = field->isChecked() ? 1 : 0;
        if (desired == current) continue;
        if (!checked(swmm_heat_set_module(m_engine, module, desired), field, tr("Saving %1").arg(field->text()))) return writes;
        ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeSolar()
{
    int writes = 0;
    for (int param = 0; param < 9; ++param) {
        auto *spin = m_solarSpin[param];
        if (!spin) continue;
        double current = 0;
        if (!checked(swmm_heat_get_solar(m_engine, param, &current), spin, tr("Reading %1").arg(spin->accessibleName()))) return writes;
        const double desired = OpenSWMM::Ui::preciseValue(spin);
        if (!changed(current, desired)) continue;
        if (!checked(swmm_heat_set_solar(m_engine, param, desired), spin, tr("Saving %1").arg(spin->accessibleName()))) return writes;
        ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeRadiative()
{
    int writes = 0;
    int mode = SWMM_HEAT_SW_CONSTANT;
    if (!checked(swmm_heat_get_shortwave_mode(m_engine, &mode), m_swConstant, tr("Reading shortwave mode"))) return writes;
    if (m_swTimeseries->isChecked()) {
        char name[256] = {};
        if (!checked(swmm_heat_get_shortwave_timeseries(m_engine, name, sizeof name), m_swTsCombo, tr("Reading shortwave time series"))) return writes;
        const QString desired = m_swTsCombo->currentData().toString();
        if (desired != QString::fromUtf8(name) || mode != SWMM_HEAT_SW_TIMESERIES) {
            if (!checked(swmm_heat_set_shortwave_timeseries(m_engine, desired.toUtf8().constData()), m_swTsCombo, tr("Saving shortwave time series"))) return writes;
            ++writes;
        }
    } else {
        const int desiredMode = m_swComputed->isChecked() ? SWMM_HEAT_SW_COMPUTED : SWMM_HEAT_SW_CONSTANT;
        if (desiredMode != mode) {
            if (!checked(swmm_heat_set_shortwave_mode(m_engine, desiredMode), m_swComputed->isChecked() ? m_swComputed : m_swConstant, tr("Saving shortwave mode"))) return writes;
            ++writes;
        }
        if (desiredMode == SWMM_HEAT_SW_CONSTANT) {
            double current = 0;
            if (!checked(swmm_heat_get_radiative(m_engine, SWMM_HEAT_RAD_SHORTWAVE, &current), m_swConstSpin, tr("Reading constant shortwave"))) return writes;
            const double desired = OpenSWMM::Ui::preciseValue(m_swConstSpin);
            if (changed(current, desired)) {
                if (!checked(swmm_heat_set_radiative(m_engine, SWMM_HEAT_RAD_SHORTWAVE, desired), m_swConstSpin, tr("Saving constant shortwave"))) return writes;
                ++writes;
            }
        }
    }
    for (const auto &param : kRadScalars) {
        auto *spin = m_radSpin[param.code];
        const QString name = QCoreApplication::translate("HeatCfg", param.label);
        double current = 0;
        if (!checked(swmm_heat_get_radiative(m_engine, param.code, &current), spin, tr("Reading %1").arg(name))) return writes;
        const double desired = OpenSWMM::Ui::preciseValue(spin);
        if (!changed(current, desired)) continue;
        if (!checked(swmm_heat_set_radiative(m_engine, param.code, desired), spin, tr("Saving %1").arg(name))) return writes;
        ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeCloud()
{
    int writes = 0, configured = 0;
    if (!checked(swmm_heat_get_cloud_configured(m_engine, &configured), m_cloudEnable, tr("Reading cloud configuration"))) return writes;
    if (!m_cloudEnable->isChecked()) {
        if (configured) {
            if (!checked(swmm_heat_clear_cloud(m_engine), m_cloudEnable, tr("Clearing cloud configuration"))) return writes;
            ++writes;
        }
        return writes;
    }
    for (int param = 0; param < 4; ++param) {
        auto *spin = m_cloudSpin[param];
        if (!spin) continue;
        double current = 0;
        if (!checked(swmm_heat_get_cloud(m_engine, param, &current), spin, tr("Reading %1").arg(spin->accessibleName()))) return writes;
        const double desired = OpenSWMM::Ui::preciseValue(spin);
        if (!(!configured && param == SWMM_HEAT_CLOUD_FRACTION) && !changed(current, desired)) continue;
        if (!checked(swmm_heat_set_cloud(m_engine, param, desired), spin, tr("Saving %1").arg(spin->accessibleName()))) return writes;
        ++writes;
    }
    char name[256] = {};
    if (!checked(swmm_heat_get_cloud_timeseries(m_engine, name, sizeof name), m_cloudTsCombo, tr("Reading cloud time series"))) return writes;
    const QString desired = m_cloudTsCombo->currentData().toString();
    if (!desired.isEmpty() && desired != QString::fromUtf8(name)) {
        if (!checked(swmm_heat_set_cloud_timeseries(m_engine, desired.toUtf8().constData()), m_cloudTsCombo, tr("Saving cloud time series"))) return writes;
        ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeToEngine()
{
    if (!m_engine) return 0;
    int writes = 0;
    // Site values precede a computed shortwave request. Stop at the first
    // failure, preserving the successful count for the owner's dirty state.
    for (auto writer : {&HeatConfigDialog::writeSolar, &HeatConfigDialog::writeSources,
                        &HeatConfigDialog::writeModules, &HeatConfigDialog::writeRadiative,
                        &HeatConfigDialog::writeCloud}) {
        writes += (this->*writer)();
        if (m_writeFailed) break;
    }
    return writes;
}

void HeatConfigDialog::onAccept()
{
    m_lastWriteCount = 0;
    if (!validateDraft()) return;
    m_writeFailed = false;
    m_lastWriteCount = writeToEngine();
    if (m_lastWriteCount > 0) {
        m_wroteAnyChanges = true;
        emit changesApplied();
    }
    if (!m_writeFailed) accept();
}

} // namespace OpenSWMMVis
