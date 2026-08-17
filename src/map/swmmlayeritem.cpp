/*!
 * \file   swmmlayeritem.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/swmmlayeritem.h"

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
// Labeling overhaul — shared screen-space label painter + scale gating.
#include "map/mapcanvas.h"
#include "render/labelpainter.h"
// Slice Z.14-paint — polygon clip mask.
#include "render/maskclipresolver.h"
// Slice Z.5b-paint — perpendicular polyline offset.
#include "render/linesymbollayer.h"
#include "render/markershape.h"
#include "render/polylineoffset.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/renderperf.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>

#include <ogr_spatialref.h>   // OGRCoordinateTransformation

#include <algorithm>
#include <array>
#include <QMap>
#include <cmath>

namespace {

/*! Scene-space Y-flip helper — matches the legacy toScene(mx, my) in
 *  swmmmodellayer.cpp (scene Y grows downward, map Y grows upward). */
inline QPointF toScene(double mx, double my) { return QPointF(mx, -my); }

/*! Slice Z.5b-paint — extract the line-symbol-layer offsetPx from a
 *  kind's renderer. Returns 0 for renderer classes / symbol-layer
 *  kinds that don't carry a per-line offset (Graduated/Categorized,
 *  marker / fill layers, empty styles), preserving the legacy "no
 *  shift" paint behaviour.
 *
 *  Graduated / Categorized per-class offsets need a per-feature
 *  lookup that this paint loop doesn't have today — the named
 *  Z.5b-paint-graduated follow-up wires that in. */
inline qreal lineOffsetForKindRenderer(
    const OpenSWMM::Render::IFeatureRenderer *r)
{
    using namespace OpenSWMM::Render;
    const auto *single = dynamic_cast<const SingleSymbolRenderer *>(r);
    if (!single) return 0.0;
    const SymbolStyle &style = single->symbol();
    if (style.layers.isEmpty()) return 0.0;
    const SymbolLayer &layer = style.layers.first();
    if (layer.kind != SymbolLayerKind::SimpleLine
        && layer.kind != SymbolLayerKind::MarkerLine) return 0.0;
    return LineSymbolLayerSpec::fromSymbolLayer(layer).offsetPx;
}

/*! Marker radius is a pen/brush-sized dot painted in scene units at a
 *  cosmetic pixel-size. Kept small so nodes stay visually compact at
 *  typical zoom; symbology->size is the designer-set marker diameter. */
inline double markerRadius(const SWMMElementSymbol &s) { return s.size * 0.5; }

/*! Shape-driven node glyph drawer. Caller has already pushed the brush
 *  and pen onto \p p. Hot shapes (the four legacy SWMM kinds) are
 *  inlined to avoid the brush/pen push that drawMarkerShape() does
 *  internally; the remaining canonical shapes route through
 *  drawMarkerShape() so we cover all 13 entries in the MarkerShape
 *  enum with one code path. */
void drawNodeGlyph(QPainter *p, const QPointF &c, double r,
                   OpenSWMM::Render::MarkerShape shape)
{
    using Shape = OpenSWMM::Render::MarkerShape;
    switch (shape) {
    case Shape::Circle:
        p->drawEllipse(c, r, r);
        return;
    case Shape::Square:
        p->drawRect(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r));
        return;
    case Shape::Diamond: {
        QPolygonF dia;
        dia << QPointF(c.x(),     c.y() - r)
            << QPointF(c.x() + r, c.y())
            << QPointF(c.x(),     c.y() + r)
            << QPointF(c.x() - r, c.y());
        p->drawPolygon(dia);
        return;
    }
    case Shape::EquilateralTriangle: {
        // Up-pointing isoceles — same silhouette as the legacy outfall
        // glyph; kept inline so the common kind paths don't churn
        // through drawMarkerShape's save/restore.
        QPolygonF tri;
        tri << QPointF(c.x(),         c.y() - r)
            << QPointF(c.x() - r,     c.y() + r * 0.8)
            << QPointF(c.x() + r,     c.y() + r * 0.8);
        p->drawPolygon(tri);
        return;
    }
    default:
        break;
    }
    // Cold path — Star / Cross / Plus / XCross / Pentagon / Hexagon /
    // Arrow / HalfCircle / right-pointing Triangle. Use the canonical
    // helper which understands all 13 shapes. drawMarkerShape pushes
    // the caller's brush + pen via painter->save/restore; that's a
    // small extra cost we only pay when the user picks one of these
    // shapes explicitly.
    OpenSWMM::Render::drawMarkerShape(p, shape, c, 2 * r,
                                       p->brush(), p->pen());
}

// Slice BI Phase 8.13.8-mini (2026-05-24) — flow-direction arrow head.
// Drawn at the midpoint of the link's visible polyline, rotated to the
// polyline tangent there, in scene-space units (the caller scales the
// pixel size into scene units via invViewScale).
void drawFlowArrow(QPainter *p, const QPointF &c, double angleRad,
                   double lenScene, const QColor &fill)
{
    // Equilateral arrowhead pointing along +x in local frame, then
    // rotated/translated. Triangle vertices (tip ahead, two tail corners).
    const double w = lenScene * 0.6;     // arrow half-width
    const double cs = std::cos(angleRad);
    const double sn = std::sin(angleRad);

    auto xform = [&](double lx, double ly) {
        return QPointF(c.x() + lx * cs - ly * sn,
                       c.y() + lx * sn + ly * cs);
    };
    QPolygonF tri;
    tri << xform(lenScene * 0.5,  0.0)
        << xform(-lenScene * 0.5,  w)
        << xform(-lenScene * 0.5, -w);

    p->setBrush(QBrush(fill));
    p->setPen(QPen(fill, 1.0));
    p->drawPolygon(tri);
}

// Walk a polyline (interleaved xy doubles, count vertices) and return
// the midpoint plus the local tangent angle. Returns true on success;
// false when the polyline has < 2 vertices or is degenerate.
bool polylineMidpoint(const double *xy, uint32_t count,
                      QPointF *midOut, double *angleOut)
{
    if (count < 2 || !xy) return false;

    // Total length pass.
    double total = 0.0;
    for (uint32_t i = 1; i < count; ++i) {
        const double dx = xy[i * 2]     - xy[(i - 1) * 2];
        const double dy = xy[i * 2 + 1] - xy[(i - 1) * 2 + 1];
        total += std::hypot(dx, dy);
    }
    if (total <= 0.0) return false;

    // Find the segment that straddles the half-length mark.
    const double half = total * 0.5;
    double acc = 0.0;
    for (uint32_t i = 1; i < count; ++i) {
        const double x0 = xy[(i - 1) * 2];
        const double y0 = xy[(i - 1) * 2 + 1];
        const double x1 = xy[i * 2];
        const double y1 = xy[i * 2 + 1];
        const double segLen = std::hypot(x1 - x0, y1 - y0);
        if (acc + segLen >= half) {
            const double t = (segLen > 0.0) ? (half - acc) / segLen : 0.0;
            *midOut   = QPointF(x0 + t * (x1 - x0), y0 + t * (y1 - y0));
            *angleOut = std::atan2(y1 - y0, x1 - x0);
            return true;
        }
        acc += segLen;
    }
    // Fallback (shouldn't reach here when total > 0).
    *midOut   = QPointF((xy[0] + xy[(count - 1) * 2]) * 0.5,
                        (xy[1] + xy[(count - 1) * 2 + 1]) * 0.5);
    *angleOut = std::atan2(xy[(count - 1) * 2 + 1] - xy[1],
                           xy[(count - 1) * 2]     - xy[0]);
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SWMMLayerItem::SWMMLayerItem(SWMMModelLayer *layer, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_layer(layer)
{
    // Selection / focus / hover are handled by the layer itself, not the
    // scene's BSP. This item is logically one big canvas-overlay.
    setFlag(QGraphicsItem::ItemIsSelectable,    false);
    setFlag(QGraphicsItem::ItemIsMovable,       false);
    // exposedRect is set when paint() is called — we rely on it for
    // viewport culling so we never iterate the full SoA on a partial
    // redraw.
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    // NoCache: layer contents are dynamic (edits, result animation,
    // selection); DeviceCoordinateCache would re-tile on every pan.
    setCacheMode(QGraphicsItem::NoCache);
    refreshBoundingRect();
}

// ---------------------------------------------------------------------------
// Bounding-rect cache
// ---------------------------------------------------------------------------

void SWMMLayerItem::refreshBoundingRect()
{
    prepareGeometryChange();
    if (!m_layer) { m_boundingRect = QRectF(); return; }

    const auto applyTransform = [this](double &x, double &y) {
        if (m_layer->m_transform)
            m_layer->m_transform->Transform(1, &x, &y);
    };

    QRectF r;
    bool first = true;
    auto extend = [&](const QPointF &p) {
        if (first) { r = QRectF(p, QSizeF(0, 0)); first = false; }
        else {
            if (p.x() < r.left())   r.setLeft(p.x());
            if (p.x() > r.right())  r.setRight(p.x());
            if (p.y() < r.top())    r.setTop(p.y());
            if (p.y() > r.bottom()) r.setBottom(p.y());
        }
    };

    for (const auto &n : m_layer->m_nodes) {
        double x = n.x, y = n.y; applyTransform(x, y);
        extend(toScene(x, y));
    }
    for (const auto &g : m_layer->m_gages) {
        double x = g.x, y = g.y; applyTransform(x, y);
        extend(toScene(x, y));
    }
    for (const auto &l : m_layer->m_links) {
        for (QPointF v : l.vertices) {
            double x = v.x(), y = v.y(); applyTransform(x, y);
            extend(toScene(x, y));
        }
    }
    for (const auto &c : m_layer->m_catchments) {
        for (QPointF v : c.vertices) {
            double x = v.x(), y = v.y(); applyTransform(x, y);
            extend(toScene(x, y));
        }
    }

    if (first) { m_boundingRect = QRectF(); return; }
    // Pad by a conservative marker size so boundingRect() encloses the
    // dot halos even when a node sits exactly on the extent corner.
    const double pad = 16.0;
    m_boundingRect = r.adjusted(-pad, -pad, pad, pad);
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void SWMMLayerItem::paint(QPainter *painter,
                          const QStyleOptionGraphicsItem *option,
                          QWidget *widget)
{
    if (!m_layer || !m_layer->isVisible()) return;

    QElapsedTimer t_total; t_total.start();
    painter->setOpacity(m_layer->opacity());

    const QRectF exposed = option->exposedRect;

    // Slice Z.14-paint — install the layer's polygon clip mask. Disabled
    // / unresolvable masks return ok=false so paint continues unclipped.
    // The clip is wrapped in painter->save()/restore() so it tears down
    // even on exceptional exits from the paint loop below.
    painter->save();
    {
        const auto clip = OpenSWMM::Render::resolveMaskClip(
            m_layer, m_layer->maskSpec());
        if (clip.ok && !clip.path.isEmpty()) {
            if (clip.mode == OpenSWMM::Render::MaskMode::ClipInside) {
                painter->setClipPath(clip.path, Qt::IntersectClip);
            } else {
                QPainterPath all;
                all.addRect(exposed.isNull()
                                ? boundingRect()
                                : exposed);
                painter->setClipPath(all.subtracted(clip.path),
                                      Qt::IntersectClip);
            }
        }
    }
    // Phase A.2: read flag arrays indexed by SoA position; no per-link
    // QString hashing on the paint hot path. Maintained by
    // SWMMModelLayer::rebuildFlagArrays() on selection / visibility
    // mutations. The `selected` count is still used for logging.
    const QStringList   &selected     = m_layer->m_selectedNames;
    const auto &nodeSel   = m_layer->m_nodeSelectedFlag;
    const auto &linkSel   = m_layer->m_linkSelectedFlag;
    const auto &catchSel  = m_layer->m_catchSelectedFlag;
    const auto &gageSel   = m_layer->m_gageSelectedFlag;
    const auto &nodeHid   = m_layer->m_nodeHiddenFlag;
    const auto &linkHid   = m_layer->m_linkHiddenFlag;
    const auto &catchHid  = m_layer->m_catchHiddenFlag;
    const auto &gageHid   = m_layer->m_gageHiddenFlag;
    const qint64 t_setup = t_total.elapsed();

    // View scale recovery. Node / gage glyphs are sized in PIXELS (so the
    // user sees the same dot diameter at every zoom — matches what
    // ItemIgnoresTransformations gave the old per-node items). To produce
    // a pixel-sized glyph while painting in scene coordinates we divide
    // the pixel radius by the view's horizontal scale factor m11(): after
    // the view's transform paints us, the result back-scales into the
    // original pixel diameter. Guarded against zero to keep paint safe
    // during the first paint before the view has set up its transform.
    const qreal m11         = painter->transform().m11();
    const qreal invViewScale = (m11 > 0.0) ? (1.0 / m11) : 1.0;

    // Scene-space coords are precomputed in SWMMModelLayer::rebuildSceneCoords
    // and refreshed incrementally on edits. paint() reads them directly so
    // there's no per-vertex Transform()/toScene() math on the hot path.

    // §QSG-1: per-kind ownership. A kind is drawn here only if the
    // QSG overlay is NOT claiming it via the layer's qsgRenderKinds
    // scope. The QSG path (mapcanvas paint + SWMMLayerQSGRenderer)
    // uploads empty geometry for kinds outside its scope so exactly
    // one pipeline draws each kind.
    const bool qsgNodes = m_layer->qsgOwnsKind(SWMMModelLayer::QsgNodes);
    const bool qsgLinks = m_layer->qsgOwnsKind(SWMMModelLayer::QsgLinks);
    const bool qsgCatch = m_layer->qsgOwnsKind(SWMMModelLayer::QsgCatch);
    const bool qsgGages = m_layer->qsgOwnsKind(SWMMModelLayer::QsgGages);

    // ---------------------------------------------------------------- Subcatchments
    if (!qsgCatch && m_layer->m_showSubcatchments)
    {
        const auto &sym = m_layer->m_subcatchSym;
        // Per-kind opacity for subcatchments.
        const qreal kindOp = m_layer->categoryOpacity(SWMMModelLayer::CatSubcatchments);
        auto fade = [kindOp](QColor c) {
            if (kindOp < 1.0 && c.isValid()) c.setAlphaF(c.alphaF() * kindOp);
            return c;
        };
        QPen pen(fade(sym.outlineColor), sym.outlineWidth);
        pen.setCosmetic(true);
        pen.setCapStyle (Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);

        painter->setPen(pen);
        painter->setBrush(QBrush(fade(sym.fillColor)));

        // Phase 8.13.6.4 — when subcatchments use a non-Single renderer,
        // fall through to per-feature setBrush so each polygon gets its
        // own bin / category colour. drawPolygon is already per-feature.
        const bool catchOverrides =
            m_layer->kindUsesOverrides(SWMMModelLayer::CatSubcatchments);

        const auto &cps    = m_layer->m_catchScenePts;
        const auto &cboxes = m_layer->m_catchSceneBBoxes;
        for (int i = 0; i < m_layer->m_catchments.size(); ++i)
        {
            if (size_t(i) < catchHid.size() && catchHid[i]) continue;
            if (i >= cps.size() || cps[i].isEmpty()) continue;
            // Viewport cull against the precomputed scene-space bbox.
            if (!exposed.isNull() && i < cboxes.size()
                && !exposed.intersects(cboxes[i]))
                continue;

            const QPolygonF poly(cps[i]);
            const bool sel = size_t(i) < catchSel.size() && catchSel[i];
            if (sel) {
                painter->setBrush(
                    PreferencesManager::instance()->selectionBrush(
                        QStringLiteral("subcatchment")));
            } else if (catchOverrides) {
                const QColor col = m_layer->featureColor(
                    SWMMModelLayer::CatSubcatchments, i);
                painter->setBrush(QBrush(fade(col.isValid() ? col : sym.fillColor)));
            }
            painter->drawPolygon(poly);
            if (sel || catchOverrides) painter->setBrush(QBrush(fade(sym.fillColor)));
        }

        // ── Outlet connector lines: PIA → outlet node / subcatchment ──
        const auto &outletLines = m_layer->m_catchOutletLines;
        if (!outletLines.isEmpty()) {
            QPen dashPen(QColor(110, 110, 110, 200), 1.0);
            dashPen.setCosmetic(true);
            dashPen.setStyle(Qt::DashLine);
            painter->setPen(dashPen);
            painter->setBrush(Qt::NoBrush);
            for (const auto &ol : outletLines) {
                if (size_t(ol.catchIdx) < catchHid.size() && catchHid[ol.catchIdx])
                    continue;
                // Skip selected here — they get a second, bolder pass below
                // so the highlight isn't drawn under the grey base line.
                if (size_t(ol.catchIdx) < catchSel.size() && catchSel[ol.catchIdx])
                    continue;
                if (!exposed.isNull()) {
                    const QRectF lb = QRectF(ol.line.p1(), ol.line.p2()).normalized();
                    if (!exposed.intersects(lb.adjusted(-1, -1, 1, 1)))
                        continue;
                }
                painter->drawLine(ol.line);
            }

            // Highlight pass for selected subcatchments: bolder dashed
            // connector + dashed ring around the receiving outlet so the
            // user can trace where a clicked subcatchment drains to.
            // Ring radius is in pixels (cosmetic in feel) by scaling with
            // invViewScale — matches the node-marker sizing convention.
            const double ringRadius = 12.0 * invViewScale;
            QPen hiPen(QColor(255, 140, 0, 230), 2.0);
            hiPen.setCosmetic(true);
            hiPen.setStyle(Qt::DashLine);
            for (const auto &ol : outletLines) {
                if (size_t(ol.catchIdx) >= catchSel.size() || !catchSel[ol.catchIdx])
                    continue;
                if (size_t(ol.catchIdx) < catchHid.size() && catchHid[ol.catchIdx])
                    continue;
                painter->setPen(hiPen);
                painter->drawLine(ol.line);
                painter->drawEllipse(ol.line.p2(), ringRadius, ringRadius);
            }
        }
    }

    // ---------------------------------------------------------------- Links
    if (!qsgLinks && m_layer->m_showLinks)
    {
        // Per-link-type pen comes from PreferencesManager so colour,
        // width, cap, join and dash are all user-tunable from the
        // Preferences dialog's Rendering page. Outlets (case 4) now
        // pull their own pen instead of falling back to the conduit
        // symbol.
        // Pen resolution mirrors the QSG renderer: start from the kind's
        // preference pen, then let the per-kind SWMMElementSymbol override
        // colour and width. The old prefs-only read meant thickness /
        // colour edits in the style dialog never reached this (CPU) paint
        // path even though the QSG path honoured them.
        auto linkPenForType = [this](int linkType) {
            auto *prefs = PreferencesManager::instance();
            QPen pen;
            SWMMElementSymbol sym;
            bool hasSym = true;
            switch (linkType) {
            case 1:
                pen = prefs->linkPen(QStringLiteral("pump"));
                sym = m_layer->pumpSymbol();    break;
            case 2:
                pen = prefs->linkPen(QStringLiteral("orifice"));
                sym = m_layer->orificeSymbol(); break;
            case 3:
                pen = prefs->linkPen(QStringLiteral("weir"));
                sym = m_layer->weirSymbol();    break;
            case 4:
                pen = prefs->linkPen(QStringLiteral("outlet"));
                hasSym = false;                 break;  // QSG parity
            default:
                pen = prefs->linkPen(QStringLiteral("conduit"));
                sym = m_layer->conduitSymbol(); break;
            }
            if (hasSym) {
                if (sym.fillColor.isValid()) pen.setColor(sym.fillColor);
                if (sym.size > 0.0)          pen.setWidthF(sym.size);
            }
            return pen;
        };

        // Bucket by link type: 0=Conduit, 1=Pump, 2=Orifice, 3=Weir, 4=Outlet.
        std::array<QVector<QLineF>, 5> segsByType;
        std::array<QVector<QLineF>, 5> selSegsByType;

        // Phase 8.13.6.4 — when a link kind's renderer is Graduated /
        // Categorized, group its segments into colour-keyed sub-buckets
        // so drawLines() can still batch per colour (5 bins ⇒ 5 sub-
        // buckets per kind). Selected links don't use override colours
        // (the selection halo wins).
        constexpr std::array<SWMMModelLayer::Category, 5> linkTypeToCategory = {
            SWMMModelLayer::CatConduits,
            SWMMModelLayer::CatPumps,
            SWMMModelLayer::CatOrifices,
            SWMMModelLayer::CatWeirs,
            SWMMModelLayer::CatOutlets,
        };
        std::array<bool, 5> typeUsesOverrides{};
        for (int t = 0; t < 5; ++t)
            typeUsesOverrides[t] = m_layer->kindUsesOverrides(linkTypeToCategory[t]);
        // Keyed by (colour, stroke width) — a Graduated theme can vary
        // BOTH per feature, and bucketing on colour alone silently
        // collapsed every width onto the kind's base pen.
        std::array<QMap<quint64, QVector<QLineF>>, 5> overrideSegsByType;

        // Slice Z.5b-paint — perpendicular polyline offset per link kind.
        // Read once at setup (cheap — at most 5 SymbolLayer scans), then
        // gated per-link in the inner segment-build loop below. Offset
        // values are stored in SCENE units (offset px × invViewScale) so
        // the on-screen shift stays constant in pixels at every zoom.
        //
        // Slice Z.5b-paint-graduated — typeHasPerFeatureOffset[t] is
        // true when the kind's renderer is Graduated/Categorized AND at
        // least one feature in that kind has a non-zero offset (cached
        // in SWMMModelLayer::m_kindFeatureOffsets via
        // rebuildKindFeatureColors). When set, the segment-build loop
        // takes the per-link slow path; when unset, the legacy fast
        // path runs with the type-uniform offset from the SingleSymbol
        // renderer.
        std::array<qreal, 5> offsetByType{};
        std::array<bool, 5>  typeHasPerFeatureOffset{};
        bool anyTypeOffset = false;
        for (int t = 0; t < 5; ++t) {
            const qreal px = lineOffsetForKindRenderer(
                m_layer->kindRenderer(linkTypeToCategory[t]));
            offsetByType[t] = px * invViewScale;
            typeHasPerFeatureOffset[t] =
                m_layer->kindHasAnyOffset(linkTypeToCategory[t]);
            if (px != 0.0 || typeHasPerFeatureOffset[t]) anyTypeOffset = true;
        }

        // Phase A.3: consume the flat link scene-coord buffer. One
        // contiguous std::vector<float> of (x, y) pairs, with per-link
        // (offset, count) parallel arrays. Cache-friendly, and the
        // exact buffer the GL pipeline (Phase B) will hand to a VBO.
        const double   *flat    = m_layer->m_linkSceneFlat.data();
        const uint32_t *offsets = m_layer->m_linkVertexOffset.data();
        const uint32_t *counts  = m_layer->m_linkVertexCount.data();
        const int       nLinks  = m_layer->m_links.size();

        // Spatial-index cull (Phase A.1, docs/RENDERING_5M_PLAN.md). At
        // zoomed-in views the grid returns a small subset of links; at
        // full extent it returns all of them. Either way we skip the
        // per-link bbox-intersect check inside the inner loop.
        QVector<int> visible;
        if (!exposed.isNull() && !m_layer->m_linkGrid.isEmpty())
            visible = m_layer->m_linkGrid.query(exposed);
        const bool useGrid = !visible.isEmpty()
                          || (!exposed.isNull() && !m_layer->m_linkGrid.isEmpty());
        const int total = useGrid ? visible.size() : nLinks;

        for (int k = 0; k < total; ++k)
        {
            const int i = useGrid ? visible[k] : k;
            if (i < 0 || i >= nLinks) continue;
            if (size_t(i) < linkHid.size() && linkHid[i]) continue;
            const uint32_t cnt = counts[i];
            if (cnt < 2) continue;
            const uint32_t off = offsets[i];

            const int type = (m_layer->m_links[i].linkType >= 0
                           && m_layer->m_links[i].linkType < 5)
                           ? m_layer->m_links[i].linkType : 0;
            const bool sel = size_t(i) < linkSel.size() && linkSel[i];
            const double *p = flat + size_t(off) * 2;

            // Slice Z.5b-paint — when the kind has a non-zero offset,
            // build an offset polyline once per link and emit segments
            // from the shifted version. Zero offset (the typical case)
            // skips the offset call entirely and uses the legacy
            // flat-array fast path.
            //
            // Slice Z.5b-paint-graduated — when the kind uses Graduated /
            // Categorized AND has any per-feature offset, prefer the
            // per-feature value (sourced from SWMMModelLayer's
            // featureOffset cache). Falls back to the kind-uniform
            // SingleSymbol offset otherwise.
            qreal offsetScene = 0.0;
            if (anyTypeOffset) {
                if (typeHasPerFeatureOffset[size_t(type)]) {
                    const double px = m_layer->featureOffset(
                        linkTypeToCategory[size_t(type)], i);
                    offsetScene = px * invViewScale;
                } else {
                    offsetScene = offsetByType[size_t(type)];
                }
            }
            QPolygonF offsetPoly;
            if (offsetScene != 0.0) {
                QPolygonF input;
                input.reserve(int(cnt));
                for (uint32_t j = 0; j < cnt; ++j)
                    input.append(QPointF(p[j * 2], p[j * 2 + 1]));
                offsetPoly =
                    OpenSWMM::Render::offsetPolyline(input, offsetScene);
            }

            auto emitSegments = [&](QVector<QLineF> &target) {
                if (offsetScene != 0.0) {
                    for (int j = 1; j < offsetPoly.size(); ++j)
                        target.emplace_back(offsetPoly[j - 1], offsetPoly[j]);
                } else {
                    for (uint32_t j = 1; j < cnt; ++j) {
                        target.emplace_back(
                            QPointF(p[(j - 1) * 2], p[(j - 1) * 2 + 1]),
                            QPointF(p[ j      * 2], p[ j      * 2 + 1]));
                    }
                }
            };

            // Phase 8.13.6.4 — non-selected links in an override-active
            // kind feed the per-colour sub-bucket. Selected links bypass
            // overrides so the selection halo paints in its own colour.
            if (sel) {
                emitSegments(selSegsByType[size_t(type)]);
            } else if (typeUsesOverrides[size_t(type)]) {
                const auto cat = linkTypeToCategory[size_t(type)];
                const QColor col = m_layer->featureColor(cat, i);
                const QRgb rgba  = col.isValid() ? col.rgba() : 0u;
                // Line archetypes carry the renderer's width axis in the size
                // channel (see SWMMModelLayer::rebuildKindFeatureColors).
                // Quantise to 1/16 px so near-equal widths still share a pen.
                const double wOv = m_layer->featureSize(cat, i);
                const quint32 wq = (wOv > 0.0)
                    ? quint32(std::lround(std::min(wOv, 256.0) * 16.0)) : 0u;
                const quint64 key = (quint64(wq) << 32) | quint64(rgba);
                emitSegments(overrideSegsByType[size_t(type)][key]);
            } else {
                emitSegments(segsByType[size_t(type)]);
            }
        }

        painter->setBrush(Qt::NoBrush);
        // Large projected coordinates (e.g. State Plane ~2e6) feed QPainter's
        // raster engine garbage at low zoom — the conduit "flashing lines"
        // bug. Project each segment to device pixels in double and stroke with
        // the transform reset, so the rasteriser only ever sees small on-screen
        // coordinates. This is the CPU-path equivalent of the content anchor
        // the QSG renderer already uses.
        const QTransform xf = painter->transform();
        auto drawDeviceLines = [&](const QVector<QLineF> &segs) {
            if (segs.isEmpty()) return;
            QVector<QLineF> dev;
            dev.reserve(segs.size());
            for (const QLineF &l : segs)
                dev.append(QLineF(xf.map(l.p1()), xf.map(l.p2())));
            painter->drawLines(dev);
        };
        painter->save();
        painter->resetTransform();   // draw link strokes in device space
        for (int t = 0; t < 5; ++t) {
            // Per-kind (sub-layer) opacity for this link type.
            const qreal kindOp =
                m_layer->categoryOpacity(linkTypeToCategory[size_t(t)]);
            auto fade = [kindOp](QColor c) {
                if (kindOp < 1.0 && c.isValid()) c.setAlphaF(c.alphaF() * kindOp);
                return c;
            };
            // Legacy fast path — single pen per kind.
            if (!segsByType[size_t(t)].isEmpty()) {
                QPen pen = linkPenForType(t);
                pen.setColor(fade(pen.color()));
                pen.setCosmetic(true);
                painter->setPen(pen);
                drawDeviceLines(segsByType[size_t(t)]);
            }
            // Phase 8.13.6.4 override path — one pen per unique colour
            // within the kind. The pen's width / style / cap / join are
            // inherited from the kind's base pen so Graduated /
            // Categorized stays visually consistent with the kind.
            if (!overrideSegsByType[size_t(t)].isEmpty()) {
                const QPen basePen = linkPenForType(t);
                for (auto it = overrideSegsByType[size_t(t)].constBegin();
                     it != overrideSegsByType[size_t(t)].constEnd(); ++it) {
                    QPen pen = basePen;
                    pen.setCosmetic(true);
                    pen.setColor(fade(QColor::fromRgba(QRgb(it.key() & 0xffffffffULL))));
                    // Upper 32 bits carry the per-feature width in 1/16 px;
                    // 0 means "no override" → keep the kind's base pen width.
                    if (const quint32 wq = quint32(it.key() >> 32); wq != 0u)
                        pen.setWidthF(double(wq) / 16.0);
                    painter->setPen(pen);
                    drawDeviceLines(it.value());
                }
            }
        }

        // Selected-link highlight pass. The selection pen drives colour
        // and is interpreted ADDITIVELY: its widthF() is added on top
        // of the per-link-type base pen, so the halo always projects
        // beyond the base line regardless of the link's own width. The
        // link's cap/join/style is inherited so the halo tracks the
        // user's pen choices unless they explicitly override.
        const QPen selPen = PreferencesManager::instance()->selectionPen(
            QStringLiteral("link"));
        for (int t = 0; t < 5; ++t) {
            if (selSegsByType[size_t(t)].isEmpty()) continue;
            QPen hi = linkPenForType(t);
            hi.setColor(selPen.color());
            hi.setWidthF(hi.widthF() + selPen.widthF());
            if (selPen.style() != Qt::SolidLine) hi.setStyle(selPen.style());
            hi.setCosmetic(true);
            painter->setPen(hi);
            drawDeviceLines(selSegsByType[size_t(t)]);
        }
        painter->restore();   // back to scene transform

        // ── Flow-direction arrows (Slice BI Phase 8.13.8-mini, 2026-05-24) ──
        // For each link kind whose SWMMElementSymbol::showArrows is true,
        // walk the visible links of that type and draw an arrowhead at
        // the polyline midpoint, pointing along the polyline tangent
        // (upstream → downstream). When arrowOnlyWhenFlowPos is set we
        // short-circuit on swmm_link_get_flow > 0 (Phase 8.13.8-α —
        // sidesteps the BI.2 expression DSL prereq). Arrows draw last
        // so they overlay both the base link and the selection halo.
        struct ArrowCfg { const SWMMElementSymbol *sym; bool enabled; };
        // Slice FX.1 — Outlets now have their own m_outletSym (was aliased
        // to m_conduitSym with enabled=false, which made Outlets uncontrollable).
        const std::array<ArrowCfg, 5> arrowCfg = {{
            {&m_layer->m_conduitSym, m_layer->m_conduitSym.showArrows},
            {&m_layer->m_pumpSym,    m_layer->m_pumpSym.showArrows   },
            {&m_layer->m_orificeSym, m_layer->m_orificeSym.showArrows},
            {&m_layer->m_weirSym,    m_layer->m_weirSym.showArrows   },
            {&m_layer->m_outletSym,  m_layer->m_outletSym.showArrows },
        }};
        const bool anyArrows = std::any_of(arrowCfg.begin(), arrowCfg.end(),
                                           [](const ArrowCfg &c) { return c.enabled; });
        if (anyArrows)
        {
            // Same large-coordinate remedy as the link strokes: map the arrow
            // anchor to device pixels and draw with the transform reset, so the
            // arrowheads don't garble/vanish at low zoom with large projected
            // coords. arrowSize is already in pixels, so it IS the device size.
            painter->save();
            painter->resetTransform();
            for (int k = 0; k < total; ++k)
            {
                const int i = useGrid ? visible[k] : k;
                if (i < 0 || i >= nLinks) continue;
                if (size_t(i) < linkHid.size() && linkHid[i]) continue;
                const uint32_t cnt = counts[i];
                if (cnt < 2) continue;

                const int type = (m_layer->m_links[i].linkType >= 0
                               && m_layer->m_links[i].linkType < 5)
                               ? m_layer->m_links[i].linkType : 0;
                const ArrowCfg &cfg = arrowCfg[size_t(type)];
                if (!cfg.enabled) continue;
                if (cfg.sym->arrowOnlyWhenFlowPos
                    && m_layer->linkFlow(i) <= 0.0) continue;

                const uint32_t off = offsets[i];
                const double  *p   = flat + size_t(off) * 2;
                QPointF mid;
                double  angle = 0.0;
                if (!polylineMidpoint(p, cnt, &mid, &angle)) continue;
                // QSG-parity — arrows fade with the kind's (sub-layer)
                // opacity like the base links do.
                QColor ac = cfg.sym->arrowColor;
                const qreal kop = m_layer->categoryOpacity(
                    linkTypeToCategory[size_t(type)]);
                if (kop < 1.0 && ac.isValid())
                    ac.setAlphaF(ac.alphaF() * kop);
                // Re-derive the heading in device space (robust to the view's
                // scale / Y orientation) by mapping the tangent through xf.
                const QPointF devMid = xf.map(mid);
                const QPointF devDir = xf.map(mid + QPointF(std::cos(angle),
                                                            std::sin(angle)));
                const double devAngle = std::atan2(devDir.y() - devMid.y(),
                                                   devDir.x() - devMid.x());
                drawFlowArrow(painter, devMid, devAngle,
                              cfg.sym->arrowSize, ac);
            }
            painter->restore();
        }
    }

    // ---------------------------------------------------------------- Nodes
    // §QSG-1: when QsgNodes is set the GPU overlay owns the node draw,
    // including the yellow selection halo (the QSG renderer's
    // `nodesSel` buffer). Painting again on CPU here would compose the
    // CPU halo BEFORE the GPU base glyphs blit on top, hiding the halo
    // entirely — so the CPU path bows out whenever the kind is owned.
    if (!qsgNodes && m_layer->m_showNodes)
    {
        // Bucket per node type so pen+brush only switch O(types) times
        // — all junctions draw together, then outfalls, etc. Each bucket
        // also tracks the SoA index alongside the scene point so the
        // per-feature override path (Phase 8.13.6.4) can look up the
        // bin / category colour for that specific feature.
        struct Bucket {
            const SWMMElementSymbol         *sym;
            int                              nodeType;
            SWMMModelLayer::Category         cat;
            QVector<QPointF>                 scenePts;
            QVector<int>                     indices;       // SoA index parallel to scenePts
            QVector<QPointF>                 selPts;
            QVector<int>                     selIndices;
        };
        // Bucket 4: virtual junctions — same CatJunctions category (D-G1:
        // no persisted 5th category) with a distinct marker override.
        Bucket buckets[5] = {
            {&m_layer->m_junctionSym, 0, SWMMModelLayer::CatJunctions, {}, {}, {}, {}},
            {&m_layer->m_outfallSym,  1, SWMMModelLayer::CatOutfalls,  {}, {}, {}, {}},
            {&m_layer->m_storageSym,  2, SWMMModelLayer::CatStorage,   {}, {}, {}, {}},
            {&m_layer->m_dividerSym,  3, SWMMModelLayer::CatDividers,  {}, {}, {}, {}},
            {&m_layer->m_virtualJunctionSym, 0, SWMMModelLayer::CatJunctions, {}, {}, {}, {}},
        };

        // Expand the cull window by the largest marker radius (in scene
        // units at the current zoom) so glyphs whose centre sits outside
        // exposedRect but whose body straddles it still draw.
        const double haloScene = 16.0 * invViewScale;

        const auto &nps = m_layer->m_nodeScenePts;
        for (int i = 0; i < m_layer->m_nodes.size(); ++i) {
            const auto &n = m_layer->m_nodes[i];
            if (size_t(i) < nodeHid.size() && nodeHid[i]) continue;
            if (i >= nps.size()) continue;
            const QPointF &sp = nps[i];
            if (!exposed.isNull() && !exposed.contains(sp)) {
                QRectF e = exposed.adjusted(-haloScene, -haloScene,
                                             haloScene,  haloScene);
                if (!e.contains(sp)) continue;
            }
            int t = (n.nodeType >= 0 && n.nodeType < 4) ? n.nodeType : 0;
            if (t == 0 && n.isVirtual) t = 4;   // virtual-junction marker override
            auto &b = buckets[t];
            const bool sel = size_t(i) < nodeSel.size() && nodeSel[i];
            if (sel) { b.selPts.append(sp);   b.selIndices.append(i); }
            else     { b.scenePts.append(sp); b.indices.append(i);    }
        }

        // Device-space projection (same large-coordinate remedy as the link
        // strokes): map each glyph centre to pixels and draw with the transform
        // reset, so node markers don't garble at low zoom with large projected
        // coords. Scene-unit radii are scaled back to pixels via the view scale.
        const QTransform xf = painter->transform();
        const double devScale = (xf.m11() > 0.0) ? xf.m11() : 1.0;
        auto devGlyph = [&](const QPointF &cScene, double rScene,
                            OpenSWMM::Render::MarkerShape shape) {
            drawNodeGlyph(painter, xf.map(cScene), rScene * devScale, shape);
        };
        painter->save();
        painter->resetTransform();   // draw glyphs in device space
        for (const Bucket &b : buckets) {
            if (b.scenePts.isEmpty() && b.selPts.isEmpty()) continue;
            // Fixed pixel-size glyphs — see the invViewScale comment at
            // the top of paint(). markerRadius returns PIXELS, not scene
            // units, so we scale into scene space for each glyph below.
            const double r = markerRadius(*b.sym) * invViewScale;
            const bool   overrides = m_layer->kindUsesOverrides(b.cat);

            const auto kindShape = b.sym->markerShape;
            // Per-kind (sub-layer) opacity — multiplies every glyph's alpha.
            const qreal kindOp = m_layer->categoryOpacity(b.cat);
            auto fade = [kindOp](QColor c) {
                if (kindOp < 1.0 && c.isValid())
                    c.setAlphaF(c.alphaF() * kindOp);
                return c;
            };

            // Base pass.
            if (!b.scenePts.isEmpty()) {
                QPen pen(fade(b.sym->outlineColor), b.sym->outlineWidth);
                pen.setCosmetic(true);
                painter->setPen(pen);
                if (!overrides) {
                    // Legacy bucketed fast path — single brush for the
                    // entire bucket.
                    painter->setBrush(QBrush(fade(b.sym->fillColor)));
                    for (const QPointF &c : b.scenePts)
                        devGlyph(c, r, kindShape);
                } else {
                    // Phase 8.13.6.4 per-feature override path — fill
                    // colour comes from the renderer's per-feature
                    // cache. Phase 8.13.43-α extends with per-feature
                    // SIZE override (negative sentinel = no override,
                    // keep kind's default glyph radius).
                    for (int j = 0; j < b.scenePts.size(); ++j) {
                        const QColor col = m_layer->featureColor(b.cat, b.indices[j]);
                        painter->setBrush(QBrush(fade(col.isValid() ? col : b.sym->fillColor)));
                        const double szOverride = m_layer->featureSize(b.cat, b.indices[j]);
                        const double rEff = (szOverride > 0.0)
                            ? (szOverride * 0.5 * invViewScale)
                            : r;
                        // M3 — per-feature marker shape from the renderer
                        // (Categorized / Rule-based); -1 = keep kind base shape.
                        const int fsh = m_layer->featureShape(b.cat, b.indices[j]);
                        const auto shp = (fsh >= 0)
                            ? static_cast<OpenSWMM::Render::MarkerShape>(fsh)
                            : kindShape;
                        devGlyph(b.scenePts[j], rEff, shp);
                    }
                }
            }

            // Selection-highlight pass. Pen + brush both come from
            // PreferencesManager. The outline keeps the symbol's own
            // colour as a fallback when the user hasn't set a custom
            // pen colour (selectionPen("node") otherwise wins).
            if (!b.selPts.isEmpty()) {
                auto *prefs = PreferencesManager::instance();
                QPen pen = prefs->selectionPen(QStringLiteral("node"));
                pen.setCosmetic(true);
                painter->setPen(pen);
                painter->setBrush(prefs->selectionBrush(QStringLiteral("node")));
                for (const QPointF &c : b.selPts)
                    devGlyph(c, r, kindShape);
            }

            // Virtual-junction mark — a dotted ring encircling the dot with
            // a small gap, in the symbol's own colour (D-G1: the glyph body
            // stays the junction blue; the ring is what sets it apart).
            if (b.sym == &m_layer->m_virtualJunctionSym) {
                QPen ring(fade(b.sym->fillColor), 1.0, Qt::DotLine);
                ring.setCosmetic(true);
                painter->setPen(ring);
                painter->setBrush(Qt::NoBrush);
                const double ringDev = r * devScale + 3.0;   // glyph px + gap
                for (const QPointF &c : b.scenePts)
                    painter->drawEllipse(xf.map(c), ringDev, ringDev);
                for (const QPointF &c : b.selPts)
                    painter->drawEllipse(xf.map(c), ringDev, ringDev);
            }
        }
        painter->restore();   // back to scene transform
    }

    // ---------------------------------------------------------------- Rain gages
    // Same §QSG-1 skip as nodes — QSG owns gages when QsgGages is set.
    if (!qsgGages && m_layer->m_showRainGages && !m_layer->m_gages.isEmpty())
    {
        const auto &sym = m_layer->m_gageSym;
        // Fixed pixel-size glyph — see invViewScale comment at top.
        const double r          = markerRadius(sym) * invViewScale;
        const double haloScene  = 16.0 * invViewScale;

        // Bucket the gage pass the same way nodes are bucketed, so
        // selected gages paint yellow on top of the base fill. Without
        // this second pass rain-gage selection looks like a no-op even
        // though the layer's selection set is correctly updated —
        // which is exactly the regression the user reported.
        QVector<QPointF> basePts, selPts;
        QVector<int>     baseIdxs;   // parallel to basePts (Phase 8.13.6.4 override lookup)
        const auto &gps = m_layer->m_gageScenePts;
        for (int i = 0; i < m_layer->m_gages.size(); ++i)
        {
            if (size_t(i) < gageHid.size() && gageHid[i]) continue;
            if (i >= gps.size()) continue;
            const QPointF &sp = gps[i];
            if (!exposed.isNull() && !exposed.contains(sp)) {
                QRectF e = exposed.adjusted(-haloScene, -haloScene,
                                             haloScene,  haloScene);
                if (!e.contains(sp)) continue;
            }
            const bool sel = size_t(i) < gageSel.size() && gageSel[i];
            if (sel) { selPts.append(sp); }
            else     { basePts.append(sp); baseIdxs.append(i); }
        }

        const bool gageOverrides = m_layer->kindUsesOverrides(SWMMModelLayer::CatRainGages);

        const auto gageShape = sym.markerShape;

        // Device-space projection (same large-coordinate remedy as links/nodes).
        const QTransform xf = painter->transform();
        const double devScale = (xf.m11() > 0.0) ? xf.m11() : 1.0;
        auto devGlyph = [&](const QPointF &cScene, double rScene,
                            OpenSWMM::Render::MarkerShape shape) {
            drawNodeGlyph(painter, xf.map(cScene), rScene * devScale, shape);
        };
        painter->save();
        painter->resetTransform();   // draw glyphs in device space

        if (!basePts.isEmpty()) {
            QPen pen(sym.outlineColor, sym.outlineWidth);
            pen.setCosmetic(true);
            painter->setPen(pen);
            if (!gageOverrides) {
                painter->setBrush(QBrush(sym.fillColor));
                for (const QPointF &sp : basePts)
                    devGlyph(sp, r, gageShape);
            } else {
                for (int j = 0; j < basePts.size(); ++j) {
                    const QColor col = m_layer->featureColor(
                        SWMMModelLayer::CatRainGages, baseIdxs[j]);
                    painter->setBrush(QBrush(col.isValid() ? col : sym.fillColor));
                    const double szOverride = m_layer->featureSize(
                        SWMMModelLayer::CatRainGages, baseIdxs[j]);
                    const double rEff = (szOverride > 0.0)
                        ? (szOverride * 0.5 * invViewScale)
                        : r;
                    devGlyph(basePts[j], rEff, gageShape);
                }
            }
        }
        if (!selPts.isEmpty()) {
            auto *prefs = PreferencesManager::instance();
            QPen pen = prefs->selectionPen(QStringLiteral("gage"));
            pen.setCosmetic(true);
            painter->setPen(pen);
            painter->setBrush(prefs->selectionBrush(QStringLiteral("gage")));
            for (const QPointF &sp : selPts)
                devGlyph(sp, r, gageShape);
        }
        painter->restore();   // back to scene transform
    }

    // ---------------------------------------------------------------- Labels
    // Labeling overhaul (LAYER_STYLING_LABELING_PLAN_2026-08-16):
    //  * text resolves from LabelConfig::expression ({token} template) or
    //    fieldName, falling back to the object name;
    //  * every visible kind is labelled — nodes, links, subcatchments and
    //    rain gages — with the per-kind SWMMElementSymbol.showLabel /
    //    labelFont / labelColor acting as a per-kind enable + font/colour
    //    override on top of the layer-level LabelConfig;
    //  * drawn in DEVICE space through the shared LabelPainter, so the
    //    point size is constant across zoom (screen-space contract);
    //  * honours the min/max scale window (LabelPainter::scaleVisible)
    //    against the owning MapCanvas' scale denominator;
    //  * greedy screen-rect collision pruning, highest priorityField value
    //    first (unset priority = insertion order).
    //
    // QSG contract: this block deliberately does NOT consult qsgOwnsKind —
    // labels are always CPU-painted, even for kinds whose geometry the QSG
    // overlay owns (the QSG renderer has no text pipeline; see the doc atop
    // swmmlayerqsgrenderer.cpp).
    const auto &labelCfg = m_layer->labelConfig();
    {
        using OpenSWMM::Render::LabelConfig;
        using OpenSWMM::Render::LabelPainter;

        const bool layerOn = labelCfg.enabled || m_layer->m_showLabels;

        auto symForCat = [&](SWMMModelLayer::Category c)
            -> const SWMMElementSymbol * {
            switch (c) {
            case SWMMModelLayer::CatJunctions:     return &m_layer->m_junctionSym;
            case SWMMModelLayer::CatOutfalls:      return &m_layer->m_outfallSym;
            case SWMMModelLayer::CatStorage:       return &m_layer->m_storageSym;
            case SWMMModelLayer::CatDividers:      return &m_layer->m_dividerSym;
            case SWMMModelLayer::CatConduits:      return &m_layer->m_conduitSym;
            case SWMMModelLayer::CatPumps:         return &m_layer->m_pumpSym;
            case SWMMModelLayer::CatOrifices:      return &m_layer->m_orificeSym;
            case SWMMModelLayer::CatWeirs:         return &m_layer->m_weirSym;
            case SWMMModelLayer::CatOutlets:       return &m_layer->m_outletSym;
            case SWMMModelLayer::CatSubcatchments: return &m_layer->m_subcatchSym;
            case SWMMModelLayer::CatRainGages:     return &m_layer->m_gageSym;
            default:                               return nullptr;
            }
        };
        // A kind is labelled when the layer-level config is on, or the
        // kind's own showLabel toggle (Symbology → Labels group) is set.
        auto kindOn = [&](SWMMModelLayer::Category c) {
            if (layerOn) return true;
            const auto *s = symForCat(c);
            return s && s->showLabel;
        };

        bool anyOn = false;
        for (int c = 0; c < SWMMModelLayer::NumCategories && !anyOn; ++c)
            anyOn = kindOn(static_cast<SWMMModelLayer::Category>(c));

        const qreal m11    = painter->transform().m11();
        const qreal m11Min = PreferencesManager::instance()->labelLodM11Min();

        // Scale-window gate — resolve the owning canvas' scale denominator
        // by walking up from the paint widget (viewport → view → MapCanvas).
        double scaleDen = 0.0;
        for (const QWidget *w = widget; w; w = w->parentWidget()) {
            if (auto *canvas = qobject_cast<const MapCanvas *>(w)) {
                scaleDen = canvas->scaleDenominator();
                break;
            }
        }

        if (anyOn && m11 >= m11Min
            && LabelPainter::scaleVisible(labelCfg, scaleDen))
        {
            // Per-kind effective config: layer LabelConfig with the kind's
            // labelFont / labelColor applied when its showLabel is set
            // (that's the per-kind override contract that makes the
            // Symbology-tab Labels group live UI).
            std::array<LabelConfig, SWMMModelLayer::NumCategories> kindCfg;
            for (int c = 0; c < SWMMModelLayer::NumCategories; ++c) {
                LabelConfig eff = labelCfg;
                const auto *s = symForCat(static_cast<SWMMModelLayer::Category>(c));
                if (s && s->showLabel) {
                    // Every override is opt-in: only a value the user
                    // actually set displaces the layer's LabelConfig.
                    //
                    // `font` used to be assigned unconditionally while size
                    // and colour were guarded. SWMMElementSymbol::labelFont
                    // is a default-constructed QFont, so merely ticking a
                    // kind's "Show labels" — without touching its font —
                    // silently reset that kind's family/weight/slant to the
                    // application default while the size and colour set on
                    // the Labels tab correctly survived.
                    if (s->labelFont != QFont()) {
                        eff.font = s->labelFont;
                        if (s->labelFont.pointSizeF() > 0.0)
                            eff.fontSizePt = s->labelFont.pointSizeF();
                    }
                    if (s->labelColor.isValid())
                        eff.color = s->labelColor;
                }
                kindCfg[size_t(c)] = eff;
            }

            // Text + priority resolution. identifyByName (a full attribute
            // fetch per feature) is only consulted when the config actually
            // references attribute values — an expression using only {name}
            // does not count. Resolved values are cached per feature in
            // m_labelCache (invalidated on data edits via the layer's
            // editRevision, or when the config's text fields change), so
            // pan/zoom repaints don't re-resolve.
            const bool exprNeedsAttrs =
                !labelCfg.expression.isEmpty()
                && labelCfg.expression.contains(QLatin1Char('{'))
                && QString(labelCfg.expression)
                           .remove(QLatin1String("{name}"))
                           .contains(QLatin1Char('{'));
            const bool needsAttrs = exprNeedsAttrs
                                 || !labelCfg.fieldName.isEmpty()
                                 || !labelCfg.priorityField.isEmpty();
            auto resolveExpression = [](const QString &expr,
                                        const QString &name,
                                        const QVariantMap &attrs) {
                QString out;
                out.reserve(expr.size());
                int i = 0;
                while (i < expr.size()) {
                    if (expr[i] == QLatin1Char('{')) {
                        const int close = expr.indexOf(QLatin1Char('}'), i + 1);
                        if (close > i) {
                            const QString token = expr.mid(i + 1, close - i - 1);
                            out += (token == QLatin1String("name"))
                                       ? name
                                       : attrs.value(token).toString();
                            i = close + 1;
                            continue;
                        }
                    }
                    out += expr[i];
                    ++i;
                }
                return out;
            };

            // Validate the label cache against the layer's data epoch and
            // the config's text-affecting fields; (re)size lazily-filled
            // per-group vectors when stale.
            if (needsAttrs) {
                if (m_labelCache.editRev != m_layer->editRevision()
                    || m_labelCache.expression    != labelCfg.expression
                    || m_labelCache.fieldName     != labelCfg.fieldName
                    || m_labelCache.priorityField != labelCfg.priorityField
                    // SoA size drift (model reload / geometry rebuild paths
                    // that don't emit modelEdited) — indices would shift.
                    || m_labelCache.text[0].size() != m_layer->m_nodes.size()
                    || m_labelCache.text[1].size() != m_layer->m_links.size()
                    || m_labelCache.text[2].size() != m_layer->m_catchments.size()
                    || m_labelCache.text[3].size() != m_layer->m_gages.size()) {
                    m_labelCache.clear();
                    m_labelCache.editRev       = m_layer->editRevision();
                    m_labelCache.expression    = labelCfg.expression;
                    m_labelCache.fieldName     = labelCfg.fieldName;
                    m_labelCache.priorityField = labelCfg.priorityField;
                    m_labelCache.text[0].resize(m_layer->m_nodes.size());
                    m_labelCache.text[1].resize(m_layer->m_links.size());
                    m_labelCache.text[2].resize(m_layer->m_catchments.size());
                    m_labelCache.text[3].resize(m_layer->m_gages.size());
                    for (auto &p : m_labelCache.priority)
                        p.clear();
                    m_labelCache.priority[0].resize(m_layer->m_nodes.size());
                    m_labelCache.priority[1].resize(m_layer->m_links.size());
                    m_labelCache.priority[2].resize(m_layer->m_catchments.size());
                    m_labelCache.priority[3].resize(m_layer->m_gages.size());
                }
            }

            struct PendingLabel {
                QPointF anchorDev;   // device-space anchor
                QString text;
                double  priority = 0.0;
                int     cat      = 0;
            };
            QVector<PendingLabel> pending;

            const QTransform xf = painter->transform();
            // \p group: 0=node 1=link 2=catch 3=gage; \p soa is the index
            // within that group's SoA (cache slot).
            auto append = [&](SWMMModelLayer::Category c, int group, int soa,
                              const QString &name, const QPointF &sceneAnchor) {
                PendingLabel pl;
                pl.cat       = int(c);
                pl.anchorDev = xf.map(sceneAnchor);
                if (needsAttrs) {
                    const bool cachable =
                        group >= 0 && group < 4
                        && soa >= 0 && soa < m_labelCache.text[group].size();
                    if (cachable && !m_labelCache.text[group][soa].isNull()) {
                        pl.text     = m_labelCache.text[group][soa];
                        pl.priority = m_labelCache.priority[group][soa];
                    } else {
                        const QVariantMap attrs = m_layer->identifyByName(name);
                        if (!labelCfg.expression.isEmpty())
                            pl.text = resolveExpression(labelCfg.expression, name, attrs);
                        else if (!labelCfg.fieldName.isEmpty())
                            pl.text = attrs.value(labelCfg.fieldName).toString();
                        if (pl.text.isEmpty())
                            pl.text = name;
                        if (!labelCfg.priorityField.isEmpty())
                            pl.priority = attrs.value(labelCfg.priorityField).toDouble();
                        if (cachable) {
                            m_labelCache.text[group][soa]     = pl.text;
                            m_labelCache.priority[group][soa] = pl.priority;
                        }
                    }
                } else if (!labelCfg.expression.isEmpty()) {
                    // {name}-only expression — resolve without an attribute
                    // fetch (unknown tokens go empty by contract).
                    pl.text = resolveExpression(labelCfg.expression, name, {});
                    if (pl.text.isEmpty())
                        pl.text = name;
                } else {
                    pl.text = name;
                }
                if (!pl.text.isEmpty())
                    pending.append(pl);
            };

            // ── Nodes ──
            if (m_layer->m_showNodes) {
                const auto &nps = m_layer->m_nodeScenePts;
                for (int i = 0; i < m_layer->m_nodes.size(); ++i) {
                    const auto &n = m_layer->m_nodes[i];
                    if (size_t(i) < nodeHid.size() && nodeHid[i]) continue;
                    if (i >= nps.size()) continue;
                    const int t = (n.nodeType >= 0 && n.nodeType < 4) ? n.nodeType : 0;
                    const auto cat = static_cast<SWMMModelLayer::Category>(
                        int(SWMMModelLayer::CatJunctions) + t);
                    if (!kindOn(cat)) continue;
                    const QPointF &sp = nps[i];
                    if (!exposed.isNull() && !exposed.contains(sp)) continue;
                    append(cat, /*group=*/0, i, n.name, sp);
                }
            }

            // ── Links (label at the middle vertex of the polyline) ──
            if (m_layer->m_showLinks
                && m_layer->m_linkVertexCount.size()
                       >= size_t(m_layer->m_links.size())
                && m_layer->m_linkVertexOffset.size()
                       >= size_t(m_layer->m_links.size())) {
                static constexpr SWMMModelLayer::Category linkCats[5] = {
                    SWMMModelLayer::CatConduits, SWMMModelLayer::CatPumps,
                    SWMMModelLayer::CatOrifices, SWMMModelLayer::CatWeirs,
                    SWMMModelLayer::CatOutlets };
                const double   *flat    = m_layer->m_linkSceneFlat.data();
                const uint32_t *offsets = m_layer->m_linkVertexOffset.data();
                const uint32_t *counts  = m_layer->m_linkVertexCount.data();
                for (int i = 0; i < m_layer->m_links.size(); ++i) {
                    if (size_t(i) < linkHid.size() && linkHid[i]) continue;
                    const uint32_t cnt = counts[i];
                    if (cnt < 2) continue;
                    const int t = (m_layer->m_links[i].linkType >= 0
                                && m_layer->m_links[i].linkType < 5)
                                ? m_layer->m_links[i].linkType : 0;
                    const auto cat = linkCats[t];
                    if (!kindOn(cat)) continue;
                    const double *p = flat + size_t(offsets[i]) * 2;
                    // Midpoint of the middle segment — cheap and close
                    // enough to the visual centre for labelling.
                    const uint32_t m0 = (cnt - 1) / 2, m1 = m0 + 1;
                    const QPointF mid(
                        (p[m0 * 2]     + p[m1 * 2])     * 0.5,
                        (p[m0 * 2 + 1] + p[m1 * 2 + 1]) * 0.5);
                    if (!exposed.isNull() && !exposed.contains(mid)) continue;
                    append(cat, /*group=*/1, i, m_layer->m_links[i].name, mid);
                }
            }

            // ── Subcatchments (bbox centre) ──
            if (m_layer->m_showSubcatchments
                && kindOn(SWMMModelLayer::CatSubcatchments)) {
                const auto &cboxes = m_layer->m_catchSceneBBoxes;
                for (int i = 0; i < m_layer->m_catchments.size(); ++i) {
                    if (size_t(i) < catchHid.size() && catchHid[i]) continue;
                    if (i >= cboxes.size() || !cboxes[i].isValid()) continue;
                    const QPointF ctr = cboxes[i].center();
                    if (!exposed.isNull() && !exposed.contains(ctr)) continue;
                    append(SWMMModelLayer::CatSubcatchments, /*group=*/2, i,
                           m_layer->m_catchments[i].name, ctr);
                }
            }

            // ── Rain gages ──
            if (m_layer->m_showRainGages
                && kindOn(SWMMModelLayer::CatRainGages)) {
                const auto &gps = m_layer->m_gageScenePts;
                for (int i = 0; i < m_layer->m_gages.size(); ++i) {
                    if (size_t(i) < gageHid.size() && gageHid[i]) continue;
                    if (i >= gps.size()) continue;
                    const QPointF &sp = gps[i];
                    if (!exposed.isNull() && !exposed.contains(sp)) continue;
                    append(SWMMModelLayer::CatRainGages, /*group=*/3, i,
                           m_layer->m_gages[i].name, sp);
                }
            }

            // Priority ordering — highest first; stable so equal (or unset)
            // priorities keep insertion order.
            if (!labelCfg.priorityField.isEmpty())
                std::stable_sort(pending.begin(), pending.end(),
                                 [](const PendingLabel &a, const PendingLabel &b) {
                                     return a.priority > b.priority;
                                 });

            // Greedy screen-space collision pruning + draw (device space,
            // constant point size across zoom).
            painter->save();
            painter->resetTransform();
            QVector<QRectF> taken;
            taken.reserve(pending.size());
            for (const PendingLabel &pl : pending) {
                const LabelConfig &cfg = kindCfg[size_t(pl.cat)];
                const QFontMetricsF fm(cfg.effectiveFont());
                const QSizeF textSize(fm.horizontalAdvance(pl.text), fm.height());
                const QRectF rect =
                    LabelPainter::labelRect(cfg, pl.anchorDev, textSize);
                bool collides = false;
                for (const QRectF &r : taken)
                    if (r.intersects(rect)) { collides = true; break; }
                if (collides) continue;
                taken.append(rect);
                LabelPainter::drawLabelAt(*painter, pl.anchorDev, pl.text, cfg);
            }
            painter->restore();
        }
    }

    // Opt-in (openswmm.render.perf) — this used to log unconditionally on
    // every scene paint, which itself cost formatting time per frame.
    qCDebug(lcRenderPerf).noquote()
                       << "[SWMMLayerItem::paint] setup_ms=" << t_setup
                       << " total_ms=" << t_total.elapsed()
                       << " links=" << m_layer->m_links.size()
                       << " grid=" << (m_layer->m_linkGrid.isEmpty() ? "n/a"
                            : QString("%1x%2").arg(m_layer->m_linkGrid.cols)
                                              .arg(m_layer->m_linkGrid.rows))
                       << " nodes=" << m_layer->m_nodes.size()
                       << " selected=" << selected.size()
                       << " exposed=" << exposed;

    // Pair with the painter->save() at the top of paint() that installed
    // the Z.14-paint mask clip (if any). Always called, mask or no mask.
    painter->restore();
}
