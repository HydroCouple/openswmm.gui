/*!
 * \file   plotattribute.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/plotattribute.h"

namespace openswmmvis::plot {

QString labelFor(PlotAttribute a)
{
    switch (a) {
    case PlotAttribute::NodeDepth:         return QStringLiteral("Depth (node)");
    case PlotAttribute::NodeHead:          return QStringLiteral("Head");
    case PlotAttribute::NodeVolume:        return QStringLiteral("Volume (node)");
    case PlotAttribute::NodeLateralInflow: return QStringLiteral("Lateral inflow");
    case PlotAttribute::NodeTotalInflow:   return QStringLiteral("Total inflow");
    case PlotAttribute::NodeOverflow:      return QStringLiteral("Overflow");
    case PlotAttribute::LinkFlow:          return QStringLiteral("Flow");
    case PlotAttribute::LinkDepth:         return QStringLiteral("Depth (link)");
    case PlotAttribute::LinkVelocity:      return QStringLiteral("Velocity (link)");
    case PlotAttribute::LinkVolume:        return QStringLiteral("Volume (link)");
    case PlotAttribute::LinkCapacity:      return QStringLiteral("Capacity");
    case PlotAttribute::SubcatchRainfall:  return QStringLiteral("Rainfall");
    case PlotAttribute::SubcatchSnowDepth: return QStringLiteral("Snow depth");
    case PlotAttribute::SubcatchEvap:      return QStringLiteral("Evaporation");
    case PlotAttribute::SubcatchInfil:     return QStringLiteral("Infiltration");
    case PlotAttribute::SubcatchRunoff:    return QStringLiteral("Runoff");
    case PlotAttribute::Mesh2DDepth:       return QStringLiteral("Depth (2D cell)");
    case PlotAttribute::Mesh2DHGL:         return QStringLiteral("HGL (2D cell)");
    case PlotAttribute::Mesh2DVelocityMag: return QStringLiteral("|V| (2D cell)");
    case PlotAttribute::Mesh2DVelocityX:   return QStringLiteral("Vx (2D cell)");
    case PlotAttribute::Mesh2DVelocityY:   return QStringLiteral("Vy (2D cell)");
    case PlotAttribute::SystemTemperature: return QStringLiteral("Air temperature");
    case PlotAttribute::SystemRainfall:    return QStringLiteral("Average rainfall");
    case PlotAttribute::SystemSnowDepth:   return QStringLiteral("Average snow depth");
    case PlotAttribute::SystemEvap:        return QStringLiteral("Average evaporation");
    case PlotAttribute::SystemInfil:       return QStringLiteral("Average infiltration");
    case PlotAttribute::SystemRunoff:      return QStringLiteral("Total runoff");
    case PlotAttribute::SystemDwInflow:    return QStringLiteral("Total DW inflow");
    case PlotAttribute::SystemGwInflow:    return QStringLiteral("Total GW inflow");
    case PlotAttribute::SystemLatInflow:   return QStringLiteral("Total lateral inflow");
    case PlotAttribute::SystemFlooding:    return QStringLiteral("Total flooding");
    case PlotAttribute::SystemOutflow:     return QStringLiteral("Total outflow");
    case PlotAttribute::SystemStorage:     return QStringLiteral("Total storage");
    case PlotAttribute::SystemEvapTotal:   return QStringLiteral("Total evaporation");
    case PlotAttribute::SystemPET:         return QStringLiteral("Potential evapotranspiration");
    case PlotAttribute::Unknown:
    default:                                return QStringLiteral("Unknown");
    }
}

QString unitsFor(PlotAttribute a, UnitSystem u)
{
    const bool us = (u == UnitSystem::US);
    switch (a) {
    case PlotAttribute::NodeDepth:
    case PlotAttribute::LinkDepth:
    case PlotAttribute::NodeHead:
    case PlotAttribute::Mesh2DDepth:
    case PlotAttribute::Mesh2DHGL:
        return us ? QStringLiteral("ft") : QStringLiteral("m");

    case PlotAttribute::NodeVolume:
    case PlotAttribute::LinkVolume:
        return us ? QStringLiteral("ft³") : QStringLiteral("m³");

    case PlotAttribute::NodeLateralInflow:
    case PlotAttribute::NodeTotalInflow:
    case PlotAttribute::NodeOverflow:
    case PlotAttribute::LinkFlow:
    case PlotAttribute::SubcatchRunoff:
    case PlotAttribute::SystemRunoff:
    case PlotAttribute::SystemDwInflow:
    case PlotAttribute::SystemGwInflow:
    case PlotAttribute::SystemLatInflow:
    case PlotAttribute::SystemFlooding:
    case PlotAttribute::SystemOutflow:
        return us ? QStringLiteral("ft³/s") : QStringLiteral("m³/s");

    case PlotAttribute::SystemStorage:
        return us ? QStringLiteral("ft³") : QStringLiteral("m³");

    case PlotAttribute::LinkVelocity:
    case PlotAttribute::Mesh2DVelocityMag:
    case PlotAttribute::Mesh2DVelocityX:
    case PlotAttribute::Mesh2DVelocityY:
        return us ? QStringLiteral("ft/s") : QStringLiteral("m/s");

    case PlotAttribute::LinkCapacity:
        return QStringLiteral("");      // dimensionless 0..1

    case PlotAttribute::SubcatchRainfall:
    case PlotAttribute::SubcatchInfil:
    case PlotAttribute::SystemRainfall:
    case PlotAttribute::SystemInfil:
        return us ? QStringLiteral("in/hr") : QStringLiteral("mm/hr");

    case PlotAttribute::SubcatchSnowDepth:
    case PlotAttribute::SystemSnowDepth:
        return us ? QStringLiteral("in") : QStringLiteral("mm");

    case PlotAttribute::SubcatchEvap:
    case PlotAttribute::SystemEvap:
    case PlotAttribute::SystemEvapTotal:
    case PlotAttribute::SystemPET:
        return us ? QStringLiteral("in/d") : QStringLiteral("mm/d");

    case PlotAttribute::SystemTemperature:
        return us ? QStringLiteral("°F") : QStringLiteral("°C");

    case PlotAttribute::Unknown:
    default:
        return QStringLiteral("");
    }
}

QString labelWithUnits(PlotAttribute a, UnitSystem u)
{
    const QString units = unitsFor(a, u);
    if (units.isEmpty())
        return labelFor(a);
    return labelFor(a) + QStringLiteral(" (") + units + QStringLiteral(")");
}

UnitSystem unitSystemFromFlowUnits(int flowUnits)
{
    // 0=CFS, 1=GPM, 2=MGD → US; 3=CMS, 4=LPS, 5=MLD → SI.
    return (flowUnits >= 3) ? UnitSystem::SI : UnitSystem::US;
}

} // namespace openswmmvis::plot
