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

#include "contour/marchingtriangles.h"
#include "io/mesh2dh5reader.h"
#include "map/mapextent.h"

#include "render/ifeaturerenderer.h"
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

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>
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
        const auto& tris = layer_->m_sceneTris;
        if (tris.isEmpty()) return;

        const QRectF exposed  = option->exposedRect;
        const double dryDepth = layer_->dryDepth();
        const double maxDepth = layer_->maxDepth();
        const auto   rampStyle = layer_->colorRampStyle();
        const int    nClasses  = std::max(2, layer_->colorClasses());

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
        auto *depthSub   = layer_->depthRampSublayer();
        auto *bandSub    = layer_->contourBandSublayer();
        auto *isolineSub = layer_->isolineSublayer();

        const bool paintHeatmap = (!depthSub   || depthSub->isVisible());
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

        // --- Pass 1: per-cell heatmap. P3 — when the DepthColorRampSublayer
        // style is present, the cell colour is driven by its low/high ramp
        // colours (+ log option) so the user's ramp edits actually render;
        // otherwise we fall back to the legacy inundation ramp. Range still
        // spans [dryDepth, maxDepth] (the layer's auto range); per-frame /
        // user range modes are layered on in P4. Graduated mode quantises to
        // colorClasses bins.
        const OpenSWMM::Render::DepthColorRampStyle *drs =
            depthSub ? depthSub->rampStyle() : nullptr;
        const bool graduated =
            (rampStyle == SWMM2DResultsLayer::ColorRampStyle::Graduated);

        auto rampLerp = [](const QColor &lo, const QColor &hi, double f) -> QColor {
            f = std::clamp(f, 0.0, 1.0);
            auto mix = [&](int a0, int b0) { return int(std::lround(a0 + (b0 - a0) * f)); };
            return QColor(mix(lo.red(), hi.red()), mix(lo.green(), hi.green()),
                          mix(lo.blue(), hi.blue()), mix(lo.alpha(), hi.alpha()));
        };
        auto colorForDepth = [&](double depth) -> QColor {
            if (depth < dryDepth || maxDepth <= dryDepth)
                return QColor(0, 0, 0, 0);   // dry → transparent
            double f = std::clamp((depth - dryDepth) / (maxDepth - dryDepth), 0.0, 1.0);
            if (drs && drs->useLogScale() && dryDepth > 0.0 && depth > 0.0
                && maxDepth > dryDepth) {
                f = std::clamp((std::log(depth) - std::log(dryDepth)) /
                               (std::log(maxDepth) - std::log(dryDepth)), 0.0, 1.0);
            }
            if (graduated) {
                const int bin = std::min(nClasses - 1, int(f * double(nClasses)));
                f = (double(bin) + 0.5) / double(nClasses);
            }
            if (drs)
                return rampLerp(drs->lowColor(), drs->highColor(), f);
            int r, g, b, a;
            const double d = dryDepth + f * (maxDepth - dryDepth);
            inundationColorRgba(d, dryDepth, maxDepth, r, g, b, a);
            return QColor(r, g, b, a);
        };

        if (paintHeatmap)
        for (const auto& t : tris) {
            // Bounding-box cull
            const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
            const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
            const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
            const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
            if (!exposed.isNull() &&
                (maxX < exposed.left()  || minX > exposed.right() ||
                 maxY < exposed.top()   || minY > exposed.bottom())) continue;

            const QColor c = colorForDepth(t.depth);
            if (c.alpha() == 0) continue;  // dry → don't paint

            p->setBrush(c);
            const QPointF pts[3] = { t.a, t.b, t.c };
            p->drawConvexPolygon(pts, 3);
        }

        // --- Pass 2 (optional): filled isobands. Phase 9 — sublayer gate +
        // sublayer-style-driven band count. The ContourBandStyle.bandCount
        // Q_PROPERTY is the authoritative number of bands; the legacy
        // filledContoursLevels() is the fallback when no sublayer is bound.
        if (paintBands && maxDepth > dryDepth) {
            using namespace OpenSWMM::Contour;
            const int bandCount = (bandSub && bandSub->bandStyle())
                ? bandSub->bandStyle()->bandCount()
                : layer_->filledContoursLevels();
            const auto levels = evenlySpacedLevelsInclusive(
                dryDepth, maxDepth, bandCount);
            if (levels.size() >= 2) {
                auto extract = [](const SWMM2DResultsLayer::SceneTri &t,
                                  QPointF &p0, QPointF &p1, QPointF &p2,
                                  double  &v0, double  &v1, double  &v2) {
                    p0 = t.a; p1 = t.b; p2 = t.c;
                    v0 = double(t.dv0);
                    v1 = double(t.dv1);
                    v2 = double(t.dv2);
                };
                const auto bands = marchingTrianglesIsobands(tris, levels, extract);
                const double alphaScalar = layer_->filledContoursOpacity();
                const int    nBands      = int(levels.size()) - 1;
                p->setPen(Qt::NoPen);
                for (const auto &bp : bands) {
                    if (bp.verts.size() < 3) continue;
                    QColor c = viridisAt(
                        (double(bp.bandIndex) + 0.5) / double(nBands));
                    c.setAlphaF(alphaScalar);
                    p->setBrush(c);
                    // Fan-triangulate the convex polygon by drawing it
                    // directly — QPainter handles convex polys efficiently.
                    p->drawConvexPolygon(bp.verts.data(),
                                         int(bp.verts.size()));
                }
            }
        }

        // --- Pass 3 (optional): iso-line contour strokes. Phase 9 — sublayer
        // gate + sublayer-style-driven iso count + colour + line width.
        if (paintIsolines && maxDepth > dryDepth) {
            using namespace OpenSWMM::Contour;
            const auto *isoStyle =
                (isolineSub && isolineSub->isolineStyle())
                ? isolineSub->isolineStyle() : nullptr;
            const int isoCount = isoStyle
                ? isoStyle->isoValueCount()
                : layer_->isolinesLevels();
            const auto levels = evenlySpacedLevels(
                dryDepth, maxDepth, isoCount);
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
                    QPen linePen(isoColor);
                    linePen.setCosmetic(true);   // constant pixel width across zoom
                    linePen.setWidthF(isoWidthPx);
                    if (isoStyle) linePen.setStyle(isoStyle->dashPattern());
                    p->setPen(linePen);
                    p->setBrush(Qt::NoBrush);
                    for (const auto &s : segs)
                        p->drawLine(s.a, s.b);

                    // Optional per-level labels (IsolineStyle::labels).
                    // QPainter-side mirror of the QSG mesh-renderer label
                    // pass: one label per level at the centroid of all its
                    // segment midpoints. The label text is drawn upright
                    // (no rotation) so it stays readable when the user
                    // zooms in.
                    if (isoStyle && isoStyle->labels()) {
                        struct Acc { double sx = 0, sy = 0; int n = 0; };
                        QHash<double, Acc> byLevel;
                        for (const auto &s : segs) {
                            auto &a = byLevel[s.level];
                            a.sx += 0.5 * (s.a.x() + s.b.x());
                            a.sy += 0.5 * (s.a.y() + s.b.y());
                            a.n  += 1;
                        }
                        QFont f;
                        f.setPointSizeF(9.0);
                        f.setBold(true);
                        p->save();
                        p->setRenderHint(QPainter::TextAntialiasing, true);
                        const QTransform wt = p->worldTransform();
                        const double scaleT = std::max(std::abs(wt.m11()), 1e-9);
                        // Render label in screen space so the font size is
                        // pixel-constant. Reset to identity for text only.
                        p->setWorldMatrixEnabled(false);
                        p->setFont(f);
                        QFontMetricsF fm(f);
                        for (auto it = byLevel.constBegin(); it != byLevel.constEnd(); ++it) {
                            if (it.value().n <= 0) continue;
                            const QString text = QString::number(it.key(), 'g', 4);
                            const QPointF scenePt(it.value().sx / it.value().n,
                                                  it.value().sy / it.value().n);
                            // Apply the world transform manually so we get
                            // screen-pixel coordinates for the text origin.
                            const QPointF px = wt.map(scenePt);
                            const QRectF br = fm.boundingRect(text);
                            const QPointF origin(px.x() - br.width() * 0.5,
                                                  px.y() + br.height() * 0.25);
                            // White halo for legibility on both light and
                            // dark underlays.
                            p->setPen(QPen(QColor(255, 255, 255, 230), 3.0,
                                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                            QPainterPath path;
                            path.addText(origin, f, text);
                            p->drawPath(path);
                            p->setPen(Qt::NoPen);
                            p->setBrush(isoColor);
                            p->drawPath(path);
                        }
                        p->setWorldMatrixEnabled(true);
                        Q_UNUSED(scaleT);
                        p->restore();
                    }
                }
            }
        }

        // --- Pass 4 (optional): mesh wireframe overlay from MeshEdgeSublayer.
        // Draws every triangle edge in the user-configured style. We don't
        // dedupe — adjacent triangles share an edge so it gets drawn twice
        // but at <=2 px width that's invisible. The slope-driven thin/wide
        // branch is not honoured here because the 2D results layer has no
        // per-edge slope cache; the uniform lineWidthPx is used.
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
            for (const auto &t : tris) {
                const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
                const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
                const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
                const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
                if (!exposed.isNull() &&
                    (maxX < exposed.left()  || minX > exposed.right() ||
                     maxY < exposed.top()   || minY > exposed.bottom())) continue;
                p->drawLine(t.a, t.b);
                p->drawLine(t.b, t.c);
                p->drawLine(t.c, t.a);
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
                    p->save();
                    p->setRenderHint(QPainter::TextAntialiasing, true);
                    p->setWorldMatrixEnabled(false);   // screen space
                    for (auto it = bucket.constBegin(); it != bucket.constEnd(); ++it) {
                        const auto &t = tris[it.value()];
                        const QString text = QString::number(t.depth, 'f', 2);
                        const QPointF px = wt.map(t.centroid);
                        QPainterPath path;
                        path.addText(px, f, text);
                        if (lc.haloEnabled) {
                            p->setPen(QPen(lc.haloColor,
                                           std::max(1.0, lc.haloRadiusPx) * 2.0,
                                           Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                            p->setBrush(Qt::NoBrush);
                            p->drawPath(path);
                        }
                        p->setPen(Qt::NoPen);
                        p->setBrush(lc.color);
                        p->drawPath(path);
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
        // Phase 9 (2026-05-25) — sublayer.isVisible() is the authoritative
        // gate (MVC). Falls back to the legacy velocityVectorsVisible()
        // when no sublayer is bound, preserving back-compat with UI code
        // that flips the boolean directly.
        auto *velSub = layer_->velocityVectorSublayer();
        const bool wantArrows = velSub
            ? velSub->isVisible()
            : layer_->velocityVectorsVisible();
        if (!wantArrows) return;
        if (!layer_->hasVelocityData()) return;

        const auto& tris = layer_->m_sceneTris;
        if (tris.isEmpty()) return;

        const QRectF exposed   = option->exposedRect;
        const double dryDepth  = layer_->dryDepth();
        const double maxVel    = std::max(layer_->maxVelocity(), 1e-6);
        const double arrowPx   = layer_->velocityArrowScale();
        const qreal  alpha     = std::clamp<qreal>(layer_->velocityOpacity(), 0.0, 1.0);

        // Convert pixel-space arrow length to scene units so glyphs render
        // at a constant on-screen size regardless of zoom.
        const QTransform xf = p->worldTransform();
        const double scale  = std::max(std::abs(xf.m11()), 1e-9);  // assume uniform scale
        const double pxToScene = 1.0 / scale;

        // Pen width also kept constant in pixels.
        QPen pen;
        pen.setCosmetic(true);
        pen.setWidthF(1.5);

        p->save();
        p->setOpacity(alpha);

        // Skip cells whose magnitude is below 0.01% of the running peak —
        // numerical noise without clipping real flow on slow shallow runs.
        const double vmagSkip = std::max(static_cast<double>(maxVel) * 1e-4,
                                          1e-9);

        for (const auto& t : tris) {
            if (t.depth < dryDepth) continue;
            if (t.vmag < vmagSkip) continue;          // sub-threshold cell

            // Cheap viewport cull around the centroid.
            if (!exposed.isNull() &&
                !exposed.contains(t.centroid)) continue;

            // Log-scaled arrow length: arrowPx * log1p(vmag / vmagRef)
            // where vmagRef = max_vel ⇒ glyph never exceeds arrowPx.
            const double mag_norm  = std::clamp(t.vmag / maxVel, 0.0, 1.0);
            const double len_scene = pxToScene * arrowPx * std::log1p(mag_norm) /
                                      std::log1p(1.0);
            if (len_scene <= 0.0) continue;

            const double inv_vmag = 1.0 / t.vmag;
            const double dx = t.vx * inv_vmag * len_scene;
            const double dy = t.vy * inv_vmag * len_scene;

            int r, g, b;
            velocityColorRgb(t.vmag, maxVel, r, g, b);
            pen.setColor(QColor(r, g, b));
            p->setPen(pen);

            const QPointF tail = t.centroid;
            const QPointF head(tail.x() + dx, tail.y() + dy);
            p->drawLine(tail, head);

            // Chevron arrowhead — two short legs at ±25° from the back-facing direction.
            const double headLen = len_scene * 0.32;
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
        }

        p->restore();

        // ------------------------------------------------------------------
        // Direction-only flow arrows (FlowArrowSublayer).
        //
        // Independent of the velocity-vector pass — these are fixed-length
        // arrows that show flow direction at cell centroids regardless of
        // magnitude. Useful when |v| varies by orders of magnitude and the
        // log-scaled glyphs collapse to invisible at small values.
        //
        // Placement: cell centroids (placeAtCellCenters=true) OR a regular
        // screen-pixel grid (placeAtCellCenters=false, default). Grid mode
        // picks the cell whose centroid is closest to each grid sample so
        // arrows snap onto real flow data without overdraw.
        if (auto *farrSub = layer_->flowArrowSublayer();
            farrSub && farrSub->isVisible() && layer_->hasVelocityData())
        {
            const auto *fs = farrSub->flowArrowStyle();
            if (!fs) return;

            const double dryCut  = std::max(fs->dryDepthCutoff(), layer_->dryDepth());
            const double arrowPx = std::max(2.0, fs->arrowLengthPx());
            const double headPx  = std::max(1.0, fs->headSizePx());
            const double spacing = std::max(4.0, fs->arrowSpacingPx());
            const QColor col     = fs->color();
            const QColor outCol  = fs->outlineColor();
            const double shaftPx = std::max(0.5, fs->shaftWidthPx());
            // VS.7 — when enabled, each arrow's shaft colour is sampled from
            // the ramp by its speed magnitude (computed per arrow below).
            const bool   colorByMag = fs->colorByMagnitude();
            const qreal  subOp   = std::clamp<qreal>(farrSub->opacity(), 0.0, 1.0);

            const QTransform xf2  = p->worldTransform();
            const double scale2   = std::max(std::abs(xf2.m11()), 1e-9);
            const double pxToScene2 = 1.0 / scale2;
            const double arrowLen   = arrowPx  * pxToScene2;
            const double headLen    = headPx   * pxToScene2;
            const double gridStep   = spacing  * pxToScene2;

            p->save();
            p->setOpacity(subOp);

            // Light outline beneath the arrow so it reads on both bright
            // and dark fills.
            QPen outlinePen(outCol);
            outlinePen.setCosmetic(true);
            outlinePen.setWidthF(shaftPx + 1.6);
            outlinePen.setCapStyle(Qt::RoundCap);

            QPen pen(col);
            pen.setCosmetic(true);
            pen.setWidthF(shaftPx);
            pen.setCapStyle(Qt::RoundCap);

            auto drawArrow = [&](const QPointF &center,
                                  double dx, double dy) {
                const double mag = std::sqrt(dx*dx + dy*dy);
                if (mag <= 0.0) return;
                // VS.7 — per-arrow shaft colour by magnitude (m/s).
                QPen shaftPen = pen;
                if (colorByMag)
                    shaftPen.setColor(fs->colorForSpeed(mag));
                const double ux = dx / mag, uy = dy / mag;
                const QPointF tail(center.x() - 0.5 * arrowLen * ux,
                                   center.y() - 0.5 * arrowLen * uy);
                const QPointF head(center.x() + 0.5 * arrowLen * ux,
                                   center.y() + 0.5 * arrowLen * uy);
                const double cosA = std::cos(0.43);
                const double sinA = std::sin(0.43);
                const QPointF left ( head.x() - headLen * ( ux * cosA - uy * sinA),
                                      head.y() - headLen * ( ux * sinA + uy * cosA) );
                const QPointF right( head.x() - headLen * ( ux * cosA + uy * sinA),
                                      head.y() - headLen * (-ux * sinA + uy * cosA) );
                if (outlinePen.widthF() > pen.widthF()) {
                    p->setPen(outlinePen);
                    p->drawLine(tail, head);
                    p->drawLine(head, left);
                    p->drawLine(head, right);
                }
                p->setPen(shaftPen);
                p->drawLine(tail, head);
                p->drawLine(head, left);
                p->drawLine(head, right);
            };

            if (fs->placeAtCellCenters()) {
                for (const auto &t : tris) {
                    if (t.depth < dryCut) continue;
                    if (t.vmag <= 0.0)    continue;
                    if (!exposed.isNull() && !exposed.contains(t.centroid)) continue;
                    drawArrow(t.centroid, double(t.vx), double(t.vy));
                }
            } else {
                // Regular grid over the exposed rect (or full bbox when not
                // given). For each grid cell, pick the closest tri centroid
                // and use its velocity.
                const QRectF area = exposed.isNull() ? layer_->m_sceneBBox : exposed;
                if (gridStep > 0.0 && area.isValid() && !tris.isEmpty()) {
                    const int   nCols = std::max(1, int(area.width()  / gridStep));
                    const int   nRows = std::max(1, int(area.height() / gridStep));
                    const double cellW = area.width()  / double(nCols);
                    const double cellH = area.height() / double(nRows);
                    // Cell-bucket index: each tri centroid drops into one
                    // cell — first wet tri per cell wins. O(N tris) once
                    // per paint.
                    QHash<qint64, int> bucket;
                    bucket.reserve(nCols * nRows);
                    for (int i = 0; i < tris.size(); ++i) {
                        const auto &t = tris[i];
                        if (t.depth < dryCut) continue;
                        if (t.vmag <= 0.0)    continue;
                        const double rx = t.centroid.x() - area.left();
                        const double ry = t.centroid.y() - area.top();
                        if (rx < 0 || ry < 0 || rx > area.width() || ry > area.height())
                            continue;
                        const int cx = std::min(nCols - 1, int(rx / cellW));
                        const int cy = std::min(nRows - 1, int(ry / cellH));
                        const qint64 key = qint64(cy) * nCols + cx;
                        if (!bucket.contains(key)) bucket.insert(key, i);
                    }
                    for (auto it = bucket.constBegin(); it != bucket.constEnd(); ++it) {
                        const auto &t = tris[it.value()];
                        drawArrow(t.centroid, double(t.vx), double(t.vy));
                    }
                }
            }
            p->restore();
        }
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

QDateTime HDF5Mesh2DSource::simTimeAt(int timeIdx) const
{
    if (!sim_start_.isValid() || !reader_) return {};
    // /time is in days since simulation start per Default2DOutputPlugin
    // ("units = days since simulation start"). Re-read each call instead of
    // caching so live-tail growth is reflected; the call is O(1) once HDF5
    // has parsed the file metadata.
    std::vector<double> times;
    if (!reader_->readTimes(times)) return {};
    if (timeIdx < 0 || timeIdx >= static_cast<int>(times.size())) return {};
    return sim_start_.addMSecs(qint64(times[timeIdx] * 86400.0 * 1000.0));
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
    m_depthRampSublayer = new OpenSWMM::Render::DepthColorRampSublayer(
        QStringLiteral("results2d.depthRamp"), this);
    m_contourBandSublayer = new OpenSWMM::Render::ContourBandSublayer(
        QStringLiteral("results2d.bands"), this);
    m_isolineSublayer = new OpenSWMM::Render::IsolineSublayer(
        QStringLiteral("results2d.isolines"), this);
    m_velocityVectorSublayer = new OpenSWMM::Render::VelocityVectorSublayer(
        QStringLiteral("results2d.velocity"), this);
    m_flowArrowSublayer = new OpenSWMM::Render::FlowArrowSublayer(
        QStringLiteral("results2d.flowArrows"), this);

    // Phase 9 (2026-05-25) — sublayer.invalidated() routes to the existing
    // graphics-item update path. This is what makes the layer-tree
    // checkbox + sublayer style edits actually show/hide / re-render the
    // 2D overlays live. Heatmap / bands / isolines all share the same
    // graphics_item_; velocity has its own arrows_item_ so route those
    // separately. Lambda captures `this` so we always observe the
    // current item pointers (they're recreated on every populateScene).
    auto wireMeshRepaint = [this](OpenSWMM::Render::ISublayer *s) {
        if (!s) return;
        QObject::connect(s, &OpenSWMM::Render::ISublayer::invalidated,
                         this, [this]() {
                             if (graphics_item_) graphics_item_->geometryChanged();
                         });
    };
    auto wireArrowRepaint = [this](OpenSWMM::Render::ISublayer *s) {
        if (!s) return;
        QObject::connect(s, &OpenSWMM::Render::ISublayer::invalidated,
                         this, [this]() {
                             if (arrows_item_) arrows_item_->geometryChanged();
                         });
    };
    wireMeshRepaint(m_meshFillSublayer);
    wireMeshRepaint(m_meshEdgeSublayer);
    wireMeshRepaint(m_meshNodeSublayer);
    wireMeshRepaint(m_depthRampSublayer);
    wireMeshRepaint(m_contourBandSublayer);
    wireMeshRepaint(m_isolineSublayer);
    wireArrowRepaint(m_velocityVectorSublayer);
    wireArrowRepaint(m_flowArrowSublayer);
}

SWMM2DResultsLayer::~SWMM2DResultsLayer() = default;

QList<OpenSWMM::Render::ISublayer *> SWMM2DResultsLayer::sublayers() const
{
    // Paint order = list order (bottom-up):
    //   mesh fill (static)      → terrain hillshade base
    //   depth ramp (dynamic)    → graduated depth/WSE/vmag fill
    //   contour bands (dynamic) → marching-squares filled bands
    //   mesh edges (static)     → wireframe over results
    //   isolines (dynamic)      → contour lines + labels
    //   mesh vertices (static)  → coupled-vertex markers
    //   velocity vectors        → magnitude-scaled glyphs
    //   flow arrows (top)       → direction-only arrow field
    // Slice GUI-2026-05-30 §2 — order is user-customisable and cached in
    // m_sublayerOrder; seeded once from the defaults above.
    if (m_sublayerOrder.isEmpty()) {
        OpenSWMM::Render::ISublayer *defaults[] = {
            m_meshFillSublayer,
            m_depthRampSublayer,
            m_contourBandSublayer,
            m_meshEdgeSublayer,
            m_isolineSublayer,
            m_meshNodeSublayer,
            m_velocityVectorSublayer,
            m_flowArrowSublayer,
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

void SWMM2DResultsLayer::setSource(std::unique_ptr<IMesh2DSource> source)
{
    source_ = std::move(source);
    current_time_idx_ = -1;
    current_depths_.clear();
    current_flux_.clear();
    have_velocity_ = false;
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

    // Show the latest frame immediately if any are available.
    if (n > 0) setCurrentTimeIndex(n - 1);
    else {
        if (graphics_item_) graphics_item_->geometryChanged();
        if (arrows_item_)   arrows_item_->geometryChanged();
    }
}

void SWMM2DResultsLayer::setCurrentTimeIndex(int t)
{
    if (!source_) return;
    const int n = source_->timeCount();
    if (n == 0) return;
    t = std::clamp(t, 0, n - 1);
    if (t == current_time_idx_ && !current_depths_.empty()) return;

    current_time_idx_ = t;
    source_->readDepthsAt(t, current_depths_);
    // Edge flux is optional — sources without it return false and leave
    // current_flux_ untouched. applyCurrentFlux_ checks the size and bails.
    if (!source_->readEdgeFluxAt(t, current_flux_)) {
        current_flux_.clear();
    }

    // Auto-track running max depth (unless the user explicitly pinned it)
    if (!max_depth_user_set_ && !current_depths_.empty()) {
        const float peak = *std::max_element(current_depths_.begin(),
                                              current_depths_.end());
        if (peak > max_depth_) max_depth_ = peak;
    }

    applyCurrentDepths_();
    applyCurrentFlux_();
    if (graphics_item_) graphics_item_->geometryChanged();
    if (arrows_item_)   arrows_item_->geometryChanged();
    emit currentTimeChanged(t);
    emit currentDateTimeChanged(source_->simTimeAt(t));
}

void SWMM2DResultsLayer::refreshTimeRange()
{
    if (!source_) return;
    const int n = source_->timeCount();
    emit timeRangeChanged(0, std::max(0, n - 1));
    if (n > 0 && current_time_idx_ < n - 1) {
        setCurrentTimeIndex(n - 1);
    }
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
        t.vx = t.vy = t.vmag = 0.0f;
    }
    have_velocity_ = false;
    if (graphics_item_) graphics_item_->geometryChanged();
    if (arrows_item_)   arrows_item_->geometryChanged();
    emit timeRangeChanged(0, 0);
    emit currentTimeChanged(-1);
    emit currentDateTimeChanged(QDateTime());
}

void SWMM2DResultsLayer::setDryDepth(double d)
{
    if (d == dry_depth_) return;
    dry_depth_ = d;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setMaxDepth(double d)
{
    if (d == max_depth_) return;
    max_depth_ = d;
    max_depth_user_set_ = true;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setVelocityVectorsVisible(bool v)
{
    if (v == velocity_visible_) return;
    velocity_visible_ = v;
    if (arrows_item_) arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::setVelocityOpacity(qreal alpha)
{
    alpha = std::clamp<qreal>(alpha, 0.0, 1.0);
    if (alpha == velocity_opacity_) return;
    velocity_opacity_ = alpha;
    if (arrows_item_) arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::setVelocityArrowScale(double scale)
{
    if (scale <= 0.0) return;
    if (scale == velocity_arrow_scale_) return;
    velocity_arrow_scale_ = scale;
    if (arrows_item_) arrows_item_->geometryChanged();
}

void SWMM2DResultsLayer::setMaxVelocity(double v)
{
    if (v <= 0.0) return;
    if (v == max_velocity_) return;
    max_velocity_ = v;
    max_velocity_user_set_ = true;
    if (arrows_item_) arrows_item_->geometryChanged();
}

// ---------------------------------------------------------------------------
// Color-ramp + contour styling (Slice CF.MVP-fix.3)
// ---------------------------------------------------------------------------

void SWMM2DResultsLayer::setColorRampStyle(ColorRampStyle s)
{
    if (s == color_ramp_style_) return;
    color_ramp_style_ = s;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setColorClasses(int n)
{
    n = std::clamp(n, 2, 64);
    if (n == color_classes_) return;
    color_classes_ = n;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setFilledContours(bool on)
{
    if (on == filled_contours_) return;
    filled_contours_ = on;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setFilledContoursOpacity(double a)
{
    a = std::clamp(a, 0.0, 1.0);
    if (a == filled_contours_opacity_) return;
    filled_contours_opacity_ = a;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setFilledContoursLevels(int n)
{
    n = std::clamp(n, 2, 32);
    if (n == filled_contours_levels_) return;
    filled_contours_levels_ = n;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setIsolines(bool on)
{
    if (on == isolines_) return;
    isolines_ = on;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setIsolinesLevels(int n)
{
    n = std::clamp(n, 1, 32);
    if (n == isolines_levels_) return;
    isolines_levels_ = n;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setIsolinesColor(QColor c)
{
    if (c == isolines_color_) return;
    isolines_color_ = c;
    if (graphics_item_) graphics_item_->geometryChanged();
}

void SWMM2DResultsLayer::setIsolinesWidth(double px)
{
    px = std::clamp(px, 0.25, 10.0);
    if (px == isolines_width_) return;
    isolines_width_ = px;
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

    // Linear scan for nearest time — adequate for the demo case's ~60-240
    // frame count. Replace with binary search if frame counts ever grow
    // large enough to matter on a presentation laptop.
    int best   = 0;
    qint64 bestDelta = std::numeric_limits<qint64>::max();
    for (int i = 0; i < n; ++i) {
        const QDateTime ti = source_->simTimeAt(i);
        if (!ti.isValid()) continue;
        const qint64 d = std::abs(t.msecsTo(ti));
        if (d < bestDelta) { bestDelta = d; best = i; }
    }
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
    // Linear scan over m_sceneTris. Stops at first hit.
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
    m_sceneBBox = QRectF();

    if (!source_) return;

    if (!source_->readMeshGeometry(vx_, vy_, vz_, tris_)) {
        return;
    }
    if (tris_.empty()) return;

    // Scene-space points: identity transform + Y-flip (scene grows downward,
    // matching the mesh layer's convention). CRS transforms come later via
    // onCanvasCRSChanged when the layer SRS framework is wired.
    const int nVerts = static_cast<int>(vx_.size());
    QVector<QPointF> scenePts;
    scenePts.reserve(nVerts);
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    for (int i = 0; i < nVerts; ++i) {
        const double sx =  vx_[i];
        const double sy = -vy_[i];
        scenePts.append(QPointF(sx, sy));
        if (sx < minX) minX = sx;
        if (sx > maxX) maxX = sx;
        if (sy < minY) minY = sy;
        if (sy > maxY) maxY = sy;
    }
    if (nVerts > 0) {
        m_sceneBBox = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
        setExtent(MapExtent(minX, -maxY, maxX, -minY));  // un-flip Y for layer extent
    }

    m_sceneTris.resize(static_cast<int>(tris_.size()));
    for (int i = 0; i < static_cast<int>(tris_.size()); ++i) {
        SceneTri& st = m_sceneTris[i];
        st.a = scenePts[tris_[i][0]];
        st.b = scenePts[tris_[i][1]];
        st.c = scenePts[tris_[i][2]];
        st.centroid = QPointF((st.a.x() + st.b.x() + st.c.x()) / 3.0,
                               (st.a.y() + st.b.y() + st.c.y()) / 3.0);
        st.depth = 0.0f;
        st.vx = st.vy = st.vmag = 0.0f;
    }

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

    // Compute per-vertex depths by averaging incident-cell depths so the
    // marching-triangles contour passes see a continuous scalar field. The
    // engine reports depth per-cell (RT0), but contour extraction needs
    // per-vertex values — a uniform cell field produces zero crossings and
    // the algorithm skips every triangle as "degenerate".
    const int nVert = static_cast<int>(vx_.size());
    std::vector<float> vsum(size_t(nVert), 0.0f);
    std::vector<int>   vcount(size_t(nVert), 0);
    for (int i = 0; i < nTri; ++i) {
        const float d = current_depths_[i];
        const auto& tri = tris_[i];
        for (int k = 0; k < 3; ++k) {
            const int vi = tri[k];
            if (vi < 0 || vi >= nVert) continue;
            vsum[vi] += d;
            ++vcount[vi];
        }
    }
    for (int v = 0; v < nVert; ++v) {
        if (vcount[v] > 0) vsum[v] /= float(vcount[v]);
    }
    for (int i = 0; i < nTri; ++i) {
        const auto& tri = tris_[i];
        SceneTri& st = m_sceneTris[i];
        st.dv0 = (tri[0] >= 0 && tri[0] < nVert) ? vsum[tri[0]] : 0.0f;
        st.dv1 = (tri[1] >= 0 && tri[1] < nVert) ? vsum[tri[1]] : 0.0f;
        st.dv2 = (tri[2] >= 0 && tri[2] < nVert) ? vsum[tri[2]] : 0.0f;
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
            if (q >  kQMax) q =  kQMax;
            if (q < -kQMax) q = -kQMax;
            a00 += nx * nx;
            a01 += nx * ny;
            a11 += ny * ny;
            b0  += nx * q;
            b1  += ny * q;
        }
        const double det = a00 * a11 - a01 * a01;
        if (std::abs(det) < 1e-12) {
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

    // Bin count comes from the styling tab; Smooth mode still gets a fixed
    // 5 bin preview in the legend (matches what users see in the dialog).
    const int bins = (color_ramp_style_ == ColorRampStyle::Graduated)
                       ? std::clamp(color_classes_, 2, 32)
                       : 5;

    // Section header for the depth ramp.
    {
        LegendSymbolItem header;
        header.label      = tr("Depth (m)");
        header.sublayerId = m_depthRampSublayer ? m_depthRampSublayer->id()
                                                : QString();
        out.append(header);
    }

    for (int i = 0; i < bins; ++i) {
        const double t  = (i + 0.5) / bins;
        const double v  = dry + t * (mx - dry);
        const double lo = dry + double(i)     / bins * (mx - dry);
        const double hi = dry + double(i + 1) / bins * (mx - dry);

        int r = 0, g = 0, b = 0, a = 0;
        inundationColorRgba(v, dry, mx, r, g, b, a);
        const QColor c(r, g, b, std::max(a, 200));

        LegendSymbolItem item;
        item.label      = QStringLiteral("%1 – %2")
                            .arg(lo, 0, 'g', 3).arg(hi, 0, 'g', 3);
        item.sublayerId = m_depthRampSublayer ? m_depthRampSublayer->id()
                                              : QString();
        item.range      = { lo, hi };
        item.classKey   = QString::number(i);

        SymbolLayer sl;
        sl.kind = SymbolLayerKind::SimpleFill;
        sl.props.insert(QStringLiteral("color"), c.name(QColor::HexArgb));
        item.symbol.layers.append(sl);
        out.append(item);
    }

    // Filled contour bands (only when enabled) — single header row + N bands.
    if (filled_contours_) {
        LegendSymbolItem header;
        header.label = tr("Filled bands");
        header.sublayerId = m_contourBandSublayer
                              ? m_contourBandSublayer->id() : QString();
        out.append(header);

        const int n = std::max(2, filled_contours_levels_);
        for (int i = 0; i < n; ++i) {
            const double t = (i + 0.5) / n;
            const double v = dry + t * (mx - dry);
            int r = 0, g = 0, b = 0, a = 0;
            inundationColorRgba(v, dry, mx, r, g, b, a);
            const int alpha = std::clamp(
                int(std::lround(filled_contours_opacity_ * 255.0)), 0, 255);
            QColor c(r, g, b, alpha);

            LegendSymbolItem item;
            const double lo = dry + double(i)     / n * (mx - dry);
            const double hi = dry + double(i + 1) / n * (mx - dry);
            item.label      = QStringLiteral("%1 – %2 m")
                                .arg(lo, 0, 'g', 3).arg(hi, 0, 'g', 3);
            item.sublayerId = m_contourBandSublayer
                                ? m_contourBandSublayer->id() : QString();
            item.range      = { lo, hi };
            SymbolLayer sl;
            sl.kind = SymbolLayerKind::SimpleFill;
            sl.props.insert(QStringLiteral("color"), c.name(QColor::HexArgb));
            item.symbol.layers.append(sl);
            out.append(item);
        }
    }

    // Iso-line strokes (single row — stroke colour + width).
    if (isolines_) {
        LegendSymbolItem item;
        item.label = tr("Iso-depth lines (%1 levels)").arg(isolines_levels_);
        item.sublayerId = m_isolineSublayer ? m_isolineSublayer->id()
                                            : QString();
        SymbolLayer sl;
        sl.kind = SymbolLayerKind::SimpleLine;
        sl.props.insert(QStringLiteral("color"),
                        isolines_color_.name(QColor::HexArgb));
        sl.props.insert(QStringLiteral("width"), isolines_width_);
        item.symbol.layers.append(sl);
        out.append(item);
    }

    // Velocity arrows (single row) — visible only when feed has data.
    if (velocity_visible_ && have_velocity_) {
        LegendSymbolItem item;
        item.label = tr("Velocity (max %1 m/s)").arg(max_velocity_, 0, 'g', 3);
        item.sublayerId = m_velocityVectorSublayer
                            ? m_velocityVectorSublayer->id() : QString();
        SymbolLayer sl;
        sl.kind = SymbolLayerKind::SimpleMarker;
        sl.props.insert(QStringLiteral("shape"), QStringLiteral("arrow"));
        int r = 0, g = 0, b = 0;
        velocityColorRgb(max_velocity_, max_velocity_, r, g, b);
        sl.props.insert(QStringLiteral("color"),
                        QColor(r, g, b, 230).name(QColor::HexArgb));
        sl.props.insert(QStringLiteral("size"), 12.0);
        item.symbol.layers.append(sl);
        out.append(item);
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

    // ── Depth color ramp (RasterColorRampSpec) ──────────────────────────
    // Slice AN.4 — extended to write all fields exposed by
    // RasterColorRampSymbolStyleAdapter (attribute / belowMin / aboveMax /
    // useLogScale) directly from the SymbolLayer props bag, falling back
    // to the spec values where they overlap.
    QObject::connect(ruleHandles[0], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[0]);
            if (!layer || layer->kind != SymbolLayerKind::RasterColorRamp) return;
            const auto spec = RasterColorRampSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_depthRampSublayer) {
                sub->setOpacity(spec.opacity);
                if (auto *st = sub->rampStyle()) {
                    const auto &p = layer->props;
                    const QColor specLow = !spec.ramp.stops.isEmpty()
                        ? spec.ramp.stops.first().second : QColor(0, 0, 255);
                    const QColor specHigh = !spec.ramp.stops.isEmpty()
                        ? spec.ramp.stops.last().second : QColor(255, 0, 0);
                    st->setMinValue(p.contains(QStringLiteral("minValue"))
                        ? p.value(QStringLiteral("minValue")).toDouble()
                        : spec.clampMin);
                    st->setMaxValue(p.contains(QStringLiteral("maxValue"))
                        ? p.value(QStringLiteral("maxValue")).toDouble()
                        : spec.clampMax);
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
                    if (p.contains(QStringLiteral("useLogScale")))
                        st->setUseLogScale(p.value(QStringLiteral("useLogScale")).toBool());
                    if (p.contains(QStringLiteral("attribute")))
                        st->setAttribute(p.value(QStringLiteral("attribute")).toString());
                }
            }
        });

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
    add(m_meshFillSublayer,        sect);
    add(m_meshEdgeSublayer,        sect);
    add(m_meshNodeSublayer,        sect);
    add(m_depthRampSublayer,       sect);
    add(m_contourBandSublayer,     sect);
    add(m_isolineSublayer,         sect);
    add(m_velocityVectorSublayer,  sect);
    add(m_flowArrowSublayer,       sect);

    return out;
}
