/*!
 * \file   swmmsnowpackpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmsnowpackpropertyadapter.h"

QString SWMMSnowpackPropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name")) return tr("Name");
    return {};
}
