/* Minimal repro driver: mimic the GUI's open -> save path.
 * Opens the .inp with the v6 engine and writes it back with the built-in
 * InpWriter (same call the GUI's SWMMVisProjectWindow::saveAs makes), so the
 * round-tripped file can be fed to the legacy worker exactly as a GUI run
 * would after auto-save.
 *
 * Usage: roundtrip_driver <in.inp> <out.inp>
 */

/**
 * @file      roundtrip_driver.c
 * @brief     Tests for roundtrip driver.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <stdio.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

/**
 *
 * @brief Open/save a deck through the native engine for manual round-trip inspection.
 * @param argc Argument count, at least 3 for execution; extra arguments ignored.
 * @param argv Borrowed process argument strings: program name, input .inp, output
 * .inp, each null-terminated. Output is a test-owned writable path.
 * @return 2 for usage error, 1 for engine open/write failure, otherwise 0 after close/
 * destroy. Writes fixed roundtrip_driver.rpt in cwd and may replace the destination.
 * Create failure is not separately checked; early error returns do not destroy eng.
 * Prints diagnostics, but does not run/compare the saved model or roll back files.
 *
 */
int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s in.inp out.inp\n", argv[0]);
        return 2;
    }

    SWMM_Engine eng = swmm_engine_create();
    int rc = swmm_engine_open(eng, argv[1], "roundtrip_driver.rpt", NULL, NULL);
    if (rc != 0) {
        fprintf(stderr, "open failed rc=%d: %s\n", rc, swmm_get_last_error_msg(eng));
        int n = swmm_get_error_count(eng);
        for (int i = 0; i < n; ++i)
            fprintf(stderr, "  err[%d]: %s\n", i, swmm_get_error_at(eng, i));
        return 1;
    }
    rc = swmm_model_write(eng, argv[2]);
    if (rc != 0) {
        fprintf(stderr, "write failed rc=%d: %s\n", rc, swmm_get_last_error_msg(eng));
        return 1;
    }
    swmm_engine_close(eng);
    swmm_engine_destroy(eng);
    printf("round-trip OK: %s -> %s\n", argv[1], argv[2]);
    return 0;
}
