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

namespace {
constexpr int kNodeOutfall = 1;   // SWMM_NODE_OUTFALL
constexpr int kNodeStorage = 2;   // SWMM_NODE_STORAGE
constexpr int kNodeDivider = 3;   // SWMM_NODE_DIVIDER
}

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

bool OpenSWMMVisMapToolAddNode::kindCanSplitConduit() const
{
    return m_nodeType != kNodeOutfall;
}

QString OpenSWMMVisMapToolAddNode::kindLabel() const
{
    switch (m_nodeType) {
    case kNodeOutfall: return tr("outfall");
    case kNodeStorage: return tr("storage node");
    case kNodeDivider: return tr("flow divider");
    default:           return tr("junction");
    }
}

void OpenSWMMVisMapToolAddNode::activate()
{
    m_snap  = {};
    m_hover = {};
    m_armed = false;
    OpenSWMMVisMapTool::activate();
    if (kindCanSplitConduit())
        emit statusMessageChanged(
            tr("Click to place a %1, or click a conduit to split it and insert "
               "the %1 there.").arg(kindLabel()));
    else
        emit statusMessageChanged(
            tr("Click to place an outfall. Outfalls end a network, so they "
               "cannot be inserted on a conduit."));
}

void OpenSWMMVisMapToolAddNode::deactivate()
{
    m_snap  = {};
    m_hover = {};
    m_armed = false;
    emit statusMessageChanged(QString());
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
    return ConduitSplitPick::nextNodeName(layer, m_elementKind);
}

void OpenSWMMVisMapToolAddNode::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    m_snap = SnapEngine::snap(this, activeModelLayer(), mx, my);
    // A snapped existing node wins over the conduit under it: the user is
    // targeting that node, not a break in the pipe.
    const bool onNode = m_snap.snapped
        && (m_snap.kind == SnapEngine::Kind::Node || m_snap.kind == SnapEngine::Kind::Gage);
    m_hover = onNode ? ConduitSplitPick::ConduitHit{}
                     : ConduitSplitPick::pickConduit(m_canvas, event->pos());
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
    // Arm on press, commit on release — never start an edit (or anything
    // that could show a dialog) while the button is still down.
    m_armed = (event->button() == Qt::LeftButton) && m_canvas;
}

void OpenSWMMVisMapToolAddNode::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_armed || !m_canvas)
        return;
    m_armed = false;

    SWMMModelLayer *layer = activeModelLayer();
    if (!layer)
        return;

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    // Resolve at the release point (a press-drag lands where the button came
    // up), with the same node-beats-conduit rule the hover uses.
    m_snap = SnapEngine::snap(this, layer, mx, my);
    const bool onNode = m_snap.snapped
        && (m_snap.kind == SnapEngine::Kind::Node || m_snap.kind == SnapEngine::Kind::Gage);
    const ConduitSplitPick::ConduitHit hit =
        onNode ? ConduitSplitPick::ConduitHit{}
               : ConduitSplitPick::pickConduit(m_canvas, event->pos());

    if (hit.valid()) {
        if (!kindCanSplitConduit()) {
            emit statusMessageChanged(
                tr("An outfall must end the network, so it cannot be inserted on "
                   "\"%1\" — click a free location instead.").arg(hit.name));
            return;   // no edit; the hover marker keeps showing the refusal
        }
        commitSplit(hit);
    } else {
        commitFreePlacement(layer, mx, my);
    }

    m_snap  = {};
    m_hover = {};
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("addnode-commit"));
}

void OpenSWMMVisMapToolAddNode::commitFreePlacement(SWMMModelLayer *layer,
                                                    double mx, double my)
{
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

    layer->setSelectedElements({{name, SWMMModelLayer::kKindNode}});
    emit nodeAdded(name, m_nodeType, px, py);
}

void OpenSWMMVisMapToolAddNode::commitSplit(const ConduitSplitPick::ConduitHit &hit)
{
    SWMMModelLayer *layer = hit.layer;
    const QString nodeName = nextAvailableName(layer);
    const QString linkName = ConduitSplitPick::nextSplitLinkName(layer, hit.name);

    auto *cmd = new InsertNodeSplitCommand(layer, hit.name, hit.t,
                                           nodeName, linkName, m_nodeType, m_canvas);
    QStringList warnings;
    if (m_canvas->undoStack()) {
        m_canvas->undoStack()->push(cmd);   // runs redo()
        warnings = cmd->warnings();
    } else {
        delete cmd;
    }

    if (layer->nodeIndex(nodeName) < 0) {
        emit statusMessageChanged(tr("Could not split \"%1\".").arg(hit.name));
        return;
    }

    layer->setSelectedElements({{nodeName, SWMMModelLayer::kKindNode}});
    emit nodeInsertedOnConduit(nodeName, hit.name, linkName);
    double x = hit.point.x(), y = hit.point.y();
    layer->cachedNodeCoord(layer->nodeIndex(nodeName), &x, &y);
    emit nodeAdded(nodeName, m_nodeType, x, y);

    QString msg = tr("Inserted %1 \"%2\", splitting \"%3\" into \"%3\" and \"%4\".")
                      .arg(kindLabel(), nodeName, hit.name, linkName);
    if (!warnings.isEmpty())
        msg += QStringLiteral(" ") + warnings.join(QStringLiteral(" "));
    emit statusMessageChanged(msg);
}

QColor OpenSWMMVisMapToolAddNode::markerColor(const SWMMModelLayer *layer) const
{
    switch (m_nodeType) {
    case kNodeOutfall: return layer->outfallSymbol().fillColor;
    case kNodeStorage: return layer->storageSymbol().fillColor;
    case kNodeDivider: return layer->dividerSymbol().fillColor;
    default:           return layer->junctionSymbol().fillColor;
    }
}

void OpenSWMMVisMapToolAddNode::paint(QPainter *painter, const MapExtent &,
                                       const SpatialReferenceSystem *)
{
    SnapEngine::paintSnapRing(painter, this, m_snap);
    if (!painter || !m_hover.valid()) return;

    // Split-point preview on the conduit under the cursor (layer CRS →
    // pixels via the canvas transform). Solid ring + crosshair when the click
    // will split; a dotted ring alone when the kind is refused on a conduit.
    double cx = m_hover.point.x(), cy = m_hover.point.y();
    m_hover.layer->transformLayerToCanvas(m_hover.point.x(), m_hover.point.y(),
                                          cx, cy);
    int px = 0, py = 0;
    toPixelCoords(cx, cy, px, py);

    const bool splits = kindCanSplitConduit();
    painter->save();
    QPen pen(markerColor(m_hover.layer));
    pen.setWidth(2);
    pen.setStyle(splits ? Qt::SolidLine : Qt::DotLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPoint(px, py), 7, 7);
    if (splits) {
        painter->drawLine(px - 10, py, px + 10, py);
        painter->drawLine(px, py - 10, px, py + 10);
    }
    painter->restore();
}
