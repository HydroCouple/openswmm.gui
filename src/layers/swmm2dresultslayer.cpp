/*!
 * \file   swmm2dresultslayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice CF.MVP — 2D inundation results layer implementation. The graphics
 * item closely mirrors `SWMM2DMeshGraphicsItem` from
 * [src/layers/swmm2dmeshlayer.cpp]; only the per-triangle colour function
 * differs (depth → inundation ramp instead of elevation → terrain ramp).
 */
#include "layers/swmm2dresultslayer.h"

#include "contour/contourchain.h"
#include "contour/marchingtriangles.h"
#include "core/swmmdatetime.h"
#include "io/mesh2dh5reader.h"
#include "map/mapextent.h"

#include "render/ifeaturerenderer.h"
#include "render/labelpainter.h"
#include "render/renderers/graduatedrenderer.h"
// Slice B.5b — Rule Model mirror for 2D results layers.
#include "render/rastersymbollayers.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/rule.h"
#include "render/rulelist.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"
// Slice Z.14-paint — polygon clip mask.
#include "render/maskclipresolver.h"
#include "ui/dialogs/ilayerstylesubject.h"

#include <QDateTime>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMultiHash>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

// ---------------------------------------------------------------------------
// Inundation colour ramp — dry → light cyan → cyan → blue → magenta → yellow
// Stops chosen so wet areas pop visually against the terrain-ramped mesh
// layer below (which is heavily green/brown).
// ---------------------------------------------------------------------------

namespace {

struct Stop { double t; int r, g, b; };
constexpr Stop kInundationStops[] = {
    {0.00, 0x9d, 0xe2, 0xf2},  // light cyan — barely wet
    {0.20, 0x1e, 0x88, 0xe5},  // bright blue
    {0.50, 0x05, 0x3c, 0x8a},  // deep navy
    {0.75, 0xc6, 0x28, 0x28},  // crimson — danger
    {1.00, 0xff, 0xeb, 0x3b},  // yellow — saturated
};

void inundationColorRgba(double depth, double dry_depth, double max_depth,
                          int& r, int& g, int& b, int& a)
{
    if (depth < dry_depth || max_depth <= dry_depth) {
        r = g = b = 0;
        a = 0;
        return;
    }
    const double t = std::clamp((depth - dry_depth) / (max_depth - dry_depth),
                                 0.0, 1.0);
    constexpr int N = int(sizeof(kInundationStops) / sizeof(*kInundationStops));
    int i = 0;
    while (i < N - 2 && kInundationStops[i + 1].t <= t) ++i;
    const Stop& lo = kInundationStops[i];
    const Stop& hi = kInundationStops[i + 1];
    const double f = (hi.t > lo.t) ? (t - lo.t) / (hi.t - lo.t) : 0.0;
    r = int(std::lround(lo.r + f * (hi.r - lo.r)));
    g = int(std::lround(lo.g + f * (hi.g - lo.g)));
    b = int(std::lround(lo.b + f * (hi.b - lo.b)));
    a = 210;
}

// CF.2 — sequential velocity-magnitude ramp (Viridis-inspired). Distinct
// hues from the depth ramp so arrows read clearly against the wet fill.
constexpr Stop kVelocityStops[] = {
    {0.00, 0x44, 0x01, 0x54},  // dark purple
    {0.30, 0x35, 0x60, 0x8d},  // teal-blue
    {0.60, 0x21, 0x90, 0x8c},  // teal
    {0.85, 0x5e, 0xc9, 0x62},  // lime
    {1.00, 0xfd, 0xe7, 0x25},  // yellow
};

void velocityColorRgb(double vmag, double max_v, int& r, int& g, int& b)
{
    const double tv = (max_v > 1e-9)
                        ? std::clamp(vmag / max_v, 0.0, 1.0)
                        : 0.0;
    constexpr int N = int(sizeof(kVelocityStops) / sizeof(*kVelocityStops));
    int i = 0;
    while (i < N - 2 && kVelocityStops[i + 1].t <= tv) ++i;
    const Stop& lo = kVelocityStops[i];
    const Stop& hi = kVelocityStops[i + 1];
    const double f = (hi.t > lo.t) ? (tv - lo.t) / (hi.t - lo.t) : 0.0;
    r = int(std::lround(lo.r + f * (hi.r - lo.r)));
    g = int(std::lround(lo.g + f * (hi.g - lo.g)));
    b = int(std::lround(lo.b + f * (hi.b - lo.b)));
}

int normalizeOneBasedConnectivity(std::vector<std::array<int, 3>>& tris,
                                  int nVerts)
{
    if (tris.empty() || nVerts <= 0) return 0;

    int minIdx = std::numeric_limits<int>::max();
    int maxIdx = std::numeric_limits<int>::lowest();
    for (const auto& tri : tris) {
        minIdx = std::min({minIdx, tri[0], tri[1], tri[2]});
        maxIdx = std::max({maxIdx, tri[0], tri[1], tri[2]});
    }

    if (minIdx != 1 || maxIdx != nVerts)
        return 0;

    for (auto& tri : tris)
        for (int& v : tri)
            --v;
    return 1;
}

bool triIndicesInRange(const std::array<int, 3>& tri, int nVerts)
{
    return tri[0] >= 0 && tri[0] < nVerts
        && tri[1] >= 0 && tri[1] < nVerts
        && tri[2] >= 0 && tri[2] < nVerts
        && tri[0] != tri[1] && tri[1] != tri[2] && tri[2] != tri[0];
}

double twiceSignedArea(const QPointF& a, const QPointF& b, const QPointF& c)
{
    return (b.x() - a.x()) * (c.y() - a.y())
         - (b.y() - a.y()) * (c.x() - a.x());
}

} // namespace

// ---------------------------------------------------------------------------
// SWMM2DResultsGraphicsItem
// ---------------------------------------------------------------------------

class SWMM2DResultsGraphicsItem : public QGraphicsItem
{
public:
    explicit SWMM2DResultsGraphicsItem(SWMM2DResultsLayer* layer,
                                        QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), layer_(layer)
    {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
        setCacheMode(QGraphicsItem::NoCache);
        setZValue(layer->layerZValue());
    }

    void geometryChanged() { prepareGeometryChange(); update(); }

    QRectF boundingRect() const override { return layer_->m_sceneBBox; }

    void paint(QPainter* p,
               const QStyleOptionGraphicsItem* option,
               QWidget*) override
    {
        if (!layer_->isVisible()) return;
        // VS.8 — when the QSG renderer owns this layer's pixels the CPU
        // passes stand down (SWMMLayerItem §QSG-1 bypass pattern).
        if (layer_->qsgOwnsRendering()) return;
        const auto& tris = layer_->m_sceneTris;
        if (tris.isEmpty()) return;

        const QRectF exposed  = option->exposedRect;
        const double dryDepth = layer_->dryDepth();
        const double maxDepth = layer_->maxDepth();

        // Phase 9 (2026-05-25) — sublayer.isVisible() is the authoritative
        // gate for each paint pass, replacing the legacy `filledContours()`
        // / `isolines()` / `velocityVectorsVisible()` booleans as the
        // primary source of truth. Toggling a sublayer in the layer tree
        // immediately turns the corresponding paint pass on/off (MVC
        // principle: one model owns visibility; multiple views observe).
        // The legacy booleans are still consulted alongside so any UI
        // wiring that flips them via the old setters keeps working
        // (they default to true, except filledContours/isolines/velocity
        // which default to false — the same defaults the new sublayer
        // mix carries).
        auto *bandSub    = layer_->contourBandSublayer();
        auto *isolineSub = layer_->isolineSublayer();

        const bool paintBands   = ( bandSub    && bandSub->isVisible());
        const bool paintIsolines= ( isolineSub && isolineSub->isVisible());

        p->save();
        p->setPen(Qt::NoPen);

        // Slice Z.14-paint — install clip from the layer's MaskSpec.
        // resolveMaskClip returns ok=false on disabled/unresolvable, in
        // which case we paint unclipped. Wrapping save()/restore() bracket
        // ensures the clip vanishes on exit.
        {
            const auto clip = OpenSWMM::Render::resolveMaskClip(
                layer_, layer_->maskSpec());
            if (clip.ok && !clip.path.isEmpty()) {
                if (clip.mode == OpenSWMM::Render::MaskMode::ClipInside) {
                    p->setClipPath(clip.path, Qt::IntersectClip);
                } else {
                    QPainterPath all;
                    all.addRect(exposed.isNull()
                                    ? layer_->m_sceneBBox.adjusted(-1, -1, 1, 1)
                                    : exposed);
                    p->setClipPath(all.subtracted(clip.path),
                                    Qt::IntersectClip);
                }
            }
        }

        // --- Pass 1 (depth fill): the depth color ramp heatmap was removed
        // (2026-06-21, redundant with contour bands). Contour bands (Pass 2
        // below) now provide the depth fill; dry cells stay unpainted so the
        // SWMM2DMeshLayer terrain shows through.

        // --- Pass 2 (optional): filled isobands. Phase 9 — sublayer gate +
        // sublayer-style-driven band count. VS.8 — the ContourBandStyle bag
        // also drives the colour source (named ramp / two-colour gradient,
        // optionally inverted), the classification range (auto [dry, max]
        // or a fixed user range) and the smooth-vs-flat rendering mode.
        if (paintBands && maxDepth > dryDepth) {
            using namespace OpenSWMM::Contour;
            const OpenSWMM::Render::ContourBandStyle *bs =
                (bandSub && bandSub->bandStyle()) ? bandSub->bandStyle()
                                                  : nullptr;
            const int bandCount = std::max(2,
                bs ? bs->bandCount() : layer_->filledContoursLevels());

            double bandLo = dryDepth, bandHi = maxDepth;
            if (bs && bs->useCustomRange() && bs->rangeMax() > bs->rangeMin()) {
                bandLo = bs->rangeMin();
                bandHi = bs->rangeMax();
            }

            const auto levels = evenlySpacedLevelsInclusive(
                bandLo, bandHi, bandCount + 1);
            if (levels.size() >= 2) {
                const double alphaScalar = layer_->filledContoursOpacity();
                const int    nBands      = int(levels.size()) - 1;
                auto bandColor = [&](int idx) -> QColor {
                    QColor c = bs ? bs->colorForBand(idx, nBands)
                                  : viridisAt((double(idx) + 0.5) / double(nBands));
                    c.setAlphaF(c.alphaF() * alphaScalar);
                    return c;
                };
                p->setPen(Qt::NoPen);

                if (!bs || bs->smoothBands()) {
                    // Smooth — marching-triangles isobands over the
                    // vertex-interpolated field (class boundaries cut
                    // through cells).
                    auto extract = [](const SWMM2DResultsLayer::SceneTri &t,
                                      QPointF &p0, QPointF &p1, QPointF &p2,
                                      double  &v0, double  &v1, double  &v2) {
                        p0 = t.a; p1 = t.b; p2 = t.c;
                        v0 = double(t.dv0);
                        v1 = double(t.dv1);
                        v2 = double(t.dv2);
                    };
                    const auto bands = marchingTrianglesIsobands(
                        tris, levels, extract,
                        /*clampUniformOutsideRange=*/false);
                    for (const auto &bp : bands) {
                        if (bp.verts.size() < 3) continue;
                        p->setBrush(bandColor(bp.bandIndex));
                        // Fan-triangulate the convex polygon by drawing it
                        // directly — QPainter handles convex polys efficiently.
                        p->drawConvexPolygon(bp.verts.data(),
                                             int(bp.verts.size()));
                    }
                } else {
                    // Flat — classify each cell by its centre depth and fill
                    // the whole triangle with the band colour (the per-cell
                    // "stepped" look raster GIS users expect).
                    const double span = bandHi - bandLo;
                    for (const auto &t : tris) {
                        if (t.depth < bandLo || span <= 0.0) continue;
                        const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
                        const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
                        const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
                        const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
                        if (!exposed.isNull() &&
                            (maxX < exposed.left()  || minX > exposed.right() ||
                             maxY < exposed.top()   || minY > exposed.bottom())) continue;
                        const int idx = std::min(nBands - 1,
                            int((t.depth - bandLo) / span * double(nBands)));
                        p->setBrush(bandColor(idx));
                        const QPointF pts[3] = { t.a, t.b, t.c };
                        p->drawConvexPolygon(pts, 3);
                    }
                }
            }
        }

        // --- Pass 3 (optional): iso-line contour strokes. Phase 9 — sublayer
        // gate + sublayer-style-driven iso count + colour + line width.
        // VS.8 — levels by count OR fixed interval + base, index contours
        // every Nth level, and along-line labels (decimals / font / halo).
        if (paintIsolines && maxDepth > dryDepth) {
            using namespace OpenSWMM::Contour;
            const auto *isoStyle =
                (isolineSub && isolineSub->isolineStyle())
                ? isolineSub->isolineStyle() : nullptr;
            const std::vector<double> levels = isoStyle
                ? isoStyle->levelsForRange(dryDepth, maxDepth)
                : evenlySpacedLevels(dryDepth, maxDepth,
                                     layer_->isolinesLevels());
            if (!levels.empty()) {
                auto extract = [](const SWMM2DResultsLayer::SceneTri &t,
                                  QPointF &p0, QPointF &p1, QPointF &p2,
                                  double  &v0, double  &v1, double  &v2) {
                    p0 = t.a; p1 = t.b; p2 = t.c;
                    v0 = double(t.dv0);
                    v1 = double(t.dv1);
                    v2 = double(t.dv2);
                };
                const auto segs = marchingTriangles(tris, levels, extract);
                if (!segs.empty()) {
                    const QColor isoColor = isoStyle
                        ? isoStyle->color() : layer_->isolinesColor();
                    const double isoWidthPx = isoStyle
                        ? std::max(0.5, isoStyle->lineWidthPx())
                        : layer_->isolinesWidth();

                    // Index-contour predicate. FixedInterval mode follows
                    // the topographic-map convention (levels at multiples
                    // of indexEvery × interval from the base contour);
                    // Count mode emphasises every Nth ordinal level.
                    const int idxEvery = isoStyle ? isoStyle->indexEvery() : 0;
                    QHash<double, int> ordinalOf;
                    for (int i = 0; i < int(levels.size()); ++i)
                        ordinalOf.insert(levels[i], i);
                    auto isIndexLevel = [&](double level) -> bool {
                        if (!isoStyle || idxEvery < 2) return false;
                        if (isoStyle->levelMode() ==
                            OpenSWMM::Render::IsolineStyle::LevelMode::FixedInterval) {
                            const long k = std::lround(
                                (level - isoStyle->baseLevel())
                                / std::max(isoStyle->levelInterval(), 1e-12));
                            return (k % idxEvery) == 0;
                        }
                        return ((ordinalOf.value(level, 0) + 1) % idxEvery) == 0;
                    };

                    QPen linePen(isoColor);
                    linePen.setCosmetic(true);   // constant pixel width across zoom
                    linePen.setWidthF(isoWidthPx);
                    if (isoStyle) linePen.setStyle(isoStyle->dashPattern());
                    QPen indexPen = linePen;
                    if (isoStyle)
                        indexPen.setWidthF(std::max(isoWidthPx,
                                                    isoStyle->indexWidthPx()));
                    p->setPen(linePen);
                    p->setBrush(Qt::NoBrush);
                    for (const auto &s : segs)
                        if (!isIndexLevel(s.level)) p->drawLine(s.a, s.b);
                    if (idxEvery >= 2) {
                        p->setPen(indexPen);
                        for (const auto &s : segs)
                            if (isIndexLevel(s.level)) p->drawLine(s.a, s.b);
                    }

                    // Optional labels (IsolineStyle::labels). VS.8 — GIS-style
                    // along-line placement: segments of each level are chained
                    // into polylines, then a label is dropped every ~250 screen
                    // px along each chain, rotated to the local line direction
                    // (flipped when it would read upside-down). Decimals, font
                    // size and halo come from the style bag. Density self-adapts
                    // to zoom because spacing is measured in screen pixels.
                    if (isoStyle && isoStyle->labels()) {
                        QHash<double, QVector<QLineF>> byLevel;
                        for (const auto &s : segs)
                            byLevel[s.level].append(QLineF(s.a, s.b));

                        QFont f;
                        f.setPointSizeF(isoStyle->labelFontPt());
                        f.setBold(true);
                        const QFontMetricsF fm(f);
                        const int  decimals = isoStyle->labelDecimals();
                        const bool halo     = isoStyle->labelHalo();
                        constexpr double kLabelSpacingPx = 250.0;

                        const QTransform wt = p->worldTransform();
                        const double quantum = 1e-9 * std::max(
                            {layer_->m_sceneBBox.width(),
                             layer_->m_sceneBBox.height(), 1.0});

                        p->save();
                        p->setRenderHint(QPainter::Antialiasing, true);
                        p->setRenderHint(QPainter::TextAntialiasing, true);
                        // Labels are laid out in screen space so the font
                        // size stays pixel-constant across zoom.
                        p->setWorldMatrixEnabled(false);

                        for (auto it = byLevel.constBegin();
                             it != byLevel.constEnd(); ++it) {
                            const QString text =
                                QString::number(it.key(), 'f', decimals);
                            const QRectF  br = fm.boundingRect(text);

                            const auto chains =
                                OpenSWMM::Contour::chainIsoSegments(
                                    it.value(), quantum);
                            for (const QPolygonF &chain : chains) {
                                if (chain.size() < 2) continue;
                                QPolygonF sp;
                                sp.reserve(chain.size());
                                for (const QPointF &pt : chain)
                                    sp.append(wt.map(pt));
                                double total = 0.0;
                                for (int i = 1; i < sp.size(); ++i)
                                    total += QLineF(sp[i - 1], sp[i]).length();
                                // Chains shorter than ~1.5 label widths get
                                // no label — avoids clutter on fragments.
                                if (total < br.width() * 1.5) continue;

                                double acc    = 0.0;
                                double nextAt = std::max(kLabelSpacingPx * 0.5,
                                                         br.width());
                                for (int i = 1; i < sp.size(); ++i) {
                                    const QPointF a = sp[i - 1];
                                    const QPointF b = sp[i];
                                    const double segLen = QLineF(a, b).length();
                                    if (segLen <= 0.0) continue;
                                    while (acc + segLen >= nextAt) {
                                        const double tf = (nextAt - acc) / segLen;
                                        const QPointF pos = a + tf * (b - a);
                                        double angle = qRadiansToDegrees(
                                            std::atan2(b.y() - a.y(),
                                                       b.x() - a.x()));
                                        if (angle >  90.0) angle -= 180.0;
                                        if (angle < -90.0) angle += 180.0;

                                        QPainterPath path;
                                        path.addText(
                                            QPointF(-br.width() * 0.5,
                                                     br.height() * 0.25),
                                            f, text);
                                        QTransform tr;
                                        tr.translate(pos.x(), pos.y());
                                        tr.rotate(angle);
                                        const QPainterPath placed = tr.map(path);

                                        if (halo) {
                                            p->setPen(QPen(
                                                QColor(255, 255, 255, 230), 3.0,
                                                Qt::SolidLine, Qt::RoundCap,
                                                Qt::RoundJoin));
                                            p->setBrush(Qt::NoBrush);
                                            p->drawPath(placed);
                                        }
                                        p->setPen(Qt::NoPen);
                                        p->setBrush(isoColor);
                                        p->drawPath(placed);
                                        nextAt += kLabelSpacingPx;
                                    }
                                    acc += segLen;
                                }
                            }
                        }
                        p->setWorldMatrixEnabled(true);
                        p->restore();
                    }
                }
            }
        }

        // --- Pass 4 (optional): mesh wireframe overlay from MeshEdgeSublayer.
        // Issue 3 — iterate the DEDUPLICATED edge set (m_sceneEdges) so each
        // undirected edge is stroked exactly once. Previously every triangle
        // drew all three of its edges, stroking each shared edge twice; with a
        // translucent edge colour the two strokes composited into a dark wash.
        if (auto *edgeSub = layer_->meshEdgeSublayer();
            edgeSub && edgeSub->isVisible())
        {
            const auto *es = edgeSub->edgeStyle();
            const QColor col = es ? es->color() : QColor(0, 0, 0, 130);
            const double w   = es ? std::max(0.1, es->lineWidthPx()) : 0.5;
            const qreal subOp = std::clamp<qreal>(edgeSub->opacity(), 0.0, 1.0);

            QPen pen(col);
            pen.setCosmetic(true);
            pen.setWidthF(w);
            if (es) pen.setStyle(es->dashPattern());
            p->save();
            p->setOpacity(subOp);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            for (const auto &e : layer_->m_sceneEdges) {
                const double minX = std::min(e.a.x(), e.b.x());
                const double maxX = std::max(e.a.x(), e.b.x());
                const double minY = std::min(e.a.y(), e.b.y());
                const double maxY = std::max(e.a.y(), e.b.y());
                if (!exposed.isNull() &&
                    (maxX < exposed.left()  || minX > exposed.right() ||
                     maxY < exposed.top()   || minY > exposed.bottom())) continue;
                p->drawLine(e.a, e.b);
            }
            p->restore();
        }

        // --- Pass 5 (optional): mesh-vertex markers from MeshNodeSublayer.
        // Vertices appear duplicated across SceneTris; we just draw at each
        // corner. The QPainter cost is negligible at marker sizes <= 8 px.
        // The taggedColor/Size knobs are not consulted in the results layer
        // (we don't carry tagged-vertex info here — that's mesh-layer-only).
        if (auto *nodeSub = layer_->meshNodeSublayer();
            nodeSub && nodeSub->isVisible())
        {
            const auto *ns = nodeSub->nodeStyle();
            const QColor col   = ns ? ns->color() : QColor(40, 40, 40, 220);
            const double szPx  = ns ? std::max(0.5, ns->markerSizePx()) : 3.0;
            const QColor outC  = ns ? ns->outlineColor() : QColor(255,255,255,220);
            const double outPx = ns ? std::max(0.0, ns->outlineWidthPx()) : 0.5;
            const qreal  subOp = std::clamp<qreal>(nodeSub->opacity(), 0.0, 1.0);
            const QTransform wt = p->worldTransform();
            const double scale  = std::max(std::abs(wt.m11()), 1e-9);
            const double rScene = (szPx * 0.5) / scale;

            p->save();
            p->setOpacity(subOp);
            if (outPx > 0.0) {
                QPen pen(outC);
                pen.setCosmetic(true);
                pen.setWidthF(outPx);
                p->setPen(pen);
            } else {
                p->setPen(Qt::NoPen);
            }
            p->setBrush(col);
            auto drawMarker = [&](const QPointF &pt) {
                p->drawEllipse(pt, rScene, rScene);
            };
            for (const auto &t : tris) {
                const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
                const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
                const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
                const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
                if (!exposed.isNull() &&
                    (maxX < exposed.left()  || minX > exposed.right() ||
                     maxY < exposed.top()   || minY > exposed.bottom())) continue;
                drawMarker(t.a);
                drawMarker(t.b);
                drawMarker(t.c);
            }
            p->restore();
        }

        // --- VS.10: optional per-cell value labels. Driven by the base
        // OpenSWMMVisLayer::labelConfig(). Wet cells are labelled with their
        // current scalar value (depth) on a coarse screen-pixel grid so the
        // labels don't overdraw; drawn in screen space so the font size is
        // constant across zoom. Mirrors the isoline-label halo treatment.
        // NOTE for finishing pass: the labelled quantity is hard-coded to
        // depth; wire it to the active result variable + add proper
        // collision avoidance when the labelling engine is generalised.
        {
            const auto &lc = layer_->labelConfig();
            if (lc.enabled) {
                const QTransform wt = p->worldTransform();
                const double scale = std::max(std::abs(wt.m11()), 1e-9);
                const QFont f = lc.effectiveFont();
                const double spacingPx = std::max(40.0, lc.fontSizePt * 6.0);
                const double gridStep  = spacingPx / scale;
                const QRectF area = exposed.isNull() ? layer_->m_sceneBBox : exposed;
                if (gridStep > 0.0 && area.isValid() && !tris.isEmpty()) {
                    const int nCols = std::max(1, int(area.width()  / gridStep));
                    const int nRows = std::max(1, int(area.height() / gridStep));
                    const double cellW = area.width()  / double(nCols);
                    const double cellH = area.height() / double(nRows);
                    QHash<qint64, int> bucket;
                    for (int i = 0; i < tris.size(); ++i) {
                        const auto &t = tris[i];
                        if (t.depth < dryDepth) continue;
                        const double rx = t.centroid.x() - area.left();
                        const double ry = t.centroid.y() - area.top();
                        if (rx < 0 || ry < 0 || rx > area.width() || ry > area.height())
                            continue;
                        const int cx = std::min(nCols - 1, int(rx / cellW));
                        const int cy = std::min(nRows - 1, int(ry / cellH));
                        const qint64 key = qint64(cy) * nCols + cx;
                        if (!bucket.contains(key)) bucket.insert(key, i);
                    }
                    // Slice US.B3 — render through the shared LabelPainter so
                    // halo + the (new) optional background frame match every
                    // other label path. Drawn in screen space at a constant
                    // point size. The baseline sat at the centroid screen
                    // point before, so offset top-left up by the font ascent.
                    const double ascent = QFontMetricsF(f).ascent();
                    p->save();
                    p->setWorldMatrixEnabled(false);   // screen space
                    for (auto it = bucket.constBegin(); it != bucket.constEnd(); ++it) {
                        const auto &t = tris[it.value()];
                        const QString text = QString::number(t.depth, 'f', 2);
                        const QPointF px = wt.map(t.centroid);
                        OpenSWMM::Render::LabelPainter::drawLabel(
                            *p, QPointF(px.x(), px.y() - ascent), text, lc);
                    }
                    p->setWorldMatrixEnabled(true);
                    p->restore();
                }
            }
        }

        // CF.3 — second pass: outline highlighted cells (box / lasso picks).
        // Drawn AFTER the depth fill so the outline sits visibly on top, even
        // over dry cells. Width compensated for current zoom so the outline
        // stays a constant ~2px regardless of zoom level.
        const QSet<int>& hi = layer_->highlightedCells();
        if (!hi.isEmpty()) {
            const QTransform wt = p->worldTransform();
            const double scale = wt.m11();
            const double penWidthScene = (scale > 0.0) ? (2.0 / scale) : 1.0;

            QPen outlinePen(QColor(255, 215, 0, 235));   // gold outline
            outlinePen.setWidthF(penWidthScene);
            outlinePen.setJoinStyle(Qt::RoundJoin);
            p->setPen(outlinePen);
            // Translucent cyan fill so the selection reads as a filled patch,
            // not just a thin outline that's easy to miss over the depth ramp.
            p->setBrush(QColor(0, 200, 255, 110));

            const int nTris = tris.size();
            for (int idx : hi) {
                if (idx < 0 || idx >= nTris) continue;
                const auto& t = tris[idx];
                // Skip cells outside the exposed rect (cheap cull).
                const double tx0 = std::min({t.a.x(), t.b.x(), t.c.x()});
                const double tx1 = std::max({t.a.x(), t.b.x(), t.c.x()});
                const double ty0 = std::min({t.a.y(), t.b.y(), t.c.y()});
                const double ty1 = std::max({t.a.y(), t.b.y(), t.c.y()});
                if (!exposed.isNull() &&
                    (tx1 < exposed.left()  || tx0 > exposed.right() ||
                     ty1 < exposed.top()   || ty0 > exposed.bottom())) continue;
                const QPointF pts[3] = { t.a, t.b, t.c };
                p->drawConvexPolygon(pts, 3);
            }
        }

        p->restore();
    }

private:
    SWMM2DResultsLayer* layer_;
};

// ---------------------------------------------------------------------------
// SWMM2DVelocityArrowsItem — centroid arrow overlay (CF.2.3)
// ---------------------------------------------------------------------------
//
// Draws one arrow per wet triangle: anchored at the cached scene-space
// centroid, length log-scaled by |v|, colour = magnitude on the velocity
// ramp. Renders above the depth fill via z-ordering (layerZValue + 0.5).
// Arrow length is kept constant in pixels by dividing by the painter's
// world-transform scale, so glyphs don't blow up at zoom-in or vanish at
// zoom-out.

class SWMM2DVelocityArrowsItem : public QGraphicsItem
{
public:
    explicit SWMM2DVelocityArrowsItem(SWMM2DResultsLayer* layer,
                                       QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), layer_(layer)
    {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
        setCacheMode(QGraphicsItem::NoCache);
        // Render above the depth fill (which uses layerZValue()).
        setZValue(layer->layerZValue() + 0.5);
    }

    void geometryChanged() { prepareGeometryChange(); update(); }

    QRectF boundingRect() const override { return layer_->m_sceneBBox; }

    void paint(QPainter* p,
               const QStyleOptionGraphicsItem* option,
               QWidget*) override
    {
        if (!layer_->isVisible()) return;
        // VS.8 — QSG ownership bypass (see SWMM2DResultsGraphicsItem).
        if (layer_->qsgOwnsRendering()) return;
        if (!layer_->hasVelocityData()) return;
        if (layer_->m_sceneTris.isEmpty()) return;
        // Velocity glyphs convey both magnitude and flow direction. The
        // separate direction-only flow-arrow pass was removed (2026-06-21,
        // redundant with velocity vectors).
        paintVelocityGlyphs(p, option);
    }

private:
    void paintVelocityGlyphs(QPainter* p,
                             const QStyleOptionGraphicsItem* option)
    {
        // Phase 9 (2026-05-25) — sublayer.isVisible() is the authoritative
        // gate (MVC). Falls back to the legacy velocityVectorsVisible()
        // when no sublayer is bound, preserving back-compat with UI code
        // that flips the boolean directly.
        auto *velSub = layer_->velocityVectorSublayer();
        const bool wantArrows = velSub
            ? velSub->isVisible()
            : layer_->velocityVectorsVisible();
        if (!wantArrows) return;

        Q_UNUSED(option);   // V0 (Issue 5) — no longer cull glyphs to the
                            // repainted tile (option->exposedRect); the view
                            // clips painting, so the placement domain is stable.
        const auto& tris = layer_->m_sceneTris;
        const qreal  alpha     = std::clamp<qreal>(layer_->velocityOpacity(), 0.0, 1.0);

        // VS.8 — the VelocityVectorStyle bag is the single source of truth
        // for sizing / colour / placement. The legacy layer fields survive
        // only for the (never-hit) no-sublayer case.
        const OpenSWMM::Render::VelocityVectorStyle *vs =
            velSub ? velSub->vectorStyle() : nullptr;

        const double dryCut = vs
            ? std::max(vs->dryDepthCutoff(), layer_->dryDepth())
            : layer_->dryDepth();
        const double maxVel = std::max(layer_->maxVelocity(), 1e-6);

        // Convert pixel-space arrow length to scene units so glyphs render
        // at a constant on-screen size regardless of zoom.
        const QTransform xf = p->worldTransform();
        const double scale  = std::max(std::abs(xf.m11()), 1e-9);  // assume uniform scale
        const double pxToScene = 1.0 / scale;

        // Pen width also kept constant in pixels.
        QPen pen;
        pen.setCosmetic(true);
        pen.setWidthF(vs ? vs->shaftWidthPx() : 1.5);
        pen.setCapStyle(Qt::RoundCap);

        p->save();
        p->setOpacity(alpha);

        // Skip cells whose magnitude is below 0.01% of the running peak —
        // numerical noise without clipping real flow on slow shallow runs.
        const double vmagSkip = std::max(static_cast<double>(maxVel) * 1e-4,
                                          1e-9);

        auto drawGlyph = [&](const SWMM2DResultsLayer::SceneTri &t) {
            // Glyph length: style-driven scaling (linear / sqrt / log with
            // min/max pixel clamps); legacy normalised-log fallback when no
            // style bag is bound.
            double lenPx = 0.0;
            if (vs) {
                lenPx = vs->glyphLengthPxForSpeed(t.vmag);
            } else {
                const double mag_norm = std::clamp(t.vmag / maxVel, 0.0, 1.0);
                lenPx = layer_->velocityArrowScale() * std::log1p(mag_norm) /
                        std::log1p(1.0);
            }
            const double len_scene = pxToScene * lenPx;
            if (len_scene <= 0.0) return;

            const double inv_vmag = 1.0 / t.vmag;
            const double dx = t.vx * inv_vmag * len_scene;
            const double dy = t.vy * inv_vmag * len_scene;

            if (vs) {
                pen.setColor(vs->colorForSpeed(t.vmag));
            } else {
                int r, g, b;
                velocityColorRgb(t.vmag, maxVel, r, g, b);
                pen.setColor(QColor(r, g, b));
            }
            p->setPen(pen);

            const QPointF tail = t.centroid;
            const QPointF head(tail.x() + dx, tail.y() + dy);
            p->drawLine(tail, head);

            // Chevron arrowhead — two short legs at ±25° from the back-facing direction.
            const double headLen = vs ? vs->headSizePx() * pxToScene
                                      : len_scene * 0.32;
            const double cosA = std::cos(0.43);   // ≈25°
            const double sinA = std::sin(0.43);
            const double ux = -dx / len_scene;    // unit vector head → tail
            const double uy = -dy / len_scene;
            const QPointF left ( head.x() + headLen * (ux * cosA - uy * sinA),
                                  head.y() + headLen * (ux * sinA + uy * cosA) );
            const QPointF right( head.x() + headLen * (ux * cosA + uy * sinA),
                                  head.y() + headLen * (-ux * sinA + uy * cosA) );
            p->drawLine(head, left);
            p->drawLine(head, right);
        };

        // VS.8 — screen-grid placement: one glyph per glyphSpacingPx-sized
        // cell, keyed to the strongest |v| triangle whose centroid lands in
        // it. Spacing <= 1 px degenerates to per-cell glyphs (legacy look).
        const double spacingPx = vs ? vs->glyphSpacingPx() : 0.0;
        if (spacingPx > 1.0) {
            // V0 (Issue 5) — key the placement grid to the WHOLE mesh bbox, not
            // option->exposedRect. exposedRect is only the repainted tile during
            // a partial QGraphicsView update, so keying the grid to it made
            // arrows appear/disappear with whatever sub-rectangle Qt happened to
            // invalidate ("renders only portions sometimes"). The view still
            // clips the actual painting to the exposed region, so this is correct
            // and only re-buckets over a stable domain.
            const QRectF area = layer_->m_sceneBBox;
            const double gridStep = spacingPx * pxToScene;
            if (gridStep > 0.0 && area.isValid()) {
                const int nCols = std::max(1, int(area.width()  / gridStep));
                const int nRows = std::max(1, int(area.height() / gridStep));
                const double cellW = area.width()  / double(nCols);
                const double cellH = area.height() / double(nRows);
                QHash<qint64, int> bucket;
                for (int i = 0; i < tris.size(); ++i) {
                    const auto &t = tris[i];
                    if (t.depth < dryCut)  continue;
                    if (t.vmag < vmagSkip) continue;
                    const double rx = t.centroid.x() - area.left();
                    const double ry = t.centroid.y() - area.top();
                    if (rx < 0 || ry < 0 || rx > area.width() || ry > area.height())
                        continue;
                    const int cx = std::min(nCols - 1, int(rx / cellW));
                    const int cy = std::min(nRows - 1, int(ry / cellH));
                    const qint64 key = qint64(cy) * nCols + cx;
                    const auto it = bucket.constFind(key);
                    if (it == bucket.constEnd() || tris[it.value()].vmag < t.vmag)
                        bucket.insert(key, i);
                }
                for (auto it = bucket.constBegin(); it != bucket.constEnd(); ++it)
                    drawGlyph(tris[it.value()]);
            }
        } else {
            // V0 (Issue 5) — no per-centroid exposedRect cull; the view already
            // clips to the dirty region, and culling to the tile dropped arrows
            // outside whatever sub-rectangle was repainted.
            for (const auto& t : tris) {
                if (t.depth < dryCut)  continue;
                if (t.vmag < vmagSkip) continue;          // sub-threshold cell
                drawGlyph(t);
            }
        }

        p->restore();
    }

private:
    SWMM2DResultsLayer* layer_;
};

// ===========================================================================
// EngineMesh2DSource
// ===========================================================================

EngineMesh2DSource::EngineMesh2DSource(std::vector<double>            vx,
                                         std::vector<double>            vy,
                                         std::vector<double>            vz,
                                         std::vector<std::array<int,3>> tris)
    : vx_(std::move(vx)), vy_(std::move(vy)), vz_(std::move(vz)),
      tris_(std::move(tris))
{}

void EngineMesh2DSource::pushDepths(std::vector<float> depths,
                                     QDateTime simTime,
                                     double elapsedSec)
{
    // If the runner already delivered flux for this same tick (flux arrived
    // first because of queue ordering or because depth bulk happens to be
    // slower), fold into the existing Tick rather than emitting a new frame.
    if (!history_.empty() &&
        history_.back().depths.empty() &&
        std::abs(history_.back().elapsed_sec - elapsedSec) < 1e-6)
    {
        history_.back().depths   = std::move(depths);
        history_.back().sim_time = simTime;
        return;
    }
    Tick t;
    t.depths      = std::move(depths);
    t.sim_time    = simTime;
    t.elapsed_sec = elapsedSec;
    history_.emplace_back(std::move(t));
}

void EngineMesh2DSource::pushFlux(std::vector<float> flux,
                                   QDateTime simTime,
                                   double elapsedSec)
{
    // Pair with the most recently pushed depths frame when the elapsed times
    // match; otherwise (depths came earlier and a new tick has begun, or
    // depths haven't arrived yet) append a tick with empty depths so that
    // readEdgeFluxAt at this index still works.
    if (!history_.empty() &&
        std::abs(history_.back().elapsed_sec - elapsedSec) < 1e-6)
    {
        history_.back().flux = std::move(flux);
        return;
    }
    Tick t;
    t.flux        = std::move(flux);
    t.sim_time    = simTime;
    t.elapsed_sec = elapsedSec;
    history_.emplace_back(std::move(t));
}

void EngineMesh2DSource::pushVertexSignedDepths(std::vector<double> depths,
                                                 QDateTime simTime,
                                                 double elapsedSec)
{
    // SIGNED render depths (η_v − z_v) from the engine's wet-masked
    // reconstruction — the datum subtraction already happened engine-side in
    // double, so compact floats lose nothing. NO clamping: negatives over the
    // dry side of partially wet cells are the sub-cell shoreline signal the
    // barycentric blend needs.
    std::vector<float> vdepths(depths.size(), 0.0f);
    for (size_t v = 0; v < depths.size(); ++v)
        vdepths[v] = float(depths[v]);

    // Pair with the tick whose elapsed time matches (same convention as
    // pushFlux); otherwise open a new tick with empty depths so a late
    // depths push folds in.
    if (!history_.empty() &&
        std::abs(history_.back().elapsed_sec - elapsedSec) < 1e-6)
    {
        history_.back().vertex_depths = std::move(vdepths);
        return;
    }
    Tick t;
    t.vertex_depths = std::move(vdepths);
    t.sim_time      = simTime;
    t.elapsed_sec   = elapsedSec;
    history_.emplace_back(std::move(t));
}

void EngineMesh2DSource::setEdgeGeometry(std::vector<float> length,
                                          std::vector<float> nx,
                                          std::vector<float> ny)
{
    edge_length_ = std::move(length);
    edge_nx_     = std::move(nx);
    edge_ny_     = std::move(ny);
}

bool EngineMesh2DSource::readMeshGeometry(std::vector<double>& vx,
                                           std::vector<double>& vy,
                                           std::vector<double>& vz,
                                           std::vector<std::array<int, 3>>& tris)
{
    vx   = vx_;
    vy   = vy_;
    vz   = vz_;
    tris = tris_;
    return true;
}

bool EngineMesh2DSource::readDepthsAt(int timeIdx, std::vector<float>& depths)
{
    if (timeIdx < 0 || timeIdx >= static_cast<int>(history_.size())) {
        depths.assign(tris_.size(), 0.0f);
        return false;
    }
    depths = history_[timeIdx].depths;
    return true;
}

QDateTime EngineMesh2DSource::simTimeAt(int i) const
{
    if (i < 0 || i >= static_cast<int>(history_.size())) return {};
    return history_[i].sim_time;
}

bool EngineMesh2DSource::readEdgeFluxAt(int timeIdx, std::vector<float>& flux)
{
    const size_t n3 = tris_.size() * 3;
    if (timeIdx < 0 || timeIdx >= static_cast<int>(history_.size())) {
        flux.assign(n3, 0.0f);
        return false;
    }
    const auto& src = history_[timeIdx].flux;
    if (src.empty()) {
        // Tick was pushed via pushDepths only — engine lacks the flux API.
        flux.assign(n3, 0.0f);
        return false;
    }
    flux = src;
    return true;
}

bool EngineMesh2DSource::readEdgeGeometry(std::vector<float>& length,
                                           std::vector<float>& nx,
                                           std::vector<float>& ny)
{
    if (edge_length_.empty()) return false;
    length = edge_length_;
    nx     = edge_nx_;
    ny     = edge_ny_;
    return true;
}

bool EngineMesh2DSource::readVertexDepthsAt(int timeIdx,
                                             std::vector<float>& vdepths)
{
    if (timeIdx < 0 || timeIdx >= static_cast<int>(history_.size()))
        return false;
    const auto& src = history_[timeIdx].vertex_depths;
    if (src.empty()) {
        // Tick was pushed without heads — engine lacks the vertex API (or
        // the heads message hasn't landed yet); caller falls back.
        return false;
    }
    vdepths = src;
    return true;
}

// ===========================================================================
// HDF5Mesh2DSource
// ===========================================================================

HDF5Mesh2DSource::HDF5Mesh2DSource()
    : reader_(std::make_unique<openswmmvis::io::Mesh2DH5Reader>())
{}

HDF5Mesh2DSource::~HDF5Mesh2DSource() = default;

bool HDF5Mesh2DSource::open(const QString& path)
{
    path_ = path;
    return reader_->open(path);
}

int HDF5Mesh2DSource::vertexCount() const   { return reader_->vertexCount(); }
int HDF5Mesh2DSource::triangleCount() const { return reader_->triangleCount(); }
int HDF5Mesh2DSource::timeCount() const     { return reader_->timeCount(); }

bool HDF5Mesh2DSource::readMeshGeometry(std::vector<double>& vx,
                                          std::vector<double>& vy,
                                          std::vector<double>& vz,
                                          std::vector<std::array<int, 3>>& tris)
{
    if (!reader_->readMeshGeometry(vx, vy, vz)) return false;
    return reader_->readTriangles(tris);
}

bool HDF5Mesh2DSource::readDepthsAt(int timeIdx, std::vector<float>& depths)
{
    return reader_->readDepthsAt(timeIdx, depths);
}

bool HDF5Mesh2DSource::readEdgeFluxAt(int timeIdx, std::vector<float>& flux)
{
    return reader_->readEdgeFluxAt(timeIdx, flux);
}

bool HDF5Mesh2DSource::readEdgeGeometry(std::vector<float>& length,
                                         std::vector<float>& nx,
                                         std::vector<float>& ny)
{
    return reader_->readEdgeGeometry(length, nx, ny);
}

bool HDF5Mesh2DSource::readVertexDepthsAt(int timeIdx,
                                           std::vector<float>& vdepths)
{
    // Only the SIGNED wet-masked render field (/Mesh2_node_depth) is consumed.
    // The legacy /Mesh2_node_head solver field is deliberately ignored: its
    // stencil blends DRY-cell bed elevations into shoreline vertices, which
    // rendered water climbing adverse slopes/bed steps with no driving head.
    // Files that predate the new dataset fall back (return false) to the
    // GUI-side wet-only reconstruction, which is free of that artifact.
    if (!reader_->readVertexSignedDepthsAt(timeIdx, depth_buf_))
        return false;
    vdepths = depth_buf_;
    return true;
}

QDateTime HDF5Mesh2DSource::simTimeAt(int timeIdx) const
{
    if (!reader_) return {};
    // The /time dataset stores the SWMM **absolute** DateTime (days since
    // 1899-12-30), NOT days-since-start: the engine writes
    // SimulationSnapshot::sim_time (= ctx.current_date) verbatim
    // (Default2DOutputPlugin.cpp), and only the HDF5 units *attribute* is
    // mislabelled "days since simulation start". Treating it as relative and
    // adding sim_start_ double-counted the epoch and shifted every 2D frame
    // ~126 years into the future (2152 vs 2026), so the animation controller's
    // causal compare (ti <= cursor) was always false and 2D playback froze on
    // frame 0. Convert as an absolute SWMM DateTime via the canonical
    // engine-native converter so the 2D axis shares the 1D results clock's
    // basis (SWMMResultsLayer uses the same openswmmvis::core converter).
    // Re-read each call so live-tail growth is reflected; O(1) once HDF5 has
    // parsed the file metadata.
    std::vector<double> times;
    if (!reader_->readTimes(times)) return {};
    if (timeIdx < 0 || timeIdx >= static_cast<int>(times.size())) return {};
    return openswmmvis::core::swmmDateTimeToQDateTime(times[timeIdx]);
}

// ===========================================================================
// SWMM2DResultsLayer
// ===========================================================================

SWMM2DResultsLayer::SWMM2DResultsLayer(const QString& name,
                                         OpenSWMMVisWorkspace* parent)
    : OpenSWMMVisLayer(name, parent)
{
    setLayerType(OpenSWMMVisLayer::SWMM2DResultsLayer);

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Default to a
    // GraduatedRenderer because this layer's primary visual axis is a
    // continuous depth attribute.  Paint loop still uses dry_depth_ /
    // max_depth_ directly; refactor deferred to 8.13.6.4.
    m_renderer = std::make_unique<OpenSWMM::Render::GraduatedRenderer>();

    // Slice S5.6 — populate the 2D default sublayer mix per plan §3.
    // QObject parent-child ownership keeps each sublayer alive for the
    // layer's lifetime. Stable IDs namespaced under "results2d." so the
    // 1D layer's "results." IDs never collide. The existing CF.MVP
    // SceneTri pipeline keeps painting; sublayers are dormant.
    m_meshFillSublayer = new OpenSWMM::Render::MeshFillSublayer(
        QStringLiteral("results2d.mesh"), this);
    m_meshEdgeSublayer = new OpenSWMM::Render::MeshEdgeSublayer(
        QStringLiteral("results2d.meshEdges"), this);
    m_meshNodeSublayer = new OpenSWMM::Render::MeshNodeSublayer(
        QStringLiteral("results2d.meshVertices"), this);
    m_contourBandSublayer = new OpenSWMM::Render::ContourBandSublayer(
        QStringLiteral("results2d.bands"), this);
    m_isolineSublayer = new OpenSWMM::Render::IsolineSublayer(
        QStringLiteral("results2d.isolines"), this);
    m_velocityVectorSublayer = new OpenSWMM::Render::VelocityVectorSublayer(
        QStringLiteral("results2d.velocity"), this);
    // Direct mesh-fill depth visualisations (alternatives to the
    // marching-squares contour bands): flat per-cell colour, and per-vertex
    // Gouraud (smooth seam-free gradient). Continuous color mapping by default.
    m_cellDepthFillSublayer = new OpenSWMM::Render::CellDepthFillSublayer(
        QStringLiteral("results2d.cellDepthFill"), this);
    m_smoothDepthFillSublayer = new OpenSWMM::Render::SmoothDepthFillSublayer(
        QStringLiteral("results2d.smoothDepthFill"), this);

    // 2026-07-24 — default the depth fill to the per-vertex Gouraud SMOOTH
    // fill rather than contour bands. The marching-squares bands read as
    // discrete steps and leave thin seam/gap artifacts between band polygons;
    // the smooth fill is a seam-free continuous gradient over the mesh. Users
    // can still switch to bands / flat cell fill from the layer tree.
    if (m_smoothDepthFillSublayer) m_smoothDepthFillSublayer->setVisible(true);
    if (m_contourBandSublayer)     m_contourBandSublayer->setVisible(false);

    // Phase 9 (2026-05-25) — sublayer.invalidated() routes to the existing
    // graphics-item update path. This is what makes the layer-tree
    // checkbox + sublayer style edits actually show/hide / re-render the
    // 2D overlays live. Heatmap / bands / isolines all share the same
    // graphics_item_; velocity has its own arrows_item_ so route those
    // separately. Lambda captures `this` so we always observe the
    // current item pointers (they're recreated on every populateScene).
    // NOTE: the item's geometryChanged()/update() alone does NOT refresh
    // MapCanvas's cached scene buffer — the canvas only re-renders on
    // repaintRequested/invalidate (same trap documented in highlightCells).
    // Without the emit, toggling a sublayer or editing its style shows
    // nothing until some other event repaints the canvas (pan/zoom/tick).
    auto wireMeshRepaint = [this](OpenSWMM::Render::ISublayer *s) {
        if (!s) return;
        QObject::connect(s, &OpenSWMM::Render::ISublayer::invalidated,
                         this, [this]() {
                             if (graphics_item_) graphics_item_->geometryChanged();
                             emit repaintRequested();
                         });
    };
    auto wireArrowRepaint = [this](OpenSWMM::Render::ISublayer *s) {
        if (!s) return;
        QObject::connect(s, &OpenSWMM::Render::ISublayer::invalidated,
                         this, [this]() {
                             if (arrows_item_) arrows_item_->geometryChanged();
                             emit repaintRequested();
                         });
    };
    wireMeshRepaint(m_meshFillSublayer);
    wireMeshRepaint(m_meshEdgeSublayer);
    wireMeshRepaint(m_meshNodeSublayer);
    wireMeshRepaint(m_contourBandSublayer);
    wireMeshRepaint(m_cellDepthFillSublayer);
    wireMeshRepaint(m_smoothDepthFillSublayer);
    wireMeshRepaint(m_isolineSublayer);
    wireArrowRepaint(m_velocityVectorSublayer);
}

SWMM2DResultsLayer::~SWMM2DResultsLayer() = default;

QList<OpenSWMM::Render::ISublayer *> SWMM2DResultsLayer::sublayers() const
{
    // Paint order = list order (bottom-up):
    //   mesh fill (static)      → terrain hillshade base
    //   contour bands (dynamic) → marching-squares filled bands (depth fill)
    //   mesh edges (static)     → wireframe over results
    //   isolines (dynamic)      → contour lines + labels
    //   mesh vertices (static)  → coupled-vertex markers
    //   velocity vectors (top)  → magnitude-scaled glyphs (also show direction)
    // 2026-06-21 — the depth color ramp and direction-only flow arrows were
    // removed: contour bands now serve as the depth fill, and velocity
    // vectors already convey flow direction.
    // Slice GUI-2026-05-30 §2 — order is user-customisable and cached in
    // m_sublayerOrder; seeded once from the defaults above.
    if (m_sublayerOrder.isEmpty()) {
        OpenSWMM::Render::ISublayer *defaults[] = {
            m_meshFillSublayer,
            m_cellDepthFillSublayer,    // flat per-cell depth fill (base)
            m_smoothDepthFillSublayer,  // Gouraud smooth depth fill (base)
            m_contourBandSublayer,
            m_meshEdgeSublayer,
            m_isolineSublayer,
            m_meshNodeSublayer,
            m_velocityVectorSublayer,
        };
        for (auto *s : defaults)
            if (s) m_sublayerOrder.append(s);
    }
    return m_sublayerOrder;
}

bool SWMM2DResultsLayer::moveSublayer(int from, int to)
{
    (void) sublayers();      // force lazy seed
    if (from < 0 || from >= m_sublayerOrder.size()
        || to   < 0 || to   >= m_sublayerOrder.size()
        || from == to)
        return false;
    m_sublayerOrder.move(from, to);
    emit repaintRequested();
    return true;
}

// ---------------------------------------------------------------------------
// Renderer (Slice BI Phase 8.13.6.6)
// ---------------------------------------------------------------------------

OpenSWMM::Render::IFeatureRenderer *SWMM2DResultsLayer::renderer() const
{
    return m_renderer.get();
}

void SWMM2DResultsLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r)
{
    if (!r)
        return;
    if (r.get() == m_renderer.get())
        return;
    m_renderer = std::move(r);
    emit rendererChanged();
}

bool SWMM2DResultsLayer::hasEdgeFluxData() const
{
    // Frame-independent capability probe, cached as a tri-state. Unlike
    // have_velocity_ (recomputed per tick from the shown frame's reconstructed
    // velocity, hence false on dry frames), this answers "does the run carry a
    // per-edge flux feed at all" — the correct gate for whole-series plotting.
    if (edge_flux_probe_ != 0) return edge_flux_probe_ > 0;
    if (!source_) return false;
    const int nTri = source_->triangleCount();
    if (nTri <= 0 || source_->timeCount() <= 0)
        return false;   // not yet knowable (e.g. live source with no frames); don't cache
    std::vector<float> probe;
    const bool ok = source_->readEdgeFluxAt(0, probe) &&
                    probe.size() == static_cast<std::size_t>(nTri) * 3;
    edge_flux_probe_ = ok ? 1 : -1;
    return ok;
}

void SWMM2DResultsLayer::setSource(std::unique_ptr<IMesh2DSource> source)
{
    source_ = std::move(source);
    current_time_idx_ = -1;
    current_depths_.clear();
    current_flux_.clear();
    have_velocity_ = false;
    edge_flux_probe_ = 0;   // re-probe edge-flux availability for the new source
    rebuildSceneGeometry_();

    const int n = source_ ? source_->timeCount() : 0;
    emit timeRangeChanged(0, std::max(0, n - 1));

    // Auto-seed max_velocity_ from a single global RT0 scan so the colour
    // ramp + arrow-length log scaling are anchored to the run's peak speed.
    // The layer's per-frame applyCurrentFlux_() only auto-GROWS max_velocity_;
    // without this seed it starts at the default (1 m/s) and shallow-flow
    // demos (snoopy peak ≈ 0.14 mm/s) end up with sub-pixel arrows.
    // Cheap: 480 frames × 128 cells × 3 edges + 2×2 inverse per cell.
    if (source_ && have_edge_geom_ && !tris_.empty() && n > 0 &&
        !max_velocity_user_set_)
    {
        constexpr float kQMax = 10.0f;
        const int nTri = static_cast<int>(tris_.size());
        float scanned_max = 0.0f;
        std::vector<float> fluxBuf;
        for (int t = 0; t < n; ++t) {
            if (!source_->readEdgeFluxAt(t, fluxBuf)) continue;
            if (static_cast<int>(fluxBuf.size()) != nTri * 3) continue;
            for (int i = 0; i < nTri; ++i) {
                double a00 = 0, a01 = 0, a11 = 0, b0 = 0, b1 = 0;
                for (int e = 0; e < 3; ++e) {
                    const int idx = i * 3 + e;
                    const double len = edge_length_[idx];
                    if (len <= 1e-12) continue;
                    double q = fluxBuf[idx] / len;
                    if (q >  kQMax) q =  kQMax;
                    if (q < -kQMax) q = -kQMax;
                    const double nx = edge_nx_[idx];
                    const double ny = edge_ny_[idx];
                    a00 += nx * nx; a01 += nx * ny; a11 += ny * ny;
                    b0  += nx * q;  b1  += ny * q;
                }
                const double det = a00 * a11 - a01 * a01;
                if (std::abs(det) < 1e-12) continue;
                const double inv_det = 1.0 / det;
                const double vx = ( a11 * b0 - a01 * b1) * inv_det;
                const double vy = (-a01 * b0 + a00 * b1) * inv_det;
                const float vmag = static_cast<float>(std::sqrt(vx*vx + vy*vy));
                if (vmag > scanned_max) scanned_max = vmag;
            }
        }
        if (scanned_max > 0.0f) {
            max_velocity_ = scanned_max;
            have_velocity_ = true;
        }
    }

    // Auto-seed max_depth_ from a global scan over every frame so the depth
    // colour ramp is anchored to the run's true peak depth from the first
    // frame shown — mirroring the velocity seed above. Without this,
    // loadFrame_() only auto-GROWS max_depth_ as frames are visited, so the
    // peak depth (which usually occurs mid-run, not on the last frame that
    // setCurrentTimeIndex shows first) is not mapped to the top of the ramp
    // until the user happens to scrub onto that exact frame — i.e. the max
    // depth doesn't reach the max colour. Skipped when the user pinned the
    // range explicitly. Cheap: n frames × cell count, one linear pass.
    if (source_ && n > 0 && !tris_.empty() && !max_depth_user_set_)
    {
        float scanned_max_depth = 0.0f;
        std::vector<float> depthBuf;
        for (int t = 0; t < n; ++t) {
            source_->readDepthsAt(t, depthBuf);
            if (depthBuf.size() != tris_.size()) continue;
            for (float d : depthBuf)
                if (std::isfinite(d) && d > scanned_max_depth)
                    scanned_max_depth = d;
        }
        if (scanned_max_depth > 0.0f)
            max_depth_ = scanned_max_depth;
    }

    // Show a frame immediately if any are available. A live source shows its
    // first frame (playback stays user-driven); a completed source shows the
    // latest.
    if (n > 0) setCurrentTimeIndex(source_->isLive() ? 0 : (n - 1));
    else {
        if (graphics_item_) graphics_item_->geometryChanged();
        if (arrows_item_)   arrows_item_->geometryChanged();
    }
}

bool SWMM2DResultsLayer::loadFrame_(int t)
{
    if (!source_) return false;
    const int n = source_->timeCount();
    if (n == 0) return false;
    t = std::clamp(t, 0, n - 1);

    current_time_idx_ = t;
    source_->readDepthsAt(t, current_depths_);
    if (current_depths_.size() != tris_.size())
        current_depths_.assign(tris_.size(), 0.0f);
    // Sanitize at the single choke point every consumer reads from
    // (heatmap fill, per-vertex reconstruction, ramp autogrow, Quantile/Jenks
    // samples — CPU and QSG paths alike). A transient NaN/Inf cell depth from
    // a live tick otherwise slips every dry-gate (`h < dry` is false for NaN),
    // poisons the vertex reconstruction star-wise around the cell, and reaches
    // the colour ramp as clamp(NaN) → arbitrary colour — the streaky-triangle
    // artifact on large live runs. Non-finite → 0 (dry).
    {
        int bad = 0;
        for (float &d : current_depths_)
            if (!std::isfinite(d)) { d = 0.0f; ++bad; }
        static const bool kRenderDebug =
            qEnvironmentVariableIsSet("OPENSWMM_2D_RENDER_DEBUG");
        if (kRenderDebug && bad > 0)
            qWarning("[2D-render] frame %d: sanitized %d non-finite cell "
                     "depth(s) from the source", t, bad);
    }
    // Edge flux is optional — sources without it return false and leave
    // current_flux_ untouched. applyCurrentFlux_ checks the size and bails.
    const std::size_t nEdgeValues = tris_.size() * 3;
    if (!have_edge_geom_
        || edge_length_.size() != nEdgeValues
        || edge_nx_.size()     != nEdgeValues
        || edge_ny_.size()     != nEdgeValues)
    {
        have_edge_geom_ = source_->readEdgeGeometry(edge_length_,
                                                    edge_nx_, edge_ny_);
        if (!have_edge_geom_
            || edge_length_.size() != nEdgeValues
            || edge_nx_.size()     != nEdgeValues
            || edge_ny_.size()     != nEdgeValues)
        {
            edge_length_.clear();
            edge_nx_.clear();
            edge_ny_.clear();
            have_edge_geom_ = false;
        }
    }

    if (source_->readEdgeFluxAt(t, current_flux_)) {
        edge_flux_probe_ = 1;
    } else {
        current_flux_.clear();
    }

    // Auto-track running max depth (unless the user explicitly pinned it).
    // The finite guard is belt-and-braces after the sanitize above: one Inf
    // peak would otherwise collapse the colour ramp for the rest of the run
    // (max_depth_ never shrinks).
    if (!max_depth_user_set_ && !current_depths_.empty()) {
        const float peak = *std::max_element(current_depths_.begin(),
                                              current_depths_.end());
        if (std::isfinite(peak) && peak > max_depth_) max_depth_ = peak;
    }

    applyCurrentDepths_();
    applyCurrentFlux_();
    if (graphics_item_) graphics_item_->geometryChanged();
    if (arrows_item_)   arrows_item_->geometryChanged();
    emit currentTimeChanged(t);
    emit currentDateTimeChanged(source_->simTimeAt(t));
    // VS.8 — route the tick through the canvas's standard repaint channel
    // so the QSG path invalidates its cached framebuffer (the QGraphicsItem
    // updates above only reach the CPU scene buffer).
    emit repaintRequested();
    return true;
}

void SWMM2DResultsLayer::setCurrentTimeIndex(int t)
{
    if (!source_) return;
    const int n = source_->timeCount();
    if (n == 0) return;
    t = std::clamp(t, 0, n - 1);
    if (t == current_time_idx_ && !current_depths_.empty()) return;
    loadFrame_(t);
}

void SWMM2DResultsLayer::refreshCurrentFrame()
{
    if (!source_) return;
    if (source_->isLive() && !live_render_enabled_) return;
    const int n = source_->timeCount();
    if (n == 0) return;
    const int t = std::clamp(current_time_idx_ < 0 ? 0 : current_time_idx_,
                             0, n - 1);
    loadFrame_(t);
}

void SWMM2DResultsLayer::setQsgOwnsRendering(bool own)
{
    if (m_qsgOwnsRendering == own) return;
    m_qsgOwnsRendering = own;
    // Both pipelines need a repaint: the CPU items must clear (or redraw)
    // and the canvas must regrab the QSG frame.
    if (graphics_item_) graphics_item_->geometryChanged();
    if (arrows_item_)   arrows_item_->geometryChanged();
    emit repaintRequested();
}

void SWMM2DResultsLayer::setLiveRenderEnabled(bool on)
{
    if (live_render_enabled_ == on) return;
    live_render_enabled_ = on;
    // Re-arm follow-live on enable so the view jumps to the newest streamed
    // frame; refreshTimeRange() advances the cursor (and itself issues a
    // repaint via setCurrentTimeIndex when the frame changes).
    if (on) {
        follow_live_ = true;
        refreshTimeRange();
    }
    // Force a repaint regardless: on disable the QSG renderer must run once
    // more to drop its node tree (view clears); on enable a same-frame
    // refresh would otherwise not redraw.
    if (graphics_item_) graphics_item_->geometryChanged();
    if (arrows_item_)   arrows_item_->geometryChanged();
    emit repaintRequested();
}

QString SWMM2DResultsLayer::sourceDescription() const
{
    if (!source_) return tr("(no source)");
    if (source_->isLive()) return tr("live (engine)");
    if (auto *h5 = dynamic_cast<const HDF5Mesh2DSource *>(source_.get()))
        return h5->path();
    return tr("(in-memory results)");
}

QVector<QPair<QString, QString>> SWMM2DResultsLayer::extendedMetadata() const
{
    QVector<QPair<QString, QString>> md;
    if (!source_) return md;

    md.append({ tr("Live"), source_->isLive() ? tr("yes (counts grow during run)")
                                               : tr("no") });
    md.append({ tr("Vertices"),          QString::number(source_->vertexCount()) });
    md.append({ tr("Cells (triangles)"), QString::number(source_->triangleCount()) });

    const int n = source_->timeCount();
    md.append({ tr("Time steps"), QString::number(n) });
    if (n > 0) {
        const QDateTime t0 = source_->simTimeAt(0);
        const QDateTime t1 = source_->simTimeAt(n - 1);
        if (t0.isValid() && t1.isValid())
            md.append({ tr("Time range"),
                        QStringLiteral("%1 → %2")
                            .arg(t0.toString(Qt::ISODate), t1.toString(Qt::ISODate)) });
    }

    md.append({ tr("Dry depth"), QStringLiteral("%1 m").arg(dryDepth(), 0, 'g', 3) });
    md.append({ tr("Velocity / flux"),
                hasEdgeFluxData() ? tr("available") : tr("none") });
    return md;
}

void SWMM2DResultsLayer::refreshTimeRange()
{
    if (!source_) return;

    // Live master gate: when live rendering is switched off mid-run, freeze the
    // 2D layer ENTIRELY — and crucially, return BEFORE emitting timeRangeChanged.
    // Frames keep streaming into the source via pushDepths even while off, so
    // emitting the (growing) range would re-normalise the animation cursor
    // against an ever-larger timespan and make the slider drift even though the
    // displayed frame is held — the "slider still progressing when live updating
    // is off" bug. The range and cursor now hold until live rendering is
    // re-enabled (setLiveRenderEnabled(true) calls refreshTimeRange to catch up).
    if (source_->isLive() && !live_render_enabled_) return;

    const int n = source_->timeCount();
    emit timeRangeChanged(0, std::max(0, n - 1));
    if (n <= 0) return;

    // Live (streaming) source: seed the first frame so the map isn't blank, then
    // follow the newest frame as ticks arrive so the run animates in place. The
    // moment the user drives playback (slider / Play), follow_live_ is cleared
    // (AnimationController::driverSetStep) so their chosen frame stays put; it
    // re-arms when they seek back to the latest frame.
    if (source_->isLive()) {
        if (current_time_idx_ < 0) { setCurrentTimeIndex(0); return; }
        if (follow_live_ && current_time_idx_ < n - 1)
            setCurrentTimeIndex(n - 1);
        return;
    }
    // Non-live (e.g. a file source still being appended) keeps follow-latest.
    if (current_time_idx_ < n - 1)
        setCurrentTimeIndex(n - 1);
}

void SWMM2DResultsLayer::closeSource()
{
    // Drop the source's underlying file handle.  unique_ptr destruction
    // runs HDF5Mesh2DSource::~HDF5Mesh2DSource → Mesh2DH5Reader::~Mesh2DH5Reader
    // → H5Fclose, releasing the file so the engine can truncate / rewrite.
    source_.reset();
    current_time_idx_ = -1;
    current_depths_.clear();
    current_flux_.clear();
    // Per-tri animated state is cleared on next setSource via
    // rebuildSceneGeometry_(); for the moment, just blank the canvas.
    for (auto &t : m_sceneTris) {
        t.depth = 0.0f;
        t.dv0 = t.dv1 = t.dv2 = 0.0f;
        t.vx = t.vy = t.vmag = 0.0f;
    }
    m_sceneEdges.clear();   // Issue 3 — drop the wireframe with the source
    vvx_.clear();           // V1 — drop the per-vertex velocity field
    vvy_.clear();
    have_velocity_ = false;
    if (graphics_item_) graphics_item_->geometryChanged();
    if (arrows_item_)   arrows_item_->geometryChanged();
    emit timeRangeChanged(0, 0);
    emit currentTimeChanged(-1);
    emit currentDateTimeChanged(QDateTime());
    emit repaintRequested();
}

void SWMM2DResultsLayer::setDryDepth(double d)
{
    const bool changed = (d != dry_depth_);
    dry_depth_ = d;
    // Drive the vector overlays' wet/dry cutoff from the same model DRY_DEPTH so
    // the rendered wet extent (cells AND velocity/flow vectors) matches what the
    // solver considers wet. The paint pass gates vectors on
    // max(sublayer dryDepthCutoff, layer dryDepth) (see paint loops ~757/896),
    // so the sublayer floor must track the layer threshold, not the 1 cm default.
    // Sync unconditionally (idempotent): the layer default already equals some
    // models' DRY_DEPTH, so an early return on `changed` would leave the 1 cm
    // sublayer floor in place and keep clipping shallow vectors.
    if (m_velocityVectorSublayer && m_velocityVectorSublayer->vectorStyle())
        m_velocityVectorSublayer->vectorStyle()->setDryDepthCutoff(d);
    if (changed) {
        if (graphics_item_) graphics_item_->geometryChanged();
        if (arrows_item_)   arrows_item_->geometryChanged();
        emit repaintRequested();
    }
}

void SWMM2DResultsLayer::setMaxDepth(double d)
{
    if (d == max_depth_) return;
    max_depth_ = d;
    max_depth_user_set_ = true;
    if (graphics_item_) graphics_item_->geometryChanged();
    emit repaintRequested();
}

// Gap A3.1 — velocity knobs are facades over the VelocityVector sublayer.
// The sublayer model is what the paint pass gates on / reads from, so the
// dialog's legacy setters and the layer tree's sublayer toggles converge on
// one state. Sublayer style mutations emit styleChanged → invalidated →
// repaint, so no manual geometryChanged is needed on those paths.

bool SWMM2DResultsLayer::velocityVectorsVisible() const
{
    return m_velocityVectorSublayer ? m_velocityVectorSublayer->isVisible()
                                    : velocity_visible_;
}

void SWMM2DResultsLayer::setVelocityVectorsVisible(bool v)
{
    velocity_visible_ = v;
    if (m_velocityVectorSublayer) {
        m_velocityVectorSublayer->setVisible(v);
        return;
    }
    if (arrows_item_) arrows_item_->geometryChanged();
    emit repaintRequested();
}

qreal SWMM2DResultsLayer::velocityOpacity() const
{
    return m_velocityVectorSublayer ? m_velocityVectorSublayer->opacity()
                                    : velocity_opacity_;
}

void SWMM2DResultsLayer::setVelocityOpacity(qreal alpha)
{
    alpha = std::clamp<qreal>(alpha, 0.0, 1.0);
    velocity_opacity_ = alpha;
    if (m_velocityVectorSublayer) {
        m_velocityVectorSublayer->setOpacity(alpha);
        return;
    }
    if (arrows_item_) arrows_item_->geometryChanged();
    emit repaintRequested();
}

double SWMM2DResultsLayer::velocityArrowScale() const
{
    if (m_velocityVectorSublayer && m_velocityVectorSublayer->vectorStyle())
        return m_velocityVectorSublayer->vectorStyle()->glyphLengthScalePxPerMps();
    return velocity_arrow_scale_;
}

void SWMM2DResultsLayer::setVelocityArrowScale(double scale)
{
    if (scale <= 0.0) return;
    velocity_arrow_scale_ = scale;
    if (m_velocityVectorSublayer && m_velocityVectorSublayer->vectorStyle()) {
        m_velocityVectorSublayer->vectorStyle()->setGlyphLengthScalePxPerMps(scale);
        return;
    }
    if (arrows_item_) arrows_item_->geometryChanged();
    emit repaintRequested();
}

void SWMM2DResultsLayer::setMaxVelocity(double v)
{
    if (v <= 0.0) return;
    if (v == max_velocity_) return;
    max_velocity_ = v;
    max_velocity_user_set_ = true;
    if (arrows_item_) arrows_item_->geometryChanged();
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Color-ramp + contour styling (Slice CF.MVP-fix.3)
// ---------------------------------------------------------------------------

void SWMM2DResultsLayer::setColorRampStyle(ColorRampStyle s)
{
    if (s == color_ramp_style_) return;
    color_ramp_style_ = s;
    if (graphics_item_) graphics_item_->geometryChanged();
    emit repaintRequested();
}

void SWMM2DResultsLayer::setColorClasses(int n)
{
    n = std::clamp(n, 2, 64);
    if (n == color_classes_) return;
    color_classes_ = n;
    if (graphics_item_) graphics_item_->geometryChanged();
    emit repaintRequested();
}

// Gap A3.1 — band / isoline knobs are facades over the ContourBand /
// Isoline sublayers (visibility, sublayer opacity, style-bag props). Paint
// gates on sublayer visibility and prefers the style-bag values, so before
// this change the dialog's setters wrote fields paint never consulted —
// every "Show bands"/"Levels"/"Colour" control in the 2D panel was dead.

bool SWMM2DResultsLayer::filledContours() const
{
    return m_contourBandSublayer ? m_contourBandSublayer->isVisible()
                                 : filled_contours_;
}

void SWMM2DResultsLayer::setFilledContours(bool on)
{
    filled_contours_ = on;
    if (m_contourBandSublayer) {
        m_contourBandSublayer->setVisible(on);
        return;
    }
    if (graphics_item_) graphics_item_->geometryChanged();
}

double SWMM2DResultsLayer::filledContoursOpacity() const
{
    return m_contourBandSublayer ? m_contourBandSublayer->opacity()
                                 : filled_contours_opacity_;
}

void SWMM2DResultsLayer::setFilledContoursOpacity(double a)
{
    a = std::clamp(a, 0.0, 1.0);
    filled_contours_opacity_ = a;
    if (m_contourBandSublayer) {
        m_contourBandSublayer->setOpacity(a);
        return;
    }
    if (graphics_item_) graphics_item_->geometryChanged();
}

int SWMM2DResultsLayer::filledContoursLevels() const
{
    if (m_contourBandSublayer && m_contourBandSublayer->bandStyle())
        return m_contourBandSublayer->bandStyle()->bandCount();
    return filled_contours_levels_;
}

void SWMM2DResultsLayer::setFilledContoursLevels(int n)
{
    n = std::clamp(n, 2, 32);
    filled_contours_levels_ = n;
    if (m_contourBandSublayer && m_contourBandSublayer->bandStyle()) {
        m_contourBandSublayer->bandStyle()->setBandCount(n);
        return;
    }
    if (graphics_item_) graphics_item_->geometryChanged();
}

bool SWMM2DResultsLayer::isolines() const
{
    return m_isolineSublayer ? m_isolineSublayer->isVisible() : isolines_;
}

void SWMM2DResultsLayer::setIsolines(bool on)
{
    isolines_ = on;
    if (m_isolineSublayer) {
        m_isolineSublayer->setVisible(on);
        return;
    }
    if (graphics_item_) graphics_item_->geometryChanged();
}

int SWMM2DResultsLayer::isolinesLevels() const
{
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        return m_isolineSublayer->isolineStyle()->isoValueCount();
    return isolines_levels_;
}

void SWMM2DResultsLayer::setIsolinesLevels(int n)
{
    n = std::clamp(n, 1, 32);
    isolines_levels_ = n;
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle()) {
        m_isolineSublayer->isolineStyle()->setIsoValueCount(n);
        return;
    }
    if (graphics_item_) graphics_item_->geometryChanged();
}

QColor SWMM2DResultsLayer::isolinesColor() const
{
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        return m_isolineSublayer->isolineStyle()->color();
    return isolines_color_;
}

void SWMM2DResultsLayer::setIsolinesColor(QColor c)
{
    isolines_color_ = c;
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle()) {
        m_isolineSublayer->isolineStyle()->setColor(c);
        return;
    }
    if (graphics_item_) graphics_item_->geometryChanged();
}

double SWMM2DResultsLayer::isolinesWidth() const
{
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        return m_isolineSublayer->isolineStyle()->lineWidthPx();
    return isolines_width_;
}

void SWMM2DResultsLayer::setIsolinesWidth(double px)
{
    px = std::clamp(px, 0.25, 10.0);
    isolines_width_ = px;
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle()) {
        m_isolineSublayer->isolineStyle()->setLineWidthPx(px);
        return;
    }
    if (graphics_item_) graphics_item_->geometryChanged();
}

std::pair<float, int> SWMM2DResultsLayer::currentPeak() const
{
    if (current_depths_.empty()) return {0.0f, -1};
    auto it = std::max_element(current_depths_.begin(), current_depths_.end());
    return { *it, static_cast<int>(std::distance(current_depths_.begin(), it)) };
}

void SWMM2DResultsLayer::setCurrentSimTime(QDateTime t)
{
    if (!source_ || !t.isValid()) return;
    const int n = source_->timeCount();
    if (n == 0) return;

    // 2026-07-19 — binary search for the nearest frame (slider scrub perf;
    // replaces the O(n) linear scan). The H5 /time dataset is monotonically
    // increasing, so bisect index space for the first frame >= t, then pick
    // the nearer of that frame and its predecessor. simTimeAt() is O(1), so
    // no need to materialise the times.
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        const QDateTime tm = source_->simTimeAt(mid);
        if (tm.isValid() && tm < t)
            lo = mid + 1;
        else
            hi = mid;
    }
    int best = lo;
    if (lo > 0) {
        const QDateTime tPrev = source_->simTimeAt(lo - 1);
        const QDateTime tCurr = source_->simTimeAt(lo);
        if (tPrev.isValid() &&
            (!tCurr.isValid() ||
             std::abs(t.msecsTo(tPrev)) <= std::abs(t.msecsTo(tCurr))))
            best = lo - 1;
    }
    // Issue 2 — this setter is only ever reached from a user scrub (the slider)
    // or animation sync, never from the live-ingest path (refreshTimeRange calls
    // setCurrentTimeIndex directly). Take manual playback control of a LIVE
    // source so the chosen frame stays put instead of being yanked to the newest
    // streamed frame on the next tick; re-arm follow-live only when the user
    // lands on the latest frame (mirrors AnimationController::driverSetStep).
    if (source_->isLive())
        follow_live_ = (best >= n - 1);
    setCurrentTimeIndex(best);
}

void SWMM2DResultsLayer::setCurrentSimTimeAsOf(QDateTime cursor)
{
    if (!source_ || !cursor.isValid()) return;
    const int n = source_->timeCount();
    if (n == 0) return;

    // Largest index whose frame time is at or before the cursor (causal floor).
    // Clamp to 0 (hold the first frame) when the cursor precedes every frame.
    //
    // 2026-07-19 — binary search (slider scrub perf; replaces the O(n)
    // linear scan). /time is monotonic: bisect for the first frame >
    // cursor; the causal floor is the frame before it.
    int lo = 0, hi = n;                    // half-open [lo, hi)
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        const QDateTime tm = source_->simTimeAt(mid);
        if (tm.isValid() && tm <= cursor)
            lo = mid + 1;
        else
            hi = mid;
    }
    const int best = std::max(0, lo - 1);
    // Issue 2 — same manual-control handoff as setCurrentSimTime: a causal sync
    // from the slider/animation must not leave a LIVE source free to snap back
    // to the newest frame. Re-arm only at the latest frame.
    if (source_->isLive())
        follow_live_ = (best >= n - 1);
    setCurrentTimeIndex(best);
}

// ---------------------------------------------------------------------------
// CF.3 — cell selection / highlight
// ---------------------------------------------------------------------------

QVector<int> SWMM2DResultsLayer::pickCellsInRect(const QRectF& sceneRect) const
{
    QVector<int> hits;
    if (sceneRect.isNull() || m_sceneTris.isEmpty()) return hits;
    hits.reserve(m_sceneTris.size() / 4);
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        if (sceneRect.contains(m_sceneTris[i].centroid))
            hits.push_back(i);
    }
    return hits;
}

QVector<int> SWMM2DResultsLayer::pickCellsInPolygon(const QPolygonF& scenePoly) const
{
    QVector<int> hits;
    if (scenePoly.size() < 3 || m_sceneTris.isEmpty()) return hits;
    hits.reserve(m_sceneTris.size() / 4);
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        if (scenePoly.containsPoint(m_sceneTris[i].centroid, Qt::OddEvenFill))
            hits.push_back(i);
    }
    return hits;
}

namespace {
// Barycentric point-in-triangle test for scene-space coordinates.
inline bool pointInTriangle(const QPointF& p,
                            const QPointF& a,
                            const QPointF& b,
                            const QPointF& c)
{
    if (!std::isfinite(p.x()) || !std::isfinite(p.y()) ||
        !std::isfinite(a.x()) || !std::isfinite(a.y()) ||
        !std::isfinite(b.x()) || !std::isfinite(b.y()) ||
        !std::isfinite(c.x()) || !std::isfinite(c.y()))
        return false;
    if (!(std::abs(twiceSignedArea(a, b, c)) > 1e-18))
        return false;

    const double d1 = (p.x() - b.x()) * (a.y() - b.y()) -
                      (a.x() - b.x()) * (p.y() - b.y());
    const double d2 = (p.x() - c.x()) * (b.y() - c.y()) -
                      (b.x() - c.x()) * (p.y() - c.y());
    const double d3 = (p.x() - a.x()) * (c.y() - a.y()) -
                      (c.x() - a.x()) * (p.y() - a.y());
    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}
} // namespace

int SWMM2DResultsLayer::pickCellAt(const QPointF& scenePt) const
{
    // Fast path: the spatial grid narrows the search to the one cell containing
    // the point — O(candidates) instead of an O(n) scan over all cells. The
    // containing triangle is guaranteed to be registered in that cell.
    if (!m_triGrid.isEmpty()) {
        const int *b = nullptr, *e = nullptr;
        m_triGrid.candidatesAtPoint(scenePt.x(), scenePt.y(), b, e);
        for (const int *p = b; p < e; ++p) {
            const auto& t = m_sceneTris[*p];
            if (pointInTriangle(scenePt, t.a, t.b, t.c))
                return *p;
        }
        return -1;
    }

    // Fallback: linear scan when the grid isn't built. Stops at first hit.
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        const auto& t = m_sceneTris[i];
        if (pointInTriangle(scenePt, t.a, t.b, t.c))
            return i;
    }
    return -1;
}

float SWMM2DResultsLayer::depthAtSceneNow(const QPointF& scenePt) const
{
    const int idx = pickCellAt(scenePt);
    if (idx < 0 || idx >= m_sceneTris.size()) return 0.0f;
    return m_sceneTris[idx].depth;
}

float SWMM2DResultsLayer::depthAtSceneInterp(const QPointF& scenePt) const
{
    return depthAtCellInterp(pickCellAt(scenePt), scenePt);
}

float SWMM2DResultsLayer::clampToDrivingHead_(int idx, double depthBlend,
                                              double w, double v, double u,
                                              double sd0, double sd1, double sd2) const
{
    if (idx < 0 || idx >= static_cast<int>(tris_.size()))
        return std::max(0.0f, float(depthBlend));
    const auto& tri = tris_[idx];
    const int nVert = static_cast<int>(vz_.size());
    auto z = [&](int k) -> double {
        const int vi = tri[k];
        return (vi >= 0 && vi < nVert) ? vz_[vi] : 0.0;
    };
    const double z0 = z(0), z1 = z(1), z2 = z(2);
    // Driving HGL = the highest free surface η_v = z_v + sd_v among the WET
    // vertices only (sd_v > 0 — water actually stands above their bed). A dry
    // vertex carries sd_v = 0 (its η collapses to its own ground), so a high-
    // and-dry ridge vertex must NOT count toward the cap — otherwise the blend
    // would march water up the adverse slope to the crest with no head to drive
    // it. With no wet vertex there is no driving head at all → depth 0.
    bool   wet    = false;
    double maxEta = 0.0;
    auto consider = [&](double zk, double sdk) {
        if (sdk > 0.0) {
            const double e = zk + sdk;
            if (!wet || e > maxEta) { maxEta = e; wet = true; }
        }
    };
    consider(z0, sd0); consider(z1, sd1); consider(z2, sd2);
    if (!wet) return 0.0f;
    // Ground at the sample under the SAME weights (ground + depth = η blended
    // linearly). capDepth is the depth that puts the water surface exactly at
    // the driving HGL; clamp the blend to it so WSE never exceeds the head.
    const double groundInterp = w * z0 + v * z1 + u * z2;
    const double capDepth      = maxEta - groundInterp;
    return std::max(0.0f, float(std::min(depthBlend, capDepth)));
}

float SWMM2DResultsLayer::depthAtCellInterp(int idx, const QPointF& scenePt) const
{
    // Interpolate depth at a point whose containing cell is already known
    // (e.g. the cached Sample::triIdx during animation), skipping the cell
    // search entirely. Bounds-checks idx so a stale cached index can't crash —
    // worst case it returns 0 until the profile rebuilds.
    if (idx < 0 || idx >= m_sceneTris.size()) return 0.0f;
    // Dry-cell mask: a cell the solver marks dry this frame (cell-mean depth
    // below DRY_DEPTH) carries no water, even if its vertices borrowed a free
    // surface from a still-wet neighbour. Without this gate the barycentric
    // blend paints water into a cell the engine considers dry — water with no
    // driving head in its own cell. Mirrors the per-cell max mask the envelope
    // path uses, so the current-frame line and the max envelope treat dry cells
    // identically.
    if (idx < static_cast<int>(current_depths_.size()) &&
        current_depths_[idx] < float(dry_depth_))
        return 0.0f;
    const auto& t = m_sceneTris[idx];
    // No whole-cell wet/dry gate: dv0/dv1/dv2 are now the SIGNED VFR depth
    // (η_vertex − z_vertex) from applyCurrentDepths_, so their barycentric blend
    // is η_interp − ground_interp at the sample, going negative over the dry
    // part of a partially-wet cell. Clamping the result at 0 (below) lets the
    // profile water line meet ground at the sub-cell intercept instead of
    // stepping at the cell boundary.
    // Barycentric weights against (a,b,c) — identical construction to
    // SWMM2DMeshLayer::sampleZAt so ground and depth share one interpolation
    // basis (ground_bary + depth_bary is linear = interpolating per-vertex WSE).
    const double v0x = t.c.x() - t.a.x(), v0y = t.c.y() - t.a.y();
    const double v1x = t.b.x() - t.a.x(), v1y = t.b.y() - t.a.y();
    const double v2x = scenePt.x() - t.a.x(), v2y = scenePt.y() - t.a.y();
    const double d00 = v0x * v0x + v0y * v0y;
    const double d01 = v0x * v1x + v0y * v1y;
    const double d11 = v1x * v1x + v1y * v1y;
    const double d20 = v2x * v0x + v2y * v0y;
    const double d21 = v2x * v1x + v2y * v1y;
    const double denom = d00 * d11 - d01 * d01;
    if (denom == 0.0) return t.depth;            // degenerate — cell value
    const double u = (d11 * d20 - d01 * d21) / denom;   // weight for c
    const double v = (d00 * d21 - d01 * d20) / denom;   // weight for b
    const double w = 1.0 - u - v;                       // weight for a
    const double blend = w * double(t.dv0) + v * double(t.dv1) + u * double(t.dv2);
    // Clamp the implied water surface to the cell's driving HGL so an edge
    // sample with extrapolating weights can't push water above the head the
    // wet vertices supply (then floor at 0 for the sub-cell dry side).
    return clampToDrivingHead_(idx, blend, w, v, u, t.dv0, t.dv1, t.dv2);
}

bool SWMM2DResultsLayer::velocityAtScene(const QPointF& scenePt,
                                         float& outVx, float& outVy) const
{
    outVx = outVy = 0.0f;
    if (vvx_.empty() || vvy_.empty()) return false;
    const int idx = pickCellAt(scenePt);
    if (idx < 0 || idx >= m_sceneTris.size() ||
        idx >= static_cast<int>(tris_.size()))
        return false;
    // A cell the solver marks dry this frame carries no flow, even if its
    // vertices borrowed a velocity from a still-wet neighbour. Gate on the
    // cell's own mean depth so arrows never appear in a dry cell.
    if (idx < static_cast<int>(current_depths_.size()) &&
        current_depths_[idx] < float(dry_depth_))
        return false;

    const auto& t   = m_sceneTris[idx];
    const auto& tri = tris_[idx];
    const int nVert = static_cast<int>(vvx_.size());
    auto vget = [&](int k, const std::vector<float>& arr) -> double {
        const int vi = tri[k];
        return (vi >= 0 && vi < nVert) ? double(arr[vi]) : 0.0;
    };
    // Barycentric weights — identical basis to depthAtCellInterp (a→w, b→v,
    // c→u) so the velocity field shares the depth field's interpolation.
    const double v0x = t.c.x() - t.a.x(), v0y = t.c.y() - t.a.y();
    const double v1x = t.b.x() - t.a.x(), v1y = t.b.y() - t.a.y();
    const double v2x = scenePt.x() - t.a.x(), v2y = scenePt.y() - t.a.y();
    const double d00 = v0x * v0x + v0y * v0y;
    const double d01 = v0x * v1x + v0y * v1y;
    const double d11 = v1x * v1x + v1y * v1y;
    const double d20 = v2x * v0x + v2y * v0y;
    const double d21 = v2x * v1x + v2y * v1y;
    const double denom = d00 * d11 - d01 * d01;
    double wA = 1.0, wB = 0.0, wC = 0.0;
    if (denom != 0.0) {
        const double u = (d11 * d20 - d01 * d21) / denom;   // weight for c
        const double v = (d00 * d21 - d01 * d20) / denom;   // weight for b
        wC = u; wB = v; wA = 1.0 - u - v;
    }
    outVx = float(wA * vget(0, vvx_) + wB * vget(1, vvx_) + wC * vget(2, vvx_));
    outVy = float(wA * vget(0, vvy_) + wB * vget(1, vvy_) + wC * vget(2, vvy_));
    return true;
}

namespace {
// Issue 4 — single source of truth for the depth-weighted free-surface
// reconstruction that turns per-cell mean depths into per-vertex SIGNED depths
// (η_v − z_v). Shared by the per-frame animated fill (applyCurrentDepths_) and
// the historical max-depth envelope (maxDepthPerVertex) so the two cannot drift
// (CLAUDE.md §4.01): the envelope is then provably the per-vertex temporal max
// of the EXACT field the animation displays, so the max-depth surface and the
// animated surface agree at every mesh vertex at that vertex's peak frame.
//
// Weighting η by the cell depth h lets deep, fully-wet cells (whose flat-cell
// η equals the true horizontal water level) dominate shoreline vertices instead
// of thin, transiently-wet cells up a slope dragging the surface up the wall.
//
// \p vsum,\p wsum are caller-owned scratch (resized here) so the per-frame
// maxDepthPerVertex loop never allocates. \p outVertexDepth is resized to
// vz.size(); a vertex with no wetted incident cell yields 0. After the call
// \p wsum[v] > 0 iff vertex v had a wetted incident cell this frame.

// Free-surface elevation η of one cell from its mean depth h̄ — inverts the
// planar-bed stage–storage relation through the cell's three vertex
// elevations (mirror of the engine's cellFreeSurfaceElevation,
// VertexReconstruction.cpp). For a PARTIALLY wet cell (η < highest vertex)
// this pools the water over the wetted fraction instead of the flat closure
// z̄ + h̄, which overstates η on cells spanning a bed step — the other half of
// the "water climbs the step" artifact. Fully wet reduces exactly to z̄ + h̄.
inline double cellEtaFromMeanDepth(double h, double za, double zb, double zc)
{
    double z1 = za, z2 = zb, z3 = zc;
    if (z1 > z2) std::swap(z1, z2);
    if (z2 > z3) std::swap(z2, z3);
    if (z1 > z2) std::swap(z1, z2);

    if (!(h > 0.0)) return z1;

    const double zbar   = (z1 + z2 + z3) / 3.0;
    const double relief = z3 - z1;
    if (relief < 1.0e-9 || h >= z3 - zbar)
        return zbar + h;                              // flat / fully wet

    const double h_at_z2 = (z2 - z1) * (z2 - z1) / (3.0 * relief);
    if (h <= h_at_z2)                                 // waterline below z2
        return z1 + std::cbrt(3.0 * h * (z2 - z1) * relief);

    // Waterline between z2 and z3: safeguarded Newton on the bracket.
    const double denom = 3.0 * relief * (z3 - z2);
    double lo = z2, hi = z3;
    double eta = zbar + h;
    if (eta <= lo || eta >= hi) eta = 0.5 * (lo + hi);
    for (int it = 0; it < 64; ++it) {
        const double dz3 = z3 - eta;
        const double f  = (eta - zbar) + dz3 * dz3 * dz3 / denom - h;
        if (f > 0.0) hi = eta; else lo = eta;
        const double df = 1.0 - dz3 * dz3 / (relief * (z3 - z2));
        double next = (df > 1.0e-12) ? eta - f / df : 0.5 * (lo + hi);
        if (next <= lo || next >= hi) next = 0.5 * (lo + hi);
        if (std::abs(next - eta) < 1.0e-12 * (1.0 + relief)) return next;
        eta = next;
    }
    return eta;
}

void reconstructVertexSignedDepths(
    const std::vector<std::array<int, 3>>& tris,
    const std::vector<float>&  cellDepths,
    const std::vector<float>&  cellZc,
    const std::vector<double>& vz,
    float dryF,
    std::vector<float>& vsum,
    std::vector<float>& wsum,
    std::vector<float>& outVertexDepth)
{
    const int nTri  = static_cast<int>(tris.size());
    const int nVert = static_cast<int>(vz.size());
    vsum.assign(static_cast<size_t>(nVert), 0.0f);
    wsum.assign(static_cast<size_t>(nVert), 0.0f);
    const int nCell = std::min<int>(nTri, static_cast<int>(cellDepths.size()));
    for (int i = 0; i < nCell; ++i) {
        const float h = cellDepths[i];
        // NaN-robust dry skip: `h < dryF` is false for NaN, so a non-finite
        // depth would NOT be skipped and would poison vsum/wsum at all three
        // vertices (→ streaked triangle fans in the Gouraud fill).
        if (!(h >= dryF)) continue;                // only wetted cells contribute
        const auto& tri = tris[i];
        // Cell free surface via the planar-bed stage–storage inversion when
        // the three vertex elevations are usable; flat closure z_c + h as the
        // fallback (out-of-range index / nodata z).
        double eta;
        if (tri[0] >= 0 && tri[0] < nVert &&
            tri[1] >= 0 && tri[1] < nVert &&
            tri[2] >= 0 && tri[2] < nVert &&
            std::isfinite(vz[tri[0]]) && std::isfinite(vz[tri[1]]) &&
            std::isfinite(vz[tri[2]])) {
            eta = cellEtaFromMeanDepth(double(h), vz[tri[0]], vz[tri[1]],
                                       vz[tri[2]]);
        } else {
            eta = double(cellZc[i]) + double(h);
        }
        const float we = h * float(eta);           // depth-weighted η contribution
        if (!std::isfinite(we)) continue;          // non-finite z_c must not spread
        for (int k = 0; k < 3; ++k) {
            const int vi = tri[k];
            if (vi < 0 || vi >= nVert) continue;
            vsum[vi] += we;
            wsum[vi] += h;
        }
    }
    outVertexDepth.assign(static_cast<size_t>(nVert), 0.0f);
    for (int v = 0; v < nVert; ++v)
        if (wsum[v] > 0.0f) {
            const double d = double(vsum[v]) / double(wsum[v]) - vz[v];
            // Non-finite vertex elevation (e.g. DTM nodata) must yield a dry
            // vertex, not a NaN that the colour ramp turns into garbage.
            outVertexDepth[v] = std::isfinite(d) ? float(d) : 0.0f;
        }
}
} // namespace

QVector<float> SWMM2DResultsLayer::maxDepthPerCell() const
{
    QVector<float> out;
    if (!source_) return out;
    const int nTri = source_->triangleCount();
    const int nT   = source_->timeCount();
    if (nTri <= 0 || nT <= 0) return out;

    out = QVector<float>(nTri, 0.0f);
    std::vector<float> buf;
    for (int t = 0; t < nT; ++t) {
        if (!source_->readDepthsAt(t, buf)) continue;
        const int n = std::min<int>(nTri, static_cast<int>(buf.size()));
        for (int i = 0; i < n; ++i)
            if (buf[i] > out[i]) out[i] = buf[i];
    }
    return out;
}

QVector<float> SWMM2DResultsLayer::maxDepthPerVertex() const
{
    QVector<float> out;
    if (!source_) return out;
    const int nVert = source_->vertexCount();
    const int nTri  = static_cast<int>(tris_.size());
    const int nT    = source_->timeCount();
    if (nVert <= 0 || nTri <= 0 || nT <= 0) return out;
    if (static_cast<int>(cellZc_.size()) != nTri) return out;

    const float dryF = float(dry_depth_);

    // Max inundation = the per-vertex temporal max of the EXACT per-frame field
    // the animation displays. reconstructVertexSignedDepths is the same helper
    // applyCurrentDepths_ uses, so the static envelope equals the animated
    // surface at each vertex's peak frame (CLAUDE.md §4.01 — one arithmetic, no
    // drift). NB: at interior sample points the interpolated envelope can sit
    // slightly above the interpolated animation (interp-of-max ≥ max-of-interp);
    // the two coincide exactly at mesh vertices, which is where consistency is
    // observable. (Run once on profile build / time-range change, not per tick.)
    std::vector<float>   vsum, wsum, frameDepth;   // scratch reused across frames
    std::vector<float>   vertMax(size_t(nVert), 0.0f);
    std::vector<uint8_t> vertWet(size_t(nVert), 0);
    std::vector<float>   buf;
    for (int t = 0; t < nT; ++t) {
        if (!source_->readDepthsAt(t, buf)) continue;
        reconstructVertexSignedDepths(tris_, buf, cellZc_, vz_, dryF,
                                      vsum, wsum, frameDepth);
        for (int v = 0; v < nVert; ++v) {
            if (wsum[v] <= 0.0f) continue;             // dry this frame
            if (!vertWet[v] || frameDepth[v] > vertMax[v]) {
                vertMax[v] = frameDepth[v];            // already signed (η_v − z_v)
                vertWet[v] = 1;
            }
        }
    }
    out = QVector<float>(nVert, 0.0f);
    for (int v = 0; v < nVert; ++v)
        if (vertWet[v]) out[v] = vertMax[v];
    return out;
}

float SWMM2DResultsLayer::maxDepthAtSceneInterp(const QPointF& scenePt,
                                                const QVector<float>& vertMax) const
{
    const int idx = pickCellAt(scenePt);
    if (idx < 0 || idx >= m_sceneTris.size() || idx >= static_cast<int>(tris_.size()))
        return 0.0f;
    const auto& t   = m_sceneTris[idx];
    const auto& tri = tris_[idx];
    auto vm = [&](int k) {
        const int vi = tri[k];
        return (vi >= 0 && vi < vertMax.size()) ? double(vertMax[vi]) : 0.0;
    };
    // Same barycentric construction as depthAtSceneInterp (a→w, b→v, c→u).
    const double v0x = t.c.x() - t.a.x(), v0y = t.c.y() - t.a.y();
    const double v1x = t.b.x() - t.a.x(), v1y = t.b.y() - t.a.y();
    const double v2x = scenePt.x() - t.a.x(), v2y = scenePt.y() - t.a.y();
    const double d00 = v0x * v0x + v0y * v0y;
    const double d01 = v0x * v1x + v0y * v1y;
    const double d11 = v1x * v1x + v1y * v1y;
    const double d20 = v2x * v0x + v2y * v0y;
    const double d21 = v2x * v1x + v2y * v1y;
    const double denom = d00 * d11 - d01 * d01;
    if (denom == 0.0) return vertMax.isEmpty() ? 0.0f : std::max(0.0f, float(vm(0)));
    const double u = (d11 * d20 - d01 * d21) / denom;   // weight for c
    const double v = (d00 * d21 - d01 * d20) / denom;   // weight for b
    const double w = 1.0 - u - v;                       // weight for a
    // vertMax is the SIGNED per-vertex max depth (η_max − z). Clamp the blend to
    // the cell's driving HGL (max η_v) so extrapolating edge weights can't lift
    // the envelope above the head the wet vertices reached, then floor at 0 so
    // the envelope tapers to dry inside partially-wet cells.
    const double blend = w * vm(0) + v * vm(1) + u * vm(2);
    return clampToDrivingHead_(idx, blend, w, v, u, vm(0), vm(1), vm(2));
}

void SWMM2DResultsLayer::highlightCells(const QSet<int>& triIdxSet)
{
    if (m_highlighted == triIdxSet) return;
    m_highlighted = triIdxSet;
    if (graphics_item_)
        graphics_item_->update();
    // The QGraphicsItem's own update() does NOT refresh the MapCanvas's cached
    // scene buffer — the canvas only re-renders on repaintRequested/invalidate.
    // Emit it so the highlight actually appears on screen.
    emit repaintRequested();
    emit highlightedCellsChanged();
}

void SWMM2DResultsLayer::clearHighlights()
{
    if (m_highlighted.isEmpty()) return;
    m_highlighted.clear();
    if (graphics_item_)
        graphics_item_->update();
    emit repaintRequested();
    emit highlightedCellsChanged();
}

// ---------------------------------------------------------------------------
// Scene plumbing
// ---------------------------------------------------------------------------

void SWMM2DResultsLayer::rebuildSceneGeometry_()
{
    m_sceneTris.clear();
    m_triGrid.clear();   // drop the stale index; every early-return path below
                         // leaves an empty grid so pickCellAt falls back safely.
    cellZc_.clear();
    eta_vsum_.clear();
    eta_wsum_.clear();
    vdepth_.clear();
    vvx_.clear();
    vvy_.clear();
    m_sceneEdges.clear();
    m_sceneVerts.clear();
    m_sceneBBox = QRectF();

    // QSG-2D-1M — every scene-geometry rebuild is a new geometry revision,
    // including the early-return "now empty" outcomes below: the renderer
    // must notice the geometry went away too.
    ++m_geomRevision;

    if (!source_) return;

    if (!source_->readMeshGeometry(vx_, vy_, vz_, tris_)) {
        return;
    }
    if (tris_.empty()) return;
    if (vx_.empty() || vy_.size() != vx_.size() || vz_.size() != vx_.size()) {
        qWarning("[2D-render] invalid mesh geometry vector sizes "
                 "(x=%llu y=%llu z=%llu)",
                 static_cast<unsigned long long>(vx_.size()),
                 static_cast<unsigned long long>(vy_.size()),
                 static_cast<unsigned long long>(vz_.size()));
        vx_.clear();
        vy_.clear();
        vz_.clear();
        tris_.clear();
        return;
    }

    // Scene-space points: identity transform + Y-flip (scene grows downward,
    // matching the mesh layer's convention). CRS transforms come later via
    // onCanvasCRSChanged when the layer SRS framework is wired.
    const int nVerts = static_cast<int>(vx_.size());
    const int normalizedStartIndex = normalizeOneBasedConnectivity(tris_, nVerts);
    if (normalizedStartIndex != 0) {
        qWarning("[2D-render] normalized 1-based 2D triangle connectivity "
                 "to zero-based vertex ids");
    }

    QVector<QPointF> scenePts;
    scenePts.reserve(nVerts);
    std::vector<char> validVerts(static_cast<size_t>(nVerts), 1);
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    int badVerts = 0;
    bool haveValidVertex = false;
    for (int i = 0; i < nVerts; ++i) {
        double sx = vx_[i];
        double sy = -vy_[i];
        if (!std::isfinite(vx_[i]) || !std::isfinite(vy_[i])
            || !std::isfinite(vz_[i])) {
            validVerts[size_t(i)] = 0;
            sx = sy = 0.0;
            ++badVerts;
        }
        scenePts.append(QPointF(sx, sy));
        if (!validVerts[size_t(i)]) continue;
        haveValidVertex = true;
        if (sx < minX) minX = sx;
        if (sx > maxX) maxX = sx;
        if (sy < minY) minY = sy;
        if (sy > maxY) maxY = sy;
    }
    if (!haveValidVertex) {
        qWarning("[2D-render] 2D mesh has no finite vertices; results layer "
                 "geometry was not built");
        vx_.clear();
        vy_.clear();
        vz_.clear();
        tris_.clear();
        return;
    }
    if (badVerts > 0) {
        qWarning("[2D-render] ignored %d non-finite 2D mesh vertex/vertices",
                 badVerts);
    }
    m_sceneBBox = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    setExtent(MapExtent(minX, -maxY, maxX, -minY));  // un-flip Y for layer extent
    // QSG-2D-1M — shared-vertex cache for the renderer. If any source
    // vertex was non-finite, leave this empty so indexed fill/unique-marker
    // paths fall back to the validated per-triangle buffers below.
    m_sceneVerts = (badVerts == 0) ? scenePts : QVector<QPointF>();

    m_sceneTris.resize(static_cast<int>(tris_.size()));
    int badTris = 0;
    for (int i = 0; i < static_cast<int>(tris_.size()); ++i) {
        SceneTri& st = m_sceneTris[i];
        st.depth = 0.0f;
        st.vx = st.vy = st.vmag = 0.0f;

        const auto& tri = tris_[i];
        const bool indicesOk = triIndicesInRange(tri, nVerts)
            && validVerts[size_t(tri[0])]
            && validVerts[size_t(tri[1])]
            && validVerts[size_t(tri[2])];
        if (!indicesOk) {
            tris_[i] = {-1, -1, -1};
            st.a = st.b = st.c = st.centroid = QPointF();
            ++badTris;
            continue;
        }

        st.a = scenePts[tri[0]];
        st.b = scenePts[tri[1]];
        st.c = scenePts[tri[2]];
        if (!(std::abs(twiceSignedArea(st.a, st.b, st.c)) > 1e-18)) {
            tris_[i] = {-1, -1, -1};
            st.a = st.b = st.c = st.centroid = QPointF();
            ++badTris;
            continue;
        }

        st.centroid = QPointF((st.a.x() + st.b.x() + st.c.x()) / 3.0,
                               (st.a.y() + st.b.y() + st.c.y()) / 3.0);
    }
    if (badTris > 0) {
        qWarning("[2D-render] collapsed %d invalid 2D mesh triangle(s); "
                 "depth contours will skip them", badTris);
    }

    // Issue 3 — deduplicated wireframe edge set (each undirected edge stored
    // once). Mirrors SWMM2DMeshLayer::rebuildSceneGeometry's pushEdge so the
    // results wireframe matches the mesh layer and no shared edge is stroked
    // twice; double-stroking the translucent edge colour darkened the overlay.
    {
        QSet<QPair<int, int>> seen;
        seen.reserve(static_cast<int>(tris_.size()) * 3);
        m_sceneEdges.reserve(static_cast<int>(tris_.size()) * 3);
        auto pushEdge = [&](int a, int b) {
            if (a == b || a < 0 || b < 0 || a >= nVerts || b >= nVerts) return;
            const QPair<int, int> key = (a < b) ? qMakePair(a, b) : qMakePair(b, a);
            if (seen.contains(key)) return;
            seen.insert(key);
            const double dx   = vx_[a] - vx_[b];
            const double dy   = vy_[a] - vy_[b];
            const double dist = std::sqrt(dx * dx + dy * dy);
            const double dz   = std::abs(vz_[a] - vz_[b]);
            SceneEdge e;
            e.a     = scenePts[a];
            e.b     = scenePts[b];
            e.slope = (dist > 1e-9) ? float(dz / dist) : 0.0f;
            m_sceneEdges.append(e);
        };
        for (const auto& tri : tris_) {
            pushEdge(tri[0], tri[1]);
            pushEdge(tri[1], tri[2]);
            pushEdge(tri[2], tri[0]);
        }
    }

    // Build the point-location index over the triangle bboxes (parallel to
    // m_sceneTris) so pickCellAt — and the profile depth sampling that rides on
    // it — is O(cell) instead of O(n).
    QVector<QRectF> triBBoxes(m_sceneTris.size());
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        const SceneTri& t = m_sceneTris[i];
        if (!triIndicesInRange(tris_[size_t(i)], nVerts)
            || !(std::abs(twiceSignedArea(t.a, t.b, t.c)) > 1e-18)) {
            triBBoxes[i] = QRectF();
            continue;
        }
        const double bMinX = std::min({t.a.x(), t.b.x(), t.c.x()});
        const double bMaxX = std::max({t.a.x(), t.b.x(), t.c.x()});
        const double bMinY = std::min({t.a.y(), t.b.y(), t.c.y()});
        const double bMaxY = std::max({t.a.y(), t.b.y(), t.c.y()});
        triBBoxes[i] = QRectF(QPointF(bMinX, bMinY), QPointF(bMaxX, bMaxY));
    }
    m_triGrid.rebuild(triBBoxes);

    // Per-cell centroid bed elevation = (z0+z1+z2)/3 (the engine's tri_cz). The
    // engine's free surface is η = z_centroid + h, so this is the only per-cell
    // geometry the per-frame reconstruction needs. Stored as float parallel to
    // tris_; the per-frame eta_* scratch is sized here too so
    // applyCurrentDepths_ never allocates.
    cellZc_.resize(m_sceneTris.size());
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        const auto& tri = tris_[i];
        if (!triIndicesInRange(tri, nVerts)) {
            cellZc_[i] = 0.0f;
            continue;
        }
        const double zc = (vz_[tri[0]] + vz_[tri[1]] + vz_[tri[2]]) / 3.0;
        cellZc_[i] = std::isfinite(zc) ? float(zc) : 0.0f;
    }
    eta_vsum_.assign(nVerts, 0.0f);
    eta_wsum_.assign(nVerts, 0.0f);
    vdepth_.assign(nVerts, 0.0f);

    // Pull time-invariant edge geometry once per source swap. When the source
    // can't provide it (older engine without the bulk API, .h5 file without
    // the geometry datasets), the velocity overlay simply stays empty.
    edge_length_.clear();
    edge_nx_.clear();
    edge_ny_.clear();
    have_edge_geom_ = false;
    if (source_) {
        have_edge_geom_ = source_->readEdgeGeometry(edge_length_,
                                                     edge_nx_, edge_ny_);
        if (!have_edge_geom_) {
            edge_length_.clear();
            edge_nx_.clear();
            edge_ny_.clear();
        }
    }
}

void SWMM2DResultsLayer::applyCurrentDepths_()
{
    if (current_depths_.size() != tris_.size()) return;
    const int nTri = static_cast<int>(tris_.size());
    for (int i = 0; i < nTri; ++i) {
        m_sceneTris[i].depth = current_depths_[i];
    }

    const int nVert = static_cast<int>(vx_.size());
    if (static_cast<int>(cellZc_.size()) != nTri) return;   // geometry not built yet

    // Prefer the engine/HDF5 reconstructed vertex field when available.
    // Live packets arrive after the cell-depth packet for the same elapsed
    // time, so refreshCurrentFrame() re-enters here and upgrades smooth fills,
    // marching-triangle contours, and profile samples without advancing time.
    bool haveSourceVertexDepths = false;
    if (source_ && current_time_idx_ >= 0) {
        std::vector<float> srcVertexDepths;
        if (source_->readVertexDepthsAt(current_time_idx_, srcVertexDepths)
            && static_cast<int>(srcVertexDepths.size()) == nVert)
        {
            vdepth_ = std::move(srcVertexDepths);
            // The source field is SIGNED (η_v − z_v): negatives over the dry
            // side of partially wet cells drive the sub-cell shoreline
            // intercept, so they MUST survive. Only non-finite values are
            // sanitised (nodata z / poisoned frames must not spread).
            for (float &d : vdepth_)
                if (!std::isfinite(d)) d = 0.0f;
            haveSourceVertexDepths = true;
        }
    }

    if (!haveSourceVertexDepths) {
        // Sub-cell free-surface reconstruction. The engine reports a per-cell
        // mean depth h = V/A under a flat-cell closure whose free surface is
        // η = z_centroid + h (horizontal at equilibrium).
        // reconstructVertexSignedDepths turns those per-cell depths into
        // SIGNED per-vertex depths (η_v − z_v) so the marching-triangles
        // bands/isolines and the Gouraud fill cut the wet/dry shoreline through
        // partially-wet cells instead of snapping to cell edges. It is the SAME
        // helper maxDepthPerVertex() reduces over time, so the animated surface
        // and the max-depth envelope share fallback arithmetic. The eta_vsum_/
        // eta_wsum_ members are reused as scratch so this hot path never
        // allocates.
        reconstructVertexSignedDepths(tris_, current_depths_, cellZc_, vz_,
                                      float(dry_depth_), eta_vsum_, eta_wsum_,
                                      vdepth_);
    }
    for (int i = 0; i < nTri; ++i) {
        const auto& tri = tris_[i];
        SceneTri& st = m_sceneTris[i];
        st.dv0 = (tri[0] >= 0 && tri[0] < nVert) ? vdepth_[tri[0]] : 0.0f;
        st.dv1 = (tri[1] >= 0 && tri[1] < nVert) ? vdepth_[tri[1]] : 0.0f;
        st.dv2 = (tri[2] >= 0 && tri[2] < nVert) ? vdepth_[tri[2]] : 0.0f;
    }
}

void SWMM2DResultsLayer::applyCurrentFlux_()
{
    have_velocity_ = false;
    if (!have_edge_geom_ || tris_.empty() ||
        current_flux_.size() != tris_.size() * 3 ||
        edge_length_.size() != tris_.size() * 3) {
        // No flux data this tick — wipe per-tri velocities so an old frame
        // doesn't ghost when the user scrubs into a flux-less region.
        for (auto& st : m_sceneTris) {
            st.vx = st.vy = st.vmag = 0.0f;
        }
        std::fill(vvx_.begin(), vvx_.end(), 0.0f);   // V1 — drop stale vertex field
        std::fill(vvy_.begin(), vvy_.end(), 0.0f);
        return;
    }

    // RT0 cell-centred velocity reconstruction. For each triangle with three
    // outward unit normals n_e and signed normal speeds q_e = flux_e/length_e,
    // solve the 3×2 least-squares system N · v ≈ q in closed form via the
    // normal equations: (NᵀN) v = Nᵀ q, with NᵀN a 2×2 SPD matrix.
    constexpr float kQMax  = 10.0f;       // clamp |q_e| against wet/dry-front spikes (m/s)
    const double    dryEps = dry_depth_;

    float running_max = 0.0f;
    const int nTri = static_cast<int>(tris_.size());
    for (int t = 0; t < nTri; ++t) {
        SceneTri& st = m_sceneTris[t];

        if (st.depth < dryEps) {
            st.vx = st.vy = st.vmag = 0.0f;
            continue;
        }

        double a00 = 0.0, a01 = 0.0, a11 = 0.0;  // NᵀN entries
        double b0  = 0.0, b1  = 0.0;             // Nᵀ q entries
        for (int e = 0; e < 3; ++e) {
            const int idx = t * 3 + e;
            const double nx = edge_nx_[idx];
            const double ny = edge_ny_[idx];
            const double len = edge_length_[idx];
            if (len <= 1e-12) continue;
            double q = current_flux_[idx] / len;
            // Clamp against wet/dry-front spikes (flux can blow up when
            // length-integrated edge flux divides by a near-zero length).
            // NaN passes both clamp comparisons and would poison the normal
            // equations → NaN vx/vy → the glyph pass extrudes garbage
            // geometry (screen-crossing spike triangles). Skip the edge.
            if (!std::isfinite(q)) continue;
            if (q >  kQMax) q =  kQMax;
            if (q < -kQMax) q = -kQMax;
            a00 += nx * nx;
            a01 += nx * ny;
            a11 += ny * ny;
            b0  += nx * q;
            b1  += ny * q;
        }
        const double det = a00 * a11 - a01 * a01;
        // NaN-robust degeneracy gate: `abs(NaN) < eps` is false, so the
        // inverted form is required to zero the cell instead of emitting
        // NaN velocities.
        if (!(std::abs(det) >= 1e-12)) {
            st.vx = st.vy = st.vmag = 0.0f;
            continue;
        }
        const double inv_det = 1.0 / det;
        const double vx_model = ( a11 * b0 - a01 * b1) * inv_det;
        const double vy_model = (-a01 * b0 + a00 * b1) * inv_det;

        // Scene-space velocity: vy is flipped so the arrow points the right
        // way after the rebuildSceneGeometry_() Y-flip on vertex coords.
        st.vx   = static_cast<float>(vx_model);
        st.vy   = static_cast<float>(-vy_model);
        st.vmag = static_cast<float>(std::sqrt(vx_model * vx_model +
                                                vy_model * vy_model));
        if (st.vmag > running_max) running_max = st.vmag;
    }

    have_velocity_ = (running_max > 0.0f);

    // Auto-grow the velocity ramp's upper bound (unless the user pinned it).
    if (!max_velocity_user_set_ && running_max > max_velocity_) {
        max_velocity_ = running_max;
    }

    // V1 (Issue 5) — reconstruct a per-VERTEX velocity field as the
    // depth-weighted average of incident wet-cell vectors, so velocityAtScene
    // (used by the grid-sampled glyph placement) varies smoothly across cell
    // boundaries instead of stepping at each cell. Depth weighting keeps thin,
    // transiently-wet cells from skewing the shoreline direction — the velocity
    // peer of the depth blend in applyCurrentDepths_.
    const int nVert = static_cast<int>(vx_.size());
    vvx_.assign(static_cast<size_t>(nVert), 0.0f);
    vvy_.assign(static_cast<size_t>(nVert), 0.0f);
    if (have_velocity_) {
        std::vector<float> wsum(static_cast<size_t>(nVert), 0.0f);
        for (int t = 0; t < nTri; ++t) {
            const SceneTri& st = m_sceneTris[t];
            if (st.depth < dryEps || st.vmag <= 0.0f) continue;
            const float wgt = st.depth;               // depth weight
            const auto& tri = tris_[t];
            for (int k = 0; k < 3; ++k) {
                const int vi = tri[k];
                if (vi < 0 || vi >= nVert) continue;
                vvx_[vi] += st.vx * wgt;
                vvy_[vi] += st.vy * wgt;
                wsum[vi] += wgt;
            }
        }
        for (int v = 0; v < nVert; ++v)
            if (wsum[v] > 0.0f) { vvx_[v] /= wsum[v]; vvy_[v] /= wsum[v]; }
    }
}

// ---------------------------------------------------------------------------
// OpenSWMMVisLayer interface
// ---------------------------------------------------------------------------

void SWMM2DResultsLayer::populateScene(QGraphicsScene* scene,
                                         const MapExtent& /*canvasExtent*/,
                                         const SpatialReferenceSystem* /*canvasSRS*/)
{
    if (!scene) return;
    if (graphics_item_) {
        scene->removeItem(graphics_item_);
        delete graphics_item_;
        graphics_item_ = nullptr;
    }
    if (arrows_item_) {
        scene->removeItem(arrows_item_);
        delete arrows_item_;
        arrows_item_ = nullptr;
    }
    graphics_item_ = new SWMM2DResultsGraphicsItem(this);
    scene->addItem(graphics_item_);
    arrows_item_ = new SWMM2DVelocityArrowsItem(this);
    scene->addItem(arrows_item_);
    graphics_item_->geometryChanged();
    arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::depopulateScene(QGraphicsScene* scene)
{
    // VS.1 — capture the vacated region before deletion so we can mark it
    // dirty afterward. Otherwise toggling this layer OFF removes the items
    // but leaves their pixels on screen until an unrelated repaint.
    QRectF dirty;
    if (graphics_item_) {
        dirty = dirty.united(graphics_item_->sceneBoundingRect());
        if (scene) scene->removeItem(graphics_item_);
        delete graphics_item_;
        graphics_item_ = nullptr;
    }
    if (arrows_item_) {
        dirty = dirty.united(arrows_item_->sceneBoundingRect());
        if (scene) scene->removeItem(arrows_item_);
        delete arrows_item_;
        arrows_item_ = nullptr;
    }
    if (scene) {
        if (dirty.isNull())
            scene->update();
        else
            scene->invalidate(dirty);
    }
}

void SWMM2DResultsLayer::refreshScene(QGraphicsScene* scene,
                                        const MapExtent& canvasExtent,
                                        const SpatialReferenceSystem* canvasSRS)
{
    if (!graphics_item_) {
        populateScene(scene, canvasExtent, canvasSRS);
        return;
    }
    graphics_item_->geometryChanged();
    if (arrows_item_) arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::onCanvasCRSChanged(
        const SpatialReferenceSystem* /*newCanvasSRS*/)
{
    // Reprojection seam — mirror SWMM2DMeshLayer's transform path when the
    // layer gains an explicit SRS. For the MVP demo case (no reprojection)
    // the identity transform set up in rebuildSceneGeometry_() is sufficient.
    rebuildSceneGeometry_();
    applyCurrentDepths_();
    applyCurrentFlux_();
    if (graphics_item_) graphics_item_->geometryChanged();
    if (arrows_item_)   arrows_item_->geometryChanged();
}

// ---------------------------------------------------------------------------
// Legend builder — graduated rows that mirror what paint() actually draws.
// ---------------------------------------------------------------------------

QList<OpenSWMM::Render::LegendSymbolItem>
SWMM2DResultsLayer::sublayerLegendItems() const
{
    using OpenSWMM::Render::LegendSymbolItem;
    using OpenSWMM::Render::SymbolLayer;
    using OpenSWMM::Render::SymbolLayerKind;

    QList<LegendSymbolItem> out;

    const double dry = dry_depth_;
    const double mx  = std::max(dry + 1e-6, max_depth_);

    // 2026-06-21 — the depth color ramp legend section was removed along with
    // the depth-ramp sublayer. Contour bands (below) now carry the depth
    // legend rows.

    // Filled contour bands — the depth fill (default on). Single header row
    // + N band rows.
    if (filledContours()) {
        LegendSymbolItem header;
        header.label = tr("Depth (m)");
        header.sublayerId = m_contourBandSublayer
                              ? m_contourBandSublayer->id() : QString();
        out.append(header);

        const OpenSWMM::Render::ContourBandStyle *bs =
            m_contourBandSublayer ? m_contourBandSublayer->bandStyle() : nullptr;
        const int alpha = std::clamp(
            int(std::lround(filledContoursOpacity() * 255.0)), 0, 255);
        const QString bandSubId =
            m_contourBandSublayer ? m_contourBandSublayer->id() : QString();

        if (bs) {
            // Slice US.2 — rows straight from the classification scheme so the
            // value labels follow the actual class edges (equal interval,
            // quantile, Jenks, …) and pick up per-class colour overrides. The
            // swatch colours match colorForBand the renderer paints with.
            const auto items = bs->scheme().legendItems(dry, mx);
            for (LegendSymbolItem item : items) {
                item.sublayerId = bandSubId;
                item.symbol.opacity = filledContoursOpacity();
                out.append(item);
            }
        } else {
            double bandLo = dry, bandHi = mx;
            const int n = std::max(2, filledContoursLevels());
            for (int i = 0; i < n; ++i) {
                const double t = (i + 0.5) / n;
                int r = 0, g = 0, b = 0, a = 0;
                inundationColorRgba(bandLo + t * (bandHi - bandLo),
                                    bandLo, bandHi, r, g, b, a);
                QColor c(r, g, b);
                c.setAlpha(alpha);

                LegendSymbolItem item;
                const double lo = bandLo + double(i)     / n * (bandHi - bandLo);
                const double hi = bandLo + double(i + 1) / n * (bandHi - bandLo);
                item.label      = QStringLiteral("%1 – %2 m")
                                    .arg(lo, 0, 'g', 3).arg(hi, 0, 'g', 3);
                item.sublayerId = bandSubId;
                item.range      = { lo, hi };
                item.classKey   = QString::number(i);
                SymbolLayer sl;
                sl.kind = SymbolLayerKind::SimpleFill;
                OpenSWMM::Render::SymbolProps::writeColor(sl.props, QStringLiteral("color"), c);
                item.symbol.layers.append(sl);
                out.append(item);
            }
        }
    }

    // Iso-line strokes — one row for the regular lines, plus an index-contour
    // row when emphasis is enabled.
    if (isolines()) {
        const OpenSWMM::Render::IsolineStyle *is =
            m_isolineSublayer ? m_isolineSublayer->isolineStyle() : nullptr;

        LegendSymbolItem item;
        if (is && is->levelMode() ==
                OpenSWMM::Render::IsolineStyle::LevelMode::FixedInterval)
            item.label = tr("Depth isolines (every %1 m)")
                             .arg(is->levelInterval(), 0, 'g', 3);
        else
            item.label = tr("Depth isolines (%1 levels)").arg(isolinesLevels());
        item.sublayerId = m_isolineSublayer ? m_isolineSublayer->id()
                                            : QString();
        SymbolLayer sl;
        sl.kind = SymbolLayerKind::SimpleLine;
        OpenSWMM::Render::SymbolProps::writeColor(sl.props, QStringLiteral("color"),
                                isolinesColor());
        sl.props.insert(QStringLiteral("width"), isolinesWidth());
        item.symbol.layers.append(sl);
        out.append(item);

        if (is && is->indexEvery() >= 2) {
            LegendSymbolItem idx;
            idx.label = tr("Index contours (every %1th)").arg(is->indexEvery());
            idx.sublayerId = m_isolineSublayer->id();
            SymbolLayer isl;
            isl.kind = SymbolLayerKind::SimpleLine;
            OpenSWMM::Render::SymbolProps::writeColor(isl.props, QStringLiteral("color"),
                                    isolinesColor());
            isl.props.insert(QStringLiteral("width"), is->indexWidthPx());
            idx.symbol.layers.append(isl);
            out.append(idx);
        }
    }

    // Velocity arrows — visible only when the feed has data. Delegates to
    // the sublayer so the rows match the painted colour bands exactly.
    if (velocityVectorsVisible() && have_velocity_) {
        if (m_velocityVectorSublayer) {
            out.append(m_velocityVectorSublayer->legendSymbolItems());
        } else {
            LegendSymbolItem item;
            item.label = tr("Velocity (max %1 m/s)").arg(max_velocity_, 0, 'g', 3);
            SymbolLayer sl;
            sl.kind = SymbolLayerKind::SimpleMarker;
            sl.props.insert(QStringLiteral("shape"), QStringLiteral("arrow"));
            int r = 0, g = 0, b = 0;
            velocityColorRgb(max_velocity_, max_velocity_, r, g, b);
            OpenSWMM::Render::SymbolProps::writeColor(sl.props, QStringLiteral("color"),
                                    QColor(r, g, b, 230));
            sl.props.insert(QStringLiteral("size"), 12.0);
            item.symbol.layers.append(sl);
            out.append(item);
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Slice U-6 — surface each sublayer's style bag as a subject for the
// unified LayerStyleDialog. Each sublayer goes into the "Sublayers"
// section so the dialog presents them as nested tabs (Mesh fill / Depth
// ramp / Contour bands / Isolines / Velocity arrows).
// ---------------------------------------------------------------------------

// ─── Slice B.5b — Rule Model mirror over 2D-results decoration state ──

// ---------------------------------------------------------------------------
// Slice DM.3 — IAttributeProvider
// ---------------------------------------------------------------------------
//
// 2D results carry per-cell `depth`, `head`, and a 2-component
// velocity field; `vmag` is the precomputed magnitude. The renderer
// panels (DepthColorRamp, ContourBand, Isoline, VelocityVector rules)
// theme one of these. Category arg ignored — the 2D layer has no
// SWMM-category concept; mesh attributes apply layer-wide.

QVector<OpenSWMM::Render::AttributeField>
SWMM2DResultsLayer::availableAttributes(OpenSWMMVis::SwmmCategory /*cat*/) const
{
    using OpenSWMM::Render::AttributeField;

    auto make = [](const char *name, const char *display,
                   const char *unit) -> AttributeField {
        AttributeField f;
        f.name        = QString::fromLatin1(name);
        f.displayName = QString::fromLatin1(display);
        f.type        = QMetaType::Double;
        f.isDynamic   = true;
        f.unit        = QString::fromLatin1(unit);
        return f;
    };

    QVector<AttributeField> out;
    out.append(make("depth", "depth (m)",                "m"));
    out.append(make("head",  "head (m)",                 "m"));
    out.append(make("vmag",  "velocity magnitude (m/s)", "m/s"));
    out.append(make("vx",    "velocity x (m/s)",         "m/s"));
    out.append(make("vy",    "velocity y (m/s)",         "m/s"));
    return out;
}

OpenSWMM::Render::RuleList *SWMM2DResultsLayer::ruleList()
{
    if (!m_ruleList)
        buildRuleListLazy();
    return m_ruleList.get();
}

const OpenSWMM::Render::RuleList *SWMM2DResultsLayer::ruleList() const
{
    if (!m_ruleList)
        buildRuleListLazy();
    return m_ruleList.get();
}

void SWMM2DResultsLayer::buildRuleListLazy() const
{
    using namespace OpenSWMM::Render;

    auto *self = const_cast<SWMM2DResultsLayer *>(this);
    m_ruleList = std::make_unique<RuleList>(self);

    // Six seed Rules covering the result-layer decoration set. Parallel to
    // SWMM2DMeshLayer's Rule List (Slice B.5b/Z.6a). The propagation
    // handlers below translate dialog edits on each Rule back to the
    // matching legacy sublayer style fields, which the existing paint
    // pipeline (depth ramp / contour bands / isolines / mesh edges / mesh
    // nodes) already reads each frame.
    struct Seed { const char *name; SymbolLayer (*build)(); };
    const Seed seeds[] = {
        {"Depth color ramp",
            [] { return RasterColorRampSymbolLayerSpec{}.toSymbolLayer(); }},
        {"Hillshade",
            [] { return HillshadeSymbolLayerSpec{}.toSymbolLayer(); }},
        {"Contour bands",
            [] {
                ContourSymbolLayerSpec s;
                s.mode = ContourMode::Filled;
                return s.toSymbolLayer();
            }},
        {"Contour lines",
            [] {
                ContourSymbolLayerSpec s;
                s.mode = ContourMode::Lines;
                return s.toSymbolLayer();
            }},
        {"Mesh edges",
            [] { return MeshEdgeSymbolLayerSpec{}.toSymbolLayer(); }},
        {"Mesh nodes",
            [] { return MeshNodeSymbolLayerSpec{}.toSymbolLayer(); }},
        // Slice AN.3 — 7th seed rule for the velocity-vector sublayer.
        // The painter (SWMM2DVelocityArrowsItem) already consumes
        // VelocityVectorStyle each frame; this rule + the back-prop
        // lambda below give the user a Symbology-tab editor surface.
        {"Velocity vectors",
            [] { return VelocityVectorSymbolLayerSpec{}.toSymbolLayer(); }},
    };

    QVector<Rule *> ruleHandles;
    ruleHandles.reserve(int(sizeof(seeds) / sizeof(*seeds)));
    for (const Seed &s : seeds) {
        auto single = std::make_unique<SingleSymbolRenderer>();
        SymbolStyle style = single->symbol();
        style.layers.clear();
        style.layers.append(s.build());
        single->setSymbol(style);
        Rule *r = m_ruleList->append(std::make_unique<Rule>(
            QString::fromLatin1(s.name), std::move(single)));
        ruleHandles.append(r);
    }

    // ── Slice Z.6a step 3 — Rule → legacy-style propagation ─────────────
    //
    // Same pattern as SWMM2DMeshLayer: each Rule's rendererReplaced signal
    // extracts the typed spec from the first SymbolLayer and writes the
    // relevant fields back onto the matching legacy sublayer style.
    // setDirty on the style triggers invalidated(), which the constructor
    // wired to graphics_item_->geometryChanged() — the next paint reads
    // the updated legacy fields.
    auto firstLayer = [](Rule *r) -> const SymbolLayer * {
        if (!r) return nullptr;
        auto *single = dynamic_cast<const SingleSymbolRenderer *>(r->renderer());
        if (!single) return nullptr;
        if (single->symbol().layers.isEmpty()) return nullptr;
        return &single->symbol().layers.first();
    };

    // ── Depth color ramp — REMOVED (2026-06-21) ─────────────────────────
    // The depth-ramp sublayer was removed (redundant with contour bands,
    // which now provide the depth fill). ruleHandles[0] ("Depth color ramp"
    // seed) is left as an inert entry with no propagation handler so the
    // handlers below keep their existing [1..6] indices (avoids a risky
    // blind reindex). FOLLOW-UP: drop the seed entry and reindex
    // ruleHandles[1..6] → [0..5] in a build-verified pass.

    // ── Hillshade (HillshadeSpec) — results layer doesn't paint hillshade
    //    in the QPainter path, but we still wire the strength multiplier
    //    onto the carried MeshFillStyle so it round-trips and a future
    //    hillshaded-results renderer can read it.
    //    Slice AN.4 — extended to write fillColor + useElevationRamp from
    //    the HillshadeSymbolStyleAdapter's prop keys. ────────────────
    QObject::connect(ruleHandles[1], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[1]);
            if (!layer || layer->kind != SymbolLayerKind::Hillshade) return;
            const auto spec = HillshadeSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *fill = self->m_meshFillSublayer; fill && fill->fillStyle()) {
                auto *st = fill->fillStyle();
                st->setHillshadeStrength(spec.strength);
                const auto &p = layer->props;
                if (p.contains(QStringLiteral("fillColor"))) {
                    const QColor c = p.value(QStringLiteral("fillColor")).value<QColor>();
                    if (c.isValid()) st->setFillColor(c);
                }
                if (p.contains(QStringLiteral("useElevationRamp")))
                    st->setUseElevationRamp(
                        p.value(QStringLiteral("useElevationRamp")).toBool());
            }
        });

    // ── Contour bands (ContourSpec mode=Filled) ─────────────────────────
    // Slice AN.4 — extended to write attribute / bandCount /
    // belowMinColor / aboveMaxColor / direct lowColor / highColor from
    // the ContourBandSymbolStyleAdapter's prop keys.
    QObject::connect(ruleHandles[2], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[2]);
            if (!layer || layer->kind != SymbolLayerKind::Contour) return;
            const auto spec = ContourSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_contourBandSublayer; sub && sub->bandStyle()) {
                auto *st = sub->bandStyle();
                const auto &p = layer->props;
                st->setBandCount(p.contains(QStringLiteral("bandCount"))
                    ? p.value(QStringLiteral("bandCount")).toInt()
                    : spec.binner.binCount());
                st->setSmoothBands(spec.smoothBands);
                const QColor specLow = !spec.ramp.stops.isEmpty()
                    ? spec.ramp.stops.first().second : QColor(0, 0, 255);
                const QColor specHigh = !spec.ramp.stops.isEmpty()
                    ? spec.ramp.stops.last().second : QColor(255, 0, 0);
                st->setLowColor(p.contains(QStringLiteral("lowColor"))
                    ? p.value(QStringLiteral("lowColor")).value<QColor>()
                    : specLow);
                st->setHighColor(p.contains(QStringLiteral("highColor"))
                    ? p.value(QStringLiteral("highColor")).value<QColor>()
                    : specHigh);
                if (p.contains(QStringLiteral("belowMinColor")))
                    st->setBelowMinColor(p.value(QStringLiteral("belowMinColor")).value<QColor>());
                if (p.contains(QStringLiteral("aboveMaxColor")))
                    st->setAboveMaxColor(p.value(QStringLiteral("aboveMaxColor")).value<QColor>());
                if (p.contains(QStringLiteral("attribute")))
                    st->setAttribute(p.value(QStringLiteral("attribute")).toString());
            }
        });

    // ── Contour lines (ContourSpec mode=Lines) ──────────────────────────
    // Slice AN.4 — extended to write attribute + dashPattern (from
    // IsolineSymbolStyleAdapter) and to honour explicit isoValueCount /
    // color / lineWidthPx / labels overrides from the prop bag.
    QObject::connect(ruleHandles[3], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[3]);
            if (!layer || layer->kind != SymbolLayerKind::Contour) return;
            const auto spec = ContourSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_isolineSublayer; sub && sub->isolineStyle()) {
                auto *st = sub->isolineStyle();
                const auto &p = layer->props;
                st->setIsoValueCount(p.contains(QStringLiteral("isoValueCount"))
                    ? p.value(QStringLiteral("isoValueCount")).toInt()
                    : spec.binner.binCount());
                st->setColor(p.contains(QStringLiteral("color"))
                    ? p.value(QStringLiteral("color")).value<QColor>()
                    : spec.lineColor);
                st->setLineWidthPx(p.contains(QStringLiteral("width"))
                    ? p.value(QStringLiteral("width")).toDouble()
                    : spec.lineWidthPx);
                st->setLabels(p.contains(QStringLiteral("labels"))
                    ? p.value(QStringLiteral("labels")).toBool()
                    : spec.labelEveryN > 0);
                if (p.contains(QStringLiteral("penStyle")))
                    st->setDashPattern(static_cast<Qt::PenStyle>(
                        p.value(QStringLiteral("penStyle")).toInt()));
                if (p.contains(QStringLiteral("attribute")))
                    st->setAttribute(p.value(QStringLiteral("attribute")).toString());
            }
        });

    // ── Mesh edges (MeshEdgeSpec) ───────────────────────────────────────
    QObject::connect(ruleHandles[4], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[4]);
            if (!layer || layer->kind != SymbolLayerKind::MeshEdge) return;
            const auto spec = MeshEdgeSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_meshEdgeSublayer; sub && sub->edgeStyle()) {
                auto *st = sub->edgeStyle();
                st->setColor(spec.color);
                st->setLineWidthPx(spec.width);
                st->setDashPattern(spec.penStyle);
                st->setUseSlopeDrivenWidth(spec.useSlopeDrivenWidth);
                st->setSlopeBreak(spec.slopeBreak);
                st->setWideWidthPx(spec.wideWidthPx);
                st->setWideColor(spec.wideColor);
            }
        });

    // ── Mesh nodes (MeshNodeSpec wraps MarkerSpec) ──────────────────────
    // Slice AN.4 — extended to write highlightTagged / taggedColor /
    // taggedSizePx from the MeshNodeSymbolStyleAdapter's prop keys.
    QObject::connect(ruleHandles[5], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[5]);
            if (!layer || layer->kind != SymbolLayerKind::MeshNode) return;
            const auto spec = MeshNodeSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_meshNodeSublayer; sub && sub->nodeStyle()) {
                auto *st = sub->nodeStyle();
                st->setColor(spec.marker.fillColor);
                st->setMarkerSizePx(spec.marker.sizePx);
                st->setOutlineColor(spec.marker.outlineColor);
                st->setOutlineWidthPx(spec.marker.outlineWidth);
                int shapeIdx = static_cast<int>(spec.marker.shape);
                if (shapeIdx < 0 || shapeIdx > 3) shapeIdx = 0;
                st->setShape(static_cast<MeshNodeStyle::MarkerShape>(shapeIdx));
                const auto &p = layer->props;
                if (p.contains(QStringLiteral("highlightTagged")))
                    st->setHighlightTagged(
                        p.value(QStringLiteral("highlightTagged")).toBool());
                if (p.contains(QStringLiteral("taggedColor"))) {
                    const QColor c = p.value(QStringLiteral("taggedColor")).value<QColor>();
                    if (c.isValid()) st->setTaggedColor(c);
                }
                if (p.contains(QStringLiteral("taggedSizePx")))
                    st->setTaggedSizePx(
                        p.value(QStringLiteral("taggedSizePx")).toDouble());
            }
        });

    // ── (Slice DM.3 IAttributeProvider impl follows the rule wiring
    //    block — search for "availableAttributes" below.) ─────────────

    // ── Velocity vectors (VelocityVectorSymbolLayerSpec) — Slice AN.3.
    //    Extracts the typed spec from the rule's first SymbolLayer and
    //    writes it onto VelocityVectorStyle. The existing painter
    //    (SWMM2DVelocityArrowsItem in swmm2dresultslayer.cpp) re-reads
    //    those fields each frame, so edits take effect on the next
    //    animation tick. ───────────────────────────────────────────────
    QObject::connect(ruleHandles[6], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[6]);
            if (!layer || layer->kind != SymbolLayerKind::VectorGlyph) return;
            const auto spec = VelocityVectorSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_velocityVectorSublayer; sub && sub->vectorStyle()) {
                auto *st = sub->vectorStyle();
                st->setGlyphLengthScalePxPerMps(spec.glyphLengthScalePxPerMps);
                st->setGlyphLengthMinPx        (spec.glyphLengthMinPx);
                st->setGlyphLengthMaxPx        (spec.glyphLengthMaxPx);
                st->setGlyphSpacingPx          (spec.glyphSpacingPx);
                st->setHeadSizePx              (spec.headSizePx);
                st->setColor                   (spec.color);
                st->setDryDepthCutoff          (spec.dryDepthCutoff);
            }
        });
}

std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
SWMM2DResultsLayer::styleSubjects()
{
    using openswmmvis::ui::ILayerStyleSubject;
    using openswmmvis::ui::LayerStyleSubject;

    std::vector<std::unique_ptr<ILayerStyleSubject>> out;

    auto add = [&](OpenSWMM::Render::ISublayer *sub, const QString &section) {
        if (!sub || !sub->style()) return;
        out.push_back(std::make_unique<LayerStyleSubject>(
            sub->displayName(), sub->style(), sub->id(), section));
    };

    const QString sect = QStringLiteral("Sublayers");
    add(m_meshFillSublayer,         sect);
    add(m_meshEdgeSublayer,         sect);
    add(m_meshNodeSublayer,         sect);
    add(m_cellDepthFillSublayer,    sect);
    add(m_smoothDepthFillSublayer,  sect);
    add(m_contourBandSublayer,      sect);
    add(m_isolineSublayer,          sect);
    add(m_velocityVectorSublayer,   sect);

    return out;
}
