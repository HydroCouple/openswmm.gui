/*!
 * \file   graphicsitems.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Implementations for custom QGraphicsItem subclasses.
 */

#include "map/graphicsitems.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QFontMetrics>
#include <QtMath>

// ============================================================================
// NodeGraphicsItem
// ============================================================================

NodeGraphicsItem::NodeGraphicsItem(const QString &name,
                                   double sceneX, double sceneY,
                                   double radius,
                                   NodeShape shape,
                                   QGraphicsItem *parent)
    : QGraphicsEllipseItem(parent),
      m_name(name),
      m_shape(shape),
      m_radius(radius)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemIgnoresTransformations); // always rendered at fixed pixel size
    setAcceptHoverEvents(true);
    setToolTip(name);

    // Position in scene coords; bounding rect in local (pixel) coords centred at origin
    setPos(sceneX, sceneY);
    setRect(-radius, -radius, radius * 2.0, radius * 2.0);
}

void NodeGraphicsItem::setElementRadius(double r)
{
    m_radius = r;
    setRect(-r, -r, r * 2.0, r * 2.0);
}

void NodeGraphicsItem::setResultValue(double /*value*/, const QColor &color)
{
    m_hasResult = true;
    setBrush(QBrush(color));
    update();
}

void NodeGraphicsItem::clearResultValue()
{
    m_hasResult = false;
    update();
}

void NodeGraphicsItem::setHighlighted(bool on)
{
    m_highlighted = on;
    update();
}

void NodeGraphicsItem::paint(QPainter *painter,
                             const QStyleOptionGraphicsItem *option,
                             QWidget * /*widget*/)
{
    painter->setRenderHint(QPainter::Antialiasing);

    QColor fill    = m_highlighted ? Qt::yellow : brush().color();
    QColor outline = pen().color();
    double w       = pen().widthF();

    painter->setPen(QPen(outline, w));
    painter->setBrush(fill);

    QRectF rc = rect();

    switch (m_shape)
    {
    case Circle:
        painter->drawEllipse(rc);
        break;

    case Square:
        painter->drawRect(rc);
        break;

    case Triangle:
    {
        QPolygonF tri;
        tri << QPointF(rc.center().x(), rc.top())
            << rc.bottomLeft()
            << rc.bottomRight();
        painter->drawPolygon(tri);
        break;
    }

    case Diamond:
    {
        QPolygonF dia;
        dia << QPointF(rc.center().x(), rc.top())
            << QPointF(rc.right(), rc.center().y())
            << QPointF(rc.center().x(), rc.bottom())
            << QPointF(rc.left(), rc.center().y());
        painter->drawPolygon(dia);
        break;
    }
    }

    // Draw selection highlight ring
    if (option->state & QStyle::State_Selected)
    {
        painter->setPen(QPen(QColor(0, 120, 255), 2.0, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(rc.adjusted(-2, -2, 2, 2));
    }
}

QPainterPath NodeGraphicsItem::shape() const
{
    QPainterPath path;
    path.addEllipse(rect());
    return path;
}

// ============================================================================
// LinkGraphicsItem
// ============================================================================

LinkGraphicsItem::LinkGraphicsItem(const QString &name,
                                   const QVector<QPointF> &vertices,
                                   QGraphicsItem *parent)
    : QGraphicsPathItem(parent),
      m_name(name)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
    setToolTip(name);
    setVertices(vertices);
}

void LinkGraphicsItem::setResultValue(double /*value*/, const QColor &color)
{
    m_hasResult = true;
    QPen p = pen();
    p.setColor(color);
    setPen(p);
    update();
}

void LinkGraphicsItem::clearResultValue()
{
    m_hasResult = false;
    update();
}

void LinkGraphicsItem::setHighlighted(bool on)
{
    m_highlighted = on;
    if (on)
        setPen(QPen(Qt::yellow, pen().widthF() + 2.0));
    else
        setPen(pen());   // reset
    update();
}

void LinkGraphicsItem::setVertices(const QVector<QPointF> &vertices)
{
    if (vertices.size() < 2)
        return;

    QPainterPath p;
    p.moveTo(vertices.first());
    for (int i = 1; i < vertices.size(); ++i)
        p.lineTo(vertices[i]);
    setPath(p);
}

// ============================================================================
// CatchmentGraphicsItem
// ============================================================================

CatchmentGraphicsItem::CatchmentGraphicsItem(const QString &name,
                                             const QPolygonF &polygon,
                                             QGraphicsItem *parent)
    : QGraphicsPolygonItem(polygon, parent),
      m_name(name)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
    setToolTip(name);
}

void CatchmentGraphicsItem::setResultValue(double /*value*/, const QColor &color)
{
    setBrush(QBrush(color));
    update();
}

void CatchmentGraphicsItem::clearResultValue()
{
    update();
}

void CatchmentGraphicsItem::setHighlighted(bool on)
{
    m_highlighted = on;
    if (on)
        setBrush(QBrush(QColor(255, 255, 0, 80)));
    update();
}

// ============================================================================
// VectorPointItem
// ============================================================================

VectorPointItem::VectorPointItem(qint64 fid,
                                 double sceneX, double sceneY,
                                 double radius,
                                 QGraphicsItem *parent)
    : QGraphicsEllipseItem(parent),
      m_fid(fid)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
    setRect(sceneX - radius, sceneY - radius, radius * 2.0, radius * 2.0);
}

void VectorPointItem::setHighlighted(bool on)
{
    m_highlighted = on;
    if (on)
        setBrush(QBrush(Qt::yellow));
    update();
}

void VectorPointItem::setElementRadius(double r)
{
    QRectF rc = rect();
    double cx = rc.center().x(), cy = rc.center().y();
    setRect(cx - r, cy - r, r * 2.0, r * 2.0);
}

// ============================================================================
// VectorLineItem
// ============================================================================

VectorLineItem::VectorLineItem(qint64 fid,
                                const QVector<QPointF> &vertices,
                                QGraphicsItem *parent)
    : QGraphicsPathItem(parent),
      m_fid(fid)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
    setVertices(vertices);
}

void VectorLineItem::setHighlighted(bool on)
{
    m_highlighted = on;
    if (on)
        setPen(QPen(Qt::yellow, pen().widthF() + 2.0));
    update();
}

void VectorLineItem::setVertices(const QVector<QPointF> &vertices)
{
    if (vertices.size() < 2)
        return;

    QPainterPath p;
    p.moveTo(vertices.first());
    for (int i = 1; i < vertices.size(); ++i)
        p.lineTo(vertices[i]);
    setPath(p);
}

// ============================================================================
// VectorPolygonItem
// ============================================================================

VectorPolygonItem::VectorPolygonItem(qint64 fid,
                                     const QPolygonF &polygon,
                                     QGraphicsItem *parent)
    : QGraphicsPolygonItem(polygon, parent),
      m_fid(fid)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setAcceptHoverEvents(true);
}

void VectorPolygonItem::setHighlighted(bool on)
{
    m_highlighted = on;
    if (on)
        setBrush(QBrush(QColor(255, 255, 0, 80)));
    update();
}

// ============================================================================
// RasterTileItem
// ============================================================================

RasterTileItem::RasterTileItem(const QPixmap &pixmap,
                               const QRectF &sceneRect,
                               QGraphicsItem *parent)
    : QGraphicsPixmapItem(pixmap, parent)
{
    // Scale and position the pixmap to cover seceRect in scene coordinates
    setPos(sceneRect.topLeft());
    if (pixmap.width() > 0 && pixmap.height() > 0)
    {
        double sx = sceneRect.width()  / pixmap.width();
        double sy = sceneRect.height() / pixmap.height();
        setTransform(QTransform::fromScale(sx, sy));
    }
}

void RasterTileItem::updateTile(const QPixmap &pixmap, const QRectF &sceneRect)
{
    setPixmap(pixmap);
    setPos(sceneRect.topLeft());
    if (pixmap.width() > 0 && pixmap.height() > 0)
    {
        double sx = sceneRect.width()  / pixmap.width();
        double sy = sceneRect.height() / pixmap.height();
        setTransform(QTransform::fromScale(sx, sy));
    }
}

// ============================================================================
// ScaleBarItem
// ============================================================================

ScaleBarItem::ScaleBarItem(QGraphicsItem *parent)
    : QGraphicsItem(parent)
{
}

QRectF ScaleBarItem::boundingRect() const
{
    return QRectF(0, 0, 200, 30);
}

void ScaleBarItem::paint(QPainter *painter,
                         const QStyleOptionGraphicsItem * /*option*/,
                         QWidget * /*widget*/)
{
    if (m_metresPerPixel <= 0.0)
        return;

    const int maxLen = m_settings ? m_settings->maxBarLength() : 100;

    // Round to a "nice" bar length in metres
    double barMetres = maxLen * m_metresPerPixel;
    double magnitude = std::pow(10.0, std::floor(std::log10(barMetres)));
    double nice      = barMetres / magnitude;
    if      (nice < 2.0) nice = 1.0;
    else if (nice < 5.0) nice = 2.0;
    else                 nice = 5.0;
    barMetres = nice * magnitude;

    // Snap bar length to the same rounded value the label will display so bar ↔ label stay in sync.
    if (m_settings && !m_rawCRS)
        barMetres = m_settings->roundedMetres(barMetres);

    int barPixels = static_cast<int>(std::round(barMetres / m_metresPerPixel));
    if (barPixels < 2) barPixels = 2;

    painter->setPen(m_settings ? m_settings->pen() : QPen(Qt::black, 2));
    painter->drawLine(0, 20, barPixels, 20);
    painter->drawLine(0, 16, 0, 24);
    painter->drawLine(barPixels, 16, barPixels, 24);

    painter->setFont(m_settings ? m_settings->font() : QFont(QStringLiteral("sans-serif"), 8));
    QString label = m_settings
                        ? m_settings->formatLabel(barMetres, m_rawCRS)
                        : ((barMetres >= 1000.0)
                               ? QStringLiteral("%1 km").arg(barMetres / 1000.0, 0, 'g', 3)
                               : QStringLiteral("%1 m").arg(barMetres, 0, 'g', 3));
    painter->drawText(0, 12, label);
}

// ============================================================================
// CoordDisplayItem
// ============================================================================

CoordDisplayItem::CoordDisplayItem(QGraphicsItem *parent)
    : QGraphicsItem(parent)
{
}

QRectF CoordDisplayItem::boundingRect() const
{
    return QRectF(0, 0, 250, 20);
}

void CoordDisplayItem::paint(QPainter *painter,
                             const QStyleOptionGraphicsItem * /*option*/,
                             QWidget * /*widget*/)
{
    QString text = QStringLiteral("X: %1  Y: %2")
                       .arg(m_x, 0, 'f', 5)
                       .arg(m_y, 0, 'f', 5);

    painter->setFont(QFont(QStringLiteral("sans-serif"), 9));
    QFontMetrics fm(painter->font());
    int textW = fm.horizontalAdvance(text);
    int textH = fm.height();

    painter->fillRect(-3, -textH, textW + 6, textH + 4,
                      QColor(255, 255, 255, 180));
    painter->setPen(Qt::black);
    painter->drawText(0, 0, text);
}

void CoordDisplayItem::setCoordinates(double x, double y)
{
    m_x = x;
    m_y = y;
    update();
}
