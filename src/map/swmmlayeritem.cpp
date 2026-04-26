/*!
 * \file   swmmlayeritem.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/swmmlayeritem.h"

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"

#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>

#include <ogr_spatialref.h>   // OGRCoordinateTransformation

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

    painter->setOpacity(m_layer->opacity());

    const QRectF exposed = option->exposedRect;
    const QSet<QString> &hidden   = m_layer->m_hiddenObjects;
    const QStringList   &selected = m_layer->m_selectedNames;
    const QSet<QString> selectedSet(selected.begin(), selected.end());

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

    auto applyTransform = [this](double &x, double &y) {
        if (m_layer->m_transform)
            m_layer->m_transform->Transform(1, &x, &y);
    };

    // ---------------------------------------------------------------- Subcatchments
    if (m_layer->m_showSubcatchments)
    {
        const auto &sym = m_layer->m_subcatchSym;
        QPen pen(sym.outlineColor, sym.outlineWidth);
        pen.setCosmetic(true);
        pen.setCapStyle (Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);

        painter->setPen(pen);
        painter->setBrush(QBrush(sym.fillColor));

        for (const auto &c : m_layer->m_catchments)
        {
            if (hidden.contains(c.name)) continue;

            QPolygonF poly;
            poly.reserve(c.vertices.size());
            for (QPointF v : c.vertices) {
                double x = v.x(), y = v.y();
                applyTransform(x, y);
                poly << toScene(x, y);
            }
            if (poly.isEmpty()) continue;
            // Viewport cull: skip polygons whose bbox misses the tile.
            if (!exposed.isNull() && !exposed.intersects(poly.boundingRect()))
                continue;

            const bool sel = selectedSet.contains(c.name);
            if (sel) painter->setBrush(QColor(255, 255, 0, 120));
            painter->drawPolygon(poly);
            if (sel) painter->setBrush(QBrush(sym.fillColor));
        }
    }

    // ---------------------------------------------------------------- Links
    if (m_layer->m_showLinks)
    {
        // Single pen: the legacy path used m_conduitSym regardless of link
        // subtype ([swmmmodellayer.cpp: populateScene]). Matching that
        // here keeps visual parity; per-subtype buckets (pump / orifice /
        // weir) can be layered on later without changing the shape of
        // this loop.
        const auto &sym = m_layer->m_conduitSym;
        QPen pen(sym.fillColor, sym.outlineWidth);
        pen.setCosmetic(true);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(pen);

        // Pass 1: non-selected links in bulk via drawLines(). Collect the
        // full segment vector once; one native call paints all of them.
        QVector<QLineF> segs;
        segs.reserve(m_layer->m_links.size() * 2);

        // Pass 2 segments (selected) kept separate so they can render
        // with the highlight pen after.
        QVector<QLineF> selSegs;

        for (const auto &l : m_layer->m_links)
        {
            if (hidden.contains(l.name) || l.vertices.size() < 2)
                continue;

            // Transform the polyline once.
            QVector<QPointF> pts;
            pts.reserve(l.vertices.size());
            QRectF bbox;
            for (int i = 0; i < l.vertices.size(); ++i) {
                double x = l.vertices[i].x(), y = l.vertices[i].y();
                applyTransform(x, y);
                const QPointF sp = toScene(x, y);
                pts.append(sp);
                if (i == 0) bbox = QRectF(sp, QSizeF(0, 0));
                else {
                    if (sp.x() < bbox.left())   bbox.setLeft  (sp.x());
                    if (sp.x() > bbox.right())  bbox.setRight (sp.x());
                    if (sp.y() < bbox.top())    bbox.setTop   (sp.y());
                    if (sp.y() > bbox.bottom()) bbox.setBottom(sp.y());
                }
            }
            // Viewport cull.
            if (!exposed.isNull() && !exposed.intersects(bbox))
                continue;

            const bool sel = selectedSet.contains(l.name);
            auto &target = sel ? selSegs : segs;
            for (int i = 1; i < pts.size(); ++i)
                target.emplace_back(pts[i-1], pts[i]);
        }

        if (!segs.isEmpty())
            painter->drawLines(segs);

        // Selected-link highlight pass — yellow, slightly thicker pen.
        if (!selSegs.isEmpty()) {
            QPen hi(Qt::yellow, sym.outlineWidth + 2.0);
            hi.setCosmetic(true);
            painter->setPen(hi);
            painter->drawLines(selSegs);
            painter->setPen(pen);
        }
    }

    // ---------------------------------------------------------------- Nodes
    if (m_layer->m_showNodes)
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

        for (const auto &n : m_layer->m_nodes) {
            if (hidden.contains(n.name)) continue;
            double x = n.x, y = n.y; applyTransform(x, y);
            const QPointF sp = toScene(x, y);
            if (!exposed.isNull() && !exposed.contains(sp)) {
                QRectF e = exposed.adjusted(-haloScene, -haloScene,
                                             haloScene,  haloScene);
                if (!e.contains(sp)) continue;
            }
            const int t = (n.nodeType >= 0 && n.nodeType < 4) ? n.nodeType : 0;
            auto &b = buckets[t];
            (selectedSet.contains(n.name) ? b.selPts : b.scenePts).append(sp);
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
    if (m_layer->m_showRainGages && !m_layer->m_gages.isEmpty())
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
        for (const auto &g : m_layer->m_gages)
        {
            if (hidden.contains(g.name)) continue;
            double x = g.x, y = g.y; applyTransform(x, y);
            const QPointF sp = toScene(x, y);
            if (!exposed.isNull() && !exposed.contains(sp)) {
                QRectF e = exposed.adjusted(-haloScene, -haloScene,
                                             haloScene,  haloScene);
                if (!e.contains(sp)) continue;
            }
            (selectedSet.contains(g.name) ? selPts : basePts).append(sp);
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
            for (const auto &n : m_layer->m_nodes) {
                if (hidden.contains(n.name)) continue;
                double x = n.x, y = n.y; applyTransform(x, y);
                const QPointF sp = toScene(x, y);
                if (!exposed.isNull() && !exposed.contains(sp)) continue;
                painter->drawText(sp + QPointF(6, -4), n.name);
            }
        }
    }
}
