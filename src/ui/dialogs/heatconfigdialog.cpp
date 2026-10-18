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
#include "ui/dialogs/initialbedqualitydialog.h"
#include "ui/precisenumericvalue.h"
#include "ui/uiscrollhelpers.h"
#include "ui/hydrothermalconfiguration.h"
#include "ui/heatproperties.h"
#include <QDataStream>
#include <cmath>
#include <limits>

#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_2d.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressDialog>
#include <QRadioButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>
#include <vector>

namespace OpenSWMMVis
{

namespace
{

// Compact decimal/scientific display keeps small physical coefficients readable.
class PhysicalNumberSpin final : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QString textFromValue(double value) const override { return locale().toString(value,'g',12); }
    double valueFromText(const QString& text) const override { return locale().toDouble(text); }
    QValidator::State validate(QString& text,int& position) const override {
        QDoubleValidator validator(minimum(),maximum(),decimals());
        validator.setLocale(locale());validator.setNotation(QDoubleValidator::ScientificNotation);
        return validator.validate(text,position);
    }
};

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
    { SWMM_HEAT_RAD_LANDCOVER_TEMPERATURE, QT_TRANSLATE_NOOP("HeatCfg", "Land-cover temperature (°C)") },
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
    for(auto* spin:findChildren<QDoubleSpinBox*>())connect(spin,&QDoubleSpinBox::valueChanged,this,&HeatConfigDialog::invalidateElementPreview);
    for(auto* combo:findChildren<QComboBox*>())connect(combo,&QComboBox::currentTextChanged,this,&HeatConfigDialog::invalidateElementPreview);
    for(auto* box:findChildren<QCheckBox*>())connect(box,&QCheckBox::toggled,this,&HeatConfigDialog::invalidateElementPreview);
    m_engineInitial=engineSignature();
    resize(960,720);
}

void HeatConfigDialog::setTransportRequested(bool enabled) { m_heatEnabled->setChecked(enabled); }

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
    m_heatEnabled = new QCheckBox(tr("Simulate temperature transport"), this);
    m_heatEnabled->setObjectName("hc_heatEnabled");
    vlay->addWidget(m_heatEnabled);
    m_processStatus = new QLabel(this);
    m_processStatus->setObjectName("hc_processStatus");
    m_processStatus->setWordWrap(true);
    vlay->addWidget(m_processStatus);
    auto *runtimeOverview = new QPushButton(tr("View saved and runtime process overview…"), this);
    runtimeOverview->setObjectName(QStringLiteral("hc_runtimeOverview"));
    runtimeOverview->setEnabled(m_engine != nullptr);
    vlay->addWidget(runtimeOverview, 0, Qt::AlignLeft);
    connect(runtimeOverview, &QPushButton::clicked,
            this, &HeatConfigDialog::showRuntimeOverview);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("hc_tabs"));
    m_tabs = tabs;
    vlay->addWidget(tabs);
    auto* sharedRadiation=new QPushButton(tr("Apply and open shared snow and radiation forcing…"),this);
    sharedRadiation->setObjectName("hc_sharedRadiation");sharedRadiation->setAutoDefault(false);
    sharedRadiation->setToolTip(tr("Apply these heat settings and open Climatology → Radiation for the shared site, cloud and forcing sources."));
    vlay->addWidget(sharedRadiation);
    connect(sharedRadiation,&QPushButton::clicked,this,[this]{onAccept();if(result()==QDialog::Accepted)emit radiationEditorRequested();});
    auto* groundwaterSettings=new QPushButton(tr("Apply and open 2D groundwater transport and thermal settings…"),this);
    groundwaterSettings->setObjectName(QStringLiteral("hc_groundwaterSettings"));
    groundwaterSettings->setAutoDefault(false);
    groundwaterSettings->setToolTip(tr("Apply these heat settings and open the 2D groundwater transport, porous soil heat and conduction options."));
    vlay->addWidget(groundwaterSettings);
    connect(groundwaterSettings,&QPushButton::clicked,this,[this]{onAccept();if(result()==QDialog::Accepted)emit groundwaterSettingsRequested();});


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
        m_sourceTable->horizontalHeader()->setStretchLastSection(false);
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
        m_overrideTable->horizontalHeader()->setStretchLastSection(false);
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
        const char *labels[4] = {
            QT_TRANSLATE_NOOP("HeatCfg",
                              "Surface exchange (latent + sensible)"),
            QT_TRANSLATE_NOOP("HeatCfg",
                              "Radiative exchange (shortwave + longwave)"),
            QT_TRANSLATE_NOOP("HeatCfg", "LID layer conduction"),
            QT_TRANSLATE_NOOP("HeatCfg", "Channel bed conduction and hyporheic exchange"),
        };
        for (int m = 0; m < 4; ++m) {
            m_modules[m] = new QCheckBox(
                QCoreApplication::translate("HeatCfg", labels[m]), fluxPage);
            m_modules[m]->setObjectName(QStringLiteral("hc_module_%1").arg(m));
            const char* const guidance[] = {
                QT_TRANSLATE_NOOP("HeatCfg", "Water-surface latent and sensible exchange. The mesh surface has a separate heat activation switch."),
                QT_TRANSLATE_NOOP("HeatCfg", "Shortwave and longwave exchange where a water-surface heat owner and its forcing are available."),
                QT_TRANSLATE_NOOP("HeatCfg", "Vertical heat transfer through LID layers. Mesh-connected LIDs also need an active groundwater heat owner."),
                QT_TRANSLATE_NOOP("HeatCfg", "1D channel and conduit beds. Mesh-bed properties and hyporheic eligibility are configured in Spatial Overrides."),
            };
            m_modules[m]->setToolTip(QCoreApplication::translate("HeatCfg", guidance[m]));
            lay->addWidget(m_modules[m]);
            m_moduleStatus[m] = new QLabel(fluxPage);
            m_moduleStatus[m]->setObjectName(QStringLiteral("hc_moduleStatus_%1").arg(m));
            m_moduleStatus[m]->setWordWrap(true);
            lay->addWidget(m_moduleStatus[m]);
            connect(m_modules[m], &QCheckBox::toggled, this, &HeatConfigDialog::refreshProcessStatus);
        }
        lay->addStretch(1);
    }
    m_dryPolicy = new QComboBox(fluxPage);
    m_dryPolicy->setObjectName("hc_dryPolicy");
    m_dryPolicy->addItem(tr("Hold last temperature"), 0);
    m_dryPolicy->addItem(tr("Use air temperature"), 1);
    m_dryPolicy->addItem(tr("Use default temperature (20 °C)"), 2);
    auto *dryForm = new QFormLayout;
    dryForm->addRow(tr("Dry element temperature:"), m_dryPolicy);
    fluxPage->layout()->addItem(dryForm);
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
        m_landcoverConfigured=new QCheckBox(tr("Set land-cover temperature (otherwise use air temperature)"),scalars);
        m_landcoverConfigured->setObjectName("hc_landcoverConfigured");
        form->addRow(m_landcoverConfigured);
        for (const auto &p : kRadScalars) {
            auto *spin = makeFractionSpin(scalars);
            spin->setObjectName(QStringLiteral("hc_rad_%1").arg(p.code));
            m_radSpin[p.code] = spin;
            if(p.code==SWMM_HEAT_RAD_LANDCOVER_TEMPERATURE)spin->setRange(-1e6,1e6);
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
        m_explicitSite = new QCheckBox(tr("Set latitude and longitude explicitly"), site);
        m_explicitSite->setObjectName("hc_explicitSite");
        sform->addRow(m_explicitSite);
        connect(m_explicitSite, &QCheckBox::toggled, this, &HeatConfigDialog::refreshProcessStatus);
        for (const auto &p : kSolarSite) {
            auto *spin = new QDoubleSpinBox(site);
            spin->setObjectName(QStringLiteral("hc_solar_%1").arg(p.code));
            spin->setDecimals(4);
            switch (p.code) {
            case SWMM_HEAT_SOLAR_LATITUDE:  spin->setRange(-90.0, 90.0); break;
            case SWMM_HEAT_SOLAR_LONGITUDE: spin->setRange(-180.0, 180.0); break;
            case SWMM_HEAT_SOLAR_TIMEZONE:  spin->setRange(-14.0, 14.0); break;
            case SWMM_HEAT_SOLAR_ELEVATION: spin->setRange(-500.0, 9000.0); break;
            default: break;
            }
            m_solarSpin[p.code] = spin;
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, p] {
                if (!m_loading && (p.code == SWMM_HEAT_SOLAR_LATITUDE || p.code == SWMM_HEAT_SOLAR_LONGITUDE))
                    m_explicitSite->setChecked(true);
                refreshProcessStatus();
            });
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


    auto *bedPage = new QWidget(tabs);
    auto *bedLayout = new QVBoxLayout(bedPage);
    auto *bedNote = new QLabel(tr("These settings control 1D channel and conduit beds. For 2D mesh-bed materials and hyporheic velocity, use Spatial Overrides with a 2D cell or mesh-region-tag scope; channel-burned cells tagged channel:* are hyporheic-eligible by default. Configure groundwater soil storage, conduction and boundaries under Assign Groundwater → Transport and thermal settings. All bed parameters use SI units."), bedPage);
    bedNote->setWordWrap(true); bedLayout->addWidget(bedNote);
    auto *channelPreviewNote = new QLabel(tr("Preview saved channels set to MESH_CONTACTS. This checks XY coverage and lists mesh cells; groundwater profile, bed thickness and thermal-owner checks still run during initialization. Apply edits and reopen this dialog to preview the saved configuration."), bedPage);
    channelPreviewNote->setWordWrap(true); bedLayout->addWidget(channelPreviewNote);
    m_channelGroundPreviewButton = new QPushButton(tr("Preview saved channel contacts"), bedPage);
    m_channelGroundPreviewButton->setObjectName(QStringLiteral("hc_channelGroundPreview"));
    m_channelGroundPreviewButton->setAutoDefault(false);
    bedLayout->addWidget(m_channelGroundPreviewButton);
    m_channelGroundPreviewStatus = new QLabel(tr("Preview has not been run."), bedPage);
    m_channelGroundPreviewStatus->setObjectName(QStringLiteral("hc_channelGroundPreviewStatus"));
    m_channelGroundPreviewStatus->setWordWrap(true);
    bedLayout->addWidget(m_channelGroundPreviewStatus);
    m_channelGroundPreviewTable = new QTableWidget(0, 4, bedPage);
    m_channelGroundPreviewTable->setObjectName(QStringLiteral("hc_channelGroundPreviewTable"));
    m_channelGroundPreviewTable->setHorizontalHeaderLabels({tr("Link"),tr("Mesh cells (1-based)"),tr("Covered / total (m)"),tr("XY result")});
    auto *channelPreviewHeader = m_channelGroundPreviewTable->horizontalHeader();
    for (int column = 0; column < 3; ++column)
        channelPreviewHeader->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    channelPreviewHeader->setSectionResizeMode(3, QHeaderView::Stretch);
    m_channelGroundPreviewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_channelGroundPreviewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_channelGroundPreviewTable->setAccessibleName(tr("Saved channel mesh-contact coverage preview"));
    m_channelGroundPreviewTable->setMaximumHeight(220);
    bedLayout->addWidget(m_channelGroundPreviewTable);
    auto* initialBedQuality = new QPushButton(tr("Apply and edit 2D local-bed initial quality…"), bedPage);
    initialBedQuality->setObjectName(QStringLiteral("hc_initialBedQuality"));
    initialBedQuality->setAutoDefault(false);
    initialBedQuality->setToolTip(tr("Apply heat settings, then edit initial pollutant and water-age concentrations stored in LOCAL_STORAGE bed porewater."));
    bedLayout->addWidget(initialBedQuality);
    connect(initialBedQuality, &QPushButton::clicked, this, [this] {
        onAccept();
        if (result() != QDialog::Accepted || !m_engine) return;
        InitialBedQualityDialog editor(m_engine, this);
        connect(&editor, &InitialBedQualityDialog::changesApplied, this, [this] {
            m_wroteAnyChanges = true;
            emit changesApplied();
        });
        editor.exec();
    });
    auto *bedForm = new QFormLayout;
    const char *bedLabels[] = {"Thermal diffusivity (m²/s)", "Solute diffusivity (m²/s)",
        "Bed thickness (m)", "Depth to deep ground (m)", "Ground temperature (°C)",
        "Hyporheic velocity (m/s)", "Sediment density (kg/m³)",
        "Sediment specific heat (J/kg/K)", "Initial bed temperature (°C)"};
    for (int i=0; i<9; ++i) {
        auto *spin = m_bedSpin[i] = new PhysicalNumberSpin(bedPage);
        spin->setObjectName(QString("hc_bed_%1").arg(i));
        spin->setDecimals(12); spin->setRange(0, 1e12);
        spin->setSingleStep(i<2?1e-7:i==5?1e-6:0.1);
        if (i==4 || i==8) spin->setRange(-1e6,1e6);
        else if (i!=1 && i!=5) spin->setMinimum(1e-12);
        bedForm->addRow(tr(bedLabels[i]), spin);
    }
    m_groundConfigured = new QCheckBox(tr("Set ground temperature (otherwise default 12 °C)"), bedPage);
    m_groundConfigured->setObjectName("hc_groundConfigured");
    m_groundMode = new QComboBox(bedPage); m_groundMode->setObjectName("hc_groundMode");
    m_groundMode->addItems({tr("Constant"),tr("Time series")});
    m_groundSeries = new QComboBox(bedPage); m_groundSeries->setObjectName("hc_groundSeries");
    m_initialConfigured = new QCheckBox(tr("Set initial bed temperature (otherwise inherit ground temperature)"), bedPage);
    m_initialConfigured->setObjectName("hc_initialConfigured");
    bedForm->insertRow(4, m_groundConfigured);
    bedForm->insertRow(5, tr("Ground boundary:"), m_groundMode);
    bedForm->insertRow(6, tr("Ground temperature series:"), m_groundSeries);
    bedForm->insertRow(bedForm->rowCount()-1,m_initialConfigured);
    bedLayout->addLayout(bedForm); bedLayout->addStretch();
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(bedPage,tabs), tr("Channel bed and ground"));
    auto *material=new QLabel(fluxPage);material->setWordWrap(true);
    double values[4]{};
    if(m_engine && swmm_heat_get_lid_material(m_engine,values)==SWMM_OK)
        material->setText(tr("LID material assumptions (read-only): water conductivity %1 W/m/K; soil conductivity %2 W/m/K; soil density %3 kg/m³; specific heat %4 J/kg/K.")
            .arg(values[0]).arg(values[1]).arg(values[2]).arg(values[3]));
    fluxPage->layout()->addWidget(material);
    connect(m_landcoverConfigured,&QCheckBox::toggled,this,&HeatConfigDialog::refreshProcessStatus);
    for (auto *c : {m_groundConfigured, m_initialConfigured})
        connect(c, &QCheckBox::toggled, this, &HeatConfigDialog::refreshProcessStatus);
    connect(m_groundMode, &QComboBox::currentIndexChanged, this, &HeatConfigDialog::refreshProcessStatus);
    connect(m_channelGroundPreviewButton,&QPushButton::clicked,this,&HeatConfigDialog::previewChannelGroundContacts);

    auto *spatialPage = new QWidget(tabs);
    auto *spatialLayout = new QVBoxLayout(spatialPage);
    auto *spatialNote = new QLabel(tr("Overrides apply to a named link, node, network tag, mesh region tag or 2D cell. Mesh region tags are separate from network tags; cell IDs are one-based. Remove a row to inherit its tag/global value. "
        "Sediment properties apply to conduit beds and cannot target nodes. "
        "A global ground-temperature series takes precedence over constant ground-temperature overrides."), spatialPage);
    spatialNote->setWordWrap(true); spatialLayout->addWidget(spatialNote);
    m_elementTable = new QTableWidget(0,4,spatialPage); m_elementTable->setObjectName("hc_elementTable");
    m_elementTable->setHorizontalHeaderLabels({tr("Property"),tr("Scope"),tr("Object / tag / cell ID"),tr("Value (SI / °C)")});
    m_elementTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    spatialLayout->addWidget(m_elementTable);
    auto *spatialButtons = new QHBoxLayout;
    auto *addElement = new QPushButton(tr("Add override"),spatialPage);
    auto *removeElement = new QPushButton(tr("Remove selected overrides / inherit"),spatialPage);
    addElement->setObjectName("hc_addElement"); removeElement->setObjectName("hc_removeElement");
    addElement->setAutoDefault(false); removeElement->setAutoDefault(false);
    connect(addElement,&QPushButton::clicked,this,&HeatConfigDialog::addElementOverride);
    connect(removeElement,&QPushButton::clicked,this,[this] {
        auto rows=m_elementTable->selectionModel()->selectedRows();
        std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.row()>b.row();});
        for (auto row:rows) m_elementTable->removeRow(row.row());
    });
    m_elementTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    spatialButtons->addWidget(addElement); spatialButtons->addWidget(removeElement);
    auto* resolve=new QPushButton(tr("Preview resolved values"),spatialPage);resolve->setObjectName("hc_previewElements");resolve->setAutoDefault(false);
    connect(resolve,&QPushButton::clicked,this,&HeatConfigDialog::previewElements);spatialButtons->addWidget(resolve);
    spatialLayout->addLayout(spatialButtons);
    m_resolvedElements=new QTableWidget(0,3,spatialPage);m_resolvedElements->setObjectName("hc_resolvedElements");
    m_resolvedElements->setHorizontalHeaderLabels({tr("Object"),tr("Effective value (SI / °C)"),tr("Value source")});
    m_resolvedElements->setEditTriggers(QAbstractItemView::NoEditTriggers);m_resolvedElements->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);spatialLayout->addWidget(m_resolvedElements);
    m_resolvedStatus=new QLabel(tr("Select an override row, then preview its resolved values."),spatialPage);m_resolvedStatus->setWordWrap(true);spatialLayout->addWidget(m_resolvedStatus);
    connect(m_elementTable->model(),&QAbstractItemModel::rowsRemoved,this,&HeatConfigDialog::invalidateElementPreview);
    connect(m_elementTable->model(),&QAbstractItemModel::rowsInserted,this,&HeatConfigDialog::invalidateElementPreview);
    auto* raster=new QPushButton(tr("Apply and assign from raster…"),spatialPage);raster->setObjectName("hc_assignRaster");raster->setAutoDefault(false);
    raster->setToolTip(tr("Accept these heat edits, then preview spatial values from a loaded raster. Each accepted edit can be undone."));
    connect(raster,&QPushButton::clicked,this,[this]{onAccept();if(result()==QDialog::Accepted)emit rasterAssignmentRequested();});spatialLayout->addWidget(raster);
    tabs->addTab(spatialPage,tr("Spatial properties"));
    connect(m_heatEnabled,&QCheckBox::toggled,this,&HeatConfigDialog::refreshProcessStatus);
    connect(m_swConstant,&QRadioButton::toggled,this,&HeatConfigDialog::refreshProcessStatus);
    connect(m_swTimeseries,&QRadioButton::toggled,this,&HeatConfigDialog::refreshProcessStatus);

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

void HeatConfigDialog::previewChannelGroundContacts()
{
    m_channelGroundPreviewTable->setRowCount(0);
    if(!m_engine) {
        m_channelGroundPreviewStatus->setText(tr("No model is available for preview."));
        return;
    }
    int links=swmm_link_count(m_engine),shown=0,complete=0,failures=0;
    for(int link=0;link<links;++link) {
        const char* rawId=swmm_link_id(m_engine,link);
        if(!rawId)continue;
        double groundSource=0;int source=-1;char sourceName[1024]{};
        if(swmm_heat_get_effective_element(m_engine,SWMM_HEAT_ATTR_GROUND_SOURCE,
            SWMM_HEAT_SCOPE_LINK,rawId,&groundSource,&source,sourceName,sizeof sourceName)!=SWMM_OK) {
            ++failures;continue;
        }
        if(std::abs(groundSource-1.0)>1e-12)continue;

        SWMM_ChannelGroundPreview preview{};preview.struct_size=sizeof preview;
        int required=0;
        const int previewRc=swmm_heat_preview_channel_ground_map(m_engine,link,&preview,nullptr,0,&required);
        if(previewRc!=SWMM_OK) {++failures;continue;}
        std::vector<SWMM_ChannelGroundContact> contacts(static_cast<std::size_t>(required));
        preview.struct_size=sizeof preview;
        if(swmm_heat_preview_channel_ground_map(m_engine,link,&preview,
                contacts.empty()?nullptr:contacts.data(),required,&required)!=SWMM_OK) {
            ++failures;continue;
        }
        QSet<int> cells;
        for(const auto& contact:contacts)cells.insert(contact.cell+1);
        QStringList cellIds;
        QList<int> sortedCells=cells.values();std::sort(sortedCells.begin(),sortedCells.end());
        for(int cell:sortedCells)cellIds.append(QString::number(cell));
        if(cellIds.size()>30) {
            cellIds=cellIds.mid(0,30);
            cellIds.append(tr("… (+%1 more)").arg(cells.size()-30));
        }
        const int row=m_channelGroundPreviewTable->rowCount();m_channelGroundPreviewTable->insertRow(row);
        const QStringList values={QString::fromUtf8(rawId),cellIds.join(QStringLiteral(", ")),
            QStringLiteral("%1 / %2").arg(preview.covered_length_m,0,'g',7).arg(preview.length_m,0,'g',7),
            QString::fromUtf8(preview.reason)};
        for(int column=0;column<values.size();++column)
            m_channelGroundPreviewTable->setItem(row,column,new QTableWidgetItem(values[column]));
        complete+=preview.valid!=0;
        ++shown;
    }
    if(shown==0 && failures==0) {
        m_channelGroundPreviewStatus->setText(tr("No links currently resolve to MESH_CONTACTS. Set Ground-source mode to MESH_CONTACTS for a link or network tag, apply the edits, then reopen this dialog."));
    } else {
        m_channelGroundPreviewStatus->setText(tr("Previewed %1 saved link setting(s). %2 complete XY coverage; %3 need geometry review.%4")
            .arg(shown).arg(complete)
            .arg(m_channelGroundPreviewTable->rowCount()-complete)
            .arg(failures?tr(" %1 link(s) could not be read.").arg(failures):QString()));
    }
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
    m_loading = true;
    int heatEnabled=0; swmm_heat_get_enabled(m_engine,&heatEnabled);
    m_heatEnabled->setChecked(heatEnabled);
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

    m_groundSeries->addItem(tr("Select a time series"), QString());
    for (int i=0;i<m_swTsCombo->count();++i)
        if (!m_swTsCombo->itemData(i).toString().isEmpty())
            m_groundSeries->addItem(m_swTsCombo->itemText(i),m_swTsCombo->itemData(i));
    tsbuf[0]=0; swmm_heat_get_sediment_ground_timeseries(m_engine,tsbuf,sizeof tsbuf);
    if (*tsbuf) m_groundSeries->setCurrentIndex(m_groundSeries->findData(QString::fromUtf8(tsbuf)));
    m_groundMode->setCurrentIndex(*tsbuf ? 1 : 0);
    for (int i=0;i<9;++i) { double v=0;
        if (swmm_heat_get_sediment(m_engine,i,&v)==SWMM_OK) OpenSWMM::Ui::setHydratedValue(m_bedSpin[i],v);
    }
    int configured=0;
    swmm_heat_sediment_temperature_configured(m_engine,4,&configured); m_groundConfigured->setChecked(configured);
    swmm_heat_sediment_temperature_configured(m_engine,8,&configured); m_initialConfigured->setChecked(configured);
    int dry=0; swmm_heat_get_dry_policy(m_engine,&dry); m_dryPolicy->setCurrentIndex(m_dryPolicy->findData(dry));
    int elementCount=0; swmm_heat_element_override_count(m_engine,&elementCount);
    for (int i=0;i<elementCount;++i) {
        int attr=0, scope=0; double v=0; char name[4096]{};
        if (swmm_heat_get_element_override(m_engine,i,&attr,&scope,name,sizeof name,&v)!=SWMM_OK) continue;
        addElementOverride(); const int row=m_elementTable->rowCount()-1;
        for (auto col:{0,1}) { auto *c=qobject_cast<QComboBox*>(m_elementTable->cellWidget(row,col));
            c->setCurrentIndex(c->findData(col==0?attr:scope)); }
        qobject_cast<QComboBox*>(m_elementTable->cellWidget(row,2))->setEditText(QString::fromUtf8(name));
        OpenSWMM::Ui::setHydratedValue(qobject_cast<QDoubleSpinBox*>(m_elementTable->cellWidget(row,3)),v);
    }
    m_elementsInitial=elementSignature();

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
    for (int m = 0; m < 4; ++m) {
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
        if (swmm_heat_get_radiative(m_engine, p.code, &v) == SWMM_OK) {
            if(p.code==SWMM_HEAT_RAD_LANDCOVER_TEMPERATURE)m_landcoverConfigured->setChecked(std::isfinite(v));
            OpenSWMM::Ui::setHydratedValue(m_radSpin[p.code], std::isfinite(v)?v:20.0);
        }

    int sited = 0; swmm_heat_get_solar_sited(m_engine,&sited);
    m_explicitSite->setChecked(sited);
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
    m_loading = false;
    refreshProcessStatus();
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
        for (auto *table : {m_sourceTable, m_overrideTable, m_elementTable})
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
    return reportFailure(field, tr("%1 failed (engine error %2). The edit was rolled back; correct the draft and try again.")
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
    if (m_groundConfigured->isChecked() && m_groundMode->currentIndex()==1 && !validSeries(m_groundSeries,true)) return false;
    QSet<QString> keys;
    for(int r=0;r<m_elementTable->rowCount();++r) {
        auto *attr=qobject_cast<QComboBox*>(m_elementTable->cellWidget(r,0));
        auto *scope=qobject_cast<QComboBox*>(m_elementTable->cellWidget(r,1));
        auto *name=qobject_cast<QComboBox*>(m_elementTable->cellWidget(r,2));
        auto *value=qobject_cast<QDoubleSpinBox*>(m_elementTable->cellWidget(r,3));
        if(name->currentText().trimmed().isEmpty()) return reportFailure(name,tr("Select an object or enter a tag for override %1.").arg(r+1));
        const int attribute=attr->currentData().toInt();
        const double numericValue=OpenSWMM::Ui::preciseValue(value);
        if(!heatPropertyValueValid(attribute,numericValue))
            return reportFailure(value,tr("%1 must be within %2 to %3.").arg(heatPropertyLabel(attribute))
                .arg(heatProperties()[attribute].minimum,0,'g',8).arg(heatProperties()[attribute].maximum,0,'g',8));
        if(attr->currentData().toInt()>=6 && scope->currentData().toInt()==SWMM_HEAT_SCOPE_NODE)
            return reportFailure(scope,tr("Sediment properties apply to links or tags, not nodes."));
        if(attr->currentData().toInt()==SWMM_HEAT_ATTR_GROUND_SOURCE &&
           scope->currentData().toInt()!=SWMM_HEAT_SCOPE_TAG &&
           scope->currentData().toInt()!=SWMM_HEAT_SCOPE_LINK)
            return reportFailure(scope,tr("Ground-source mode applies to network tags or links, not nodes or 2D mesh scopes."));
        const QString key=QString::number(attr->currentData().toInt())+':'+QString::number(scope->currentData().toInt())+':'+name->currentText();
        if(keys.contains(key))return reportFailure(name,tr("Duplicate spatial override; edit the existing row."));
        keys.insert(key);
    }
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
    for (int module = 0; module < 4; ++module) {
        int current = 0;
        auto *field = m_modules[module];
        if (!checked(swmm_heat_get_module(m_engine, module, &current), field, tr("Reading %1").arg(field->text()))) return writes;
        const int desired = field->isChecked() ? 1 : 0;
        if (desired == current) continue;
        if (!checked(swmm_heat_set_module(m_engine, module, desired), field, tr("Saving %1").arg(field->text()))) return writes;
        ++writes;
    }
    int policy=0; swmm_heat_get_dry_policy(m_engine,&policy);
    if (policy!=m_dryPolicy->currentData().toInt()) {
        if (!checked(swmm_heat_set_dry_policy(m_engine,m_dryPolicy->currentData().toInt()),m_dryPolicy,tr("Saving dry temperature policy"))) return writes;
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
        int sited=0; swmm_heat_get_solar_sited(m_engine,&sited);
        const bool authorSite=param<2 && m_explicitSite->isChecked() && !sited;
        if (!changed(current, desired) && !authorSite) continue;
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
        const double desired = param.code==SWMM_HEAT_RAD_LANDCOVER_TEMPERATURE&&!m_landcoverConfigured->isChecked()
            ? std::numeric_limits<double>::quiet_NaN() : OpenSWMM::Ui::preciseValue(spin);
        if ((!std::isfinite(current)&&!std::isfinite(desired)) || !changed(current, desired)) continue;
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
    int enabled=0; swmm_heat_get_enabled(m_engine,&enabled);
    if (bool(enabled)!=m_heatEnabled->isChecked()) {
        if (!checked(swmm_options_set(m_engine,"HEAT_TRANSPORT",m_heatEnabled->isChecked()?"YES":"NO"),m_heatEnabled,tr("Saving temperature transport"))) return 0;
        ++writes;
    }
    // Site values precede a computed shortwave request. Stop at the first
    // failure, preserving the successful count for the owner's dirty state.
    for (auto writer : {&HeatConfigDialog::writeSolar, &HeatConfigDialog::writeSources,
                        &HeatConfigDialog::writeModules, &HeatConfigDialog::writeRadiative,
                        &HeatConfigDialog::writeCloud, &HeatConfigDialog::writeBed, &HeatConfigDialog::writeElements}) {
        writes += (this->*writer)();
        if (m_writeFailed) break;
    }
    if(!m_writeFailed&&!m_explicitSite->isChecked()) {
        int sited=0;swmm_heat_get_solar_sited(m_engine,&sited);
        if(sited) { if(!checked(swmm_heat_clear_solar_site(m_engine),m_explicitSite,tr("Restoring default site")))return writes; ++writes; }
    }
    if(writes>0&&!m_writeFailed&&!checked(OpenSWMM::Ui::ensureHeatConfigurationFile(m_engine),m_processStatus,tr("Preparing heat settings for saving")))return writes;
    return writes;
}

void HeatConfigDialog::onAccept()
{
    m_lastWriteCount = 0;
    if (!validateDraft()) return;
    if(engineSignature()!=m_engineInitial) {
        reportFailure(nullptr,tr("Heat or shared radiation settings changed in another editor. Reopen this dialog before applying."));
        return;
    }
    m_writeFailed = false;
    const int result=swmm_hydrothermal_edit_atomic(m_engine, [](SWMM_Engine, void* user) -> int {
        auto *dialog=static_cast<HeatConfigDialog*>(user);
        dialog->m_lastWriteCount=dialog->writeToEngine();
        return dialog->m_writeFailed ? SWMM_ERR_BADPARAM : SWMM_OK;
    }, this);
    if (result!=SWMM_OK) {
        m_lastWriteCount=0; m_writeFailed=true;
        if (m_validationError->text().isEmpty()) reportFailure(nullptr,tr("Configuration is editable before initialization. Reset the simulation to edit."));
    }
    if (m_lastWriteCount > 0) {
        m_wroteAnyChanges = true;
        emit changesApplied();
    }
    if (!m_writeFailed) accept();
}

void HeatConfigDialog::refreshProcessStatus()
{
    if (m_loading || !m_processStatus || !m_groundMode) return;
    const bool enabled=m_heatEnabled->isChecked();
    m_processStatus->setText(enabled
        ? tr("Temperature transport configured. Local heat exchanges below run only in supported domains. Configure 2D porous-soil heat storage, conduction and boundaries in Assign Groundwater → Transport and thermal settings; those processes have separate activation requirements.")
        : tr("Network and lumped temperature transport is off. These heat-module settings are retained but inactive for that process; 2D mesh heat has separate activation controls."));
    char ignored[32]{};
    if(m_engine)swmm_options_get(m_engine,"IGNORE_QUALITY",ignored,sizeof ignored);
    if(enabled && QString::fromUtf8(ignored)=="YES")m_processStatus->setText(m_processStatus->text()+tr(" Network and lumped temperature transport are inactive because Water quality is off; mesh temperature has its own domain setting."));
    const QStringList scopes={tr("Water surfaces"),tr("Water surfaces with configured radiation forcing"),
        tr("LID soil and storage layers"),tr("1D channel and conduit sediment beds")};
    const bool network_enabled=QString::fromUtf8(ignored)!="YES";
    for (int i=0;i<4;++i) {
        const QString state=!m_modules[i]->isChecked()?tr("Off"):
            !enabled?tr("Configured On · Inactive for network and lumped domains because temperature transport is off; mesh heat has separate activation controls"):
            !network_enabled?tr("Configured On · Network and lumped use is inactive while Water quality is off; mesh heat has separate activation controls"):
            tr("Configured On · Runs when this domain has water and its required inputs are available");
        m_moduleStatus[i]->setText(state+" — "+scopes[i]);
    }
    m_radSpin[8]->setEnabled(m_landcoverConfigured->isChecked());
    m_swComputed->setEnabled(m_explicitSite->isChecked() || m_swComputed->isChecked());
    m_explicitSite->setEnabled(!m_swComputed->isChecked());
    m_swComputed->setToolTip(m_explicitSite->isChecked()?QString():tr("Set latitude and longitude explicitly on the Solar tab."));
    m_swConstSpin->setEnabled(m_swConstant->isChecked());
    m_swTsCombo->setEnabled(m_swTimeseries->isChecked());
    m_groundMode->setEnabled(m_groundConfigured->isChecked());
    m_groundSeries->setEnabled(m_groundConfigured->isChecked() && m_groundMode->currentIndex()==1);
    m_bedSpin[4]->setEnabled(m_groundConfigured->isChecked() && m_groundMode->currentIndex()==0);
    m_bedSpin[8]->setEnabled(m_initialConfigured->isChecked());
}

QString HeatConfigDialog::runtimeOverviewText() const
{
    if (!m_engine)
        return tr("The model is no longer available. Reopen the project to inspect process status.");

    QStringList lines;
    int savedHeat = 0;
    if (swmm_heat_get_enabled(m_engine, &savedHeat) != SWMM_OK)
        return tr("The saved heat-transport setting could not be read.");
    lines << tr("Saved network and lumped temperature transport: %1.").arg(savedHeat ? tr("on") : tr("off"));
    lines << tr("Current editor draft: %1.").arg(m_heatEnabled && m_heatEnabled->isChecked() ? tr("on") : tr("off"));

    char ignored[32]{};
    const bool canReadQuality = swmm_options_get(m_engine, "IGNORE_QUALITY", ignored, sizeof ignored) == SWMM_OK;
    const bool qualityOff = canReadQuality && QString::fromUtf8(ignored).compare(QStringLiteral("YES"), Qt::CaseInsensitive) == 0;
    if (qualityOff)
        lines << tr("Network and lumped temperature transport is inactive while Water quality is off. Mesh temperature has a separate activation setting.");
    else if (!savedHeat)
        lines << tr("Temperature transport is off; pollutant, reaction, MSX and water-age settings remain independent.");

    const QStringList moduleNames = {tr("Water-surface heat exchange"), tr("Radiation"),
                                     tr("LID layer heat transfer"), tr("1D channel-bed exchange")};
    for (int module = 0; module < 4; ++module) {
        int configured = 0;
        if (swmm_heat_get_module(m_engine, module, &configured) != SWMM_OK) continue;
        const bool draftOn = m_modules[module] && m_modules[module]->isChecked();
        QString state = configured ? tr("saved on") : tr("saved off");
        if (draftOn != (configured != 0))
            state += tr("; draft %1").arg(draftOn ? tr("on") : tr("off"));
        if (configured && (!savedHeat || qualityOff))
            state += tr("; inactive for network and lumped domains");
        lines << tr("%1: %2.").arg(moduleNames[module], state);
    }

    int engineState = SWMM_STATE_NONE;
    if (swmm_engine_get_state(m_engine, &engineState) != SWMM_OK) {
        lines << tr("Runtime state could not be read.");
        return lines.join(QLatin1Char('\n'));
    }
    const bool initialized = engineState == SWMM_STATE_INITIALIZED || engineState == SWMM_STATE_STARTED ||
                             engineState == SWMM_STATE_RUNNING || engineState == SWMM_STATE_ENDED;
    const auto stateName = [engineState]() {
        switch (engineState) {
        case SWMM_STATE_CREATED: return QObject::tr("created");
        case SWMM_STATE_OPENED: return QObject::tr("opened");
        case SWMM_STATE_BUILDING: return QObject::tr("being built");
        case SWMM_STATE_INITIALIZED: return QObject::tr("initialized");
        case SWMM_STATE_STARTED: return QObject::tr("started");
        case SWMM_STATE_RUNNING: return QObject::tr("running");
        case SWMM_STATE_ENDED: return QObject::tr("ended");
        default: return QObject::tr("unavailable");
        }
    };
    lines << tr("Simulation state: %1.").arg(stateName());

    SWMM_Heat2DStatus mesh{}; mesh.struct_size = sizeof(mesh);
    const bool haveMesh = swmm_heat_get_2d_status(m_engine, &mesh) == SWMM_OK;
    SWMM_GroundHeatStatus ground{}; ground.struct_size = sizeof(ground);
    const bool haveGround = swmm_heat_get_ground_status(m_engine, &ground) == SWMM_OK;
    SWMM_ChannelGroundStatus channel{}; channel.struct_size = sizeof(channel);
    const bool haveChannel = swmm_heat_get_channel_ground_status(m_engine, &channel) == SWMM_OK;
    const auto domainState = [](bool requested, bool ready, bool active, const char *reason) {
        QString result = !requested ? QObject::tr("not requested")
            : !ready ? QObject::tr("requested; not initialized")
            : active ? QObject::tr("active") : QObject::tr("initialized; inactive");
        const QString why = QString::fromUtf8(reason ? reason : "").trimmed();
        if (!why.isEmpty()) result += QObject::tr(" — %1").arg(why);
        return result;
    };

    if (!initialized) {
        lines << tr("Runtime activation and energy receipts are not available until the model is initialized.");
        if (haveMesh) lines << tr("2D heat: %1.").arg(domainState(mesh.requested, mesh.initialized,
            mesh.surface_active || mesh.radiative_active || mesh.bed_active, mesh.reason));
        if (haveGround) lines << tr("Porous-soil heat: %1.").arg(domainState(ground.requested, ground.initialized,
            ground.conduction_active || ground.lateral_active, ground.reason));
        if (haveChannel) lines << tr("Routed-channel/soil exchange: %1.").arg(domainState(channel.requested,
            channel.initialized, channel.exchange_active, channel.reason));
        return lines.join(QLatin1Char('\n'));
    }

    if (haveMesh) {
        const QString bedModel = mesh.bed_model == 1 ? tr("local bed storage")
                              : mesh.bed_model == 2 ? tr("ground-column bed") : tr("no bed model");
        lines << tr("2D surface heat: %1; radiation: %2; bed: %3 (%4).")
            .arg(mesh.surface_active ? tr("active") : tr("inactive"),
                 mesh.radiative_active ? tr("active") : tr("inactive"),
                 mesh.bed_active ? tr("active") : tr("inactive"), bedModel);
        if (mesh.reason[0]) lines << tr("2D heat reason: %1").arg(QString::fromUtf8(mesh.reason));
        if (mesh.experimental) lines << tr("2D heat capability is marked experimental.");
    }
    if (haveGround) {
        lines << tr("Porous-soil heat: %1; %2 aquifer cells, %3 standalone cells, %4 thermal layers; lateral conduction %5.")
            .arg(domainState(ground.requested, ground.initialized,
                             ground.conduction_active || ground.lateral_active, ground.reason))
            .arg(ground.aquifer_cells).arg(ground.standalone_cells).arg(ground.thermal_layers)
            .arg(ground.lateral_active ? tr("active") : tr("inactive"));
        if (ground.experimental) lines << tr("Porous-soil heat capability is marked experimental.");
    }
    if (haveChannel) {
        lines << tr("Routed-channel/soil exchange: %1; %2 mapped links, %3 contact cells and %4 contact intervals.")
            .arg(domainState(channel.requested, channel.initialized, channel.exchange_active, channel.reason))
            .arg(channel.mapped_links).arg(channel.contact_cells).arg(channel.contact_intervals);
        if (channel.experimental) lines << tr("Routed-channel/soil exchange is marked experimental.");
    }

    int cellCount = 0;
    if (swmm_2d_cell_count(m_engine, &cellCount) == SWMM_OK && cellCount > 0) {
        const auto sumValues = [this, cellCount](int kind, bool groundValues, double &total) {
            constexpr int chunkSize = 4096;
            std::vector<double> values(static_cast<std::size_t>(std::min(cellCount, chunkSize)));
            bool found = false;
            for (int first = 0; first < cellCount; first += chunkSize) {
                const int count = std::min(chunkSize, cellCount - first);
                if (static_cast<int>(values.size()) != count) values.resize(static_cast<std::size_t>(count));
                const int rc = groundValues
                    ? swmm_heat_get_ground_values(m_engine, kind, first, count, values.data())
                    : swmm_heat_get_2d_values(m_engine, kind, first, count, values.data());
                if (rc != SWMM_OK) return false;
                for (double v : values) if (std::isfinite(v)) { total += v; found = true; }
            }
            return found;
        };
        QStringList receipts;
        const auto appendReceipt = [&](int kind, bool groundValues, const QString &name) {
            double total = 0.0;
            if (sumValues(kind, groundValues, total))
                receipts << tr("%1 %2 J").arg(name).arg(total, 0, 'g', 8);
        };
        if (haveMesh && mesh.initialized) {
            appendReceipt(SWMM_HEAT_2D_ATMOSPHERIC_ENERGY, false, tr("mesh atmosphere"));
            appendReceipt(SWMM_HEAT_2D_RADIATIVE_ENERGY, false, tr("mesh radiation"));
            appendReceipt(SWMM_HEAT_2D_LATENT_ENERGY, false, tr("mesh latent heat"));
            appendReceipt(SWMM_HEAT_2D_WATER_TO_BED_ENERGY, false, tr("water to bed"));
        }
        if (haveGround && ground.initialized) {
            appendReceipt(SWMM_GROUND_HEAT_ADVECTIVE_ENERGY, true, tr("soil advection"));
            appendReceipt(SWMM_GROUND_HEAT_SURFACE_ENERGY, true, tr("soil surface"));
            appendReceipt(SWMM_GROUND_HEAT_DEEP_ENERGY, true, tr("soil deep boundary"));
            appendReceipt(SWMM_GROUND_HEAT_LATERAL_ENERGY, true, tr("soil lateral"));
            appendReceipt(SWMM_GROUND_HEAT_CHANNEL_CONTACT, true, tr("channel contact"));
            appendReceipt(SWMM_GROUND_HEAT_LID_CONTACT, true, tr("LID contact"));
            appendReceipt(SWMM_GROUND_HEAT_RESIDUAL, true, tr("soil residual"));
        }
        if (!receipts.isEmpty()) {
            lines << tr("Measured cumulative signed receipts since initialization (SI joules): %1.").arg(receipts.join(QStringLiteral("; ")));
            lines << tr("Activity is shown separately from receipt totals; a zero signed total does not mean that a process is disabled.");
        } else if ((haveMesh && mesh.initialized) || (haveGround && ground.initialized)) {
            lines << tr("No cumulative energy receipts are available for the initialized thermal domains.");
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void HeatConfigDialog::showRuntimeOverview()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Saved and runtime process overview"));
    dialog.setObjectName(QStringLiteral("hc_runtimeOverviewDialog"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QTextBrowser(&dialog);
    text->setObjectName(QStringLiteral("hc_runtimeOverviewText"));
    text->setAccessibleName(tr("Saved settings and runtime process status"));
    text->setOpenLinks(false);
    text->setPlainText(runtimeOverviewText());
    layout->addWidget(text, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(760, 520);
    dialog.exec();
}

void HeatConfigDialog::addElementOverride()
{
    const int row=m_elementTable->rowCount(); m_elementTable->insertRow(row);
    auto *attr=new QComboBox(m_elementTable);
    for(int i=0;i<int(heatProperties().size());++i)attr->addItem(heatPropertyLabel(i),i);
    auto *scope=new QComboBox(m_elementTable);
    scope->addItem(tr("Network tag"),SWMM_HEAT_SCOPE_TAG);
    scope->addItem(tr("Node"),SWMM_HEAT_SCOPE_NODE);
    scope->addItem(tr("Link"),SWMM_HEAT_SCOPE_LINK);
    scope->addItem(tr("Mesh region tag"),SWMM_HEAT_SCOPE_MESH_TAG);
    scope->addItem(tr("2D cell"),SWMM_HEAT_SCOPE_CELL2D);
    scope->setToolTip(tr("Network tags and mesh region tags are separate. A 2D cell uses its one-based mesh cell ID."));
    auto *name=new QComboBox(m_elementTable);name->setEditable(true);
    auto populate=[this,scope,name] {
        const auto previous=name->currentText(); name->clear();
        name->setValidator(nullptr);
        if(name->lineEdit())name->lineEdit()->setPlaceholderText(QString());
        if(m_engine) {
            const int selectedScope=scope->currentData().toInt();
            if(selectedScope==SWMM_HEAT_SCOPE_NODE)
                for(int i=0;i<swmm_node_count(m_engine);++i) name->addItem(QString::fromUtf8(swmm_node_id(m_engine,i)));
            if(selectedScope==SWMM_HEAT_SCOPE_LINK)
                for(int i=0;i<swmm_link_count(m_engine);++i) name->addItem(QString::fromUtf8(swmm_link_id(m_engine,i)));
            if(selectedScope==SWMM_HEAT_SCOPE_TAG) {
                QSet<QString> tags;
                for(int i=0;i<swmm_node_count(m_engine);++i) {
                    char tag[1024]{};
                    if(swmm_node_get_tag(m_engine,i,tag,sizeof tag)==SWMM_OK && *tag) tags.insert(QString::fromUtf8(tag));
                }
                for(int i=0;i<swmm_link_count(m_engine);++i) {
                    char tag[1024]{};
                    if(swmm_link_get_tag(m_engine,i,tag,sizeof tag)==SWMM_OK && *tag) tags.insert(QString::fromUtf8(tag));
                }
                QStringList sorted=tags.values(); sorted.sort(Qt::CaseInsensitive);
                name->addItems(sorted);
            }
            if(selectedScope==SWMM_HEAT_SCOPE_CELL2D || selectedScope==SWMM_HEAT_SCOPE_MESH_TAG) {
                int cells=0;
                if(swmm_2d_cell_count(m_engine,&cells)==SWMM_OK) {
                    if(selectedScope==SWMM_HEAT_SCOPE_CELL2D) {
                        // Avoid materializing millions of combo-box rows for
                        // a large mesh. The editable ID stays bounded by the
                        // current mesh cell count.
                        name->setValidator(new QIntValidator(1,cells,name));
                        if(name->lineEdit())name->lineEdit()->setPlaceholderText(tr("1-based cell ID"));
                    } else {
                        QSet<QString> tags;
                        for(int i=0;i<cells;++i) {
                            char tag[1024]{};
                            if(swmm_2d_get_triangle_tag(m_engine,i,tag,sizeof tag)==SWMM_OK && *tag)
                                tags.insert(QString::fromUtf8(tag));
                        }
                        QStringList sorted=tags.values(); sorted.sort(Qt::CaseInsensitive);
                        name->addItems(sorted);
                    }
                }
            }
        }
        name->setEditText(previous);
    };
    connect(scope,&QComboBox::currentIndexChanged,this,populate);
    auto *value=new PhysicalNumberSpin(m_elementTable);value->setRange(-1e12,1e12);value->setDecimals(12);
    auto configureValue=[attr,value] {
        const int attribute=attr->currentData().toInt();
        const auto& property=heatProperties()[attribute];
        value->setRange(property.minimum,property.maximum);
        if(attribute==SWMM_HEAT_ATTR_GROUND_SOURCE) {
            value->setDecimals(0);value->setSingleStep(1);
            value->setToolTip(QObject::tr("0 = prescribed ground temperature; 1 = exchange with mapped mesh groundwater."));
        } else {
            value->setDecimals(12);value->setSingleStep(0.1);value->setToolTip(QString());
        }
    };
    connect(attr,&QComboBox::currentIndexChanged,this,configureValue);configureValue();
    m_elementTable->setCellWidget(row,0,attr);m_elementTable->setCellWidget(row,1,scope);
    m_elementTable->setCellWidget(row,2,name);m_elementTable->setCellWidget(row,3,value);
    for(auto* combo:{attr,scope,name})connect(combo,&QComboBox::currentTextChanged,this,&HeatConfigDialog::invalidateElementPreview);
    connect(value,&QDoubleSpinBox::valueChanged,this,&HeatConfigDialog::invalidateElementPreview);
    name->setToolTip(tr("Choose an existing object or tag. Mesh region tags are independent of network tags; 2D cell IDs are one-based."));
    const QStringList labels={tr("Property"),tr("Scope"),tr("Object, tag or cell ID"),tr("Value in SI units or degrees Celsius")};
    for(int col=0;col<4;++col)m_elementTable->cellWidget(row,col)->setAccessibleName(tr("Override %1, %2").arg(row+1).arg(labels[col]));
}

void HeatConfigDialog::invalidateElementPreview() {
    if(!m_resolvedElements)return;
    m_resolvedElements->setRowCount(0);
    m_resolvedStatus->setText(tr("Draft changed. Preview again to resolve the pending values."));
}

void HeatConfigDialog::previewElements()
{
    m_resolvedElements->setRowCount(0);
    if(!m_engine||!validateDraft())return;
    if(engineSignature()!=m_engineInitial){reportFailure(m_elementTable,tr("Heat settings changed in another view. Reopen this editor."));return;}
    const int row=m_elementTable->currentRow();
    if(row<0){reportFailure(m_elementTable,tr("Select an override row to preview its property and scope."));return;}
    const int attr=qobject_cast<QComboBox*>(m_elementTable->cellWidget(row,0))->currentData().toInt();
    const int scope=qobject_cast<QComboBox*>(m_elementTable->cellWidget(row,1))->currentData().toInt();
    const auto target=qobject_cast<QComboBox*>(m_elementTable->cellWidget(row,2))->currentText();
    // Resolve the complete draft using the engine, then deliberately roll it
    // back. Preview cannot activate a process or author a configuration file.
    QProgressDialog meshProgress(tr("Resolving mesh-cell heat values…"),tr("Cancel"),0,0,this);
    meshProgress.setWindowModality(Qt::ApplicationModal);
    meshProgress.setMinimumDuration(450);
    int meshCells=0;
    QProgressDialog* progress=nullptr;
    if(scope==SWMM_HEAT_SCOPE_MESH_TAG && swmm_2d_cell_count(m_engine,&meshCells)==SWMM_OK && meshCells>5000) {
        meshProgress.setRange(0,meshCells);
        progress=&meshProgress;
    }
    struct Context { HeatConfigDialog* dialog; int attr; int scope; QString target; QProgressDialog* progress=nullptr; bool read=false; bool cancelled=false; int total=0; } ctx{this,attr,scope,target,progress};
    const int previewStatus=swmm_hydrothermal_edit_atomic(m_engine,[](SWMM_Engine engine,void* user)->int {
        auto& c=*static_cast<Context*>(user);auto* d=c.dialog;d->m_writeFailed=false;d->writeToEngine();
        if(d->m_writeFailed)return SWMM_ERR_BADPARAM;
        const auto append=[&](int kind,const QString& id) -> int {
            double value=0;int source=0;char origin[1024]{};
            const QByteArray encoded=id.toUtf8();
            if(swmm_heat_get_effective_element(engine,c.attr,kind,encoded.constData(),&value,&source,origin,sizeof origin)!=SWMM_OK)return SWMM_ERR_BADPARAM;
            ++c.total;
            if(d->m_resolvedElements->rowCount()<200){const int r=d->m_resolvedElements->rowCount();d->m_resolvedElements->insertRow(r);
                const QStringList values={id,std::isnan(value)?(source==5?tr("Time series"):tr("Air temperature")):QString::number(value,'g',17),QString::fromUtf8(origin)};
                for(int k=0;k<3;++k)d->m_resolvedElements->setItem(r,k,new QTableWidgetItem(values[k]));}
            return SWMM_OK;
        };
        if(c.scope==SWMM_HEAT_SCOPE_CELL2D || c.scope==SWMM_HEAT_SCOPE_MESH_TAG) {
            int cells=0;if(swmm_2d_cell_count(engine,&cells)!=SWMM_OK)return SWMM_ERR_BADPARAM;
            if(c.scope==SWMM_HEAT_SCOPE_CELL2D) {
                bool ok=false;const int cell=c.target.toInt(&ok)-1;
                if(!ok||cell<0||cell>=cells)return SWMM_ERR_BADPARAM;
                if(append(SWMM_HEAT_SCOPE_CELL2D,QString::number(cell+1))!=SWMM_OK)return SWMM_ERR_BADPARAM;
            } else {
                for(int i=0;i<cells;++i) {
                    char tag[1024]{};if(swmm_2d_get_triangle_tag(engine,i,tag,sizeof tag)!=SWMM_OK)return SWMM_ERR_BADPARAM;
                    if(QString::fromUtf8(tag)==c.target &&
                       append(SWMM_HEAT_SCOPE_CELL2D,QString::number(i+1))!=SWMM_OK)return SWMM_ERR_BADPARAM;
                    if(c.progress && ((i+1)%256==0 || i+1==cells)) {
                        c.progress->setValue(i+1);
                        if(c.progress->wasCanceled()) { c.cancelled=true; return SWMM_ERR_BADPARAM; }
                    }
                }
            }
        } else {
            for(int kind:{SWMM_HEAT_SCOPE_NODE,SWMM_HEAT_SCOPE_LINK}) {
                if((c.scope!=SWMM_HEAT_SCOPE_TAG&&c.scope!=kind)||(kind==SWMM_HEAT_SCOPE_NODE&&c.attr>=6))continue;
                const int n=kind==SWMM_HEAT_SCOPE_NODE?swmm_node_count(engine):swmm_link_count(engine);
                for(int i=0;i<n;++i){const char* id=kind==SWMM_HEAT_SCOPE_NODE?swmm_node_id(engine,i):swmm_link_id(engine,i);if(!id)continue;
                    char tag[1024]{};if(c.scope==SWMM_HEAT_SCOPE_TAG){const int rc=kind==SWMM_HEAT_SCOPE_NODE?swmm_node_get_tag(engine,i,tag,sizeof tag):swmm_link_get_tag(engine,i,tag,sizeof tag);if(rc!=SWMM_OK||QString::fromUtf8(tag)!=c.target)continue;}
                    else if(QString::fromUtf8(id)!=c.target)continue;
                    if(append(kind,QString::fromUtf8(id))!=SWMM_OK)return SWMM_ERR_BADPARAM;
                }
            }
        }
        c.read=true;return SWMM_ERR_BADPARAM;
    },&ctx);
    if(progress)progress->reset();
    if(ctx.read)m_resolvedStatus->setText(tr("Draft preview: %1; %2 affected objects, first %3 shown. Process activation is configured separately.").arg(heatPropertyLabel(attr)).arg(ctx.total).arg(m_resolvedElements->rowCount()));
    else if(ctx.cancelled)m_resolvedStatus->setText(tr("Preview cancelled. The draft remains unchanged and no settings were applied."));
    else m_resolvedStatus->setText(previewStatus==SWMM_OK?tr("No matching objects were found for this override."):tr("The preview could not be resolved. Check that the selected object or tag still exists."));
}

QString HeatConfigDialog::elementSignature() const
{
    QString result;
    for(int r=0;r<m_elementTable->rowCount();++r) {
        for(int c=0;c<3;++c) {
            const auto *combo=qobject_cast<QComboBox*>(m_elementTable->cellWidget(r,c));
            const QString v=c==2?combo->currentText():combo->currentData().toString();
            result+=QString::number(v.size())+':'+v+';';
        }
        result+=QString::number(OpenSWMM::Ui::preciseValue(qobject_cast<QDoubleSpinBox*>(m_elementTable->cellWidget(r,3))),'g',17)+'\n';
    }
    return result;
}

int HeatConfigDialog::writeElements()
{
    if(elementSignature()==m_elementsInitial)return 0;
    int count=0,writes=0;
    if(!checked(swmm_heat_element_override_count(m_engine,&count),m_elementTable,tr("Reading heat overrides")))return 0;
    for(int i=count-1;i>=0;--i) {
        if(!checked(swmm_heat_remove_element_override(m_engine,i),m_elementTable,tr("Removing heat override")))return writes;
        ++writes;
    }
    for(int r=0;r<m_elementTable->rowCount();++r) {
        auto combo=[&](int c){return qobject_cast<QComboBox*>(m_elementTable->cellWidget(r,c));};
        const double v=OpenSWMM::Ui::preciseValue(qobject_cast<QDoubleSpinBox*>(m_elementTable->cellWidget(r,3)));
        if(!checked(swmm_heat_set_element_override(m_engine,combo(0)->currentData().toInt(),combo(1)->currentData().toInt(),combo(2)->currentText().toUtf8().constData(),v),combo(2),tr("Saving spatial override %1 (check scope, name and value)").arg(r+1)))return writes;
        ++writes;
    }
    return writes;
}

int HeatConfigDialog::writeBed()
{
    int writes=0;
    for(int i=0;i<9;++i) {
        double current=0;
        if(!checked(swmm_heat_get_sediment(m_engine,i,&current),m_bedSpin[i],tr("Reading bed parameter")))return writes;
        const bool temperature=i==4 || i==8;
        int configured=0;
        if(temperature)swmm_heat_sediment_temperature_configured(m_engine,i,&configured);
        const bool wanted=i==4?m_groundConfigured->isChecked():i==8?m_initialConfigured->isChecked():true;
        if(temperature&&!wanted) {
            if(configured) {
                if(!checked(swmm_heat_clear_sediment_temperature(m_engine,i),m_bedSpin[i],tr("Restoring inherited bed temperature")))return writes;
                ++writes;
            }
            continue;
        }
        if(i==4 && m_groundMode->currentIndex()==1)continue;
        const double desired=OpenSWMM::Ui::preciseValue(m_bedSpin[i]);
        char series[4096]{};if(i==4)swmm_heat_get_sediment_ground_timeseries(m_engine,series,sizeof series);
        if(!changed(current,desired)&&(!temperature||configured)&&!*series)continue;
        if(!checked(swmm_heat_set_sediment(m_engine,i,desired),m_bedSpin[i],tr("Saving bed parameter %1").arg(i+1)))return writes;
        ++writes;
    }
    if(m_groundConfigured->isChecked()&&m_groundMode->currentIndex()==1) {
        char current[4096]{};swmm_heat_get_sediment_ground_timeseries(m_engine,current,sizeof current);
        const auto name=m_groundSeries->currentData().toString();
        if(name!=QString::fromUtf8(current)) {
            if(!checked(swmm_heat_set_sediment_ground_timeseries(m_engine,name.toUtf8().constData()),m_groundSeries,tr("Saving ground-temperature time series")))return writes;
            ++writes;
        }
    }
    return writes;
}

QString HeatConfigDialog::engineSignature() const { return configurationSignature(m_engine); }

QString HeatConfigDialog::configurationSignature(SWMM_Engine engine)
{
    if(!engine)return {};
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
    unsigned flags=0;swmm_heat_get_authoring_flags(engine,&flags);stream<<flags;
    const int nodes=swmm_node_count(engine),links=swmm_link_count(engine);stream<<nodes<<links;
    for(int i=0;i<nodes;++i){char tag[4096]{};swmm_node_get_tag(engine,i,tag,sizeof tag);
        stream<<QByteArray(swmm_node_id(engine,i))<<QByteArray(tag);}
    for(int i=0;i<links;++i){char tag[4096]{};swmm_link_get_tag(engine,i,tag,sizeof tag);
        stream<<QByteArray(swmm_link_id(engine,i))<<QByteArray(tag);}
    const int componentCount=swmm_process_component_count(engine);stream<<componentCount;
    for(int i=0;i<componentCount;++i){char id[1024]{},config[4096]{},resolved[4096]{};
        swmm_process_component_get(engine,i,id,sizeof id,config,sizeof config,resolved,sizeof resolved);
        stream<<QByteArray(id)<<QByteArray(config)<<QByteArray(resolved);}
    int value=0;swmm_heat_get_enabled(engine,&value);stream<<value;
    for(int i=0;i<4;++i){swmm_heat_get_module(engine,i,&value);stream<<value;}
    swmm_heat_get_dry_policy(engine,&value);stream<<value;
    for(int i=0;i<7;++i){double t=0;swmm_heat_get_source_configured(engine,i,&value);stream<<value;swmm_heat_get_source_temp(engine,i,&t);stream<<t;}
    int count=0;swmm_heat_node_override_count(engine,&count);stream<<count;
    for(int i=0;i<count;++i){int source=0,node=0;double t=0;swmm_heat_get_node_override(engine,i,&source,&node,&t);stream<<source<<node<<t;}
    for(int i=0;i<9;++i){double v=0;swmm_heat_get_radiative(engine,i,&v);stream<<v;swmm_heat_get_solar(engine,i,&v);stream<<v;swmm_heat_get_sediment(engine,i,&v);stream<<v;}
    for(int i:{4,8}){swmm_heat_sediment_temperature_configured(engine,i,&value);stream<<value;}
    swmm_heat_get_solar_sited(engine,&value);stream<<value;swmm_heat_get_shortwave_mode(engine,&value);stream<<value;
    swmm_heat_get_cloud_configured(engine,&value);stream<<value;
    for(int i=0;i<4;++i){double v=0;swmm_heat_get_cloud(engine,i,&v);stream<<v;}
    char name[4096]{};
    swmm_heat_get_shortwave_timeseries(engine,name,sizeof name);stream<<QByteArray(name);
    swmm_heat_get_cloud_timeseries(engine,name,sizeof name);stream<<QByteArray(name);
    swmm_heat_get_sediment_ground_timeseries(engine,name,sizeof name);stream<<QByteArray(name);
    swmm_heat_element_override_count(engine,&count);stream<<count;
    for(int i=0;i<count;++i){int attr=0,scope=0;double v=0;swmm_heat_get_element_override(engine,i,&attr,&scope,name,sizeof name,&v);stream<<attr<<scope<<QByteArray(name)<<v;}
    return QString::fromLatin1(bytes.toBase64());
}

} // namespace OpenSWMMVis
