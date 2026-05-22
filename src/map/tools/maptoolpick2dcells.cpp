/*!
 * \file   maptoolpick2dcells.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/tools/maptoolpick2dcells.h"

#include "layers/swmm2dresultslayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <cmath>

MapToolPick2DCells::MapToolPick2DCells(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("pick-2d-cells"), canvas, parent)
{
}

void MapToolPick2DCells::setMode(Mode m)
{
    if (m_mode == m) return;
    m_mode = m;
    // Cancel any in-progress selection on mode swap.
    m_dragging  = false;
    m_drawing   = false;
    m_lassoMapPts.clear();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-mode"));
}

void MapToolPick2DCells::activate()
{
    OpenSWMMVisMapTool::activate();
    m_targetLayer = findResultsLayer_();
    m_dragging = false;
    m_drawing  = false;
    m_lassoMapPts.clear();
}

void MapToolPick2DCells::deactivate()
{
    m_dragging = false;
    m_drawing  = false;
    m_lassoMapPts.clear();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-deactivate"));
    OpenSWMMVisMapTool::deactivate();
}

SWMM2DResultsLayer *MapToolPick2DCells::findResultsLayer_() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (auto *r2d = qobject_cast<SWMM2DResultsLayer *>(l))
            return r2d;
    }
    return nullptr;
}

QPointF MapToolPick2DCells::pixelToScene_(int px, int py) const
{
    double mx = 0.0, my = 0.0;
    toMapCoords(px, py, mx, my);
    // Layer convention: scene Y is the map Y flipped (see
    // SWMM2DResultsLayer::rebuildSceneGeometry_).
    return QPointF(mx, -my);
}

void MapToolPick2DCells::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    if (event->button() != Qt::LeftButton) {
        // Right-button: lasso vertex undo; otherwise ignore.
        if (m_mode == Mode::Lasso && m_drawing && event->button() == Qt::RightButton) {
            if (!m_lassoMapPts.isEmpty()) {
                m_lassoMapPts.removeLast();
                if (m_lassoMapPts.isEmpty()) m_drawing = false;
                m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-lasso-undo"));
            }
        }
        return;
    }

    if (m_mode == Mode::Box) {
        m_startPixel   = event->pos();
        m_currentPixel = event->pos();
        m_dragging     = false;
    } else {
        // Lasso: snap-to-pixel-position and append.
        double mx = 0.0, my = 0.0;
        toMapCoords(event->pos().x(), event->pos().y(), mx, my);
        m_lassoMapPts.push_back(QPointF(mx, my));
        m_drawing = true;
        m_cursorPixel = event->pos();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-lasso-vertex"));
    }
}

void MapToolPick2DCells::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;

    if (m_mode == Mode::Box) {
        if (!(event->buttons() & Qt::LeftButton)) return;
        m_currentPixel = event->pos();
        const QPoint delta = m_currentPixel - m_startPixel;
        if (!m_dragging &&
            (std::abs(delta.x()) > kDragThreshPx || std::abs(delta.y()) > kDragThreshPx))
        {
            m_dragging = true;
        }
        if (m_dragging)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-box-drag"));
    } else if (m_drawing) {
        m_cursorPixel = event->pos();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-lasso-preview"));
    }
}

void MapToolPick2DCells::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    if (m_mode != Mode::Box) return;
    if (event->button() != Qt::LeftButton) return;

    SWMM2DResultsLayer *layer = m_targetLayer.data();
    if (!layer)
        layer = findResultsLayer_();
    if (!layer) {
        m_dragging = false;
        return;
    }

    QVector<int> hits;
    if (m_dragging) {
        // Build scene-space rect from the two corners.
        const QPointF a = pixelToScene_(m_startPixel.x(),   m_startPixel.y());
        const QPointF b = pixelToScene_(m_currentPixel.x(), m_currentPixel.y());
        const QRectF sceneRect = QRectF(a, b).normalized();
        hits = layer->pickCellsInRect(sceneRect);
    } else {
        // Single click — hit-test the cell under the cursor.
        const QPointF scenePt = pixelToScene_(event->pos().x(), event->pos().y());
        const int idx = layer->pickCellAt(scenePt);
        if (idx >= 0) hits.push_back(idx);
    }

    m_dragging = false;
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-box-end"));

    // Persist highlight on the layer + announce upstream.
    QSet<int> set;
    for (int i : hits) set.insert(i);
    layer->highlightCells(set);
    emit cellsPicked(layer, hits);
}

void MapToolPick2DCells::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    if (m_mode != Mode::Lasso) return;
    if (event->button() != Qt::LeftButton) return;
    if (!m_drawing) return;

    // Double-click adds one vertex (the first click) — strip it before commit.
    if (!m_lassoMapPts.isEmpty())
        m_lassoMapPts.removeLast();
    if (m_lassoMapPts.size() < 3) {
        m_drawing = false;
        m_lassoMapPts.clear();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-lasso-cancel"));
        return;
    }

    SWMM2DResultsLayer *layer = m_targetLayer.data();
    if (!layer) layer = findResultsLayer_();
    if (!layer) {
        m_drawing = false;
        m_lassoMapPts.clear();
        return;
    }

    QPolygonF scenePoly;
    scenePoly.reserve(m_lassoMapPts.size());
    for (const QPointF& mp : m_lassoMapPts)
        scenePoly.append(QPointF(mp.x(), -mp.y()));

    const QVector<int> hits = layer->pickCellsInPolygon(scenePoly);

    m_drawing = false;
    m_lassoMapPts.clear();
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-lasso-commit"));

    QSet<int> set;
    for (int i : hits) set.insert(i);
    layer->highlightCells(set);
    emit cellsPicked(layer, hits);
}

void MapToolPick2DCells::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        m_dragging = false;
        m_drawing  = false;
        m_lassoMapPts.clear();
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-esc"));
        break;
    case Qt::Key_B:
        setMode(Mode::Box);
        break;
    case Qt::Key_L:
        setMode(Mode::Lasso);
        break;
    default:
        OpenSWMMVisMapTool::keyPressEvent(event);
        break;
    }
}

void MapToolPick2DCells::paint(QPainter *painter,
                                const MapExtent & /*canvasExtent*/,
                                const SpatialReferenceSystem * /*canvasSRS*/)
{
    if (!painter || !m_canvas) return;

    painter->save();

    // Box rubber-band.
    if (m_mode == Mode::Box && m_dragging) {
        const QRect pixelRect(m_startPixel, m_currentPixel);
        QPen pen(QColor(45, 130, 220, 220));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(1);
        painter->setPen(pen);
        painter->setBrush(QColor(45, 130, 220, 60));
        painter->drawRect(pixelRect.normalized());
    }

    // Lasso preview.
    if (m_mode == Mode::Lasso && m_drawing && !m_lassoMapPts.isEmpty()) {
        QPolygon viewPoly;
        viewPoly.reserve(m_lassoMapPts.size() + 1);
        for (const QPointF& mp : m_lassoMapPts) {
            int px = 0, py = 0;
            toPixelCoords(mp.x(), mp.y(), px, py);
            viewPoly << QPoint(px, py);
        }
        viewPoly << m_cursorPixel;     // preview edge to current cursor

        QPen pen(QColor(200, 90, 35, 230));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(2);
        painter->setPen(pen);
        painter->setBrush(QColor(200, 90, 35, 50));
        painter->drawPolygon(viewPoly);

        // Vertex dots.
        painter->setBrush(QColor(200, 90, 35, 220));
        painter->setPen(Qt::NoPen);
        for (const QPoint& p : viewPoly)
            painter->drawEllipse(p, 3, 3);
    }

    painter->restore();
}
