/**
 * @file      heatconfigdialog.h
 * @brief     Editor for the heat-transport configuration — `[HEAT_SOURCES]`
 *        inlet temperatures, `[HEAT_FLUXES]` module toggles, and H6a's
 *        `[RADIATIVE_FLUXES]` / `[SOLAR_RADIATION]` / `[CLOUD_COVER]`
 *        (GUI plan G4g, against the corrected spec — there is no
 *        `[HEAT_METEOROLOGY]` section).
 *
 * @details Heat enters the model at seven water sources, each with a
 *          GLOBAL inlet temperature in °C and — for DWF and external
 *          inflow — per-NODE overrides (the engine parser's H1 scope
 *          rule, which `swmm_heat_set_node_override` refuses to break).
 *          A source the model never set reads the 20 °C default;
 *          `swmm_heat_get_source_configured` is what separates "set to
 *          20" from "never set", and the editor preserves that
 *          distinction with a per-source check box so an untouched OK
 *          cannot invent `[HEAT_SOURCES]` rows.
 *
 *          The engine handle IS the model (GUI plan §2): edits go through
 *          `openswmm_heat.h` directly, values the parser refuses are
 *          refused here too (ranges mirror `parse_celsius` and friends),
 *          and IO3b's renderer persists whatever this dialog writes.
 *
 *          Dependency-light — Qt Widgets plus that one engine ABI — so a
 *          test can construct it against a synthetic BUILDING engine and
 *          drive the widgets (the WaterAgeSourcesDialog precedent).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   Apache-2.0
 */

#ifndef OPENSWMM_UI_DIALOGS_HEATCONFIGDIALOG_H
#define OPENSWMM_UI_DIALOGS_HEATCONFIGDIALOG_H

#include <openswmm/engine/openswmm_engine.h>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTabWidget;

namespace OpenSWMMVis
{

/*!
 * \brief Editor for `[HEAT_SOURCES]`, `[HEAT_FLUXES]` and the H6a
 *        radiative / solar / cloud, channel-bed and spatial configuration.
 */
class HeatConfigDialog : public QDialog
{
    Q_OBJECT

public:
    /*! Tab indices, in build order. */
    enum Tab {
        TabSources = 0,
        TabFluxes = 1,
        TabRadiative = 2,
        TabSolar = 3,
        TabCloud = 4,
        TabBed = 5,
        TabSpatial = 6
    };

    /**
     * @brief Build the seven-tab heat editor and hydrate its draft from a borrowed engine.
     * @param[in] engine Engine handle, retained without ownership; may be null for an unhydrated
     * editor. A live handle must remain valid until invalidateEngine() or dialog destruction.
     * Acceptance requires an editable engine before simulation initialization.
     * @param[in] parent Optional Qt parent that owns the dialog. Child controls are owned by the
     * dialog hierarchy; closing does not delete this dialog automatically.
     * @note Construction reads settings, records a configuration signature, and wires draft changes
     * to preview invalidation. It does not write engine settings. Callers connect engine/project
     * closure to invalidateEngine(); this class does not track the handle's lifetime automatically.
     */
    explicit HeatConfigDialog(SWMM_Engine engine, QWidget *parent = nullptr);
    /**
     * @brief Destroy the dialog and its Qt-owned controls without destroying the borrowed engine.
     * @note Drafts are discarded by widget destruction. The destructor performs no rollback of an
     * already accepted edit or of the nested initial-bed-quality editor.
     */
    ~HeatConfigDialog() override = default;
    /**
     * @brief Serialize the saved heat configuration and selected model identities for stale-editor
     * detection.
     * @param[in] engine Borrowed live engine to inspect, or null.
     * @return Empty string for null; otherwise a Base64 encoding of QDataStream bytes containing
     * authoring flags, node/link counts, IDs and tags, component records, heat switches, sources
     * and overrides, radiative/solar/bed/cloud parameters, configured flags, and bound series
     * names.
     * @note This is an equality token for the sampled configuration, not a cryptographic digest,
     * portable file format, or complete model revision. Getter errors are not reported; initialized
     * locals and fixed-size name buffers are serialized as read. Runtime energy totals and
     * unsampled mesh topology are excluded.
     */
    static QString configurationSignature(SWMM_Engine engine);

    /*! \brief True once OK has written at least one change to the engine. */
    /**
     * @brief Report whether this dialog or its nested bed-quality editor has committed changes.
     * @return True after at least one successful nonempty heat acceptance, or a changesApplied
     * signal from the nested initial-bed-quality editor; false initially and after failed/unchanged
     * acceptance alone.
     * @note Sticky for this dialog's lifetime. Cancel does not clear this flag or undo an earlier
     * committed nested edit.
     */
    bool wroteAnyChanges() const { return m_wroteAnyChanges; }

    /*! \brief Select the tab a menu action maps to (see \ref Tab). */
    /**
     * @brief Select an existing editor tab without applying any draft changes.
     * @param[in] idx Zero-based Tab index; negative or out-of-range indices are ignored.
     */
    void setCurrentTab(int idx);
    /**
     * @brief Replace only the temperature-transport checkbox in the editor draft.
     * @param[in] enabled Requested HEAT_TRANSPORT state; saved engine options are unchanged until
     * acceptance.
     * @note Updates the process status and invalidates the resolved-element preview through the
     * checkbox connections.
     */
    void setTransportRequested(bool enabled);
    /**
     * @brief Control whether subsequently opened runtime-overview dialogs offer 2D settings
     * navigation.
     * @param[in] enabled True to offer the route. This only changes the navigation flag; it does
     * not activate mesh heat or alter the current draft.
     */
    void setTwoDTransportNavigationEnabled(bool enabled);

    /*! \brief Number of engine writes the last OK performed (test hook). */
    /**
     * @brief Return the number of counted heat mutations committed by the most recent acceptance
     * attempt.
     * @return Zero initially, after invalid/stale acceptance, rollback, or unchanged acceptance;
     * otherwise the sum of successful writer mutation counts. Count includes removal/reinsertion of
     * changed spatial overrides but excludes component-asset registration and nested bed-quality
     * edits.
     */
    int lastWriteCount() const { return m_lastWriteCount; }

signals:
    /**
     * @brief Notify the owner after a successful nonempty atomic heat acceptance or a nested
     * bed-quality edit.
     * @note The owner is responsible for dirty-state propagation, persistence and undo integration;
     * this dialog does not create a project undo command.
     */
    void changesApplied();
    /**
     * @brief Request shared radiation navigation after the heat draft has been accepted
     * successfully.
     * @note The caller opens the destination; this signal itself creates no editor.
     */
    void radiationEditorRequested();
    /**
     * @brief Request groundwater transport/thermal navigation after successful heat acceptance.
     */
    void groundwaterSettingsRequested();
    /**
     * @brief Request 2D transport navigation after the overview closes and a queued heat acceptance
     * succeeds.
     * @note Emitted only by the optional overview route; the caller selects or opens the
     * destination.
     */
    void twoDSettingsRequested();
    /**
     * @brief Request raster-based spatial assignment after successful heat acceptance.
     * @note Existing heat edits are already applied when emitted; assignment and its undo behavior
     * belong to the caller.
     */
    void rasterAssignmentRequested();

public slots:
    /**
     * @brief Forget the borrowed engine, disable the dialog, and reject its modal loop.
     * @note Call before engine destruction or project/model replacement on the GUI thread. No
     * engine dereference or rollback is attempted; any already committed edit remains committed.
     */
    void invalidateEngine();

private slots:
    /**
     * @brief Validate the complete draft, reject stale saved settings, and atomically apply heat
     * changes.
     * @note Resets lastWriteCount() before validation. A signature mismatch leaves the draft open
     * without writes. The synchronous hydrothermal transaction rolls back failed writes, resets the
     * count to zero, and displays the first error; success with a positive count sets
     * wroteAnyChanges() and emits changesApplied(). Successful unchanged drafts also accept. The
     * borrowed engine must still be valid; lifecycle/allocation transaction failures leave the
     * editor open.
     */
    void onAccept();
    /**
     * @brief Append a draft DWF/external-inflow node-temperature row with a 20 degree Celsius
     * default.
     * @note Comboboxes store engine source codes and zero-based node indices. The table owns the
     * new widgets; an absent engine produces an empty node list. No saved setting is changed.
     */
    void onAddOverride();
    /**
     * @brief Remove the current draft node-override row, if any, and refresh accessible row labels.
     * @note Saved overrides are removed only when a subsequent acceptance succeeds.
     */
    void onRemoveOverride();
    /**
     * @brief Show a stack-owned modal snapshot of saved, draft, and runtime process status.
     * @note The read-only report is captured when opened, not continuously refreshed. If enabled,
     * its 2D action first closes this nested dialog, then queues onAccept() on this object's
     * lifetime-bound context and emits twoDSettingsRequested() only on successful acceptance.
     */
    void showRuntimeOverview();

private:
    /**
     * @brief Create the owned source, flux, radiation, solar, cloud, channel-bed and spatial
     * controls once.
     * @note Requires a fresh dialog. Connects validation/acceptance and apply-before-navigation
     * actions, sets physical ranges and accessibility labels, and reads LID material assumptions
     * for display. Initial-bed-quality navigation accepts the heat draft before opening a
     * stack-owned child editor; its changes are propagated separately.
     */
    void buildUi();
    /**
     * @brief Hydrate the existing controls from saved engine values without authoring new
     * configuration.
     * @note Null handle is ignored. Intended once after buildUi(): series choices and override rows
     * are appended rather than cleared. Retains exact hydrated spin values for later comparison,
     * distinguishes configured/default temperatures, records initial series and spatial signatures,
     * and suppresses explicit-site auto-authoring while loading. Most read errors leave default
     * controls; unreadable override rows are skipped.
     */
    void readFromEngine();
    /**
     * @brief Validate node references, unique override keys, bound series, and spatial
     * property/scope constraints.
     * @return True when these checks pass; false after reportFailure() selects the first invalid
     * field or when no engine is available.
     * @note Clears prior error text and refreshes node-row accessibility. Verifies current node
     * index/ID pairs, only DWF/external node scopes, required series of engine table type zero,
     * nonempty spatial targets, property ranges, sediment/node incompatibility and ground-source
     * link/tag-only scope. It does not resolve every spatial object/tag or test every engine
     * setter; the atomic writer supplies the remaining checks.
     */
    bool validateDraft();
    /**
     * @brief Display an error and focus its associated draft field without writing the engine.
     * @param[in] field Borrowed offending control, or null for a dialog-wide error. A matching
     * containing tab/table cell is selected and scrolled into view.
     * @param[in] message Plain-text error displayed and appended to the field's accessible
     * description.
     * @return Always false, allowing direct return from a failed validation branch.
     */
    bool reportFailure(QWidget *field, const QString &message);
    /**
     * @brief Translate an engine writer/read status into the current transaction's error state.
     * @param[in] status Engine status; SWMM_OK passes through successfully.
     * @param[in] field Borrowed related control or null, forwarded to reportFailure() on failure.
     * @param[in] action Human-readable operation name included with the numeric engine status.
     * @return True for SWMM_OK; otherwise false after setting m_writeFailed and reporting the
     * error.
     * @note Does not itself undo anything. The enclosing hydrothermal transaction performs
     * rollback; callers must stop their writer after false.
     */
    bool checked(int status, QWidget *field, const QString &action);
    /**
     * @brief Rebuild one-based row/column accessibility names and uniqueness/temperature guidance
     * for node overrides.
     */
    void refreshOverrideAccessibility();
    /**
     * @brief Apply the current controls in dependency order inside an enclosing hydrothermal
     * transaction.
     * @return Count of successful mutations so far, or zero for a null handle. Counts are
     * provisional until the caller commits the transaction; inspect m_writeFailed to distinguish
     * failure from no change.
     * @note Requires a validated draft and m_writeFailed reset by the caller. Writes
     * HEAT_TRANSPORT, solar site, sources, modules/dry policy, radiation, cloud, bed and spatial
     * overrides, stopping on failure. Clears the explicit site last when unchecked and registers a
     * relative persistent heat-component asset name after nonempty success. Registration writes no
     * file; the project save publishes it. Preview callers deliberately roll back these same
     * changes.
     */
    int writeToEngine();
    /**
     * @brief Write changed solar site/atmosphere values, preserving explicit-site authorship.
     * @return Successful setter count before the first checked error; failures set m_writeFailed
     * for caller rollback.
     * @note Uses degrees north/east, UTC-offset hours, elevation metres, dimensionless optical
     * depths/albedo and precipitable-water/ozone centimetres. When explicit site is requested but
     * absent, equal latitude/longitude can still be authored; unchecked site clearing occurs later
     * in writeToEngine().
     */
    int writeSolar();
    /**
     * @brief Write shortwave mode/binding and changed radiative scalar values.
     * @return Successful setter count before the first checked error; caller must roll back if
     * m_writeFailed is set.
     * @note Shortwave constants are W/m2; fractions/emissivities are dimensionless and land-cover
     * temperature is degrees Celsius. Timeseries mode binds the selected name; constant/computed
     * modes switch first. Unchecked land-cover temperature writes NaN to inherit air temperature,
     * while two nonfinite values are treated as unchanged.
     */
    int writeRadiative();
    /**
     * @brief Reconcile configured global source temperatures and keyed per-node overrides with the
     * draft.
     * @return Successful setter/clear/remove count before the first checked error; all counts are
     * provisional within the caller's transaction.
     * @note Temperatures are degrees Celsius. Preserves the distinction between an unconfigured 20
     * degree default and an explicit 20 degree value. Removes missing source/node keys in reverse
     * order, then updates or inserts changed values using exact hydrated precision.
     */
    int writeSources();
    /**
     * @brief Write changed heat-module toggles and the dry-element temperature policy.
     * @return Successful mutation count before the first checked failure, which sets m_writeFailed.
     * @note Iterates the four module enums in control order; dry policy selects hold-last, air, or
     * default temperature. These authoring switches do not certify runtime domain activation.
     */
    int writeModules();
    /**
     * @brief Reconcile cloud configuration, coefficients and an optionally selected fraction
     * series.
     * @return Successful mutation count before the first checked error; caller owns
     * commit/rollback.
     * @note Unchecking clears existing cloud configuration. Enabling an unconfigured cloud authors
     * its fraction even if numerically unchanged. Fraction is dimensionless [0,1]; attenuation
     * coefficients use their engine meanings. A nonempty changed series binds by name; an empty
     * selection does not unbind an existing series in this path.
     */
    int writeCloud();
    /**
     * @brief Write changed SI sediment parameters and explicit/inherited bed-temperature settings.
     * @return Successful setter/clear count before the first checked failure, with m_writeFailed
     * set for rollback.
     * @note Parameters use m2/s diffusivities, metre depths, degrees Celsius temperatures, m/s
     * hyporheic velocity, kg/m3 density and J/kg/K specific heat. Unconfigured temperatures clear
     * explicit authorship. Ground-series mode skips the constant temperature setter and binds only
     * a changed series; switching to constant also replaces a saved series even if the numeric
     * value matches.
     */
    int writeBed();
    /**
     * @brief Replace saved spatial overrides when the serialized draft differs from its hydrated
     * signature.
     * @return Zero for an unchanged signature; otherwise successful removals plus insertions before
     * the first checked failure.
     * @note Requires the caller's transaction: deletes all existing rows in reverse order then
     * inserts draft rows in order, so an error can leave partial provisional state until rollback.
     * Uses exact current target spelling, property/scope codes and precise values in SI units or
     * degrees Celsius; cell identifiers are one-based.
     */
    int writeElements();
    /**
     * @brief Append a draft property/scope/target/value row with owned editable controls.
     * @note Scope changes repopulate available node/link IDs or sorted tags while preserving typed
     * text. Cell targets use a bounded integer validator rather than materializing all cells.
     * Property selection sets its physical range; ground-source mode uses integer codes 0/1. Does
     * not write the engine; row/field changes invalidate the draft preview.
     */
    void addElementOverride();
    /**
     * @brief Resolve the complete pending draft in a deliberately rolled-back engine transaction.
     * @note Requires a live engine, valid nonstale draft and selected spatial row. Applies
     * provisional settings then resolves the selected property's scope, including exact tag matches
     * or one-based cell IDs; displays at most 200 objects while counting all matches. Mesh-tag
     * scopes above 5000 cells expose cancellation polled every 256 cells. The callback deliberately
     * returns an error even after successful reads to restore settings and component registration.
     * Partial table rows can remain visible on cancellation/failure; lastWriteCount and
     * committed-change signals are unchanged.
     */
    void previewElements();
    /**
     * @brief Preview saved link-to-mesh XY contacts without applying the current draft.
     * @note Filters link IDs case-insensitively and processes only saved links resolving to
     * MESH_CONTACTS. Shows at most 250 successful link rows; each sample reads the first 128
     * contact records and displays up to 30 distinct one-based cell IDs plus remaining counts.
     * Lengths are metres. Progress pumps GUI events and supports engine cancellation, retaining
     * partial results. Completeness here concerns XY coverage; initialization still validates
     * groundwater profiles, bed depths and thermal ownership.
     */
    void previewChannelGroundContacts();
    /**
     * @brief Clear resolved draft rows and mark the displayed preview stale after a control change.
     * @note Safe before the resolved table exists; changes only display state, not engine settings.
     */
    void invalidateElementPreview();
    /**
     * @brief Serialize the ordered draft spatial rows for exact change detection.
     * @return Length-prefixed property/scope data and current target text, followed by each precise
     * numeric value with 17 significant digits.
     * @note Assumes all table rows contain the expected combobox/spin widgets. Includes row order
     * and untrimmed target text, so either difference triggers full saved-row replacement.
     */
    QString elementSignature() const;
    /**
     * @brief Read the current borrowed engine's saved-configuration equality token.
     * @return configurationSignature(m_engine), empty when the engine has been invalidated.
     */
    QString engineSignature() const;
    /**
     * @brief Describe saved and draft heat requests separately from runtime activation and energy
     * receipts.
     * @return Localized plain text, or an explanatory message when the engine or saved heat flag is
     * unavailable. Unreadable optional module/domain status sections are omitted.
     * @note Does not initialize or edit the engine. Before initialization, reports requests/reasons
     * only; afterward includes mesh/soil/channel activation and finite cumulative signed energy
     * receipts in SI joules, summing cells in chunks of 4096. A zero signed receipt is not
     * interpreted as disabled activity.
     */
    QString runtimeOverviewText() const;
    QString m_engineInitial;
    /**
     * @brief Refresh draft-dependent control enablement and explanatory domain-activation labels.
     * @note Ignored during hydration or before required controls exist. Reads saved IGNORE_QUALITY
     * for network guidance; retains independent mesh activation semantics. Controls
     * computed-shortwave site requirements and explicit land-cover/ground/initial-bed temperature
     * widgets, without mutating saved settings.
     */
    void refreshProcessStatus();

    SWMM_Engine m_engine = nullptr;
    bool m_wroteAnyChanges = false;
    int m_lastWriteCount = 0;
    bool m_writeFailed = false;
    QLabel *m_validationError = nullptr;
    QTabWidget *m_tabs = nullptr;
    bool m_twoDTransportNavigationEnabled = false;

    // Sources tab
    QTableWidget *m_sourceTable = nullptr;   ///< 7 rows: check + °C spin
    QTableWidget *m_overrideTable = nullptr; ///< source / node / °C

    // Fluxes tab
    QCheckBox *m_modules[4] = {};
    QCheckBox *m_heatEnabled = nullptr;
    QLabel *m_processStatus = nullptr;
    QLabel *m_moduleStatus[4] = {};
    QComboBox *m_dryPolicy = nullptr;
    QCheckBox *m_groundConfigured = nullptr;
    QCheckBox *m_initialConfigured = nullptr;
    QComboBox *m_groundMode = nullptr;
    QComboBox *m_groundSeries = nullptr;
    QDoubleSpinBox *m_bedSpin[9] = {};
    QPushButton *m_channelGroundPreviewButton = nullptr;
    QLineEdit *m_channelGroundPreviewFilter = nullptr;
    QLabel *m_channelGroundPreviewStatus = nullptr;
    QTableWidget *m_channelGroundPreviewTable = nullptr;
    QTableWidget *m_elementTable = nullptr;
    QTableWidget *m_resolvedElements = nullptr;
    QLabel *m_resolvedStatus = nullptr;
    QString m_elementsInitial;
    QCheckBox *m_explicitSite = nullptr;
    bool m_loading = false;

    // Radiative tab
    QRadioButton *m_swConstant = nullptr;
    QRadioButton *m_swTimeseries = nullptr;
    QRadioButton *m_swComputed = nullptr;
    QDoubleSpinBox *m_swConstSpin = nullptr;
    QComboBox *m_swTsCombo = nullptr;
    /// Bound series names as hydrated (swmm_heat_get_*_timeseries), so the
    /// OK path can tell a real rebind from reselecting what was shown.
    QString m_swTsInitial;
    QString m_cloudTsInitial;
    QCheckBox *m_landcoverConfigured = nullptr;
    QDoubleSpinBox *m_radSpin[9] = {}; ///< index = param enum; [0] unused

    // Solar tab
    QDoubleSpinBox *m_solarSpin[9] = {}; ///< index = param enum

    // Cloud tab
    QCheckBox *m_cloudEnable = nullptr;
    QDoubleSpinBox *m_cloudSpin[4] = {}; ///< index = param enum
    QComboBox *m_cloudTsCombo = nullptr;
};

} // namespace OpenSWMMVis

#endif // OPENSWMM_UI_DIALOGS_HEATCONFIGDIALOG_H
