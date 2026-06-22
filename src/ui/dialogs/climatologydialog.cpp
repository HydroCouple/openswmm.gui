/*!
 * \file   climatologydialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \license GPL-3.0-or-later
 *
 * \see climatologydialog.h
 * \see Legacy reference: SWMM-GUI/Epaswmm5/Dclimate.pas (6-tab Climatology editor)
 * \see Engine API: openswmm/engine/openswmm_climate.h
 */
#include "ui/dialogs/climatologydialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_climate.h>
#include <openswmm/engine/openswmm_model.h>

namespace {

const char *const kMonths[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// Legacy ADC presets (objprops.txt DefADCurve / NatADCurve).
const double kDefADCurve[10] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
const double kNatADCurve[10] = {0.10, 0.35, 0.53, 0.66, 0.75,
                                0.82, 0.87, 0.92, 0.95, 0.98};

// Build a single-column, 12-row monthly grid (vertical header = month names).
QTableWidget *makeMonthlyTable(const QString &header)
{
    auto *t = new QTableWidget(12, 1);
    t->setHorizontalHeaderLabels(QStringList{header});
    QStringList rows;
    for (const char *m : kMonths) rows << QString::fromLatin1(m);
    t->setVerticalHeaderLabels(rows);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int i = 0; i < 12; ++i)
        t->setItem(i, 0, new QTableWidgetItem(QStringLiteral("0")));
    return t;
}

double cellValue(QTableWidget *t, int row, int col)
{
    auto *it = t->item(row, col);
    return it ? it->text().toDouble() : 0.0;
}

void setCell(QTableWidget *t, int row, int col, double v)
{
    auto *it = t->item(row, col);
    if (!it) { it = new QTableWidgetItem; t->setItem(row, col, it); }
    it->setText(QString::number(v, 'g', 10));
}

void readColumn(QTableWidget *t, int col, double *out, int n)
{
    for (int i = 0; i < n; ++i) out[i] = cellValue(t, i, col);
}

void writeColumn(QTableWidget *t, int col, const double *in, int n)
{
    for (int i = 0; i < n; ++i) setCell(t, i, col, in[i]);
}

} // namespace

ClimatologyDialog::ClimatologyDialog(SWMM_Engine engine, SWMMModelLayer *layer,
                                     QWidget *parent)
    : QDialog(parent), m_engine(engine), m_layer(layer)
{
    setWindowTitle(tr("Climatology"));
    int us = 0;
    if (m_engine) swmm_get_unit_system(m_engine, &us);
    m_unitSystem = us;

    buildUi();
    readFromEngine();
    m_initialSig = serialize();
}

void ClimatologyDialog::setCurrentTab(int idx)
{
    if (m_tabs && idx >= 0 && idx < m_tabs->count())
        m_tabs->setCurrentIndex(idx);
}

void ClimatologyDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    outer->addWidget(m_tabs);

    buildTemperatureTab(m_tabs);
    buildEvaporationTab(m_tabs);
    buildWindTab(m_tabs);
    buildSnowTab(m_tabs);
    buildAdcTab(m_tabs);
    buildAdjustmentsTab(m_tabs);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                    this);
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &ClimatologyDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(560, 460);
}

// ---------------------------------------------------------------------------
// Temperature
// ---------------------------------------------------------------------------

void ClimatologyDialog::buildTemperatureTab(QTabWidget *tabs)
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);

    m_tempSource = new QComboBox;
    m_tempSource->setObjectName(QStringLiteral("clim_tempSource"));
    m_tempSource->addItem(tr("No Data"));            // 0
    m_tempSource->addItem(tr("Time Series"));        // 1
    m_tempSource->addItem(tr("External Climate File")); // 2
    form->addRow(tr("Source of Temperature Data:"), m_tempSource);

    m_tempTs = new QComboBox;
    m_tempTs->setEditable(true);
    populateTimeseriesCombo(m_tempTs);
    form->addRow(tr("Time Series:"), m_tempTs);

    auto *fileRow = new QWidget;
    auto *fileLay = new QHBoxLayout(fileRow);
    fileLay->setContentsMargins(0, 0, 0, 0);
    m_tempFile = new QLineEdit;
    auto *browse = new QPushButton(tr("Browse…"));
    fileLay->addWidget(m_tempFile);
    fileLay->addWidget(browse);
    form->addRow(tr("Climate File:"), fileRow);
    connect(browse, &QPushButton::clicked, this, &ClimatologyDialog::onBrowseTempFile);

    m_tempStartCheck = new QCheckBox(tr("Start reading file at"));
    m_tempStartDate = new QDateEdit;
    m_tempStartDate->setCalendarPopup(true);
    m_tempStartDate->setDisplayFormat(QStringLiteral("MM/dd/yyyy"));
    m_tempStartDate->setEnabled(false);
    connect(m_tempStartCheck, &QCheckBox::toggled, m_tempStartDate, &QWidget::setEnabled);
    auto *startRow = new QWidget;
    auto *startLay = new QHBoxLayout(startRow);
    startLay->setContentsMargins(0, 0, 0, 0);
    startLay->addWidget(m_tempStartCheck);
    startLay->addWidget(m_tempStartDate);
    form->addRow(QString(), startRow);

    m_tempUnits = new QComboBox;
    m_tempUnits->addItem(tr("Auto / file default"), -1);
    m_tempUnits->addItem(tr("Tenths of degrees Celsius (C10)"), 0);
    m_tempUnits->addItem(tr("Degrees Celsius (C)"), 1);
    m_tempUnits->addItem(tr("Degrees Fahrenheit (F)"), 2);
    form->addRow(tr("Climate-file units:"), m_tempUnits);

    connect(m_tempSource, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ClimatologyDialog::onTempSourceChanged);

    tabs->addTab(page, tr("Temperature"));
}

void ClimatologyDialog::onTempSourceChanged()
{
    const int src = m_tempSource ? m_tempSource->currentIndex() : 0;
    if (m_tempTs)    m_tempTs->setEnabled(src == 1);
    const bool file = (src == 2);
    if (m_tempFile)  m_tempFile->setEnabled(file);
    if (m_tempStartCheck) m_tempStartCheck->setEnabled(file);
    if (m_tempStartDate)  m_tempStartDate->setEnabled(file && m_tempStartCheck->isChecked());
    if (m_tempUnits) m_tempUnits->setEnabled(file);
}

void ClimatologyDialog::onBrowseTempFile()
{
    const QString fn = QFileDialog::getOpenFileName(
        this, tr("Select Climate File"), m_tempFile ? m_tempFile->text() : QString(),
        tr("Data files (*.dat *.txt);;All files (*)"));
    if (!fn.isEmpty() && m_tempFile) m_tempFile->setText(fn);
}

// ---------------------------------------------------------------------------
// Evaporation
// ---------------------------------------------------------------------------

void ClimatologyDialog::buildEvaporationTab(QTabWidget *tabs)
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);

    m_evapType = new QComboBox;
    m_evapType->setObjectName(QStringLiteral("clim_evapType"));
    m_evapType->addItem(tr("Constant Value"), 0);          // CONSTANT
    m_evapType->addItem(tr("Monthly Averages"), 1);        // MONTHLY
    m_evapType->addItem(tr("Time Series"), 2);             // TIMESERIES
    m_evapType->addItem(tr("Temperatures (Hargreaves)"), 3); // TEMPERATURE
    m_evapType->addItem(tr("Climate File (pan)"), 4);      // FILE/PAN
    form->addRow(tr("Source of Evaporation Rates:"), m_evapType);

    // Stacked controls — page index == engine evap_type enum.
    m_evapStack = new QStackedWidget;
    const QString rate = isSI() ? tr("mm/day") : tr("in/day");

    // 0 CONSTANT
    {
        auto *w = new QWidget; auto *l = new QFormLayout(w);
        m_evapConstant = new QDoubleSpinBox;
        m_evapConstant->setRange(0.0, 1.0e6);
        m_evapConstant->setDecimals(4);
        l->addRow(tr("Evaporation (%1):").arg(rate), m_evapConstant);
        m_evapStack->addWidget(w);
    }
    // 1 MONTHLY
    {
        auto *w = new QWidget; auto *l = new QVBoxLayout(w);
        l->addWidget(new QLabel(tr("Monthly Evaporation (%1)").arg(rate)));
        m_evapMonthly = makeMonthlyTable(rate);
        l->addWidget(m_evapMonthly);
        m_evapStack->addWidget(w);
    }
    // 2 TIMESERIES
    {
        auto *w = new QWidget; auto *l = new QFormLayout(w);
        m_evapTs = new QComboBox; m_evapTs->setEditable(true);
        populateTimeseriesCombo(m_evapTs);
        l->addRow(tr("Time Series:"), m_evapTs);
        m_evapStack->addWidget(w);
    }
    // 3 TEMPERATURE
    {
        auto *w = new QWidget; auto *l = new QVBoxLayout(w);
        auto *lbl = new QLabel(tr("Evaporation is computed from the daily "
                                  "temperatures in the climate file selected on "
                                  "the Temperature page (Hargreaves method)."));
        lbl->setWordWrap(true);
        l->addWidget(lbl);
        m_evapStack->addWidget(w);
    }
    // 4 FILE / PAN
    {
        auto *w = new QWidget; auto *l = new QVBoxLayout(w);
        l->addWidget(new QLabel(tr("Monthly Pan Coefficients")));
        m_panCoeff = makeMonthlyTable(tr("Pan Coeff"));
        l->addWidget(m_panCoeff);
        m_evapStack->addWidget(w);
    }
    form->addRow(m_evapStack);

    m_recovery = new QComboBox; m_recovery->setEditable(true);
    m_recovery->addItem(QString());  // empty = none
    populatePatternCombo(m_recovery);
    form->addRow(tr("Soil Recovery Pattern (optional):"), m_recovery);

    m_dryOnly = new QCheckBox(tr("Evaporate only during dry periods"));
    form->addRow(QString(), m_dryOnly);

    connect(m_evapType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ClimatologyDialog::onEvapTypeChanged);

    tabs->addTab(page, tr("Evaporation"));
}

void ClimatologyDialog::onEvapTypeChanged()
{
    if (!m_evapType || !m_evapStack) return;
    const int type = m_evapType->currentData().toInt();
    if (type >= 0 && type < m_evapStack->count())
        m_evapStack->setCurrentIndex(type);
}

// ---------------------------------------------------------------------------
// Wind
// ---------------------------------------------------------------------------

void ClimatologyDialog::buildWindTab(QTabWidget *tabs)
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);

    m_windType = new QComboBox;
    m_windType->addItem(tr("Monthly Averages"), 0);
    m_windType->addItem(tr("Use Climate File (see Temperature page)"), 1);
    auto *form = new QFormLayout;
    form->addRow(tr("Source of Wind Speed:"), m_windType);
    lay->addLayout(form);

    const QString unit = isSI() ? tr("km/hr") : tr("mph");
    lay->addWidget(new QLabel(tr("Monthly Wind Speed (%1)").arg(unit)));
    m_windMonthly = makeMonthlyTable(unit);
    lay->addWidget(m_windMonthly);

    connect(m_windType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ClimatologyDialog::onWindTypeChanged);

    tabs->addTab(page, tr("Wind Speed"));
}

void ClimatologyDialog::onWindTypeChanged()
{
    if (m_windMonthly && m_windType)
        m_windMonthly->setEnabled(m_windType->currentData().toInt() == 0);
}

// ---------------------------------------------------------------------------
// Snow Melt
// ---------------------------------------------------------------------------

void ClimatologyDialog::buildSnowTab(QTabWidget *tabs)
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);

    const QString tUnit = isSI() ? tr("deg C") : tr("deg F");
    const QString eUnit = isSI() ? tr("meters") : tr("feet");

    m_snowTemp = new QDoubleSpinBox;
    m_snowTemp->setObjectName(QStringLiteral("clim_snowTemp"));
    m_snowTemp->setRange(-100.0, 100.0); m_snowTemp->setDecimals(2);
    form->addRow(tr("Dividing Temperature (%1):").arg(tUnit), m_snowTemp);

    m_atiWeight = new QDoubleSpinBox;
    m_atiWeight->setRange(0.0, 1.0); m_atiWeight->setDecimals(2); m_atiWeight->setSingleStep(0.05);
    form->addRow(tr("ATI Weight (fraction):"), m_atiWeight);

    m_negMelt = new QDoubleSpinBox;
    m_negMelt->setRange(0.0, 1.0); m_negMelt->setDecimals(2); m_negMelt->setSingleStep(0.05);
    form->addRow(tr("Negative Melt Ratio (fraction):"), m_negMelt);

    m_elevation = new QDoubleSpinBox;
    m_elevation->setRange(-1.0e5, 1.0e5); m_elevation->setDecimals(2);
    form->addRow(tr("Elevation above MSL (%1):").arg(eUnit), m_elevation);

    m_latitude = new QDoubleSpinBox;
    m_latitude->setObjectName(QStringLiteral("clim_latitude"));
    m_latitude->setRange(-90.0, 90.0); m_latitude->setDecimals(2);
    form->addRow(tr("Latitude (degrees):"), m_latitude);

    m_longitude = new QDoubleSpinBox;
    m_longitude->setRange(-1440.0, 1440.0); m_longitude->setDecimals(1);
    form->addRow(tr("Longitude Correction (+/- minutes):"), m_longitude);

    tabs->addTab(page, tr("Snow Melt"));
}

// ---------------------------------------------------------------------------
// Areal Depletion
// ---------------------------------------------------------------------------

void ClimatologyDialog::buildAdcTab(QTabWidget *tabs)
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel(tr("Fraction of Area Covered by Snow")));

    m_adc = new QTableWidget(10, 2);
    m_adc->setHorizontalHeaderLabels(QStringList{tr("Impervious"), tr("Pervious")});
    QStringList ratios;
    for (int i = 0; i < 10; ++i) ratios << QString::number(i * 0.1, 'f', 1);
    m_adc->setVerticalHeaderLabels(ratios);
    m_adc->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < 2; ++c)
            m_adc->setItem(r, c, new QTableWidgetItem(QStringLiteral("1")));
    lay->addWidget(m_adc);

    auto *btns = new QGridLayout;
    auto *iNo = new QPushButton(tr("Impervious: No Depletion"));
    auto *iNat = new QPushButton(tr("Impervious: Natural Area"));
    auto *pNo = new QPushButton(tr("Pervious: No Depletion"));
    auto *pNat = new QPushButton(tr("Pervious: Natural Area"));
    btns->addWidget(iNo, 0, 0); btns->addWidget(iNat, 0, 1);
    btns->addWidget(pNo, 1, 0); btns->addWidget(pNat, 1, 1);
    lay->addLayout(btns);
    connect(iNo,  &QPushButton::clicked, this, [this]{ onAdcPreset(0, false); });
    connect(iNat, &QPushButton::clicked, this, [this]{ onAdcPreset(0, true); });
    connect(pNo,  &QPushButton::clicked, this, [this]{ onAdcPreset(1, false); });
    connect(pNat, &QPushButton::clicked, this, [this]{ onAdcPreset(1, true); });

    tabs->addTab(page, tr("Areal Depletion"));
}

void ClimatologyDialog::onAdcPreset(int column, bool natural)
{
    const double *src = natural ? kNatADCurve : kDefADCurve;
    writeColumn(m_adc, column, src, 10);
}

// ---------------------------------------------------------------------------
// Adjustments
// ---------------------------------------------------------------------------

void ClimatologyDialog::buildAdjustmentsTab(QTabWidget *tabs)
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);

    m_adjust = new QTableWidget(12, 4);
    m_adjust->setHorizontalHeaderLabels(
        QStringList{tr("Temp"), tr("Evap"), tr("Rain"), tr("Cond")});
    QStringList rows;
    for (const char *m : kMonths) rows << QString::fromLatin1(m);
    m_adjust->setVerticalHeaderLabels(rows);
    m_adjust->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int r = 0; r < 12; ++r)
        for (int c = 0; c < 4; ++c)
            m_adjust->setItem(r, c, new QTableWidgetItem(
                QStringLiteral("%1").arg(c == 0 ? 0 : 1)));
    lay->addWidget(new QLabel(tr("Temp = +/- offset; Evap = multiplier; "
                                 "Rain = multiplier; Cond = multiplier.")));
    lay->addWidget(m_adjust);

    auto *clear = new QPushButton(tr("Clear All"));
    lay->addWidget(clear, 0, Qt::AlignLeft);
    connect(clear, &QPushButton::clicked, this, &ClimatologyDialog::onClearAdjustments);

    tabs->addTab(page, tr("Adjustments"));
}

void ClimatologyDialog::onClearAdjustments()
{
    for (int r = 0; r < 12; ++r) {
        setCell(m_adjust, r, 0, 0.0);   // temp offset
        setCell(m_adjust, r, 1, 1.0);   // evap mult
        setCell(m_adjust, r, 2, 1.0);   // rain mult
        setCell(m_adjust, r, 3, 1.0);   // cond mult
    }
}

// ---------------------------------------------------------------------------
// Combo population
// ---------------------------------------------------------------------------

void ClimatologyDialog::populateTimeseriesCombo(QComboBox *combo) const
{
    if (!m_engine || !combo) return;
    const int n = swmm_table_count(m_engine);
    for (int i = 0; i < n; ++i) {
        const char *id = swmm_table_id(m_engine, i);
        if (id) combo->addItem(QString::fromUtf8(id));
    }
}

void ClimatologyDialog::populatePatternCombo(QComboBox *combo) const
{
    if (!m_engine || !combo) return;
    const int n = swmm_pattern_count(m_engine);
    for (int i = 0; i < n; ++i) {
        const char *id = swmm_pattern_id(m_engine, i);
        if (id) combo->addItem(QString::fromUtf8(id));
    }
}

// ---------------------------------------------------------------------------
// Engine I/O
// ---------------------------------------------------------------------------

void ClimatologyDialog::readFromEngine()
{
    if (!m_engine) return;
    int i = 0; double d = 0.0; char buf[512] = {};

    // Temperature
    if (swmm_climate_get_temp_source(m_engine, &i) == SWMM_OK)
        m_tempSource->setCurrentIndex(qBound(0, i, 2));
    if (swmm_climate_get_temp_timeseries(m_engine, buf, sizeof(buf)) == SWMM_OK)
        m_tempTs->setCurrentText(QString::fromUtf8(buf));
    char absb[512] = {}, orig[512] = {};
    if (swmm_file_path_get(m_engine, SWMM_FILE_CLIMATE_TEMP, "",
                           absb, sizeof(absb), orig, sizeof(orig)) == SWMM_OK) {
        const QString shown = orig[0] ? QString::fromUtf8(orig)
                                      : QString::fromUtf8(absb);
        m_tempFile->setText(shown);
    }
    if (swmm_climate_get_temp_file_start(m_engine, &d) == SWMM_OK && d > 0.0) {
        m_tempStartCheck->setChecked(true);
        m_tempStartDate->setDate(QDate(1899, 12, 30).addDays(static_cast<qint64>(d)));
    }
    if (swmm_climate_get_temp_units(m_engine, &i) == SWMM_OK) {
        const int idx = m_tempUnits->findData(i);
        m_tempUnits->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    // Evaporation
    if (swmm_climate_get_evap_type(m_engine, &i) == SWMM_OK) {
        const int idx = m_evapType->findData(i);
        if (idx >= 0) m_evapType->setCurrentIndex(idx);
    }
    {
        double m[12] = {};
        if (swmm_climate_get_evap_monthly(m_engine, m, 12) == SWMM_OK) {
            writeColumn(m_evapMonthly, 0, m, 12);
            m_evapConstant->setValue(m[0]);
        }
        double pc[12] = {};
        if (swmm_climate_get_pan_coeff(m_engine, pc, 12) == SWMM_OK)
            writeColumn(m_panCoeff, 0, pc, 12);
    }
    if (swmm_climate_get_evap_timeseries(m_engine, buf, sizeof(buf)) == SWMM_OK)
        m_evapTs->setCurrentText(QString::fromUtf8(buf));
    if (swmm_climate_get_evap_recovery(m_engine, buf, sizeof(buf)) == SWMM_OK)
        m_recovery->setCurrentText(QString::fromUtf8(buf));
    if (swmm_climate_get_dry_only(m_engine, &i) == SWMM_OK)
        m_dryOnly->setChecked(i != 0);
    onEvapTypeChanged();

    // Wind
    if (swmm_climate_get_wind_type(m_engine, &i) == SWMM_OK) {
        const int idx = m_windType->findData(i);
        if (idx >= 0) m_windType->setCurrentIndex(idx);
    }
    {
        double w[12] = {};
        if (swmm_climate_get_wind_monthly(m_engine, w, 12) == SWMM_OK)
            writeColumn(m_windMonthly, 0, w, 12);
    }
    onWindTypeChanged();

    // Snow melt
    if (swmm_climate_get_snow_temp(m_engine, &d) == SWMM_OK) m_snowTemp->setValue(d);
    if (swmm_climate_get_ati_weight(m_engine, &d) == SWMM_OK) m_atiWeight->setValue(d);
    if (swmm_climate_get_neg_melt_ratio(m_engine, &d) == SWMM_OK) m_negMelt->setValue(d);
    if (swmm_climate_get_elevation(m_engine, &d) == SWMM_OK) m_elevation->setValue(d);
    if (swmm_climate_get_latitude(m_engine, &d) == SWMM_OK) m_latitude->setValue(d);
    if (swmm_climate_get_longitude_correction(m_engine, &d) == SWMM_OK) m_longitude->setValue(d);

    // Areal depletion
    {
        double imp[10] = {}, prv[10] = {};
        if (swmm_climate_get_adc_impervious(m_engine, imp, 10) == SWMM_OK)
            writeColumn(m_adc, 0, imp, 10);
        if (swmm_climate_get_adc_pervious(m_engine, prv, 10) == SWMM_OK)
            writeColumn(m_adc, 1, prv, 10);
    }

    // Adjustments
    {
        double t[12] = {}, e[12] = {}, r[12] = {}, c[12] = {};
        swmm_climate_get_adjust_temperature(m_engine, t, 12);
        swmm_climate_get_adjust_evaporation(m_engine, e, 12);
        swmm_climate_get_adjust_rainfall(m_engine, r, 12);
        swmm_climate_get_adjust_conductivity(m_engine, c, 12);
        for (int k = 0; k < 12; ++k) {
            setCell(m_adjust, k, 0, t[k]);
            setCell(m_adjust, k, 1, e[k]);
            setCell(m_adjust, k, 2, r[k]);
            setCell(m_adjust, k, 3, c[k]);
        }
    }

    onTempSourceChanged();
}

void ClimatologyDialog::writeToEngine()
{
    if (!m_engine) return;

    // Temperature
    swmm_climate_set_temp_source(m_engine, m_tempSource->currentIndex());
    if (!m_tempTs->currentText().trimmed().isEmpty())
        swmm_climate_set_temp_timeseries(m_engine,
            m_tempTs->currentText().trimmed().toUtf8().constData());
    if (m_tempSource->currentIndex() == 2 && !m_tempFile->text().trimmed().isEmpty())
        swmm_file_path_set(m_engine, SWMM_FILE_CLIMATE_TEMP, "",
                           m_tempFile->text().trimmed().toUtf8().constData());
    if (m_tempStartCheck->isChecked())
        swmm_climate_set_temp_file_start(m_engine,
            static_cast<double>(QDate(1899, 12, 30).daysTo(m_tempStartDate->date())));
    else
        swmm_climate_set_temp_file_start(m_engine, 0.0);
    swmm_climate_set_temp_units(m_engine, m_tempUnits->currentData().toInt());

    // Evaporation
    const int evapType = m_evapType->currentData().toInt();
    swmm_climate_set_evap_type(m_engine, evapType);
    {
        double m[12] = {};
        if (evapType == 0) {
            for (int k = 0; k < 12; ++k) m[k] = m_evapConstant->value();
        } else {
            readColumn(m_evapMonthly, 0, m, 12);
        }
        swmm_climate_set_evap_monthly(m_engine, m, 12);
        double pc[12] = {};
        readColumn(m_panCoeff, 0, pc, 12);
        swmm_climate_set_pan_coeff(m_engine, pc, 12);
    }
    if (evapType == 2 && !m_evapTs->currentText().trimmed().isEmpty())
        swmm_climate_set_evap_timeseries(m_engine,
            m_evapTs->currentText().trimmed().toUtf8().constData());
    swmm_climate_set_evap_recovery(m_engine,
        m_recovery->currentText().trimmed().toUtf8().constData());
    swmm_climate_set_dry_only(m_engine, m_dryOnly->isChecked() ? 1 : 0);

    // Wind
    swmm_climate_set_wind_type(m_engine, m_windType->currentData().toInt());
    {
        double w[12] = {};
        readColumn(m_windMonthly, 0, w, 12);
        swmm_climate_set_wind_monthly(m_engine, w, 12);
    }

    // Snow melt
    swmm_climate_set_snow_temp(m_engine, m_snowTemp->value());
    swmm_climate_set_ati_weight(m_engine, m_atiWeight->value());
    swmm_climate_set_neg_melt_ratio(m_engine, m_negMelt->value());
    swmm_climate_set_elevation(m_engine, m_elevation->value());
    swmm_climate_set_latitude(m_engine, m_latitude->value());
    swmm_climate_set_longitude_correction(m_engine, m_longitude->value());

    // Areal depletion
    {
        double imp[10] = {}, prv[10] = {};
        readColumn(m_adc, 0, imp, 10);
        readColumn(m_adc, 1, prv, 10);
        swmm_climate_set_adc_impervious(m_engine, imp, 10);
        swmm_climate_set_adc_pervious(m_engine, prv, 10);
    }

    // Adjustments
    {
        double t[12] = {}, e[12] = {}, r[12] = {}, c[12] = {};
        for (int k = 0; k < 12; ++k) {
            t[k] = cellValue(m_adjust, k, 0);
            e[k] = cellValue(m_adjust, k, 1);
            r[k] = cellValue(m_adjust, k, 2);
            c[k] = cellValue(m_adjust, k, 3);
        }
        swmm_climate_set_adjust_temperature(m_engine, t, 12);
        swmm_climate_set_adjust_evaporation(m_engine, e, 12);
        swmm_climate_set_adjust_rainfall(m_engine, r, 12);
        swmm_climate_set_adjust_conductivity(m_engine, c, 12);
    }
}

QString ClimatologyDialog::serialize() const
{
    QString s;
    s += QString::number(m_tempSource->currentIndex()) + '|';
    s += m_tempTs->currentText() + '|';
    s += m_tempFile->text() + '|';
    s += (m_tempStartCheck->isChecked()
              ? m_tempStartDate->date().toString(Qt::ISODate) : QString()) + '|';
    s += QString::number(m_tempUnits->currentData().toInt()) + '|';
    s += QString::number(m_evapType->currentData().toInt()) + '|';
    s += QString::number(m_evapConstant->value()) + '|';
    s += m_evapTs->currentText() + '|';
    s += m_recovery->currentText() + '|';
    s += QString(m_dryOnly->isChecked() ? "1" : "0") + '|';
    s += QString::number(m_windType->currentData().toInt()) + '|';
    s += QString::number(m_snowTemp->value()) + '|';
    s += QString::number(m_atiWeight->value()) + '|';
    s += QString::number(m_negMelt->value()) + '|';
    s += QString::number(m_elevation->value()) + '|';
    s += QString::number(m_latitude->value()) + '|';
    s += QString::number(m_longitude->value()) + '|';
    const auto grid = [&s](QTableWidget *t, int rows, int cols) {
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                s += (t->item(r, c) ? t->item(r, c)->text() : QString()) + ',';
    };
    grid(m_evapMonthly, 12, 1);
    grid(m_panCoeff, 12, 1);
    grid(m_windMonthly, 12, 1);
    grid(m_adc, 10, 2);
    grid(m_adjust, 12, 4);
    return s;
}

void ClimatologyDialog::onAccept()
{
    if (serialize() != m_initialSig) {
        writeToEngine();
        m_wroteChanges = true;
    }
    accept();
}
