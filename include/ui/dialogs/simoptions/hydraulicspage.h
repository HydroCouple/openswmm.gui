/*!
 * \file   hydraulicspage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */
#ifndef HYDRAULICSPAGE_H
#define HYDRAULICSPAGE_H

#include "ui/dialogs/simoptions/simoptionspage.h"

class QCheckBox;
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
    explicit HydraulicsPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;
    void applyCapabilities() override;
    void refreshGates() override;

    /*! \brief Tab order, so the dialog can name gates without magic numbers. */
    enum Tab { TabRouting = 0, TabDynamicWave, TabFiniteVolume, TabUnsteadyFriction };

    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }
    /*! \brief Current FLOW_ROUTING token (STEADY | KINWAVE | DYNWAVE | FV). */
    [[nodiscard]] QString flowRouting() const;
    /*! \brief Used by the System / Performance fast preset. */
    void setMinimumStep(double seconds);

private:
    void buildUi();
    void tagWidgets();

    QTabWidget *m_tabs = nullptr;

    QCheckBox      *m_andersonAccelBox  = nullptr;
    QDoubleSpinBox *m_dpsAlphaSpin      = nullptr;
    QDoubleSpinBox *m_dpsCelerSpin      = nullptr;
    QDoubleSpinBox *m_dpsDecaySpin      = nullptr;
    QComboBox      *m_forceMainCombo    = nullptr;
    QComboBox      *m_fvBackendCombo      = nullptr;
    QDoubleSpinBox *m_fvCellLengthSpin    = nullptr;   // project length units; 0 = one cell/conduit
    QSpinBox       *m_fvCflCensusSpin     = nullptr;
    QDoubleSpinBox *m_fvCflSpin           = nullptr;
    QCheckBox      *m_fvCompactionBox     = nullptr;
    class QGroupBox *m_fvGroup            = nullptr;
    QComboBox      *m_fvLimiterCombo      = nullptr;   // 2nd order only
    QCheckBox      *m_fvLtsBox            = nullptr;
    QSpinBox       *m_fvLtsTiersSpin      = nullptr;   // needs LTS on
    QSpinBox       *m_fvMinCellsSpin      = nullptr;
    QSpinBox       *m_fvMinParallelSpin   = nullptr;
    QComboBox      *m_fvOrderCombo        = nullptr;
    class QGroupBox *m_fvPerfGroup        = nullptr;
    QCheckBox      *m_fvPressImplicitBox  = nullptr;   // FV_PRESSURIZED_IMPLICIT (experimental)
    QComboBox      *m_fvPressureClosureCombo = nullptr; // FV_PRESSURE_CLOSURE (SLOT|TPA)
    QComboBox      *m_fvRiemannCombo      = nullptr;
    QDoubleSpinBox *m_fvSlotCeleritySpin  = nullptr;   // project length units / s
    QComboBox      *m_fvStructCouplingCombo = nullptr;
    QComboBox      *m_fvTimeIntCombo      = nullptr;
    QDoubleSpinBox *m_headTolSpin       = nullptr;
    QComboBox      *m_inertialDampCombo = nullptr;
    QDoubleSpinBox *m_latFlowTolSpin    = nullptr;     // percent
    QDoubleSpinBox *m_lengtheningSpin   = nullptr;
    QSpinBox       *m_maxTrialsSpin     = nullptr;
    QDoubleSpinBox *m_minSlopeSpin      = nullptr;     // percent
    QDoubleSpinBox *m_minStepSpin       = nullptr;     // MINIMUM_STEP (seconds)
    QDoubleSpinBox *m_minSurfAreaSpin   = nullptr;
    QComboBox      *m_nodeContinuityCombo = nullptr;
    QComboBox      *m_normalFlowCombo   = nullptr;
    QComboBox      *m_routingCombo      = nullptr;
    QCheckBox      *m_skipSteadyBox     = nullptr;
    QComboBox      *m_surchargeCombo    = nullptr;
    QDoubleSpinBox *m_sysFlowTolSpin    = nullptr;     // percent
    QDoubleSpinBox *m_tpaCeleritySpin   = nullptr;   // TPA_CELERITY (TPA only)
    class QGroupBox *m_ufGroup            = nullptr;
    QDoubleSpinBox *m_ufK3Spin            = nullptr;   // UF_K3 (method != NONE only)
    QComboBox      *m_ufMethodCombo       = nullptr;   // UNSTEADY_FRICTION (NONE|VITKOVSKY)
    QDoubleSpinBox *m_variableStepSpin  = nullptr;};

} // namespace openswmmvis::ui

#endif // HYDRAULICSPAGE_H
