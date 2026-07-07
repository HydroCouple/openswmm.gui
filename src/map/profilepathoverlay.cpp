/*!
 * \file   profilepathoverlay.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/profilepathoverlay.h"

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "render/categoricalpalette.h"

#include <QBrush>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>

namespace
{

// Scene-space Y-flip — matches SWMMLayerItem convention.
inline QPointF toScene(double mx, double my) { return QPointF(mx, -my); }

constexpr qreal kBaseStrokePx       = 6.0;
constexpr qreal kDimAlpha           = 0.35;   // dimmed candidates
constexpr qreal kPromotedAlpha      = 1.00;
constexpr qreal kEqualAlpha         = 0.75;   // no path highlighted

// Z-values so the overlay sits firmly above every map layer.  MapCanvas
// assigns layer z as `i * 1000.0` (see mapcanvas.cpp updateLayerZValues),
// so an overlay band starting at 1e7 leaves 4 orders of magnitude of
// headroom — large enough that no realistic layer stack can collide.
// Halos sit above the path lines so endpoints remain visible when paths
// overlap them.  Transparency on the pen colours (kDimAlpha/kEqualAlpha)
// lets the user still see whatever sits underneath.
constexpr qreal kZBase        = 1.0e7;
constexpr qreal kZDimmed      = kZBase + 0.0;
constexpr qreal kZHighlighted = kZBase + 10.0;
constexpr qreal kZHalo        = kZBase + 20.0;

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

    // Halo geometry + style is sourced from PreferencesManager every time
    // the endpoints change so users see edits land immediately.  We use
    // ItemIgnoresTransformations so the ellipse stays at a constant
    // pixel-size regardless of map zoom — its position is in scene coords
    // (the node's map XY) but its rect is in untransformed view units.
    auto *prefs = PreferencesManager::instance();
    const int   r   = prefs->profileEndpointHaloRadiusPx();
    const QPen  startPen = prefs->profileStartEndpointPen();
    const QPen  endPen   = prefs->profileEndEndpointPen();

    auto place = [this, r](QGraphicsEllipseItem *&halo,
                           int engNodeIdx,
                           const QPen &pen) {
        if (engNodeIdx < 0) {
            if (halo) { delete halo; halo = nullptr; }
            return;
        }
        const QPointF p = sceneCoordForNode(engNodeIdx);
        if (!halo) {
            halo = new QGraphicsEllipseItem(this);
            halo->setBrush(Qt::NoBrush);
            halo->setZValue(kZHalo);
            halo->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        }
        QPen p2 = pen;
        p2.setCosmetic(true);
        halo->setPen(p2);
        // With ItemIgnoresTransformations the item's rect is in pixels and
        // its setPos() is in scene coords.  Centre the rect on (0,0) and
        // position the item at the node's scene point.
        halo->setRect(-r, -r, 2 * r, 2 * r);
        halo->setPos(p);
    };
    place(m_startHalo, m_startEngineNodeIdx, startPen);
    place(m_endHalo,   m_endEngineNodeIdx,   endPen);
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

void ProfilePathOverlay::paintOverlay(QPainter &p, const SceneToPixel &toPixel) const
{
    if (!m_model) return;

    auto pixelPathFor = [this, &toPixel](const ProfileRouter::Path &rp) {
        QPainterPath painterPath;
        bool first = true;
        for (int edgeIdx = 0; edgeIdx < rp.linkIds.size(); ++edgeIdx) {
            QVector<QPointF> poly = sceneCoordsForLink(rp.linkIds[edgeIdx]);
            if (poly.isEmpty()) continue;

            if (edgeIdx + 1 < rp.nodes.size()) {
                const QPointF upstreamScene = sceneCoordForNode(rp.nodes[edgeIdx]);
                const QPointF d0 = poly.first() - upstreamScene;
                const QPointF dN = poly.last()  - upstreamScene;
                if (QPointF::dotProduct(d0, d0) > QPointF::dotProduct(dN, dN))
                    std::reverse(poly.begin(), poly.end());
            }

            if (first) {
                painterPath.moveTo(toPixel(poly.first()));
                first = false;
            } else {
                painterPath.lineTo(toPixel(poly.first()));
            }
            for (int v = 1; v < poly.size(); ++v)
                painterPath.lineTo(toPixel(poly[v]));
        }
        return painterPath;
    };

    auto drawPath = [&p, this, &pixelPathFor](int idx) {
        if (idx < 0 || idx >= m_paths.size()) return;
        qreal alpha = kEqualAlpha;
        qreal width = kBaseStrokePx;
        if (m_highlighted >= 0) {
            if (idx == m_highlighted) {
                alpha = kPromotedAlpha;
                width = kBaseStrokePx + 1.5;
            } else {
                alpha = kDimAlpha;
            }
        }

        const QPainterPath path = pixelPathFor(m_paths[idx]);
        if (path.isEmpty()) return;

        QColor casing(0xFF, 0xFF, 0xFF);
        casing.setAlphaF(std::min<qreal>(1.0, alpha + 0.20));
        QPen casingPen(casing);
        casingPen.setWidthF(width + 3.0);
        casingPen.setJoinStyle(Qt::RoundJoin);
        casingPen.setCapStyle(Qt::RoundCap);

        QColor c = CategoricalPalette::at(idx);
        c.setAlphaF(alpha);
        QPen colorPen(c);
        colorPen.setWidthF(width);
        colorPen.setJoinStyle(Qt::RoundJoin);
        colorPen.setCapStyle(Qt::RoundCap);

        p.setBrush(Qt::NoBrush);
        p.setPen(casingPen);
        p.drawPath(path);
        p.setPen(colorPen);
        p.drawPath(path);
    };

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < m_paths.size(); ++i) {
        if (i == m_highlighted) continue;
        drawPath(i);
    }
    if (m_highlighted >= 0)
        drawPath(m_highlighted);

    auto drawHalo = [this, &p, &toPixel](int engNodeIdx, const QPen &pen) {
        if (engNodeIdx < 0) return;
        const QPointF centre = toPixel(sceneCoordForNode(engNodeIdx));
        auto *prefs = PreferencesManager::instance();
        const qreal r = prefs ? prefs->profileEndpointHaloRadiusPx() : 11.0;
        QPen haloPen = pen;
        haloPen.setCosmetic(false);
        p.setBrush(Qt::NoBrush);
        p.setPen(haloPen);
        p.drawEllipse(centre, r, r);
    };

    if (auto *prefs = PreferencesManager::instance()) {
        drawHalo(m_startEngineNodeIdx, prefs->profileStartEndpointPen());
        drawHalo(m_endEngineNodeIdx,   prefs->profileEndEndpointPen());
    }

    p.restore();
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
