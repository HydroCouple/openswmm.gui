/*!
 * \file   swmmpatternpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmpatternpropertyadapter.h"

QString SWMMPatternPropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name")) return tr("Name");
    return {};
}
