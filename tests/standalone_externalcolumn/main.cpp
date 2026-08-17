/*!
 * \file   main.cpp
 * \brief  Standalone (Qt-free) checks for ui/util/externalcolumnfilecore.h —
 *         the pure parsing core behind the external-column-file util
 *         (workplans/MULTICOLUMN_SERIES_SINGLE_READ_2026-08-17.md, GUI
 *         handoff §Build & verification). Compile + run with any C++17
 *         compiler; see README.md. The Qt-level readHeaders/readColumn
 *         wrapper is covered by tests/unit/test_externalcolumnfile.cpp.
 */
#include "../../include/ui/util/externalcolumnfilecore.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace ec = openswmmvis::ui::extcol;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  "    \
                      << #cond << "\n";                                    \
        }                                                                  \
    } while (0)

static void testCellTrimAndSplit()
{
    CHECK(ec::cellTrim("  x \r\n") == "x");
    CHECK(ec::cellTrim("\"quoted name\"") == "quoted name");

    const auto cells = ec::splitCells("a,\"b,c\",d", ',');
    CHECK(cells.size() == 3);
    // Quote-aware: the inner comma is not a split point, and cellTrim then
    // strips the surrounding quotes (engine cell_trim parity).
    CHECK(cells[1] == "b,c");

    const auto tabs = ec::splitCells("t1\tt2\t", '\t');
    CHECK(tabs.size() == 3);
    CHECK(tabs[2].empty());         // trailing empty cell preserved
}

static void testLineClassification()
{
    // Split literals so "\xBF" isn't glued to the 'D' (hex-escape greed).
    std::string bom = "\xEF\xBB\xBF" "Date,V";
    ec::normalizeLine(bom, /*firstLine=*/true);
    CHECK(bom == "Date,V");

    std::string crlf = "Date,V\r";
    ec::normalizeLine(crlf, false);
    CHECK(crlf == "Date,V");

    CHECK(ec::isBlankOrComment(""));
    CHECK(ec::isBlankOrComment("   "));
    CHECK(ec::isBlankOrComment("; comment"));
    CHECK(ec::isBlankOrComment("# comment"));
    CHECK(!ec::isBlankOrComment("2007-01-01,1.0"));

    CHECK(ec::isTsfHeader("IDs:\tRG1\tRG2"));
    CHECK(ec::isTsfHeader("ids:\tRG1"));      // case-insensitive (engine parity)
    CHECK(!ec::isTsfHeader("Identifiers"));
}

static void testSniffDelimiter()
{
    CHECK(ec::sniffDelimiter("a\tb\tc") == '\t');
    CHECK(ec::sniffDelimiter("a,b,c") == ',');
    CHECK(ec::sniffDelimiter("a;b;c") == ';');
    CHECK(ec::sniffDelimiter("abc") == ',');       // default
    // Real tie-break: 1 tab vs 1 comma. The old check used "a\tb,c\td"
    // (2 tabs vs 1 comma), which passes under `tabs > commas` too — so it
    // never exercised the `>=` (review E-3.6). This one fails if the rule
    // is weakened.
    CHECK(ec::sniffDelimiter("a\tb,c") == '\t');   // tabs == commas → tab

    // Quote-aware counting (review B-8): delimiters inside a quoted column
    // name must not be counted. Both of these mis-sniffed as comma before.
    CHECK(ec::sniffDelimiter("Date\t\"a,b,c\"") == '\t');
    CHECK(ec::sniffDelimiter("Date\t\"a;b;c\"\tR2") == '\t');
    // …and a genuinely comma-delimited header with a quoted comma still
    // sniffs as comma (the engine's worked example).
    CHECK(ec::sniffDelimiter("Date,\"Rain, North\",R2") == ',');
}

static void testDatetimes()
{
    ec::DateTimeParts p{};

    CHECK(ec::parseSeriesDateTime("2007-01-01 06:30:15", p));
    CHECK(p.year == 2007 && p.month == 1 && p.day == 1);
    CHECK(p.hour == 6 && p.minute == 30 && p.second == 15);

    CHECK(ec::parseSeriesDateTime("2007-01-01T06:30", p));
    CHECK(p.hour == 6 && p.minute == 30 && p.second == 0);

    CHECK(ec::parseSeriesDateTime("6/30/2007", p));
    CHECK(p.month == 6 && p.day == 30 && p.hour == 0);

    // 12-hour AM/PM (the PCSWMM .tsf form) — engine-mirroring edges.
    CHECK(ec::parseSeriesDateTime("1/1/2007 12:00:00 AM", p));
    CHECK(p.hour == 0 && p.minute == 0);            // 12 AM → midnight
    CHECK(ec::parseSeriesDateTime("1/1/2007 12:30:00 PM", p));
    CHECK(p.hour == 12 && p.minute == 30);          // 12 PM → noon
    CHECK(ec::parseSeriesDateTime("1/1/2007 8:15:00 PM", p));
    CHECK(p.hour == 20 && p.minute == 15);
    CHECK(ec::parseSeriesDateTime("1/1/2007 8:15 am", p));
    CHECK(p.hour == 8 && p.minute == 15);           // lowercase meridiem

    // Single-digit 24-hour US.
    CHECK(ec::parseSeriesDateTime("1/5/2020 3:04", p));
    CHECK(p.month == 1 && p.day == 5 && p.hour == 3 && p.minute == 4);

    CHECK(!ec::parseSeriesDateTime("not a date", p));
}

/*! Unified colon-split rule (review B-2): LAST colon, drive letter exempt,
 *  and only a suffix free of path separators counts as a column. Both engine
 *  readers now use the same rule, so a file referenced by a gage AND a
 *  timeseries derives the same cache key and is parsed once. */
static void testPathColumnTokens()
{
    std::string path, col;

    // 1. Plain relative path — no colon at all.
    ec::splitPathColumn("rain.csv", path, col);
    CHECK(path == "rain.csv" && col.empty());

    // 2. Relative path + column.
    ec::splitPathColumn("data/rain.csv:RG_2", path, col);
    CHECK(path == "data/rain.csv" && col == "RG_2");

    // 3. Windows drive letter, no column.
    ec::splitPathColumn("C:\\data\\rain.csv", path, col);
    CHECK(path == "C:\\data\\rain.csv" && col.empty());

    // 4. Windows drive letter + column.
    ec::splitPathColumn("C:\\data\\rain.csv:RG_2", path, col);
    CHECK(path == "C:\\data\\rain.csv" && col == "RG_2");

    // 4b. Drive-relative Windows path ("C:rain.csv") — the drive-letter
    //     exemption is what keeps this from splitting into "C" + "rain.csv";
    //     the path-separator guard alone cannot see it.
    ec::splitPathColumn("C:rain.csv", path, col);
    CHECK(path == "C:rain.csv" && col.empty());

    // 5. Bare filename + column.
    ec::splitPathColumn("f.csv:COL", path, col);
    CHECK(path == "f.csv" && col == "COL");

    // 6. Colon inside a DIRECTORY name (legal on POSIX/macOS) — the suffix
    //    after the last colon holds a path separator, so there is no column.
    //    The old first-colon rule returned path "/x/Data" here.
    ec::splitPathColumn("/x/Data:2024/f.csv", path, col);
    CHECK(path == "/x/Data:2024/f.csv" && col.empty());

    // 7. …and the same path WITH a column still splits in the right place.
    ec::splitPathColumn("/x/Data:2024/f.csv:EAST", path, col);
    CHECK(path == "/x/Data:2024/f.csv" && col == "EAST");

    // 8. A Windows path with a colon in a directory name, plus a column.
    ec::splitPathColumn("C:\\Data:2024\\f.csv:EAST", path, col);
    CHECK(path == "C:\\Data:2024\\f.csv" && col == "EAST");
    ec::splitPathColumn("C:\\Data:2024\\f.csv", path, col);
    CHECK(path == "C:\\Data:2024\\f.csv" && col.empty());

    CHECK(ec::composePathColumn("rain.csv", "") == "rain.csv");
    CHECK(ec::composePathColumn("rain.csv", "RG_2") == "rain.csv:RG_2");

    // Compose → split round-trips for every shape above, including the
    // colon-in-a-directory ones (the point of the unified rule).
    const char *paths[] = {"rain.csv", "data/rain.csv", "C:\\data\\rain.csv",
                           "/x/Data:2024/f.csv", "C:\\Data:2024\\f.csv"};
    for (const char *pth : paths) {
        ec::splitPathColumn(ec::composePathColumn(pth, "EAST"), path, col);
        CHECK(path == pth && col == "EAST");
        ec::splitPathColumn(ec::composePathColumn(pth, ""), path, col);
        CHECK(path == pth && col.empty());
    }
}

/*! The fabricated-name convention is defined once, in the core. It is a
 *  DISPLAY name only — never persisted (review B-4). */
static void testFabricatedColumnNames()
{
    CHECK(ec::fabricatedColumnName(1) == "col_1");
    CHECK(ec::fabricatedColumnName(12) == "col_12");
}

static void testFindColumn()
{
    const std::vector<std::string> headers{"RG_EAST", "RG_WEST"};
    CHECK(ec::findColumn(headers, "RG_WEST") == 1);
    CHECK(ec::findColumn(headers, "rg_east") == 0);   // case-insensitive
    CHECK(ec::findColumn(headers, "NOPE") == -1);
}

/*! Drive the actual TSF header flow over the bundled fixture: detect the
 *  IDs row, enumerate columns, skip the parameter + units rows, parse the
 *  first data row's AM/PM datetime and cell values. */
static void testTsfFixture()
{
    std::ifstream in("sample.tsf");
    if (!in.is_open()) {
        // Also tolerate being run from the repo root.
        in.open("tests/standalone_externalcolumn/sample.tsf");
    }
    CHECK(in.is_open());
    if (!in.is_open()) return;

    std::string line;
    CHECK(static_cast<bool>(std::getline(in, line)));
    ec::normalizeLine(line, true);
    CHECK(ec::isTsfHeader(line));

    const auto ids = ec::splitCells(line, '\t');
    CHECK(ids.size() == 3);
    CHECK(ids[1] == "RG1" && ids[2] == "RG2");

    std::getline(in, line);   // parameter row (Date/Time  Rainfall …)
    std::getline(in, line);   // units row (in.  in.)

    CHECK(static_cast<bool>(std::getline(in, line)));
    ec::normalizeLine(line, false);
    const auto cells = ec::splitCells(line, '\t');
    CHECK(cells.size() == 3);

    ec::DateTimeParts p{};
    CHECK(ec::parseSeriesDateTime(cells[0], p));
    CHECK(p.year == 2007 && p.month == 1 && p.day == 1);
    CHECK(p.hour == 0 && p.minute == 0);   // "12:00:00 AM" → midnight
    CHECK(cells[1] == "0.00" && cells[2] == "0.05");
}

int main()
{
    testCellTrimAndSplit();
    testLineClassification();
    testSniffDelimiter();
    testDatetimes();
    testPathColumnTokens();
    testFabricatedColumnNames();
    testFindColumn();
    testTsfFixture();

    if (g_failures == 0) {
        std::cout << "standalone_externalcolumn: ALL CHECKS PASSED\n";
        return 0;
    }
    std::cerr << "standalone_externalcolumn: " << g_failures << " FAILURE(S)\n";
    return 1;
}
