/*!
 * \file   externalcolumnfile.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/util/externalcolumnfile.h"

#include "ui/util/externalcolumnfilecore.h"
#include "io/timeseriesparse.h"
#include "core/swmmdatetime.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTextStream>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace openswmmvis::ui {

namespace {

/*! Timestamp cell → SWMM DateTime. The engine-mirroring grammar
 *  (extcol::parseSeriesDateTime — ISO / US / AM-PM) is tried first;
 *  io::tryParseTimestamp keeps the GUI's extra legacy formats
 *  (e.g. dd/MM/yyyy European) working as before. No hours-since-start
 *  fallback here — column files are absolute-dated. */
bool parseTimestampCell(const std::string& cell, double& jOut)
{
    extcol::DateTimeParts p;
    if (extcol::parseSeriesDateTime(cell, p)) {
        const QDate d(p.year, p.month, p.day);
        const QTime t(p.hour, p.minute, p.second);
        if (d.isValid() && t.isValid()) {
            jOut = openswmmvis::core::qDateTimeToSwmmDateTime(
                QDateTime(d, t, Qt::UTC));
            return std::isfinite(jOut);
        }
    }
    return openswmmvis::io::tryParseTimestamp(
        QString::fromStdString(cell), jOut,
        std::numeric_limits<double>::quiet_NaN());
}

/*! Value cell → double via strtod (engine parity: accepts what the engine
 *  accepts). Returns NaN for empty / non-numeric cells. */
double parseValueCell(const std::string& cell)
{
    if (cell.empty()) return std::numeric_limits<double>::quiet_NaN();
    char* endp = nullptr;
    const double v = std::strtod(cell.c_str(), &endp);
    if (endp == cell.c_str()) return std::numeric_limits<double>::quiet_NaN();
    return v;
}

/*! Shared single-pass parse. \a pointsOut may be null (header-only probe —
 *  data rows are then not read, keeping readHeaders() cheap on huge files).
 *  Returns false when the file cannot be opened, no header/content line
 *  exists, or a non-empty \a columnSelector cannot be resolved (no such
 *  header, or the file is headerless and therefore has no names at all). */
bool parseFile(const QString& path, const QString& columnSelector,
               QVector<ExternalSeriesPoint>* pointsOut,
               QStringList* headersOut, QString* errorOut,
               bool* fabricatedOut)
{
    if (headersOut) headersOut->clear();
    if (pointsOut) pointsOut->clear();
    if (fabricatedOut) *fabricatedOut = false;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    // ── Header row (first non-blank, non-comment line) ──────────────────────
    std::string headerLine;
    bool haveHeader = false;
    bool firstLine  = true;
    while (!in.atEnd()) {
        headerLine = in.readLine().toStdString();
        extcol::normalizeLine(headerLine, firstLine);
        firstLine = false;
        if (extcol::isBlankOrComment(headerLine)) continue;
        haveHeader = true;
        break;
    }
    if (!haveHeader) {
        if (errorOut) *errorOut = QStringLiteral("no header row");
        return false;
    }

    char delim = ',';
    std::vector<std::string> headers;      // value columns only (time excluded)
    bool fabricated = false;               // headerless → display-only names

    if (extcol::isTsfHeader(headerLine)) {
        // PCSWMM TSF: tab-delimited; IDs-row tokens 1..N name the columns;
        // the parameter row (line 2) and units row (line 3) carry no data.
        delim = '\t';
        const auto cells = extcol::splitCells(headerLine, delim);
        headers.assign(cells.begin() + 1, cells.end());
        for (int i = 0; i < 2 && !in.atEnd(); ++i) in.readLine();
    } else {
        delim = extcol::sniffDelimiter(headerLine);
        const auto cells = extcol::splitCells(headerLine, delim);
        // Header-vs-data probe: when the first content line parses as a data
        // row, fabricate display-only "col_N" names for the column pickers.
        // The line itself is still consumed as the header row and is NOT kept
        // as a sample — the engine has no such probe (it always spends line 1
        // on headers, MultiColumnSeriesFile.cpp header loop), and the GUI used
        // to show one extra row that the run would never see (review B-4).
        double probeT = 0.0;
        if (cells.size() >= 2 && parseTimestampCell(cells[0], probeT)) {
            fabricated = true;
            if (fabricatedOut) *fabricatedOut = true;
            for (std::size_t c = 1; c < cells.size(); ++c)
                headers.push_back(extcol::fabricatedColumnName(c));
        } else {
            headers.assign(cells.begin() + 1, cells.end());
        }
    }

    if (headersOut) {
        for (const std::string& h : headers)
            *headersOut << QString::fromStdString(h);
    }
    if (!pointsOut) return true;   // header-only probe

    // ── Column resolution ("" → first data column; a miss is an error) ──────
    // A non-empty selector that matches nothing is a hard failure in the
    // engine (ERR_TABLE_FILE_READ / ERR_RAIN_FILE_FORMAT), so it must not
    // quietly become "column 1" here — that previewed one column while the
    // run refused to open the file (review B-3).
    int activeCol = 0;
    if (!columnSelector.isEmpty()) {
        // A headerless file has no names for the engine to match, so NO name
        // resolves against it — not even a fabricated one. Its first data
        // column is addressable only through an empty selector (review B-4).
        activeCol = fabricated
                        ? -1
                        : extcol::findColumn(headers, columnSelector.toStdString());
        if (activeCol < 0) {
            if (errorOut)
                *errorOut = fabricated
                    ? QObject::tr("%1 has no header row, so no column can be "
                                  "selected by name — only its first column "
                                  "can be read.")
                          .arg(QFileInfo(path).fileName())
                    : QObject::tr("column \"%1\" not found in %2")
                          .arg(columnSelector, QFileInfo(path).fileName());
            return false;
        }
    }
    const std::size_t cellIdx = static_cast<std::size_t>(activeCol) + 1;

    auto appendRow = [&](const std::vector<std::string>& cells, double tJ) {
        if (cellIdx >= cells.size()) return;                 // short row
        const double v = parseValueCell(cells[cellIdx]);
        if (std::isnan(v)) return;   // missing cell — engine skips these too
        pointsOut->push_back({tJ, v});
    };

    // ── Data rows ───────────────────────────────────────────────────────────
    while (!in.atEnd()) {
        std::string line = in.readLine().toStdString();
        extcol::normalizeLine(line, false);
        if (extcol::isBlankOrComment(line)) continue;
        const auto cells = extcol::splitCells(line, delim);
        double tJ = 0.0;
        if (cells.empty() || !parseTimestampCell(cells[0], tJ)) continue;
        appendRow(cells, tJ);
    }

    // ── Sort ascending (records may be out of order; engine parity) ────────
    auto& pts = *pointsOut;
    if (!std::is_sorted(pts.begin(), pts.end(),
                        [](const ExternalSeriesPoint& a,
                           const ExternalSeriesPoint& b)
                        { return a.timeJulian < b.timeJulian; })) {
        std::stable_sort(pts.begin(), pts.end(),
                         [](const ExternalSeriesPoint& a,
                            const ExternalSeriesPoint& b)
                         { return a.timeJulian < b.timeJulian; });
    }
    return true;
}

} // namespace

QStringList readHeaders(const QString& path, QString* errorOut,
                        bool* fabricatedOut)
{
    QStringList headers;
    parseFile(path, QString(), /*pointsOut=*/nullptr, &headers, errorOut,
              fabricatedOut);
    return headers;
}

int readColumn(const QString& path, const QString& columnSelector,
               QVector<ExternalSeriesPoint>& pointsOut,
               QStringList* headersOut, QString* errorOut,
               bool* fabricatedOut)
{
    if (!parseFile(path, columnSelector, &pointsOut, headersOut, errorOut,
                   fabricatedOut))
        return -1;
    return pointsOut.size();
}

QString reconcileColumnSelector(const QString& path, const QString& current)
{
    if (path.isEmpty()) return {};

    bool fabricated = false;
    const QStringList headers = readHeaders(path, /*errorOut=*/nullptr,
                                            &fabricated);
    // A fabricated name is display-only: the engine spends line 1 as its header
    // row and would never match "col_2", so the only addressable column of a
    // headerless file is its first, via an empty selector.
    if (fabricated || headers.isEmpty()) return {};

    // Case-insensitive like readColumn's own matching, but the file's spelling
    // is what gets stored — the token then reads the way the file does.
    for (const QString& h : headers)
        if (h.compare(current, Qt::CaseInsensitive) == 0) return h;

    return headers.first();
}

} // namespace openswmmvis::ui
