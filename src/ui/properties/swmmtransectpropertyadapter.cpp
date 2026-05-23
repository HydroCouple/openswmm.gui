/*!
 * \file   swmmtransectpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmtransectpropertyadapter.h"

QString SWMMTransectPropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name")) return tr("Name");
    return {};
}
