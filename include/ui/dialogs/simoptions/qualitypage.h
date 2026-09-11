/*!
 * \file   qualitypage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
    explicit QualityPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;
    void applyCapabilities() override;
    void refreshGates() override;

    /*! \brief Tab order, so the dialog can name gates without magic numbers. */
    enum Tab { TabGeneral = 0, TabArd, TabLard, TabReserved };

    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }
    /*! \brief Current QUALITY_SOLVER token (LEGACY | EULERIAN_ARD | LAGRANGIAN). */
    [[nodiscard]] QString qualitySolver() const;
    /*! \brief True while WATER_AGE or HEAT_TRANSPORT is ticked — half of the
     *         dialog's "is there anything to transport?" row gate. */
    [[nodiscard]] bool tracksReservedSpecies() const;

private:
    void buildUi();
    void tagWidgets();

    QTabWidget *m_tabs = nullptr;

    QComboBox      *m_qualitySolverCombo   = nullptr;  // QUALITY_SOLVER (header)
    QComboBox      *m_outfallBackflowCombo = nullptr;  // OUTFALL_BACKFLOW_QUALITY
    QDoubleSpinBox *m_qualityStepSpin      = nullptr;  // QUALITY_STEP (s)
    QSpinBox       *m_maxSegmentsSpin      = nullptr;  // MAX_SEGMENTS_PER_LINK
    QComboBox      *m_dispersionCombo      = nullptr;  // DISPERSION OFF|RWPT
    QSpinBox       *m_rwptSeedSpin         = nullptr;  // RWPT_SEED (RWPT only)
    QCheckBox      *m_waterAgeBox          = nullptr;  // WATER_AGE
    QCheckBox      *m_heatTransportBox     = nullptr;  // HEAT_TRANSPORT
    QComboBox      *m_fvScalarSchemeCombo  = nullptr;  // FV_SCALAR_SCHEME
};

} // namespace openswmmvis::ui

#endif // QUALITYPAGE_H
