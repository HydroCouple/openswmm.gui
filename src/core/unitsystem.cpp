/*!
 * \file   unitsystem.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "core/unitsystem.h"

#include <openswmm/engine/openswmm_model.h>

#include <QPointer>

namespace
{
    // The facade target — points at the active project's per-instance
    // UnitSystem. Cleared (or rebound) by SWMMVis::onActiveSubWindowChanged
    // each time the focused MDI subwindow changes.
    QPointer<UnitSystem> g_activeUnits;
}

UnitSystem::UnitSystem(QObject *parent)
    : QObject(parent)
{}

// ---------------------------------------------------------------------------
// Delegating facade
// ---------------------------------------------------------------------------

UnitSystem *UnitSystem::instance()
{
    // Facade singleton — its OWN accessors are never read; existing call
    // sites use it as a forwarder. We return a pointer to the facade object
    // itself so the QSignalBlocker / connect()-style call patterns at
    // existing sites still work, but every getter forwards to g_activeUnits.
    static UnitSystem s_facade;
    return &s_facade;
}

void UnitSystem::setActiveProject(UnitSystem *units)
{
    if (g_activeUnits.data() == units)
        return;

    UnitSystem *facade = instance();

    // Disconnect the facade re-broadcast from the previously-active project.
    if (g_activeUnits)
        QObject::disconnect(g_activeUnits.data(), &UnitSystem::unitsChanged,
                            facade, &UnitSystem::unitsChanged);

    g_activeUnits = units;

    // Re-broadcast project-level change events through the facade so that
    // existing listeners (status-bar combo, dialogs) refresh without knowing
    // which project they came from.
    if (units)
        QObject::connect(units, &UnitSystem::unitsChanged,
                         facade, &UnitSystem::unitsChanged,
                         Qt::UniqueConnection);

    // Notify listeners that the facade now reports a (potentially) different
    // value — a tab switch is observationally identical to a units change.
    emit facade->unitsChanged(facade->flowUnits());
}

UnitSystem *UnitSystem::activeProject()
{
    return g_activeUnits.data();
}

// ---------------------------------------------------------------------------
// Per-project state — facade methods forward to g_activeUnits when called on
// the singleton; per-project instances act on their own state directly.
// ---------------------------------------------------------------------------

swmm_FlowUnitsProperty UnitSystem::flowUnits() const
{
    if (this == instance())
        return g_activeUnits ? g_activeUnits->mFlowUnits : swmm_CFS;
    return mFlowUnits;
}

void UnitSystem::syncFromEngine(SWMM_Engine engine)
{
    if (this == instance())
    {
        if (g_activeUnits) g_activeUnits->syncFromEngine(engine);
        return;
    }

    char buf[32] = {};
    if (swmm_options_get(engine, "FLOW_UNITS", buf, sizeof(buf)) == 0)
    {
        QString s = QString(buf).trimmed().toUpper();
        swmm_FlowUnitsProperty u = swmm_CFS;
        if      (s == "CFS") u = swmm_CFS;
        else if (s == "GPM") u = swmm_GPM;
        else if (s == "MGD") u = swmm_MGD;
        else if (s == "CMS") u = swmm_CMS;
        else if (s == "LPS") u = swmm_LPS;
        else if (s == "MLD") u = swmm_MLD;
        if (u != mFlowUnits)
        {
            mFlowUnits = u;
            emit unitsChanged(mFlowUnits);
        }
    }
}

void UnitSystem::setFlowUnits(swmm_FlowUnitsProperty units, SWMM_Engine engine)
{
    if (this == instance())
    {
        if (g_activeUnits) g_activeUnits->setFlowUnits(units, engine);
        return;
    }

    if (units == mFlowUnits)
        return;

    mFlowUnits = units;

    if (engine)
    {
        static const char *labels[] = { "CFS","GPM","MGD","CMS","LPS","MLD" };
        swmm_options_set(engine, "FLOW_UNITS", labels[static_cast<int>(units)]);
    }

    emit unitsChanged(mFlowUnits);
}

bool UnitSystem::isSI() const
{
    const auto u = flowUnits();
    return u == swmm_CMS || u == swmm_LPS || u == swmm_MLD;
}

QString UnitSystem::flowUnitLabel() const
{
    switch (flowUnits())
    {
        case swmm_CFS: return QStringLiteral("CFS");
        case swmm_GPM: return QStringLiteral("GPM");
        case swmm_MGD: return QStringLiteral("MGD");
        case swmm_CMS: return QStringLiteral("CMS");
        case swmm_LPS: return QStringLiteral("LPS");
        case swmm_MLD: return QStringLiteral("MLD");
    }
    return QStringLiteral("CFS");
}

QString UnitSystem::lengthLabel() const
{
    return isSI() ? QStringLiteral("m") : QStringLiteral("ft");
}

QString UnitSystem::depthLabel() const
{
    return isSI() ? QStringLiteral("m") : QStringLiteral("ft");
}

QString UnitSystem::velocityLabel() const
{
    return isSI() ? QStringLiteral("m/s") : QStringLiteral("ft/s");
}

QString UnitSystem::areaLabel() const
{
    return isSI() ? QStringLiteral("ha") : QStringLiteral("ac");
}

QString UnitSystem::volumeLabel() const
{
    return isSI() ? QStringLiteral("m\u00B3") : QStringLiteral("ft\u00B3");
}
