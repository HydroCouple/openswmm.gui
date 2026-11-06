/**
 * @file      swmmlayerqsgrenderer.cpp
 * @brief     Qt scene graph rendering of SWMM network layers.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 *
 * Phase B.RHI of docs/RENDERING_5M_PLAN.md — Qt Quick Scene Graph
 * renderer for SWMMModelLayer. Renders ALL vector layers (lines,
 * subcatchments, node glyphs, gages) via QSGGeometryNode, with
 * selection coloring per class. Native Metal / Vulkan / D3D11 via
 * QRhi underneath; no QPainter, no GL paint engine quirks.
 *
 * LABELS ARE INTENTIONALLY NOT DRAWN HERE. The label contract
 * (LAYER_STYLING_LABELING_PLAN_2026-08-16) is a hybrid: even when this
 * QSG overlay owns every geometry kind (qsgOwnsKind), text labels are
 * painted by the CPU pass in SWMMLayerItem::paint — screen-space via the
 * shared LabelPainter, deliberately NOT gated on qsgOwnsKind. A pure-QSG
 * glyph pipeline (atlas + text nodes) was evaluated and rejected: the CPU
 * overlay is correct, zoom-stable, and cheap relative to geometry upload.
 * If you add a text pass here, remove the CPU label block in
 * swmmlayeritem.cpp or labels will double-draw.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "map/swmmlayerqsgrenderer.h"

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "render/markershape.h"
#include "render/linkstroke.h"
#include "render/subcatchmentgeometry.h"
#include "render/qsgpremultiply.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QPainterPath>
#include <QPolygonF>
#include <QHash>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------
// Lightweight frame-time sampler — active only when SWMMVIS_RENDER_PERF is set.
// ---------------------------------------------------------------------------
namespace
{

using OpenSWMM::Render::premul;

constexpr int kReportInterval = 60;

struct PerfSampler {
    bool enabled = false;
    qint64 geomTotal = 0;
    int geomCount = 0;
    qint64 panTotal = 0;
    int panCount = 0;
    int frameCount = 0;

    /**
     * @brief Set shared sampler enabled according to PRESENCE of SWMMVIS_RENDER_PERF environment
     *   variable, including value0. Existing totals/counts retained.
     */
    void init() { enabled = qEnvironmentVariableIsSet("SWMMVIS_RENDER_PERF"); }

    /**
     * @param[in] ms Elapsed nonnegative frame CPU milliseconds supplied by caller.
     * @param[in] wasRebuild True accumulates rebuild bucket, false pan/matrix bucket.
     * @note Disabled no-op. Adds duration/count, reports/resets every60recorded frames.
     *   Process-static sampler aggregates renderer instances; caller serializes, no locking/GPU
     *   timer.
     */
    void record(qint64 ms, bool wasRebuild)
    {
        if (!enabled)
            return;
        if (wasRebuild) {
            geomTotal += ms;
            ++geomCount;
        } else {
            panTotal += ms;
            ++panCount;
        }
        if (++frameCount >= kReportInterval)
            report();
    }

    /**
     * @brief Log rebuild/pan average CPU ms (0for empty bucket), then zero all totals/counts/frame
     *   count.
     * @note Direct call logs even disabled; enabled flag retained. No renderer/node/model mutation.
     */
    void report()
    {
        const double ga = geomCount ? double(geomTotal) / geomCount : 0.0;
        const double pa = panCount ? double(panTotal) / panCount : 0.0;
        qDebug().noquote() << "[SWMMVis render]"
                           << "rebuild_frames=" << geomCount
                           << QString("avg_rebuild_ms=%1").arg(ga, 0, 'f', 1)
                           << "pan_frames=" << panCount
                           << QString("avg_pan_ms=%1").arg(pa, 0, 'f', 2);
        geomTotal = panTotal = 0;
        geomCount = panCount = frameCount = 0;
    }
};

/**
 * @return Borrowed process-static timing sampler; do not free, shared render callbacks must
 *   serialize mutable access.
 */
PerfSampler &sampler()
{
    static PerfSampler s;
    return s;
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

// Shape-agnostic glyph emitter.
//
// Emits a triangle fan (or triangle pair / quad) that fills the requested
// MarkerShape inside a (2 r) bounding box centred at (sx, sy). The
// pixel-fan approach keeps geometry uniform across shapes and avoids
// per-shape vertex-attribute juggling.
//
// VertexT is either QSGGeometry::Point2D (no colour) or
// QSGGeometry::ColoredPoint2D (per-vertex colour). The MakeVert lambda
// builds the right vertex type for the caller.
//
// Cross-cap arms (Plus, Cross, XCross) are emitted as two thin
// rectangles. Star is a 10-vertex two-triangle-fan (outer + inner
// vertices) — visually convincing for ~6 px markers without a custom
// shader. Pentagon/Hexagon/Arrow/HalfCircle are emitted as triangle fans
// from the centre.
//
// Circle segment count scales with the requested radius so small
// markers stay cheap and big markers stay round. A 24-segment fan at
// the default 8 px radius is indistinguishable from a smooth disk at
// typical zoom levels with MSAA active.
/**
 * @tparam VertexT Point2D or ColoredPoint2D-like output value.
 * @tparam MakeVert Callable returning VertexT for an XY position, invoked synchronously; exceptions
 *   propagate.
 * @param[in,out] out Append-only triangle list, existing entries retained.
 * @param[in] sx Glyph center X in output geometry frame.
 * @param[in] sy Glyph center Y in same frame.
 * @param[in] r Finite nonnegative glyph radius in same units, no local clamp.
 * @param[in] shape Supported canonical shape; unhandled enum emits no body.
 * @param[in] v Borrowed/moved callable mapping each emitted XY to output vertex.
 * @note Common shapes emit independent triangles; Circle fixed16fansegments, HalfCircle12, star
 *   alternating inner ratio0.382, cross/plus two rectangles. No rotation, view scaling, colour
 *   processing or validity/overflow check. Caller reserves/serializes destination; allocation
 *   failure may leave partial append.
 */
template <typename VertexT, typename MakeVert>
void appendMarkerShapeImpl(std::vector<VertexT> &out, float sx, float sy, float r,
                           OpenSWMM::Render::MarkerShape shape, MakeVert &&v)
{
    using Shape = OpenSWMM::Render::MarkerShape;
    constexpr float kTau = 6.28318530718f;

    auto fan = [&](int segments) {
        const float k = kTau / float(segments);
        for (int s = 0; s < segments; ++s) {
            const float a0 = k * float(s);
            const float a1 = k * float(s + 1);
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r * std::cos(a0), sy + r * std::sin(a0)));
            out.push_back(v(sx + r * std::cos(a1), sy + r * std::sin(a1)));
        }
    };

    auto quad = [&](float x0, float y0, float x1, float y1, float x2, float y2, float x3,
                    float y3) {
        out.push_back(v(x0, y0));
        out.push_back(v(x1, y1));
        out.push_back(v(x2, y2));
        out.push_back(v(x0, y0));
        out.push_back(v(x2, y2));
        out.push_back(v(x3, y3));
    };

    switch (shape) {
    case Shape::Circle: {
        // Fixed 16-segment fan. A previous attempt scaled segments by
        // `r` — but `r` is in scene units (after invView scaling), so
        // at full extent on large models it would clamp to 48 segments
        // per glyph, pushing the junction vertex buffer past 6 M verts
        // on West-Whiteland-scale models. That hit a per-buffer limit
        // on the OpenGL RHI backend and showed up as stray lines /
        // triangles between distant points (stale buffer tail being
        // rendered as DrawTriangles). 16 segments is plenty for any
        // reasonable on-screen size and bounds the per-glyph vertex
        // cost at 48 vertices — total fits in safe-range regardless of
        // node count.
        constexpr int segments = 16;
        fan(segments);
        break;
    }
    case Shape::Square:
        quad(sx - r, sy - r, sx + r, sy - r, sx + r, sy + r, sx - r, sy + r);
        break;
    case Shape::Triangle:
        // Canonical: right-pointing isoceles.
        out.push_back(v(sx + r, sy));
        out.push_back(v(sx - r, sy + r));
        out.push_back(v(sx - r, sy - r));
        break;
    case Shape::Diamond:
        out.push_back(v(sx, sy - r));
        out.push_back(v(sx + r, sy));
        out.push_back(v(sx, sy + r));
        out.push_back(v(sx, sy - r));
        out.push_back(v(sx, sy + r));
        out.push_back(v(sx - r, sy));
        break;
    case Shape::Star: {
        // Five-pointed star: triangle fan from centre to alternating
        // outer / inner vertices around the circle.
        constexpr int kPoints = 5;
        const float inner = r * 0.382f;     // golden-ratio-ish inset
        const float aStart = -kTau * 0.25f; // tip up
        for (int s = 0; s < kPoints * 2; ++s) {
            const float a0 = aStart + kTau * float(s) / float(kPoints * 2);
            const float a1 = aStart + kTau * float(s + 1) / float(kPoints * 2);
            const float r0 = (s % 2 == 0) ? r : inner;
            const float r1 = (s % 2 == 0) ? inner : r;
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r0 * std::cos(a0), sy + r0 * std::sin(a0)));
            out.push_back(v(sx + r1 * std::cos(a1), sy + r1 * std::sin(a1)));
        }
        break;
    }
    case Shape::Cross:
    case Shape::Plus: {
        const float w = (shape == Shape::Plus) ? r * 0.45f : r * 0.25f;
        quad(sx - r, sy - w, sx + r, sy - w, sx + r, sy + w, sx - r, sy + w);
        quad(sx - w, sy - r, sx + w, sy - r, sx + w, sy + r, sx - w, sy + r);
        break;
    }
    case Shape::XCross: {
        // Two rotated bars (× shape). Half-width and half-length of
        // each bar in local coords, then rotated into world coords.
        const float w = r * 0.25f;
        const float c = 0.70710678f; // cos/sin 45°
        const float ax = r * c;      // bar length × cos45
        const float wx = w * c;      // bar half-width × cos45
        // Bar 1: along (+1,+1) — long axis tilted up-right.
        quad(sx + (-ax + wx), sy + (-ax - wx), sx + (ax + wx), sy + (ax - wx), sx + (ax - wx),
             sy + (ax + wx), sx + (-ax - wx), sy + (-ax + wx));
        // Bar 2: along (+1,-1) — long axis tilted up-left.
        quad(sx + (-ax - wx), sy + (ax - wx), sx + (ax - wx), sy + (-ax - wx), sx + (ax + wx),
             sy + (-ax + wx), sx + (-ax + wx), sy + (ax + wx));
        break;
    }
    case Shape::Pentagon: {
        constexpr int n = 5;
        const float aStart = -kTau * 0.25f;
        for (int s = 0; s < n; ++s) {
            const float a0 = aStart + kTau * float(s) / float(n);
            const float a1 = aStart + kTau * float(s + 1) / float(n);
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r * std::cos(a0), sy + r * std::sin(a0)));
            out.push_back(v(sx + r * std::cos(a1), sy + r * std::sin(a1)));
        }
        break;
    }
    case Shape::Hexagon: {
        constexpr int n = 6;
        for (int s = 0; s < n; ++s) {
            const float a0 = kTau * float(s) / float(n);
            const float a1 = kTau * float(s + 1) / float(n);
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r * std::cos(a0), sy + r * std::sin(a0)));
            out.push_back(v(sx + r * std::cos(a1), sy + r * std::sin(a1)));
        }
        break;
    }
    case Shape::Arrow:
        // Right-pointing arrow head — single triangle.
        out.push_back(v(sx + r, sy));
        out.push_back(v(sx - r, sy + r * 0.7f));
        out.push_back(v(sx - r, sy - r * 0.7f));
        break;
    case Shape::EquilateralTriangle: {
        // Up-pointing: apex at top.
        const float h = r * 1.1547f; // 2/sqrt(3) so flat-base is r-aligned
        out.push_back(v(sx, sy - h));
        out.push_back(v(sx + r, sy + h * 0.5f));
        out.push_back(v(sx - r, sy + h * 0.5f));
        break;
    }
    case Shape::HalfCircle: {
        // Top half: arc from -π through 0. Fixed segment count for the
        // same reason as Circle above — scene-unit `r` is not a valid
        // scale here.
        constexpr int segments = 12;
        for (int s = 0; s < segments; ++s) {
            const float a0 = float(M_PI) + kTau * 0.5f * float(s) / float(segments);
            const float a1 = float(M_PI) + kTau * 0.5f * float(s + 1) / float(segments);
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r * std::cos(a0), sy + r * std::sin(a0)));
            out.push_back(v(sx + r * std::cos(a1), sy + r * std::sin(a1)));
        }
        break;
    }
    }
}

/**
 * @param[in,out] out Append-only uncoloured triangle vertices, no clearing.
 * @param[in] sx Glyph center X in output geometry frame.
 * @param[in] sy Glyph center Y in same frame.
 * @param[in] r Finite nonnegative glyph radius in same units, no local clamp.
 * @param[in] shape Supported canonical shape; unhandled enum emits no body.
 * @note Delegates shared shape emitter, no GPU upload or resource ownership; units already
 *   scaled/anchored by caller.
 */
void appendNodeGlyphTriangles(std::vector<QSGGeometry::Point2D> &out, float sx, float sy, float r,
                              OpenSWMM::Render::MarkerShape shape)
{
    auto v = [](float x, float y) {
        QSGGeometry::Point2D p;
        p.x = x;
        p.y = y;
        return p;
    };
    appendMarkerShapeImpl(out, sx, sy, r, shape, v);
}

/**
 * @param[in,out] out Append-only uncoloured triangle vertices, no clearing.
 * @param[in] sx Glyph center X in output geometry frame.
 * @param[in] sy Glyph center Y in same frame.
 * @param[in] r Finite nonnegative glyph radius in same units, no local clamp.
 * @param[in] shape Supported canonical shape; unhandled enum emits no body.
 * @note Thin alias with default Diamond; caller can provide any supported marker shape.
 * @note Delegates shared shape emitter, no GPU upload or resource ownership; units already
 *   scaled/anchored by caller.
 */
void appendGageTriangles(
    std::vector<QSGGeometry::Point2D> &out, float sx, float sy, float r,
    OpenSWMM::Render::MarkerShape shape = OpenSWMM::Render::MarkerShape::Diamond)
{
    appendNodeGlyphTriangles(out, sx, sy, r, shape);
}

/**
 * @param[in,out] out Append-only triangle list, six vertices per accepted segment.
 * @param[in] ax First endpoint X.
 * @param[in] ay First endpoint Y.
 * @param[in] bx Second endpoint X.
 * @param[in] by Second endpoint Y.
 * @param[in] hw Stroke HALF-width in same finite coordinate units, caller supplies nonnegative.
 * @note Segment length<1e-9returns, otherwise perpendicular quad with butt caps. No
 *   joins/culling/colour/finite validation or resource upload; allocation may partially append.
 */
void appendThickSegment(std::vector<QSGGeometry::Point2D> &out, float ax, float ay, float bx,
                        float by, float hw)
{
    const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9f)
        return;
    const float nx = -dy / len * hw, ny = dx / len * hw;
    auto v = [](float x, float y) {
        QSGGeometry::Point2D p;
        p.x = x;
        p.y = y;
        return p;
    };
    out.push_back(v(ax + nx, ay + ny));
    out.push_back(v(bx + nx, by + ny));
    out.push_back(v(ax - nx, ay - ny));
    out.push_back(v(bx + nx, by + ny));
    out.push_back(v(bx - nx, by - ny));
    out.push_back(v(ax - nx, ay - ny));
}

// Emit a dashed line as a run of thick quads (QSG has no native dash on a
// flat-colour material, so we tessellate). dash_on / dash_off are in the same
// (scene) units as the endpoints — callers scale pixel sizes by invView. This
// revives the subcatchment centroid→outlet connector that the CPU painter drew
// with a cosmetic Qt::DashLine pen before subcatchments moved to the QSG path.
/**
 * @param[in,out] out Append-only uncoloured triangles.
 * @param[in] ax First endpoint X.
 * @param[in] ay First endpoint Y.
 * @param[in] bx Second endpoint X.
 * @param[in] by Second endpoint Y.
 * @param[in] hw Stroke half-width in endpoint units.
 * @param[in] dash_on Finite nonnegative on-length in same units.
 * @param[in] dash_off Finite nonnegative gap length; positive period must advance float
 *   accumulator.
 * @note Length<1e-9no-op; period<=1e-9falls back solid quad, else on quads clipped at endpoint. No
 *   phase carry across segments/finite/overflow checks, caller prepares pixel→scene scale.
 */
void appendDashedThickLine(std::vector<QSGGeometry::Point2D> &out, float ax, float ay, float bx,
                           float by, float hw, float dash_on, float dash_off)
{
    const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9f)
        return;
    const float ux = dx / len, uy = dy / len;
    const float period = dash_on + dash_off;
    if (period <= 1e-9f) {
        appendThickSegment(out, ax, ay, bx, by, hw);
        return;
    }
    for (float s = 0.0f; s < len; s += period) {
        const float e = std::min(s + dash_on, len);
        appendThickSegment(out, ax + ux * s, ay + uy * s, ax + ux * e, ay + uy * e, hw);
    }
}

// Dashed ring OUTLINE (not a disc) emitted as a run of thick chords. The GPU
// mirror of the CPU painter's `drawEllipse(p2, r, r)` with a dashed cosmetic
// pen and NoBrush (swmmlayeritem.cpp) — the highlight that marks where a
// selected subcatchment drains to: the receiving node, or the representative
// centroid of the runon subcatchment. Both cases are already the connector's
// p2, so one ring covers them.
//
// Chords, not arcs: QSG has no curve primitive, so the circle is walked in
// fixed arc-length steps and each step inside an "on" dash becomes one thick
// quad. r / hw / dash_on / dash_off are all in scene units — callers scale
// pixel sizes by invView, so the ring holds its on-screen size at any zoom.
constexpr int kRingSegs = 48; // 48 chords reads as smooth at a ~12 px radius

/**
 * @param[in,out] out Append-only uncoloured triangle list.
 * @param[in] cx Circle center X.
 * @param[in] cy Circle center Y.
 * @param[in] r Finite radius in scene units, nonpositive ignored.
 * @param[in] hw Finite half-width, nonpositive ignored.
 * @param[in] dash_on Finite nonnegative arc on-length.
 * @param[in] dash_off Finite nonnegative arc gap, period<=1e-9draws all chords.
 * @note Uses48chords and midpoint phase classification, not exact arc clipping; step<=1e-9returns.
 *   No view conversion, source mutation or GPU upload.
 */
void appendDashedRing(std::vector<QSGGeometry::Point2D> &out, float cx, float cy, float r, float hw,
                      float dash_on, float dash_off)
{
    if (r <= 0.f || hw <= 0.f)
        return;
    const float circ = 6.28318531f * r;
    const float period = dash_on + dash_off;
    const float step = circ / float(kRingSegs);
    if (step <= 1e-9f)
        return;

    for (int i = 0; i < kRingSegs; ++i) {
        const float s0 = step * float(i);
        const float s1 = s0 + step;
        // Solid when the caller asked for no gap; otherwise keep this chord
        // only when its midpoint falls in the "on" half of the dash period.
        if (period > 1e-9f) {
            const float phase = std::fmod(0.5f * (s0 + s1), period);
            if (phase >= dash_on)
                continue;
        }
        const float a0 = s0 / r, a1 = s1 / r; // arc length → angle
        appendThickSegment(out, cx + r * std::cos(a0), cy + r * std::sin(a0), cx + r * std::cos(a1),
                           cy + r * std::sin(a1), hw);
    }
}

// Flow-direction arrow (GPU mirror of swmmlayeritem.cpp drawFlowArrow +
// polylineMidpoint). Walks the link's scene polyline (absolute scene doubles,
// interleaved xy, `count` verts), finds the half-length midpoint and the local
// tangent there, and appends ONE filled arrowhead triangle pointing along the
// from→to direction (upstream → downstream). Vertices are emitted
// anchor-relative (minus ox/oy) and narrowed to float, matching the link
// segment buffer. `lenScene` is the arrow length in scene units
// (arrowSize_px * invView). Returns silently on degenerate polylines.
/**
 * @param[in,out] out Append-only coloured triangle list.
 * @param[in] xy Borrowed finite interleaved absolute scene XY,2*countentries.
 * @param[in] count Polyline vertex count, at least2needed.
 * @param[in] ox Precision anchor X subtracted before float narrowing.
 * @param[in] oy Precision anchor Y.
 * @param[in] lenScene Positive finite full arrow length in scene units; nonpositive ignored.
 * @param[in] widthScene Finite full width; nonpositive uses1.2×length.
 * @param[in] cr Already-premultiplied red byte.
 * @param[in] cg Already-premultiplied green byte.
 * @param[in] cb Already-premultiplied blue byte.
 * @param[in] ca Alpha byte.
 * @note Null/short/zero-total polyline emits nothing. Finds half-arclength local tangent, rotates
 *   centered+xtriangle and appends3vertices. No current-flow gate, source mutation, colour
 *   premultiplication or geographic conversion here; caller supplies coherent values.
 */
void appendFlowArrowColored(std::vector<QSGGeometry::ColoredPoint2D> &out, const double *xy,
                            uint32_t count, double ox, double oy, float lenScene, float widthScene,
                            uchar cr, uchar cg, uchar cb, uchar ca)
{
    if (count < 2 || !xy || lenScene <= 0.f)
        return;
    double total = 0.0;
    for (uint32_t i = 1; i < count; ++i)
        total += std::hypot(xy[i * 2] - xy[(i - 1) * 2], xy[i * 2 + 1] - xy[(i - 1) * 2 + 1]);
    if (total <= 0.0)
        return;

    const double half = total * 0.5;
    double acc = 0.0, mx = 0.0, my = 0.0, ang = 0.0;
    for (uint32_t i = 1; i < count; ++i) {
        const double x0 = xy[(i - 1) * 2], y0 = xy[(i - 1) * 2 + 1], x1 = xy[i * 2],
                     y1 = xy[i * 2 + 1];
        const double segLen = std::hypot(x1 - x0, y1 - y0);
        if (acc + segLen >= half) {
            const double t = (segLen > 0.0) ? (half - acc) / segLen : 0.0;
            mx = x0 + t * (x1 - x0);
            my = y0 + t * (y1 - y0);
            ang = std::atan2(y1 - y0, x1 - x0);
            break;
        }
        acc += segLen;
    }

    // Independent width across the link; non-positive falls back to the
    // historic 1.2 x length (matches CPU drawFlowArrow).
    const double w = (widthScene > 0.f ? double(widthScene) : double(lenScene) * 1.2) * 0.5;
    const double cs = std::cos(ang), sn = std::sin(ang);
    auto v = [&](double lx, double ly) {
        QSGGeometry::ColoredPoint2D p;
        p.x = float(mx + lx * cs - ly * sn - ox);
        p.y = float(my + lx * sn + ly * cs - oy);
        p.r = cr;
        p.g = cg;
        p.b = cb;
        p.a = ca;
        return p;
    };
    out.push_back(v(lenScene * 0.5, 0.0)); // tip (downstream)
    out.push_back(v(-lenScene * 0.5, w));
    out.push_back(v(-lenScene * 0.5, -w));
}

/**
 * @param[in] mode QSG drawing mode.
 * @param[in] color Flat QColor including alpha passed to material.
 * @param[in] lw Native line width only for DrawLines/DrawLineStrip, default1; no validation.
 * @return Newly allocated QSGGeometryNode owning empty Point2D geometry and QSGFlatColorMaterial.
 *   Caller/parent owns node; allocate/use/destroy in render-resource context, no model mutation.
 */
QSGGeometryNode *makeFlatColorNode(QSGGeometry::DrawingMode mode, QColor color, float lw = 1.0f)
{
    auto *node = new QSGGeometryNode();
    auto *geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geo->setDrawingMode(mode);
    if (mode == QSGGeometry::DrawLines || mode == QSGGeometry::DrawLineStrip)
        geo->setLineWidth(lw);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *mat = new QSGFlatColorMaterial();
    mat->setColor(color);
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

// §QSG-3 — per-vertex coloured node (mirrors swmm2dmeshqsgrenderer.cpp's
// makeColoredNode). Used for nodesSel so each glyph carries its own
// colour (selection brush, or per-feature override) without needing a
// separate QSGGeometryNode per colour.
/**
 * @param[in] mode QSG drawing mode, typically DrawTriangles.
 * @return New node owning empty ColoredPoint2D geometry and QSGVertexColorMaterial; caller/parent
 *   owns node. Caller supplies premultiplied vertex colours and render-thread lifetime.
 */
QSGGeometryNode *makeColoredNode(QSGGeometry::DrawingMode mode)
{
    auto *node = new QSGGeometryNode();
    auto *geo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geo->setDrawingMode(mode);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *mat = new QSGVertexColorMaterial();
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

// Dirty flags for a geometry upload. A node whose geometry held ZERO vertices
// has no batch: the batch renderer skips empty elements when it builds
// batches, and a later DirtyGeometry on an element without a batch is a no-op
// (Qt's Renderer::nodeChanged only re-uploads or invalidates an EXISTING
// batch). Such a node never paints again until something else forces a batch
// rebuild — a layer toggle, a full tree rebuild. That is exactly the empty
// project: the first frame uploads nothing to every bucket, so the junctions
// and conduits the user then draws never reach the screen (and it is why the
// selection overlays used to "silently swallow" their first upload).
// DirtyMaterial on an unbatched element is the flag that requests
// BuildBatches, so raise it together with DirtyGeometry whenever a bucket goes
// from empty to populated.
/**
 * @param[in] wasEmpty Previous geometry vertexCount==0.
 * @param[in] n New vertex count, caller supplies nonnegative.
 * @return DirtyGeometry always, plus DirtyMaterial on empty→positive transition to force batch
 *   creation. Pure flag calculation, no node mutation.
 */
inline QSGNode::DirtyState geometryDirtyFlags(bool wasEmpty, int n)
{
    return (wasEmpty && n > 0) ? (QSGNode::DirtyGeometry | QSGNode::DirtyMaterial)
                               : QSGNode::DirtyState(QSGNode::DirtyGeometry);
}

/**
 * @param[in,out] node Required coloured geometry node with matching vertex layout/triangle mode;
 *   all existing children must be reusable same-family geometry nodes.
 * @param[in] verts Immutable premultiplied vertex array, size must fit int.
 * @note Copy first<=60000vertices to node, spill further chunks into reused/new DrawTriangles
 *   coloured children, empty surplus child geometry without deleting. Dirty flags handle
 *   empty→populated. Existing unrelated children/layout are unsafe; caller supplies dedicated
 *   family. Render-thread resource operation, no GPU completion guarantee/rollback on allocation.
 */
void uploadColoredVerts(QSGGeometryNode *node,
                        const std::vector<QSGGeometry::ColoredPoint2D> &verts)
{
    // QSG's batch renderer indexes batched geometry with 16-bit indices, so a
    // single geometry node above 65535 vertices wraps those indices and draws
    // garbage triangles spanning unrelated primitives — the low-zoom
    // "polygonal triangles connecting conduits that aren't connected" artifact
    // (every link visible at once easily exceeds the limit). Keep the node
    // itself under the cap and spill the remainder into child geometry nodes,
    // each also under the cap. (60000 is a safe multiple of 6 = whole quads.)
    constexpr int kMaxPerNode = 60000;
    const int total = int(verts.size());

    auto fill = [](QSGGeometryNode *gn, const QSGGeometry::ColoredPoint2D *src, int n) {
        auto *geo = gn->geometry();
        const bool wasEmpty = geo->vertexCount() == 0;
        if (geo->vertexCount() != n)
            geo->allocate(n);
        if (n > 0)
            std::memcpy(geo->vertexDataAsColoredPoint2D(), src,
                        size_t(n) * sizeof(QSGGeometry::ColoredPoint2D));
        gn->markDirty(geometryDirtyFlags(wasEmpty, n)); // see uploadVerts
    };

    // Chunk 0 → the node itself.
    fill(node, verts.data(), std::min(kMaxPerNode, total));

    // Remaining chunks → child geometry nodes (created on demand, reused
    // across frames; surplus children from a larger frame are emptied below).
    int off = kMaxPerNode;
    QSGNode *child = node->firstChild();
    while (off < total) {
        QSGGeometryNode *gn;
        if (child) {
            gn = static_cast<QSGGeometryNode *>(child);
            child = child->nextSibling();
        } else {
            gn = makeColoredNode(QSGGeometry::DrawTriangles);
            node->appendChildNode(gn);
        }
        fill(gn, verts.data() + off, std::min(kMaxPerNode, total - off));
        off += kMaxPerNode;
    }
    while (child) {
        fill(static_cast<QSGGeometryNode *>(child), nullptr, 0);
        child = child->nextSibling();
    }
}

// Coloured variant of appendNodeGlyphTriangles — bakes the colour into
// every emitted vertex so the QSGVertexColorMaterial pipeline picks it
// up. Shape selection is identical to the flat-colour version so
// positions stay consistent between the two paths.
/**
 * @param[in,out] out Append-only coloured triangle list.
 * @param[in] sx Glyph center X in output geometry frame.
 * @param[in] sy Glyph center Y in same frame.
 * @param[in] r Finite nonnegative glyph radius in same units, no local clamp.
 * @param[in] shape Supported canonical shape; unhandled enum emits no body.
 * @param[in] cr Already-premultiplied red byte.
 * @param[in] cg Already-premultiplied green byte.
 * @param[in] cb Already-premultiplied blue byte.
 * @param[in] ca Alpha byte.
 * @note Shared shape geometry, constant supplied bytes copied to every vertex without further
 *   premultiplication. No GPU upload, input mutation or thread dispatch.
 */
void appendNodeGlyphTrianglesColored(std::vector<QSGGeometry::ColoredPoint2D> &out, float sx,
                                     float sy, float r, OpenSWMM::Render::MarkerShape shape,
                                     uchar cr, uchar cg, uchar cb, uchar ca)
{
    auto v = [&](float x, float y) {
        QSGGeometry::ColoredPoint2D p;
        p.x = x;
        p.y = y;
        p.r = cr;
        p.g = cg;
        p.b = cb;
        p.a = ca;
        return p;
    };
    appendMarkerShapeImpl(out, sx, sy, r, shape, v);
}

// Cache each glyph's exterior contour once, excluding triangle-fan spokes.
// Emit a triangle strip around that contour so outline width stays cosmetic
// on Metal/D3D as well as OpenGL (native wide lines are not portable).
/**
 * @param[in,out] out Append-only premultiplied triangle outline list.
 * @param[in] x Center X in anchored geometry frame.
 * @param[in] y Center Y.
 * @param[in] radius Finite glyph radius in geometry units.
 * @param[in] width Finite outline full-width in same units; nonpositive ignored.
 * @param[in] shape Canonical glyph used for cached unit contour, unhandled shape can yield empty.
 * @param[in] color Straight QColor, invalid/alpha0ignored; helper premultiplies.
 * @note Lazily caches unit exterior contours by shape via rounded triangle union/simplification,
 *   then emits6vertices per contour edge using denominator floor0.25mitre offsets. Does not include
 *   fan spokes. Static cache has no lock; serialize across renderers/windows sharing it. No
 *   cull/view scaling/GPU upload here.
 */
void appendMarkerOutlineColored(std::vector<QSGGeometry::ColoredPoint2D> &out, float x, float y,
                                float radius, float width, OpenSWMM::Render::MarkerShape shape,
                                QColor color)
{
    if (width <= 0.0f || !color.isValid() || color.alpha() == 0)
        return;
    static QHash<int, QList<QPolygonF>> contours;
    const int key = int(shape);
    if (!contours.contains(key)) {
        std::vector<QSGGeometry::Point2D> triangles;
        appendNodeGlyphTriangles(triangles, 0, 0, 1, shape);
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
            QPolygonF triangle;
            for (size_t j = i; j < i + 3; ++j)
                triangle << QPointF(std::round(triangles[j].x * 1e5) / 1e5,
                                    std::round(triangles[j].y * 1e5) / 1e5);
            path.addPolygon(triangle);
            path.closeSubpath();
        }
        contours.insert(key, path.simplified().toSubpathPolygons());
    }
    const uchar alpha = uchar(color.alpha());
    const auto vertex = [&](QPointF point) {
        QSGGeometry::ColoredPoint2D v;
        v.x = float(point.x());
        v.y = float(point.y());
        v.r = premul(uchar(color.red()), alpha);
        v.g = premul(uchar(color.green()), alpha);
        v.b = premul(uchar(color.blue()), alpha);
        v.a = alpha;
        return v;
    };
    for (const auto &polygon : contours[key]) {
        if (polygon.size() < 3)
            continue;
        const int count = polygon.size() - (polygon.first() == polygon.last() ? 1 : 0);
        if (count < 3)
            continue;
        const auto normal = [](QPointF edge) {
            const double length = std::hypot(edge.x(), edge.y());
            return length > 0.0 ? QPointF(-edge.y(), edge.x()) / length : QPointF();
        };
        const auto offset = [&](int i) {
            const QPointF prev = normal(polygon[i] - polygon[(i + count - 1) % count]);
            const QPointF next = normal(polygon[(i + 1) % count] - polygon[i]);
            const double denominator = 1.0 + QPointF::dotProduct(prev, next);
            return (prev + next) * (0.5 * width / std::max(0.25, denominator));
        };
        for (int i = 0; i < count; ++i) {
            const int j = (i + 1) % count;
            const QPointF a = QPointF(x, y) + polygon[i] * radius;
            const QPointF b = QPointF(x, y) + polygon[j] * radius;
            const QPointF da = offset(i), db = offset(j);
            out.push_back(vertex(a + da));
            out.push_back(vertex(b + db));
            out.push_back(vertex(b - db));
            out.push_back(vertex(a + da));
            out.push_back(vertex(b - db));
            out.push_back(vertex(a - da));
        }
    }
}

/**
 * @param[in,out] node Required node with Point2D geometry; matching layout/capacity owned by node.
 * @param[in] verts Immutable uncoloured vertices, size/byte count fit int.
 * @note Resize/copy whole stream and mark geometry plus first-population material dirty. No
 *   chunking/index-limit protection unlike coloured helper; caller bounds flat stream.
 *   Render-thread resource call, no model change/GPU completion guarantee.
 */
void uploadVerts(QSGGeometryNode *node, const std::vector<QSGGeometry::Point2D> &verts)
{
    auto *geo = node->geometry();
    const int n = int(verts.size());
    const bool wasEmpty = geo->vertexCount() == 0;
    if (geo->vertexCount() != n)
        geo->allocate(n);
    if (n > 0)
        std::memcpy(geo->vertexData(), verts.data(), n * sizeof(QSGGeometry::Point2D));
    node->markDirty(geometryDirtyFlags(wasEmpty, n));
}

/**
 * @param[in,out] node Required QSGGeometryNode with actual QSGFlatColorMaterial, unchecked cast.
 * @param[in] color New flat QColor copied, including invalid.
 * @note Changed colour sets material and DirtyMaterial; equal no-op. No vertex upload or ownership
 *   transfer, render-thread only.
 */
void setNodeColor(QSGGeometryNode *node, QColor color)
{
    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    if (mat->color() != color) {
        mat->setColor(color);
        node->markDirty(QSGNode::DirtyMaterial);
    }
}

// Selection helpers. The QSG renderer drives QSGFlatColorMaterial nodes,
// which take a single QColor — so we surface pen colour for outlines /
// line nodes and brush colour for fills. Width/cap/join from the
// selection pen aren't representable here (no per-vertex line width on
// QSGGeometry::DrawTriangles).
/**
 * @param[in] key Required NUL-terminated Latin-1 preference object-kind key.
 * @return Selection pen colour including alpha, yellow fallback if singleton absent. Does not carry
 *   width/dash/cap/join or mutate preferences; caller handles premultiplication.
 */
QColor selPenColor(const char *key)
{
    auto *p = PreferencesManager::instance();
    return p ? p->selectionPen(QString::fromLatin1(key)).color() : QColor(255, 255, 0);
}
/**
 * @param[in] key Required NUL-terminated Latin-1 preference object-kind key.
 * @return Selection brush colour including alpha, yellow fallback if singleton absent. Does not
 *   carry width/dash/cap/join or mutate preferences; caller handles premultiplication.
 */
QColor selBrushColor(const char *key)
{
    auto *p = PreferencesManager::instance();
    return p ? p->selectionBrush(QString::fromLatin1(key)).color() : QColor(255, 255, 0);
}

} // namespace

// ---------------------------------------------------------------------------

SWMMLayerQSGRenderer::SWMMLayerQSGRenderer(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    sampler().init();
}

SWMMLayerQSGRenderer::~SWMMLayerQSGRenderer() = default;

void SWMMLayerQSGRenderer::setLayer(SWMMModelLayer *layer)
{
    if (m_layer == layer)
        return;
    if (m_layer)
        QObject::disconnect(m_layer, nullptr, this, nullptr);
    m_layer = layer;
    if (m_layer) {
        // Selection change: only the 5 overlay buffers need rebuilding.
        // Set m_selectionPending so the repaintRequested that always follows
        // selectionChanged is absorbed without triggering a full rebuild.
        connect(m_layer, &SWMMModelLayer::selectionChanged, this, [this](const QStringList &) {
            // §QSG-3 — selection updates need a full rebuild
            // because selection colour is baked per-vertex into
            // the base buckets. The selection-only branch in
            // updatePaintNode is therefore unused for nodes.
            m_contentDirty = true;
            m_selDirty = true;
            m_selectionPending = true;
            noteContentChanged();
        });
        // All other model changes (geometry, symbology, visibility): full rebuild.
        connect(m_layer, &SWMMModelLayer::repaintRequested, this, [this]() {
            if (m_selectionPending) {
                m_selectionPending = false;
                return;
            }
            m_contentDirty = true;
            noteContentChanged();
        });
        // Geometry mutations (add / move / delete a node, link, subcatchment or
        // gage) must ALWAYS force a full content rebuild. They cannot ride the
        // repaintRequested handler above: its selection-absorb optimisation
        // (m_selectionPending, set when a selection change is coalesced with the
        // repaint that follows it) can swallow the repaint a just-added object
        // emits, leaving the new object out of the QSG geometry until an
        // unrelated rebuild — the "have to toggle the layer off/on to see a
        // newly added junction" bug. geometryChanged fires on every add/move/
        // delete and is exempt from that absorb, so wire it straight to a
        // rebuild and clear any pending-absorb state.
        connect(m_layer, &SWMMModelLayer::geometryChanged, this, [this]() {
            m_contentDirty = true;
            m_selectionPending = false;
            // Subcatchment fills are triangulated behind a
            // geomRevision-keyed cache; force it stale so a moved/added
            // polygon re-triangulates on the next rebuild.
            m_catchTriCache.revision = std::numeric_limits<quint64>::max();
            noteContentChanged();
        });
    }
    m_catchTriCache.revision = std::numeric_limits<quint64>::max();
    m_selDirty = false;
    m_selectionPending = false;
    m_contentDirty = true;
    update();
}

void SWMMLayerQSGRenderer::forceRebuild()
{
    m_catchTriCache.revision = std::numeric_limits<quint64>::max();
    m_contentDirty = true;
    m_selDirty = true;
    m_selectionPending = false;
    noteContentChanged();
}

void SWMMLayerQSGRenderer::setMapExtent(const MapExtent &extent)
{
    if (extent == m_extent)
        return;
    const bool zoomChanged = !qFuzzyCompare(extent.width(), m_extent.width()) ||
                             !qFuzzyCompare(extent.height(), m_extent.height());
    m_extent = extent;
    if (zoomChanged) {
        // Zoom changes invalidate the cull bounds AND the precision
        // anchor — full content rebuild required.
        m_contentDirty = true;
    } else if (m_lastBuiltExtent.isValid()) {
        // Pan-only path: only re-cull/upload vertices when the new
        // viewport drifts more than half the width/height of the
        // viewport the cached vertices were built against. Up to that
        // point the wider cullMargin guarantees the cached vertex set
        // already covers what's on screen, so we just update the
        // transform matrix in updatePaintNode().
        const double dx = std::abs(extent.centerX() - m_lastBuiltExtent.centerX());
        const double dy = std::abs(extent.centerY() - m_lastBuiltExtent.centerY());
        if (dx > m_lastBuiltExtent.width() * 0.5 || dy > m_lastBuiltExtent.height() * 0.5)
            m_contentDirty = true;
    } else {
        // First pan before any successful build — must rebuild.
        m_contentDirty = true;
    }
    update();
}

QSGNode *SWMMLayerQSGRenderer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // VS.1 — render nothing when the layer is hidden. MapCanvas only pushes
    // the layer to this renderer inside its qsgActive (visible-layer) block,
    // so on a whole-layer toggle-off the renderer is never told to clear and
    // its node tree would otherwise keep showing the full network (the
    // "glyphs remain after turning the layer off" artifact). The renderer's
    // repaintRequested → update() connection guarantees updatePaintNode runs
    // on the toggle; dropping the node here empties the FBO immediately and
    // it round-trips back to a full build when the layer is shown again.
    if (!m_layer || !m_layer->isVisible() || !m_extent.isValid() || width() <= 0 || height() <= 0) {
        delete oldNode;
        return nullptr;
    }

    QElapsedTimer frameTimer;
    if (sampler().enabled)
        frameTimer.start();
    const bool wasRebuild = m_contentDirty || m_selDirty;

    // ---- Node tree (draw order = back to front) ----------------------------
    auto *root = static_cast<QSGTransformNode *>(oldNode);
    QSGGeometryNode *catchFill = nullptr, *catchSelFill = nullptr;
    QSGGeometryNode *catchEdge = nullptr, *catchSelEdge = nullptr;
    QSGGeometryNode *catchOutletLines = nullptr, *catchOutletLinesSel = nullptr;
    QSGGeometryNode *junctionsBase = nullptr, *outfallsBase = nullptr;
    QSGGeometryNode *storageBase = nullptr, *dividersBase = nullptr;
    QSGGeometryNode *gagesBase = nullptr;
    QSGGeometryNode *lines = nullptr, *linesSel = nullptr;
    QSGGeometryNode *nodesSel = nullptr, *gagesSel = nullptr;

    if (!root) {
        // Selection style — pens carry outline colour, brushes carry
        // fill colour (incl. alpha). selPoly is the polygon outline;
        // selPolyFill, selNode, selGage are fills. selLine is the link
        // halo's stroke colour. cap/join/width from the pens aren't
        // representable on a QSGFlatColorMaterial.
        const QColor selPoly = selPenColor("subcatchment");
        const QColor selPolyFill = selBrushColor("subcatchment");
        const QColor selNode = selBrushColor("node");
        const QColor selGage = selBrushColor("gage");
        const QColor selLine = selPenColor("link");

        root = new QSGTransformNode();
        // #33 Stage 1b — catchFill is now vertex-coloured so each subcatchment
        // can carry its own per-feature override colour (Graduated/Categorized).
        catchFill = makeColoredNode(QSGGeometry::DrawTriangles);
        catchSelFill = makeFlatColorNode(QSGGeometry::DrawTriangles, selPolyFill);
        // Feedback C13 — outlines are tessellated as pixel-width quads (same
        // technique as the link bodies and the A1 node outlines) so the
        // Symbology outline width applies; a DrawLines node with lineWidth
        // 1 ignored it on every backend.
        catchEdge = makeFlatColorNode(QSGGeometry::DrawTriangles,
                                      m_layer->subcatchmentSymbol().outlineColor);
        catchSelEdge = makeFlatColorNode(QSGGeometry::DrawTriangles, selPoly);
        // Subcatchment centroid→outlet connector lines (dashed, tessellated as
        // thick quads). Base grey + a bolder orange highlight for the selected
        // subcatchment, mirroring the old CPU painter pens.
        catchOutletLines =
            makeFlatColorNode(QSGGeometry::DrawTriangles, QColor(110, 110, 110, 200));
        catchOutletLinesSel =
            makeFlatColorNode(QSGGeometry::DrawTriangles, QColor(255, 140, 0, 230));
        // §QSG-3 — vertex-coloured base node buckets. Per-vertex colour
        // lets each glyph carry its own colour: kind's fillColor for
        // unselected nodes, selection brush colour for selected nodes,
        // per-feature override colour where applicable. Eliminates the
        // separate `nodesSel` overlay (which had a latent rendering bug
        // — its geometry uploads silently didn't paint despite correct
        // vertex/material/parent state).
        junctionsBase = makeColoredNode(QSGGeometry::DrawTriangles);
        outfallsBase = makeColoredNode(QSGGeometry::DrawTriangles);
        storageBase = makeColoredNode(QSGGeometry::DrawTriangles);
        dividersBase = makeColoredNode(QSGGeometry::DrawTriangles);
        // CPU-parity — vertex-coloured so per-gage renderer overrides
        // (featureColor) and the kind opacity fade bake per vertex, same
        // as the node buckets.
        gagesBase = makeColoredNode(QSGGeometry::DrawTriangles);
        // #33 Stage 1b — link base node is vertex-coloured for per-feature
        // (Graduated/Categorized) link colours; the selection halo stays flat.
        lines = makeColoredNode(QSGGeometry::DrawTriangles);
        linesSel = makeFlatColorNode(QSGGeometry::DrawTriangles, selLine);
        // §QSG-3 — nodesSel uses vertex-coloured material so each
        // selected glyph carries its own colour in the vertex stream.
        // The same QSGFlatColorMaterial node used to silently swallow
        // post-creation geometry uploads on the macOS Metal backend
        // (verified by FBO dump: 24 verts in the geometry, opaque
        // yellow material, valid widget coords, but zero yellow pixels
        // in the rendered frame).  The vertex-coloured path uses the
        // exact same code shape as SWMM2DMeshQSGRenderer's coloured
        // triangle node, which paints correctly in the same offscreen
        // FBO setup.
        nodesSel = makeColoredNode(QSGGeometry::DrawTriangles);
        gagesSel = makeFlatColorNode(QSGGeometry::DrawTriangles, selGage);
        // QSG paints children in tree order: earlier siblings draw
        // first (back), later siblings draw on top (front). Layer
        // stacking (back → front):
        //   1. Subcatchment fill / outline (background)
        //   2. Link lines (above catchments, below nodes)
        //   3. Selected-link halo (above base lines)
        //   4. Node glyphs by kind (above all link geometry)
        //   5. Rain-gage glyphs
        //   6. Node + gage selection overlays (top)
        // Previously the node buckets were appended *before* the line
        // nodes, which made conduits paint *over* junctions — a
        // standard GIS Z-order bug. The order below matches QGIS /
        // ArcMap conventions for point-over-line.
        for (auto *n : {catchFill, catchSelFill, catchEdge, catchSelEdge, catchOutletLines,
                        catchOutletLinesSel, lines, linesSel, junctionsBase, outfallsBase,
                        storageBase, dividersBase, gagesBase, nodesSel, gagesSel})
            root->appendChildNode(n);
    } else {
        auto *c = root->firstChild();
        catchFill = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        catchSelFill = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        catchEdge = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        catchSelEdge = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        catchOutletLines = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        catchOutletLinesSel = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        lines = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        linesSel = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        junctionsBase = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        outfallsBase = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        storageBase = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        dividersBase = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        gagesBase = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        nodesSel = static_cast<QSGGeometryNode *>(c);
        c = c->nextSibling();
        gagesSel = static_cast<QSGGeometryNode *>(c);
    }

    // ---- Shared render params (used by both full and selection-only paths) --
    // sx_r / invView are pixel-scale ratios so they're safe as float;
    // cull bounds, however, are scene-space coordinates and must stay
    // in double — float quantises projected-CRS coords (6-7 digit
    // magnitudes) to ~1 m, which trips false-positive / false-negative
    // cull decisions at high zoom and can flash links in and out as
    // the viewport scrolls.
    const float sx_r = float(width()) / float(m_extent.width());
    const float invView = (sx_r > 0.0f) ? 1.0f / sx_r : 1.0f;
    // Precision anchor used to make GPU vertices small relative to a local
    // origin before the float() narrowing. NOTE: this is refreshed inside the
    // content-rebuild block below AFTER m_anchorX/Y are recomputed — the
    // recompute and the vertex building must agree on the same anchor, or the
    // transform matrix (which uses the recomputed m_anchorX/Y) and the vertices
    // (which use ox/oy) disagree, throwing every vertex off by the anchor delta
    // and quantising the un-anchored coords to a float grid (the "fishnet
    // conduits + invisible nodes on first load" bug).
    double ox = m_anchorX, oy = m_anchorY;
    // Wide cull margin (half the viewport on each side) so a pan that
    // shifts the viewport by up to half its width/height stays within
    // the cached vertex set — setMapExtent() exploits this to skip
    // m_contentDirty on small pans, leaving updatePaintNode() to just
    // refresh the transform matrix.
    const double cullMarginX = m_extent.width() * 0.5;
    const double cullMarginY = m_extent.height() * 0.5;
    const double cullX0 = m_extent.xMin() - cullMarginX;
    const double cullX1 = m_extent.xMax() + cullMarginX;
    const double cullY0 = -m_extent.yMax() - cullMarginY;
    const double cullY1 = -m_extent.yMin() + cullMarginY;

    // ---- Content rebuild (full) --------------------------------------------
    if (m_contentDirty) {
        // Precision anchor — recomputed from node scene bounds.
        {
            double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
            for (const QPointF &p : m_layer->m_nodeScenePts) {
                if (p.x() < minX)
                    minX = p.x();
                if (p.x() > maxX)
                    maxX = p.x();
                if (p.y() < minY)
                    minY = p.y();
                if (p.y() > maxY)
                    maxY = p.y();
            }
            m_anchorX = (minX <= maxX) ? (minX + maxX) * 0.5 : 0.0;
            m_anchorY = (minY <= maxY) ? (minY + maxY) * 0.5 : 0.0;
        }
        // Re-sync the local anchor to the freshly recomputed member so the
        // vertices built below and the transform matrix at the end of this
        // function use the SAME origin. Without this, the first rebuild after
        // a model load (anchor 0 → scene-centre) builds every vertex against
        // a stale origin, producing the fishnet + off-screen-nodes artifact.
        ox = m_anchorX;
        oy = m_anchorY;

        // Material refresh.
        // Selection style — pens carry outline colour, brushes carry
        // fill colour (incl. alpha). selPoly is the polygon outline;
        // selPolyFill, selNode, selGage are fills. selLine is the link
        // halo's stroke colour. cap/join/width from the pens aren't
        // representable on a QSGFlatColorMaterial.
        const QColor selPoly = selPenColor("subcatchment");
        const QColor selPolyFill = selBrushColor("subcatchment");
        const QColor selNode = selBrushColor("node");
        const QColor selGage = selBrushColor("gage");
        const QColor selLine = selPenColor("link");
        // #33 Stage 1b — catchFill + lines are now QSGVertexColorMaterial;
        // their colours are baked per-vertex at upload time, so NO
        // material-level setColor (which would crash the static_cast).
        setNodeColor(catchSelFill, selPolyFill);
        // CPU-parity — fade the subcatchment outline by the kind's
        // (sub-layer) opacity, matching the fill fade + the CPU painter.
        {
            QColor edge = m_layer->subcatchmentSymbol().outlineColor;
            const qreal kop = m_layer->categoryOpacity(SWMMModelLayer::CatSubcatchments);
            if (kop < 1.0 && edge.isValid())
                edge.setAlphaF(edge.alphaF() * kop);
            setNodeColor(catchEdge, edge);
        }
        setNodeColor(catchSelEdge, selPoly);
        // §QSG-3 — junctionsBase/outfallsBase/storageBase/dividersBase
        // use QSGVertexColorMaterial; colours are baked per-vertex at
        // upload time, no material-level setColor required.
        // gagesBase now uses QSGVertexColorMaterial — per-gage colour (incl.
        // featureColor overrides + kind opacity fade) bakes per vertex at
        // upload time; material-level setColor would crash the static_cast.
        setNodeColor(linesSel, selLine);
        // §QSG-3 — nodesSel uses QSGVertexColorMaterial; the colour is
        // baked into each vertex at upload time, so no material-level
        // setColor is needed (and would crash trying to static_cast).
        setNodeColor(gagesSel, selGage);

        // ---- Subcatchments -------------------------------------------------
        // §QSG-1: only render kinds owned by the QSG scope; uploading
        // empty vertex buffers for un-owned kinds keeps the geometry
        // node in the tree but makes the GPU draw zero triangles, so
        // the CPU SWMMLayerItem path can own that kind without
        // doubling-up.
        if (m_layer->showSubcatchments() && m_layer->qsgOwnsKind(SWMMModelLayer::QsgCatch)) {
            const auto &cps = m_layer->m_catchScenePts;
            const auto &cBboxes = m_layer->m_catchSceneBBoxes;
            const auto &cHid = m_layer->m_catchHiddenFlag;
            const auto &cSel = m_layer->m_catchSelectedFlag;

            // Triangulation cache — only re-triangulate on geometry change.
            const quint64 rev = m_layer->geomRevision();
            if (m_catchTriCache.revision != rev) {
                m_catchTriCache.tris.resize(cps.size());
                for (int i = 0; i < cps.size(); ++i)
                    m_catchTriCache.tris[i] =
                        OpenSWMM::Render::SubcatchmentGeometry::triangulate(cps[i]);
                m_catchTriCache.revision = rev;
            }

            // #33 Stage 1b — fillBase is vertex-coloured (per-subcatchment
            // override colour); selection fill + edges stay flat.
            std::vector<QSGGeometry::ColoredPoint2D> fillBase;
            std::vector<QSGGeometry::Point2D> fillSel, edgeBase, edgeSelTris;
            const float selEdgeHW = 1.5f * invView;
            // Feedback C13 — half-width of the outline quads in scene units;
            // zero or negative width draws no outline, matching the CPU pen.
            const float edgeHW = 0.5f * float(m_layer->subcatchmentSymbol().outlineWidth) * invView;
            const QColor catchDef = m_layer->subcatchmentSymbol().fillColor;
            for (int i = 0; i < cps.size(); ++i) {
                if (size_t(i) < cHid.size() && cHid[i])
                    continue;
                if (size_t(i) < size_t(cBboxes.size())) {
                    const QRectF &bb = cBboxes[i];
                    if (bb.right() < cullX0 || bb.left() > cullX1 || bb.bottom() < cullY0 ||
                        bb.top() > cullY1)
                        continue;
                }
                const auto &poly = cps[i];
                if (poly.size() < 3)
                    continue;
                const auto &tris = m_catchTriCache.tris[i];
                const bool sel = size_t(i) < cSel.size() && cSel[i];
                // Per-feature override colour (Graduated/Categorized) or the
                // kind default; `i` is the catch SoA index = featureColor key.
                const QColor fc = m_layer->featureColor(SWMMModelLayer::CatSubcatchments, i);
                const QColor cc = fc.isValid() ? fc : catchDef;
                const qreal ckop = m_layer->categoryOpacity(SWMMModelLayer::CatSubcatchments);
                const uchar fA = uchar(ckop < 1.0 ? cc.alpha() * ckop : cc.alpha());
                const uchar fR = premul(uchar(cc.red()), fA), fG = premul(uchar(cc.green()), fA),
                            fB = premul(uchar(cc.blue()), fA);
                for (const QPointF &point : tris) {
                    QSGGeometry::ColoredPoint2D p;
                    p.x = float(point.x() - ox);
                    p.y = float(point.y() - oy);
                    p.r = fR;
                    p.g = fG;
                    p.b = fB;
                    p.a = fA;
                    fillBase.push_back(p);
                }
                if (edgeHW > 0.0f) {
                    for (const auto &ring : OpenSWMM::Render::SubcatchmentGeometry::rings(poly))
                        for (int j = ring.first; j < ring.first + ring.count; ++j) {
                            const float ax = float(poly[j].x() - ox), ay = float(poly[j].y() - oy);
                            const float bx = float(
                                poly[ring.first + (j - ring.first + 1) % ring.count].x() - ox);
                            const float by = float(
                                poly[ring.first + (j - ring.first + 1) % ring.count].y() - oy);
                            appendThickSegment(edgeBase, ax, ay, bx, by, edgeHW);
                        }
                }
                if (sel) {
                    for (const QPointF &point : tris) {
                        QSGGeometry::Point2D p;
                        p.x = float(point.x() - ox);
                        p.y = float(point.y() - oy);
                        fillSel.push_back(p);
                    }
                    for (const auto &ring : OpenSWMM::Render::SubcatchmentGeometry::rings(poly))
                        for (int j = ring.first; j < ring.first + ring.count; ++j) {
                            const float ax = float(poly[j].x() - ox), ay = float(poly[j].y() - oy);
                            const float bx = float(
                                poly[ring.first + (j - ring.first + 1) % ring.count].x() - ox);
                            const float by = float(
                                poly[ring.first + (j - ring.first + 1) % ring.count].y() - oy);
                            appendThickSegment(edgeSelTris, ax, ay, bx, by, selEdgeHW);
                        }
                }
            }
            uploadColoredVerts(catchFill, fillBase); // #33 Stage 1b — per-feature
            uploadVerts(catchSelFill, fillSel);
            uploadVerts(catchEdge, edgeBase);
            uploadVerts(catchSelEdge, edgeSelTris);

            // Centroid→outlet connector lines (dashed). Built from the
            // layer-maintained scene-space lines; selected subcatchments get a
            // bolder orange dash so the user can trace where runoff drains.
            std::vector<QSGGeometry::Point2D> olBase, olSel;
            const float olDashOn = 6.0f * invView;
            const float olDashOff = 4.0f * invView;
            const float olHwBase = 0.75f * invView;
            const float olHwSel = 1.0f * invView;
            // Outlet ring radius — 12 px, matching the CPU painter's
            // `12.0 * invViewScale` so both paths draw the same highlight.
            const float olRingR = 12.0f * invView;
            for (const auto &ol : m_layer->m_catchOutletLines) {
                const int ci = ol.catchIdx;
                if (size_t(ci) < cHid.size() && cHid[ci])
                    continue;
                // Cull in raw scene coords (cullX0.. are scene-space, matching
                // the polygon/glyph cull above); offset is subtracted only when
                // the geometry vertices are emitted.
                const double rx1 = ol.line.p1().x(), ry1 = ol.line.p1().y();
                const double rx2 = ol.line.p2().x(), ry2 = ol.line.p2().y();
                if (std::max(rx1, rx2) < cullX0 || std::min(rx1, rx2) > cullX1 ||
                    std::max(ry1, ry2) < cullY0 || std::min(ry1, ry2) > cullY1)
                    continue;
                const float ax = float(rx1 - ox), ay = float(ry1 - oy);
                const float bx = float(rx2 - ox), by = float(ry2 - oy);
                const bool sel = size_t(ci) < cSel.size() && cSel[ci];
                if (sel) {
                    appendDashedThickLine(olSel, ax, ay, bx, by, olHwSel, olDashOn, olDashOff);
                    // Ring the far end so the receiving outlet is called out,
                    // not just the path to it. p2 is the outlet NODE's scene
                    // point, or — when the subcatchment drains to another
                    // subcatchment (runon) — that subcatchment's centroid;
                    // SWMMModelLayer resolves both to this one point, so a
                    // single ring serves both cases.
                    appendDashedRing(olSel, bx, by, olRingR, olHwSel, olDashOn, olDashOff);
                } else {
                    appendDashedThickLine(olBase, ax, ay, bx, by, olHwBase, olDashOn, olDashOff);
                }
            }
            uploadVerts(catchOutletLines, olBase);
            uploadVerts(catchOutletLinesSel, olSel);
        } else {
            // Subcatchments not in QSG scope — clear so the CPU path
            // owns this kind without the GPU drawing on top of it.
            uploadColoredVerts(catchFill, {}); // #33 Stage 1b — colored node
            uploadVerts(catchSelFill, {});
            uploadVerts(catchEdge, {});
            uploadVerts(catchSelEdge, {});
            uploadVerts(catchOutletLines, {});
            uploadVerts(catchOutletLinesSel, {});
        }

        // ---- Links ---------------------------------------------------------
        if (m_layer->showLinks() && m_layer->qsgOwnsKind(SWMMModelLayer::QsgLinks)) {
            const std::vector<double> &flat = m_layer->m_linkSceneFlat;
            const std::vector<uint32_t> &offsets = m_layer->m_linkVertexOffset;
            const std::vector<uint32_t> &counts = m_layer->m_linkVertexCount;
            const auto &lSel = m_layer->m_linkSelectedFlag;
            const auto &lHid = m_layer->m_linkHiddenFlag;
            const QVector<QRectF> &lBboxes = m_layer->m_linkSceneBBoxes;
            const auto &links = m_layer->m_links;
            static constexpr SWMMModelLayer::Category kLinkCat[5] = {
                SWMMModelLayer::CatConduits, SWMMModelLayer::CatPumps, SWMMModelLayer::CatOrifices,
                SWMMModelLayer::CatWeirs, SWMMModelLayer::CatOutlets};
            // Per-link-type colour + half-width, mirroring the CPU
            // linkPenForType(): start from the kind's preference pen, then let
            // the per-type SWMMElementSymbol override colour / width. Without
            // this every link took the conduit colour + width, so on initial
            // load all link types rendered the same conduit blue at conduit
            // thickness. The per-feature Graduated/Categorized colour
            // (featureColor) still wins over this default when present.
            auto *prefs = PreferencesManager::instance();
            struct LinkStyle {
                QColor color;
                float hw;
            };
            LinkStyle lstyle[5];
            {
                auto build = [&](const char *kind, const SWMMElementSymbol *sym) {
                    QPen pen = prefs->linkPen(QString::fromLatin1(kind));
                    QColor c = pen.color();
                    double w = pen.widthF();
                    if (sym) {
                        if (sym->fillColor.isValid())
                            c = sym->fillColor;
                        if (sym->size > 0.0)
                            w = sym->size;
                    }
                    return LinkStyle{c, float((w > 0.0 ? w : 1.0) * invView)};
                };
                const SWMMElementSymbol cd = m_layer->conduitSymbol();
                const SWMMElementSymbol pm = m_layer->pumpSymbol();
                const SWMMElementSymbol orf = m_layer->orificeSymbol();
                const SWMMElementSymbol wr = m_layer->weirSymbol();
                lstyle[0] = build("conduit", &cd);
                lstyle[1] = build("pump", &pm);
                lstyle[2] = build("orifice", &orf);
                lstyle[3] = build("weir", &wr);
                lstyle[4] = build("outlet", nullptr); // outlets share the prefs outlet pen
            }
            // Selection halo is a uniform +1 px cue over the conduit pen.
            const float selHW =
                (float(m_layer->conduitSymbol().size > 0.0 ? m_layer->conduitSymbol().size : 1.0) +
                 1.0f) *
                invView;

            // Per-kind flow-direction arrow config (GPU mirror of the CPU arrow
            // pass in SWMMLayerItem::paint). Styling comes from each kind's
            // SWMMElementSymbol: showArrows / arrowSize (px) / arrowColor /
            // arrowOnlyWhenFlowPos. Arrowheads collect into a separate buffer
            // and are appended to the link buffer after the segment loop so they
            // draw last (over the links). The arrow points along the polyline
            // from→to direction, which is upstream→downstream.
            struct ArrowStyle {
                bool on;
                bool onlyFlowPos;
                float lenScene;
                float widScene;
                uchar r, g, b, a;
            };
            ArrowStyle astyle[5];
            {
                // CPU-parity — arrows fade with the kind's (sub-layer)
                // opacity like the base links do.
                auto buildA = [&](const SWMMElementSymbol &s, SWMMModelLayer::Category cat) {
                    QColor c = s.arrowColor;
                    const qreal kop = m_layer->categoryOpacity(cat);
                    if (kop < 1.0 && c.isValid())
                        c.setAlphaF(c.alphaF() * kop);
                    const uchar aa = uchar(c.alpha());
                    return ArrowStyle{s.showArrows,
                                      s.arrowOnlyWhenFlowPos,
                                      float(s.arrowSize * invView),
                                      float(s.arrowWidth * invView),
                                      premul(uchar(c.red()), aa),
                                      premul(uchar(c.green()), aa),
                                      premul(uchar(c.blue()), aa),
                                      aa};
                };
                astyle[0] = buildA(m_layer->conduitSymbol(), kLinkCat[0]);
                astyle[1] = buildA(m_layer->pumpSymbol(), kLinkCat[1]);
                astyle[2] = buildA(m_layer->orificeSymbol(), kLinkCat[2]);
                astyle[3] = buildA(m_layer->weirSymbol(), kLinkCat[3]);
                astyle[4] = buildA(m_layer->m_outletSym, kLinkCat[4]);
            }
            const bool anyArrows =
                astyle[0].on || astyle[1].on || astyle[2].on || astyle[3].on || astyle[4].on;
            std::vector<QSGGeometry::ColoredPoint2D> arrowTri;

            // Diagnostic, opt-in via SWMMVIS_LOG_LINK_DRAW=1 (same idiom as
            // SWMMVIS_LOG_REDRAW / OPENSWMM_2D_RENDER_DEBUG). Reports, once
            // per frame, why the LAST link in the SoA — the one a conduit
            // split appends — is or is not emitted. Answers in one run
            // whether a vanished split half is missing geometry, hidden,
            // culled, or absent from the arrays altogether.
            static const bool kLogLinkDraw =
                qEnvironmentVariableIntValue("SWMMVIS_LOG_LINK_DRAW") > 0;
            if (kLogLinkDraw && !counts.empty()) {
                const size_t t = counts.size() - 1;
                const bool hasBB = int(t) < lBboxes.size();
                const QRectF bb = hasBB ? lBboxes[int(t)] : QRectF();
                const bool culled = hasBB && (bb.right() < cullX0 || bb.left() > cullX1 ||
                                              bb.bottom() < cullY0 || bb.top() > cullY1);
                qInfo().nospace() << "[linkdraw] tail=" << t
                                  << " renderLinks=" << m_layer->renderLinkCount()
                                  << " soaLinks=" << m_layer->cachedLinkCount() << " name="
                                  << (int(t) < links.size() ? links[int(t)].name
                                                            : QStringLiteral("<none>"))
                                  << " vcount=" << counts[t]
                                  << " hidden=" << (t < lHid.size() ? int(lHid[t]) : -1)
                                  << " bbox=" << bb << " cull=[" << cullX0 << "," << cullY0
                                  << " .. " << cullX1 << "," << cullY1 << "]"
                                  << " culled=" << culled << " drawn="
                                  << (counts[t] >= 2 && !(t < lHid.size() && lHid[t]) && !culled);
            }

            std::vector<QSGGeometry::ColoredPoint2D> baseTri;
            std::vector<QSGGeometry::Point2D> selTri;
            size_t baseSegs = 0, selSegs = 0;
            size_t baseLinks = 0, selLinks = 0;
            for (size_t i = 0; i < counts.size(); ++i) {
                if (counts[i] < 2)
                    continue;
                if (i < lHid.size() && lHid[i])
                    continue;
                if (size_t(i) < size_t(lBboxes.size())) {
                    const QRectF &bb = lBboxes[int(i)];
                    if (bb.right() < cullX0 || bb.left() > cullX1 || bb.bottom() < cullY0 ||
                        bb.top() > cullY1)
                        continue;
                }
                baseSegs += counts[i] - 1;
                ++baseLinks;
                if (i < lSel.size() && lSel[i]) {
                    selSegs += counts[i] - 1;
                    ++selLinks;
                }
            }
            // Six vertices per segment, at most three per bevel, and two
            // semicircular caps per visible link.
            constexpr auto capVerts = OpenSWMM::Render::kLinkCapSegments * 6;
            baseTri.reserve(baseSegs * 9 + baseLinks * capVerts);
            selTri.reserve(selSegs * 9 + selLinks * capVerts);

            for (size_t i = 0; i < counts.size(); ++i) {
                const uint32_t cnt = counts[i];
                if (cnt < 2)
                    continue;
                if (i < lHid.size() && lHid[i])
                    continue;
                if (size_t(i) < size_t(lBboxes.size())) {
                    const QRectF &bb = lBboxes[int(i)];
                    if (bb.right() < cullX0 || bb.left() > cullX1 || bb.bottom() < cullY0 ||
                        bb.top() > cullY1)
                        continue;
                }
                // Per-feature colour: link type → category → featureColor(cat,i)
                // (m_kindFeatureColors is SoA-indexed, so `i` is the key).
                const int lt = (int(i) < links.size() && links[int(i)].linkType >= 0 &&
                                links[int(i)].linkType < 5)
                                   ? links[int(i)].linkType
                                   : 0;
                const QColor fc = m_layer->featureColor(kLinkCat[lt], int(i));
                const QColor lc = fc.isValid() ? fc : lstyle[lt].color;
                // Per-feature stroke width, falling back to the per-type pen.
                // For LINE archetypes the renderer writes its width axis into
                // the symbol's "width" prop, and rebuildKindFeatureColors
                // stores that in the size channel (it reads "size" first, then
                // "width"), so featureSize() is the width for link kinds.
                // Without this, "Width by value" on a Graduated conduit theme
                // was computed, cached and then silently dropped here — every
                // link drew at its kind's fixed pen width.
                const double wOv = m_layer->featureSize(kLinkCat[lt], int(i));
                const float hw = (wOv > 0.0) ? float(wOv * invView) : lstyle[lt].hw;
                const qreal lkop = m_layer->categoryOpacity(kLinkCat[lt]); // per-kind opacity
                const uchar lA = uchar(lkop < 1.0 ? lc.alpha() * lkop : lc.alpha());
                const uchar lR = premul(uchar(lc.red()), lA), lG = premul(uchar(lc.green()), lA),
                            lB = premul(uchar(lc.blue()), lA);
                const double *p = flat.data() + size_t(offsets[i]) * 2;
                const bool sel = i < lSel.size() && lSel[i];
                OpenSWMM::Render::appendLinkStrokeTriangles(p, cnt, ox, oy, hw, [&](QPointF point) {
                    QSGGeometry::ColoredPoint2D v;
                    v.set(float(point.x()), float(point.y()), lR, lG, lB, lA);
                    baseTri.push_back(v);
                });
                if (sel)
                    OpenSWMM::Render::appendLinkStrokeTriangles(
                        p, cnt, ox, oy, selHW, [&](QPointF point) {
                            QSGGeometry::Point2D v;
                            v.set(float(point.x()), float(point.y()));
                            selTri.push_back(v);
                        });
                // Flow-direction arrowhead (collected separately, drawn last).
                if (anyArrows && astyle[lt].on &&
                    !(astyle[lt].onlyFlowPos && m_layer->linkFlow(int(i)) <= 0.0))
                    appendFlowArrowColored(arrowTri, p, cnt, ox, oy, astyle[lt].lenScene,
                                           astyle[lt].widScene, astyle[lt].r, astyle[lt].g,
                                           astyle[lt].b, astyle[lt].a);
            }
            // Append arrows last so they overlay the link segments (matches the
            // CPU SWMMLayerItem draw order).
            baseTri.insert(baseTri.end(), arrowTri.begin(), arrowTri.end());
            uploadColoredVerts(lines, baseTri); // #33 Stage 1b — per-feature
            uploadVerts(linesSel, selTri);
        } else {
            uploadColoredVerts(lines, {}); // #33 Stage 1b — colored node
            uploadVerts(linesSel, {});
        }

        // ---- Nodes ---------------------------------------------------------
        if (m_layer->showNodes() && m_layer->qsgOwnsKind(SWMMModelLayer::QsgNodes)) {
            constexpr float kMinPx = 1.0f;
            // §QSG-3 — vertex-coloured buckets. Each glyph emits 3..24
            // verts in its kind's bucket with the chosen colour baked
            // into each vertex. Selected nodes use selBrushColor;
            // unselected nodes use the kind's fillColor (no per-feature
            // override yet — wire to layer->featureColor() in a later
            // pass).
            std::vector<QSGGeometry::ColoredPoint2D> junc, outf, stor, divr;
            const uchar selR = uchar(selNode.red());
            const uchar selG = uchar(selNode.green());
            const uchar selB = uchar(selNode.blue());
            const uchar selA = uchar(selNode.alpha());
            auto unpack = [](const QColor &c, uchar &r, uchar &g, uchar &b, uchar &a) {
                r = uchar(c.red());
                g = uchar(c.green());
                b = uchar(c.blue());
                a = uchar(c.alpha());
            };
            uchar jR, jG, jB, jA;
            unpack(m_layer->junctionSymbol().fillColor, jR, jG, jB, jA);
            uchar oR, oG, oB, oA;
            unpack(m_layer->outfallSymbol().fillColor, oR, oG, oB, oA);
            uchar sR, sG, sB, sA;
            unpack(m_layer->storageSymbol().fillColor, sR, sG, sB, sA);
            uchar dR, dG, dB, dA;
            unpack(m_layer->dividerSymbol().fillColor, dR, dG, dB, dA);
            uchar vR, vG, vB, vA;
            unpack(m_layer->m_virtualJunctionSym.fillColor, vR, vG, vB, vA);
            uchar iR, iG, iB, iA;
            unpack(m_layer->m_inletJunctionSym.fillColor, iR, iG, iB, iA);
            // Per-kind marker shape, looked up once per frame to keep
            // the inner loop branch-free on the symbol struct.
            const auto jShape = m_layer->junctionSymbol().markerShape;
            const auto oShape = m_layer->outfallSymbol().markerShape;
            const auto sShape = m_layer->storageSymbol().markerShape;
            const auto dShape = m_layer->dividerSymbol().markerShape;
            const auto vShape = m_layer->m_virtualJunctionSym.markerShape;
            const auto iShape = m_layer->m_inletJunctionSym.markerShape;
            const auto &nps = m_layer->m_nodeScenePts;
            const auto &nodes = m_layer->m_nodes;
            const auto &nHid = m_layer->m_nodeHiddenFlag;
            const auto &nSel = m_layer->m_nodeSelectedFlag;
            for (int i = 0; i < nodes.size(); ++i) {
                if (size_t(i) < nHid.size() && nHid[i])
                    continue;
                if (i >= nps.size())
                    continue;
                const QPointF &p = nps[i];
                if (p.x() < cullX0 || p.x() > cullX1 || p.y() < cullY0 || p.y() > cullY1)
                    continue;
                const int nt =
                    (nodes[i].nodeType >= 0 && nodes[i].nodeType < 4) ? nodes[i].nodeType : 0;
                float pxR = float(m_layer->junctionSymbol().size) * 0.5f;
                if (nt == 1)
                    pxR = float(m_layer->outfallSymbol().size) * 0.5f;
                else if (nt == 2)
                    pxR = float(m_layer->storageSymbol().size) * 0.5f;
                else if (nt == 3)
                    pxR = float(m_layer->dividerSymbol().size) * 0.5f;
                const float fx = float(p.x() - ox), fy = float(p.y() - oy);
                auto *bucket = &junc;
                uchar cR = jR, cG = jG, cB = jB, cA = jA;
                auto shape = jShape;
                SWMMModelLayer::Category cat = SWMMModelLayer::CatJunctions;
                if (nt == 1) {
                    bucket = &outf;
                    cR = oR;
                    cG = oG;
                    cB = oB;
                    cA = oA;
                    shape = oShape;
                    cat = SWMMModelLayer::CatOutfalls;
                } else if (nt == 2) {
                    bucket = &stor;
                    cR = sR;
                    cG = sG;
                    cB = sB;
                    cA = sA;
                    shape = sShape;
                    cat = SWMMModelLayer::CatStorage;
                } else if (nt == 3) {
                    bucket = &divr;
                    cR = dR;
                    cG = dG;
                    cB = dB;
                    cA = dA;
                    shape = dShape;
                    cat = SWMMModelLayer::CatDividers;
                }
                // Virtual junctions — same bucket/category (D-G1: no
                // persisted 5th category), the virtual symbol's dot; the
                // distinguishing dotted ring is emitted after the glyph.
                // Inlet junctions carry BOTH flags, so they are tested first
                // and take the inlet symbol (and no dotted ring — the diamond
                // is what sets them apart).
                const bool isIJ = (nt == 0 && nodes[i].isInlet);
                const bool isVJ = (nt == 0 && nodes[i].isVirtual && !isIJ);
                if (isIJ) {
                    pxR = float(m_layer->m_inletJunctionSym.size) * 0.5f;
                    cR = iR;
                    cG = iG;
                    cB = iB;
                    cA = iA;
                    shape = iShape;
                } else if (isVJ) {
                    pxR = float(m_layer->m_virtualJunctionSym.size) * 0.5f;
                    cR = vR;
                    cG = vG;
                    cB = vB;
                    cA = vA;
                    shape = vShape;
                }
                // CPU-parity — per-feature SIZE override (Graduated "size by
                // value") and SHAPE override (Categorized / Rule-based). The
                // CPU painter honoured both (swmmlayeritem.cpp node loop);
                // this path rendered every node of a kind at the uniform
                // struct size/shape, so the output-size axis showed only on
                // the QPainter path. Negative sentinels = no override.
                {
                    const double szOv = m_layer->featureSize(cat, i);
                    if (szOv > 0.0)
                        pxR = float(szOv) * 0.5f;
                    const int shOv = m_layer->featureShape(cat, i);
                    if (shOv >= 0)
                        shape = static_cast<OpenSWMM::Render::MarkerShape>(shOv);
                }
                if (pxR < kMinPx)
                    continue;
                const float r = pxR * invView;
                // #33 Stage 1 (X3) — per-feature override colour from the
                // renderer-driven cache (Graduated / Categorized). `i` is the
                // node SoA index, which is exactly what featureColor() is keyed
                // by (m_kindFeatureColors is SoA-indexed). Invalid → no override
                // → keep the kind's struct colour above. This is what makes the
                // GPU path match the CPU SWMMLayerItem path.
                {
                    const QColor ov = m_layer->featureColor(cat, i);
                    if (ov.isValid()) {
                        cR = uchar(ov.red());
                        cG = uchar(ov.green());
                        cB = uchar(ov.blue());
                        cA = uchar(ov.alpha());
                    }
                    // Per-kind (sub-layer) opacity — fade this kind's alpha.
                    const qreal kop = m_layer->categoryOpacity(cat);
                    if (kop < 1.0)
                        cA = uchar(cA * kop);
                }
                // Selection: replace the per-kind/per-feature base colour with
                // the selection brush colour. Same shape and size, just a
                // recolour — matches CPU painter behaviour.
                if (size_t(i) < nSel.size() && nSel[i]) {
                    cR = selR;
                    cG = selG;
                    cB = selB;
                    cA = selA;
                }
                appendNodeGlyphTrianglesColored(*bucket, fx, fy, r, shape, premul(cR, cA),
                                                premul(cG, cA), premul(cB, cA), cA);
                const SWMMElementSymbol *symbol = &m_layer->m_junctionSym;
                if (nt == 1)
                    symbol = &m_layer->m_outfallSym;
                else if (nt == 2)
                    symbol = &m_layer->m_storageSym;
                else if (nt == 3)
                    symbol = &m_layer->m_dividerSym;
                else if (isIJ)
                    symbol = &m_layer->m_inletJunctionSym;
                else if (isVJ)
                    symbol = &m_layer->m_virtualJunctionSym;
                QColor outline = symbol->outlineColor;
                float outlineWidth = float(symbol->outlineWidth);
                if (size_t(i) < nSel.size() && nSel[i]) {
                    const QPen selection =
                        PreferencesManager::instance()->selectionPen(QStringLiteral("node"));
                    outline = selection.color();
                    outlineWidth = float(selection.widthF());
                } else {
                    outline.setAlphaF(outline.alphaF() * m_layer->categoryOpacity(cat));
                }
                appendMarkerOutlineColored(*bucket, fx, fy, r, outlineWidth * invView, shape,
                                           outline);
                // Virtual-junction dotted ring — small dots encircling the
                // glyph with a gap (CPU-parity with SWMMLayerItem's cosmetic
                // DotLine ellipse; triangles are all this path can draw).
                if (isVJ) {
                    const float ringR = (pxR + 3.0f) * invView;
                    const float dotR = 0.8f * invView;
                    constexpr int kRingDots = 12;
                    for (int k = 0; k < kRingDots; ++k) {
                        const float ang = float(k) * (2.0f * float(M_PI) / kRingDots);
                        appendNodeGlyphTrianglesColored(
                            *bucket, fx + ringR * std::cos(ang), fy + ringR * std::sin(ang), dotR,
                            OpenSWMM::Render::MarkerShape::Circle, premul(cR, cA), premul(cG, cA),
                            premul(cB, cA), cA);
                    }
                }
            }
            uploadColoredVerts(junctionsBase, junc);
            uploadColoredVerts(outfallsBase, outf);
            uploadColoredVerts(storageBase, stor);
            uploadColoredVerts(dividersBase, divr);
            // nodesSel is no longer used for nodes — keep it empty so
            // the QSG tree slot stays consistent for the else-branch
            // child lookup but draws nothing.
            uploadColoredVerts(nodesSel, {});
        } else {
            uploadColoredVerts(junctionsBase, {});
            uploadColoredVerts(outfallsBase, {});
            uploadColoredVerts(storageBase, {});
            uploadColoredVerts(dividersBase, {});
            uploadColoredVerts(nodesSel, {});
        }

        // ---- Gages ---------------------------------------------------------
        if (m_layer->showRainGages() && m_layer->qsgOwnsKind(SWMMModelLayer::QsgGages)) {
            constexpr float kMinPx = 1.0f;
            const float gagePxR = float(m_layer->rainGageSymbol().size) * 0.5f;
            const auto gShape = m_layer->rainGageSymbol().markerShape;
            const QColor gFill = m_layer->rainGageSymbol().fillColor;
            const qreal gKop = m_layer->categoryOpacity(SWMMModelLayer::CatRainGages);
            std::vector<QSGGeometry::ColoredPoint2D> base;
            std::vector<QSGGeometry::Point2D> sel;
            {
                const auto &gps = m_layer->m_gageScenePts;
                const auto &gages = m_layer->m_gages;
                const auto &gHid = m_layer->m_gageHiddenFlag;
                const auto &gSel = m_layer->m_gageSelectedFlag;
                for (int i = 0; i < gages.size(); ++i) {
                    if (size_t(i) < gHid.size() && gHid[i])
                        continue;
                    if (i >= gps.size())
                        continue;
                    const QPointF &p = gps[i];
                    if (p.x() < cullX0 || p.x() > cullX1 || p.y() < cullY0 || p.y() > cullY1)
                        continue;
                    // CPU-parity — per-gage renderer overrides: size from
                    // featureSize (Graduated "size by value"), colour from
                    // featureColor (Graduated / Categorized), faded by the
                    // kind opacity. Negative / invalid sentinels = struct.
                    float pxR = gagePxR;
                    const double szOv = m_layer->featureSize(SWMMModelLayer::CatRainGages, i);
                    if (szOv > 0.0)
                        pxR = float(szOv) * 0.5f;
                    if (pxR < kMinPx)
                        continue;
                    const float r = pxR * invView;
                    QColor c = m_layer->featureColor(SWMMModelLayer::CatRainGages, i);
                    if (!c.isValid())
                        c = gFill;
                    if (gKop < 1.0)
                        c.setAlphaF(c.alphaF() * gKop);
                    const float fx = float(p.x() - ox), fy = float(p.y() - oy);
                    {
                        const uchar ga = uchar(c.alpha());
                        appendNodeGlyphTrianglesColored(
                            base, fx, fy, r, gShape, premul(uchar(c.red()), ga),
                            premul(uchar(c.green()), ga), premul(uchar(c.blue()), ga), ga);
                        QColor outline = m_layer->rainGageSymbol().outlineColor;
                        outline.setAlphaF(outline.alphaF() * gKop);
                        appendMarkerOutlineColored(base, fx, fy, r,
                                                   float(m_layer->rainGageSymbol().outlineWidth) *
                                                       invView,
                                                   gShape, outline);
                    }
                    if (size_t(i) < gSel.size() && gSel[i])
                        appendGageTriangles(sel, fx, fy, r, gShape); // §QSG-3 — same size as base
                }
            }
            uploadColoredVerts(gagesBase, base);
            uploadVerts(gagesSel, sel);
        } else {
            uploadColoredVerts(gagesBase, {});
            uploadVerts(gagesSel, {});
        }

        m_contentDirty = false;
        m_selDirty = false; // base rebuild covers selection too
        // Stamp the extent so setMapExtent() can decide pan-only vs.
        // full rebuild based on how far the next viewport drifts.
        m_lastBuiltExtent = m_extent;

        // ---- Selection-only rebuild (base geometry unchanged) ------------------
    } else if (m_selDirty) {
        // Only the 5 overlay buffers need updating. The 8 base-geometry
        // buffers (fills, edges, base glyphs, base lines) are untouched.
        const float selEdgeHW = 1.5f * invView;
        const float selHW = 2.0f * invView;
        // Selection style — pens carry outline colour, brushes carry
        // fill colour (incl. alpha). selPoly is the polygon outline;
        // selPolyFill, selNode, selGage are fills. selLine is the link
        // halo's stroke colour. cap/join/width from the pens aren't
        // representable on a QSGFlatColorMaterial.
        const QColor selPoly = selPenColor("subcatchment");
        const QColor selPolyFill = selBrushColor("subcatchment");
        const QColor selNode = selBrushColor("node");
        const QColor selGage = selBrushColor("gage");
        const QColor selLine = selPenColor("link");

        // Subcatchment selection overlays
        if (m_layer->showSubcatchments() && m_layer->qsgOwnsKind(SWMMModelLayer::QsgCatch)) {
            const auto &cps = m_layer->m_catchScenePts;
            const auto &cBboxes = m_layer->m_catchSceneBBoxes;
            const auto &cHid = m_layer->m_catchHiddenFlag;
            const auto &cSel = m_layer->m_catchSelectedFlag;
            std::vector<QSGGeometry::Point2D> fillSel, edgeSelTris;
            for (int i = 0; i < cps.size(); ++i) {
                if (size_t(i) < cSel.size() && !cSel[i])
                    continue;
                if (size_t(i) < cHid.size() && cHid[i])
                    continue;
                if (size_t(i) < size_t(cBboxes.size())) {
                    const QRectF &bb = cBboxes[i];
                    if (bb.right() < cullX0 || bb.left() > cullX1 || bb.bottom() < cullY0 ||
                        bb.top() > cullY1)
                        continue;
                }
                const auto &poly = cps[i];
                if (poly.size() < 3 || size_t(i) >= size_t(m_catchTriCache.tris.size()))
                    continue;
                const auto &tris = m_catchTriCache.tris[i];
                for (const QPointF &point : tris) {
                    QSGGeometry::Point2D p;
                    p.x = float(point.x() - ox);
                    p.y = float(point.y() - oy);
                    fillSel.push_back(p);
                }
                for (const auto &ring : OpenSWMM::Render::SubcatchmentGeometry::rings(poly))
                    for (int j = ring.first; j < ring.first + ring.count; ++j) {
                        const float ax = float(poly[j].x() - ox), ay = float(poly[j].y() - oy);
                        const float bx =
                            float(poly[ring.first + (j - ring.first + 1) % ring.count].x() - ox);
                        const float by =
                            float(poly[ring.first + (j - ring.first + 1) % ring.count].y() - oy);
                        appendThickSegment(edgeSelTris, ax, ay, bx, by, selEdgeHW);
                    }
            }
            setNodeColor(catchSelFill, selPolyFill);
            setNodeColor(catchSelEdge, selPoly);
            uploadVerts(catchSelFill, fillSel);
            uploadVerts(catchSelEdge, edgeSelTris);
        }

        // Link selection overlay
        if (m_layer->showLinks() && m_layer->qsgOwnsKind(SWMMModelLayer::QsgLinks)) {
            const std::vector<double> &flat = m_layer->m_linkSceneFlat;
            const std::vector<uint32_t> &offsets = m_layer->m_linkVertexOffset;
            const std::vector<uint32_t> &counts = m_layer->m_linkVertexCount;
            const auto &lSel = m_layer->m_linkSelectedFlag;
            const auto &lHid = m_layer->m_linkHiddenFlag;
            const QVector<QRectF> &lBboxes = m_layer->m_linkSceneBBoxes;
            std::vector<QSGGeometry::Point2D> selTri;
            for (size_t i = 0; i < counts.size(); ++i) {
                if (i >= lSel.size() || !lSel[i])
                    continue;
                if (counts[i] < 2)
                    continue;
                if (i < lHid.size() && lHid[i])
                    continue;
                if (size_t(i) < size_t(lBboxes.size())) {
                    const QRectF &bb = lBboxes[int(i)];
                    if (bb.right() < cullX0 || bb.left() > cullX1 || bb.bottom() < cullY0 ||
                        bb.top() > cullY1)
                        continue;
                }
                const double *p = flat.data() + size_t(offsets[i]) * 2;
                const uint32_t cnt = counts[i];
                OpenSWMM::Render::appendLinkStrokeTriangles(
                    p, cnt, ox, oy, selHW, [&](QPointF point) {
                        QSGGeometry::Point2D v;
                        v.set(float(point.x()), float(point.y()));
                        selTri.push_back(v);
                    });
            }
            setNodeColor(linesSel, selLine);
            uploadVerts(linesSel, selTri);
        }

        // Node selection overlay
        if (m_layer->showNodes() && m_layer->qsgOwnsKind(SWMMModelLayer::QsgNodes)) {
            const auto &nps = m_layer->m_nodeScenePts;
            const auto &nodes = m_layer->m_nodes;
            const auto &nHid = m_layer->m_nodeHiddenFlag;
            const auto &nSel = m_layer->m_nodeSelectedFlag;
            std::vector<QSGGeometry::ColoredPoint2D> sel;
            const uchar selR = uchar(selNode.red());
            const uchar selG = uchar(selNode.green());
            const uchar selB = uchar(selNode.blue());
            const uchar selA = uchar(selNode.alpha());
            for (int i = 0; i < nodes.size(); ++i) {
                if (size_t(i) >= nSel.size() || !nSel[i])
                    continue;
                if (size_t(i) < nHid.size() && nHid[i])
                    continue;
                if (i >= nps.size())
                    continue;
                const QPointF &p = nps[i];
                if (p.x() < cullX0 || p.x() > cullX1 || p.y() < cullY0 || p.y() > cullY1)
                    continue;
                const int nt =
                    (nodes[i].nodeType >= 0 && nodes[i].nodeType < 4) ? nodes[i].nodeType : 0;
                float pxR = float(m_layer->junctionSymbol().size) * 0.5f;
                auto shape = m_layer->junctionSymbol().markerShape;
                if (nt == 1) {
                    pxR = float(m_layer->outfallSymbol().size) * 0.5f;
                    shape = m_layer->outfallSymbol().markerShape;
                } else if (nt == 2) {
                    pxR = float(m_layer->storageSymbol().size) * 0.5f;
                    shape = m_layer->storageSymbol().markerShape;
                } else if (nt == 3) {
                    pxR = float(m_layer->dividerSymbol().size) * 0.5f;
                    shape = m_layer->dividerSymbol().markerShape;
                }
                if (pxR < 1.0f)
                    continue;
                appendNodeGlyphTrianglesColored(sel, float(p.x() - ox), float(p.y() - oy),
                                                pxR * invView, shape, premul(selR, selA),
                                                premul(selG, selA), premul(selB, selA), selA);
            }
            uploadColoredVerts(nodesSel, sel);
        }

        // Gage selection overlay
        if (m_layer->showRainGages() && m_layer->qsgOwnsKind(SWMMModelLayer::QsgGages)) {
            const auto &gps = m_layer->m_gageScenePts;
            const auto &gages = m_layer->m_gages;
            const auto &gHid = m_layer->m_gageHiddenFlag;
            const auto &gSel = m_layer->m_gageSelectedFlag;
            // §QSG-3 — same size as base.
            const float r = float(m_layer->rainGageSymbol().size) * 0.5f * invView;
            const auto gShape = m_layer->rainGageSymbol().markerShape;
            std::vector<QSGGeometry::Point2D> sel;
            for (int i = 0; i < gages.size(); ++i) {
                if (size_t(i) >= gSel.size() || !gSel[i])
                    continue;
                if (size_t(i) < gHid.size() && gHid[i])
                    continue;
                if (i >= gps.size())
                    continue;
                const QPointF &p = gps[i];
                if (p.x() < cullX0 || p.x() > cullX1 || p.y() < cullY0 || p.y() > cullY1)
                    continue;
                appendGageTriangles(sel, float(p.x() - ox), float(p.y() - oy), r, gShape);
            }
            setNodeColor(gagesSel, selGage);
            uploadVerts(gagesSel, sel);
        }

        m_selDirty = false;
    }

    // ---- Transform (always — pan changes only the translate) ---------------
    const float msx = float(width()) / float(m_extent.width());
    const float msy = float(height()) / float(m_extent.height());
    QMatrix4x4 mat;
    mat.scale(msx, msy);
    mat.translate(float(m_anchorX - m_extent.xMin()), float(m_anchorY + m_extent.yMax()));
    if (root->matrix() != mat)
        root->setMatrix(mat);

    if (sampler().enabled)
        sampler().record(frameTimer.elapsed(), wasRebuild);

    return root;
}
