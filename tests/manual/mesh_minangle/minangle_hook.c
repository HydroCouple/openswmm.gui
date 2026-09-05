/* Does the cancellation/progress hook (triunsuitable) actually fire while
 * Triangle is stuck in runaway -q refinement? If it does, the GUI's Cancel
 * works and a vertex budget can abort cleanly. If it does not, the worker is
 * unkillable and the only fix is to refuse the run up front.
 *
 * Built WITH -DEXTERNAL_TEST so this file supplies triunsuitable().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "triangle.h"
extern void triexit(int);

static long g_calls = 0;
static long g_limit = 0;

int triunsuitable(double *triorg, double *tridest, double *triapex, REAL area)
{
    (void)triorg; (void)tridest; (void)triapex; (void)area;
    ++g_calls;
    if (g_limit > 0 && g_calls >= g_limit) {
        printf("  hook fired %ld times -> requesting abort via triexit path\n", g_calls);
        fflush(stdout);
        /* Emulate what a budget guard would do: bail out of Triangle. */
        triexit(1);
    }
    return 0;   /* never ask for extra refinement of our own */
}

int main(int argc, char **argv)
{
    struct triangulateio in, out;
    char sw[64];
    int rc, i;
    double gap = (argc > 1) ? atof(argv[1]) : 0.001;
    double ang = (argc > 2) ? atof(argv[2]) : 26.0;
    g_limit    = (argc > 3) ? atol(argv[3]) : 2000000L;

    memset(&in, 0, sizeof(in)); memset(&out, 0, sizeof(out));
    in.numberofpoints = 6;
    in.pointlist = (REAL *) malloc(sizeof(REAL) * 12);
    { double P[12] = {0,0, 1000,0, 1000,1000, 0,1000, 200,0, 800,0};
      P[9]=gap; P[11]=gap;
      for (i=0;i<12;++i) in.pointlist[i]=P[i]; }
    in.numberofsegments = 5;
    in.segmentlist = (int *) malloc(sizeof(int)*10);
    { int S[10]={0,1,1,2,2,3,3,0,4,5}; memcpy(in.segmentlist,S,sizeof(S)); }

    /* 'u' enables the usertest hook - exactly what MeshGenerator emits. */
    snprintf(sw, sizeof(sw), "pzq%.2fuQ", ang);
    printf("gap=%g minAngle=%g switches=%s hookLimit=%ld\n", gap, ang, sw, g_limit);
    fflush(stdout);

    rc = triangulate_safe(sw, &in, &out, NULL);
    printf("rc=%d  hookCalls=%ld  %s\n", rc, g_calls,
           rc ? "(aborted)" : "(completed)");
    return 0;
}
