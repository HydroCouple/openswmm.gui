/**
 * @file      unitsystem.h
 * @brief     Per-project flow-units state + label helpers.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef UNITSYSTEM_H
#define UNITSYSTEM_H

#include <QObject>
#include <QString>

#include <openswmm/legacy/engine/openswmm_solver.h>
#include <openswmm/engine/openswmm_model.h>

/**
 * @brief Per-project flow-units state + label helpers.
 *
 * Each SWMMVisProjectWindow owns one instance and writes/reads the FLOW_UNITS
 * option through it. The class also exposes a delegating facade via
 * UnitSystem::instance() — a singleton whose accessors forward to whichever
 * per-project instance is currently active (set on MDI tab switch).
 *
 * Existing callsites like `UnitSystem::instance()->depthLabel()` keep working
 * unchanged; the value reflects the focused project, not a global setting.
 */
class UnitSystem : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct project-local units initialized to CFS (US customary).
     * @param[in] parent Optional Qt owner; the object follows normal QObject lifetime rules.
     */
    explicit UnitSystem(QObject *parent = nullptr);

    // ---- Delegating facade ---------------------------------------------------

    /** Singleton facade — accessors below forward to the active project.     * @return Borrowed process-lifetime facade; do not delete it. Without an active project its getters report CFS.
     */
    static UnitSystem *instance();

    /** Bind the facade to a per-project UnitSystem (or null when no project).     * @param[in] units Borrowed project-local instance, or nullptr to detach. QPointer clears a destroyed target.
     * @details Rebinds unitsChanged forwarding and emits the facade signal on a changed binding. Call from the GUI thread.
     */
    static void setActiveProject(UnitSystem *units);

    /** Returns the currently-bound per-project UnitSystem (may be null).     * @return Borrowed current target, or nullptr after detachment or target destruction.
     */
    static UnitSystem *activeProject();

    // ---- Per-project state ---------------------------------------------------

    /**
     * @brief Read project flow units, forwarding through the facade when appropriate.
     * @return Stored FLOW_UNITS choice; the detached facade reports swmm_CFS.
     */
    swmm_FlowUnitsProperty flowUnits() const;

    /** Read units from an open engine (call after swmm_engine_open).     * @param[in] engine Borrowed open engine from which FLOW_UNITS is read.
     * @details A failed read leaves state unchanged. Unrecognized text resolves to CFS; a changed value emits unitsChanged. The facade forwards only when a project is bound.
     */
    void syncFromEngine(SWMM_Engine engine);

    /** Write new units to an open engine and emit unitsChanged.     * @param[in] units Valid swmm_FlowUnitsProperty enumerator in the CFS through MLD range.
     * @param[in] engine Optional borrowed open engine; nullptr changes GUI state only.
     * @details An unchanged value is a no-op. Otherwise updates stored units, attempts to write FLOW_UNITS when an engine is supplied, and emits unitsChanged; engine write errors are not returned. The detached facade does nothing.
     */
    void setFlowUnits(swmm_FlowUnitsProperty units, SWMM_Engine engine = nullptr);

    /**
     * @brief Classify the current flow-unit system.
     * @return True for CMS, LPS or MLD; false for US customary units.
     */
    bool isSI() const;
    /**
     * @brief Return the display unit for the current project.
     * @return FLOW_UNITS abbreviation (CFS, GPM, MGD, CMS, LPS or MLD).
     */
    QString flowUnitLabel() const; // "CFS", "CMS", …
    /**
     * @brief Return the display unit for the current project.
     * @return m for SI or ft for US customary lengths.
     */
    QString lengthLabel() const; // "ft" or "m"
    /**
     * @brief Return the display unit for the current project.
     * @return m for SI or ft for US customary depths.
     */
    QString depthLabel() const; // "ft" or "m"
    /**
     * @brief Return the display unit for the current project.
     * @return m/s for SI or ft/s for US customary velocity.
     */
    QString velocityLabel() const; // "ft/s" or "m/s"
    /**
     * @brief Return the display unit for the current project.
     * @return ha for SI or ac for US customary subcatchment area.
     */
    QString areaLabel() const; // "ac" or "ha"
    /**
     * @brief Return the display unit for the current project.
     * @return m³ for SI or ft³ for US customary volume.
     */
    QString volumeLabel() const; // "ft³" or "m³"

signals:
    /**
     * @brief Notify listeners after the stored unit or active-project binding changes.
     * @param[in] newUnits Flow-unit choice now reported by the emitting object.
     */
    void unitsChanged(swmm_FlowUnitsProperty newUnits);

private:
    swmm_FlowUnitsProperty mFlowUnits = swmm_CFS;
};

#endif // UNITSYSTEM_H
