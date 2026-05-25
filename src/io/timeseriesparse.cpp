/*!
 * \file   timeseriesparse.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "io/timeseriesparse.h"

#include "plot/swmmjuliandatetime.h"

#include <QDateTime>
#include <QStringList>

#include <cmath>
#include <limits>

namespace openswmmvis::io {

QChar guessDelimiter(const QString& sampleLine)
{
    const int tabs   = sampleLine.count(QLatin1Char('\t'));
    const int commas = sampleLine.count(QLatin1Char(','));
    const int semis  = sampleLine.count(QLatin1Char(';'));
    if (tabs >= commas && tabs >= semis && tabs > 0) return QLatin1Char('\t');
    if (semis > commas && semis > 0)                  return QLatin1Char(';');
    return QLatin1Char(',');
}

bool tryParseTimestamp(const QString& s, double& jOut, double fallbackBase)
{
    static const QString kFormats[] = {
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm"),
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("MM/dd/yyyy HH:mm:ss"),
        QStringLiteral("MM/dd/yyyy HH:mm"),
        QStringLiteral("MM/dd/yyyy"),
        QStringLiteral("dd/MM/yyyy HH:mm"),
    };

    const QString trimmed = s.trimmed();
    for (const QString& fmt : kFormats) {
        QDateTime dt = QDateTime::fromString(trimmed, fmt);
        if (!dt.isValid()) continue;
        if (dt.timeSpec() == Qt::LocalTime) dt.setTimeSpec(Qt::UTC);
        jOut = openswmmvis::plot::dateTimeToSwmmJulian(dt);
        return std::isfinite(jOut);
    }

    bool ok = false;
    const double hours = trimmed.toDouble(&ok);
    if (ok && std::isfinite(fallbackBase)) {
        jOut = fallbackBase + hours / 24.0;
        return true;
    }
    return false;
}

bool parseRow(const QString& line,
              QChar delim,
              double& timeJulianOut,
              std::vector<double>& valuesOut,
              double hoursSinceStartFallbackBase)
{
    const QStringList cells = line.split(delim);
    if (cells.size() < 2) return false;

    if (!tryParseTimestamp(cells.first(), timeJulianOut,
                            hoursSinceStartFallbackBase)) return false;

    valuesOut.assign(static_cast<std::size_t>(cells.size() - 1),
                     std::numeric_limits<double>::quiet_NaN());
    for (int c = 1; c < cells.size(); ++c) {
        const QString s = cells.at(c).trimmed();
        if (s.isEmpty()) continue;
        bool ok = false;
        const double v = s.toDouble(&ok);
        if (ok) valuesOut[static_cast<std::size_t>(c - 1)] = v;
    }
    return true;
}

} // namespace openswmmvis::io
