/**
 * @file      qualitypage.h
 * @brief     Simulation Options → Quality & Transport.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → Quality & Transport.
 *
 * QUALITY_SOLVER sits in a page header above the tab bar for the same reason
 * FLOW_ROUTING does on Routing & Hydraulics: it gates two of the four tabs
 * (PLAN §2.1).
 *
 * Tabs: General · Eulerian ARD · Lagrangian (LARD) · Reserved Species.
 *
 * "General" is the tab that applies under every solver — the same role the
 * "Routing" tab plays on Routing & Hydraulics. It is not called "Solver":
 * the solver selector is the page header, not a control on that tab.
 *
 * FV_SCALAR_SCHEME stays on Eulerian ARD, not on Routing & Hydraulics: its
 * only live consumer is the ARD engine, which reads it under any routing
 * model (PLAN §1.4).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef QUALITYPAGE_H
#define QUALITYPAGE_H

#include "ui/dialogs/simoptions/simoptionspage.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTabWidget;

namespace openswmmvis::ui
{

class QualityPage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Construct the owned quality/transport option draft controls and secondary editors.
     * @param ctx Borrowed GUI-thread context that must outlive the page and provide valid
     * backing engine/layer/project handles while accessed.
     * @param parent Optional QWidget owner; null leaves lifetime to the caller.
     * Builds/tags controls only; read() and capability/gate application are separate.
     */
    explicit QualityPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned Quality & Transport string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Hydrate quality drafts from common options with fixed engine-default fallbacks.
     * Numeric conversion failures use fallbacks before widget clamping; unknown combo
     * tokens leave the prior selection unchanged. Unrecognized boolean text becomes
     * unchecked. Signals are not blocked, so changed solver/species inputs can notify
     * the host and changed dispersion refreshes its gate. No engine setters run.
     */
    void read() override;
    /**
     * @brief Attempt all nine quality option keys through context change detection.
     * @return Sum of successful setters; no-op and refused writes contribute zero.
     * Writes independently of selected solver or disabled widgets, preserving configured
     * inactive-solver drafts. QUALITY_STEP uses two decimals; other values use tokens
     * or integers. No transaction, validation, undo creation or file save occurs here.
     */
    int write() override;
    /**
     * @brief Disable solver, backflow, age and heat controls when cached transport support is
     * absent.
     * Provides legacy/pre-option-surface tooltips. Supported capability is an early no-op,
     * so this method does not re-enable previously disabled controls. Host tab gates
     * are separate; no engine probe, draft clearing or option write is performed.
     */
    void applyCapabilities() override;
    /**
     * @brief Enable RWPT seed only for the exact RWPT dispersion draft token.
     * Refreshes its tooltip and returns safely without either control. Solver-dependent
     * tab gating belongs to the host; this method neither edits the seed nor writes it.
     */
    void refreshGates() override;

    /*! \brief Tab order, so the dialog can name gates without magic numbers. */
    enum Tab { TabGeneral = 0, TabArd, TabLard, TabReserved };

    /**
     * @brief Expose the owned tab container for host tab gating/navigation.
     * @return Borrowed container; do not delete or retain beyond the page's lifetime.
     */
    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }
    /**
     * @brief Read the current solver draft token.
     * @return Owned LEGACY, EULERIAN_ARD or LAGRANGIAN item data, or empty without the combo.
     * Does not query engine state or apply the draft.
     */
    [[nodiscard]] QString qualitySolver() const;
    /**
     * @brief Test the age/temperature draft switches for cross-page transport gating.
     * @return True when either existing checkbox is checked, regardless of solver or
     * widget enablement; false when both are absent/unchecked. No engine query.
     */
    [[nodiscard]] bool tracksReservedSpecies() const;
    /**
     * @brief Read the draft heat-transport switch.
     * @return True only for an existing checked heat checkbox, even if disabled.
     */
    bool heatEnabled() const;
    /**
     * @brief Read the draft water-age switch.
     * @return True only for an existing checked age checkbox, even if disabled.
     */
    bool ageEnabled() const;
    /**
     * @brief Synchronize the heat draft checkbox from another editor.
     * @param enabled Desired checked state, including when the control is disabled.
     * Assumes buildUi() completed; changed state can emit gateInputsChanged through the
     * checkbox connection. Does not directly write HEAT_TRANSPORT or block signals.
     */
    void setHeatEnabled(bool enabled);

signals:
    /**
     * @brief Request that the host open the heat editor within its settings edit session.
     * The signal alone applies no option and conveys no success/acceptance result.
     */
    void showHeatSettingsRequested();

public:
private:
    /**
     * @brief Create owned solver, ARD/LARD, reserved-species controls and connections once.
     * Solver index and age/heat toggle callbacks ignore their supplied scalar value and
     * emit gateInputsChanged; dispersion index callbacks refresh only the seed gate.
     * Heat settings emit a host request. Source-age and initial-quality buttons open
     * modal dialogs that write directly to the engine, so their accepted/partial writes
     * survive outer Cancel. Layer/project close signals invalidate those child engines;
     * project changes are marked on child changesApplied. Source-age additionally guards
     * the project with QPointer across exec and checks wroteAnyChanges afterward.
     * Controls otherwise hold drafts; this builder performs no common-option commit.
     */
    void buildUi();
    /**
     * @brief Associate all nine common quality option keys with their draft widgets.
     * Tags support reachability inspection without querying or persisting values.
     */
    void tagWidgets();

    QTabWidget *m_tabs = nullptr;

    QComboBox *m_qualitySolverCombo = nullptr;   // QUALITY_SOLVER (header)
    QComboBox *m_outfallBackflowCombo = nullptr; // OUTFALL_BACKFLOW_QUALITY
    QDoubleSpinBox *m_qualityStepSpin = nullptr; // QUALITY_STEP (s)
    QSpinBox *m_maxSegmentsSpin = nullptr;       // MAX_SEGMENTS_PER_LINK
    QComboBox *m_dispersionCombo = nullptr;      // DISPERSION OFF|RWPT
    QSpinBox *m_rwptSeedSpin = nullptr;          // RWPT_SEED (RWPT only)
    QCheckBox *m_waterAgeBox = nullptr;          // WATER_AGE
    QCheckBox *m_heatTransportBox = nullptr;     // HEAT_TRANSPORT
    QComboBox *m_fvScalarSchemeCombo = nullptr;  // FV_SCALAR_SCHEME
};

} // namespace openswmmvis::ui

#endif // QUALITYPAGE_H
