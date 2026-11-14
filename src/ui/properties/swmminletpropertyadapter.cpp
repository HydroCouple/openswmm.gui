/**
 * @file      swmminletpropertyadapter.cpp
 * @brief     Property-tree adapter for [INLETS].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/swmminletpropertyadapter.h"

QString SWMMInletPropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))
        return tr("Name");
    return {};
}
