/*!
 * \file   profilebuilder.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "plot/profilebuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ProfileBuilder
{

namespace
{

constexpr double kPosInf =  std::numeric_limits<double>::infinity();
constexpr double kNegInf = -std::numeric_limits<double>::infinity();

inline double safeAbs(double v) { return std::fabs(v); }

// Pulls the velocity magnitude at a given path-link index and period from
// the source.  Returns 0.0 (treated as "missing → zero velocity head") when
// any index is out of range.
double linkVelocityAt(const SourceSeries &src, int pathLinkIdx, int period)
{
    if (pathLinkIdx < 0 || pathLinkIdx >= src.linkVelocity.size()) return 0.0;
    const auto &row = src.linkVelocity[pathLinkIdx];
    if (period < 0 || period >= row.size()) return 0.0;
    return safeAbs(static_cast<double>(row[period]));
}

} // namespace

// --------------------------------------------------------------------------
// Static-topology utilities
// --------------------------------------------------------------------------

QVector<double> computeChainage(const QVector<LinkStatic> &links)
{
    QVector<double> chain;
    chain.reserve(links.size() + 1);
    chain.push_back(0.0);
    double accum = 0.0;
    for (const LinkStatic &l : links) {
        accum += l.length;
        chain.push_back(accum);
    }
    return chain;
}

double groundElev(const NodeStatic &n)
{
    // Rim = invert + maxDepth.  Terrain-based ground lines live on
    // PathStatic::terrainSamples and are applied by the renderer, not here.
    return n.invertElev + n.maxDepth;
}

double conduitInvertUpstream(const PathStatic &path, int linkIdx)
{
    if (linkIdx < 0 || linkIdx >= path.links.size())   return 0.0;
    if (linkIdx + 1 > path.nodes.size())               return 0.0;
    return path.nodes[linkIdx].invertElev + path.links[linkIdx].offset1;
}

double conduitInvertDownstream(const PathStatic &path, int linkIdx)
{
    if (linkIdx < 0 || linkIdx >= path.links.size())   return 0.0;
    if (linkIdx + 1 >= path.nodes.size())              return 0.0;
    return path.nodes[linkIdx + 1].invertElev + path.links[linkIdx].offset2;
}

// --------------------------------------------------------------------------
// Per-period helpers
// --------------------------------------------------------------------------

double velocityHead(const PathStatic &path,
                    const SourceSeries &src,
                    int nodeIdx,
                    int period,
                    double gravity)
{
    const int N = path.nodes.size();
    if (N < 2 || nodeIdx < 0 || nodeIdx >= N) return 0.0;
    if (gravity <= 0.0) return 0.0;

    const int upstreamLink   = nodeIdx - 1;          // valid for nodeIdx >= 1
    const int downstreamLink = nodeIdx;              // valid for nodeIdx <= N-2

    double v = 0.0;
    if (nodeIdx == 0) {
        v = linkVelocityAt(src, downstreamLink, period);
    } else if (nodeIdx == N - 1) {
        v = linkVelocityAt(src, upstreamLink, period);
    } else {
        const double vu = linkVelocityAt(src, upstreamLink, period);
        const double vd = linkVelocityAt(src, downstreamLink, period);
        v = 0.5 * (vu + vd);
    }
    return (v * v) / (2.0 * gravity);
}

double eglAtPeriod(const PathStatic &path,
                   const SourceSeries &src,
                   int nodeIdx,
                   int period,
                   double gravity)
{
    if (nodeIdx < 0 || nodeIdx >= src.nodeHead.size()) return 0.0;
    const auto &row = src.nodeHead[nodeIdx];
    if (period < 0 || period >= row.size()) return 0.0;
    // HGL = NodeHead (the engine's hydraulic grade output).
    const double hgl = static_cast<double>(row[period]);
    return hgl + velocityHead(path, src, nodeIdx, period, gravity);
}

// --------------------------------------------------------------------------
// Validation + full compute
// --------------------------------------------------------------------------

Diagnostic validate(const PathStatic &path, const SourceSeries &src)
{
    Diagnostic d;
    if (path.nodes.size() < 2) {
        d.error = QStringLiteral("Path must have at least 2 nodes.");
        return d;
    }
    if (path.links.size() != path.nodes.size() - 1) {
        d.error = QStringLiteral("Path link count (%1) must equal node count - 1 (%2).")
                  .arg(path.links.size()).arg(path.nodes.size() - 1);
        return d;
    }
    if (!path.chainage.isEmpty() && path.chainage.size() != path.nodes.size()) {
        d.error = QStringLiteral("Chainage array length must equal node count (or be empty).");
        return d;
    }
    if (src.periodCount < 0) {
        d.error = QStringLiteral("periodCount is negative.");
        return d;
    }
    if (src.periodCount > 0 && src.reportStepSec <= 0) {
        d.error = QStringLiteral("reportStepSec must be positive on a non-empty series.");
        return d;
    }
    if (src.nodeHead.size() != path.nodes.size()) {
        d.error = QStringLiteral("nodeHead first-dim (%1) must equal path node count (%2).")
                  .arg(src.nodeHead.size()).arg(path.nodes.size());
        return d;
    }
    if (!src.linkVelocity.isEmpty()
        && src.linkVelocity.size() != path.links.size()) {
        d.error = QStringLiteral(
            "linkVelocity first-dim (%1) must equal path link count (%2) when present.")
                  .arg(src.linkVelocity.size()).arg(path.links.size());
        return d;
    }
    return d;
}

SourceDerived compute(const PathStatic &path,
                      const SourceSeries &src,
                      double gravity)
{
    SourceDerived out;
    const Diagnostic d = validate(path, src);
    if (!d.error.isEmpty()) return out;

    const int N = path.nodes.size();
    const int P = src.periodCount;

    out.hglByPeriod.resize(N);
    out.eglByPeriod.resize(N);
    out.waterSurfaceByPeriod.resize(N);
    out.minHgl.fill(kPosInf, N);
    out.maxHgl.fill(kNegInf, N);
    out.minEgl.fill(kPosInf, N);
    out.maxEgl.fill(kNegInf, N);
    out.minWaterSurface.fill(kPosInf, N);
    out.maxWaterSurface.fill(kNegInf, N);

    for (int n = 0; n < N; ++n) {
        out.hglByPeriod[n].reserve(P);
        out.eglByPeriod[n].reserve(P);
        out.waterSurfaceByPeriod[n].reserve(P);
    }

    // nodeDepth is optional per the SourceSeries contract — present only when
    // the caller pre-fetched it (the in-tree ProfileSourceFetcher does).  Test
    // the per-node row size at use-time so a partial array still works.
    const bool haveDepthArray = !src.nodeDepth.isEmpty();

    for (int p = 0; p < P; ++p) {
        for (int n = 0; n < N; ++n) {
            // HGL = NodeHead from the .out file (the engine's hydraulic
            // grade line output).  When this node is absent from the
            // output file the row is empty — write NaN so the widget
            // skips that node/period instead of rendering a bogus line at
            // elevation 0.
            const auto &headRow = src.nodeHead[n];
            const bool  haveHgl = (p < headRow.size());
            const double hgl    = haveHgl
                                      ? static_cast<double>(headRow[p])
                                      : std::numeric_limits<double>::quiet_NaN();
            const double vh     = velocityHead(path, src, n, p, gravity);
            const double egl    = haveHgl ? (hgl + vh)
                                          : std::numeric_limits<double>::quiet_NaN();

            // Water surface = invert + depth.  Distinct from HGL when the
            // conduit is pressurized (HGL > rim → free-surface caps at rim).
            // Sized defensively in case nodeDepth has fewer rows than nodes.
            double ws = std::numeric_limits<double>::quiet_NaN();
            bool   haveWs = false;
            if (haveDepthArray && n < src.nodeDepth.size()) {
                const auto &depthRow = src.nodeDepth[n];
                if (p < depthRow.size()) {
                    ws = path.nodes[n].invertElev
                         + static_cast<double>(depthRow[p]);
                    haveWs = true;
                }
            }

            out.hglByPeriod[n].push_back(hgl);
            out.eglByPeriod[n].push_back(egl);
            out.waterSurfaceByPeriod[n].push_back(ws);

            if (haveHgl) {
                if (hgl < out.minHgl[n]) out.minHgl[n] = hgl;
                if (hgl > out.maxHgl[n]) out.maxHgl[n] = hgl;
                if (egl < out.minEgl[n]) out.minEgl[n] = egl;
                if (egl > out.maxEgl[n]) out.maxEgl[n] = egl;
            }
            if (haveWs) {
                if (ws < out.minWaterSurface[n]) out.minWaterSurface[n] = ws;
                if (ws > out.maxWaterSurface[n]) out.maxWaterSurface[n] = ws;
            }
        }
    }

    // Zero-period guard: leave min/max at +inf/-inf so callers can detect
    // "no envelope data" rather than show misleading defaults.
    if (P == 0) {
        out.minHgl.fill(0.0, N);
        out.maxHgl.fill(0.0, N);
        out.minEgl.fill(0.0, N);
        out.maxEgl.fill(0.0, N);
        out.minWaterSurface.fill(0.0, N);
        out.maxWaterSurface.fill(0.0, N);
    }

    return out;
}

} // namespace ProfileBuilder
