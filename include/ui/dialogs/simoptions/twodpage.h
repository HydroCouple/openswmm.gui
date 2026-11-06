/**
 * @file      twodpage.h
 * @brief     Simulation Options → 2D Surface Routing.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → 2D Surface Routing.
 *
 * Tabs: Hydrodynamics · Wetting & Drying · Coupling · Processes ·
 * Performance & Output. Five rather than PLAN §2's four: §1.3 reserved "a
 * Processes tab after Coupling", and that content (the 2D process switches and
 * the groundwater enables, U1/U5) now exists.
 *
 * Two tab names depart from §2 because the content moved out from under them:
 * "Rainfall & Output" lost its rainfall to the Processes tab, and "Mesh &
 * Closure" never held a mesh setting — its first group is the depth thresholds
 * that decide when a cell counts as wet.
 *
 * Every key on this page is an `[2D_OPTIONS]` key and goes through
 * `swmm_options_set_ext`, not the plain `[OPTIONS]` setter — so the page keeps
 * its own writeIfChanged rather than using the context's.
 *
 * The page is only built when the engine ships the 2D module; the whole class
 * is compiled out otherwise.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef TWODPAGE_H
#define TWODPAGE_H

#ifdef OPENSWMM_HAS_2D

#include <functional>

#include "ui/dialogs/simoptions/simoptionspage.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QCustomTimespanEdit;

namespace openswmmvis::ui
{

class TwoDPage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Construct owned surface-routing, process, coupling and output draft controls.
     * @param ctx Borrowed GUI-thread context that must outlive page accesses; engine,
     * layer/project handles must remain valid during queries and nested file/action prompts.
     * @param parent Optional QWidget owner; null leaves lifetime to the caller.
     * Builds/tags controls and snapshots species names from the current engine. Does not
     * hydrate every option, start a run or apply drafts; available only in 2D builds.
     */
    explicit TwoDPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned 2D Surface Routing string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Hydrate extended and dedicated aquifer drafts, output selections and process
     * summaries.
     * Local extension reader borrows key/fallback and uses a 256-byte UTF-8 buffer;
     * missing engine/failed getter returns fallback, successful blank remains blank.
     * Numeric key/fallback helpers retain fallback on conversion failure before clamping.
     * Wide key reader uses 4096 bytes or returns empty; combo selector borrows combo/token
     * and preserves prior selection on no match. Uses 2D preferences plus fixed defaults.
     * Report/infiltration zero steps mirror schedule hooks; selected controls are signal-
     * blocked, others can refresh gates during hydration. Infiltration AUTO scans at most
     * 50,000 cells only without default rows. Emits transportSelectionChanged, reads
     * 80-byte dedicated groundwater fields without checking getter status and refreshes
     * gates. Does not write options, refresh the construction-time species catalog or save.
     */
    void read() override;
    bool validate(QString *warn) override;
    /**
     * @brief Attempt surface drafts through extension setters and grouped aquifer process setter.
     * @return Successful independent extension writes plus one for a changed successful
     * GW_ET/LINK_SEEPAGE/WILTING_SUCTION group. Failures/no-ops contribute zero; the complete
     * pass is non-atomic and does not call validate(), undo or save files.
     * Local helpers borrow key/value, read 256 bytes (4096 for variable/species lists),
     * record considered keys even for refusal/no-op and compare ordinary values numerically.
     * Report/infiltration cadence compares whole seconds, persisting zero for inherited
     * steps. Variable masks compare semantically; species ALL/empty compare equivalent,
     * other lists compare case-insensitively but retain order. Grouped aquifer comparison
     * preserves custom wilting text within absolute 5e-10 project length units; successful
     * group writes emit layer optionsChanged. Disabled controls still supply draft values.
     */
    int write() override;
    /**
     * @brief Refresh draft process controls and read-only groundwater/ET guidance.
     * Shows ADVECTION only for local-inertial momentum without clearing its saved state.
     * Queries configured aquifer mode, ET demand status and groundwater authoring row counts;
     * explicit groundwater NO disables ET/link settings. Boundary-ET eligibility gates
     * wilting controls. Keeps ordinary infiltration destination/method editable and disables
     * only the obsolete AQUIFER_2D item, without automatically replacing an existing choice.
     * Assumes owned controls exist; does not migrate ownership, change options, transform
     * units or establish runtime thermal/transport activation from configuration rows.
     */
    void refreshGates() override;

    /*! \brief Tab order, so the dialog can name tabs without magic numbers. */
    enum Tab { TabHydrodynamics = 0, TabWetDry, TabCoupling, TabProcesses, TabOutput };

    /**
     * @brief Expose the owned tab container for host navigation.
     * @return Borrowed widget; do not delete or retain beyond page lifetime.
     */
    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }

    /*!
     * \brief What this page needs from Dates & Times (PLAN §4.1).
     *
     * Two of its steps can mirror the project's, and the output-size estimate
     * multiplies by the run length. All three are read through the owning
     * page's accessors rather than by reaching for its widgets.
     */
    struct ScheduleHooks {
        std::function<double()> wetStepSeconds;
        std::function<double()> reportStepSeconds;
        std::function<qint64()> durationSeconds;
    };
    /**
     * @brief Replace retained providers for mirrored cadence and output-duration estimates.
     * @param hooks Moved callbacks returning wet/report seconds and whole duration seconds; each may
     * be empty. Caller keeps captures live until replacement/page destruction. Invoked
     * synchronously on the GUI thread; exceptions propagate. Assignment invokes nothing
     * and does not refresh mirrored values or apply engine options.
     */
    void setScheduleHooks(ScheduleHooks hooks);

    /*! \brief TRANSPORT_* state, read by the Models page's matrix. */
    /**
     * @brief Read one surface transport-class draft checkbox.
     * @param speciesClass SWMM_TRANSPORT_CLASS_* index zero..three, not a species row index.
     * @return True only for an existing checked box in range, regardless of enablement;
     * false for invalid indices/absent controls. Does not query actual runtime activation.
     */
    [[nodiscard]] bool transport2DEnabled(int speciesClass) const;
    /**
     * @brief Update one enabled surface transport-class draft without an echo signal.
     * @param speciesClass SWMM_TRANSPORT_CLASS_* index zero..three; invalid is a no-op.
     * @param on Desired checked state. Missing/disabled box is a no-op.
     * Blocks the box's signals while assigning, so does not emit transportSelectionChanged;
     * caller refreshes its own preview. Does not write TRANSPORT_* to the engine.
     */
    void setTransport2DEnabled(int speciesClass, bool on);
    /**
     * @brief Encode the current groundwater enable draft for matrix preview.
     * @return Zero for exact NO, one for exact YES, minus one for AUTO/any other token.
     * Assumes the owned combo exists; does not resolve AUTO coverage or query runtime state.
     */
    int groundwaterRequested() const;

public slots:
    /*! \brief Dates & Times moved a step or the run window; re-mirror and
     *         re-estimate. */
    /**
     * @brief Mirror inherited reporting/wet-weather steps from available hooks, then re-estimate
     * output.
     * Only checked inheritance boxes copy seconds into their owned editors; empty providers
     * leave those values unchanged. Signal feedback can refresh estimates repeatedly.
     * Does not set engine cadence, validate step multiples or modify explicit step drafts.
     */
    void scheduleChanged();

signals:
    /**
     * @brief Request host navigation to mesh snow assignments/settings.
     * The signal itself opens no editor and applies no draft.
     */
    void showSnowAssignmentsRequested();
    /**
     * @brief Request host navigation to mesh groundwater assignments/settings.
     * Carries no mesh identity or success result; the host chooses its target.
     */
    void showGroundwaterAssignmentsRequested();
    /**
     * @brief Request host navigation to the shared project evaporation editor.
     * Does not itself commit climate or surface evaporation drafts.
     */
    void showEvaporationSettingsRequested();
    /*! \brief A TRANSPORT_* box moved — the Models page's matrix is stale. */
    /**
     * @brief Notify the Models preview after a user transport-class toggle or completed read.
     * Listeners re-query class drafts; no engine setter or runtime success is implied.
     * Programmatic setTransport2DEnabled suppresses this echo.
     */
    void transportSelectionChanged();
    /*! \brief A sub-dialog wrote straight to the engine; the project is
     *         dirty regardless of this dialog's own write pass. */
    /**
     * @brief Provide a direct-edit notification seam for host dirty-state tracking.
     * Carries no key/value or persistence guarantee; declaration alone does not mean
     * read(), write() or every secondary action emits this signal.
     */
    void engineEditedDirectly();

private:
    /**
     * @brief Create owned solver/process/output controls and GUI-thread connections once.
     * Momentum/groundwater indices refresh gates; groundwater also notifies host inputs.
     * Transport bool callbacks ignore their values and emit selection changes. Inherited-
     * step callbacks receive a same bool, gate the editor and copy available schedule
     * seconds; report changes and borrowed list-item/precision/compression notifications
     * refresh estimates, ignoring scalar/item arguments. All-species bool gates its list.
     * Preset helper copies label/tooltip and retains a literal preset token in a button
     * callback that sets its variable mask. Output browse changes only path text.
     * Infiltration action searches the project window, then all top-level widgets, for a
     * named QAction to trigger; missing action prompts. Snow/groundwater/evaporation
     * buttons emit host requests. Species catalog is captured once; depth bit is mandatory.
     * Callbacks can open nested dialogs/actions, so backing dependencies must remain valid;
     * construction and draft callbacks do not commit common surface options.
     */
    void buildUi();
    /**
     * @brief Associate surface and aquifer process option keys with draft widgets.
     * Includes inherited-step editors, variable/species lists and four transport classes;
     * tags support reachability inspection and do not read or apply their values.
     */
    void tagWidgets();
    /**
     * @brief Render a rough uncompressed time-varying output estimate from the current draft.
     * Requires a live engine, positive loaded cell count, duration and report step;
     * otherwise shows a reason/dash. Uses frames = duration/step + 1 (fractional in byte
     * arithmetic), three/four cell-edge slots, selected field groups and four/eight bytes
     * per value. Species count comes from current catalog/checks, not resolved transported
     * species, and omits some reserved-state details. Does not account for geometry, actual
     * compression or runtime activation; displayed compression savings are informational.
     * Local token helper tests a borrowed engine variable name against the mask; byte
     * formatter receives nonnegative byte count and returns B..TB using powers of 1024.
     * Queries counts without separate error reporting; does not allocate/write output files.
     */
    void updateOutputSizeEstimate();
    /**
     * @brief Collect checked variable-group bits from list item UserRole indices.
     * @return ORed unsigned mask; assumes owned list and valid indices below unsigned width.
     * Does not force depth independently, validate engine schema or write the selection.
     */
    [[nodiscard]] unsigned reportVarsMask() const;
    /**
     * @brief Hydrate variable-group checks while retaining mandatory depth.
     * @param mask Requested engine dataset-group bit mask; unknown bits have no list effect.
     * Treats index zero as depth and always checks it. Blocks list signals during changes
     * and explicitly refreshes the size estimate; no engine write occurs.
     */
    void setReportVarsMask(unsigned mask);
    /**
     * @brief Render the current species draft in list order.
     * @return ALL when the All box is checked or no names are checked; otherwise copied
     * space-separated checked item text. Does not resolve declarations, quote names,
     * sort tokens or filter them against currently active transport species.
     */
    [[nodiscard]] QString reportSpeciesText() const; ///< "ALL" or space-separated
    /**
     * @brief Hydrate species filters while retaining names absent from the model catalog.
     * @param text Borrowed whitespace/comma-delimited names; empty or sole case-insensitive
     * ALL enables the all-species mode. Splits into tokens without a quoting grammar.
     * Blocks list/box signals, unchecks existing rows and checks first fixed-string matches,
     * adding table-owned undeclared-name rows as needed. Old unmatched rows are retained
     * unchecked. Updates list enablement but does not explicitly refresh the size estimate
     * or query/write engine species state.
     */
    void setReportSpeciesText(const QString &text);

    QTabWidget *m_tabs = nullptr;
    ScheduleHooks m_schedule;

    QDoubleSpinBox *m_maxTimestepSpin = nullptr;
    QGroupBox *m_marcherGroup = nullptr;
    QDoubleSpinBox *m_thetaSpin = nullptr;
    QDoubleSpinBox *m_thetaReferenceStepSpin = nullptr;
    QDoubleSpinBox *m_cflNumberSpin = nullptr;
    QSpinBox *m_ltsTiersSpin = nullptr;
    QDoubleSpinBox *m_hMoveSpin = nullptr;
    QDoubleSpinBox *m_froudeMaxSpin = nullptr;
    QComboBox *m_coriolisCombo = nullptr, *m_turbulenceCombo = nullptr;
    QLineEdit *m_coriolisLatitude = nullptr, *m_coriolisF = nullptr, *m_smagorinskyCs = nullptr;
    QLineEdit *m_eddyViscosityBackground = nullptr; ///< EDDY_VISCOSITY_BACKGROUND (m2/s)
    QWidget *m_coriolisLatitudeRow = nullptr, *m_coriolisFRow = nullptr, *m_smagorinskyRow = nullptr;
    QWidget *m_eddyViscosityBackgroundRow = nullptr;
    QLineEdit *m_mixingLengthCoefficient = nullptr; ///< MIXING_LENGTH_COEFFICIENT (l = min(c*h, 0.41*d))
    QWidget *m_mixingLengthRow = nullptr;
    QGroupBox *m_momentumPhysicsGroup = nullptr;
    QLabel *m_momentumPhysicsStatus = nullptr;
    QComboBox *m_momentum2DCombo = nullptr; ///< MOMENTUM_EQUATION
    QSpinBox *m_reconOrder2DSpin = nullptr; ///< RECONSTRUCTION_ORDER
    QCheckBox *m_advection2DBox = nullptr;
    QLabel *m_advection2DStatusLabel = nullptr;
    QComboBox *m_backend2DCombo = nullptr; ///< BACKEND
    QCheckBox *m_couplingAreaAutoBox = nullptr;
    QDoubleSpinBox *m_dryDepthSpin = nullptr;
    QDoubleSpinBox *m_limiterEpsSpin = nullptr;
    QDoubleSpinBox *m_fluxDhEpsSpin = nullptr;
    QComboBox *m_cellClosureCombo = nullptr;
    QComboBox *m_faceReconCombo = nullptr;
    QDoubleSpinBox *m_vfrMinWetFracSpin = nullptr;
    QDoubleSpinBox *m_couplingCdSpin = nullptr;
    QDoubleSpinBox *m_couplingSyncSpin = nullptr;
    // 1D↔2D exchange law and coupled-junction ponds (fine-scale coupling plan
    // §3–4): COUPLING_LAW / _HEAD / _COEFFS / _POND_MODE / _POND_RELAX /
    // _RIM_BAND / _EQUALIZE.
    QComboBox *m_couplingLawCombo = nullptr;
    QComboBox *m_couplingHeadCombo = nullptr;
    QComboBox *m_couplingCoeffsCombo = nullptr;
    QComboBox *m_couplingPondCombo = nullptr;
    QDoubleSpinBox *m_couplingPondRelaxSpin = nullptr;
    QDoubleSpinBox *m_couplingRimBandSpin = nullptr; ///< minimum = "mode default"
    QComboBox *m_couplingEqualizeCombo = nullptr;
    QComboBox *m_rainfall2DModeCombo = nullptr;
    QCheckBox *m_report2DBox = nullptr;
    QLineEdit *m_output2DFileEdit = nullptr;

    // E1 — OUTPUT_PRECISION / OUTPUT_COMPRESSION / REPORT_2D_STEP /
    // REPORT_2D_VARIABLES / REPORT_2D_SPECIES.
    QComboBox *m_output2DPrecisionCombo = nullptr;
    QSpinBox *m_output2DCompressionSpin = nullptr;
    QCheckBox *m_report2DStepSameBox = nullptr; ///< checked ⇒ REPORT_2D_STEP = REPORT_STEP (0)
    QCustomTimespanEdit *m_report2DStepEdit = nullptr;
    QListWidget *m_report2DVarsList =
        nullptr; ///< one checkable row per swmm_2d_output_variable_name
    QCheckBox *m_report2DAllSpeciesBox = nullptr; ///< checked ⇒ REPORT_2D_SPECIES ALL
    QListWidget *m_report2DSpeciesList = nullptr; ///< pollutants + MSX species
    QLabel *m_output2DSizeLabel = nullptr;        ///< live size estimate

    // U1 — Processes group ([2D_OPTIONS] process keys, E2).
    QComboBox *m_infil2DModeCombo = nullptr;   ///< AUTO | YES | NO
    QCheckBox *m_infil2DStepSameBox = nullptr; ///< checked ⇒ INFIL_STEP = WET_STEP (0)
    QCustomTimespanEdit *m_infil2DStepEdit = nullptr;
    QComboBox *m_infil2DMethodCombo = nullptr;
    QComboBox *m_infil2DDestCombo = nullptr;
    QPushButton *m_editInfilCellsBtn = nullptr;
    QComboBox *m_evap2DCombo = nullptr; ///< NO | YES | CLIMATE
    QCheckBox *m_transport2DBox[4] = {nullptr, nullptr, nullptr, nullptr};

    // U5 — the Groundwater group: [2D_OPTIONS] GROUNDWATER / GW_ET, plus the
    // Transport summary naming the [GW_*] authoring surface (U4).
    QGroupBox *m_gw2DGroup = nullptr;
    QComboBox *m_gw2DEnableCombo = nullptr;
    QComboBox *m_gwLinkCombo = nullptr;
    QCheckBox *m_gwWiltingAuto = nullptr;
    QDoubleSpinBox *m_gwWilting = nullptr;
    QLabel *m_gwEtAllocation = nullptr;
    QComboBox *m_gw2DEtCombo = nullptr;
    QLabel *m_gw2DStatusLabel = nullptr;
    QPushButton *m_gw2DEditBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMM_HAS_2D

#endif // TWODPAGE_H
