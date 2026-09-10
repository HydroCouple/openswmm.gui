/*!
 * \file   twodpage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Simulation Options → 2D Surface Routing.
 *
 * Tabs: Hydrodynamics · Mesh & Closure · Coupling · Processes · Rainfall &
 * Output. Five rather than PLAN §2's four: §1.3 reserved "a Processes tab
 * after Coupling", and that content (the 2D process switches and the
 * groundwater enables, U1/U5) now exists.
 *
 * Every key on this page is an `[2D_OPTIONS]` key and goes through
 * `swmm_options_set_ext`, not the plain `[OPTIONS]` setter — so the page keeps
 * its own writeIfChanged rather than using the context's.
 *
 * The page is only built when the engine ships the 2D module; the whole class
 * is compiled out otherwise.
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
    explicit TwoDPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;
    void refreshGates() override;

    /*! \brief Tab order, so the dialog can name tabs without magic numbers. */
    enum Tab { TabHydrodynamics = 0, TabMesh, TabCoupling, TabProcesses, TabOutput };

    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }

    /*!
     * \brief What this page needs from Dates & Times (PLAN §4.1).
     *
     * Two of its steps can mirror the project's, and the output-size estimate
     * multiplies by the run length. All three are read through the owning
     * page's accessors rather than by reaching for its widgets.
     */
    struct ScheduleHooks {
        std::function<qint64()> wetStepSeconds;
        std::function<qint64()> reportStepSeconds;
        std::function<qint64()> durationSeconds;
    };
    void setScheduleHooks(ScheduleHooks hooks);

    /*! \brief TRANSPORT_* state, read by the Models page's matrix. */
    [[nodiscard]] bool transport2DEnabled(int speciesClass) const;
    void setTransport2DEnabled(int speciesClass, bool on);

public slots:
    /*! \brief Dates & Times moved a step or the run window; re-mirror and
     *         re-estimate. */
    void scheduleChanged();

signals:
    /*! \brief A TRANSPORT_* box moved — the Models page's matrix is stale. */
    void transportSelectionChanged();
    /*! \brief A sub-dialog wrote straight to the engine; the project is
     *         dirty regardless of this dialog's own write pass. */
    void engineEditedDirectly();

private:
    void buildUi();
    void tagWidgets();
    void updateOutputSizeEstimate();
    [[nodiscard]] unsigned reportVarsMask() const;
    void     setReportVarsMask(unsigned mask);
    [[nodiscard]] QString reportSpeciesText() const;   ///< "ALL" or space-separated
    void     setReportSpeciesText(const QString &text);

    QTabWidget *m_tabs = nullptr;
    ScheduleHooks m_schedule;

    QDoubleSpinBox *m_maxTimestepSpin   = nullptr;
    QGroupBox      *m_marcherGroup      = nullptr;
    QDoubleSpinBox *m_thetaSpin         = nullptr;
    QDoubleSpinBox *m_cflNumberSpin     = nullptr;
    QSpinBox       *m_ltsTiersSpin      = nullptr;
    QDoubleSpinBox *m_hMoveSpin         = nullptr;
    QDoubleSpinBox *m_froudeMaxSpin     = nullptr;
    QComboBox      *m_momentum2DCombo   = nullptr;   ///< MOMENTUM_EQUATION
    QSpinBox       *m_reconOrder2DSpin  = nullptr;   ///< RECONSTRUCTION_ORDER
    QCheckBox      *m_advection2DBox    = nullptr;
    QComboBox      *m_backend2DCombo    = nullptr;   ///< BACKEND
    QCheckBox      *m_couplingAreaAutoBox = nullptr;
    QDoubleSpinBox *m_dryDepthSpin      = nullptr;
    QDoubleSpinBox *m_limiterEpsSpin    = nullptr;
    QDoubleSpinBox *m_fluxDhEpsSpin     = nullptr;
    QComboBox      *m_cellClosureCombo  = nullptr;
    QComboBox      *m_faceReconCombo    = nullptr;
    QDoubleSpinBox *m_vfrMinWetFracSpin = nullptr;
    QDoubleSpinBox *m_couplingCdSpin    = nullptr;
    QDoubleSpinBox *m_couplingSyncSpin  = nullptr;
    QComboBox      *m_rainfall2DModeCombo = nullptr;
    QCheckBox      *m_report2DBox       = nullptr;
    QLineEdit      *m_output2DFileEdit  = nullptr;

    // E1 — OUTPUT_PRECISION / OUTPUT_COMPRESSION / REPORT_2D_STEP /
    // REPORT_2D_VARIABLES / REPORT_2D_SPECIES.
    QComboBox      *m_output2DPrecisionCombo   = nullptr;
    QSpinBox       *m_output2DCompressionSpin  = nullptr;
    QCheckBox      *m_report2DStepSameBox      = nullptr;  ///< checked ⇒ REPORT_2D_STEP = REPORT_STEP (0)
    QCustomTimespanEdit *m_report2DStepEdit    = nullptr;
    QListWidget    *m_report2DVarsList         = nullptr;  ///< one checkable row per swmm_2d_output_variable_name
    QCheckBox      *m_report2DAllSpeciesBox    = nullptr;  ///< checked ⇒ REPORT_2D_SPECIES ALL
    QListWidget    *m_report2DSpeciesList      = nullptr;  ///< pollutants + MSX species
    QLabel         *m_output2DSizeLabel        = nullptr;  ///< live size estimate

    // U1 — Processes group ([2D_OPTIONS] process keys, E2).
    QComboBox           *m_infil2DModeCombo    = nullptr;  ///< AUTO | YES | NO
    QCheckBox           *m_infil2DStepSameBox  = nullptr;  ///< checked ⇒ INFIL_STEP = WET_STEP (0)
    QCustomTimespanEdit *m_infil2DStepEdit     = nullptr;
    QComboBox           *m_infil2DMethodCombo  = nullptr;
    QComboBox           *m_infil2DDestCombo    = nullptr;
    QPushButton         *m_editInfilCellsBtn   = nullptr;
    QComboBox           *m_evap2DCombo         = nullptr;  ///< NO | YES | CLIMATE
    QCheckBox           *m_transport2DBox[4]   = {nullptr, nullptr, nullptr, nullptr};

    // U5 — the Groundwater group: [2D_OPTIONS] GROUNDWATER / GW_ET, plus the
    // Transport summary naming the [GW_*] authoring surface (U4).
    QGroupBox   *m_gw2DGroup       = nullptr;
    QComboBox   *m_gw2DEnableCombo = nullptr;
    QComboBox   *m_gw2DEtCombo     = nullptr;
    QLabel      *m_gw2DStatusLabel = nullptr;
    QPushButton *m_gw2DEditBtn     = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMM_HAS_2D

#endif // TWODPAGE_H
