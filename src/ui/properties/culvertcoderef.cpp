/*!
 * \file   culvertcoderef.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/properties/culvertcoderef.h"

#include "ui/properties/culvertcodes.h"

void registerCulvertCodeRefConverter()
{
    static bool s_registered = false;
    if (s_registered) return;
    QMetaType::registerConverter<CulvertCodeRef, QString>(
        [](const CulvertCodeRef &r) { return culvertCodeLabel(r.code); });
    s_registered = true;
}
