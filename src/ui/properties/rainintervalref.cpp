/*!
 * \file   rainintervalref.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/properties/rainintervalref.h"

void registerRainIntervalRefConverter()
{
    static bool s_registered = false;
    if (s_registered) return;
    QMetaType::registerConverter<RainIntervalRef, QString>(
        [](const RainIntervalRef &r) { return rain_interval::secondsToHMM(r.seconds); });
    s_registered = true;
}
