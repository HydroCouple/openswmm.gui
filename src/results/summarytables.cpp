// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      summarytables.cpp
 * @brief     Report summary tables saved beside a run's output (review G-08).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "results/summarytables.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <vector>

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_statistics.h>
#include <openswmm/engine/openswmm_subcatchments.h>

namespace openswmmvis::results
{

namespace
{
constexpr auto kFormat = "openswmm-summary-tables";
constexpr int kVersion = 1;

[[maybe_unused]] QString objectName(SWMM_Engine engine, int type, int index)
{
    const char *id = nullptr;
    if (type == SummaryNode)
        id = swmm_node_id(engine, index);
    else if (type == SummaryLink)
        id = swmm_link_id(engine, index);
    else if (type == SummarySubcatch)
        id = swmm_subcatch_id(engine, index);
    else
        return QStringLiteral("System");
    return id ? QString::fromUtf8(id) : QString();
}
} // namespace

QString summaryTablesPath(const QString &outputPath)
{
    return outputPath + QStringLiteral(".summary.json");
}

std::optional<SummaryTables> captureSummaryTables(SWMM_Engine engine, QString *error)
{
#ifndef OPENSWMM_HAS_SUMMARY_TABLES
    Q_UNUSED(engine);
    if (error)
        *error = QStringLiteral("This engine build has no summary-table API.");
    return std::nullopt;
#else
    SummaryTables result;
    for (int t = 0; t < SWMM_SUMMARY_TABLE_COUNT; ++t) {
        SummaryTable table;
        table.id = t;
        char text[512] = {};
        const int rc = swmm_summary_table_title(engine, t, text, int(sizeof text));
        if (rc != SWMM_OK) {
            if (error)
                *error = QStringLiteral("The engine did not give summary table %1 (code %2).")
                             .arg(t)
                             .arg(rc);
            return std::nullopt;
        }
        table.title = QString::fromUtf8(text);
        int columns = 0, rows = 0;
        swmm_summary_column_count(engine, t, &columns);
        swmm_summary_row_count(engine, t, &rows);
        for (int c = 0; c < columns; ++c) {
            char name[256] = {}, units[64] = {};
            swmm_summary_column(engine, t, c, name, int(sizeof name), units, int(sizeof units));
            table.columns.append({QString::fromUtf8(name), QString::fromUtf8(units)});
        }
        if (rows > 0) {
            // Whole-table reads build the table once; per-cell reads rebuild rows each call.
            std::vector<int> types(rows), indices(rows);
            std::vector<double> values(std::size_t(rows) * std::size_t(std::max(columns, 0)));
            int needed = 0;
            swmm_summary_labels_read(engine, t, nullptr, 0, &needed);
            QByteArray labels(std::max(needed, 1), '\0');
            if (swmm_summary_rows_read(engine, t, types.data(), indices.data(), rows) != SWMM_OK ||
                swmm_summary_values_read(engine, t, values.data(), int(values.size())) !=
                    SWMM_OK ||
                swmm_summary_labels_read(engine, t, labels.data(), int(labels.size()), nullptr) !=
                    SWMM_OK) {
                if (error)
                    *error = QStringLiteral("The engine did not give the rows of %1.")
                                 .arg(table.title);
                return std::nullopt;
            }
            const QStringList rowLabels =
                QString::fromUtf8(labels.constData()).split(QLatin1Char('\n'));
            for (int r = 0; r < rows; ++r) {
                SummaryRow row;
                row.objectType = types[std::size_t(r)];
                row.objectIndex = indices[std::size_t(r)];
                row.label = rowLabels.value(r);
                row.objectName = objectName(engine, row.objectType, row.objectIndex);
                for (int c = 0; c < columns; ++c)
                    row.values.append(values[std::size_t(r) * std::size_t(columns) + std::size_t(c)]);
                table.rows.append(row);
            }
        }
        result.tables.append(table);
    }
    return result;
#endif
}

bool saveSummaryTables(const SummaryTables &tables, const QString &path, QString *error)
{
    QJsonArray tableArray;
    for (const auto &table : tables.tables) {
        QJsonArray columns, rows;
        for (const auto &column : table.columns)
            columns.append(QJsonObject{{QStringLiteral("name"), column.name},
                                       {QStringLiteral("units"), column.units}});
        for (const auto &row : table.rows) {
            QJsonArray values;
            for (double v : row.values)
                values.append(std::isfinite(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Null));
            rows.append(QJsonObject{{QStringLiteral("type"), row.objectType},
                                    {QStringLiteral("index"), row.objectIndex},
                                    {QStringLiteral("name"), row.objectName},
                                    {QStringLiteral("label"), row.label},
                                    {QStringLiteral("values"), values}});
        }
        tableArray.append(QJsonObject{{QStringLiteral("id"), table.id},
                                      {QStringLiteral("title"), table.title},
                                      {QStringLiteral("columns"), columns},
                                      {QStringLiteral("rows"), rows}});
    }
    const QJsonObject root{{QStringLiteral("format"), QLatin1String(kFormat)},
                           {QStringLiteral("version"), kVersion},
                           {QStringLiteral("tables"), tableArray}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

std::optional<SummaryTables> loadSummaryTables(const QString &path, QString *error)
{
    const auto fail = [&](const QString &why) -> std::optional<SummaryTables> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("Not a summary-table file: %1").arg(parse.errorString()));
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat) ||
        root.value(QStringLiteral("version")).toInt() != kVersion)
        return fail(QStringLiteral("Not a summary-table file of version %1.").arg(kVersion));
    SummaryTables result;
    for (const QJsonValue &tv : root.value(QStringLiteral("tables")).toArray()) {
        const QJsonObject t = tv.toObject();
        SummaryTable table;
        table.id = t.value(QStringLiteral("id")).toInt(-1);
        table.title = t.value(QStringLiteral("title")).toString();
        for (const QJsonValue &cv : t.value(QStringLiteral("columns")).toArray())
            table.columns.append({cv.toObject().value(QStringLiteral("name")).toString(),
                                  cv.toObject().value(QStringLiteral("units")).toString()});
        for (const QJsonValue &rv : t.value(QStringLiteral("rows")).toArray()) {
            const QJsonObject r = rv.toObject();
            SummaryRow row;
            row.objectType = r.value(QStringLiteral("type")).toInt(-1);
            row.objectIndex = r.value(QStringLiteral("index")).toInt(-1);
            row.objectName = r.value(QStringLiteral("name")).toString();
            row.label = r.value(QStringLiteral("label")).toString();
            for (const QJsonValue &v : r.value(QStringLiteral("values")).toArray())
                row.values.append(v.isDouble() ? v.toDouble() : std::nan(""));
            if (row.values.size() != table.columns.size())
                return fail(QStringLiteral("Table %1 has a row with %2 values for %3 columns.")
                                .arg(table.title)
                                .arg(row.values.size())
                                .arg(table.columns.size()));
            table.rows.append(row);
        }
        result.tables.append(table);
    }
    return result;
}

} // namespace openswmmvis::results
