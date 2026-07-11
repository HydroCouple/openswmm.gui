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

#include "core/swmmdatetime.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>

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
//
// Delegate the actual conversion arithmetic to the canonical
// openswmmvis::core converter (Phase 3 of the datetime consolidation) so
// this file no longer hand-rolls the epoch/rounding math. The canonical
// converter always returns a Qt::UTC-labelled QDateTime; every OTHER
// QDateTime in this dialog (m_startEdit/m_endEdit via parseEngineDateTime(),
// the [EVENTS] table via this function) is built with the DEFAULT
// (Qt::LocalTime) spec, and validateEvents()/writeEventsToEngine() compare
// them directly. Re-labelling here to match keeps every comparison in this
// file on one consistent basis — using the canonical converter's UTC label
// as-is would silently shift those comparisons by the local UTC offset on
// any machine not in the UTC zone. Only the LABEL changes here, not the
// calendar values, so this is not the "no timezone conversion" contract
// core/swmmdatetime.h documents — it's this file's pre-existing convention
// preserved on top of the fixed arithmetic.
// ---------------------------------------------------------------------------

double SimulationOptionsDialog::oaDateFromQDateTime(const QDateTime &dt)
{
    if (!dt.isValid()) return 0.0;
    return openswmmvis::core::qDateTimeToSwmmDateTime(dt);
}

QDateTime SimulationOptionsDialog::qDateTimeFromOaDate(double oa)
{
    const QDateTime utc = openswmmvis::core::swmmDateTimeToQDateTime(oa);
    if (!utc.isValid()) return {};
    return QDateTime(utc.date(), utc.time());   // re-label: see file-header note
}
