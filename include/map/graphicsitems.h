/*!
 * \file   graphicsitems.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Custom QGraphicsItem subclasses for the OpenSWMMVis map scene.
 */

#ifndef GRAPHICSITEMS_H
#define GRAPHICSITEMS_H

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QVariantMap>

class OpenSWMMVisLayer;

// ============================================================================
// Item type IDs  (QGraphicsItem::UserType + N)
// ============================================================================
enum OpenSWMMVisItemType
{
    NodeItemType            = QGraphicsItem::UserType + 1,
    LinkItemType            = QGraphicsItem::UserType + 2,
    CatchmentItemType       = QGraphicsItem::UserType + 3,
    VectorPointItemType     = QGraphicsItem::UserType + 4,
    VectorLineItemType      = QGraphicsItem::UserType + 5,
    VectorPolygonItemType   = QGraphicsItem::UserType + 6,
    RasterTileItemType      = QGraphicsItem::UserType + 7,
    MeasureVertexItemType   = QGraphicsItem::UserType + 8,
    MeasureSegmentItemType  = QGraphicsItem::UserType + 9,
    IdentifyCrossItemType   = QGraphicsItem::UserType + 10,
    ScaleBarItemType        = QGraphicsItem::UserType + 11,
    CoordDisplayItemType    = QGraphicsItem::UserType + 12,
};

// ============================================================================
// NodeGraphicsItem  — SWMM nodes (junctions, outfalls, storages, dividers)
// ============================================================================
class NodeGraphicsItem : public QGraphicsEllipseItem
{
public:
    enum NodeShape { Circle, Triangle, Square, Diamond };

    explicit NodeGraphicsItem(const QString &name,
                              double sceneX, double sceneY,
                              double radius,
                              NodeShape shape = Circle,
                              QGraphicsItem *parent = nullptr);

    int type() const override { return NodeItemType; }

    [[nodiscard]] QString   elementName() const { return m_name; }
    [[nodiscard]] NodeShape nodeShape()  const { return m_shape; }

    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    /** Set a scalar result value (0–1 normalized) and override the fill brush. */
    void setResultValue(double value, const QColor &color);
    void clearResultValue();

    void setElementRadius(double r);
    void setHighlighted(bool on);

    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    [[nodiscard]] QPainterPath shape() const override;

    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    QString       m_name;
    NodeShape     m_shape      = Circle;
    QVariantMap   m_attrs;
    double        m_radius     = 4.0;
    bool          m_highlighted = false;
    bool          m_hasResult   = false;
    OpenSWMMVisLayer *m_ownerLayer  = nullptr;
};

// ============================================================================
// LinkGraphicsItem  — SWMM links (conduits, pumps, orifices, weirs)
// ============================================================================
class LinkGraphicsItem : public QGraphicsPathItem
{
public:
    explicit LinkGraphicsItem(const QString &name,
                              const QVector<QPointF> &vertices,
                              QGraphicsItem *parent = nullptr);

    int type() const override { return LinkItemType; }

    [[nodiscard]] QString elementName() const { return m_name; }

    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    void setResultValue(double value, const QColor &color);
    void clearResultValue();

    void setHighlighted(bool on);
    void setVertices(const QVector<QPointF> &vertices);

    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    QString       m_name;
    QVariantMap   m_attrs;
    bool          m_highlighted  = false;
    bool          m_hasResult    = false;
    OpenSWMMVisLayer *m_ownerLayer  = nullptr;
};

// ============================================================================
// CatchmentGraphicsItem — SWMM subcatchments
// ============================================================================
class CatchmentGraphicsItem : public QGraphicsPolygonItem
{
public:
    explicit CatchmentGraphicsItem(const QString &name,
                                   const QPolygonF &polygon,
                                   QGraphicsItem *parent = nullptr);

    int type() const override { return CatchmentItemType; }

    [[nodiscard]] QString elementName() const { return m_name; }

    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    void setResultValue(double value, const QColor &color);
    void clearResultValue();

    void setHighlighted(bool on);

    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    QString       m_name;
    QVariantMap   m_attrs;
    bool          m_highlighted = false;
    OpenSWMMVisLayer *m_ownerLayer  = nullptr;
};

// ============================================================================
// VectorPointItem — OGR point feature
// ============================================================================
class VectorPointItem : public QGraphicsEllipseItem
{
public:
    explicit VectorPointItem(qint64 fid,
                             double sceneX, double sceneY,
                             double radius,
                             QGraphicsItem *parent = nullptr);

    int type() const override { return VectorPointItemType; }

    [[nodiscard]] qint64 featureId() const { return m_fid; }

    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    void setHighlighted(bool on);
    void setElementRadius(double r);

    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    qint64        m_fid;
    QVariantMap   m_attrs;
    bool          m_highlighted = false;
    OpenSWMMVisLayer *m_ownerLayer  = nullptr;
};

// ============================================================================
// VectorLineItem — OGR line feature
// ============================================================================
class VectorLineItem : public QGraphicsPathItem
{
public:
    explicit VectorLineItem(qint64 fid,
                            const QVector<QPointF> &vertices,
                            QGraphicsItem *parent = nullptr);

    int type() const override { return VectorLineItemType; }

    [[nodiscard]] qint64 featureId() const { return m_fid; }

    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    void setHighlighted(bool on);
    void setVertices(const QVector<QPointF> &vertices);

    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    qint64        m_fid;
    QVariantMap   m_attrs;
    bool          m_highlighted = false;
    OpenSWMMVisLayer *m_ownerLayer  = nullptr;
};

// ============================================================================
// VectorPolygonItem — OGR polygon feature
// ============================================================================
class VectorPolygonItem : public QGraphicsPolygonItem
{
public:
    explicit VectorPolygonItem(qint64 fid,
                               const QPolygonF &polygon,
                               QGraphicsItem *parent = nullptr);

    int type() const override { return VectorPolygonItemType; }

    [[nodiscard]] qint64 featureId() const { return m_fid; }

    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    void setHighlighted(bool on);

    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    qint64        m_fid;
    QVariantMap   m_attrs;
    bool          m_highlighted = false;
    OpenSWMMVisLayer *m_ownerLayer  = nullptr;
};

// ============================================================================
// RasterTileItem — for raster / WMS / WMTS image tiles
// ============================================================================
class RasterTileItem : public QGraphicsPixmapItem
{
public:
    explicit RasterTileItem(const QPixmap &pixmap,
                            const QRectF &sceneRect,
                            QGraphicsItem *parent = nullptr);

    int type() const override { return RasterTileItemType; }

    void updateTile(const QPixmap &pixmap, const QRectF &sceneRect);

    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    OpenSWMMVisLayer *m_ownerLayer = nullptr;
};

// ============================================================================
// ScaleBarItem — always-visible scale bar (painted in view coordinates)
// ============================================================================
class ScaleBarItem : public QGraphicsItem
{
public:
    explicit ScaleBarItem(QGraphicsItem *parent = nullptr);

    int type() const override { return ScaleBarItemType; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    void setMapUnitsPerPixel(double mpp) { m_mapUnitsPerPixel = mpp; }

private:
    double m_mapUnitsPerPixel = 1.0;
};

// ============================================================================
// CoordDisplayItem — always-visible coordinate readout (view coordinates)
// ============================================================================
class CoordDisplayItem : public QGraphicsItem
{
public:
    explicit CoordDisplayItem(QGraphicsItem *parent = nullptr);

    int type() const override { return CoordDisplayItemType; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    void setCoordinates(double x, double y);

private:
    double m_x = 0.0;
    double m_y = 0.0;
};

#endif // GRAPHICSITEMS_H
