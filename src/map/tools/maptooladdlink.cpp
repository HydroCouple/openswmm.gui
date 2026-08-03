/*!
 * \file   maptooladdlink.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/tools/maptooladdlink.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/mapextent.h"
#include "map/snapengine.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "core/preferencesmanager.h"

#include <openswmm/engine/openswmm_links.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

OpenSWMMVisMapToolAddLink::OpenSWMMVisMapToolAddLink(MapCanvas *canvas,
                                                      int linkType,
                                                      const QString &elementKind,
                                                      QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Link"), canvas, parent)
    , m_linkType(linkType)
    , m_elementKind(elementKind)
{
}

QCursor OpenSWMMVisMapToolAddLink::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolAddLink::activate()
{
    cancel();
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolAddLink::deactivate()
{
    cancel();
    OpenSWMMVisMapTool::deactivate();
}

SWMMModelLayer *OpenSWMMVisMapToolAddLink::activeModelLayer() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers())
        if (auto *ml = qobject_cast<SWMMModelLayer *>(l)) return ml;
    return nullptr;
}

QString OpenSWMMVisMapToolAddLink::nextAvailableName(SWMMModelLayer *layer) const
{
    const QString prefix =
        PreferencesManager::instance()->elementNamePrefix(m_elementKind);
    for (int n = 1; n < 100000; ++n) {
        const QString candidate = prefix + QString::number(n);
        if (layer->linkIndex(candidate) < 0)
            return candidate;
    }
    return prefix + QStringLiteral("_X");
}

QString OpenSWMMVisMapToolAddLink::snapToNode(SWMMModelLayer *layer,
                                               double mapX, double mapY,
                                               double *snapX, double *snapY) const
{
    // Build a small map-space tolerance box from the preference snap radius.
    const int snapPx = PreferencesManager::instance()->snapTolerancePx();
    double mx2, my2;
    toMapCoords(snapPx, snapPx, mx2, my2);
    double mx0, my0;
    toMapCoords(0, 0, mx0, my0);
    const double tolX = std::abs(mx2 - mx0);
    const double tolY = std::abs(my2 - my0);
    const double tol  = std::max(tolX, tolY);

    // identifyAt returns the nearest feature within tol.
    const QVariantMap hit = layer->identifyAt(mapX, mapY, nullptr, tol);
    const QString type = hit.value(QStringLiteral("elementType")).toString();
    if (type != QStringLiteral("Node")) return {};

    const QString name = hit.value(QStringLiteral("elementName")).toString();
    if (name.isEmpty()) return {};

    const int idx = layer->nodeIndex(name);
    if (idx < 0) return {};

    // Virtual junctions accept exactly two conduits, both fixed by the split
    // that created them — never a drawn endpoint (non-conduits are outright
    // illegal there, and a third conduit would violate the pair rule).
    if (layer->nodeIsVirtual(idx)) return {};

    // cachedNodeCoord returns layer-CRS coords; the caller compares
    // these against (mapX, mapY) which are canvas-CRS, and feeds them
    // into rubber-band paint code that uses toPixelCoords (canvas→
    // pixel). Round-trip via transformLayerToCanvas so the snapped
    // point is in canvas CRS regardless of whether the layer is
    // re-projected against a basemap.
    double nx = 0, ny = 0;
    layer->cachedNodeCoord(idx, &nx, &ny);
    double cnx = nx, cny = ny;
    layer->transformLayerToCanvas(nx, ny, cnx, cny);
    if (snapX) *snapX = cnx;
    if (snapY) *snapY = cny;
    return name;
}

void OpenSWMMVisMapToolAddLink::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    SWMMModelLayer *layer = activeModelLayer();
    if (!layer) return;

    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    if (event->button() == Qt::RightButton) {
        if (m_state == State::Drawing && !m_vertices.isEmpty()) {
            m_vertices.removeLast();
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-rubber"));
        } else {
            cancel();
        }
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    double sx = mx, sy = my;
    const QString snapName = snapToNode(layer, mx, my, &sx, &sy);

    if (m_state == State::Idle) {
        if (snapName.isEmpty()) return; // must start on a node
        m_fromNode   = snapName;
        m_fromPt     = QPointF(sx, sy);
        m_cursor     = QPointF(sx, sy); // prevent flash to (0,0) before first mouse-move
        m_snapPt     = m_fromPt;
        m_state      = State::Drawing;
        m_vertices.clear();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-rubber"));
        return;
    }

    // State::Drawing
    if (!snapName.isEmpty() && snapName != m_fromNode) {
        // Commit: click landed on a valid to-node.
        commit(layer, snapName, sx, sy);
        return;
    }
    if (!snapName.isEmpty()) return; // clicked same node — ignore

    // Add an intermediate vertex — snap if a vertex candidate is available.
    // SnapEngine returns the snapped point in layer CRS; m_vertices is kept
    // in canvas CRS so the rubber-band paint (toPixelCoords) stays aligned.
    // commit() converts the full polyline to layer CRS before storing.
    double vx = mx, vy = my;
    if (m_vertexSnap.snapped)
        layer->transformLayerToCanvas(m_vertexSnap.x, m_vertexSnap.y, vx, vy);
    m_vertices << QPointF(vx, vy);
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-rubber"));
}

void OpenSWMMVisMapToolAddLink::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas || m_state == State::Idle) return;
    SWMMModelLayer *layer = activeModelLayer();

    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    m_cursor = QPointF(mx, my);

    // Node snap detection (for end-node commitment — node-only, required).
    double sx = mx, sy = my;
    if (layer) {
        const QString snap = snapToNode(layer, mx, my, &sx, &sy);
        m_snapTarget = snap;
        m_snapPt     = snap.isEmpty() ? QPointF(mx, my) : QPointF(sx, sy);
    }

    // General vertex snap for the rubber-band tip (only when NOT targeting an end-node).
    if (m_snapTarget.isEmpty() && layer)
        m_vertexSnap = SnapEngine::snap(this, layer, mx, my);
    else
        m_vertexSnap = {};

    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-rubber"));
}

void OpenSWMMVisMapToolAddLink::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    // The second press of the double-click already fired through
    // mousePressEvent and may have added an intermediate vertex (or
    // chained into a new link). Drop any stray trailing vertex and
    // fully exit the drawing flow.
    if (m_state == State::Drawing && !m_vertices.isEmpty())
        m_vertices.removeLast();
    cancel();
}

void OpenSWMMVisMapToolAddLink::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) cancel();
}

void OpenSWMMVisMapToolAddLink::paint(QPainter *painter,
                                       const MapExtent &,
                                       const SpatialReferenceSystem *)
{
    if (m_state == State::Idle || !m_canvas) return;

    painter->save();

    // Build pixel coords for the full polyline: from-node → vertices → cursor.
    auto toPixel = [&](QPointF pt) -> QPoint {
        int px = 0, py = 0;
        toPixelCoords(pt.x(), pt.y(), px, py);
        return QPoint(px, py);
    };

    const QPoint fromPx = toPixel(m_fromPt);
    // Rubber-band tip: node snap takes priority; then general vertex snap; then raw cursor.
    QPoint snapPx;
    if (!m_snapTarget.isEmpty())
        snapPx = toPixel(m_snapPt);
    else if (m_vertexSnap.snapped)
        snapPx = toPixel(QPointF(m_vertexSnap.x, m_vertexSnap.y));
    else
        snapPx = toPixel(m_cursor);

    QVector<QPoint> pts;
    pts << fromPx;
    for (const QPointF &v : m_vertices)
        pts << toPixel(v);
    pts << snapPx;

    // Rubber-band line.
    painter->setPen(QPen(QColor(0, 120, 200), 2, Qt::DashLine));
    for (int i = 0; i + 1 < pts.size(); ++i)
        painter->drawLine(pts[i], pts[i + 1]);

    // Intermediate vertex dots.
    painter->setBrush(QColor(0, 120, 200));
    painter->setPen(Qt::NoPen);
    for (int i = 1; i + 1 < pts.size(); ++i)
        painter->drawEllipse(pts[i], 4, 4);

    // Snap ring: orange for node snap, cyan for vertex snap.
    if (!m_snapTarget.isEmpty()) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 165, 0), 2));
        painter->drawEllipse(snapPx, 9, 9);
    } else if (m_vertexSnap.snapped) {
        painter->restore();
        SnapEngine::paintSnapRing(painter, this, m_vertexSnap, QColor(0, 200, 220));
        painter->save();
    }

    // From-node anchor dot.
    painter->setBrush(QColor(0, 200, 0));
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(fromPx, 5, 5);

    painter->restore();
}

void OpenSWMMVisMapToolAddLink::cancel()
{
    m_state      = State::Idle;
    m_fromNode.clear();
    m_vertices.clear();
    m_snapTarget.clear();
    m_vertexSnap = {};
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-cancel"));
}

void OpenSWMMVisMapToolAddLink::setTerrain(GISRasterLayer *layer, double offset, double factor)
{
    m_terrainLayer  = layer;
    m_terrainOffset = offset;
    m_terrainFactor = factor;
}

void OpenSWMMVisMapToolAddLink::commit(SWMMModelLayer *layer,
                                        const QString  &toNodeName,
                                        double toCanvasX, double toCanvasY)
{
    // Estimate invert offsets from terrain at each endpoint. Both endpoints
    // are read in layer CRS — m_fromPt is canvas CRS (set by snapToNode
    // round-trip in mousePressEvent) so convert it back, and cachedNodeCoord
    // already returns layer CRS. Pass layer->srs() so the raster lookup
    // re-projects to the DTM's native CRS internally.
    double offsetUp = 0.0, offsetDn = 0.0;
    if (m_terrainLayer) {
        double fromLX = m_fromPt.x(), fromLY = m_fromPt.y();
        layer->transformCanvasToLayer(m_fromPt.x(), m_fromPt.y(), fromLX, fromLY);

        bool okFrom = false;
        const double zFrom = m_terrainLayer->valueAt(fromLX, fromLY,
                                                      layer->srs(), 1, &okFrom);
        if (okFrom)
            offsetUp = zFrom * m_terrainFactor + m_terrainOffset;

        // Resolve to-node coordinates from the layer geometry cache.
        const int toIdx = layer->nodeIndex(toNodeName);
        if (toIdx >= 0) {
            double toX = 0.0, toY = 0.0;
            layer->cachedNodeCoord(toIdx, &toX, &toY);
            bool okTo = false;
            const double zTo = m_terrainLayer->valueAt(toX, toY,
                                                       layer->srs(), 1, &okTo);
            if (okTo)
                offsetDn = zTo * m_terrainFactor + m_terrainOffset;
        }
    }

    // m_vertices accumulates in canvas CRS so the rubber-band paint stays
    // aligned with the mouse; the engine + SoA store want layer CRS, so
    // convert once here. When the layer and canvas CRSes coincide this is
    // a no-op (transformCanvasToLayer short-circuits when m_transform is
    // null).
    QVector<QPointF> layerVertices;
    layerVertices.reserve(m_vertices.size());
    for (const QPointF &v : m_vertices) {
        double lx = v.x(), ly = v.y();
        layer->transformCanvasToLayer(v.x(), v.y(), lx, ly);
        layerVertices.append(QPointF(lx, ly));
    }

    const QString name = nextAvailableName(layer);
    auto *cmd = new AddLinkCommand(layer, name, m_linkType,
                                    m_fromNode, toNodeName,
                                    layerVertices, m_canvas,
                                    offsetUp, offsetDn);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    layer->setSelectedElements({{name, SWMMModelLayer::kKindLink}});
    emit linkAdded(name, m_linkType, m_fromNode, toNodeName);

    // Chain into a new link starting at the just-committed end node so the
    // user can draw a connected polyline without re-arming the tool. Right-
    // click / Escape still routes to cancel() for a full exit.
    m_fromNode   = toNodeName;
    m_fromPt     = QPointF(toCanvasX, toCanvasY);
    m_cursor     = m_fromPt;
    m_snapPt     = m_fromPt;
    m_vertices.clear();
    m_snapTarget.clear();
    m_vertexSnap = {};
    m_state      = State::Drawing;
    if (m_canvas)
        // Scene, not Overlay alone: a link was just added to the model, so
        // layer content changed. Overlay only repaints the widget from the
        // cached scene/QSG buffers, which is why the new link stayed invisible
        // until a zoom changed the extent and forced both caches to miss.
        // Matches OpenSWMMVisMapToolAddNode's channel choice.
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("addlink-chain"));
}
