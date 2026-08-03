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

} // namespace MeshProfileInterp

#endif // MESH_PROFILE_INTERP_H
