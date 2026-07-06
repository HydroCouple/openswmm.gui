/*!
 * \file   qsg2dlodpolicy.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 3 — deterministic level-of-detail policy for the 2D
 * mesh / results QSG renderers.
 *
 * decide() maps (viewport, extent, mesh density, sublayer intents) to a
 * bucket plus per-pass visibility decisions:
 *
 *   Far  — aggregate/overview-style fill only; no dense wireframe, vertex
 *          markers, contour labels, or dense per-cell velocity glyphs.
 *   Mid  — exact fill; contours/vectors allowed with caps; edges only when
 *          cells are big enough for a wireframe to read.
 *   Near — exact everything the user asked for.
 *
 * Selected-element overlays are ALWAYS drawn when a selection exists,
 * regardless of bucket, so hidden dense passes never hide the selection.
 *
 * The decision also carries a contentKey() — bucket plus a quantised zoom
 * octave — that the renderers use as the rebuild key: zooming re-creates
 * content only when the key changes (or the view leaves the coverage rect
 * content was built for), never on every wheel tick. Both the bucket and
 * the octave apply hysteresis against the previous decision so sub-percent
 * zoom jitter can never oscillate the key.
 *
 * Pure Qt-Core header + .cpp — no Qt Quick / Gui dependency; unit-tested
 * in tests/unit/test_qsg2d_lodpolicy.cpp.
 */
#ifndef OPENSWMM_RENDER_QSG2DLODPOLICY_H
#define OPENSWMM_RENDER_QSG2DLODPOLICY_H

#include <QtGlobal>

#include <limits>

namespace OpenSWMM::Render
{

struct Qsg2DLodInputs
{
    // ── View ────────────────────────────────────────────────────────────
    double viewportWidthPx  = 0.0;  ///< logical px
    double viewportHeightPx = 0.0;
    double extentWidth      = 0.0;  ///< map units
    double extentHeight     = 0.0;

    // ── Mesh density ────────────────────────────────────────────────────
    qint64 cellCount    = 0;    ///< total mesh cells
    double meshBBoxArea = 0.0;  ///< scene-space area of the mesh bbox
    /*! Fraction of the mesh bbox intersecting the view, in [0,1]. The
     *  caller computes this from the two rects; used to estimate the
     *  visible cell count for glyph caps. */
    double visibleFraction = 1.0;

    // ── Sublayer intents (user-facing visibility toggles) ───────────────
    bool wantFill          = true;
    bool wantEdges         = false;
    bool wantVertexMarkers = false;
    bool wantContours      = false;
    bool wantContourLabels = false;
    bool wantVelocity      = false;
    bool haveSelection     = false;

    // ── Hysteresis anchors (previous decision; defaults = none) ─────────
    int previousBucket   = -1;
    int previousZoomStep = std::numeric_limits<int>::min();
};

struct Qsg2DLodDecision
{
    enum Bucket { Far = 0, Mid = 1, Near = 2 };

    Bucket bucket        = Near;
    int    zoomStep      = 0;      ///< quantised log2(px per map unit)
    double avgCellAreaPx = 0.0;    ///< diagnostic: mean projected cell area

    // Per-pass verdicts — already combined with the user intents, i.e.
    // false either because the user turned the pass off or because the
    // bucket suppresses it.
    bool drawFill             = true;
    bool drawEdges            = false;
    bool drawVertexMarkers    = false;
    bool drawContours         = false;   ///< isolines
    bool drawContourLabels    = false;
    bool drawVelocityVectors  = false;
    bool drawSelectedOverlays = false;   ///< true whenever a selection exists

    /*! Far-bucket fill strategy: render the aggregate/overview fill (when
     *  the layer has one) instead of exact per-cell geometry. */
    bool useAggregateFill = false;

    /*! Exact marching-triangles contour BANDS allowed (Mid/Near). At Far
     *  the renderers fall back to flat per-cell classification, which is
     *  visually identical once cells are subpixel. */
    bool exactContourBands = false;

    /*! Dense one-glyph-per-cell velocity mode allowed (Near + under cap).
     *  When false, screen-space grid sampling must be used instead. */
    bool   denseVelocityAllowed = false;
    qint64 maxVelocityGlyphs    = 0;
    int    maxContourLabels     = 0;

    /*! Content rebuild key: changes only when the bucket or the zoom
     *  octave changes. */
    [[nodiscard]] quint64 contentKey() const
    {
        return (quint64(quint32(zoomStep)) << 2) | quint64(bucket);
    }
};

class Qsg2DLodPolicy
{
public:
    // Bucket thresholds on the mean projected cell area (px²). A cell
    // whose area is below kFarMaxCellAreaPx spans < ~3 px — subpixel-ish,
    // wireframe/markers are pure noise. Above kNearMinCellAreaPx a cell
    // spans ≥ ~13 px and exact detail reads clearly.
    static constexpr double kFarMaxCellAreaPx   = 8.0;
    static constexpr double kNearMinCellAreaPx  = 160.0;
    /*! Edges need at least this much projected cell area to read as a
     *  wireframe rather than a dark wash (Mid gate). */
    static constexpr double kEdgeMinCellAreaPx  = 32.0;
    /*! Vertex markers only make sense when cells are clearly resolved. */
    static constexpr double kMarkerMinCellAreaPx = 200.0;
    /*! Hysteresis band around the bucket thresholds (fraction). */
    static constexpr double kBucketHysteresis   = 0.25;
    /*! Hysteresis around zoom-octave boundaries (in octaves). */
    static constexpr double kZoomStepHysteresis = 0.08;
    /*! Dense per-cell velocity glyphs allowed only below this many
     *  estimated visible cells. */
    static constexpr qint64 kDenseVelocityCellCap = 20000;
    static constexpr int    kMidMaxContourLabels  = 40;
    static constexpr int    kNearMaxContourLabels = 400;

    [[nodiscard]] static Qsg2DLodDecision decide(const Qsg2DLodInputs &in);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_QSG2DLODPOLICY_H
