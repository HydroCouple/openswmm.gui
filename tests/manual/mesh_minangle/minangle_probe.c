/* Probe: why does forcing a minimum angle fail, and why worse on large areas?
 *
 * Hypothesis under test:
 *   (a) Triangle's -q refinement dies with "Ran out of precision" when the
 *       PSLG contains a SMALL INPUT ANGLE, not because the input is degenerate.
 *   (b) The failure arrives SOONER at large coordinate magnitudes (UTM), because
 *       the split point is compared to the segment endpoint in EXACT doubles and
 *       the ULP grows with coordinate magnitude.
 *
 * Build:
 *   cc -DTRILIBRARY -DANSI_DECLARATORS -DNO_TIMER -Ivendor/triangle \
 *      tests/manual/mesh_minangle/minangle_probe.c vendor/triangle/triangle.c \
 *      -o tests/manual/mesh_minangle/minangle_probe -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "triangle.h"

/* triangle.c is built here WITHOUT EXTERNAL_TEST, so it supplies its own
   triunsuitable(). Nothing to provide. */

static void run(const char *label, double ox, double oy,
                double wedge_deg, double minangle)
{
    struct triangulateio in, out;
    char sw[64];
    int rc;

    /* A wedge: a thin triangle whose apex angle is `wedge_deg`. This is the
       classic small-input-angle PSLG that -q cannot satisfy. */
    double apex_dx = 1000.0;
    double half = wedge_deg * 0.5 * 3.14159265358979323846 / 180.0;
    double dy = apex_dx * (half > 0 ? (double)tan(half) : 0.0);

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    in.numberofpoints = 3;
    in.pointlist = (REAL *) malloc(sizeof(REAL) * 6);
    in.pointlist[0] = ox;              in.pointlist[1] = oy;            /* apex */
    in.pointlist[2] = ox + apex_dx;    in.pointlist[3] = oy - dy;
    in.pointlist[4] = ox + apex_dx;    in.pointlist[5] = oy + dy;

    in.numberofsegments = 3;
    in.segmentlist = (int *) malloc(sizeof(int) * 6);
    in.segmentlist[0]=0; in.segmentlist[1]=1;
    in.segmentlist[2]=1; in.segmentlist[3]=2;
    in.segmentlist[4]=2; in.segmentlist[5]=0;

    snprintf(sw, sizeof(sw), "pzq%.2fa%.4f", minangle, 5000.0);

    printf("---- %s | origin=(%.0f,%.0f) wedge=%.1f deg minAngle=%.1f sw=%s\n",
           label, ox, oy, wedge_deg, minangle, sw);
    fflush(stdout);

    rc = triangulate_safe(sw, &in, &out, NULL);

    if (rc != 0) printf("   RESULT: Triangle FATAL (triexit) -> GUI would say 'degenerate geometry'\n");
    else         printf("   RESULT: ok, %d triangles, %d vertices\n",
                        out.numberoftriangles, out.numberofpoints);
    fflush(stdout);

    free(in.pointlist); free(in.segmentlist);
}

int main(void)
{
    /* (a) small input angle, near the origin */
    run("small angle @ origin", 0.0, 0.0, 1.0, 26.0);
    /* same geometry, no min angle -> should succeed */
    run("small angle @ origin, NO min angle", 0.0, 0.0, 1.0, 0.0);
    /* (b) identical geometry translated to a UTM-scale origin */
    run("small angle @ UTM", 500000.0, 5400000.0, 1.0, 26.0);
    /* a benign wedge for contrast */
    run("benign 60 deg @ origin", 0.0, 0.0, 60.0, 26.0);
    run("benign 60 deg @ UTM",    500000.0, 5400000.0, 60.0, 26.0);
    return 0;
}
