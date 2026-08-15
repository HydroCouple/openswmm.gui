#ifndef OBJECTDEFAULTSPAGE_H
#define OBJECTDEFAULTSPAGE_H

/*!
 * \file  objectdefaultspage.h
 * \brief Preferences page editing PreferencesManager::ObjectDefaults — the
 *        per-object-type property defaults applied to newly created objects.
 *
 *        Self-contained: PreferencesDialog only calls loadFrom / applyTo /
 *        resetToSeeds, keeping the dialog's four-touchpoint pattern to a few
 *        lines. Both the US and SI sets are edited in one session; the
 *        unit-system selector at the top switches which set the widgets
 *        show, and applyTo() writes both.
 *
 *        Plan: workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md
 */

#include <QWidget>

#include "core/preferencesmanager.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

class ObjectDefaultsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ObjectDefaultsPage(QWidget *parent = nullptr);

    //! Pull both sets from the manager and show the set matching the
    //! active project's unit system (US when no project is open).
    void loadFrom(PreferencesManager *p);

    //! Commit the visible widgets, then write BOTH sets to the manager.
    void applyTo(PreferencesManager *p);

    //! Restore both sets to the compiled-in seeds and refresh the widgets.
    void resetToSeeds();

private:
    using OD = PreferencesManager::ObjectDefaults;

    void buildUi();
    void populateWidgets(const OD &d);
    void commitWidgets(OD &d) const;
    void onUnitSystemSwitched(int comboIndex);

    OD   m_us;
    OD   m_si;
    bool m_showingSi = false;

    QComboBox *m_unitSystemCombo = nullptr;

    // Nodes
    QDoubleSpinBox *m_junctionMaxDepth = nullptr;
    QDoubleSpinBox *m_junctionInitDepth = nullptr;
    QDoubleSpinBox *m_junctionSurDepth = nullptr;
    QDoubleSpinBox *m_junctionPondedArea = nullptr;
    QComboBox      *m_outfallType = nullptr;
    QCheckBox      *m_outfallFlapGate = nullptr;
    QDoubleSpinBox *m_storageMaxDepth = nullptr;
    QDoubleSpinBox *m_storageFuncCoeff = nullptr;
    QDoubleSpinBox *m_storageFuncExponent = nullptr;
    QDoubleSpinBox *m_storageFuncConstant = nullptr;
    QDoubleSpinBox *m_storageSeepRate = nullptr;
    QComboBox      *m_dividerType = nullptr;

    // Links
    QComboBox      *m_conduitShape = nullptr;
    QDoubleSpinBox *m_conduitGeom1 = nullptr;
    QDoubleSpinBox *m_conduitGeom2 = nullptr;
    QDoubleSpinBox *m_conduitGeom3 = nullptr;
    QDoubleSpinBox *m_conduitGeom4 = nullptr;
    QDoubleSpinBox *m_conduitRoughness = nullptr;
    QDoubleSpinBox *m_conduitLength = nullptr;
    QSpinBox       *m_conduitBarrels = nullptr;
    QDoubleSpinBox *m_conduitLossInlet = nullptr;
    QDoubleSpinBox *m_conduitLossOutlet = nullptr;
    QCheckBox      *m_conduitFlapGate = nullptr;
    QCheckBox      *m_pumpInitStateOn = nullptr;
    QDoubleSpinBox *m_pumpStartupDepth = nullptr;
    QDoubleSpinBox *m_pumpShutoffDepth = nullptr;
    QComboBox      *m_orificeType = nullptr;
    QDoubleSpinBox *m_orificeGeom1 = nullptr;
    QDoubleSpinBox *m_orificeCd = nullptr;
    QCheckBox      *m_orificeFlapGate = nullptr;
    QDoubleSpinBox *m_orificeOpenCloseRate = nullptr;
    QComboBox      *m_weirType = nullptr;
    QDoubleSpinBox *m_weirGeom1 = nullptr;
    QDoubleSpinBox *m_weirGeom2 = nullptr;
    QDoubleSpinBox *m_weirCd = nullptr;
    QSpinBox       *m_weirEndContractions = nullptr;
    QCheckBox      *m_weirFlapGate = nullptr;
    QComboBox      *m_outletRatingType = nullptr;
    QDoubleSpinBox *m_outletCoeff = nullptr;
    QDoubleSpinBox *m_outletExponent = nullptr;
    QCheckBox      *m_outletFlapGate = nullptr;

    // Subcatchments
    QDoubleSpinBox *m_subcatchArea = nullptr;
    QDoubleSpinBox *m_subcatchWidth = nullptr;
    QDoubleSpinBox *m_subcatchSlopePct = nullptr;
    QDoubleSpinBox *m_subcatchImpervPct = nullptr;
    QDoubleSpinBox *m_subcatchNImperv = nullptr;
    QDoubleSpinBox *m_subcatchNPerv = nullptr;
    QDoubleSpinBox *m_subcatchDsImperv = nullptr;
    QDoubleSpinBox *m_subcatchDsPerv = nullptr;
    QDoubleSpinBox *m_subcatchPctZeroImperv = nullptr;
    QDoubleSpinBox *m_hortonMaxRate = nullptr;
    QDoubleSpinBox *m_hortonMinRate = nullptr;
    QDoubleSpinBox *m_hortonDecay = nullptr;
    QDoubleSpinBox *m_hortonDryTime = nullptr;
    QDoubleSpinBox *m_gaSuction = nullptr;
    QDoubleSpinBox *m_gaKsat = nullptr;
    QDoubleSpinBox *m_gaImd = nullptr;
    QDoubleSpinBox *m_cnCurveNumber = nullptr;
    QDoubleSpinBox *m_cnDryTime = nullptr;

    // Rain gages
    QComboBox      *m_gageRainFormat = nullptr;
    QSpinBox       *m_gageIntervalMin = nullptr;
    QDoubleSpinBox *m_gageSnowCatch = nullptr;
};

#endif // OBJECTDEFAULTSPAGE_H
