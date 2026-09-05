/* Probe v2: which PSLG configuration actually kills -q refinement?
 *
 * v1 showed a lone small input angle is handled gracefully (Triangle gives up
 * on that corner by design). So test CLOSE FEATURES instead: a constraint
 * vertex/segment lying very near another segment without touching it, which is
 * what a SWMM model is full of (conduits passing close to unrelated nodes).
 *
 * Also tests whether coordinate MAGNITUDE changes the outcome for the same
 * geometry, i.e. whether large-area/UTM models fail where local ones survive.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "triangle.h"

static int run(const char *label, double ox, double oy, double gap,
               double minangle, double maxarea, int verbose)
{
    struct triangulateio in, out;
    char sw[64];
    int rc;
    memset(&in, 0, sizeof(in)); memset(&out, 0, sizeof(out));

    /* Outer box 0..1000 x 0..1000, plus an interior constraint segment that
       runs parallel to the bottom edge at height `gap`. As gap -> 0 the two
       segments become a "close feature" pair that -q must resolve. */
    in.numberofpoints = 6;
    in.pointlist = (REAL *) malloc(sizeof(REAL) * 12);
    double P[12] = { 0,0,  1000,0,  1000,1000,  0,1000,   200,0,   800,0 };
    P[8]  = 200;  P[9]  = gap;
    P[10] = 800;  P[11] = gap;
    for (int i = 0; i < 6; ++i) {
        in.pointlist[2*i+0] = ox + P[2*i+0];
        in.pointlist[2*i+1] = oy + P[2*i+1];
    }
    in.numberofsegments = 5;
    in.segmentlist = (int *) malloc(sizeof(int) * 10);
    int S[10] = {0,1, 1,2, 2,3, 3,0, 4,5};
    memcpy(in.segmentlist, S, sizeof(S));

    if (maxarea > 0) snprintf(sw, sizeof(sw), "pzq%.2fa%.4fQ", minangle, maxarea);
    else             snprintf(sw, sizeof(sw), "pzq%.2fQ", minangle);

    rc = triangulate_safe(sw, &in, &out, NULL);
    printf("  gap=%-10.6g origin=%-9.0f minAngle=%-5.1f -> %s",
           gap, ox, minangle,
           rc != 0 ? "FATAL (GUI: 'degenerate geometry')" : "ok");
    if (rc == 0) printf("  (%d tris)", out.numberoftriangles);
    printf("\n"); fflush(stdout);
    free(in.pointlist); free(in.segmentlist);
    return rc;
}

int main(void)
{
    double gaps[] = { 10.0, 1.0, 1e-2, 1e-4, 1e-6, 1e-8 };
    int i;

    printf("== A. close-feature gap sweep, LOCAL coords (origin 0), minAngle 26\n");
    for (i = 0; i < 6; ++i) run("local", 0.0, 0.0, gaps[i], 26.0, 0.0, 0);

    printf("\n== B. same gaps, UTM-scale coords (origin 500000, 5400000)\n");
    for (i = 0; i < 6; ++i) run("utm", 500000.0, 5400000.0, gaps[i], 26.0, 0.0, 0);

    printf("\n== C. same gaps, LOCAL, NO min angle (isolates -q as the cause)\n");
    for (i = 0; i < 6; ++i) run("noq", 0.0, 0.0, gaps[i], 0.0, 0.0, 0);

    printf("\n== D. minAngle sweep at a fixed awkward gap (1e-4), LOCAL\n");
    { double a; for (a = 5.0; a <= 34.0; a += 5.0) run("angle", 0.0, 0.0, 1e-4, a, 0.0, 0); }

    return 0;
}
