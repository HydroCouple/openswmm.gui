/*!
 * \file   swmmjuliandatetime.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Forwarding shim — kept so existing call sites keep building.
 *
 * SWMM's absolute time is an OLE-Automation date (days since 1899-12-30), NOT a
 * Julian date; the original name here is a misnomer. The canonical conversion
 * now lives in `core/swmmdatetime.h` and delegates to the engine's native
 * encode/decode primitives, which fixes the fractional-second truncation
 * (GH #1) and removes the ad-hoc epoch/timezone handling (GH #2).
 *
 * These wrappers simply forward to `openswmmvis::core`. New code should call
 * the canonical helpers directly.
 */
#ifndef OPENSWMMVIS_PLOT_SWMMJULIANDATETIME_H
#define OPENSWMMVIS_PLOT_SWMMJULIANDATETIME_H

#include "core/swmmdatetime.h"

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <cmath>
#include <cstdint>

namespace openswmmvis::plot {

// TODO(datetime-consolidation, GH #2): migrate the remaining call sites to
// openswmmvis::core::swmmDateTimeToQDateTime / qDateTimeToSwmmDateTime, then
// mark these [[deprecated]] and delete this header (consolidation Phase 3).

/*! \brief Forwards to openswmmvis::core::swmmDateTimeToQDateTime. */
inline QDateTime swmmJulianToDateTime(double julian)
{
    return openswmmvis::core::swmmDateTimeToQDateTime(julian);
}

/*! \brief Forwards to openswmmvis::core::qDateTimeToSwmmDateTime. */
inline double dateTimeToSwmmJulian(const QDateTime& dt)
{
    return openswmmvis::core::qDateTimeToSwmmDateTime(dt);
}

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SWMMJULIANDATETIME_H
