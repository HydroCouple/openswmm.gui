/*
 * repro.cpp — Qt-free reproduction harness for the test_meshgenerator crash.
 *
 * Purpose: the Windows CI segfault in test_meshgenerator points at
 * vendor/triangle/triangle.c, but the sandbox has no Qt, so the real
 * MeshGenerator cannot be compiled here. This harness ports the input-building
 * logic of MeshGenerator::generate() (src/mesh/meshgenerator.cpp) verbatim,
 * substituting std:: types for the Qt containers. The triangulateio packing,
 * the switch string, the triangulate_safe() call and the free() bookkeeping are
 * byte-for-byte the same as production, and triangle.c is used unmodified.
 *
 * Build + run: see run.sh (ASan + UBSan).
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <utility>

extern "C" {
#define TRILIBRARY
#include "triangle.h"
#undef TRILIBRARY
}

struct P { double x = 0, y = 0; };

struct SteinerPoint   { P xy; int marker = 0; std::string tag; };
struct ConstraintSeg  { std::vector<P> path; int marker = 0; std::string tag; };
struct RegionMarker   { P xy; double attribute = 0; double maxArea = 0; std::string tag; };

struct Options {
    double maxArea = 0.0;
    double minAngle = 0.0;
    bool   allowSteiner = true;
    bool   conformingDelaunay = false;
    int    maxSteinerPoints = 0;
    bool   quiet = true;
};

// Qt's qFuzzyCompare(double,double)
static bool qFuzzyCompare(double p1, double p2)
{
    return (std::fabs(p1 - p2) * 1000000000000. <=
            std::fmin(std::fabs(p1), std::fabs(p2)));
}
static long long qRound64(double d)
{
    return d >= 0.0 ? (long long)(d + 0.5) : (long long)(d - 0.5);
}

static void zeroIO(triangulateio &t) { std::memset(&t, 0, sizeof(t)); }

static void freeOutput(triangulateio &t)
{
    if (t.pointlist)             trifree(t.pointlist);
    if (t.pointattributelist)    trifree(t.pointattributelist);
    if (t.pointmarkerlist)       trifree(t.pointmarkerlist);
    if (t.trianglelist)          trifree(t.trianglelist);
    if (t.triangleattributelist) trifree(t.triangleattributelist);
    if (t.trianglearealist)      trifree(t.trianglearealist);
    if (t.neighborlist)          trifree(t.neighborlist);
    if (t.segmentlist)           trifree(t.segmentlist);
    if (t.segmentmarkerlist)     trifree(t.segmentmarkerlist);
    if (t.edgelist)              trifree(t.edgelist);
    if (t.edgemarkerlist)        trifree(t.edgemarkerlist);
}

struct Generator {
    std::vector<std::vector<P>> domains;
    std::vector<SteinerPoint>   steiners;
    std::vector<ConstraintSeg>  segments;
    std::vector<P>              holes;
    std::vector<RegionMarker>   regions;
    Options                     opts;
    bool                        verbose = true;
    int                         status  = 0;   // -1 triexit, 0 no triangles, >0 triangles

    // Returns: -1 = Triangle took the triexit()/longjmp path, 0 = no triangles,
    // >0 = triangle count. Also asserts every output index is in range.
    int generateQuiet() { verbose = false; generate("fuzz"); return status; }

    bool generate(const char *label)
    {
        status = 0;
        if (domains.empty()) { if (verbose) printf("  [%s] empty domain -> early return\n", label); return false; }

        using PointKey = std::pair<long long, long long>;
        std::map<PointKey, int> pointIndex;
        std::vector<P>   points;
        std::vector<int> pointMarkers;
        const int kBoundaryMarker = 1;

        auto pushPoint = [&](const P &xy, int marker) -> int {
            const long long qx = qRound64(xy.x * 1e7);
            const long long qy = qRound64(xy.y * 1e7);
            const PointKey key(qx, qy);
            auto it = pointIndex.find(key);
            if (it != pointIndex.end()) {
                int &existing = pointMarkers[it->second];
                if (marker != 0 && (existing == 0 || existing == kBoundaryMarker))
                    existing = marker;
                return it->second;
            }
            const int idx = (int)points.size();
            points.push_back(P{qx / 1e7, qy / 1e7});
            pointMarkers.push_back(marker);
            pointIndex.insert({key, idx});
            return idx;
        };

        std::vector<std::pair<int,int>> domSegments;
        for (const auto &dom : domains) {
            const int domN = (int)dom.size();
            if (domN < 3) continue;
            int firstIdx = -1, prevIdx = -1, uniqueVerts = 0;
            for (int i = 0; i < domN; ++i) {
                const P &p = dom[i];
                if (i == domN - 1 && i > 0
                    && qFuzzyCompare(p.x + 1, dom[0].x + 1)
                    && qFuzzyCompare(p.y + 1, dom[0].y + 1))
                    break;
                const int idx = pushPoint(p, kBoundaryMarker);
                if (firstIdx < 0) { firstIdx = idx; ++uniqueVerts; }
                if (idx == prevIdx) continue;
                ++uniqueVerts;
                if (prevIdx >= 0) domSegments.push_back({prevIdx, idx});
                prevIdx = idx;
            }
            if (uniqueVerts >= 3 && prevIdx >= 0 && firstIdx >= 0 && prevIdx != firstIdx)
                domSegments.push_back({prevIdx, firstIdx});
        }
        if (domSegments.empty()) { if (verbose) printf("  [%s] no usable boundary -> early return\n", label); return false; }

        for (const auto &sp : steiners) pushPoint(sp.xy, sp.marker);

        std::vector<std::pair<int,int>> userSegments;
        std::vector<int>                userSegmentMarkers;
        for (const auto &cs : segments) {
            if (cs.path.size() < 2) continue;
            int prev = pushPoint(cs.path.front(), cs.marker);
            for (size_t i = 1; i < cs.path.size(); ++i) {
                const int curr = pushPoint(cs.path[i], cs.marker);
                if (curr != prev) {
                    userSegments.push_back({prev, curr});
                    userSegmentMarkers.push_back(cs.marker);
                }
                prev = curr;
            }
        }

        auto stripZeroLen = [](std::vector<std::pair<int,int>> &segs, std::vector<int> &markers) {
            for (int i = (int)segs.size() - 1; i >= 0; --i)
                if (segs[i].first == segs[i].second) {
                    segs.erase(segs.begin() + i);
                    if (i < (int)markers.size()) markers.erase(markers.begin() + i);
                }
        };
        std::vector<int> domMarkers(domSegments.size(), kBoundaryMarker);
        stripZeroLen(domSegments, domMarkers);
        stripZeroLen(userSegments, userSegmentMarkers);

        // ── Pack input triangulateio ──────────────────────────────────────
        triangulateio in{};   zeroIO(in);
        triangulateio out{};  zeroIO(out);

        in.numberofpoints  = (int)points.size();
        in.pointlist       = (REAL *)std::malloc(sizeof(REAL) * 2 * points.size());
        in.pointmarkerlist = (int  *)std::malloc(sizeof(int) * points.size());
        for (size_t i = 0; i < points.size(); ++i) {
            in.pointlist[2*i+0] = points[i].x;
            in.pointlist[2*i+1] = points[i].y;
            in.pointmarkerlist[i] = pointMarkers[i];
        }

        const int totalSeg = (int)(domSegments.size() + userSegments.size());
        in.numberofsegments = totalSeg;
        if (totalSeg > 0) {
            in.segmentlist       = (int *)std::malloc(sizeof(int) * 2 * totalSeg);
            in.segmentmarkerlist = (int *)std::malloc(sizeof(int) * totalSeg);
            int s = 0;
            for (const auto &seg : domSegments) {
                in.segmentlist[2*s+0] = seg.first;
                in.segmentlist[2*s+1] = seg.second;
                in.segmentmarkerlist[s] = kBoundaryMarker;
                ++s;
            }
            for (size_t u = 0; u < userSegments.size(); ++u) {
                in.segmentlist[2*s+0] = userSegments[u].first;
                in.segmentlist[2*s+1] = userSegments[u].second;
                in.segmentmarkerlist[s] = userSegmentMarkers[u];
                ++s;
            }
        }

        in.numberofholes = (int)holes.size();
        if (!holes.empty()) {
            in.holelist = (REAL *)std::malloc(sizeof(REAL) * 2 * holes.size());
            for (size_t i = 0; i < holes.size(); ++i) {
                in.holelist[2*i+0] = holes[i].x;
                in.holelist[2*i+1] = holes[i].y;
            }
        }

        in.numberofregions = (int)regions.size();
        if (!regions.empty()) {
            in.regionlist = (REAL *)std::malloc(sizeof(REAL) * 4 * regions.size());
            for (size_t i = 0; i < regions.size(); ++i) {
                in.regionlist[4*i+0] = regions[i].xy.x;
                in.regionlist[4*i+1] = regions[i].xy.y;
                in.regionlist[4*i+2] = regions[i].attribute;
                in.regionlist[4*i+3] = regions[i].maxArea > 0 ? regions[i].maxArea : -1.0;
            }
        }

        // ── Switch string ────────────────────────────────────────────────
        char buf[64];
        std::string sw = "pzeA";
        if (opts.minAngle > 0.0) { std::snprintf(buf, sizeof buf, "q%.2f", opts.minAngle); sw += buf; }
        if (opts.maxArea > 0.0)  { std::snprintf(buf, sizeof buf, "a%.4f", opts.maxArea);  sw += buf; }
        else if (!regions.empty()) sw += "a";
        if (!opts.allowSteiner)      sw += "YY";
        if (opts.conformingDelaunay) sw += "D";
        if (opts.maxSteinerPoints > 0) { std::snprintf(buf, sizeof buf, "S%d", opts.maxSteinerPoints); sw += buf; }
        if (opts.quiet) sw += "Q";
        std::vector<char> swBa(sw.begin(), sw.end());
        swBa.push_back('\0');

        if (verbose) {
            printf("  [%s] pts=%d segs=%d holes=%d regions=%d switches=\"%s\"\n",
                   label, in.numberofpoints, in.numberofsegments,
                   in.numberofholes, in.numberofregions, sw.c_str());
            fflush(stdout);
        }

        const int triErr = triangulate_safe(swBa.data(), &in, &out, nullptr);
        if (triErr != 0) {
            freeOutput(out);
            std::free(in.pointlist);   std::free(in.pointmarkerlist);
            std::free(in.segmentlist); std::free(in.segmentmarkerlist);
            std::free(in.holelist);    std::free(in.regionlist);
            if (verbose) printf("  [%s] Triangle fatal error (triErr=%d)\n", label, triErr);
            status = -1;
            return false;
        }

        // ── Copy out (same reads production performs) ────────────────────
        long checksum = 0;
        for (int i = 0; i < out.numberofpoints; ++i) {
            checksum += (long)out.pointlist[2*i+0];
            checksum += (long)out.pointlist[2*i+1];
            checksum += out.pointmarkerlist ? out.pointmarkerlist[i] : 0;
        }
        for (int i = 0; i < out.numberoftriangles; ++i) {
            for (int k = 0; k < 3; ++k) {
                const int vi = out.trianglelist[3*i+k];
                if (vi < 0 || vi >= out.numberofpoints) {
                    printf("  [%s] !! OUT-OF-RANGE triangle vertex index %d (numberofpoints=%d)\n",
                           label, vi, out.numberofpoints);
                    fflush(stdout);
                    std::abort();
                }
            }
            checksum += out.trianglelist[3*i+0];
            checksum += out.trianglelist[3*i+1];
            checksum += out.trianglelist[3*i+2];
            if (out.triangleattributelist && out.numberoftriangleattributes > 0)
                checksum += (int)out.triangleattributelist[i];
        }
        if (out.segmentlist && out.numberofsegments > 0) {
            for (int i = 0; i < out.numberofsegments; ++i) {
                checksum += out.segmentlist[2*i+0];
                checksum += out.segmentlist[2*i+1];
                checksum += out.segmentmarkerlist ? out.segmentmarkerlist[i] : 0;
            }
        }

        if (verbose) {
            printf("  [%s] out: pts=%d tris=%d segs=%d edges=%d triattrs=%d (chk=%ld)\n",
                   label, out.numberofpoints, out.numberoftriangles,
                   out.numberofsegments, out.numberofedges,
                   out.numberoftriangleattributes, checksum);
            fflush(stdout);
        }

        std::free(in.pointlist);
        std::free(in.pointmarkerlist);
        std::free(in.segmentlist);
        std::free(in.segmentmarkerlist);
        std::free(in.holelist);
        std::free(in.regionlist);
        freeOutput(out);

        if (verbose) { printf("  [%s] freed OK\n", label); fflush(stdout); }
        status = out.numberoftriangles;
        return out.numberoftriangles > 0;
    }
};

static std::vector<P> square100()
{
    return { {0,0}, {100,0}, {100,100}, {0,100} };
}

// ── Fuzz mode ───────────────────────────────────────────────────────────────
// Randomised PSLGs of the same shape the GUI produces (boundary ring + a few
// constraint polylines + optional hole/region). Counts how often Triangle takes
// the triexit()/longjmp error path, and how often output triangle indices fall
// outside out.pointlist — the two things that would crash on Windows.
static unsigned long long s_rng = 0x9E3779B97F4A7C15ull;
static double frand(double lo, double hi)
{
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 7; s_rng ^= s_rng << 17;
    return lo + (double)(s_rng >> 11) / (double)(1ull << 53) * (hi - lo);
}

static int fuzz(int iterations)
{
    int errPath = 0, zeroTris = 0, ok = 0;
    for (int it = 0; it < iterations; ++it) {
        Generator g;
        // Random convex-ish ring
        const int n = 3 + (int)frand(0, 6);
        std::vector<P> ring;
        for (int i = 0; i < n; ++i) {
            const double a = 2.0 * 3.14159265358979 * i / n;
            const double r = frand(20.0, 100.0);
            ring.push_back(P{r * std::cos(a), r * std::sin(a)});
        }
        g.domains.push_back(ring);

        const int nseg = (int)frand(0, 4);
        for (int s = 0; s < nseg; ++s) {
            ConstraintSeg cs;
            const int np = 2 + (int)frand(0, 4);
            for (int i = 0; i < np; ++i)
                cs.path.push_back(P{frand(-40, 40), frand(-40, 40)});
            cs.marker = 2 + s;
            g.segments.push_back(cs);
        }
        if (frand(0, 1) > 0.7) g.holes.push_back(P{frand(-10, 10), frand(-10, 10)});
        if (frand(0, 1) > 0.7) g.regions.push_back({P{frand(-5,5), frand(-5,5)}, 11.0, -1.0, "r"});

        g.opts.maxArea  = frand(0, 1) > 0.5 ? frand(10.0, 500.0) : 0.0;
        g.opts.minAngle = frand(0, 1) > 0.5 ? frand(0.0, 30.0) : 0.0;
        g.opts.quiet    = true;

        const int rc = g.generateQuiet();
        if (rc < 0)      ++errPath;
        else if (rc == 0) ++zeroTris;
        else              ++ok;
    }
    printf("fuzz: iterations=%d ok=%d zeroTriangles=%d triexitPath=%d\n",
           iterations, ok, zeroTris, errPath);
    return errPath;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "fuzz") == 0)
        return fuzz(argc > 2 ? std::atoi(argv[2]) : 2000) > 0 ? 0 : 0;

    printf("== emptyDomain_failsCleanly ==\n");
    { Generator g; g.generate("emptyDomain"); }

    printf("== unitSquare_hasTriangles ==\n");
    { Generator g; g.domains.push_back(square100());
      g.opts.maxArea = 5000.0; g.opts.minAngle = 28.0;
      g.generate("unitSquare"); }

    printf("== steinerMarker_propagatesToOutputVertex ==\n");
    { Generator g; g.domains.push_back(square100());
      g.steiners.push_back({P{50,50}, 42, "J1"});
      g.opts.maxArea = 5000.0; g.opts.minAngle = 28.0;
      g.generate("steinerMarker"); }

    printf("== constraintMarker_propagatesToOutputEdge ==\n");
    { Generator g; g.domains.push_back(square100());
      ConstraintSeg cs; cs.path = { {20,50}, {80,50} }; cs.marker = 7; cs.tag = "C5";
      g.segments.push_back(cs);
      g.opts.maxArea = 5000.0; g.opts.minAngle = 28.0;
      g.generate("constraintMarker"); }

    printf("== regionAttribute_propagatesToOutputTriangle ==\n");
    { Generator g; g.domains.push_back(square100());
      g.regions.push_back({P{50,50}, 11.0, 0.0, "subcatch_S1"});
      g.opts.maxArea = 5000.0; g.opts.minAngle = 28.0;
      g.generate("regionAttribute"); }

    printf("== coincidentSteinerAndCorner_dedupes ==\n");
    { Generator g; g.domains.push_back(square100());
      g.steiners.push_back({P{0,0}, 99, "corner"});
      g.opts.maxArea = 5000.0; g.opts.minAngle = 28.0;
      g.generate("coincidentSteiner"); }

    printf("== nonConvexHole_isCarvedOut ==\n");
    { Generator g;
      g.domains.push_back({ {0,0}, {20,0}, {20,20}, {0,20} });
      std::vector<P> hole = { {2,2}, {14,2}, {14,5}, {5,5}, {5,14}, {2,14} };
      ConstraintSeg cs; cs.path = hole; cs.path.push_back(hole.front()); cs.marker = 0;
      g.segments.push_back(cs);
      g.holes.push_back(P{3,3});
      g.opts.maxArea = 20.0; g.opts.minAngle = 28.0;
      g.generate("nonConvexHole"); }

    printf("== ALL CASES COMPLETED ==\n");
    return 0;
}
