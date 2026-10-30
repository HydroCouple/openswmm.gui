/**
 * @file      nodecompoundeditref.cpp
 * @brief     Compound-attribute cell value for the node Property Browser.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/nodecompoundeditref.h"

void registerNodeCompoundEditRefConverter()
{
    static bool s_registered = false;
    if (s_registered)
        return;
    QMetaType::registerConverter<NodeCompoundEditRef, QString>(
        [](const NodeCompoundEditRef &r) { return r.summary; });
    s_registered = true;
}
