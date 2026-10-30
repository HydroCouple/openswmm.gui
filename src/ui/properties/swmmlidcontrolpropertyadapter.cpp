/**
 * @file      swmmlidcontrolpropertyadapter.cpp
 * @brief     Property-tree adapter for [LID_CONTROLS].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/swmmlidcontrolpropertyadapter.h"

QString SWMMLIDControlPropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))
        return tr("Name");
    return {};
}
