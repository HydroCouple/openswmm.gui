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
            if (sel) painter->setBrush(QColor(255, 255, 0, 120));
            painter->drawPolygon(poly);
            if (sel) painter->setBrush(QBrush(sym.fillColor));
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
        auto linkColorForType = [this](int linkType) {
            auto *prefs = PreferencesManager::instance();
            switch (linkType) {
            case 1:  return m_layer->m_pumpSym.fillColor;
            case 2:  return m_layer->m_orificeSym.fillColor;
            case 3:  return m_layer->m_weirSym.fillColor;
            case 4:  return prefs->linkColor(QStringLiteral("outlet"));
            default: return m_layer->m_conduitSym.fillColor;
            }
        };
        auto linkWidthForType = [this](int linkType) {
            switch (linkType) {
            case 1:  return m_layer->m_pumpSym.outlineWidth;
            case 2:  return m_layer->m_orificeSym.outlineWidth;
            case 3:  return m_layer->m_weirSym.outlineWidth;
            case 4:  return m_layer->m_conduitSym.outlineWidth;
            default: return m_layer->m_conduitSym.outlineWidth;
            }
        };

        // Bucket by link type: 0=Conduit, 1=Pump, 2=Orifice, 3=Weir, 4=Outlet.
        std::array<QVector<QLineF>, 5> segsByType;
        std::array<QVector<QLineF>, 5> selSegsByType;

        // Phase A.3: consume the flat link scene-coord buffer. One
        // contiguous std::vector<float> of (x, y) pairs, with per-link
        // (offset, count) parallel arrays. Cache-friendly, and the
        // exact buffer the GL pipeline (Phase B) will hand to a VBO.
        const float    *flat    = m_layer->m_linkSceneFlat.data();
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
            auto &target = sel ? selSegsByType[size_t(type)] : segsByType[size_t(type)];
            const float *p = flat + size_t(off) * 2;
            for (uint32_t j = 1; j < cnt; ++j) {
                target.emplace_back(QPointF(p[(j - 1) * 2], p[(j - 1) * 2 + 1]),
                                    QPointF(p[ j      * 2], p[ j      * 2 + 1]));
            }
        }

        painter->setBrush(Qt::NoBrush);
        for (int t = 0; t < 5; ++t) {
            if (segsByType[size_t(t)].isEmpty()) continue;
            QPen pen(linkColorForType(t), linkWidthForType(t));
            pen.setCosmetic(true);
            painter->setPen(pen);
            painter->drawLines(segsByType[size_t(t)]);
        }

        // Selected-link highlight pass — yellow, slightly thicker than base.
        for (int t = 0; t < 5; ++t) {
            if (selSegsByType[size_t(t)].isEmpty()) continue;
            QPen hi(Qt::yellow, linkWidthForType(t) + 2.0);
            hi.setCosmetic(true);
            painter->setPen(hi);
            painter->drawLines(selSegsByType[size_t(t)]);
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
        // — all junctions draw together, then outfalls, etc.
        struct Bucket {
            const SWMMElementSymbol *sym;
            int                      nodeType;
            QVector<QPointF>         scenePts;
            QVector<QPointF>         selPts;
        };
        Bucket buckets[4] = {
            {&m_layer->m_junctionSym, 0, {}, {}},
            {&m_layer->m_outfallSym,  1, {}, {}},
            {&m_layer->m_storageSym,  2, {}, {}},
            {&m_layer->m_dividerSym,  3, {}, {}},
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
            (sel ? b.selPts : b.scenePts).append(sp);
        }

        for (const Bucket &b : buckets) {
            if (b.scenePts.isEmpty() && b.selPts.isEmpty()) continue;
            // Fixed pixel-size glyphs — see the invViewScale comment at
            // the top of paint(). markerRadius returns PIXELS, not scene
            // units, so we scale into scene space for each glyph below.
            const double r = markerRadius(*b.sym) * invViewScale;

            // Base pass.
            if (!b.scenePts.isEmpty()) {
                QPen pen(b.sym->outlineColor, b.sym->outlineWidth);
                pen.setCosmetic(true);
                painter->setPen(pen);
                painter->setBrush(QBrush(b.sym->fillColor));
                for (const QPointF &c : b.scenePts)
                    drawNodeGlyph(painter, c, r, b.nodeType);
            }

            // Selection-highlight pass (yellow fill).
            if (!b.selPts.isEmpty()) {
                QPen pen(b.sym->outlineColor, b.sym->outlineWidth + 1.0);
                pen.setCosmetic(true);
                painter->setPen(pen);
                painter->setBrush(QBrush(Qt::yellow));
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
            (sel ? selPts : basePts).append(sp);
        }

        if (!basePts.isEmpty()) {
            QPen pen(sym.outlineColor, sym.outlineWidth);
            pen.setCosmetic(true);
            painter->setPen(pen);
            painter->setBrush(QBrush(sym.fillColor));
            for (const QPointF &sp : basePts)
                drawNodeGlyph(painter, sp, r, /*diamond*/3);
        }
        if (!selPts.isEmpty()) {
            QPen pen(sym.outlineColor, sym.outlineWidth + 1.0);
            pen.setCosmetic(true);
            painter->setPen(pen);
            painter->setBrush(QBrush(Qt::yellow));
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
