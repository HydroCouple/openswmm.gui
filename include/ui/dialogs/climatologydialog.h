/**
 * @file      climatologydialog.h
 * @brief     Climatology editor — edits the model's [TEMPERATURE], [EVAPORATION], [WINDSPEED],
 *            HUMIDITY, SNOWMELT, areal-depletion (ADC) and [ADJUSTMENTS] configuration via the
 *            engine's swmm_climate_* C API.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Climatology editor — edits the model's [TEMPERATURE], [EVAPORATION],
 * [WINDSPEED], HUMIDITY, SNOWMELT, areal-depletion (ADC) and [ADJUSTMENTS]
 * configuration via the engine's swmm_climate_* C API. Six tabs mirror the
 * legacy EPA SWMM 5 Climatology editor (Dclimate): Temperature, Evaporation,
 * Wind Speed, Snow Melt, Areal Depletion, Adjustments; a Humidity tab (the
 * heat model's relative-humidity / dew-point input) sits after Wind Speed,
 * and Radiation adds shared snow/heat site, met-file and cloud settings.
 * Reads on construction; validates and atomically writes changed settings on OK.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef CLIMATOLOGYDIALOG_H
#define CLIMATOLOGYDIALOG_H

#include <QDialog>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>
#include "ui/models/snowclimatesettings.h"

class QTabWidget;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QDateEdit;
class QDateTimeEdit;
class QDoubleSpinBox;
class QTableWidget;
class QStackedWidget;

class SWMMModelLayer;

namespace openswmmvis::ui
{
class RelativePathPicker;
}

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
        TabWind = 2,
        TabHumidity = 3,
        TabSnowMelt = 4,
        TabArealDepletion = 5,
        TabAdjustments = 6,
        TabRadiation = 7
    };

    /**
     * @brief Build a staged climatology/snow/radiation editor and capture its displayed
     * baseline.
     * @param[in] engine Nullable borrowed engine; caller must keep it live until
     * invalidateEngine() or destruction. No automatic engine-close subscription is installed
     * here.
     * @param[in] layer Optional borrowed layer stored for caller context; this implementation
     * does not dereference it or take ownership.
     * @param[in] parent Optional Qt parent owning the dialog.
     * @details GUI-thread widget/API access only. Reads the project unit-system flag (US
     * fallback), hydrates eight tabs, records change signatures and absolute climate-path
     * baseline. Constructor does not publish authoring setters; Cancel normally discards the
     * local draft.
     */
    explicit ClimatologyDialog(SWMM_Engine engine, SWMMModelLayer *layer = nullptr,
                               QWidget *parent = nullptr);
    /**
     * @brief Destroy Qt-owned draft controls without releasing the engine or applying settings.
     * @details Successful prior engine publications are not rolled back by destruction.
     */
    ~ClimatologyDialog() override = default;

    /**
     * @brief Report whether a changed OK action completed atomic engine publication.
     * @return False initially, for unchanged acceptance or failed validation/publication; true
     * after a changed successful onAccept(). Does not compare individual final values against
     * earlier project history.
     *
     * @details True after OK wrote at least one changed value.
     */
    [[nodiscard]] bool wroteAnyChanges() const { return m_wroteChanges; }

    /**
     * @brief Select a valid zero-based climatology page.
     * @param[in] idx Tab ordinal 0..7 in the initialized editor; out-of-range values are
     * ignored.
     * @details Presentation only, with no engine write.
     *
     * @details Select the tab a toolbar button maps to.
     */
    void setCurrentTab(int idx);

    /**
     * @brief Rebase both file pickers against the project input directory.
     * @param[in] dir Borrowed directory copied as the anchor; empty uses absolute path display.
     * @details If the draft matched its initial signature before rebasing, refreshes path
     * identity properties and baseline so display-only anchor changes remain clean. Otherwise
     * preserves the previous baseline. Does not move/write files or change engine file
     * references.
     *
     * @details Directory of the project's `.inp`, used to show and commit the climate
     *  file relative to the project. Set by the caller, which knows the model
     *  layer — deriving it here would drag SWMMModelLayer's implementation into
     *  every target that links this dialog. Empty = absolute paths.
     */
    void setProjectAnchor(const QString &dir);
    /**
     * @brief Read a comparable displayed climate/snow/radiation signature via a temporary
     * editor.
     * @param[in] engine Borrowed nullable engine, read synchronously on the GUI thread.
     * @param[in] anchor Borrowed project directory used to normalize picker path identities.
     * @return Owned serialize() string from a fully constructed/hydrated temporary dialog;
     * includes widget clamping/formatting conventions, not an exact hash of all native fields or
     * a file-content fingerprint. No setters or modal execution.
     *
     * @details Every value this dialog edits, as read from \a engine, for stale-edit
     *  and Undo identity checks.
     */
    static QString configurationSignature(SWMM_Engine engine, const QString &anchor);

public slots:
    /**
     * @brief Clear the borrowed handle, disable controls and reject before engine release.
     * @details No engine access/save is performed. Caller owns this lifetime handoff; completed
     * publication flag remains unchanged.
     */
    void invalidateEngine();

private slots:
    /**
     * @brief Validate changed draft/concurrency and atomically publish shared hydrothermal
     * settings.
     * @details No engine rejects; exact unchanged signature accepts without writes. Checks all
     * numeric grid cells are finite and required series selections nonempty, then compares a
     * fresh displayed signature to reject stale external edits. Updates the snow draft and runs
     * changed snow/radiation/cloud apply, climate setters and persistent heat-component
     * registration inside swmm_hydrothermal_edit_atomic(). Failure warns and retains the draft
     * with native rollback; success sets wroteAnyChanges and accepts. Registration prepares a
     * future save asset name and does not write a configuration file here.
     */
    void onAccept();
    /**
     * @brief Select the evaporation draft stack page from the combo's stored API enum.
     * @details Ignores missing controls/out-of-range data; preserves inactive page values and
     * publishes no engine setters.
     */
    void onEvapTypeChanged();
    /**
     * @brief Gate time-series versus climate-file temperature inputs from the source index.
     * @details Enables file start date only for File plus its checked start-date option; retains
     * inactive values without changing engine data.
     */
    void onTempSourceChanged();
    /**
     * @brief Enable monthly wind values only for the monthly source choice.
     * @details File choice disables the table without discarding its values or publishing
     * setters.
     */
    void onWindTypeChanged();
    /**
     * @brief Select the constant/monthly/time-series humidity draft page.
     * @details Stored type must be a valid stack ordinal; no-op without controls and no engine
     * publication.
     */
    void onHumidityTypeChanged();
    /**
     * @brief Update humidity range/units for relative percentage versus project-temperature dew
     * point.
     * @details Relative humidity clamps its constant spin to 0..100%; dew point permits
     * -200..200°C/°F according to project units. Changing quantity does not convert existing
     * numeric values; Qt range changes can clamp the local constant. Monthly labels change but
     * cells are not independently validated here.
     */
    void onHumidityVarChanged();
    /**
     * @brief Replace one ten-point snow-depletion draft column with a fixed preset.
     * @param[in] column Valid ADC column 0 impervious or 1 pervious; caller must supply a valid
     * table column.
     * @param[in] natural True selects the ten-point natural curve, false fills ones for no
     * depletion.
     * @details Updates displayed/cache values only; publication awaits OK.
     */
    void onAdcPreset(int column, bool natural);
    /**
     * @brief Reset all twelve monthly temperature adjustments to zero and other columns to one.
     * @details Rain/conductivity ones are dimensionless multipliers; evaporation one is an
     * additive 1 mm/day or in/day, following current implementation. Does not reset evaporation
     * to additive zero or call native setters.
     */
    void onClearAdjustments();
    /**
     * @brief Open a read-only effective-evaporation preview computed from the unsaved draft.
     * @details Requires a valid model start/end interval; samples a chosen end-exclusive window
     * daily/weekly/30-day, refusing over 2000 displayed samples. Passes temporary UTF-8 draft
     * sources/absolute climate path and twelve monthly adjustments through the native
     * size-query/fill protocol. Reports coverage/held/missing/estimated values and effective
     * rates in mm/day or in/day. Does not publish the draft or advance simulation; future live
     * runtime forcing is excluded. Native diagnostics/modal dialogs report failures, and caller
     * must keep engine/context valid during nested preview interaction.
     */
    void onPreviewEvaporation();

private:
    /**
     * @brief Construct eight owned, scrollable climatology tabs and OK/Cancel controls once.
     * @details OK routes to validated atomic publication; Cancel rejects local edits. Requires
     * uninitialized control members and does not hydrate or mutate engine values by itself.
     */
    void buildUi();
    /**
     * @brief Create the owned temperature source, editable series, project-relative climate
     * file, optional date and explicit file-unit inputs; draft gates retain inactive values.
     * @param[in,out] tabs Non-null borrowed tab widget adopting the new scroll area/page and its
     * child controls.
     * @details Called once during buildUi() before readFromEngine(); signal handlers change
     * local draft/presentation only. Does not publish authoring setters or transfer engine
     * ownership.
     */
    void buildTemperatureTab(QTabWidget *tabs);
    /**
     * @brief Create the owned constant/monthly/series/Hargreaves/file-pan evaporation drafts,
     * recovery pattern, dry-only flag and read-only preview; rates use mm/day or in/day.
     * @param[in,out] tabs Non-null borrowed tab widget adopting the new scroll area/page and its
     * child controls.
     * @details Called once during buildUi() before readFromEngine(); signal handlers change
     * local draft/presentation only. Does not publish authoring setters or transfer engine
     * ownership.
     */
    void buildEvaporationTab(QTabWidget *tabs);
    /**
     * @brief Create the owned monthly-versus-file wind drafts and twelve values in km/h for SI
     * or mph for US.
     * @param[in,out] tabs Non-null borrowed tab widget adopting the new scroll area/page and its
     * child controls.
     * @details Called once during buildUi() before readFromEngine(); signal handlers change
     * local draft/presentation only. Does not publish authoring setters or transfer engine
     * ownership.
     */
    void buildWindTab(QTabWidget *tabs);
    /**
     * @brief Create the owned relative-humidity percentage or project-unit dew-point drafts with
     * constant/monthly/series sources.
     * @param[in,out] tabs Non-null borrowed tab widget adopting the new scroll area/page and its
     * child controls.
     * @details Called once during buildUi() before readFromEngine(); signal handlers change
     * local draft/presentation only. Does not publish authoring setters or transfer engine
     * ownership.
     */
    void buildHumidityTab(QTabWidget *tabs);
    /**
     * @brief Create the owned degree-day/UEB choice, UEB step in seconds, local parameter dialog
     * and legacy project-temperature/elevation snow fields.
     * @param[in,out] tabs Non-null borrowed tab widget adopting the new scroll area/page and its
     * child controls.
     * @details Called once during buildUi() before readFromEngine(); signal handlers change
     * local draft/presentation only. Does not publish authoring setters or transfer engine
     * ownership.
     */
    void buildSnowTab(QTabWidget *tabs);
    /**
     * @brief Create the owned ten snow-depth-ratio rows (0.0..0.9) for impervious/pervious
     * covered-area fractions and two presets.
     * @param[in,out] tabs Non-null borrowed tab widget adopting the new scroll area/page and its
     * child controls.
     * @details Called once during buildUi() before readFromEngine(); signal handlers change
     * local draft/presentation only. Does not publish authoring setters or transfer engine
     * ownership.
     */
    void buildAdcTab(QTabWidget *tabs);
    /**
     * @brief Create the owned twelve monthly temperature/evaporation additive offsets and
     * rainfall/conductivity multipliers; defaults are temperature zero and other columns one.
     * @param[in,out] tabs Non-null borrowed tab widget adopting the new scroll area/page and its
     * child controls.
     * @details Called once during buildUi() before readFromEngine(); signal handlers change
     * local draft/presentation only. Does not publish authoring setters or transfer engine
     * ownership.
     */
    void buildAdjustmentsTab(QTabWidget *tabs);
    /**
     * @brief Create the owned shared explicit site, radiation sources, met-file preview and
     * cloud controls; site elevation metres, UTC offset hours and flux W/m² regardless of
     * project units.
     * @param[in,out] tabs Non-null borrowed tab widget adopting the new scroll area/page and its
     * child controls.
     * @details Called once during buildUi() before readFromEngine(); signal handlers change
     * local draft/presentation only. Does not publish authoring setters or transfer engine
     * ownership.
     */
    void buildRadiationTab(QTabWidget *tabs);
    /**
     * @brief Edit local UEB options/monthly temperature ranges in a nested modal draft.
     * @details Accept copies only changed numeric options at seventeen digits, always stores
     * selected temperature/partition schemes and twelve ranges in Celsius (zero means derive
     * from source). Cancel leaves the parent snow draft intact. Does not publish the parent
     * draft or native setters.
     */
    void editUebParameters();
    /**
     * @brief Copy current snow/radiation/cloud controls into the staged shared settings.
     * @details Retains originally loaded high-precision site/cloud numbers when displayed values
     * are unchanged. Normalizes source text and updates the met-file authored display path only
     * after its absolute identity changed. Site elevation is metres, timezone hours, radiation
     * W/m² and cloud fractions dimensionless regardless of project length units. No engine
     * write.
     */
    void updateSnowDraft();

    /**
     * @brief Hydrate climate tables/precise controls and shared snow/radiation baselines.
     * @details No-op without engine. Failed scalar reads usually leave prior defaults, while
     * unchecked adjustment-array failures yield zero-initialized entries. Copies fixed-buffer
     * references/resolved paths and exact numeric cell caches; project conventions are °C/°F,
     * m/ft, mm/in per day and km/h/mph. Loads shared snow settings, captures their signature and
     * control display/path properties, and updates UI gates without publishing setters.
     */
    void readFromEngine();
    /**
     * @brief Sequentially publish all legacy climate controls using native project-unit
     * conventions.
     * @return Empty after all setters succeed, otherwise first translated refusal; missing
     * engine returns a diagnostic.
     * @details Writes temperature, evaporation, wind, humidity, degree-day snow, ADC and
     * adjustment groups in order. This helper has no rollback; caller onAccept() supplies the
     * surrounding atomic hydrothermal transaction. Unchanged hydrated spins/cells retain exact
     * numeric caches; edited constant evaporation/humidity fills all twelve months. Climate path
     * is set only for active File with a changed nonempty absolute identity; displayPath
     * preserves project-relative spelling. Does not itself apply shared UEB/radiation/cloud
     * settings or save files.
     */
    QString writeToEngine();
    /**
     * @brief Encode displayed draft values and cached numeric grids for change/concurrency
     * checks.
     * @return Owned delimiter-separated string including UEB option rows, snow/radiation/cloud
     * controls, absolute picker identities and six grids; no engine queries or file hashing.
     * @details Uses mixed QString numeric precision conventions, captures inactive controls too
     * and performs no semantic validation. Intended only for this editor's expected-state
     * comparison, not external durable identity or serialization to input syntax.
     *
     * @details Serialize all widget values to a string, to detect real edits on OK.
     */
    [[nodiscard]] QString serialize() const;

    /**
     * @brief Test the unit flag captured from the engine at construction.
     * @return True only for flag one, otherwise false/US; no live unit query or conversion.
     */
    [[nodiscard]] bool isSI() const { return m_unitSystem == 1; }
    /**
     * @brief Append copied UTF-8 time-series IDs to an existing combo.
     * @param[in,out] combo Borrowed optional target; null or missing engine is ignored.
     * @details Does not clear old entries, add an empty sentinel or subscribe to later registry
     * changes. Null native IDs are skipped; no ownership transfer.
     */
    void populateTimeseriesCombo(QComboBox *combo) const;
    /**
     * @brief Append copied UTF-8 pattern IDs to an existing combo.
     * @param[in,out] combo Borrowed optional target; null or missing engine is ignored.
     * @details Does not clear existing items/filter pattern kinds or preserve a dynamic registry
     * subscription. Native null IDs are skipped.
     */
    void populatePatternCombo(QComboBox *combo) const;

    SWMM_Engine m_engine = nullptr;
    SWMMModelLayer *m_layer = nullptr;
    QTabWidget *m_tabs = nullptr;
    bool m_wroteChanges = false;
    int m_unitSystem = 0; // 0=US, 1=SI
    QString m_initialSig; // widget signature captured after read

    // --- Temperature ---
    QComboBox *m_tempSource = nullptr; // 0 None / 1 TimeSeries / 2 File
    QComboBox *m_tempTs = nullptr;
    openswmmvis::ui::RelativePathPicker *m_tempFile = nullptr;
    QString m_projectAnchor; ///< .inp dir; see setProjectAnchor.
    QCheckBox *m_tempStartCheck = nullptr;
    QDateEdit *m_tempStartDate = nullptr;
    QComboBox *m_tempUnits = nullptr; // -1 Auto / 0 C10 / 1 C / 2 F

    // --- Evaporation ---
    QComboBox *m_evapType = nullptr; // engine enum via itemData
    QStackedWidget *m_evapStack = nullptr;
    QDoubleSpinBox *m_evapConstant = nullptr;
    QTableWidget *m_evapMonthly = nullptr; // 12 rows
    QComboBox *m_evapTs = nullptr;
    QTableWidget *m_panCoeff = nullptr; // 12 rows
    QComboBox *m_recovery = nullptr;
    QCheckBox *m_dryOnly = nullptr;

    // --- Wind ---
    QComboBox *m_windType = nullptr;       // 0 Monthly / 1 File
    QTableWidget *m_windMonthly = nullptr; // 12 rows

    // --- Humidity ([TEMPERATURE] HUMIDITY) ---
    QComboBox *m_humVar = nullptr;  // 0 Relative % / 1 Dew point
    QComboBox *m_humType = nullptr; // 0 Constant / 1 Monthly / 2 TimeSeries
    QStackedWidget *m_humStack = nullptr;
    QDoubleSpinBox *m_humConstant = nullptr;
    QTableWidget *m_humMonthly = nullptr; // 12 rows
    QComboBox *m_humTs = nullptr;

    // --- Snow Melt ---
    QDoubleSpinBox *m_snowTemp = nullptr;
    QDoubleSpinBox *m_atiWeight = nullptr;
    QDoubleSpinBox *m_negMelt = nullptr;
    QDoubleSpinBox *m_elevation = nullptr;
    QDoubleSpinBox *m_latitude = nullptr;
    QDoubleSpinBox *m_longitude = nullptr;
    QComboBox *m_snowModel = nullptr;
    QDoubleSpinBox *m_snowStep = nullptr;
    QCheckBox *m_radiationSite = nullptr;
    QDoubleSpinBox *m_siteValues[4]{};
    QComboBox *m_radiationModes[2]{};
    QComboBox *m_radiationSeries[2]{};
    QComboBox *m_cloudMode = nullptr;
    QComboBox *m_cloudSeries = nullptr;
    QDoubleSpinBox *m_cloudValues[4]{};
    openswmmvis::ui::RelativePathPicker *m_metFile = nullptr;
    openswmmvis::ui::SnowClimateSettings m_snowDraft;
    QString m_initialSnowSig;

    // --- Areal depletion ---
    QTableWidget *m_adc = nullptr; // 10 rows x 2 cols (imperv, perv)

    // --- Adjustments ---
    QTableWidget *m_adjust = nullptr; // 12 rows x 4 cols
};

#endif // CLIMATOLOGYDIALOG_H
