// Validation probe for the Attribute Table's output-backed dynamics columns.
//
// Replicates, byte for byte, the aggregation SWMMResultsLayer::linkStatsFor /
// subcatchStatsFor perform, so the numbers printed here are the numbers the
// Attribute Table shows. Compare against the .rpt summary tables the same run
// produced: Link Flow Summary (Maximum |Flow| / |Veloc| / Max-Full-Depth) and
// Subcatchment Runoff Summary (Total Precip / Total Runoff / Peak Runoff).
//
// Build + run: see RESULTS.md in this directory.

#include <openswmm/engine/openswmm_output.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_xsect.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Same tables as swmmresultslayer.cpp.
static constexpr double kQcf[6] = {1.0, 448.831, 0.64632, 0.02832, 28.317, 2.4466};
static constexpr double kUcfRainfall[2] = {43200.0, 1097280.0};
static constexpr double kUcfLandArea[2] = {2.2956e-5, 0.92903e-5};
static constexpr double kUcfVolume[2]   = {1.0, 0.02832};

int main(int argc, char **argv)
{
    if (argc < 3) { std::printf("usage: verify_dynamics <inp> <out>\n"); return 2; }

    SWMM_Output out = swmm_output_open(argv[2]);
    if (!out) { std::printf("open .out failed\n"); return 1; }

    const int nPeriods = swmm_output_get_period_count(out);
    const int fu       = swmm_output_get_flow_units(out);
    const bool si      = fu >= 3;
    const double qcf   = kQcf[fu];
    const double dt    = double(swmm_output_get_report_step(out));
    std::printf("periods=%d flow_units=%d report_step=%.0fs\n\n", nPeriods, fu, dt);

    // The subcatchment area comes from the model, exactly as the layer reads
    // it from m_modelLayer->engine().
    SWMM_Engine eng = swmm_engine_create();
    swmm_engine_open(eng, argv[1], "", "", nullptr);

    std::vector<float> buf(static_cast<size_t>(nPeriods));
    const int last = nPeriods - 1;

    std::printf("LINKS   (compare: Link Flow Summary)\n");
    std::printf("%-8s %10s %10s %10s %14s %10s\n",
                "link", "maxFlow", "maxVeloc", "maxFill", "volFlow(ft3)", "surchHr");
    for (int i = 0; i < swmm_output_get_link_count(out); ++i) {
        const char *lid = swmm_output_get_link_id(out, i);
        double maxFlow = 0, maxVel = 0, maxFill = 0, vol = 0, surch = 0;
        if (swmm_output_get_link_series(out, i, SWMM_OUT_LINK_FLOW, 0, last, buf.data()) == 0)
            for (float f : buf) {
                const double q = std::abs(double(f));
                if (q > maxFlow) maxFlow = q;
                vol += q;
            }
        vol = vol / qcf * dt * kUcfVolume[si ? 1 : 0];
        if (swmm_output_get_link_series(out, i, SWMM_OUT_LINK_VELOCITY, 0, last, buf.data()) == 0)
            for (float f : buf)
                if (std::abs(double(f)) > maxVel) maxVel = std::abs(double(f));

        // Full depth from the model's resolved cross-section — NOT
        // SWMM_OUT_LINK_CAPACITY, which is the area ratio.
        double yFull = 0.0;
        const int lIdx = swmm_link_index(eng, lid);
        SWMM_XSect xs = nullptr;
        if (lIdx >= 0 && swmm_link_create_xsect(eng, lIdx, &xs) == SWMM_OK && xs) {
            swmm_xsect_full_properties(xs, &yFull, nullptr, nullptr, nullptr, nullptr, nullptr);
            swmm_xsect_free(xs);
        }
        if (yFull > 0.0
            && swmm_output_get_link_series(out, i, SWMM_OUT_LINK_DEPTH, 0, last, buf.data()) == 0) {
            int n = 0;
            for (float f : buf) {
                const double fill = double(f) / yFull;
                if (fill > maxFill) maxFill = fill;
                if (fill >= 1.0) ++n;
            }
            surch = n * dt / 3600.0;
        }
        std::printf("%-8s %10.2f %10.2f %10.2f %14.0f %10.2f\n",
                    lid, maxFlow, maxVel, maxFill, vol, surch);
    }

    // ---- Pumps (compare: Pumping Summary) --------------------------------
    //
    // The engine's on/off test is `setting > 0 && q > 0` (SWMMEngine.cpp).
    // Both operands are in the .out file: FLOW is q, and CAPACITY is the
    // link's `setting` for every non-conduit (link.c:703). So all three pump
    // statistics reconstruct from the output after all.
    std::printf("\nPUMPS   (compare: Pumping Summary)\n");
    std::printf("%-26s %8s %10s %14s %10s\n",
                "pump", "cycles", "onTime(hr)", "volume(m3/ft3)", "%utilized");
    const double totalHr = nPeriods * dt / 3600.0;
    for (int i = 0; i < swmm_output_get_link_count(out); ++i) {
        const char *lid = swmm_output_get_link_id(out, i);
        const int lIdx = swmm_link_index(eng, lid);
        int ltype = -1;
        if (lIdx < 0 || swmm_link_get_type(eng, lIdx, &ltype) != SWMM_OK) continue;
        if (ltype != SWMM_LINK_PUMP) continue;

        std::vector<float> setting(static_cast<size_t>(nPeriods));
        if (swmm_output_get_link_series(out, i, SWMM_OUT_LINK_FLOW, 0, last, buf.data()) != 0)
            continue;
        if (swmm_output_get_link_series(out, i, SWMM_OUT_LINK_CAPACITY, 0, last,
                                         setting.data()) != 0)
            continue;

        int cycles = 0, onPeriods = 0;
        bool wasOn = false;
        double vol = 0.0;
        for (size_t p = 0; p < buf.size(); ++p) {
            const double q = std::abs(double(buf[p]));
            const bool isOn = (double(setting[p]) > 0.0 && q > 0.0);
            if (isOn && !wasOn) ++cycles;
            wasOn = isOn;
            if (isOn) { ++onPeriods; vol += q; }
        }
        const double onHr = onPeriods * dt / 3600.0;
        vol = vol / qcf * dt * kUcfVolume[si ? 1 : 0];
        std::printf("%-26s %8d %10.2f %14.0f %10.2f\n",
                    lid, cycles, onHr, vol, 100.0 * onHr / totalHr);
    }

    std::printf("\nSUBCATCHMENTS   (compare: Subcatchment Runoff Summary)\n");
    std::printf("%-8s %10s %14s %14s %10s\n",
                "sub", "area", "precip(ft3)", "runoff(ft3)", "peakRunoff");
    for (int i = 0; i < swmm_output_get_subcatch_count(out); ++i) {
        const char *id = swmm_output_get_subcatch_id(out, i);
        double maxRunoff = 0, runoffVol = 0, precipVol = 0, areaProj = 0;
        if (swmm_output_get_subcatch_series(out, i, SWMM_OUT_SUBCATCH_RUNOFF, 0, last, buf.data()) == 0)
            for (float f : buf) {
                if (double(f) > maxRunoff) maxRunoff = double(f);
                runoffVol += double(f);
            }
        runoffVol = runoffVol / qcf * dt * kUcfVolume[si ? 1 : 0];

        const int sIdx = swmm_subcatch_index(eng, id);
        if (sIdx >= 0) swmm_subcatch_get_area(eng, sIdx, &areaProj);
        const double areaFt2 = areaProj / kUcfLandArea[si ? 1 : 0];
        if (areaFt2 > 0.0
            && swmm_output_get_subcatch_series(out, i, SWMM_OUT_SUBCATCH_RAINFALL,
                                                0, last, buf.data()) == 0) {
            double depthFt = 0;
            for (float f : buf) depthFt += double(f) / kUcfRainfall[si ? 1 : 0] * dt;
            precipVol = depthFt * areaFt2 * kUcfVolume[si ? 1 : 0];
        }
        std::printf("%-8s %10.3f %14.0f %14.0f %10.2f    [precip=%.2f in, runoff=%.3f 10^6gal]\n",
                    id, areaProj, precipVol, runoffVol, maxRunoff,
                    areaFt2 > 0 ? precipVol / areaFt2 * 12.0 : 0.0,
                    runoffVol * 7.48052 / 1.0e6);
    }

    swmm_engine_close(eng);
    swmm_engine_destroy(eng);
    swmm_output_close(out);
    return 0;
}
