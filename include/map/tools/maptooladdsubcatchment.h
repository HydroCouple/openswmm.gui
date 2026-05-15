/*!
 * \file   maptooladdsubcatchment.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Polygon draw tool for SWMM subcatchments.
 */

#ifndef MAPTOOLADDSUBCATCHMENT_H
#define MAPTOOLADDSUBCATCHMENT_H

#include "map/tools/maptool.h"

#include <QPointF>
#include <QString>
#include <QVector>

class SWMMModelLayer;

/*!
 * \class OpenSWMMVisMapToolAddSubcatchment
 * \brief Click-to-place polygon vertices; double-click to close and commit.
 *
 * Interaction:
 *   - Left-click → add vertex; rubber-band polygon updates.
 *   - Double-click → close polygon and commit (minimum 3 vertices).
 *   - Right-click → remove last vertex (or cancel if < 2 remain).
 *   - Enter / Return → commit (same as double-click).
 *   - Escape → cancel.
 */
class OpenSWMMVisMapToolAddSubcatchment : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolAddSubcatchment(MapCanvas *canvas,
                                               QObject   *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent  (QMouseEvent *event) override;
    void mouseMoveEvent   (QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent    (QKeyEvent   *event) override;
    void paint(QPainter *painter, const MapExtent &extent,
               const SpatialReferenceSystem *srs) override;

signals:
    void subcatchmentAdded(const QString &name);

private:
    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;
    void cancel();
    void commit();

    QVector<QPointF> m_vertices; // map coords of placed vertices
    QPointF          m_cursor;   // current cursor in map coords
    bool             m_drawing = false;
};

#endif // MAPTOOLADDSUBCATCHMENT_H
