/*!
 * \file   overviewmappanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/overviewmappanel.h"

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

namespace openswmmvis::ui {

// =============================================================================
// OverviewMapWidget
// =============================================================================

OverviewMapWidget::OverviewMapWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(180, 120);
    setMouseTracking(true);
}

void OverviewMapWidget::setCanvas(MapCanvas *canvas)
{
    m_canvas = canvas;
    update();
}

void OverviewMapWidget::setModelLayer(SWMMModelLayer *layer)
{
    m_modelLayer = layer;
    refresh();
}

void OverviewMapWidget::refresh()
{
    m_nodeCoords.clear();
    m_linkLines.clear();
    m_modelExtent = QRectF();
    // Querying the model layer's full geometry is out of scope for this
    // first cut — instead we rely on the layer's bounding extent.
    if (m_modelLayer) {
        // OpenSWMMVisLayer::extent() returns a MapExtent; convert to QRectF.
        // (We avoid pulling thousands of node coordinates per repaint.)
        const auto ext = m_modelLayer->extent();
        m_modelExtent = QRectF(QPointF(ext.xMin(), ext.yMin()),
                                QPointF(ext.xMax(), ext.yMax()));
    }
    update();
}

QPointF OverviewMapWidget::widgetToMap(const QPoint &px) const
{
    if (m_modelExtent.isEmpty()) return {};
    const double fx = double(px.x()) / std::max(1, width());
    const double fy = double(px.y()) / std::max(1, height());
    return QPointF(
        m_modelExtent.left() + fx * m_modelExtent.width(),
        m_modelExtent.bottom() - fy * m_modelExtent.height());   // y-flip
}

void OverviewMapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor(245, 245, 245));
    if (m_modelExtent.isEmpty()) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(rect(), Qt::AlignCenter, tr("(no model)"));
        return;
    }

    // Frame
    p.setPen(QColor(160, 160, 160));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // Viewport rectangle: project canvas extent into widget coords.
    if (m_canvas) {
        const auto ext = m_canvas->extent();
        const double w = m_modelExtent.width();
        const double h = m_modelExtent.height();
        if (w > 0 && h > 0) {
            const double fxL = (ext.xMin() - m_modelExtent.left()) / w;
            const double fxR = (ext.xMax() - m_modelExtent.left()) / w;
            // y-flip the canvas extent into widget space:
            const double fyT = (m_modelExtent.bottom() - ext.yMax()) / h;
            const double fyB = (m_modelExtent.bottom() - ext.yMin()) / h;
            const QRectF vp(QPointF(fxL * width(), fyT * height()),
                            QPointF(fxR * width(), fyB * height()));

            QPen vpPen(QColor(45, 130, 220, 230));
            vpPen.setWidth(2);
            p.setPen(vpPen);
            p.setBrush(QColor(45, 130, 220, 60));
            p.drawRect(vp);
        }
    }
}

void OverviewMapWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_dragging  = true;
    m_dragStart = e->pos();
    const QPointF mapPt = widgetToMap(e->pos());
    emit requestCenterAt(mapPt);
}

void OverviewMapWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging) return;
    const QPointF mapPt = widgetToMap(e->pos());
    emit requestCenterAt(mapPt);
}

void OverviewMapWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}

// =============================================================================
// OverviewMapPanel
// =============================================================================

OverviewMapPanel::OverviewMapPanel(QWidget *parent)
    : QDockWidget(tr("Overview Map"), parent)
{
    setObjectName(QStringLiteral("dockWidgetOverviewMap"));
    m_widget = new OverviewMapWidget(this);
    setWidget(m_widget);
}

} // namespace openswmmvis::ui
