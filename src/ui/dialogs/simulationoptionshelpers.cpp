/*!
 * \file   simulationoptionshelpers.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Static helper definitions for SimulationOptionsDialog. Split out of the
 * main .cpp so leaf QtTests (test_simulationoptionsdialog) can compile the
 * helpers without dragging in GDAL/OGR via the layer + CRS code that the
 * dialog's spatial tab needs.
 */
#include "ui/dialogs/simulationoptionsdialog.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>

#include <cmath>

int SimulationOptionsDialog::parseEngineBool(const QString &s)
{
    const QString n = s.trimmed().toUpper();
    if (n == QStringLiteral("YES") || n == QStringLiteral("TRUE")  || n == QStringLiteral("1"))
        return Qt::Checked;
    if (n == QStringLiteral("NO")  || n == QStringLiteral("FALSE") || n == QStringLiteral("0"))
        return Qt::Unchecked;
    return Qt::PartiallyChecked;
}

QString SimulationOptionsDialog::engineBoolString(bool on)
{
    return on ? QStringLiteral("YES") : QStringLiteral("NO");
}

void SimulationOptionsDialog::formatEngineDateTime(const QDateTime &dt,
                                                   QString &out_date,
                                                   QString &out_time)
{
    out_date = dt.date().toString(QStringLiteral("MM/dd/yyyy"));
    out_time = dt.time().toString(QStringLiteral("HH:mm:ss"));
}

QDateTime SimulationOptionsDialog::parseEngineDateTime(const QString &date,
                                                       const QString &time)
{
    QDate d = QDate::fromString(date.trimmed(), QStringLiteral("MM/dd/yyyy"));
    QTime t = QTime::fromString(time.trimmed(), QStringLiteral("HH:mm:ss"));
    if (!d.isValid() || !t.isValid())
        return {};
    return QDateTime(d, t);
}

// ---------------------------------------------------------------------------
// OADate helpers — SWMM uses decimal days since 1899-12-30 00:00 (same as
// Excel / OLE Automation, minus the 1900-leap-year bug — irrelevant for any
// real-world SWMM model dated past 1900-03-01).  See engine
// src/engine/core/DateTime.hpp.  Slice CW (2026-05-21).
// ---------------------------------------------------------------------------

namespace {
constexpr int kOaMSecsPerDay = 86'400'000;
const QDate &oaEpoch() {
    static const QDate kEpoch(1899, 12, 30);
    return kEpoch;
}
} // namespace

double SimulationOptionsDialog::oaDateFromQDateTime(const QDateTime &dt)
{
    if (!dt.isValid()) return 0.0;
    const qint64 days = oaEpoch().daysTo(dt.date());
    const qint64 ms   = dt.time().msecsSinceStartOfDay();
    return static_cast<double>(days) + static_cast<double>(ms) /
                                       static_cast<double>(kOaMSecsPerDay);
}

QDateTime SimulationOptionsDialog::qDateTimeFromOaDate(double oa)
{
    // floor() so negative offsets round toward -infinity and the fractional
    // part stays in [0, 1).  In practice SWMM dates are post-1899 so the
    // floor() vs trunc() distinction is academic.
    const double whole = std::floor(oa);
    const double frac  = oa - whole;
    const qint64 days  = static_cast<qint64>(whole);
    const int    ms    = static_cast<int>(std::llround(
                            frac * static_cast<double>(kOaMSecsPerDay)));
    return QDateTime(oaEpoch().addDays(days),
                     QTime::fromMSecsSinceStartOfDay(ms));
}
