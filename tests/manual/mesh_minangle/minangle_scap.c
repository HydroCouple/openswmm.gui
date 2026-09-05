/* Does Triangle's -S Steiner cap turn the runaway -q refinement into a
 * bounded, SUCCESSFUL run (degraded quality) rather than a hang?
 * usage: minangle_scap <gap> <minAngle> <steinerCap|-1>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "triangle.h"
int main(int argc, char **argv)
{
    struct triangulateio in, out; char sw[64]; int rc, i;
    double gap = atof(argv[1]), ang = atof(argv[2]); long cap = atol(argv[3]);
    memset(&in,0,sizeof(in)); memset(&out,0,sizeof(out));
    in.numberofpoints = 6;
    in.pointlist = (REAL*)malloc(sizeof(REAL)*12);
    { double P[12]={0,0,1000,0,1000,1000,0,1000,200,0,800,0};
      P[9]=gap; P[11]=gap; for(i=0;i<12;++i) in.pointlist[i]=P[i]; }
    in.numberofsegments = 5;
    in.segmentlist = (int*)malloc(sizeof(int)*10);
    { int S[10]={0,1,1,2,2,3,3,0,4,5}; memcpy(in.segmentlist,S,sizeof(S)); }
    if (cap > 0) snprintf(sw,sizeof(sw),"pzq%.2fS%ldQ",ang,cap);
    else         snprintf(sw,sizeof(sw),"pzq%.2fQ",ang);
    rc = triangulate_safe(sw,&in,&out,NULL);
    if (rc) printf("FATAL  sw=%s\n", sw);
    else    printf("ok tris=%-8d verts=%-8d sw=%s\n", out.numberoftriangles, out.numberofpoints, sw);
    return 0;
}
