/**
 * @file      maptoolmeasure.h
 * @brief     Interactive tool for measuring geodesic distances or areas on the map.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPTOOLMEASURE_H
#define MAPTOOLMEASURE_H

#include "map/tools/maptool.h"
#include "core/measurementunitmanager.h"

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QVector>

class OpenSWMMVisWorkspace;
class OGRCoordinateTransformation;

/*!
 * \enum MeasureMode
 * \brief Selects whether the measure tool computes distance or area.
 */
enum class MeasureMode {
    Distance, /*!< Cumulative geodesic line distance. */
    Area,     /*!< Geodesic polygon area. */
};

/*!
 * \class OpenSWMMVisMapToolMeasure
 * \brief Interactive tool for measuring geodesic distances or areas on the map.
 *
 * Measurements are CRS-aware:
 *  - Geographic CRS (lon/lat): coordinates are transformed to WGS-84 then
 *    the Haversine formula (R = 6 378 137 m) is applied.
 *  - Projected CRS: Euclidean distance in CRS linear units × linearUnitsToMetres().
 *  - Local / undefined CRS: raw Euclidean, still converted and labelled using selected units.
 *
 * Distance mode: shows a cumulative label at every committed vertex.
 * Area mode: shows a semi-transparent polygon fill while drawing.
 * Double-click finalises the measurement; subsequent left-click starts a new one.
 * Escape clears all points; Backspace/Delete remove the last pending vertex.
 */
class OpenSWMMVisMapToolMeasure : public OpenSWMMVisMapTool
{
    Q_OBJECT

    Q_PROPERTY(MeasureMode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(double total READ total NOTIFY totalChanged)

public:
    /**
     * @brief Create inactive distance tool, read display preferences and connect live
     *   preference/CRS updates.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool; actual mouse conversion
     *   requires live canvas.
     * @param[in] parent Optional QObject owner; otherwise caller owns tool.
     * @note GUI-thread only. Defaults metres/square metres, no initial WGS84 transform until
     *   activation or CRS-change signal. Decorations/MeasureTool preference changes reload style
     *   and redraw existing geometry. CRS changes rebuild transform/recalculate existing stored
     *   coordinates WITHOUT reprojecting those vertices, so caller normally clears measurement when
     *   changing canvas frame. No engine/layer edits.
     */
    explicit OpenSWMMVisMapToolMeasure(MapCanvas *canvas, QObject *parent = nullptr);
    /**
     * @brief Destroy owned OGR transform and QObject connections; does not clear host active-tool
     *   pointer or emit measurementCleared.
     */
    ~OpenSWMMVisMapToolMeasure() override;

    /**
     * @return Crosshair independent of measure mode/finalized state; no widget cursor mutation.
     */
    [[nodiscard]] QCursor cursor() const override;

    // ----- Mode & units --------------------------------------------------

    /**
     * @return Stored mode, Distance initially; no calculation or mutation.
     */
    [[nodiscard]] MeasureMode mode() const;
    /**
     * @param[in] mode Valid MeasureMode enum; equality no-op, no enum validation.
     * @brief Change mode, clear measurement (including zero-total/cleared signals), then emit
     *   modeChanged.
     * @note Keeps unit/style settings and tool activation. No preferences persistence.
     */
    void setMode(MeasureMode mode);

    /**
     * @return Stored distance display/conversion unit, Metres initially, independent of canvas CRS
     *   linear units.
     */
    [[nodiscard]] MeasurementUnitManager::DistanceUnit distanceUnit() const;
    /**
     * @param[in] unit Supported distance unit enum; equality no-op, no enum validation.
     * @brief Store unit; in Distance mode recalculate committed total and redraw Overlay, then emit
     *   distanceUnitChanged.
     * @note In Area mode does not recalculate/redraw, only stores/emits. No clear, geometric
     *   reprojection, automatic magnitude unit switching or preference persistence.
     */
    void setDistanceUnit(MeasurementUnitManager::DistanceUnit unit);

    /**
     * @return Stored area display/conversion unit, SquareMetres initially; no automatic hectare/km²
     *   selection.
     */
    [[nodiscard]] MeasurementUnitManager::AreaUnit areaUnit() const;
    /**
     * @param[in] unit Supported area unit enum; equality no-op, no enum validation.
     * @brief Store unit; in Area mode recalculate committed total and redraw Overlay, then emit
     *   areaUnitChanged.
     * @note Distance mode only stores/emits. No geometry clear/transform or preferences
     *   persistence.
     */
    void setAreaUnit(MeasurementUnitManager::AreaUnit unit);

    /*!
     * \brief Returns the cumulative measured value in the selected unit.
     * @return Cached COMMITTED-vertex distance/area in selected unit; excludes live rubber cursor
     *   even when painted label includes it. Zero after clear; nonfinite input/calculation can
     *   propagate.
     * @note Projected/geographic helpers produce metres/m²; local/undefined CRS raw units are still
     *   passed through metre conversion and labelled with selected units, so caller must ensure
     *   scale meaningful.
     */
    [[nodiscard]] double total() const;

    // ----- Actions -------------------------------------------------------

    /**
     * @brief Clear vertices/cumulative distances, set total0 and unfinalize while leaving tool
     *   mode/units/transform active.
     * @note Always emits totalChanged(0) then measurementCleared, even already empty, and
     *   invalidates Overlay on canvas. Mouse position retained; no status/engine changes.
     */
    void clearMeasurement();

    // ----- Tool interface ------------------------------------------------

    /**
     * @brief Rebuild geographic-to-WGS84 transform, clear measurement/signals/redraw, then activate
     *   base. No result preserved between activations.
     */
    void activate() override;
    /**
     * @brief Clear measurement/signals/redraw, destroy transform and deactivate base. No finalized
     *   result persists after deactivation.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event; left only. Live canvas REQUIRED for initialized
     *   coordinate conversion, despite optional construction.
     * @brief Append clicked canvas-map coordinate; if finalized, clear prior result first;
     *   recalculate and redraw Overlay.
     * @note No snapping, duplicate suppression, event acceptance or live mouse-position reset.
     *   Without canvas helper leaves local outputs uninitialized; host must not dispatch mouse
     *   events detached.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event and live canvas for coordinate conversion.
     * @brief Update canvas-map live mouse position, redrawing only if pending vertices and not
     *   finalized.
     * @note Does not alter total/cumulative committed geometry or emit totalChanged; event not
     *   accepted.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event on nonempty/unfinalized measurement, live canvas
     *   required; button is not checked.
     * @brief Append endpoint only if exact-different from last point, mark finalized, recalculate
     *   and redraw.
     * @note No minimum distinct vertices/area validity test beyond nonempty. Unlike press, this
     *   handles any button delivered as double-click. Event not accepted; next left press starts
     *   new measurement.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key event; no null guard.
     * @brief Escape clears entire measurement; Backspace/Delete remove last vertex only while
     *   nonfinalized then recalculate/redraw.
     * @note Other keys ignored, no acceptance/base forwarding or tool switch. Empty removal no-op.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @param[in] painter Required active widget-pixel painter when canvas/nonempty geometry exists;
     *   null not guarded then.
     * @param[in] canvasExtent Unused current canvas extent.
     * @param[in] canvasSRS Unused borrowed current CRS; calculations query canvas directly.
     * @brief Draw measurement path/area, vertex handles, committed cumulative labels and live
     *   rubber labels using current preferences.
     * @note Area closes polygon for fill/area calculation, draws live cursor when pending;
     *   finalized fill opacity doubled/capped. Distance labels thin when>20vertices, ghost shows
     *   latest segment+running total. Live labels do not update total(). No clipping/DPR/transform
     *   reset.
     * @note Geometric drawing uses local save/restore, but label code leaves painter font/pen
     *   changed; caller wraps outer painter state. Does not mutate geometry or request repaint.
     *   Mouse position can be previous/default point before first move after press/clear.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /**
     * @param[in] mode New stored valid-mode request.
     * @note After mode-change clear emitted totalChanged0/measurementCleared. Synchronous GUI
     *   signal, no engine/layer mutation.
     */
    void modeChanged(MeasureMode mode);
    /**
     * @param[in] unit New stored distance unit.
     * @note After applicable distance recalculation/totalChanged and invalidation. Synchronous GUI
     *   signal, no engine/layer mutation.
     */
    void distanceUnitChanged(MeasurementUnitManager::DistanceUnit unit);
    /**
     * @param[in] unit New stored area unit.
     * @note After applicable area recalculation/totalChanged and invalidation. Synchronous GUI
     *   signal, no engine/layer mutation.
     */
    void areaUnitChanged(MeasurementUnitManager::AreaUnit unit);
    /**
     * @param[in] total Cached committed total in currently selected mode/unit, including zero or
     *   nonfinite result.
     * @note Every recalculate/clear emits, without equality suppression; mouse-move live preview
     *   does not. Synchronous GUI signal, no engine/layer mutation.
     */
    void totalChanged(double total);
    /**
     * @note Every clear emits even already empty, including activate/deactivate/mode
     *   change/Escape/new post-finalization measurement. Synchronous GUI signal, no engine/layer
     *   mutation.
     */
    void measurementCleared();

private:
    /**
     * @brief Rebuild cumulative segment distances for committed vertices and selected-unit total,
     *   then emit totalChanged unconditionally.
     * @note Distance sum excludes cursor and polygon closure; Area uses closed committed ring.
     *   Computes distance vector even Area mode. No geometric validation, equality suppression or
     *   repaint here; local/raw values still treated as metre/m² inputs to unit conversion.
     */
    void recalculate();
    /**
     * @brief Destroy old transform and, only for geographic canvas CRS, attempt owned transform to
     *   EPSG4326 WGS84.
     * @note Includes identity geographic source; null/nongeographic canvas leaves null. Creation
     *   failure silent and geographic calculations then use raw coordinates as degrees. Does not
     *   reproject stored vertices or recalculate itself.
     */
    void rebuildTransform();
    /**
     * @brief Destroy owned GDAL coordinate transform if present and set pointer null; repeated
     *   calls safe.
     * @note Does not change vertices/units or emit signals. GUI-thread access serialized with
     *   calculations.
     */
    void clearTransform();
    /**
     * @brief Copy measure line/fill colours, font family/size, label decimals and fill opacity from
     *   preferences.
     * @note No local validation, recalculation, repaint or unit change;
     *   constructor/preference-change caller handles redraw.
     */
    void applyPreferences();

    /**
     * @param[in] a First canvas-map point in current canvas CRS, finite coordinates expected.
     * @param[in] b Second point in same frame.
     * @return Projected Euclidean distance times linearUnitsToMetres, geographic Haversine on
     *   radius6378137m sphere after optional transform, otherwise raw Euclidean units. No
     *   elevation/geodesic ellipsoid or mutation.
     * @note Optional transform return statuses ignored; no longitude/latitude range, finite or
     *   haversine-roundoff clamp. Local CRS units not converted even if wrapper advertises linear
     *   scale.
     */
    double geodesicDistance(const QPointF &a, const QPointF &b) const;
    /**
     * @param[in] vertices Ordered canvas-map ring,>=3entries for nonzero; implicit last-to-first
     *   closure, may repeat first.
     * @return Absolute planar shoelace area times projected linear-scale squared; geographic
     *   spherical trapezoid sum on radius6378137m sphere after optional transforms; raw planar area
     *   for local/absent CRS. Fewer than3returns0.
     * @note No ellipsoidal/geographic polygon validity, antimeridian longitude unwrapping, hole
     *   subtraction or self-intersection repair. Transform statuses ignored, nonfinite propagates.
     *   Absolute value loses winding; no source mutation.
     */
    double geodesicArea(const QVector<QPointF> &vertices) const;

    // Formats a metre value in the current distance unit: "142.30 m"
    /**
     * @param[in] metres Distance-helper numeric result; ordinary projected/geographic result in
     *   metres, local raw value treated as metres.
     * @return Fixed decimal QString value+selected unit symbol, using preference decimals and
     *   direct unit factor. No locale grouping or automatic kilometre selection; no mutation.
     */
    QString formatDist(double metres) const;
    // Formats a square-metre value in the current area unit: "3 847.20 m²"
    /**
     * @param[in] sqm Area-helper numeric result; ordinary result m², local raw value treated as m².
     * @return Fixed decimal value+selected area symbol, no locale grouping/automatic hectare or km²
     *   switching. Uses preference decimals; no mutation.
     */
    QString formatArea(double sqm) const;

    MeasureMode m_mode = MeasureMode::Distance;
    MeasurementUnitManager::DistanceUnit m_distanceUnit = MeasurementUnitManager::Metres;
    MeasurementUnitManager::AreaUnit m_areaUnit = MeasurementUnitManager::SquareMetres;

    QVector<QPointF> m_vertices;     /*!< In canvas-CRS coordinates. */
    QVector<double> m_segmentMetres; /*!< Cumulative metres at each vertex. */
    QPointF m_mousePos;              /*!< Current (moving) mouse position. */
    double m_total = 0.0;
    bool m_finalized = false;

    // Cached display preferences (updated via applyPreferences())
    QColor m_lineColor{Qt::red};
    QFont m_labelFont{QStringLiteral("sans-serif"), 8};
    int m_labelDecimals{2};
    QColor m_fillColor{100, 149, 237};
    int m_fillOpacity{30};

    OGRCoordinateTransformation *m_toWgs84 = nullptr; /*!< Owned; null for projected/local CRS. */
};

#endif // MAPTOOLMEASURE_H
