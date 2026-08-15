/*!
 * \file   maptoolidentify.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
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
struct IdentifyResult
{
    QString              layerName;    /*!< Name of the layer that returned results. */
    QList<QVariantMap>   features;     /*!< One map per matching feature/element. */
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

    Q_PROPERTY(double   mapTolerance READ mapTolerance WRITE setMapTolerance
               NOTIFY mapToleranceChanged)
    Q_PROPERTY(bool     queryAllLayers READ queryAllLayers WRITE setQueryAllLayers
               NOTIFY queryAllLayersChanged)

public:

    explicit OpenSWMMVisMapToolIdentify(MapCanvas *canvas, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    // ----- Configuration -------------------------------------------------

    /*!
     * \brief Returns the search tolerance in map units (default 1e-6).
     */
    [[nodiscard]] double mapTolerance()     const;
    void setMapTolerance(double tolerance);

    /*!
     * \brief When true, all visible layers are queried; otherwise only the top-most.
     */
    [[nodiscard]] bool   queryAllLayers()   const;
    void setQueryAllLayers(bool all);

    // ----- Tool interface ------------------------------------------------

    void activate()   override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    void mapToleranceChanged(double tolerance);
    void queryAllLayersChanged(bool all);

    /*!
     * \brief Emitted after a click with the list of identify results (one per layer).
     */
    void identifyResult(const QList<IdentifyResult> &results);

private:
    double  m_tolerance    = 1e-6;
    bool    m_queryAll     = true;
    double  m_lastX        = 0.0;
    double  m_lastY        = 0.0;
    bool    m_hasResult    = false;
};

#endif // MAPTOOLIDENTIFY_H
