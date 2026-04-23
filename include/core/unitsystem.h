/*!
 * \file   unitsystem.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#ifndef UNITSYSTEM_H
#define UNITSYSTEM_H

#include <QObject>
#include <QString>

#ifdef HAVE_OPENSWMMCORE
#include <openswmm/legacy/engine/openswmm_solver.h>
#include <openswmm/engine/openswmm_model.h>
#else
typedef enum { swmm_CFS=0, swmm_GPM=1, swmm_MGD=2, swmm_CMS=3, swmm_LPS=4, swmm_MLD=5 } swmm_FlowUnitsProperty;
typedef void* SWMM_Engine;
#endif

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
    explicit UnitSystem(QObject *parent = nullptr);

    // ---- Delegating facade ---------------------------------------------------

    /** Singleton facade — accessors below forward to the active project. */
    static UnitSystem *instance();

    /** Bind the facade to a per-project UnitSystem (or null when no project). */
    static void setActiveProject(UnitSystem *units);

    /** Returns the currently-bound per-project UnitSystem (may be null). */
    static UnitSystem *activeProject();

    // ---- Per-project state ---------------------------------------------------

    swmm_FlowUnitsProperty flowUnits() const;

    /** Read units from an open engine (call after swmm_engine_open). */
    void syncFromEngine(SWMM_Engine engine);

    /** Write new units to an open engine and emit unitsChanged. */
    void setFlowUnits(swmm_FlowUnitsProperty units, SWMM_Engine engine = nullptr);

    bool    isSI()            const;
    QString flowUnitLabel()   const;  // "CFS", "CMS", …
    QString lengthLabel()     const;  // "ft" or "m"
    QString depthLabel()      const;  // "ft" or "m"
    QString velocityLabel()   const;  // "ft/s" or "m/s"
    QString areaLabel()       const;  // "ac" or "ha"
    QString volumeLabel()     const;  // "ft³" or "m³"

signals:
    void unitsChanged(swmm_FlowUnitsProperty newUnits);

private:
    swmm_FlowUnitsProperty mFlowUnits = swmm_CFS;
};

#endif // UNITSYSTEM_H
