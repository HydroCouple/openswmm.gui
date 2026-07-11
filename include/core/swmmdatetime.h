/*!
 * \file   swmmdatetime.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Canonical SWMM DateTime <-> QDateTime conversion for the GUI.
 *
 * SWMM stores an absolute time as a single `double`: the integer part is the
 * number of days since 1899-12-30 and the fractional part is the fraction of a
 * 24-hour day. This is the OLE-Automation / Delphi TDateTime convention — it is
 * *not* an astronomical Julian date.
 *
 * These helpers delegate to the engine's own encode/decode primitives
 * (`openswmm/engine/openswmm_datetime.h`) so every conversion is numerically
 * identical to a SWMM run. In particular the decode side *rounds* the
 * fractional day to the nearest second (`floor(frac*86400 + 0.5)`, clamped to
 * end-of-day) rather than truncating — which is what fixes the "00:15 shown as
 * 00:14" defect (GH #1).
 *
 * Time is treated as a timezone-naive wall clock. The QDateTime is labelled
 * `Qt::UTC` purely so that no local-time / daylight-saving arithmetic is ever
 * applied on the round-trip (GH #2). Never feed the results of these helpers
 * through `toLocalTime()` / `toUTC()` for conversion purposes.
 *
 * Header-only. Requires linking the engine target for the `swmm_datetime_*`
 * symbols.
 */
#ifndef OPENSWMMVIS_CORE_SWMMDATETIME_H
#define OPENSWMMVIS_CORE_SWMMDATETIME_H

#include <openswmm/engine/openswmm_datetime.h>

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <cmath>
#include <limits>

namespace openswmmvis::core {

/*! \brief SWMM DateTime double → UTC-labelled QDateTime (wall clock).
 *
 *  Delegates decoding to the engine, which rounds to the nearest second and
 *  clamps end-of-day. Returns an invalid QDateTime when \p value is not finite
 *  or decodes to an invalid calendar date. */
inline QDateTime swmmDateTimeToQDateTime(double value)
{
    if (!std::isfinite(value))
        return {};

    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    swmm_datetime_decode_date(value, &y, &mo, &d);
    swmm_datetime_decode_time(value, &h, &mi, &s);

    const QDate date(y, mo, d);
    if (!date.isValid())
        return {};
    return QDateTime(date, QTime(h, mi, s), Qt::UTC);
}

/*! \brief QDateTime → SWMM DateTime double.
 *
 *  Reads the calendar components directly (no timezone conversion) and encodes
 *  via the engine. Sub-second input is rounded to the nearest second to match
 *  the engine's integer-second resolution, so e.g. 00:14:59.750 encodes to the
 *  same double as 00:15:00. Returns NaN for an invalid QDateTime. */
inline double qDateTimeToSwmmDateTime(const QDateTime& dt)
{
    if (!dt.isValid())
        return std::numeric_limits<double>::quiet_NaN();

    const QDate d = dt.date();
    const QTime t = dt.time();

    double datePart = 0.0;
    swmm_datetime_encode_date(d.year(), d.month(), d.day(), &datePart);

    // Round to the nearest whole second (engine resolution). Clamp a rounded-up
    // end-of-day back into [0, 86399] to match the engine's decodeTime clamp
    // rather than spuriously rolling into the next calendar day.
    const long secOfDay = std::lround(
        (t.hour() * 3600 + t.minute() * 60 + t.second()) + t.msec() / 1000.0);
    const int clamped = static_cast<int>(secOfDay >= 86400 ? 86399 : secOfDay);

    double timePart = 0.0;
    swmm_datetime_encode_time(clamped / 3600, (clamped % 3600) / 60, clamped % 60,
                              &timePart);
    return datePart + timePart;
}

} // namespace openswmmvis::core

#endif // OPENSWMMVIS_CORE_SWMMDATETIME_H
