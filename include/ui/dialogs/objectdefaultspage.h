#ifndef OBJECTDEFAULTSPAGE_H
#define OBJECTDEFAULTSPAGE_H

/**
 * @file      objectdefaultspage.h
 * @brief     Preferences page editing PreferencesManager::ObjectDefaults — the
 *        per-object-type property defaults applied to newly created objects.
 *
 *        Self-contained: PreferencesDialog only calls loadFrom / applyTo /
 *        resetToSeeds, keeping the dialog's four-touchpoint pattern to a few
 *        lines. Both the US and SI sets are edited in one session; the
 *        unit-system selector at the top switches which set the widgets
 *        show, and applyTo() writes both.
 *
 *        Plan: workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
    /**
     * @brief Build a GUI preferences page with separate compiled US and SI draft defaults.
     * @param[in] parent Optional Qt owner of the page and its controls.
     * @details Initially displays US seeds. Controls and cached defaults are local until
     * applyTo() is called; existing project objects are never edited by this page.
     */
    explicit ObjectDefaultsPage(QWidget *parent = nullptr);

    /**
     * @brief Replace both draft sets from preferences and display the remembered editing unit
     * system.
     * @param[in] p Non-null borrowed preferences manager; read synchronously and not retained.
     * @details The saved editing-system choice wins; only its absence falls back to the active
     * UnitSystem (US if unavailable). Discards current unsaved widget/draft changes, blocks
     * selector signals and populates the chosen set without converting numbers.
     */
    void loadFrom(PreferencesManager *p);

    /**
     * @brief Save the visible draft, both unit-specific defaults and the editing-system choice.
     * @param[in,out] p Non-null borrowed preferences manager receiving both US and SI
     * ObjectDefaults sets and the selector state; not retained.
     * @details Reads widget values into the currently displayed cached set, then calls the
     * manager's settings mutators. No existing model objects are changed, no unit conversion
     * occurs and no error/status result is returned.
     *
     * @details Commit the visible widgets, then write BOTH sets to the manager.
     */
    void applyTo(PreferencesManager *p);

    /**
     * @brief Replace both local draft sets with compiled defaults and refresh the active set.
     * @details Keeps the current unit selector; does not persist changes until applyTo().
     *
     * @details Restore both sets to the compiled-in seeds and refresh the widgets.
     */
    void resetToSeeds();

private:
    using OD = PreferencesManager::ObjectDefaults;

    /**
     * @brief Create the owned Nodes, Links, Subcatchments and Rain Gages editors once.
     * @details Numeric widgets impose nonnegative ranges and display precision, with specific
     * integer bounds for barrels, contractions and rain intervals. Values retain selected
     * ObjectDefaults units: ft/m lengths, acres/hectares subcatchment area, inches/mm depression
     * and suction depths, inches/hour or mm/hour rates, hours/days/minutes as labeled.
     * Coefficients retain each unit system's stored semantics rather than being converted.
     */
    void buildUi();
    /**
     * @brief Load a unit-specific defaults draft into initialized widgets.
     * @param[in] d Borrowed US or SI ObjectDefaults value; copied into controls without
     * conversion or changing the other cached draft.
     * @details Spin boxes can round/clamp to their UI precision/range; unknown combo keywords
     * select row zero. The caller is responsible for selecting the matching m_showingSi state.
     */
    void populateWidgets(const OD &d);
    /**
     * @brief Overwrite a defaults value from initialized numeric, keyword and boolean controls.
     * @param[in,out] d Draft receiving all fields represented by this page in its current unit
     * system; widget values are copied without conversion or extra cross-field validation.
     * @details Does not write PreferencesManager or mutate the visible widgets; current control
     * precision and combo fallback choices determine saved values.
     */
    void commitWidgets(OD &d) const;
    /**
     * @brief Cache the outgoing draft and show the requested unit-specific draft.
     * @param[in] comboIndex One selects SI; every other value selects US.
     * @details Same-system requests do nothing. A real switch commits current controls to their
     * previous cache, changes m_showingSi and populates the other cache without converting
     * values or saving preferences.
     */
    void onUnitSystemSwitched(int comboIndex);

    OD m_us;
    OD m_si;
    bool m_showingSi = false;

    QComboBox *m_unitSystemCombo = nullptr;

    // Nodes
    QDoubleSpinBox *m_junctionMaxDepth = nullptr;
    QDoubleSpinBox *m_junctionInitDepth = nullptr;
    QDoubleSpinBox *m_junctionSurDepth = nullptr;
    QDoubleSpinBox *m_junctionPondedArea = nullptr;
    QDoubleSpinBox *m_junctionStorageArea = nullptr;
    QComboBox *m_outfallType = nullptr;
    QCheckBox *m_outfallFlapGate = nullptr;
    QDoubleSpinBox *m_storageMaxDepth = nullptr;
    QDoubleSpinBox *m_storageFuncCoeff = nullptr;
    QDoubleSpinBox *m_storageFuncExponent = nullptr;
    QDoubleSpinBox *m_storageFuncConstant = nullptr;
    QDoubleSpinBox *m_storageSeepRate = nullptr;
    QComboBox *m_dividerType = nullptr;

    // Links
    QComboBox *m_conduitShape = nullptr;
    QDoubleSpinBox *m_conduitGeom1 = nullptr;
    QDoubleSpinBox *m_conduitGeom2 = nullptr;
    QDoubleSpinBox *m_conduitGeom3 = nullptr;
    QDoubleSpinBox *m_conduitGeom4 = nullptr;
    QDoubleSpinBox *m_conduitRoughness = nullptr;
    QDoubleSpinBox *m_conduitLength = nullptr;
    QSpinBox *m_conduitBarrels = nullptr;
    QDoubleSpinBox *m_conduitLossInlet = nullptr;
    QDoubleSpinBox *m_conduitLossOutlet = nullptr;
    QCheckBox *m_conduitFlapGate = nullptr;
    QCheckBox *m_pumpInitStateOn = nullptr;
    QDoubleSpinBox *m_pumpStartupDepth = nullptr;
    QDoubleSpinBox *m_pumpShutoffDepth = nullptr;
    QComboBox *m_orificeType = nullptr;
    QDoubleSpinBox *m_orificeGeom1 = nullptr;
    QDoubleSpinBox *m_orificeCd = nullptr;
    QCheckBox *m_orificeFlapGate = nullptr;
    QDoubleSpinBox *m_orificeOpenCloseRate = nullptr;
    QComboBox *m_weirType = nullptr;
    QDoubleSpinBox *m_weirGeom1 = nullptr;
    QDoubleSpinBox *m_weirGeom2 = nullptr;
    QDoubleSpinBox *m_weirCd = nullptr;
    QSpinBox *m_weirEndContractions = nullptr;
    QCheckBox *m_weirFlapGate = nullptr;
    QComboBox *m_outletRatingType = nullptr;
    QDoubleSpinBox *m_outletCoeff = nullptr;
    QDoubleSpinBox *m_outletExponent = nullptr;
    QCheckBox *m_outletFlapGate = nullptr;

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
    QComboBox *m_gageRainFormat = nullptr;
    QSpinBox *m_gageIntervalMin = nullptr;
    QDoubleSpinBox *m_gageSnowCatch = nullptr;
};

#endif // OBJECTDEFAULTSPAGE_H
