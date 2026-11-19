/**
 * @file      maptoolidentify.h
 * @brief     Holds identify results from a single layer.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPTOOLIDENTIFY_H
#define MAPTOOLIDENTIFY_H

#include "map/tools/maptool.h"

#include <QList>
#include <QVariantMap>

/*!
 * \struct IdentifyResult
 * \brief Holds identify results from a single layer.
 */
struct IdentifyResult {
    QString layerName;           /*!< Name of the layer that returned results. */
    QList<QVariantMap> features; /*!< One map per matching feature/element. */
};

/*!
 * \class OpenSWMMVisMapToolIdentify
 * \brief Tool that queries all visible layers at a clicked point and reports attributes.
 * \details On mouse press the tool calls identifyAt() on every active GIS and
 *          SWMM model layer, collects the attribute maps, and emits identifyResult().
 *          The PropertiesPanel widget listens to this signal to populate itself.
 *          A cross-hair overlay is painted at the queried point until the next
 *          identify action or until the tool is deactivated.
 */
class OpenSWMMVisMapToolIdentify : public OpenSWMMVisMapTool
{
    Q_OBJECT

    Q_PROPERTY(
        double mapTolerance READ mapTolerance WRITE setMapTolerance NOTIFY mapToleranceChanged)
    Q_PROPERTY(bool queryAllLayers READ queryAllLayers WRITE setQueryAllLayers NOTIFY
                   queryAllLayersChanged)

public:
    /**
     * @brief Create inactive tool without installing on canvas or editing layers.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/event/paint use.
     * @param[in] parent Optional QObject lifetime owner; otherwise caller owns tool.
     * @note All methods/events serialized on GUI thread. Construction does not wire result/status
     *   consumers.
     */
    explicit OpenSWMMVisMapToolIdentify(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return Qt WhatsThis cursor independent of hit; no widget mutation.
     */
    [[nodiscard]] QCursor cursor() const override;

    // ----- Configuration -------------------------------------------------

    /*!
     * \brief Returns the search tolerance in map units (default 1e-6).
     * @return Raw configured tolerance, default1e-6canvas-map units, not a fixed pixel radius.
     *   Setter can store negative/nonfinite values.
     */
    [[nodiscard]] double mapTolerance() const;
    /**
     * @param[in] tolerance Raw map-unit query radius; caller supplies finite nonnegative meaningful
     *   value.
     * @brief Store tolerance and emit mapToleranceChanged unless qFuzzyCompare says equal.
     * @note No clamp, new identify query, result clear, repaint or persistence. Delegated layer
     *   queries interpret tolerance using their own CRS/cache rules.
     */
    void setMapTolerance(double tolerance);

    /*!
     * \brief When true, all visible layers are queried; otherwise stop at the top-most supported
     *   layer that returns a hit.
     * @return True queries every supported visible layer, false stops at FIRST layer yielding a
     *   nonempty result; upper visible layers with no hit are still queried/skipped.
     */
    [[nodiscard]] bool queryAllLayers() const;
    /**
     * @param[in] all Target query mode; equality no-op.
     * @brief Store query breadth and emit queryAllLayersChanged on transition. Does not rerun/clear
     *   previous identify or repaint.
     */
    void setQueryAllLayers(bool all);

    // ----- Tool interface ------------------------------------------------

    /**
     * @brief Hide stored marker by clearing hasResult then activate base; no result signal or
     *   explicit overlay redraw.
     */
    void activate() override;
    /**
     * @brief Hide marker and deactivate base; coordinates retained and no result-cleared signal or
     *   direct overlay invalidation.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed left mouse event; null is not guarded.
     * @brief Query clicked canvas location across supported visible layers from highest stack index
     *   downward.
     * @note Only GISVectorLayer and SWMMModelLayer recognized; no opacity/scale/show-kind gating
     *   beyond delegated queries. Vector queries receive the canvas SRS and tolerance in canvas
     *   units; model queries use their prepared cached inverse. Model results are lightweight
     *   identity maps, not full static properties.
     * @note Omits empty result layers; false queryAll stops at first hit, not first visible layer.
     *   Emits identifyResult even empty, sets marker present even miss, then invalidates Overlay.
     *   No model selection/edit or undo; event not accepted.
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @param[in] painter Required active widget-pixel painter if marker and canvas exist; null not
     *   guarded then.
     * @param[in] canvasExtent Unused current canvas extent.
     * @param[in] canvasSRS Unused borrowed CRS.
     * @brief Draw red two-pixel8pixelcross and4pixelcircle at last clicked canvas point, including
     *   misses.
     * @note Saves/restores state, no transform/DPR reset, result query or repaint request.
     *   Hidden/idle marker no-op.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /**
     * @param[in] tolerance Raw newly stored value; not guaranteed finite/nonnegative.
     * @note Emitted only on setter fuzzy-difference, before any subsequent click query; not new
     *   identify result.
     */
    void mapToleranceChanged(double tolerance);
    /**
     * @param[in] all New stored query breadth flag.
     * @note Emitted only on mode transition, no query/result reset performed automatically.
     */
    void queryAllLayersChanged(bool all);

    /*!
     * \brief Emitted after a click with the list of identify results (one per layer).
     * @param[in] results Value results in descending layer-stack hit order, only supported layers
     *   with nonempty maps; can be empty.
     * @note Synchronous click completion; string layerName is display name, not unique source
     *   identity. Consumer copies before independent retention. Does not select model objects or
     *   include unsupported raster/results/feature-layer types.
     */
    void identifyResult(const QList<IdentifyResult> &results);

private:
    double m_tolerance = 1e-6;
    bool m_queryAll = true;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    bool m_hasResult = false;
};

#endif // MAPTOOLIDENTIFY_H
