/**
 * @file      resulttable.cpp
 * @brief     V6 — tables of result series with units, status and CSV export.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "results/resulttable.h"

#include "core/swmmdatetime.h"

#include <QObject>
#include <QSaveFile>
#include <QStandardItemModel>
#include <QTextStream>

#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace openswmmvis::results
{

namespace
{
const double kNaN = std::numeric_limits<double>::quiet_NaN();

/// Millisecond key: series of one run share exact times, but a whole-ms key
/// also merges the same instant computed through different paths.
qint64 timeKey(double julian)
{
    return qint64(std::llround(julian * 86400000.0));
}

/// Union of every series' sample times (ms key -> SWMM DateTime).
std::map<qint64, double> unionTimes(const QVector<plot::SeriesData> &series)
{
    std::map<qint64, double> times;
    for (const plot::SeriesData &s : series)
        for (double t : s.timesJulian)
            if (std::isfinite(t))
                times.emplace(timeKey(t), t);
    return times;
}

/// Samples of one series keyed by time.
std::map<qint64, double> samplesOf(const plot::SeriesData &s)
{
    std::map<qint64, double> out;
    const std::size_t n = std::min(s.timesJulian.size(), s.values.size());
    for (std::size_t i = 0; i < n; ++i)
        if (std::isfinite(s.timesJulian[i]))
            out[timeKey(s.timesJulian[i])] = s.values[i];
    return out;
}

QString csvCell(QString text, QChar delimiter)
{
    if (text.contains(delimiter) || text.contains('"') || text.contains('\r') ||
        text.contains('\n')) {
        text.replace('"', QStringLiteral("\"\""));
        text = '"' + text + '"';
    }
    return text;
}

QString sharedUnits(const ResultTable &table)
{
    if (table.columnUnits.isEmpty())
        return {};
    const QString first = table.columnUnits.first();
    for (const QString &u : table.columnUnits)
        if (u != first)
            return {};
    return first;
}
} // namespace

QString timeLabel(double swmmDateTime)
{
    if (!std::isfinite(swmmDateTime))
        return {};
    return core::swmmDateTimeToQDateTime(swmmDateTime)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

ResultTable tableByObject(const QStringList &objectLabels,
                          const QVector<plot::SeriesData> &series, const QString &units,
                          const QString &note)
{
    ResultTable table;
    table.rowHeader = QObject::tr("Object");
    const auto times = unionTimes(series);
    for (const auto &[key, t] : times) {
        Q_UNUSED(key);
        table.columnHeaders.append(timeLabel(t));
        table.columnUnits.append(units);
        table.columnNotes.append(note);
    }
    for (int r = 0; r < objectLabels.size(); ++r) {
        table.rowLabels.append(objectLabels[r]);
        QVector<double> row(int(times.size()), kNaN);
        QString status;
        if (r < series.size() && series[r].ok) {
            const auto samples = samplesOf(series[r]);
            int c = 0, missing = 0;
            for (const auto &[key, t] : times) {
                Q_UNUSED(t);
                const auto it = samples.find(key);
                if (it != samples.end() && std::isfinite(it->second))
                    row[c] = it->second;
                else
                    ++missing;
                ++c;
            }
            if (missing > 0)
                status = QObject::tr("%1 of %2 values missing").arg(missing).arg(times.size());
            // e.g. a region aggregated over fewer cells at some frames.
            if (!series[r].displayNote.isEmpty())
                status += (status.isEmpty() ? QString() : QStringLiteral("; ")) +
                          series[r].displayNote;
        } else {
            status = r < series.size() && !series[r].errorMessage.isEmpty()
                         ? series[r].errorMessage
                         : QObject::tr("No values");
        }
        table.values.append(row);
        table.rowStatus.append(status);
    }
    return table;
}

ResultTable tableByVariable(const QStringList &headers, const QStringList &units,
                            const QVector<plot::SeriesData> &series, const QStringList &notes)
{
    ResultTable table;
    table.rowHeader = QObject::tr("Time");
    table.columnHeaders = headers;
    table.columnUnits = units;
    table.columnNotes = notes;
    while (table.columnUnits.size() < table.columnHeaders.size())
        table.columnUnits.append(QString());
    while (table.columnNotes.size() < table.columnHeaders.size())
        table.columnNotes.append(QString());
    QVector<std::map<qint64, double>> samples;
    for (const plot::SeriesData &s : series)
        samples.append(s.ok ? samplesOf(s) : std::map<qint64, double>{});
    for (const auto &[key, t] : unionTimes(series)) {
        table.rowLabels.append(timeLabel(t));
        table.rowTimes.append(t);
        QVector<double> row(headers.size(), kNaN);
        QStringList missing;
        for (int c = 0; c < headers.size(); ++c) {
            if (c < samples.size()) {
                const auto it = samples[c].find(key);
                if (it != samples[c].end() && std::isfinite(it->second)) {
                    row[c] = it->second;
                    continue;
                }
            }
            missing.append(headers[c]);
        }
        table.values.append(row);
        table.rowStatus.append(
            missing.isEmpty()
                ? QString()
                : QObject::tr("missing: %1").arg(missing.join(QStringLiteral("; "))));
    }
    return table;
}

bool writeDelimited(const ResultTable &table, const QString &path, QChar delimiter,
                    QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    auto line = [&](const QStringList &cells) {
        QStringList quoted;
        for (const QString &c : cells)
            quoted.append(csvCell(c, delimiter));
        out << quoted.join(delimiter) << '\n';
    };
    line(QStringList{table.rowHeader} + table.columnHeaders + QStringList{QObject::tr("Status")});
    line(QStringList{QObject::tr("Units")} + table.columnUnits + QStringList{QString()});
    for (int r = 0; r < table.rowCount(); ++r) {
        QStringList cells{table.rowLabels[r]};
        for (int c = 0; c < table.columnCount(); ++c) {
            const double v = r < table.values.size() && c < table.values[r].size()
                                 ? table.values[r][c]
                                 : kNaN;
            cells.append(std::isfinite(v) ? QString::number(v, 'g', 12) : QString());
        }
        cells.append(r < table.rowStatus.size() ? table.rowStatus[r] : QString());
        line(cells);
    }
    out.flush();
    if (out.status() != QTextStream::Ok) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

void fillItemModel(const ResultTable &table, QStandardItemModel &model)
{
    model.clear();
    const QString shared = sharedUnits(table);
    QStringList headers;
    headers.append(shared.isEmpty()
                       ? table.rowHeader
                       : QObject::tr("%1 (values in %2)").arg(table.rowHeader, shared));
    for (int c = 0; c < table.columnCount(); ++c) {
        const QString u = table.columnUnits.value(c);
        headers.append(shared.isEmpty() && !u.isEmpty()
                           ? QStringLiteral("%1 (%2)").arg(table.columnHeaders[c], u)
                           : table.columnHeaders[c]);
    }
    headers.append(QObject::tr("Status"));
    model.setColumnCount(int(headers.size()));
    model.setHorizontalHeaderLabels(headers);
    // How a value is computed (e.g. a load from report-step samples) as tips.
    QString sharedNote = table.columnNotes.value(0);
    for (int c = 0; c < table.columnCount(); ++c) {
        const QString note = table.columnNotes.value(c);
        if (!note.isEmpty())
            model.setHeaderData(c + 1, Qt::Horizontal, note, Qt::ToolTipRole);
        if (note != sharedNote)
            sharedNote.clear();
    }
    if (!sharedNote.isEmpty())
        model.setHeaderData(0, Qt::Horizontal, sharedNote, Qt::ToolTipRole);
    for (int r = 0; r < table.rowCount(); ++r) {
        QList<QStandardItem *> row;
        auto *label = new QStandardItem(table.rowLabels[r]);
        if (r < table.rowTimes.size())
            label->setData(table.rowTimes[r], Qt::UserRole);
        row.append(label);
        const QString status = table.rowStatus.value(r);
        for (int c = 0; c < table.columnCount(); ++c) {
            const double v = r < table.values.size() && c < table.values[r].size()
                                 ? table.values[r][c]
                                 : kNaN;
            auto *item = new QStandardItem;
            if (std::isfinite(v)) {
                item->setData(v, Qt::EditRole); // the view formats it; sorts numerically
            } else {
                item->setToolTip(QObject::tr("Missing (not zero). %1").arg(status));
            }
            row.append(item);
        }
        row.append(new QStandardItem(status));
        model.appendRow(row);
    }
}

} // namespace openswmmvis::results
