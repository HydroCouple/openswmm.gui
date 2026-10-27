/**
 * @file      graphicsitems.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Custom QGraphicsItem subclasses for the OpenSWMMVis map scene.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
#include <QPointF>
#include <QVariantMap>
#include <QVector>

#include "map/scalebarsettings.h"

class OpenSWMMVisLayer;

// ============================================================================
// Item type IDs  (QGraphicsItem::UserType + N)
// ============================================================================
enum OpenSWMMVisItemType {
    NodeItemType = QGraphicsItem::UserType + 1,
    LinkItemType = QGraphicsItem::UserType + 2,
    CatchmentItemType = QGraphicsItem::UserType + 3,
    VectorPointItemType = QGraphicsItem::UserType + 4,
    VectorLineItemType = QGraphicsItem::UserType + 5,
    VectorPolygonItemType = QGraphicsItem::UserType + 6,
    RasterTileItemType = QGraphicsItem::UserType + 7,
    MeasureVertexItemType = QGraphicsItem::UserType + 8,
    MeasureSegmentItemType = QGraphicsItem::UserType + 9,
    IdentifyCrossItemType = QGraphicsItem::UserType + 10,
    ScaleBarItemType = QGraphicsItem::UserType + 11,
    CoordDisplayItemType = QGraphicsItem::UserType + 12,
    VectorPolygonPathItemType = QGraphicsItem::UserType + 13,
};

// ============================================================================
// NodeGraphicsItem  — SWMM nodes (junctions, outfalls, storages, dividers)
// ============================================================================
class NodeGraphicsItem : public QGraphicsEllipseItem
{
public:
    enum NodeShape { Circle, Triangle, Square, Diamond };

    /**
     * @brief Create selectable hover-enabled fixed-screen-size SWMM marker centered locally at zero
     *   and positioned at scene anchor.
     * @param[in] name Exact SWMM node name copied to identity and tooltip.
     * @param[in] sceneX Scene anchor X.
     * @param[in] sceneY Scene anchor Y, caller applies any map Y inversion.
     * @param[in] radius Raw local logical-pixel radius, unchecked; transform ignored flag keeps
     *   marker size fixed.
     * @param[in] shape Circle/Triangle/Square/Diamond paint shape, no enum validation.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Input geometry already in item/scene coordinates, no CRS projection or engine/OGR
     *   mutation. Identity and attributes are snapshots; ownerLayer initially null.
     */
    explicit NodeGraphicsItem(const QString &name, double sceneX, double sceneY, double radius,
                              NodeShape shape = Circle, QGraphicsItem *parent = nullptr);

    /**
     * @return Stable NodeItemType custom QGraphicsItem discriminator, independent of engine
     *   subtype/feature ID. No mutation.
     */
    int type() const override { return NodeItemType; }

    /**
     * @return Constructor-copied exact element identity by value, not a live engine lookup;
     *   unaffected by geometry/result/style changes.
     */
    [[nodiscard]] QString elementName() const { return m_name; }
    /**
     * @return Stored constructor shape enum; not affected by engine type conversion or hit-test
     *   shape, which remains ellipse.
     */
    [[nodiscard]] NodeShape nodeShape() const { return m_shape; }

    /**
     * @param[in] attrs Attribute value map copied into item, no live engine/provider backing.
     * @brief Replace metadata snapshot without tooltip/identity/style update, signal or repaint;
     *   callers refresh explicitly if needed.
     */
    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    /**
     * @return Value copy of stored metadata, empty initially; may be stale relative to engine/OGR
     *   changes. No ownership transfer of layer objects.
     */
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    /** Set a scalar result value (0–1 normalized) and override the fill brush.
     * @param[in] value Ignored scalar argument, not stored, normalized or validated.
     * @param[in] color Colour directly replaces fill brush with solid QBrush.
     * @note Requests item update; no engine/result lookup or attribute metadata update. Sets
     *   internal result-present flag, which does not restore prior style later.
     */
    void setResultValue(double value, const QColor &color);
    /**
     * @brief Clear result-present flag and request repaint. Existing result colour remains; no
     *   saved brush/pen restored or engine state changed.
     */
    void clearResultValue();

    /**
     * @param[in] r Raw new local radius, caller supplies nonnegative finite; no clamp.
     * @brief Resize local ellipse bounding rect around origin and store radius. No engine geometry
     *   or attribute change. Qt setRect updates scene geometry/repaint.
     */
    void setElementRadius(double r);
    /**
     * @param[in] on Requested visual highlight flag, no equality suppression.
     * @note Stores highlight flag and update; painter uses yellow while true and current underlying
     *   brush again when false. Does not change brush or Qt selected flag.
     */
    void setHighlighted(bool on);

    /**
     * @param[in] painter Required active local-item painter; no null guard.
     * @param[in] option Required QStyleOptionGraphicsItem; State_Selected draws blue dashed ellipse
     *   ring.
     * @param[in] widget Ignored borrowed Qt widget.
     * @brief Draw configured geometric marker, yellow when custom-highlighted, then Qt selection
     *   ring. No engine/metadata query.
     * @note Painter state is NOT saved/restored by this method; Qt/caller handles outer state. Node
     *   shape invalid enum draws no body; hit-test still ellipse.
     */
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    /**
     * @return New elliptical hit-test path of rect for ALL marker shapes, even
     *   triangle/square/diamond; excludes extra selected ring. No geometry mutation.
     */
    [[nodiscard]] QPainterPath shape() const override;

    /**
     * @param[in] layer Optional borrowed raw layer identity, must outlive any caller dereferencing
     *   it.
     * @brief Store owner association only; does not reparent/own item or layer, update
     *   geometry/style, connect signals or repaint.
     */
    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    /**
     * @return Borrowed raw associated layer or null, not deletion-guarded; no ownership transfer or
     *   validation.
     */
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    QString m_name;
    NodeShape m_shape = Circle;
    QVariantMap m_attrs;
    double m_radius = 4.0;
    bool m_highlighted = false;
    bool m_hasResult = false;
    OpenSWMMVisLayer *m_ownerLayer = nullptr;
};

// ============================================================================
// LinkGraphicsItem  — SWMM links (conduits, pumps, orifices, weirs)
// ============================================================================
class LinkGraphicsItem : public QGraphicsPathItem
{
public:
    /**
     * @brief Create selectable hover-enabled polyline item, using geometry setter for initial path.
     * @param[in] name Exact SWMM link name copied to identity/tooltip.
     * @param[in] vertices Polyline points already in item/scene frame; at least2needed to populate
     *   path.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Input geometry already in item/scene coordinates, no CRS projection or engine/OGR
     *   mutation. Identity and attributes are snapshots; ownerLayer initially null.
     */
    explicit LinkGraphicsItem(const QString &name, const QVector<QPointF> &vertices,
                              QGraphicsItem *parent = nullptr);

    /**
     * @return Stable LinkItemType custom QGraphicsItem discriminator, independent of engine
     *   subtype/feature ID. No mutation.
     */
    int type() const override { return LinkItemType; }

    /**
     * @return Constructor-copied exact element identity by value, not a live engine lookup;
     *   unaffected by geometry/result/style changes.
     */
    [[nodiscard]] QString elementName() const { return m_name; }

    /**
     * @param[in] attrs Attribute value map copied into item, no live engine/provider backing.
     * @brief Replace metadata snapshot without tooltip/identity/style update, signal or repaint;
     *   callers refresh explicitly if needed.
     */
    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    /**
     * @return Value copy of stored metadata, empty initially; may be stale relative to engine/OGR
     *   changes. No ownership transfer of layer objects.
     */
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    /**
     * @param[in] value Ignored scalar argument, not stored, normalized or validated.
     * @param[in] color Colour directly replaces pen colour (other current pen fields retained).
     * @note Requests item update; no engine/result lookup or attribute metadata update. Sets
     *   internal result-present flag, which does not restore prior style later.
     */
    void setResultValue(double value, const QColor &color);
    /**
     * @brief Clear result-present flag and request repaint. Existing result colour remains; no
     *   saved brush/pen restored or engine state changed.
     */
    void clearResultValue();

    /**
     * @param[in] on Requested visual highlight flag, no equality suppression.
     * @note Stores flag; each true replaces pen with solid yellow/current width+2, so repeated true
     *   accumulates width. False does NOT restore saved pen; current yellow/widened pen remains.
     *   Requests update, no Qt selection flag change.
     */
    void setHighlighted(bool on);
    /**
     * @param[in] vertices At least2prepared local polyline points, no finite/duplicate checks.
     * @brief Replace path by moveTo first then lineTo others; fewer than2returns WITHOUT clearing
     *   old path. No engine/cache/attribute mutation.
     */
    void setVertices(const QVector<QPointF> &vertices);

    /**
     * @param[in] layer Optional borrowed raw layer identity, must outlive any caller dereferencing
     *   it.
     * @brief Store owner association only; does not reparent/own item or layer, update
     *   geometry/style, connect signals or repaint.
     */
    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    /**
     * @return Borrowed raw associated layer or null, not deletion-guarded; no ownership transfer or
     *   validation.
     */
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    QString m_name;
    QVariantMap m_attrs;
    bool m_highlighted = false;
    bool m_hasResult = false;
    OpenSWMMVisLayer *m_ownerLayer = nullptr;
};

// ============================================================================
// CatchmentGraphicsItem — SWMM subcatchments
// ============================================================================
class CatchmentGraphicsItem : public QGraphicsPolygonItem
{
public:
    /**
     * @brief Create selectable hover-enabled subcatchment polygon.
     * @param[in] name Exact subcatchment name copied to identity/tooltip.
     * @param[in] polygon Polygon copied unchanged, native item frame and inherited
     *   QGraphicsPolygonItem fill/hit rules.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Input geometry already in item/scene coordinates, no CRS projection or engine/OGR
     *   mutation. Identity and attributes are snapshots; ownerLayer initially null.
     */
    explicit CatchmentGraphicsItem(const QString &name, const QPolygonF &polygon,
                                   QGraphicsItem *parent = nullptr);

    /**
     * @return Stable CatchmentItemType custom QGraphicsItem discriminator, independent of engine
     *   subtype/feature ID. No mutation.
     */
    int type() const override { return CatchmentItemType; }

    /**
     * @return Constructor-copied exact element identity by value, not a live engine lookup;
     *   unaffected by geometry/result/style changes.
     */
    [[nodiscard]] QString elementName() const { return m_name; }

    /**
     * @param[in] attrs Attribute value map copied into item, no live engine/provider backing.
     * @brief Replace metadata snapshot without tooltip/identity/style update, signal or repaint;
     *   callers refresh explicitly if needed.
     */
    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    /**
     * @return Value copy of stored metadata, empty initially; may be stale relative to engine/OGR
     *   changes. No ownership transfer of layer objects.
     */
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    /**
     * @param[in] value Ignored scalar argument, not stored, normalized or validated.
     * @param[in] color Colour directly replaces fill brush with solid QBrush.
     * @note Requests item update; no engine/result lookup or attribute metadata update. No
     *   result-present flag is stored.
     */
    void setResultValue(double value, const QColor &color);
    /**
     * @brief Request repaint only. Existing result colour remains; no saved brush/pen restored or
     *   engine state changed.
     */
    void clearResultValue();

    /**
     * @param[in] on Requested visual highlight flag, no equality suppression.
     * @note Stores flag; true replaces brush with translucent yellow alpha80, false does NOT
     *   restore prior brush. Requests update; Qt selected state/engine metadata unchanged.
     */
    void setHighlighted(bool on);

    /**
     * @param[in] layer Optional borrowed raw layer identity, must outlive any caller dereferencing
     *   it.
     * @brief Store owner association only; does not reparent/own item or layer, update
     *   geometry/style, connect signals or repaint.
     */
    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    /**
     * @return Borrowed raw associated layer or null, not deletion-guarded; no ownership transfer or
     *   validation.
     */
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    QString m_name;
    QVariantMap m_attrs;
    bool m_highlighted = false;
    OpenSWMMVisLayer *m_ownerLayer = nullptr;
};

// ============================================================================
// VectorPointItem — OGR point feature
// ============================================================================
class VectorPointItem : public QGraphicsEllipseItem
{
public:
    /**
     * @brief Create a selectable hover-enabled GIS point with a fixed-pixel Circle marker.
     * @param[in] fid OGR feature ID stored unchanged.
     * @param[in] sceneX Map anchor X stored as item position.
     * @param[in] sceneY Map anchor Y in the same prepared scene frame.
     * @param[in] radius Logical screen-pixel radius, unchecked. The origin-centered
     *   ellipse ignores map transformations so size is independent of map CRS/zoom.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Input geometry already in item/scene coordinates, no CRS projection or engine/OGR
     *   mutation. Identity and attributes are snapshots; ownerLayer initially null.
     */
    explicit VectorPointItem(qint64 fid, double sceneX, double sceneY, double radius,
                             QGraphicsItem *parent = nullptr);

    /**
     * @return Stable VectorPointItemType custom QGraphicsItem discriminator, independent of engine
     *   subtype/feature ID. No mutation.
     */
    int type() const override { return VectorPointItemType; }

    /**
     * @return Constructor-copied OGR FID, no existence/source identity validation; not
     *   owned/allocated by item.
     */
    [[nodiscard]] qint64 featureId() const { return m_fid; }

    /**
     * @param[in] attrs Attribute value map copied into item, no live engine/provider backing.
     * @brief Replace metadata snapshot without tooltip/identity/style update, signal or repaint;
     *   callers refresh explicitly if needed.
     */
    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    /**
     * @return Value copy of stored metadata, empty initially; may be stale relative to engine/OGR
     *   changes. No ownership transfer of layer objects.
     */
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    /**
     * @param[in] on Requested visual highlight flag, no equality suppression.
     * @note Stores flag; true replaces brush with opaque yellow, false does NOT restore prior
     *   brush. Requests update; Qt selected state/engine metadata unchanged.
     */
    void setHighlighted(bool on);
    /**
     * @param[in] r New logical screen-pixel radius, caller supplies nonnegative finite; no clamp.
     * @brief Resize local ellipse bounding rect around current rect center; item position
     *   unchanged. No engine geometry or attribute change. Qt setRect updates scene
     *   geometry/repaint.
     */
    void setElementRadius(double r);

    /*! G-1 — marker shape as a canonical OpenSWMM::Render::MarkerShape int
     *  (0..18). Painted via drawMarkerShape so GIS vector points get the
     *  full canonical shape set instead of always-ellipse.
     * @param[in] shapeValue Canonical MarkerShape int0..18 expected, not validated; passed to
     *   marker painter.
     * @note Stores value and requests update even equal. Does not change inherited ellipse
     *   hit-test/bounding rect or feature geometry.
     */
    void setMarkerShape(int shapeValue)
    {
        m_shape = shapeValue;
        update();
    }
    /**
     * @return Raw stored canonical shape int, Circle0initial; no validation or engine query.
     */
    [[nodiscard]] int markerShape() const { return m_shape; }

    /**
     * @param[in] layer Optional borrowed raw layer identity, must outlive any caller dereferencing
     *   it.
     * @brief Store owner association only; does not reparent/own item or layer, update
     *   geometry/style, connect signals or repaint.
     */
    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    /**
     * @return Borrowed raw associated layer or null, not deletion-guarded; no ownership transfer or
     *   validation.
     */
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

protected:
    /**
     * @param[in] painter Required active local-item painter; no null guard.
     * @param[in] option Required QStyleOptionGraphicsItem; State_Selected draws blue dashed ellipse
     *   ring.
     * @param[in] widget Ignored borrowed Qt widget.
     * @brief Draw canonical marker helper using current rect/brush/pen, then Qt selection ring. No
     *   engine/metadata query.
     * @note Painter state is NOT saved/restored by this method; Qt/caller handles outer state.
     *   Marker size=min rect width/height in local units, not necessarily fixed pixels under view
     *   transform.
     */
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    qint64 m_fid;
    QVariantMap m_attrs;
    bool m_highlighted = false;
    int m_shape = 0; // canonical MarkerShape::Circle
    OpenSWMMVisLayer *m_ownerLayer = nullptr;
};

// ============================================================================
// VectorLineItem — OGR line feature
// ============================================================================
class VectorLineItem : public QGraphicsPathItem
{
public:
    /**
     * @brief Create selectable hover-enabled GIS line, no automatic tooltip.
     * @param[in] fid OGR feature ID stored unchanged.
     * @param[in] vertices Prepared item-frame polyline,>=2points needed; copied into path.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Input geometry already in item/scene coordinates, no CRS projection or engine/OGR
     *   mutation. Identity and attributes are snapshots; ownerLayer initially null.
     */
    explicit VectorLineItem(qint64 fid, const QVector<QPointF> &vertices,
                            QGraphicsItem *parent = nullptr);

    /**
     * @return Stable VectorLineItemType custom QGraphicsItem discriminator, independent of engine
     *   subtype/feature ID. No mutation.
     */
    int type() const override { return VectorLineItemType; }

    /**
     * @return Constructor-copied OGR FID, no existence/source identity validation; not
     *   owned/allocated by item.
     */
    [[nodiscard]] qint64 featureId() const { return m_fid; }

    /**
     * @param[in] attrs Attribute value map copied into item, no live engine/provider backing.
     * @brief Replace metadata snapshot without tooltip/identity/style update, signal or repaint;
     *   callers refresh explicitly if needed.
     */
    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    /**
     * @return Value copy of stored metadata, empty initially; may be stale relative to engine/OGR
     *   changes. No ownership transfer of layer objects.
     */
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    /**
     * @param[in] on Requested visual highlight flag, no equality suppression.
     * @note Stores flag; each true replaces pen with solid yellow/current width+2, so repeated true
     *   accumulates width. False does NOT restore saved pen; current yellow/widened pen remains.
     *   Requests update, no Qt selection flag change.
     */
    void setHighlighted(bool on);
    /**
     * @param[in] vertices At least2prepared local polyline points, no finite/duplicate checks.
     * @brief Replace path by moveTo first then lineTo others; fewer than2returns WITHOUT clearing
     *   old path. No engine/cache/attribute mutation.
     */
    void setVertices(const QVector<QPointF> &vertices);

    /**
     * @param[in] layer Optional borrowed raw layer identity, must outlive any caller dereferencing
     *   it.
     * @brief Store owner association only; does not reparent/own item or layer, update
     *   geometry/style, connect signals or repaint.
     */
    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    /**
     * @return Borrowed raw associated layer or null, not deletion-guarded; no ownership transfer or
     *   validation.
     */
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    qint64 m_fid;
    QVariantMap m_attrs;
    bool m_highlighted = false;
    OpenSWMMVisLayer *m_ownerLayer = nullptr;
};

// ============================================================================
// VectorPolygonItem — OGR polygon feature
// ============================================================================
class VectorPolygonItem : public QGraphicsPolygonItem
{
public:
    /**
     * @brief Create selectable hover-enabled GIS polygon.
     * @param[in] fid OGR feature ID stored unchanged.
     * @param[in] polygon Prepared local polygon copied directly; this class has no separate holes
     *   input.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Input geometry already in item/scene coordinates, no CRS projection or engine/OGR
     *   mutation. Identity and attributes are snapshots; ownerLayer initially null.
     */
    explicit VectorPolygonItem(qint64 fid, const QPolygonF &polygon,
                               QGraphicsItem *parent = nullptr);

    /**
     * @return Stable VectorPolygonItemType custom QGraphicsItem discriminator, independent of
     *   engine subtype/feature ID. No mutation.
     */
    int type() const override { return VectorPolygonItemType; }

    /**
     * @return Constructor-copied OGR FID, no existence/source identity validation; not
     *   owned/allocated by item.
     */
    [[nodiscard]] qint64 featureId() const { return m_fid; }

    /**
     * @param[in] attrs Attribute value map copied into item, no live engine/provider backing.
     * @brief Replace metadata snapshot without tooltip/identity/style update, signal or repaint;
     *   callers refresh explicitly if needed.
     */
    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    /**
     * @return Value copy of stored metadata, empty initially; may be stale relative to engine/OGR
     *   changes. No ownership transfer of layer objects.
     */
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    /**
     * @param[in] on Requested visual highlight flag, no equality suppression.
     * @note Stores flag; true replaces brush with translucent yellow alpha80, false does NOT
     *   restore prior brush. Requests update; Qt selected state/engine metadata unchanged.
     */
    void setHighlighted(bool on);

    /**
     * @param[in] layer Optional borrowed raw layer identity, must outlive any caller dereferencing
     *   it.
     * @brief Store owner association only; does not reparent/own item or layer, update
     *   geometry/style, connect signals or repaint.
     */
    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    /**
     * @return Borrowed raw associated layer or null, not deletion-guarded; no ownership transfer or
     *   validation.
     */
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    qint64 m_fid;
    QVariantMap m_attrs;
    bool m_highlighted = false;
    OpenSWMMVisLayer *m_ownerLayer = nullptr;
};

// ============================================================================
// VectorPolygonPathItem — OGR polygon feature WITH interior rings (holes)
// ============================================================================
// Path-based sibling of VectorPolygonItem: builds a QPainterPath from the
// exterior ring plus every interior ring using the odd-even fill rule, so the
// fill correctly excludes the holes and hit-testing misses points in a hole.
// Takes rings as plain point vectors (scene coords) to avoid a header
// dependency on core/editgeometry.h.
class VectorPolygonPathItem : public QGraphicsPathItem
{
public:
    /**
     * @brief Create selectable hover-enabled GIS path polygon with hole-aware odd-even fill.
     * @param[in] fid OGR feature ID stored unchanged.
     * @param[in] exterior Prepared exterior ring; fewer than3points ignored.
     * @param[in] interiors Prepared interior rings; each>=3points closed into same OddEvenFill
     *   path, no topology validation.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Input geometry already in item/scene coordinates, no CRS projection or engine/OGR
     *   mutation. Identity and attributes are snapshots; ownerLayer initially null.
     */
    explicit VectorPolygonPathItem(qint64 fid, const QVector<QPointF> &exterior,
                                   const QVector<QVector<QPointF>> &interiors = {},
                                   QGraphicsItem *parent = nullptr);

    /**
     * @return Stable VectorPolygonPathItemType custom QGraphicsItem discriminator, independent of
     *   engine subtype/feature ID. No mutation.
     */
    int type() const override { return VectorPolygonPathItemType; }

    /**
     * @return Constructor-copied OGR FID, no existence/source identity validation; not
     *   owned/allocated by item.
     */
    [[nodiscard]] qint64 featureId() const { return m_fid; }

    /**
     * @param[in] attrs Attribute value map copied into item, no live engine/provider backing.
     * @brief Replace metadata snapshot without tooltip/identity/style update, signal or repaint;
     *   callers refresh explicitly if needed.
     */
    void setAttributes(const QVariantMap &attrs) { m_attrs = attrs; }
    /**
     * @return Value copy of stored metadata, empty initially; may be stale relative to engine/OGR
     *   changes. No ownership transfer of layer objects.
     */
    [[nodiscard]] QVariantMap attributes() const { return m_attrs; }

    /**
     * @param[in] on Requested visual highlight flag, no equality suppression.
     * @note Stores flag; true replaces brush with translucent yellow alpha80, false does NOT
     *   restore prior brush. Requests update; Qt selected state/engine metadata unchanged.
     */
    void setHighlighted(bool on);

    /**
     * @param[in] layer Optional borrowed raw layer identity, must outlive any caller dereferencing
     *   it.
     * @brief Store owner association only; does not reparent/own item or layer, update
     *   geometry/style, connect signals or repaint.
     */
    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    /**
     * @return Borrowed raw associated layer or null, not deletion-guarded; no ownership transfer or
     *   validation.
     */
    [[nodiscard]] OpenSWMMVisLayer *ownerLayer() const { return m_ownerLayer; }

private:
    qint64 m_fid;
    QVariantMap m_attrs;
    bool m_highlighted = false;
    OpenSWMMVisLayer *m_ownerLayer = nullptr;
};

// ============================================================================
// RasterTileItem — for raster / WMS / WMTS image tiles
// ============================================================================
class RasterTileItem : public QGraphicsPixmapItem
{
public:
    /**
     * @brief Create unselectable-by-default pixmap tile scaled to target rect.
     * @param[in] pixmap Pixmap copied/shared by Qt base item.
     * @param[in] sceneRect Prepared target rectangle: position topLeft, independent X/Y scales from
     *   raw pixmap width/height.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Input geometry already in item/scene coordinates, no CRS projection or engine/OGR
     *   mutation. Identity and attributes are snapshots; ownerLayer initially null.
     */
    explicit RasterTileItem(const QPixmap &pixmap, const QRectF &sceneRect,
                            QGraphicsItem *parent = nullptr);

    /**
     * @return Stable RasterTileItemType custom QGraphicsItem discriminator, independent of engine
     *   subtype/feature ID. No mutation.
     */
    int type() const override { return RasterTileItemType; }

    /**
     * @param[in] pixmap New Qt pixmap copied/shared into item.
     * @param[in] sceneRect Prepared target rectangle, top-left position and raw width/height
     *   ratios.
     * @note Replaces pixmap/position; if pixmap dimensions positive replaces transform with X/Y
     *   scale, otherwise PREVIOUS transform retained. No aspect preservation, reprojection or layer
     *   notification.
     */
    void updateTile(const QPixmap &pixmap, const QRectF &sceneRect);

    /**
     * @param[in] layer Optional borrowed raw layer identity, must outlive any caller dereferencing
     *   it.
     * @brief Store owner association only; does not reparent/own item or layer, update
     *   geometry/style, connect signals or repaint.
     */
    void setOwnerLayer(OpenSWMMVisLayer *layer) { m_ownerLayer = layer; }
    /**
     * @return Borrowed raw associated layer or null, not deletion-guarded; no ownership transfer or
     *   validation.
     */
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
    /**
     * @brief Create default local graphics decoration item; no IgnoreTransformations or viewport
     *   anchoring configured here.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Host positions/flags it for desired screen behavior. No model/CRS ownership or signals.
     */
    explicit ScaleBarItem(QGraphicsItem *parent = nullptr);

    /**
     * @return Stable ScaleBarItemType custom QGraphicsItem discriminator, independent of engine
     *   subtype/feature ID. No mutation.
     */
    int type() const override { return ScaleBarItemType; }

    /**
     * @return Fixed local 200×30at origin regardless actual text/bar extent; does not measure
     *   current settings or prepareGeometryChange.
     */
    QRectF boundingRect() const override;
    /**
     * @param[in] painter Required active local-item painter; null not guarded when drawing.
     * @param[in] option Ignored borrowed graphics style option.
     * @param[in] widget Ignored borrowed widget.
     * @brief Draw1/2/5decade nice bar/ticks and configured label from metres-per-pixel scale. No
     *   CRS calculation/query performed here.
     * @note Painter state NOT saved/restored, caller guards. Nonpositive scale returns; caller
     *   supplies finite positive scale/max length to avoid log/division problems. Settings max
     *   length defaults100px; rounded label value drives bar length, min2pixels. Raw mode skips
     *   roundedMetres but still uses same numeric scale. Fixed boundingRect can be exceeded.
     */
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    /**
     * @param[in] mpp Raw map units per pixel copied without validation.
     * @brief Store legacy map-scale field only; current paint does NOT read it. No repaint/geometry
     *   update.
     */
    void setMapUnitsPerPixel(double mpp) { m_mapUnitsPerPixel = mpp; }
    /**
     * @param[in] mpp Physical metres/pixel (or raw scale when raw mode desired), positive finite
     *   expected.
     * @brief Store numeric scale used by paint without validation, recalculation or update();
     *   caller schedules repaint.
     */
    void setMetresPerPixel(double mpp) { m_metresPerPixel = mpp; }
    /**
     * @param[in] settings Optional borrowed RAW ScaleBarSettings, must outlive painting; null uses
     *   built-in pen/font/100pixel/metric defaults.
     * @brief Replace style pointer only, no ownership/connection/lifetime guard or repaint.
     */
    void setSettings(ScaleBarSettings *settings) { m_settings = settings; }
    /**
     * @param[in] raw True passes raw flag to label formatter and skips roundedMetres; false
     *   physical formatter.
     * @brief Store interpretation flag without converting numeric scale, changing units or
     *   requesting repaint.
     */
    void setRawCRS(bool raw) { m_rawCRS = raw; }

private:
    double m_mapUnitsPerPixel = 1.0;
    double m_metresPerPixel = 1.0; /*!< CRS-aware metres/pixel; set by caller. */
    bool m_rawCRS = false;
    ScaleBarSettings *m_settings = nullptr;
};

// ============================================================================
// CoordDisplayItem — always-visible coordinate readout (view coordinates)
// ============================================================================
class CoordDisplayItem : public QGraphicsItem
{
public:
    /**
     * @brief Create default local graphics decoration item; no IgnoreTransformations or viewport
     *   anchoring configured here.
     * @param[in] parent Optional QGraphicsItem parent owns item; otherwise scene/caller controls
     *   lifetime. GUI-thread access only.
     * @note Host positions/flags it for desired screen behavior. No model/CRS ownership or signals.
     */
    explicit CoordDisplayItem(QGraphicsItem *parent = nullptr);

    /**
     * @return Stable CoordDisplayItemType custom QGraphicsItem discriminator, independent of engine
     *   subtype/feature ID. No mutation.
     */
    int type() const override { return CoordDisplayItemType; }

    /**
     * @return Fixed local 250×20at origin regardless actual text/bar extent; does not measure
     *   current settings or prepareGeometryChange.
     */
    QRectF boundingRect() const override;
    /**
     * @param[in] painter Required active local-item painter; null not guarded when drawing.
     * @param[in] option Ignored borrowed graphics style option.
     * @param[in] widget Ignored borrowed widget.
     * @brief Draw X/Yfixed5decimals with9pointfont, translucent white background and black text at
     *   local origin. No CRS calculation/query performed here.
     * @note Painter state NOT saved/restored, caller guards. Background/text can extend outside
     *   fixed boundingRect, including above origin. No unit labels, projection, validity check or
     *   repaint request.
     */
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    /**
     * @param[in] x Raw current coordinate X to display, any CRS/units chosen by caller.
     * @param[in] y Raw coordinate Y, no scene inversion or reprojection here.
     * @brief Copy both values and update(), even equal; no finite check, signals or layer/engine
     *   change.
     */
    void setCoordinates(double x, double y);

private:
    double m_x = 0.0;
    double m_y = 0.0;
};

#endif // GRAPHICSITEMS_H
