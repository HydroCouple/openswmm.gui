/**
 * @file      swmmdatetime.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Canonical SWMM DateTime <-> QDateTime conversion for the GUI.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CORE_SWMMDATETIME_H
#define OPENSWMMVIS_CORE_SWMMDATETIME_H

#include "core/swmmdatetimeformat.h"

#include <openswmm/engine/openswmm_datetime.h>

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <cmath>
#include <limits>

namespace openswmmvis::core
{

/*! \brief SWMM DateTime double → UTC-labelled QDateTime (wall clock).
 *
 *  Delegates decoding to the engine, which rounds to the nearest second and
 *  clamps end-of-day. Returns an invalid QDateTime when \p value is not finite
 *  or decodes to an invalid calendar date.
 *
 * @param[in] value Absolute SWMM date/time in days since 1899-12-30, with time
 *                  represented by the fractional day; not elapsed simulation seconds.
 * @return UTC-labelled wall-clock date/time at whole-second resolution, or an invalid
 *         QDateTime for a non-finite input or an invalid decoded calendar date.
 * @note The UTC label suppresses local timezone/DST arithmetic. It does not convert
 *       a local instant to UTC. Rounding at midnight is clamped within the decoded day.
 */
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

/**
 * @brief Decode checkpoint/report dates while retaining millisecond resolution.
 * @param[in] value Absolute SWMM date/time in fractional days since 1899-12-30.
 * @return UTC-labelled wall-clock QDateTime, or an invalid value for non-finite,
 *         unsupported, or invalid calendar input.
 * @details The day is obtained with floor(), and its fractional part is rounded to
 *          the nearest millisecond. A rounded 24-hour remainder can advance to the
 *          next date, unlike swmmDateTimeToQDateTime()'s whole-second clamp.
 * @note No timezone conversion occurs. This helper is intended for checkpoint and
 *       report metadata, not the engine's whole-second authoring round trip.
 */
inline QDateTime swmmDateTimeToMilliseconds(double value)
{
    if (!std::isfinite(value) || value < QDate(1899, 12, 30).daysTo(QDate(1, 1, 1)) ||
        value > 2958465.99999999)
        return {};
    const auto days = static_cast<qint64>(std::floor(value));
    const auto millis = static_cast<qint64>(std::llround((value - days) * 86400000.0));
    const auto date = QDate(1899, 12, 30).addDays(days);
    if (!date.isValid())
        return {};
    return QDateTime(date, QTime(0, 0), Qt::UTC).addMSecs(millis);
}

/*! \brief QDateTime → SWMM DateTime double.
 *
 *  Reads the calendar components directly (no timezone conversion) and encodes
 *  via the engine. Sub-second input is rounded to the nearest second to match
 *  the engine's integer-second resolution, so e.g. 00:14:59.750 encodes to the
 *  same double as 00:15:00. Returns NaN for an invalid QDateTime.
 *
 * @param[in] dt Calendar and clock fields to encode, regardless of the QDateTime's
 *               timezone label. The input is not modified or converted to another zone.
 * @return SWMM date/time in fractional days since 1899-12-30, or quiet NaN for an
 *         invalid QDateTime. Subseconds are rounded to the nearest whole second.
 * @pre For valid inputs, the calendar date must lie within the engine encoder's
 *      supported range of years 1 through 9999; no extra range check is performed here.
 * @note An end-of-day rounded time is clamped to 23:59:59, not advanced to tomorrow.
 */
inline double qDateTimeToSwmmDateTime(const QDateTime &dt)
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
    const long secOfDay =
        std::lround((t.hour() * 3600 + t.minute() * 60 + t.second()) + t.msec() / 1000.0);
    const int clamped = static_cast<int>(secOfDay >= 86400 ? 86399 : secOfDay);

    double timePart = 0.0;
    swmm_datetime_encode_time(clamped / 3600, (clamped % 3600) / 60, clamped % 60, &timePart);
    return datePart + timePart;
}

} // namespace openswmmvis::core

#endif // OPENSWMMVIS_CORE_SWMMDATETIME_H
