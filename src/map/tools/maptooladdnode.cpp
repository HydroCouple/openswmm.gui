/*!
 * \file   maptooladdnode.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptooladdnode.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "core/preferencesmanager.h"

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

void OpenSWMMVisMapToolAddNode::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas)
        return;

    SWMMModelLayer *layer = activeModelLayer();
    if (!layer)
        return;

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    const QString name = nextAvailableName(layer);

    auto *cmd = new AddNodeCommand(layer, name, m_nodeType, mx, my, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    emit nodeAdded(name, m_nodeType, mx, my);

    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("addnode-commit"));
}
