/*!
 * \file   maptooladdnode.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptooladdnode.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/snapengine.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "core/preferencesmanager.h"

#include <QPainter>
#include <QMouseEvent>

OpenSWMMVisMapToolAddNode::OpenSWMMVisMapToolAddNode(MapCanvas *canvas,
                                                     int nodeType,
                                                     const QString &elementKind,
                                                     QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Node"), canvas, parent),
      m_nodeType(nodeType),
      m_elementKind(elementKind)
{
}

QCursor OpenSWMMVisMapToolAddNode::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolAddNode::activate()
{
    m_snap = {};
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolAddNode::deactivate()
{
    m_snap = {};
    OpenSWMMVisMapTool::deactivate();
}

SWMMModelLayer *OpenSWMMVisMapToolAddNode::activeModelLayer() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *layer : m_canvas->layers())
    {
        if (auto *model = qobject_cast<SWMMModelLayer *>(layer))
            return model;
    }
    return nullptr;
}

QString OpenSWMMVisMapToolAddNode::nextAvailableName(SWMMModelLayer *layer) const
{
    const QString prefix =
        PreferencesManager::instance()->elementNamePrefix(m_elementKind);
    if (!layer) return prefix + QStringLiteral("1");
    for (int n = 1; n < 100000; ++n)
    {
        const QString candidate = prefix + QString::number(n);
        if (layer->nodeIndex(candidate) < 0)
            return candidate;
    }
    return prefix + QStringLiteral("_X");
}

void OpenSWMMVisMapToolAddNode::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    m_snap = SnapEngine::snap(this, activeModelLayer(), mx, my);
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addnode-rubber"));
}

void OpenSWMMVisMapToolAddNode::setTerrain(GISRasterLayer *layer, double offset, double factor)
{
    m_terrainLayer  = layer;
    m_terrainOffset = offset;
    m_terrainFactor = factor;
}

void OpenSWMMVisMapToolAddNode::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas)
        return;

    SWMMModelLayer *layer = activeModelLayer();
    if (!layer)
        return;

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    // Resolve the placement in the LAYER's CRS — m_nodes / the engine
    // store coords in layer CRS, and rebuildSceneCoords applies the
    // layer→canvas transform at render time. Without this conversion,
    // a click in a basemap-driven canvas (e.g. Web Mercator) would be
    // stored verbatim, then re-projected by the renderer and land far
    // off-screen. Snap results are already in layer CRS (sourced from
    // cachedNodeCoord / cachedLinkPolyline), so use them directly.
    double px = mx, py = my;
    if (m_snap.snapped) {
        px = m_snap.x;
        py = m_snap.y;
    } else {
        layer->transformCanvasToLayer(mx, my, px, py);
    }

    // Sample terrain Z — pass the layer's CRS as the input CRS so
    // valueAt re-projects to the raster's native CRS internally.
    double invertElev = 0.0;
    if (m_terrainLayer) {
        bool ok = false;
        const double z = m_terrainLayer->valueAt(px, py,
                                                  layer->srs(),
                                                  1, &ok);
        if (ok)
            invertElev = z * m_terrainFactor + m_terrainOffset;
    }

    const QString name = nextAvailableName(layer);

    auto *cmd = new AddNodeCommand(layer, name, m_nodeType, px, py,
                                   m_canvas, invertElev);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    layer->setSelectedElementNames({name});
    emit nodeAdded(name, m_nodeType, px, py);

    m_snap = {};
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("addnode-commit"));
}

void OpenSWMMVisMapToolAddNode::paint(QPainter *painter, const MapExtent &,
                                       const SpatialReferenceSystem *)
{
    SnapEngine::paintSnapRing(painter, this, m_snap);
}
