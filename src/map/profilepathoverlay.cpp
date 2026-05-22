/*!
 * \file   profilepathoverlay.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/profilepathoverlay.h"

#include "layers/swmmmodellayer.h"
#include "render/categoricalpalette.h"

#include <QBrush>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPen>

namespace
{

// Scene-space Y-flip — matches SWMMLayerItem convention.
inline QPointF toScene(double mx, double my) { return QPointF(mx, -my); }

constexpr qreal kBaseStrokePx       = 6.0;
constexpr qreal kDimAlpha           = 0.35;   // dimmed candidates
constexpr qreal kPromotedAlpha      = 1.00;
constexpr qreal kEqualAlpha         = 0.75;   // no path highlighted
constexpr qreal kHaloRadiusPx       = 28.0;   // doubled for visibility
constexpr qreal kHaloPenWidthPx     = 3.5;

// Z-values so the overlay sits firmly above the SWMM map items (which
// typically render in the 0-500 range).  Halos above the path lines so
// endpoints remain visible when paths overlap them.
constexpr qreal kZDimmed      = 10000.0;
constexpr qreal kZHighlighted = 10010.0;
constexpr qreal kZHalo        = 10020.0;

QPen makePathPen(const QColor &baseColor, qreal alpha, qreal width)
{
    QColor c = baseColor;
    c.setAlphaF(alpha);
    QPen p(c);
    p.setWidthF(width);
    p.setCosmetic(true);            // constant width regardless of zoom
    p.setJoinStyle(Qt::RoundJoin);
    p.setCapStyle(Qt::RoundCap);
    return p;
}

} // namespace

ProfilePathOverlay::ProfilePathOverlay(SWMMModelLayer *model,
                                       QGraphicsItem *parent)
    : QGraphicsItemGroup(parent),
      m_model(model)
{
    setHandlesChildEvents(false);
    setAcceptedMouseButtons(Qt::NoButton);  // overlay is non-interactive
    setZValue(kZDimmed);
}

void ProfilePathOverlay::setPaths(const QVector<ProfileRouter::Path> &paths)
{
    m_paths = paths;
    m_highlighted = -1;
    rebuild();
}

void ProfilePathOverlay::setHighlightedPath(int index)
{
    if (index < -1) index = -1;
    if (index >= m_paths.size()) index = -1;
    if (m_highlighted == index) return;
    m_highlighted = index;
    applyHighlightStyling();
}

void ProfilePathOverlay::setEndpoints(int startEngineNodeIdx, int endEngineNodeIdx)
{
    m_startEngineNodeIdx = startEngineNodeIdx;
    m_endEngineNodeIdx   = endEngineNodeIdx;
    // Re-position halos.
    auto place = [this](QGraphicsEllipseItem *&halo, int engNodeIdx, const QColor &color) {
        if (engNodeIdx < 0) {
            if (halo) { delete halo; halo = nullptr; }
            return;
        }
        const QPointF p = sceneCoordForNode(engNodeIdx);
        if (!halo) {
            halo = new QGraphicsEllipseItem(this);
            halo->setBrush(Qt::NoBrush);
            halo->setZValue(kZHalo);
        }
        QPen pen(color);
        pen.setWidthF(kHaloPenWidthPx);
        pen.setCosmetic(true);
        halo->setPen(pen);
        // Halo extent is cosmetic-style: anchor at the node, radius in scene
        // units derived from pixel target.  Since we draw cosmetic strokes
        // the ring scales with zoom; for v1 keep the radius in scene units
        // so it remains tied to map distance (callers can tune later).
        const qreal r = kHaloRadiusPx;
        halo->setRect(p.x() - r, p.y() - r, 2 * r, 2 * r);
    };
    place(m_startHalo, m_startEngineNodeIdx, QColor(0x2c, 0xa0, 0x2c));  // green
    place(m_endHalo,   m_endEngineNodeIdx,   QColor(0xd6, 0x27, 0x28));  // red
}

void ProfilePathOverlay::clear()
{
    m_paths.clear();
    m_highlighted = -1;
    rebuild();
    if (m_startHalo) { delete m_startHalo; m_startHalo = nullptr; }
    if (m_endHalo)   { delete m_endHalo;   m_endHalo   = nullptr; }
}

int ProfilePathOverlay::pathCount() const
{
    return m_paths.size();
}

QColor ProfilePathOverlay::colorForPath(int index) const
{
    if (index < 0 || index >= m_paths.size()) return Qt::gray;
    return CategoricalPalette::at(index);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ProfilePathOverlay::rebuild()
{
    // Tear down existing path items.
    for (QGraphicsPathItem *item : m_pathItems) {
        if (item) delete item;
    }
    m_pathItems.clear();

    if (!m_model) return;

    // Build a QPainterPath per candidate path by concatenating link polylines
    // in the router's traversal order.
    for (int pi = 0; pi < m_paths.size(); ++pi) {
        const ProfileRouter::Path &rp = m_paths[pi];
        QPainterPath painterPath;
        bool first = true;
        for (int edgeIdx = 0; edgeIdx < rp.linkIds.size(); ++edgeIdx) {
            const int engLinkIdx = rp.linkIds[edgeIdx];
            QVector<QPointF> poly = sceneCoordsForLink(engLinkIdx);
            if (poly.isEmpty()) continue;

            // Orient: poly may run from model.fromNode → model.toNode, which
            // could be opposite to the path's traversal (nodes[i] → nodes[i+1]).
            // We force orientation by comparing the first poly point to the
            // path-upstream node's scene coord and reversing if needed.
            if (edgeIdx + 1 < rp.nodes.size()) {
                const QPointF upstreamScene = sceneCoordForNode(rp.nodes[edgeIdx]);
                if (!poly.isEmpty()) {
                    const QPointF d0 = poly.first() - upstreamScene;
                    const QPointF dN = poly.last()  - upstreamScene;
                    if (QPointF::dotProduct(d0, d0) > QPointF::dotProduct(dN, dN)) {
                        std::reverse(poly.begin(), poly.end());
                    }
                }
            }

            if (first) {
                painterPath.moveTo(poly.first());
                first = false;
            }
            for (int v = 1; v < poly.size(); ++v)
                painterPath.lineTo(poly[v]);
        }

        auto *item = new QGraphicsPathItem(painterPath, this);
        item->setBrush(Qt::NoBrush);
        m_pathItems.push_back(item);
    }

    applyHighlightStyling();
}

void ProfilePathOverlay::applyHighlightStyling()
{
    for (int i = 0; i < m_pathItems.size(); ++i) {
        QGraphicsPathItem *item = m_pathItems[i];
        if (!item) continue;
        const QColor baseColor = CategoricalPalette::at(i);
        qreal alpha, width, z;
        if (m_highlighted < 0) {
            alpha = kEqualAlpha;
            width = kBaseStrokePx;
            z     = kZDimmed;
        } else if (i == m_highlighted) {
            alpha = kPromotedAlpha;
            width = kBaseStrokePx + 1.5;
            z     = kZHighlighted;
        } else {
            alpha = kDimAlpha;
            width = kBaseStrokePx;
            z     = kZDimmed;
        }
        item->setPen(makePathPen(baseColor, alpha, width));
        item->setZValue(z);
    }
}

QVector<QPointF> ProfilePathOverlay::sceneCoordsForLink(int engineLinkIdx) const
{
    QVector<QPointF> out;
    if (!m_model) return out;
    const QVector<QPointF> poly = m_model->cachedLinkPolyline(engineLinkIdx);
    out.reserve(poly.size());
    for (const QPointF &p : poly)
        out.push_back(toScene(p.x(), p.y()));
    return out;
}

QPointF ProfilePathOverlay::sceneCoordForNode(int engineNodeIdx) const
{
    if (!m_model) return QPointF();
    double x = 0.0, y = 0.0;
    if (!m_model->cachedNodeCoord(engineNodeIdx, &x, &y)) return QPointF();
    return toScene(x, y);
}
