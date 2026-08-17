/*!
 * \file   externalcolumnfilecore.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Multi-column series file (CSV/TSV/TSF) — pure-C++ parsing core.
 *
 * Header-only, deliberately Qt-free so the format rules can be compiled and
 * exercised standalone (tests/standalone_externalcolumn/) and shared by
 * non-widget code (TimeseriesRegistry's `path:col` token handling). The Qt
 * wrapper lives in ui/util/externalcolumnfile.{h,cpp}.
 *
 * Every rule here mirrors the engine so the GUI preview and the run agree:
 *   - TSF detection / cell handling: openswmm.engine
 *     src/engine/input/MultiColumnSeriesFile.cpp (istarts_with "IDs:",
 *     quote-aware tab/comma split, BOM + '\r' strip, ';'/'#' comments).
 *   - Datetime grammar: MultiColumnSeriesFile.cpp parse_series_datetime
 *     (ISO-8601, US M/D/Y, optional trailing AM/PM as used by PCSWMM .tsf).
 *   - `path:col` split: the unified last-colon rule now shared by both engine
 *     readers (CatchmentHandler.cpp readGage FILE token and
 *     PostParseResolver.cpp's timeseries token) — see splitPathColumn().
 * Delimiter sniffing keeps the GUI's semicolon extension on top of the
 * engine's tab-vs-comma rule, and skips quoted regions like the cell
 * splitter (see sniffDelimiter()).
 */
#ifndef OPENSWMMVIS_UI_UTIL_EXTERNALCOLUMNFILECORE_H
#define OPENSWMMVIS_UI_UTIL_EXTERNALCOLUMNFILECORE_H

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace openswmmvis::ui::extcol {

/*! \brief Trim ASCII whitespace and one surrounding pair of double quotes.
 *  Mirrors the engine's cell_trim. */
inline std::string cellTrim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    auto space = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (a < b && space(s[a])) ++a;
    while (b > a && space(s[b - 1])) --b;
    if (b - a >= 2 && s[a] == '"' && s[b - 1] == '"') { ++a; --b; }
    return s.substr(a, b - a);
}

/*! \brief Quote-aware split on a single-character delimiter (engine cell_split). */
inline std::vector<std::string> splitCells(const std::string& line, char delim)
{
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (const char c : line) {
        if (c == '"') { inQuotes = !inQuotes; cur.push_back(c); }
        else if (c == delim && !inQuotes) { out.push_back(cellTrim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cellTrim(cur));
    return out;
}

/*! \brief Case-insensitive equality (engine csv_iequals). */
inline bool iequals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i]))
            != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

/*! \brief Strip a UTF-8 BOM (first line only) and a trailing '\r'. */
inline void normalizeLine(std::string& line, bool firstLine)
{
    if (firstLine && line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
        line.erase(0, 3);
    if (!line.empty() && line.back() == '\r') line.pop_back();
}

/*! \brief True for empty/all-whitespace lines and ';'/'#' comment lines. */
inline bool isBlankOrComment(const std::string& line)
{
    for (const char c : line) {
        if (c == ' ' || c == '\t') continue;
        return c == ';' || c == '#';
    }
    return true;  // all-whitespace / empty
}

/*! \brief True when the (normalized) line starts a PCSWMM .tsf header —
 *  case-insensitive "IDs:" prefix, per the engine's format sniff. */
inline bool isTsfHeader(const std::string& line)
{
    static const std::string prefix = "IDs:";
    if (line.size() < prefix.size()) return false;
    return iequals(line.substr(0, prefix.size()), prefix);
}

/*! \brief Sniff the cell delimiter from the header row. The engine rule is
 *  "tab when tabs > 0 and tabs >= commas, else comma"; the GUI additionally
 *  recognizes semicolon-delimited files (io::guessDelimiter), so that
 *  extension is preserved here — the two agree on every engine-loadable file.
 *
 *  Delimiters inside a quoted cell are NOT counted (review B-8): a
 *  tab-delimited header like `Date<TAB>"a,b,c"` used to sniff as comma and
 *  then mis-split every row. Quote handling now matches splitCells(). */
inline char sniffDelimiter(const std::string& headerLine)
{
    long tabs = 0, commas = 0, semis = 0;
    bool inQuotes = false;
    for (const char c : headerLine) {
        if (c == '"') { inQuotes = !inQuotes; continue; }
        if (inQuotes)  continue;
        if (c == '\t')      ++tabs;
        else if (c == ',')  ++commas;
        else if (c == ';')  ++semis;
    }
    if (tabs >= commas && tabs >= semis && tabs > 0) return '\t';
    if (semis > commas && semis > 0)                  return ';';
    return ',';
}

/*! \brief Calendar components produced by parseSeriesDateTime(). */
struct DateTimeParts {
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0;
};

/*! \brief 0 = none, 1 = AM, 2 = PM — from the cell's trailing token
 *  (engine trailing_meridiem). */
inline int trailingMeridiem(const std::string& cell)
{
    const std::size_t e = cell.find_last_not_of(" \t\r\n");
    if (e == std::string::npos || e < 1) return 0;
    const char m1 = static_cast<char>(std::toupper(static_cast<unsigned char>(cell[e])));
    const char m0 = static_cast<char>(std::toupper(static_cast<unsigned char>(cell[e - 1])));
    if (m1 != 'M' || (m0 != 'A' && m0 != 'P')) return 0;
    return m0 == 'A' ? 1 : 2;
}

/*! \brief Parse a series timestamp cell into calendar components.
 *
 *  Mirrors the engine's parse_series_datetime exactly:
 *   - ISO-8601:  YYYY-MM-DD[ T]HH:MM[:SS]
 *   - US:        M/D/YYYY [H:MM[:SS]] with an optional trailing AM/PM token
 *                (the PCSWMM .tsf form; 12:xx AM → 00:xx, h PM → h+12)
 *  Component *validity* (real calendar date, hour range) is the caller's
 *  concern — the engine defers that to its date encoder, the Qt wrapper
 *  defers it to QDate/QTime. */
inline bool parseSeriesDateTime(const std::string& cell, DateTimeParts& out)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    const char* c = cell.c_str();

    // ISO-8601: YYYY-MM-DD[ T]HH:MM[:SS]
    int n = std::sscanf(c, "%d-%d-%d%*[ T]%d:%d:%d", &y, &mo, &d, &h, &mi, &s);
    if (n >= 3) {
        if (n < 4) { h = mi = s = 0; }
        else if (n < 6) { s = 0; }
        out = {y, mo, d, h, mi, s};
        return true;
    }

    // US: MM/DD/YYYY[ HH:MM[:SS]], optionally 12-hour with trailing AM/PM.
    n = std::sscanf(c, "%d/%d/%d %d:%d:%d", &mo, &d, &y, &h, &mi, &s);
    if (n >= 3) {
        if (n < 4) { h = mi = s = 0; }
        else if (n < 6) { s = 0; }
        if (n >= 4) {
            const int meridiem = trailingMeridiem(cell);
            if (meridiem != 0) {
                if (h == 12) h = 0;          // 12:xx AM → 00:xx, 12:xx PM → 12:xx
                if (meridiem == 2) h += 12;  // PM
            }
        }
        out = {y, mo, d, h, mi, s};
        return true;
    }
    return false;
}

/*! \brief Split a "path[:column]" token into its parts.
 *
 *  Unified rule, shared with both engine readers (review B-2 — the engine's
 *  gage reader used first-colon-after-drive-letter while its timeseries
 *  reader used rfind(':'), so a path containing a colon produced two
 *  different cache keys):
 *    1. Consider the LAST ':' in the token.
 *    2. Ignore it when it is a Windows drive-letter colon (index 1 with an
 *       alphabetic index 0), e.g. "C:\rain.csv".
 *    3. Treat it as the column separator only when the suffix after it holds
 *       no path separator ('/' or '\\'); otherwise the colon belongs to a
 *       directory name (POSIX/macOS allow it, e.g. "/x/Data:2024/f.csv").
 *  No separator → the whole token is the path and the column is empty. */
inline void splitPathColumn(const std::string& token,
                            std::string& pathOut, std::string& columnOut)
{
    const std::size_t colSep = token.rfind(':');
    const bool driveLetter =
        colSep == 1 && std::isalpha(static_cast<unsigned char>(token[0]));
    if (colSep != std::string::npos && !driveLetter
        && token.find_first_of("/\\", colSep + 1) == std::string::npos) {
        pathOut   = token.substr(0, colSep);
        columnOut = token.substr(colSep + 1);
    } else {
        pathOut   = token;
        columnOut.clear();
    }
}

/*! \brief Compose the "path:column" token (empty column → bare path).
 *  The GUI always builds this token itself — the user never types the colon. */
inline std::string composePathColumn(const std::string& path,
                                     const std::string& column)
{
    return column.empty() ? path : path + ":" + column;
}

/*! \brief Resolve a column selector against a value-column name list
 *  (0-based, time column excluded). Case-insensitive, mirroring the
 *  engine's find_column. Returns -1 when \a name matches nothing. */
inline int findColumn(const std::vector<std::string>& valueHeaders,
                      const std::string& name)
{
    for (std::size_t i = 0; i < valueHeaders.size(); ++i)
        if (iequals(valueHeaders[i], name)) return static_cast<int>(i);
    return -1;
}

/*! \brief Display name the GUI shows for value column \a oneBasedIndex of a
 *  headerless file. Single definition of the convention.
 *
 *  These names exist only in the GUI preview: the engine has no
 *  header-vs-data probe (it always spends the first content line as the
 *  header row), so a fabricated name can never be resolved by find_column
 *  and must never reach a persisted "path:col" token (review B-4). Callers
 *  learn that a header list is fabricated from readHeaders()/readColumn()'s
 *  \c fabricatedOut flag — never by pattern-matching this string, which a
 *  real file is free to use as a genuine header. */
inline std::string fabricatedColumnName(std::size_t oneBasedIndex)
{
    return "col_" + std::to_string(oneBasedIndex);
}

} // namespace openswmmvis::ui::extcol

#endif // OPENSWMMVIS_UI_UTIL_EXTERNALCOLUMNFILECORE_H
