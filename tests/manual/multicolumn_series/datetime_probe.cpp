// Ad-hoc probe: what timestamp cells does the multi-column series parser
// accept, and how does it interpret them?  Mirrors the engine grammar in
// include/ui/util/externalcolumnfilecore.h (parseSeriesDateTime).
//   c++ -std=c++17 -I../../../include datetime_probe.cpp -o datetime_probe && ./datetime_probe

/**
 * @file      datetime_probe.cpp
 * @brief     Tests for datetime probe.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/util/externalcolumnfilecore.h"
#include <cstdio>
#include <string>
#include <vector>

/**
 *
 * @brief Print acceptance/calendar results for the hardcoded timestamp grammar matrix.
 * @return Always 0; rejected cells are reported as NO rather than assertion failures.
 * Calls the production external-column parser with ISO/US/invalid timestamp samples,
 * prints calendar components without timezone conversion, and writes no files.
 *
 */
int main()
{
    const std::vector<std::string> cells = {
        // ISO-8601
        "2007-01-01",
        "2007-01-01 00:00",
        "2007-01-01 13:30:15",
        "2007-01-01T13:30",
        "2007-01-01T13:30:15",
        "2007-1-1 5:07",
        "2007-01-01T13:30:15Z",
        "2007-01-01T13:30:15+05:00",
        "2007-01-01 13:30:15.500",
        // US
        "1/1/2007",
        "01/01/2007 13:30",
        "1/1/2007 13:30:15",
        "1/1/2007 12:00:00 AM",
        "1/1/2007 12:30:00 PM",
        "1/1/2007 1:30 PM",
        "1/1/2007 1:30 pm",
        "1/1/2007 11:59:59 PM",
        "1/1/2007 13:30 PM",
        // edge / rejected
        "1/1/07 6:00",
        "13/01/2007 00:00",
        "2007/01/01 00:00",
        "Jan 1, 2007 00:00",
        "1-1-2007 06:00",
        "0.25",
        "36524.5",
        "",
    };
    std::printf("%-30s | %-3s | parsed\n", "cell", "ok");
    std::printf("-------------------------------|-----|-------------------\n");
    for (const auto &c : cells) {
        openswmmvis::ui::extcol::DateTimeParts p;
        const bool ok = openswmmvis::ui::extcol::parseSeriesDateTime(c, p);
        if (ok)
            std::printf("%-30s | yes | %04d-%02d-%02d %02d:%02d:%02d\n", c.c_str(), p.year, p.month,
                        p.day, p.hour, p.minute, p.second);
        else
            std::printf("%-30s | NO  | -\n", c.c_str());
    }
    return 0;
}
