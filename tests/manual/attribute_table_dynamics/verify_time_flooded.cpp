// Unit check for the Attribute Table's "Time Flooded (hr)" node column.
//
// The engine's node time-flooded aggregators return SECONDS — OutputReader
// counts flooded report periods and multiplies by report_step, and the
// legacy statsrpt.c divides by 3600 for its display-only "hours" column.
// The GUI column has always been labelled "(hr)", and a comment in
// swmmattributetablemodel.cpp asserted the value "is already hours", so the
// cell read 3600x high on all three paths that serve it
// (SWMMResultsLayer::nodeStatTimeFlooded, the node_stat_time_flooded getter
// tag, and SWMMModelLayer::identifyByName).
//
// Ground truth is the run's own .rpt "Node Flooding Summary" -> Hours Flooded.
// For pump_model.rpt: CALEXAN-I = 0.55, CCANPRE-I = 0.06, SCANOUT1 = 0.00.
//
// Build + run: see RESULTS.md in this directory (same recipe as
// verify_dynamics.cpp), then:
//   ./tests/manual/attribute_table_dynamics/verify_time_flooded \
//       tests/manual/attribute_table_dynamics/pump_model.out

/**
 * @file      verify_time_flooded.cpp
 * @brief     Tests for verify time flooded.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <openswmm/engine/openswmm_output.h>

#include <cstdio>
#include <cstring>
#include <string>

/**
 *
 * @brief Print flooded-node raw seconds and converted hours for manual report comparison.
 * @param argc Process argument count; optional first path overrides pump_model.out.
 * @param argv Borrowed null-terminated process strings; argv[1] is the native output
 * filename when present, remaining arguments ignored.
 * @return 1 if output cannot open, otherwise 0 after closing it. Skips missing node
 * IDs, failed statistics and nonpositive values. No automated comparison/tolerance
 * against the report; stdout values divide seconds by 3600 and no files are written.
 *
 */
int main(int argc, char **argv)
{
    const char *out = (argc > 1) ? argv[1] : "pump_model.out";

    SWMM_Output h = swmm_output_open(out);
    if (!h) {
        std::fprintf(stderr, "cannot open %s\n", out);
        return 1;
    }

    const int nNodes = swmm_output_get_node_count(h);

    std::printf("%-16s %14s %14s\n", "node", "raw(engine)", "hours(/3600)");
    std::printf("%-16s %14s %14s\n", "----", "-----------", "------------");

    for (int i = 0; i < nNodes; ++i) {
        const char *id = swmm_output_get_node_id(h, i);
        if (!id)
            continue;

        double raw = 0.0;
        if (swmm_output_get_node_stat_time_flooded(h, i, &raw) != 0)
            continue;
        if (raw <= 0.0)
            continue; // only the nodes the .rpt lists

        std::printf("%-16s %14.2f %14.4f\n", id, raw, raw / 3600.0);
    }

    swmm_output_close(h);
    std::printf("\nCompare the right column against the .rpt "
                "'Node Flooding Summary' -> 'Hours Flooded'.\n");
    return 0;
}
