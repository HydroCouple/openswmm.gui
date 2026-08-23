/*!
 * \file   meshprofileinterp.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Water-surface bridging for the 2D longitudinal profile.
 *
 *         The mesh profile sampler produces a continuous, barycentric-
 *         interpolated sample series, but partially-wet / dry saddle cells
 *         break the painted water surface into disconnected runs. This helper
 *         turns a raw per-sample surface into a "paint top" series that:
 *
 *           (a) renders genuinely-wet-but-very-shallow films (depth just under
 *               the layer's hydraulic dry threshold) instead of dropping them;
 *           (b) bridges dry gaps that lie between two wet runs with a smooth,
 *               no-upstream-flow-constrained water surface.
 *
 *         Bridging is restricted to TRUE NO-DATA gaps: with the free-surface
 *         extrapolation fix a shoreline tapers to its sub-cell bed intercept
 *         and is no longer a gap, so a gap whose samples all carry a valid η
 *         (Sample::cellHasSurface) is genuinely dry ground — two pools
 *         separated by a dry crest stay split, each flat at its own level,
 *         instead of being joined by a chainage-linear ramp. Only a gap
 *         containing at least one no-surface sample (a cell with no valid η
 *         anywhere in its vertex stars) may still be bridged.
 *
 *         Direction signal: the painter carries no per-sample velocity, so the
 *         most robust available signal is the two bracketing wet-surface
 *         elevations themselves. Linear interpolation in chainage between them
 *         is monotonic, so the bridged surface never rises above the upstream
 *         (higher) end — water cannot pool higher downstream than its source.
 *         Where the bed rises above the interpolated surface (a dry crest) the
 *         sample stays dry, splitting the gap, so water never flows "uphill"
 *         over a hill. An off-mesh (NaN ground) sample inside a gap blocks
 *         bridging across it, preserving a genuine off-mesh gap.
 *
 *         Header-only so the painter and the unit test share one implementation.
 */

#ifndef MESH_PROFILE_INTERP_H
#define MESH_PROFILE_INTERP_H

#include "plot/meshprofilesampler.h"

#include <QVector>

#include <cmath>
#include <functional>
#include <limits>

namespace MeshProfileInterp
{

/*! Paint floor (m): a sample whose water surface sits more than this above the
 *  bed renders as water. Deliberately far below the layer's hydraulic
 *  dryDepth() (~1e-4 m) so genuinely-wet thin films still draw; truly
 *  zero/negative depth (surface at/under the bed) still reads as dry. */
inline constexpr double kFilm = 1e-6;

/*!
 * \brief Compute the per-sample water-surface elevation to paint for one
 *        wet-band pass, with shallow-film rendering and dry-gap bridging.
 * \param s        Ordered profile samples (chainage / ground / depths).
 * \param topElev  Maps a sample to its raw water-surface elevation
 *                 (e.g. ground + depthNow, or ground + maxDepth).
 * \return Parallel array of "paint top" elevations; NaN where the sample is
 *         dry (off-mesh, at/under the bed, or an unbridged crest).
 */
inline QVector<double> bridgedTops(
    const QVector<MeshProfileSampler::Sample> &s,
    const std::function<double(const MeshProfileSampler::Sample &)> &topElev)
{
    const int n = s.size();
    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    QVector<double> top(n, kNaN);

    // 1) Raw per-sample surface — films included via kFilm.
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(s[i].ground)) continue;
        const double t = topElev(s[i]);
        if (t > s[i].ground + kFilm) top[i] = t;
    }

    // 2) Bridge dry gaps that lie between two wet runs.
    int i = 0;
    while (i < n) {
        if (std::isnan(top[i])) { ++i; continue; }

        // End of the current wet run.
        int runEnd = i;
        while (runEnd + 1 < n && !std::isnan(top[runEnd + 1])) ++runEnd;

        // Walk the gap to the next wet sample. An off-mesh (NaN ground) sample
        // blocks the bridge — a genuine mesh hole must stay a true gap. A gap
        // whose samples ALL carry a valid free surface (cellHasSurface) is
        // genuinely dry ground, not missing data — never bridge it.
        const int gapStart = runEnd + 1;
        int next = gapStart;
        bool blocked = false;
        bool sawNoData = false;
        while (next < n && std::isnan(top[next])) {
            if (!std::isfinite(s[next].ground)) { blocked = true; break; }
            if (!s[next].cellHasSurface) sawNoData = true;
            ++next;
        }

        if (!blocked && sawNoData && next < n && gapStart < next) {
            const double cL = s[runEnd].chainage, wseL = top[runEnd];
            const double cR = s[next].chainage,    wseR = top[next];
            const double dC = cR - cL;
            const double wseUp = std::max(wseL, wseR);   // upstream (higher) level
            for (int k = gapStart; k < next; ++k) {
                if (!std::isfinite(s[k].ground)) continue;   // safety
                double wse = (dC > 1e-12)
                    ? wseL + (s[k].chainage - cL) / dC * (wseR - wseL)
                    : wseL;
                if (wse > wseUp) wse = wseUp;                // no uphill water
                if (wse > s[k].ground + kFilm)               // above the bed → wet
                    top[k] = wse;
                // else: dry crest — leave NaN so the run splits at the hill.
            }
        }

        i = blocked ? (runEnd + 1) : std::max(next, runEnd + 1);
    }

    return top;
}

/*!
 * \brief Shoreline intercept just outside one end of a wet run — the exact
 *        chainage where the (wet-side-extrapolated) water surface crosses the
 *        piecewise-linear ground between the run's boundary sample and its
 *        adjacent dry sample.
 *
 *        CellSurfaceInterp tapers the depth FIELD to the sub-cell waterline,
 *        but the painted band could only end at the last sample that happened
 *        to be wet — up to one resample step short of the true shoreline, with
 *        a vertical cliff of height (WSE − ground). This helper closes that
 *        sub-sample gap at paint time so the band tapers to a point on the
 *        ground line (premature-truncation fix).
 *
 *        The surface is extended from the run's two boundary-most wet samples
 *        (flat for a single-sample run) and intersected with the ground
 *        segment toward the dry neighbour. An intercept exists only when the
 *        ground actually rises through the surface within that segment — a dry
 *        neighbour whose ground stays below the surface (a no-data
 *        termination, e.g. the split flank of an unbridged pool over a low
 *        bench) keeps its hard edge, as does an off-mesh (NaN ground)
 *        neighbour and a run ending at the data edge.
 *
 * \param s         Ordered profile samples (chainage / ground).
 * \param top       Paint-top series from bridgedTops(), parallel to \p s.
 * \param runFirst  Index of the run's first wet (non-NaN top) sample.
 * \param runLast   Index of the run's last wet sample.
 * \param trailing  true → intercept past runLast; false → before runFirst.
 * \param outChainage,outElev  The intercept; written only on success. The
 *        elevation lies on the drawn ground segment by construction, so both
 *        the WSE polyline and the fill close exactly onto the ground line.
 * \return true when a physically meaningful intercept exists.
 */
inline bool shorelineIntercept(const QVector<MeshProfileSampler::Sample> &s,
                               const QVector<double> &top,
                               int runFirst, int runLast, bool trailing,
                               double *outChainage, double *outElev)
{
    const int n = s.size();
    if (runFirst < 0 || runLast >= n || runFirst > runLast || top.size() != n)
        return false;
    const int i = trailing ? runLast : runFirst;          // boundary wet sample
    const int j = trailing ? runLast - 1 : runFirst + 1;  // inner wet neighbour
    const int k = trailing ? runLast + 1 : runFirst - 1;  // adjacent dry sample
    if (k < 0 || k >= n) return false;                    // run ends at the data edge
    if (!std::isfinite(s[k].ground)) return false;        // off-mesh — keep the hard edge
    if (!std::isnan(top[k])) return false;                // neighbour not dry (defensive)
    if (std::isnan(top[i]) || !std::isfinite(s[i].ground)) return false;

    const double ci = s[i].chainage, ck = s[k].chainage;
    const double dSeg = ck - ci;                          // signed, toward the dry side
    if (std::abs(dSeg) < 1e-12) return false;

    // Wet-side surface slope from the two boundary-most wet samples; a
    // single-sample run extends flat.
    double mTop = 0.0;
    if (j >= runFirst && j <= runLast) {
        const double dcw = ci - s[j].chainage;
        if (std::abs(dcw) > 1e-12 && !std::isnan(top[j]))
            mTop = (top[i] - top[j]) / dcw;
    }

    // Along t ∈ [0,1] from sample i to sample k:
    //   surface(t) = top_i + mTop·dSeg·t
    //   ground(t)  = g_i + (g_k − g_i)·t
    // They cross where surface == ground.
    const double gi = s[i].ground, gk = s[k].ground;
    const double denom = (gk - gi) - mTop * dSeg;
    if (denom <= 1e-12) return false;      // ground never rises through the surface
    const double t = (top[i] - gi) / denom;
    if (t <= 0.0 || t > 1.0) return false; // no crossing inside the segment

    if (outChainage) *outChainage = ci + t * dSeg;
    if (outElev)     *outElev     = gi + t * (gk - gi);
    return true;
}

} // namespace MeshProfileInterp

#endif // MESH_PROFILE_INTERP_H
