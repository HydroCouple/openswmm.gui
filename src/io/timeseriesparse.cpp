/*!
 * \file   timeseriesparse.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "io/timeseriesparse.h"

#include "core/swmmdatetime.h"

#include <QDateTime>
#include <QRegularExpression>
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
        // Multi-column series files (spec §3.1): 12-hour US datetimes with a
        // trailing AM/PM token (the PCSWMM .tsf form) plus their 24-hour
        // single-digit month/day siblings — the engine's sscanf-based parser
        // accepts all of these, so the GUI must too. Single-letter fields
        // match 1-or-2-digit components; both AP and ap are listed because
        // older Qt matched the meridiem case-sensitively.
        QStringLiteral("M/d/yyyy h:mm:ss AP"),
        QStringLiteral("M/d/yyyy h:mm AP"),
        QStringLiteral("M/d/yyyy h:mm:ss ap"),
        QStringLiteral("M/d/yyyy h:mm ap"),
        QStringLiteral("M/d/yyyy h:mm:ss"),
        QStringLiteral("M/d/yyyy h:mm"),
    };

    const QString trimmed = s.trimmed();
    for (const QString& fmt : kFormats) {
        QDateTime dt = QDateTime::fromString(trimmed, fmt);
        if (!dt.isValid()) continue;
        // qDateTimeToSwmmDateTime() reads calendar components directly and
        // never converts via toUTC()/toLocalTime(), so fromString()'s default
        // Qt::LocalTime label (there's no timezone in these formats) doesn't
        // need correcting here (GH #2 review — see swmmdatetime.h).
        jOut = openswmmvis::core::qDateTimeToSwmmDateTime(dt);
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

namespace {

// Engine's TBLSEPSTR = " \t\n\r," — split on whitespace or comma and drop
// empties. Pre-compiled regex matches the engine's strtok() behaviour.
QStringList tokenizeDatLine_(const QString& line)
{
    static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
    return line.split(sep, Qt::SkipEmptyParts);
}

// Engine-faithful clock parser: matches datetime_strToTime in legacy/engine/
// datetime.c — sscanf("%d:%d:%d") with min=0, sec=0 defaults. Avoids the
// per-call overhead of QTime::fromString (which retries multiple format
// strings and runs full calendrical validation); on a 1.97M-row file the
// fromString path alone burned tens of seconds.
bool tryParseClockToDayFraction_(const QString& s, double& fracOut)
{
    const auto parts = QStringView{s}.split(QLatin1Char(':'),
                                            Qt::KeepEmptyParts,
                                            Qt::CaseSensitive);
    if (parts.isEmpty() || parts.size() > 3) return false;

    bool ok = false;
    const int hr = parts.at(0).toInt(&ok);
    if (!ok) return false;
    int min = 0, sec = 0;
    if (parts.size() >= 2) {
        min = parts.at(1).toInt(&ok);
        if (!ok) return false;
    }
    if (parts.size() == 3) {
        sec = parts.at(2).toInt(&ok);
        if (!ok) return false;
    }
    if (hr < 0 || min < 0 || sec < 0) return false;
    fracOut = (hr * 3600 + min * 60 + sec) / 86400.0;
    return true;
}

// Engine-faithful date parser: matches datetime_strToDate — requires '-' or
// '/' separator, parses three integers, picks ordering by position heuristic
// (4-digit-first → Y/M/D, first int > 12 → D/M/Y, else SWMM default M/D/Y).
// Skips QDate::fromString's format-string loop entirely.
bool tryParseDateOnly_(const QString& s, double& jOut)
{
    QChar delim = QLatin1Char('-');
    if (!s.contains(delim)) {
        delim = QLatin1Char('/');
        if (!s.contains(delim)) return false;
    }
    const auto parts = QStringView{s}.split(delim, Qt::KeepEmptyParts,
                                             Qt::CaseSensitive);
    if (parts.size() != 3) return false;

    bool ok1 = false, ok2 = false, ok3 = false;
    const int a = parts.at(0).toInt(&ok1);
    const int b = parts.at(1).toInt(&ok2);
    const int c = parts.at(2).toInt(&ok3);
    if (!ok1 || !ok2 || !ok3) return false;

    int yr = 0, mon = 0, day = 0;
    if (parts.at(0).size() == 4 || a > 31) {
        yr = a; mon = b; day = c;            // Y/M/D
    } else if (a > 12) {
        day = a; mon = b; yr = c;            // D/M/Y
    } else {
        mon = a; day = b; yr = c;            // M/D/Y (SWMM default)
    }

    const QDate d(yr, mon, day);
    if (!d.isValid()) return false;
    const QDateTime dt(d, QTime(0, 0), Qt::UTC);
    jOut = openswmmvis::core::qDateTimeToSwmmDateTime(dt);
    return std::isfinite(jOut);
}

} // namespace

bool parseSwmmDatLine(const QString& line,
                      SwmmDatState&  state,
                      double&        timeJulianOut,
                      double&        valueOut)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';'))) return false;

    const QStringList tok = tokenizeDatLine_(trimmed);
    if (tok.size() < 2 || tok.size() > 3) return false;

    // The value is always the LAST token regardless of format.
    bool valOk = false;
    const double v = tok.last().toDouble(&valOk);
    if (!valOk) return false;

    if (tok.size() == 2) {
        // <time> <value>. Time = HH:MM[:SS] or numeric hours.
        double frac = 0.0;
        bool numOk = false;
        const double hours = tok.at(0).toDouble(&numOk);
        if (numOk) {
            frac = hours / 24.0;
        } else if (!tryParseClockToDayFraction_(tok.at(0), frac)) {
            return false;
        }
        timeJulianOut = state.lastDateJulian + frac;
        valueOut      = v;
        return true;
    }

    // tok.size() == 3 → <date> <time> <value>.
    double dayJ = 0.0;
    if (!tryParseDateOnly_(tok.at(0), dayJ)) return false;

    double frac = 0.0;
    bool numOk = false;
    const double hours = tok.at(1).toDouble(&numOk);
    if (numOk) {
        frac = hours / 24.0;
    } else if (!tryParseClockToDayFraction_(tok.at(1), frac)) {
        return false;
    }

    state.lastDateJulian = dayJ;
    state.anyDateSeen    = true;
    timeJulianOut        = dayJ + frac;
    valueOut             = v;
    return true;
}

} // namespace openswmmvis::io
