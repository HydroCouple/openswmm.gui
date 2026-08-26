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
#include <QItemSelectionModel>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <functional>

int SimulationOptionsDialog::parseEngineBool(const QString &s)
{
    const QString n = s.trimmed().toUpper();
    if (n == QStringLiteral("YES") || n == QStringLiteral("TRUE")  || n == QStringLiteral("1"))
        return Qt::Checked;
    if (n == QStringLiteral("NO")  || n == QStringLiteral("FALSE") || n == QStringLiteral("0"))
        return Qt::Unchecked;
    return Qt::PartiallyChecked;
}

void SimulationOptionsDialog::fastPresetValues(int &out_threads,
                                               double &out_min_step_sec)
{
    // Conservative fast recipe (see FAST_RUN_RECIPE.md): all 8 P-cores + a 1.0 s
    // step floor so the 2D coupling can't collapse the 1D adaptive step.
    // ~2.6x on the Bellinge 1D/2D benchmark with BETTER mass balance than the
    // as-shipped run (+2.8% vs -3.4% flow-routing continuity at 24h).
    out_threads      = 8;
    out_min_step_sec = 1.0;
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

qint64 SimulationOptionsDialog::parseStepSeconds(const QString &s,
                                                 qint64 fallback)
{
    const QString t = s.trimmed();
    if (t.isEmpty()) return fallback;

    if (!t.contains(QLatin1Char(':'))) {
        // Decimal form, not just integers: swmm_options_get() renders
        // REPORT_STEP / ROUTING_STEP with std::to_string(double), so the
        // engine hands back "900.000000". An integer-only parse rejected
        // that and silently fell back to the preferences default, which then
        // got written straight back to the engine on OK — the reporting step
        // appeared not to persist.
        bool ok = false;
        const double v = t.toDouble(&ok);
        if (!ok || v < 0.0) return fallback;
        return qRound64(v);
    }

    const QStringList parts = t.split(QLatin1Char(':'));
    if (parts.size() > 3) return fallback;
    double secs = 0.0;
    for (const QString &p : parts) {
        bool ok = false;
        const double v = p.trimmed().toDouble(&ok);
        if (!ok || v < 0.0) return fallback;
        secs = secs * 60.0 + v;
    }
    return qRound64(secs);
}

bool SimulationOptionsDialog::optionValueEquals(const QString &a,
                                                const QString &b)
{
    const QString ta = a.trimmed(), tb = b.trimmed();
    if (ta == tb) return true;

    bool okA = false, okB = false;
    const double da = ta.toDouble(&okA);
    const double db = tb.toDouble(&okB);
    if (!okA || !okB) return false;

    // Formatting differences ("0.000000" vs "0.00") are exactly equal after
    // parsing; the relative tolerance only absorbs last-digit rounding.
    const double scale = std::max({1.0, std::abs(da), std::abs(db)});
    return std::abs(da - db) <= 1e-9 * scale;
}

QList<int> SimulationOptionsDialog::selectedRowsDescending(
    const QTableWidget *table)
{
    QList<int> rows;
    if (!table) return rows;

    // The selection model sees row selections even when the cells hold only
    // setCellWidget() editors and no QTableWidgetItems (the [EVENTS] table).
    if (const auto *sel = table->selectionModel()) {
        const auto idxs = sel->selectedRows();
        rows.reserve(idxs.size());
        for (const auto &idx : idxs)
            rows.append(idx.row());
    }

    // Fallback for plain cell selections that don't span a full row.
    if (rows.isEmpty()) {
        const auto items = table->selectedItems();
        for (const auto *it : items)
            if (!rows.contains(it->row()))
                rows.append(it->row());
    }

    std::sort(rows.begin(), rows.end(), std::greater<int>());
    return rows;
}
