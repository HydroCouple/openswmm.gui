/*!
 * \file   swmmjuliandatetime.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL — Julian-date → QDateTime conversion for the engine's
 *         simulation epoch.
 *
 * SWMM5 stores absolute times as a double "Julian" date counted from the
 * epoch 1899-12-30 00:00 (the same convention as Excel's "1900 system"
 * minus its leap-day quirks). The integer part is days; the fraction is
 * day-of-day.
 *
 * Implementation matches `swmm5/datetime.c::DateTimeFromYMD` byte-for-byte
 * by offsetting from the canonical epoch via Qt date arithmetic.
 *
 * Header-only — pure helper, no dependencies beyond Qt Core.
 */
#ifndef OPENSWMMVIS_PLOT_SWMMJULIANDATETIME_H
#define OPENSWMMVIS_PLOT_SWMMJULIANDATETIME_H

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <cmath>
#include <cstdint>

namespace openswmmvis::plot {

/*! \brief Convert a SWMM Julian date (days since 1899-12-30) to a UTC QDateTime.
 *  \param julian Days as a double; integer part = whole days, fraction = day-of-day.
 *  \returns A valid QDateTime in UTC. Returns an invalid QDateTime if \p julian
 *           is not finite. */
inline QDateTime swmmJulianToDateTime(double julian)
{
    if (!std::isfinite(julian))
        return {};

    static const QDate kEpoch(1899, 12, 30);
    const double days = std::floor(julian);
    const double frac = julian - days;

    return QDateTime(
        kEpoch.addDays(static_cast<qint64>(days)),
        QTime(0, 0).addMSecs(static_cast<qint64>(frac * 86'400'000.0)),
        Qt::UTC);
}

/*! \brief Inverse of `swmmJulianToDateTime` — primarily used by unit tests. */
inline double dateTimeToSwmmJulian(const QDateTime& dt)
{
    if (!dt.isValid())
        return std::numeric_limits<double>::quiet_NaN();

    static const QDate kEpoch(1899, 12, 30);
    const QDateTime utc = dt.toUTC();
    const qint64 days = kEpoch.daysTo(utc.date());
    const qint64 ms   = QTime(0, 0).msecsTo(utc.time());
    return static_cast<double>(days) +
           static_cast<double>(ms) / 86'400'000.0;
}

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SWMMJULIANDATETIME_H
