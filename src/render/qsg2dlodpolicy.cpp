/*!
 * \file   qsg2dlodpolicy.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 3 — see qsg2dlodpolicy.h.
 */
#include "render/qsg2dlodpolicy.h"

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

namespace {

/*! Raw (hysteresis-free) bucket from mean projected cell area. */
Qsg2DLodDecision::Bucket rawBucket(double avgCellAreaPx, double farMaxCellAreaPx)
{
    if (avgCellAreaPx < farMaxCellAreaPx)
        return Qsg2DLodDecision::Far;
    if (avgCellAreaPx >= Qsg2DLodPolicy::kNearMinCellAreaPx)
        return Qsg2DLodDecision::Near;
    return Qsg2DLodDecision::Mid;
}

/*! Bucket with hysteresis: keep the previous bucket while the metric sits
 *  within ±kBucketHysteresis of the threshold it would have to cross. */
Qsg2DLodDecision::Bucket stableBucket(double avgCellAreaPx, int previous,
                                      double farMaxCellAreaPx)
{
    const auto raw = rawBucket(avgCellAreaPx, farMaxCellAreaPx);
    if (previous < Qsg2DLodDecision::Far || previous > Qsg2DLodDecision::Near
        || previous == int(raw))
        return raw;

    const double h = Qsg2DLodPolicy::kBucketHysteresis;
    auto withinBand = [&](double threshold) {
        return avgCellAreaPx >= threshold * (1.0 - h)
            && avgCellAreaPx <= threshold * (1.0 + h);
    };

    // Only adjacent-bucket flips can be damped; a two-bucket jump is a
    // genuine zoom change and always takes effect.
    const int prev = previous;
    const int cur  = int(raw);
    if (std::abs(cur - prev) == 1) {
        const double threshold = (std::min(cur, prev) == Qsg2DLodDecision::Far)
                                     ? farMaxCellAreaPx
                                     : Qsg2DLodPolicy::kNearMinCellAreaPx;
        if (withinBand(threshold))
            return Qsg2DLodDecision::Bucket(prev);
    }
    return raw;
}

/*! Zoom octave with hysteresis: keep the previous step while log2(ppu)
 *  stays inside [prev - hys, prev + 1 + hys). */
int stableZoomStep(double pxPerUnit, int previous)
{
    const double raw = std::log2(std::max(pxPerUnit, 1e-30));
    if (previous != std::numeric_limits<int>::min()) {
        const double h = Qsg2DLodPolicy::kZoomStepHysteresis;
        if (raw >= double(previous) - h && raw < double(previous) + 1.0 + h)
            return previous;
    }
    return int(std::floor(raw));
}

} // namespace

Qsg2DLodDecision Qsg2DLodPolicy::decide(const Qsg2DLodInputs &in)
{
    Qsg2DLodDecision d;

    const bool viewValid = in.viewportWidthPx > 0.0 && in.viewportHeightPx > 0.0
                        && in.extentWidth    > 0.0 && in.extentHeight     > 0.0;
    if (!viewValid) {
        // Degenerate view — draw nothing dense, but never lose a selection.
        d.bucket               = Qsg2DLodDecision::Far;
        d.zoomStep             = 0;
        d.drawFill             = in.wantFill;
        d.useAggregateFill     = true;
        d.drawSelectedOverlays = in.haveSelection;
        return d;
    }

    const double pxPerUnit = in.viewportWidthPx / in.extentWidth;
    const double cellAreaScene =
        (in.cellCount > 0 && in.meshBBoxArea > 0.0)
            ? in.meshBBoxArea / double(in.cellCount)
            : 0.0;
    d.avgCellAreaPx = cellAreaScene * pxPerUnit * pxPerUnit;

    // A mesh with unknown density (no cells / no bbox yet) renders Near —
    // there is nothing dense to protect against.
    d.bucket = (cellAreaScene > 0.0)
                   ? stableBucket(d.avgCellAreaPx, in.previousBucket,
                                  in.farMaxCellAreaPx)
                   : Qsg2DLodDecision::Near;
    d.zoomStep = stableZoomStep(pxPerUnit, in.previousZoomStep);

    const double visFrac = std::clamp(in.visibleFraction, 0.0, 1.0);
    const qint64 visibleCellsEstimate =
        qint64(std::llround(double(in.cellCount) * visFrac));

    switch (d.bucket) {
    case Qsg2DLodDecision::Far:
        d.drawFill             = in.wantFill;
        d.useAggregateFill     = true;
        d.drawEdges            = false;
        d.drawVertexMarkers    = false;
        d.drawContours         = false;
        d.drawContourLabels    = false;
        d.exactContourBands    = false;
        // Screen-space SAMPLED glyphs stay bounded at any zoom, so the
        // velocity overlay survives Far; only the one-glyph-per-cell dense
        // mode is banned (million-scale noise).
        d.drawVelocityVectors  = in.wantVelocity;
        d.denseVelocityAllowed = false;
        d.maxVelocityGlyphs    = kDenseVelocityCellCap;
        d.maxContourLabels     = 0;
        break;

    case Qsg2DLodDecision::Mid:
        d.drawFill             = in.wantFill;
        d.useAggregateFill     = false;
        d.drawEdges            = in.wantEdges
                                 && d.avgCellAreaPx >= in.edgeMinCellAreaPx;
        d.drawVertexMarkers    = false;
        d.drawContours         = in.wantContours;
        d.drawContourLabels    = in.wantContourLabels;
        d.exactContourBands    = true;
        d.drawVelocityVectors  = in.wantVelocity;
        d.denseVelocityAllowed = false;   // grid sampling only at Mid
        d.maxVelocityGlyphs    = kDenseVelocityCellCap;
        d.maxContourLabels     = kMidMaxContourLabels;
        break;

    case Qsg2DLodDecision::Near:
        d.drawFill             = in.wantFill;
        d.useAggregateFill     = false;
        d.drawEdges            = in.wantEdges
                                 && d.avgCellAreaPx >= in.edgeMinCellAreaPx;
        d.drawVertexMarkers    = in.wantVertexMarkers
                                 && d.avgCellAreaPx >= in.markerMinCellAreaPx;
        d.drawContours         = in.wantContours;
        d.drawContourLabels    = in.wantContourLabels;
        d.exactContourBands    = true;
        d.drawVelocityVectors  = in.wantVelocity;
        d.denseVelocityAllowed = visibleCellsEstimate <= kDenseVelocityCellCap;
        d.maxVelocityGlyphs    = kDenseVelocityCellCap;
        d.maxContourLabels     = kNearMaxContourLabels;
        break;
    }

    // Selection overlays are exempt from every dense-pass gate.
    d.drawSelectedOverlays = in.haveSelection;

    return d;
}

} // namespace OpenSWMM::Render
