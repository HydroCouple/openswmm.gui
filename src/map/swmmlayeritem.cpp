/*!
 * \file   swmmlayeritem.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/swmmlayeritem.h"

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>

#include <ogr_spatialref.h>   // OGRCoordinateTransformation

#include <array>
#include <cmath>

namespace {

/*! Scene-space Y-flip helper — matches the legacy toScene(mx, my) in
 *  swmmmodellayer.cpp (scene Y grows downward, map Y grows upward). */
inline QPointF toScene(double mx, double my) { return QPointF(mx, -my); }

/*! Marker radius is a pen/brush-sized dot painted in scene units at a
 *  cosmetic pixel-size. Kept small so nodes stay visually compact at
 *  typical zoom; symbology->size is the designer-set marker diameter. */
inline double markerRadius(const SWMMElementSymbol &s) { return s.size * 0.5; }

/*! Shape-sensitive node glyph drawer. Mirrors NodeGraphicsItem's old
 *  per-item rendering so switching to the batched path is visually
 *  identical for the existing palette. Expects a caller that has
 *  already pushed the right pen + brush; just draws the outline. */
void drawNodeGlyph(QPainter *p, const QPointF &c, double r, int nodeType)
{
    switch (nodeType) {
    case 1: { // Outfall — triangle
        QPolygonF tri;
        tri << QPointF(c.x(),         c.y() - r)
            << QPointF(c.x() - r,     c.y() + r * 0.8)
            << QPointF(c.x() + r,     c.y() + r * 0.8);
        p->drawPolygon(tri);
        break;
    }
    case 2: { // Storage — square
        p->drawRect(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r));
        break;
    }
    case 3: { // Divider — diamond
        QPolygonF dia;
        dia << QPointF(c.x(),         c.y() - r)
            << QPointF(c.x() + r,     c.y())
            << QPointF(c.x(),         c.y() + r)
            << QPointF(c.x() - r,     c.y());
        p->drawPolygon(dia);
        break;
    }
    default: // 0 = Junction (and anything unknown) — circle
        p->drawEllipse(c, r, r);
        break;
    }
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
                          QWidget * /*widget*/)
{
    if (!m_layer || !m_layer->isVisible()) return;

    QElapsedTimer t_total; t_total.start();
    painter->setOpacity(m_layer->opacity());

    const QRectF exposed = option->exposedRect;
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

    // Phase B.RHI — `glRenderingEnabled` is repurposed: when true, an
    // EXTERNAL GPU renderer (the QSG overlay in MapCanvas) is handling
    // the link draw, so we skip lines here. Subcatchments / nodes /
    // gages continue on this CPU path until B.RHI.3 moves them to QSG
    // too. The `glOn` local keeps its old name for diff readability.
    const bool glOn = m_layer->glRenderingEnabled();

    // ---------------------------------------------------------------- Subcatchments
    if (!glOn && m_layer->m_showSubcatchments)
    {
        const auto &sym = m_layer->m_subcatchSym;
        QPen pen(sym.outlineColor, sym.outlineWidth);
        pen.setCosmetic(true);
        pen.setCapStyle (Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);

        painter->setPen(pen);
        painter->setBrush(QBrush(sym.fillColor));

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
                painter->setBrush(QBrush(col.isValid() ? col : sym.fillColor));
            }
            painter->drawPolygon(poly);
            if (sel || catchOverrides) painter->setBrush(QBrush(sym.fillColor));
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
    if (!glOn && m_layer->m_showLinks)
    {
        // Per-link-type pen comes from PreferencesManager so colour,
        // width, cap, join and dash are all user-tunable from the
        // Preferences dialog's Rendering page. Outlets (case 4) now
        // pull their own pen instead of falling back to the conduit
        // symbol.
        auto linkPenForType = [](int linkType) {
            auto *prefs = PreferencesManager::instance();
            switch (linkType) {
            case 1:  return prefs->linkPen(QStringLiteral("pump"));
            case 2:  return prefs->linkPen(QStringLiteral("orifice"));
            case 3:  return prefs->linkPen(QStringLiteral("weir"));
            case 4:  return prefs->linkPen(QStringLiteral("outlet"));
            default: return prefs->linkPen(QStringLiteral("conduit"));
            }
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
        std::array<QHash<QRgb, QVector<QLineF>>, 5> overrideSegsByType;

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

            // Phase 8.13.6.4 — non-selected links in an override-active
            // kind feed the per-colour sub-bucket. Selected links bypass
            // overrides so the selection halo paints in its own colour.
            if (sel) {
                auto &target = selSegsByType[size_t(type)];
                for (uint32_t j = 1; j < cnt; ++j) {
                    target.emplace_back(QPointF(p[(j - 1) * 2], p[(j - 1) * 2 + 1]),
                                        QPointF(p[ j      * 2], p[ j      * 2 + 1]));
                }
            } else if (typeUsesOverrides[size_t(type)]) {
                const QColor col = m_layer->featureColor(linkTypeToCategory[size_t(type)], i);
                const QRgb key = col.isValid() ? col.rgba() : 0u;
                auto &target = overrideSegsByType[size_t(type)][key];
                for (uint32_t j = 1; j < cnt; ++j) {
                    target.emplace_back(QPointF(p[(j - 1) * 2], p[(j - 1) * 2 + 1]),
                                        QPointF(p[ j      * 2], p[ j      * 2 + 1]));
                }
            } else {
                auto &target = segsByType[size_t(type)];
                for (uint32_t j = 1; j < cnt; ++j) {
                    target.emplace_back(QPointF(p[(j - 1) * 2], p[(j - 1) * 2 + 1]),
                                        QPointF(p[ j      * 2], p[ j      * 2 + 1]));
                }
            }
        }

        painter->setBrush(Qt::NoBrush);
        for (int t = 0; t < 5; ++t) {
            // Legacy fast path — single pen per kind.
            if (!segsByType[size_t(t)].isEmpty()) {
                QPen pen = linkPenForType(t);
                pen.setCosmetic(true);
                painter->setPen(pen);
                painter->drawLines(segsByType[size_t(t)]);
            }
            // Phase 8.13.6.4 override path — one pen per unique colour
            // within the kind. The pen's width / style / cap / join are
            // inherited from the kind's base pen so Graduated /
            // Categorized stays visually consistent with the kind.
            if (!overrideSegsByType[size_t(t)].isEmpty()) {
                QPen pen = linkPenForType(t);
                pen.setCosmetic(true);
                for (auto it = overrideSegsByType[size_t(t)].constBegin();
                     it != overrideSegsByType[size_t(t)].constEnd(); ++it) {
                    pen.setColor(QColor::fromRgba(it.key()));
                    painter->setPen(pen);
                    painter->drawLines(it.value());
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
            painter->drawLines(selSegsByType[size_t(t)]);
        }

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
                const double lenScene = cfg.sym->arrowSize * invViewScale;
                drawFlowArrow(painter, mid, angle, lenScene, cfg.sym->arrowColor);
            }
        }
    }

    // ---------------------------------------------------------------- Nodes
    // B.RHI.3c — when the QSG overlay is active it owns ALL node /
    // gage / subcatchment rendering, including selection highlight.
    // Painting them again on the CPU here would compose on top of the
    // canvas blit (CPU yellow), then QSG paints fresh base-color
    // glyphs on top of that, hiding the highlight entirely. Skip the
    // CPU pass whenever glOn is true.
    if (!glOn && m_layer->m_showNodes)
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
        Bucket buckets[4] = {
            {&m_layer->m_junctionSym, 0, SWMMModelLayer::CatJunctions, {}, {}, {}, {}},
            {&m_layer->m_outfallSym,  1, SWMMModelLayer::CatOutfalls,  {}, {}, {}, {}},
            {&m_layer->m_storageSym,  2, SWMMModelLayer::CatStorage,   {}, {}, {}, {}},
            {&m_layer->m_dividerSym,  3, SWMMModelLayer::CatDividers,  {}, {}, {}, {}},
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
            const int t = (n.nodeType >= 0 && n.nodeType < 4) ? n.nodeType : 0;
            auto &b = buckets[t];
            const bool sel = size_t(i) < nodeSel.size() && nodeSel[i];
            if (sel) { b.selPts.append(sp);   b.selIndices.append(i); }
            else     { b.scenePts.append(sp); b.indices.append(i);    }
        }

        for (const Bucket &b : buckets) {
            if (b.scenePts.isEmpty() && b.selPts.isEmpty()) continue;
            // Fixed pixel-size glyphs — see the invViewScale comment at
            // the top of paint(). markerRadius returns PIXELS, not scene
            // units, so we scale into scene space for each glyph below.
            const double r = markerRadius(*b.sym) * invViewScale;
            const bool   overrides = m_layer->kindUsesOverrides(b.cat);

            // Base pass.
            if (!b.scenePts.isEmpty()) {
                QPen pen(b.sym->outlineColor, b.sym->outlineWidth);
                pen.setCosmetic(true);
                painter->setPen(pen);
                if (!overrides) {
                    // Legacy bucketed fast path — single brush for the
                    // entire bucket.
                    painter->setBrush(QBrush(b.sym->fillColor));
                    for (const QPointF &c : b.scenePts)
                        drawNodeGlyph(painter, c, r, b.nodeType);
                } else {
                    // Phase 8.13.6.4 per-feature override path — fill
                    // colour comes from the renderer's per-feature
                    // cache. Phase 8.13.43-α extends with per-feature
                    // SIZE override (negative sentinel = no override,
                    // keep kind's default glyph radius).
                    for (int j = 0; j < b.scenePts.size(); ++j) {
                        const QColor col = m_layer->featureColor(b.cat, b.indices[j]);
                        painter->setBrush(QBrush(col.isValid() ? col : b.sym->fillColor));
                        const double szOverride = m_layer->featureSize(b.cat, b.indices[j]);
                        const double rEff = (szOverride > 0.0)
                            ? (szOverride * 0.5 * invViewScale)
                            : r;
                        drawNodeGlyph(painter, b.scenePts[j], rEff, b.nodeType);
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
                    drawNodeGlyph(painter, c, r, b.nodeType);
            }
        }
    }

    // ---------------------------------------------------------------- Rain gages
    // Same B.RHI.3c skip as nodes — QSG owns gage rendering when glOn.
    if (!glOn && m_layer->m_showRainGages && !m_layer->m_gages.isEmpty())
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

        if (!basePts.isEmpty()) {
            QPen pen(sym.outlineColor, sym.outlineWidth);
            pen.setCosmetic(true);
            painter->setPen(pen);
            if (!gageOverrides) {
                painter->setBrush(QBrush(sym.fillColor));
                for (const QPointF &sp : basePts)
                    drawNodeGlyph(painter, sp, r, /*diamond*/3);
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
                    drawNodeGlyph(painter, basePts[j], rEff, /*diamond*/3);
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
                drawNodeGlyph(painter, sp, r, /*diamond*/3);
        }
    }

    // ---------------------------------------------------------------- Labels
    if (m_layer->m_showLabels && m_layer->m_showNodes)
    {
        // Labels are expensive; draw only for visible, un-hidden nodes.
        // LOD: skip labels entirely when the zoom is too coarse for them
        // to be legible. Threshold lives in PreferencesManager (Slice V)
        // so users can tune it via Tools → Preferences.
        const qreal m11    = painter->transform().m11();
        const qreal m11Min = PreferencesManager::instance()->labelLodM11Min();
        if (m11 >= m11Min) {
            painter->setPen(QColor(m_layer->m_junctionSym.labelColor));
            const auto &nps = m_layer->m_nodeScenePts;
            for (int i = 0; i < m_layer->m_nodes.size(); ++i) {
                const auto &n = m_layer->m_nodes[i];
                if (size_t(i) < nodeHid.size() && nodeHid[i]) continue;
                if (i >= nps.size()) continue;
                const QPointF &sp = nps[i];
                if (!exposed.isNull() && !exposed.contains(sp)) continue;
                painter->drawText(sp + QPointF(6, -4), n.name);
            }
        }
    }

    qDebug().noquote() << "[SWMMLayerItem::paint] setup_ms=" << t_setup
                       << " total_ms=" << t_total.elapsed()
                       << " links=" << m_layer->m_links.size()
                       << " grid=" << (m_layer->m_linkGrid.isEmpty() ? "n/a"
                            : QString("%1x%2").arg(m_layer->m_linkGrid.cols)
                                              .arg(m_layer->m_linkGrid.rows))
                       << " nodes=" << m_layer->m_nodes.size()
                       << " selected=" << selected.size()
                       << " exposed=" << exposed;
}
