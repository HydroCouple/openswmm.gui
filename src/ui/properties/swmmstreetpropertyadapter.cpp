/*!
 * \file   swmmstreetpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmstreetpropertyadapter.h"

QString SWMMStreetPropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))              return tr("Name");
    if (property == QLatin1String("crownWidth"))        return tr("Road Width");
    if (property == QLatin1String("curbHeight"))        return tr("Curb Height");
    if (property == QLatin1String("crossSlope"))        return tr("Cross Slope (%)");
    if (property == QLatin1String("roadRoughness"))     return tr("Road Roughness (n)");
    if (property == QLatin1String("gutterDepression"))  return tr("Gutter Depression");
    if (property == QLatin1String("gutterWidth"))       return tr("Gutter Width");
    if (property == QLatin1String("sides"))             return tr("Sides");
    if (property == QLatin1String("backingWidth"))      return tr("Backing Width");
    if (property == QLatin1String("backingSlope"))      return tr("Backing Slope (%)");
    if (property == QLatin1String("backingRoughness"))  return tr("Backing Roughness (n)");
    return {};
}
