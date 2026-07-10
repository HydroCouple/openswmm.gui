/*!
 * \file   maptooladdgage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/tools/maptooladdgage.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_gages.h>

#include <QMouseEvent>

OpenSWMMVisMapToolAddGage::OpenSWMMVisMapToolAddGage(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Rain Gage"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolAddGage::cursor() const { return Qt::CrossCursor; }

SWMMModelLayer *OpenSWMMVisMapToolAddGage::activeModelLayer() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers())
        if (auto *ml = qobject_cast<SWMMModelLayer *>(l)) return ml;
    return nullptr;
}

QString OpenSWMMVisMapToolAddGage::nextAvailableName(SWMMModelLayer *layer) const
{
    for (int n = 1; n < 100000; ++n) {
        const QString candidate = QStringLiteral("RG") + QString::number(n);
        const QByteArray utf8 = candidate.toUtf8();
        if (swmm_gage_index(layer->engine(), utf8.constData()) < 0)
            return candidate;
    }
    return QStringLiteral("RG_X");
}

void OpenSWMMVisMapToolAddGage::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas) return;
    SWMMModelLayer *layer = activeModelLayer();
    if (!layer) return;

    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    // toMapCoords returns canvas-CRS coords; the SoA store / engine
    // expect layer CRS so the renderer's m_transform doesn't double-
    // project the gage off-screen when the canvas CRS differs from
    // the layer CRS (typical with basemaps forcing Web Mercator).
    double px = mx, py = my;
    layer->transformCanvasToLayer(mx, my, px, py);

    const QString name = nextAvailableName(layer);

    auto *cmd = new AddGageCommand(layer, name, px, py, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    layer->setSelectedElements({{name, SWMMModelLayer::kKindGage}});
    emit gageAdded(name, px, py);
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                          QStringLiteral("addgage-commit"));
}
