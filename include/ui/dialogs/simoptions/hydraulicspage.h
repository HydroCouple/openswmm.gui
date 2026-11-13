/**
 * @file      hydraulicspage.h
 * @brief     Simulation Options → Routing & Hydraulics.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → Routing & Hydraulics.
 *
 * The headline page of the restructure. FLOW_ROUTING sits in a header above
 * the tab bar because it gates three of the four tabs — it can neither live
 * inside one of them nor stay two sidebar rows away on Models / Processes,
 * which is where it used to be (PLAN §2.1).
 *
 * Tabs: Routing · Dynamic Wave · Finite Volume · Unsteady Friction.
 *
 * "Skip steady state" lands on the shared Routing tab rather than Dynamic
 * Wave: SWMMEngine::isInSteadyState() is called from the main routing step
 * with no routing-model guard, so SKIP_STEADY_STATE is honoured under every
 * method (PLAN §7 Q1, resolved by measurement).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef HYDRAULICSPAGE_H
#define HYDRAULICSPAGE_H

#include "ui/dialogs/simoptions/simoptionspage.h"
#include <QMap>

class QCheckBox;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QSpinBox;
class QTabWidget;

namespace openswmmvis::ui
{

class HydraulicsPage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Construct routing and hydraulic draft controls from cached engine capabilities.
     * @param ctx Borrowed GUI-thread context that must outlive the page; valid handles
     * and a well-formed implicit schema are required whenever those paths are used.
     * @param parent Optional widget owner; null leaves lifetime to the caller.
     * Builds/tags controls; hydration and applying capability gates are separate.
     */
    explicit HydraulicsPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned Routing & Hydraulics string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Hydrate hydraulic drafts from common options, preferences and fixed solver defaults.
     * Local numeric readers accept a borrowed key plus numeric fallback, returning the
     * converted value or fallback on conversion failure before widget clamping. Combo
     * selector borrows a non-null combo and token, preserving selection on no match;
     * boolean helper maps its scalar to YES/NO. Signals are not blocked, but m_reading
     * suppresses Backward-Euler compatibility resets during hydration. Reads implicit
     * text only when supported, then clears m_reading and refreshes intra-page gates.
     * Does not call option setters, validate the complete draft or save files.
     */
    void read() override;
    /**
     * @brief Attempt common hydraulic and FV drafts independently through context comparison.
     * @return Successful setter sum; no-op/refusal contributes zero and partial success
     * has no rollback. Writes inactive/disabled solver drafts too; implicit method,
     * preconditioner and text fields are included only when cached support is true.
     * Numeric formatting follows each widget/key precision and implicit text is passed
     * verbatim. SKIP_STEADY_STATE is read/tagged here but is not submitted by this
     * implementation.
     * LAT_FLOW_TOL and SYS_FLOW_TOL are submitted in percent. Does not invoke validate(),
     * create undo or save files; the count does not certify a coherent complete solver setup.
     */
    int write() override;
    /**
     * @brief Disable unsupported solver choices/controls using the cached capability snapshot.
     * A local helper borrows combo, exact item-data token, tooltip and fallback index;
     * with a standard item model it disables the matching item and changes a currently
     * selected unsupported item to that fallback. Null/nonstandard combos are ignored.
     * FV falls back to dynamic wave, TPA/dynamic slot to EXTRAN and semi-implicit to
     * explicit. Pressure closure/Anderson are disabled where unsupported; UF receives
     * a tooltip while host gates control its tab. Does not re-probe, re-enable choices
     * or write options, though fallback selection can emit normal gate signals.
     */
    void applyCapabilities() override;
    /**
     * @brief Refresh intra-page controls for the current routing/integration/surcharge drafts.
     * Native Backward Euler under FV enables the schema group/tab and disables explicit
     * performance/reconstruction controls, skip-steady and friction-method selection.
     * Other gates cover DPS/TPA celerity, second-order limiter, LTS tiers, UF coefficient
     * and steady-flow tolerances. Uses cached capability flags; host controls other tabs.
     * Assumes buildUi() completed, does not normalize incompatible drafts or write keys.
     */
    void refreshGates() override;
    /**
     * @brief Check native implicit field input and Backward-Euler compatibility.
     * @param warn Optional borrowed error output, overwritten on the first refusal and
     * left unchanged on success; this method does not append diagnostics.
     * @return True immediately without implicit support. Otherwise false for an active FV
     * Backward-Euler draft with incompatible order/closure/backend/LTS/pressure-only/
     * steady-skip/friction settings, unacceptable schema text or minimum step above
     * maximum. Schema text is checked even when the native integrator is inactive.
     * Requires schema editors for FV_IMPLICIT_MIN_STEP/MAX_STEP; does not repair drafts,
     * validate every explicit hydraulic setting or change engine state.
     */
    bool validate(QString *warn) override;

    /*! \brief Tab order, so the dialog can name gates without magic numbers. */
    enum Tab { TabRouting = 0, TabDynamicWave, TabFiniteVolume, TabUnsteadyFriction };

    /**
     * @brief Expose the owned tab container for host gating/navigation.
     * @return Borrowed container; caller must not delete or retain beyond page life.
     */
    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }
    /**
     * @brief Read the selected routing draft token.
     * @return Owned STEADY, KINWAVE, DYNWAVE or FV item data, or empty without the combo.
     * Does not query engine state or assert that the solver is supported/initialized.
     */
    [[nodiscard]] QString flowRouting() const;
    /**
     * @brief Update the minimum-routing-step draft from the performance preset.
     * @param seconds Desired seconds, clamped/rounded by the existing 0.01..360.0,
     * three-decimal spin box. Missing editor is a no-op; does not set MINIMUM_STEP in
     * the engine, change other controls or persist the draft.
     */
    void setMinimumStep(double seconds);

private:
    /**
     * @brief Create owned hydraulic controls, schema validators and gate connections once.
     * Builds native methods/preconditioners/fields from the cached schema, using integer
     * bounds or C-locale scientific validators with seventeen decimals. Flow/surcharge/
     * order/friction index and LTS/skip toggles ignore scalar arguments and notify host
     * gates; routing has two connections and can notify twice. Selecting Backward Euler
     * outside read() resets pending drafts to first-order SLOT/CPU, LTS/pressure-only/
     * skip-steady off and friction NONE, preserving cell count/celerity; it then refreshes
     * gates and notifies. This is draft normalization, not an engine transaction.
     * Time/decay/minimum-step fields use seconds; flow/slope tolerances use percent and
     * FV cell lengths/celerities use project length units. DPS celerity is displayed m/s.
     * Does not hydrate option values, start a solver or apply drafts.
     */
    void buildUi();
    /**
     * @brief Tag fixed routing/hydraulic controls with their common option keys.
     * Implicit schema widgets are tagged during buildUi(). Tags support reachability
     * inspection and do not themselves prove a corresponding write() call exists.
     */
    void tagWidgets();

    QTabWidget *m_tabs = nullptr;
    QGroupBox *m_implicitGroup = nullptr;
    QComboBox *m_implicitMethod = nullptr, *m_implicitPreconditioner = nullptr;
    QMap<QString, QLineEdit *> m_implicitFields;
    bool m_reading = false;

    QCheckBox *m_andersonAccelBox = nullptr;
    QDoubleSpinBox *m_dpsAlphaSpin = nullptr;
    QDoubleSpinBox *m_dpsCelerSpin = nullptr;
    QDoubleSpinBox *m_dpsDecaySpin = nullptr;
    QComboBox *m_forceMainCombo = nullptr;
    QComboBox *m_fvBackendCombo = nullptr;
    QDoubleSpinBox *m_fvCellLengthSpin = nullptr; // project length units; 0 = minimum cell count
    QSpinBox *m_fvCflCensusSpin = nullptr;
    QDoubleSpinBox *m_fvCflSpin = nullptr;
    QCheckBox *m_fvCompactionBox = nullptr;
    class QGroupBox *m_fvGroup = nullptr;
    QComboBox *m_fvLimiterCombo = nullptr; // 2nd order only
    QCheckBox *m_fvLtsBox = nullptr;
    QSpinBox *m_fvLtsTiersSpin = nullptr; // needs LTS on
    QSpinBox *m_fvMinCellsSpin = nullptr;
    QSpinBox *m_fvMinParallelSpin = nullptr;
    QComboBox *m_fvOrderCombo = nullptr;
    class QGroupBox *m_fvPerfGroup = nullptr;
    QCheckBox *m_fvPressImplicitBox = nullptr;     // FV_PRESSURIZED_IMPLICIT (experimental)
    QCheckBox *m_fvNodeFeedbackBox = nullptr;      // FV_NODE_FEEDBACK_DT
    QComboBox *m_fvPressureClosureCombo = nullptr; // FV_PRESSURE_CLOSURE (SLOT|TPA)
    QComboBox *m_fvRiemannCombo = nullptr;
    QDoubleSpinBox *m_fvSlotCeleritySpin = nullptr; // project length units / s
    QComboBox *m_fvStructCouplingCombo = nullptr;
    QComboBox *m_fvTimeIntCombo = nullptr;
    QDoubleSpinBox *m_headTolSpin = nullptr;
    QComboBox *m_inertialDampCombo = nullptr;
    QDoubleSpinBox *m_latFlowTolSpin = nullptr; // percent
    QDoubleSpinBox *m_lengtheningSpin = nullptr;
    QSpinBox *m_maxTrialsSpin = nullptr;
    QDoubleSpinBox *m_minSlopeSpin = nullptr; // percent
    QDoubleSpinBox *m_minStepSpin = nullptr;  // MINIMUM_STEP (seconds)
    QDoubleSpinBox *m_minSurfAreaSpin = nullptr;
    QComboBox *m_nodeContinuityCombo = nullptr;
    QComboBox *m_normalFlowCombo = nullptr;
    QComboBox *m_routingCombo = nullptr;
    QCheckBox *m_skipSteadyBox = nullptr;
    QComboBox *m_surchargeCombo = nullptr;
    QDoubleSpinBox *m_sysFlowTolSpin = nullptr;  // percent
    QDoubleSpinBox *m_tpaCeleritySpin = nullptr; // TPA_CELERITY (TPA only)
    class QGroupBox *m_ufGroup = nullptr;
    QDoubleSpinBox *m_ufK3Spin = nullptr; // UF_K3 (method != NONE only)
    QComboBox *m_ufMethodCombo = nullptr; // UNSTEADY_FRICTION (NONE|VITKOVSKY)
    QDoubleSpinBox *m_variableStepSpin = nullptr;
};

} // namespace openswmmvis::ui

#endif // HYDRAULICSPAGE_H
