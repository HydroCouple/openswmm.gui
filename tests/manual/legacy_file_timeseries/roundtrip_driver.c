/* Minimal repro driver: mimic the GUI's open -> save path.
 * Opens the .inp with the v6 engine and writes it back with the built-in
 * InpWriter (same call the GUI's SWMMVisProjectWindow::saveAs makes), so the
 * round-tripped file can be fed to the legacy worker exactly as a GUI run
 * would after auto-save.
 *
 * Usage: roundtrip_driver <in.inp> <out.inp>
 */
#include <stdio.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s in.inp out.inp\n", argv[0]); return 2; }

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
