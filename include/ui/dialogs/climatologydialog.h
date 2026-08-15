/*!
 * \file   climatologydialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Climatology editor — edits the model's [TEMPERATURE], [EVAPORATION],
 * [WINDSPEED], SNOWMELT, areal-depletion (ADC) and [ADJUSTMENTS] configuration
 * via the engine's swmm_climate_* C API. Six tabs mirror the legacy EPA SWMM 5
 * Climatology editor (Dclimate): Temperature, Evaporation, Wind Speed,
 * Snow Melt, Areal Depletion, Adjustments. Reads on construction, writes on OK.
 */
#ifndef CLIMATOLOGYDIALOG_H
#define CLIMATOLOGYDIALOG_H

#include <QDialog>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>

class QTabWidget;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QDateEdit;
class QDoubleSpinBox;
class QTableWidget;
class QStackedWidget;

class SWMMModelLayer;

/*!
 * \class ClimatologyDialog
 * \brief Modal editor for the model climatology configuration.
 */
class ClimatologyDialog : public QDialog
{
    Q_OBJECT

public:
    /*! Tab indices (legacy order). */
    enum Tab {
        TabTemperature = 0,
        TabEvaporation = 1,
        TabWind        = 2,
        TabSnowMelt    = 3,
        TabArealDepletion = 4,
        TabAdjustments = 5
    };

    explicit ClimatologyDialog(SWMM_Engine engine,
                               SWMMModelLayer *layer = nullptr,
                               QWidget *parent = nullptr);
    ~ClimatologyDialog() override = default;

    /*! True after OK wrote at least one changed value. */
    [[nodiscard]] bool wroteAnyChanges() const { return m_wroteChanges; }

    /*! Select the tab a toolbar button maps to. */
    void setCurrentTab(int idx);

private slots:
    void onAccept();
    void onEvapTypeChanged();
    void onTempSourceChanged();
    void onWindTypeChanged();
    void onBrowseTempFile();
    void onAdcPreset(int column, bool natural);
    void onClearAdjustments();

private:
    void buildUi();
    void buildTemperatureTab(QTabWidget *tabs);
    void buildEvaporationTab(QTabWidget *tabs);
    void buildWindTab(QTabWidget *tabs);
    void buildSnowTab(QTabWidget *tabs);
    void buildAdcTab(QTabWidget *tabs);
    void buildAdjustmentsTab(QTabWidget *tabs);

    void readFromEngine();
    void writeToEngine();
    /*! Serialize all widget values to a string, to detect real edits on OK. */
    [[nodiscard]] QString serialize() const;

    [[nodiscard]] bool isSI() const { return m_unitSystem == 1; }
    void populateTimeseriesCombo(QComboBox *combo) const;
    void populatePatternCombo(QComboBox *combo) const;

    SWMM_Engine     m_engine = nullptr;
    SWMMModelLayer *m_layer  = nullptr;
    QTabWidget     *m_tabs   = nullptr;
    bool            m_wroteChanges = false;
    int             m_unitSystem = 0;   // 0=US, 1=SI
    QString         m_initialSig;       // widget signature captured after read

    // --- Temperature ---
    QComboBox      *m_tempSource     = nullptr;  // 0 None / 1 TimeSeries / 2 File
    QComboBox      *m_tempTs         = nullptr;
    QLineEdit      *m_tempFile       = nullptr;
    QCheckBox      *m_tempStartCheck = nullptr;
    QDateEdit      *m_tempStartDate  = nullptr;
    QComboBox      *m_tempUnits      = nullptr;  // -1 Auto / 0 C10 / 1 C / 2 F

    // --- Evaporation ---
    QComboBox      *m_evapType       = nullptr;  // engine enum via itemData
    QStackedWidget *m_evapStack      = nullptr;
    QDoubleSpinBox *m_evapConstant   = nullptr;
    QTableWidget   *m_evapMonthly    = nullptr;  // 12 rows
    QComboBox      *m_evapTs         = nullptr;
    QTableWidget   *m_panCoeff       = nullptr;  // 12 rows
    QComboBox      *m_recovery       = nullptr;
    QCheckBox      *m_dryOnly        = nullptr;

    // --- Wind ---
    QComboBox      *m_windType       = nullptr;  // 0 Monthly / 1 File
    QTableWidget   *m_windMonthly    = nullptr;  // 12 rows

    // --- Snow Melt ---
    QDoubleSpinBox *m_snowTemp       = nullptr;
    QDoubleSpinBox *m_atiWeight      = nullptr;
    QDoubleSpinBox *m_negMelt        = nullptr;
    QDoubleSpinBox *m_elevation      = nullptr;
    QDoubleSpinBox *m_latitude       = nullptr;
    QDoubleSpinBox *m_longitude      = nullptr;

    // --- Areal depletion ---
    QTableWidget   *m_adc            = nullptr;  // 10 rows x 2 cols (imperv, perv)

    // --- Adjustments ---
    QTableWidget   *m_adjust         = nullptr;  // 12 rows x 4 cols
};

#endif // CLIMATOLOGYDIALOG_H
