/*!
 * \file   maptooladdsubcatchment.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/tools/maptooladdsubcatchment.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_subcatchments.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

OpenSWMMVisMapToolAddSubcatchment::OpenSWMMVisMapToolAddSubcatchment(MapCanvas *canvas,
                                                                      QObject   *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Subcatchment"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolAddSubcatchment::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolAddSubcatchment::activate()
{
    cancel();
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolAddSubcatchment::deactivate()
{
    cancel();
    OpenSWMMVisMapTool::deactivate();
}

SWMMModelLayer *OpenSWMMVisMapToolAddSubcatchment::activeModelLayer() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers())
        if (auto *ml = qobject_cast<SWMMModelLayer *>(l)) return ml;
    return nullptr;
}

QString OpenSWMMVisMapToolAddSubcatchment::nextAvailableName(SWMMModelLayer *layer) const
{
    for (int n = 1; n < 100000; ++n) {
        const QString candidate = QStringLiteral("Sub") + QString::number(n);
        const QByteArray utf8 = candidate.toUtf8();
        if (swmm_subcatch_index(layer->engine(), utf8.constData()) < 0)
            return candidate;
    }
    return QStringLiteral("Sub_X");
}

void OpenSWMMVisMapToolAddSubcatchment::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas) return;

    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    if (event->button() == Qt::RightButton) {
        if (m_vertices.size() > 1)
            m_vertices.removeLast();
        else
            cancel();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addsubcatch-rubber"));
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    m_vertices << QPointF(mx, my);
    m_drawing = true;
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addsubcatch-rubber"));
}

void OpenSWMMVisMapToolAddSubcatchment::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_drawing || !m_canvas) return;
    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    m_cursor = QPointF(mx, my);
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addsubcatch-rubber"));
}

void OpenSWMMVisMapToolAddSubcatchment::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    // The single-click from the double-click already added a vertex — remove
    // the duplicate before committing.
    if (m_vertices.size() > 1)
        m_vertices.removeLast();
    commit();
}

void OpenSWMMVisMapToolAddSubcatchment::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancel();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        commit();
    }
}

void OpenSWMMVisMapToolAddSubcatchment::paint(QPainter *painter,
                                               const MapExtent &,
                                               const SpatialReferenceSystem *)
{
    if (!m_drawing || m_vertices.isEmpty()) return;

    painter->save();

    auto toPixel = [&](QPointF pt) -> QPoint {
        int px = 0, py = 0;
        toPixelCoords(pt.x(), pt.y(), px, py);
        return QPoint(px, py);
    };

    // Build closed polygon preview (vertices + cursor).
    QVector<QPoint> pts;
    for (const QPointF &v : m_vertices) pts << toPixel(v);
    pts << toPixel(m_cursor);

    // Fill.
    QPolygon poly(pts);
    painter->setBrush(QColor(100, 200, 100, 60));
    painter->setPen(QPen(QColor(0, 140, 0), 2, Qt::DashLine));
    painter->drawPolygon(poly);

    // Vertex dots.
    painter->setBrush(QColor(0, 140, 0));
    painter->setPen(Qt::NoPen);
    for (const QPoint &p : pts) painter->drawEllipse(p, 4, 4);

    // Closing line back to first vertex (if ≥ 2 vertices placed).
    if (pts.size() >= 2) {
        painter->setPen(QPen(QColor(0, 140, 0), 1, Qt::DotLine));
        painter->drawLine(pts.last(), pts.first());
    }

    painter->restore();
}

void OpenSWMMVisMapToolAddSubcatchment::cancel()
{
    m_vertices.clear();
    m_drawing = false;
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addsubcatch-cancel"));
}

void OpenSWMMVisMapToolAddSubcatchment::commit()
{
    if (m_vertices.size() < 3) { cancel(); return; }
    SWMMModelLayer *layer = activeModelLayer();
    if (!layer) { cancel(); return; }

    const QString name = nextAvailableName(layer);
    auto *cmd = new AddSubcatchmentCommand(layer, name, m_vertices, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    emit subcatchmentAdded(name);
    cancel();
}
