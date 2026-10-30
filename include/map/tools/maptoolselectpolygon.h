/**
 * @file      maptoolselectpolygon.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Interactive polygon (lasso) selection for SWMM and vector layers.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MAPTOOLSELECTPOLYGON_H
#define MAPTOOLSELECTPOLYGON_H

#include "map/tools/maptool.h"

#include <QColor>
#include <QPoint>
#include <QVector>

class OpenSWMMVisLayer;

/*!
 * \class OpenSWMMVisMapToolSelectPolygon
 * \brief Selects SWMM model objects and vector features by a drawn polygon.
 * \details Left-click drops polygon vertices on the map. The growing polygon is
 *          rendered as an overlay with a rubber-band edge tracking the cursor.
 *          Right-click, double-click, or Enter closes the polygon and selects
 *          every node, link, subcatchment, and rain gage whose representative
 *          point falls inside it, plus GISVectorLayer features whose
 *          geometry intersects it. Editable FeatureLayer is not handled here. Escape cancels the
 *   in-progress polygon.
 *
 *          Holding Shift adds the enclosed objects to the current selection;
 *          holding Ctrl removes them. With no modifier the selection is
 *          replaced. Selection is applied through SWMMModelLayer and
 *          GISVectorLayer so the other views and map highlights stay in sync.
 */
class OpenSWMMVisMapToolSelectPolygon : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /**
     * @brief Create inactive pixel-vertex polygon-selection tool with blue line/fill style.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/event use.
     * @param[in] parent Optional QObject lifetime owner, otherwise caller owns tool.
     * @note GUI-thread only; no installation, layer edit, engine mutation or undo. Polygon stored
     *   in widget pixels until finalization, so navigation during capture changes map region
     *   selected.
     */
    explicit OpenSWMMVisMapToolSelectPolygon(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return Crosshair independent of polygon vertex count; no widget cursor mutation.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Clear pixel vertices/cursor flag then activate base; no selection clear or explicit
     *   overlay invalidation.
     */
    void activate() override;
    /**
     * @brief Cancel pending polygon with redraw when needed then deactivate base; existing layer
     *   selections retained.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event; left appends pixel vertex, right finalizes with
     *   current modifiers.
     * @brief Capture vertices or close polygon without hit test at press.
     * @note Captures even no canvas; finalization later cancels if unavailable. No
     *   duplicate/minimum-distance filtering, event acceptance or cursor-point append on right.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move when vertices exist.
     * @brief Update live pixel cursor and request Overlay repaint, without adding committed vertex
     *   or selecting.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Ignored borrowed event, may be null.
     * @brief No-op because polygon vertices committed on press; release does not select or accept
     *   event.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event; modifiers used, button/position ignored.
     * @brief Finalize EXISTING vertex list without appending double-click position.
     * @note Relies on any preceding press to capture endpoint; fewer than3vertices cancels. No
     *   button check or event acceptance.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key; Escape cancel, Enter/Return finalize with modifiers.
     * @brief Cancel/finalize pending polygon and accept handled keys; others unchanged. Live cursor
     *   is never appended by Enter.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @param[in] painter Required active widget-pixel painter when nonempty; null not guarded then.
     * @param[in] canvasExtent Unused current canvas extent.
     * @param[in] canvasSRS Unused borrowed CRS.
     * @brief Draw translucent closed preview, solid committed edges, dashed cursor/closing edges
     *   and6pixelsquarehandles.
     * @note Saves/restores antialias/pen/brush state, no DPR/transform reset. Geometry remains
     *   widget pixels; no query/model mutation or repaint emitted.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! Emitted after the selection changes on \p layer.
     * @param[in] layer Borrowed affected supported layer; consumer guards if retaining.
     * @note GIS vector emits only when ID set changes; model emits after every finalization setter
     *   even if normalized selection unchanged. Hidden/unsupported layers do not emit; no
     *   project-wide SelectionManager update explicitly performed here.
     */
    void selectionChanged(OpenSWMMVisLayer *layer);

private:
    /*! Close the polygon, run the spatial query, and apply the selection.
     * @param[in] mods Control subtracts before Shift union, otherwise Replace; other bits ignored.
     * @note No canvas/<3committed vertices cancels. Converts current pixel list through current
     *   canvas extent, then every VISIBLE GISVectorLayer or SWMMModelLayer; editable
     *   FeatureLayer/mesh/results/raster unsupported here despite prior summary. Opacity/global
     *   show-kind not independently filtered.
     * @note GIS polygon geometry query receives canvas CRS; model per-kind queries use node/gage
     *   point and link/catch BBOX CENTER with cached inverse transform/hidden-name filtering, not
     *   full line/polygon intersection. Selection preserves typed kind bits when same names span
     *   SWMM namespaces.
     * @note Applies layer selection setters and signals, clears capture and requests Overlay. No
     *   geometry/engine writes, undo or direct Scene invalidation; downstream layer-selection
     *   notifications handle highlights.
     */
    void finalizeSelection(Qt::KeyboardModifiers mods);
    /*! Discard the in-progress polygon without selecting.
     * @note Clears pixel vertices/cursor flag without touching layer selections. Requests Overlay
     *   only if vertices existed; safe repeatedly/no canvas. No cancelled/result signal or tool
     *   switch.
     */
    void cancel();
    /*! Ask the canvas to repaint the overlay channel.
     * @note Invalidate canvas Overlay with select-polygon reason when canvas lives; null no-op.
     *   Does not synchronously paint or update scene/selection.
     */
    void requestRepaint();

    QVector<QPoint> m_vertices; // committed vertices (widget pixels)
    QPoint m_cursorPx;          // live cursor position (widget pixels)
    bool m_haveCursor = false;
    QColor m_lineColor = QColor(0, 120, 255);
    QColor m_fillColor = QColor(0, 120, 255, 40);
};

#endif // MAPTOOLSELECTPOLYGON_H
