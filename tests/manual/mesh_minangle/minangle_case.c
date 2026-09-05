/* One case per process so a non-terminating configuration can be timed out and
 * attributed, instead of hanging the whole sweep (which is what v2 did).
 *
 * usage: minangle_case <originX> <originY> <gap> <minAngle> [maxArea]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "triangle.h"

int main(int argc, char **argv)
{
    struct triangulateio in, out;
    char sw[64];
    int rc, i;
    double ox, oy, gap, minangle, maxarea;

    if (argc < 5) { fprintf(stderr, "need 4 args\n"); return 2; }
    ox = atof(argv[1]); oy = atof(argv[2]);
    gap = atof(argv[3]); minangle = atof(argv[4]);
    maxarea = (argc > 5) ? atof(argv[5]) : 0.0;

    memset(&in, 0, sizeof(in)); memset(&out, 0, sizeof(out));

    in.numberofpoints = 6;
    in.pointlist = (REAL *) malloc(sizeof(REAL) * 12);
    {
        double P[12] = { 0,0, 1000,0, 1000,1000, 0,1000, 200,0, 800,0 };
        P[9] = gap; P[11] = gap;
        for (i = 0; i < 6; ++i) {
            in.pointlist[2*i+0] = ox + P[2*i+0];
            in.pointlist[2*i+1] = oy + P[2*i+1];
        }
    }
    in.numberofsegments = 5;
    in.segmentlist = (int *) malloc(sizeof(int) * 10);
    { int S[10] = {0,1, 1,2, 2,3, 3,0, 4,5}; memcpy(in.segmentlist, S, sizeof(S)); }

    if (maxarea > 0) snprintf(sw, sizeof(sw), "pzq%.2fa%.4fQ", minangle, maxarea);
    else             snprintf(sw, sizeof(sw), "pzq%.2fQ", minangle);

    rc = triangulate_safe(sw, &in, &out, NULL);
    if (rc != 0) { printf("FATAL\n"); return 1; }
    printf("ok tris=%d verts=%d\n", out.numberoftriangles, out.numberofpoints);
    return 0;
}
