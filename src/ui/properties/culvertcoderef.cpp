/**
 * @file      culvertcoderef.cpp
 * @brief     Phase 0 of docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md — value type for the
 *            conduit "Culvert Code" Property Browser row.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/properties/culvertcoderef.h"

#include "ui/properties/culvertcodes.h"

void registerCulvertCodeRefConverter()
{
    static bool s_registered = false;
    if (s_registered)
        return;
    QMetaType::registerConverter<CulvertCodeRef, QString>(
        [](const CulvertCodeRef &r) { return culvertCodeLabel(r.code); });
    s_registered = true;
}
