/*!
 * \file   maptoolselectpolygon.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Interactive polygon (lasso) feature-selection tool for SWMM layers.
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
 * \brief Selects SWMM model features enclosed by a user-drawn polygon.
 * \details Left-click drops polygon vertices on the map. The growing polygon is
 *          rendered as an overlay with a rubber-band edge tracking the cursor.
 *          Right-click, double-click, or Enter closes the polygon and selects
 *          every node, link, subcatchment, and rain gage whose representative
 *          point falls inside it. Escape cancels the in-progress polygon.
 *
 *          Holding Shift adds the enclosed objects to the current selection;
 *          holding Ctrl removes them. With no modifier the selection is
 *          replaced. Selection is applied through SWMMModelLayer so the
 *          Attribute Panel, Object Browser, and map highlight all stay in sync.
 */
class OpenSWMMVisMapToolSelectPolygon : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolSelectPolygon(MapCanvas *canvas,
                                             QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event)       override;
    void mouseMoveEvent(QMouseEvent *event)        override;
    void mouseReleaseEvent(QMouseEvent *event)     override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)           override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! Emitted after the selection changes on \p layer. */
    void selectionChanged(OpenSWMMVisLayer *layer);

private:
    /*! Close the polygon, run the spatial query, and apply the selection. */
    void finalizeSelection(Qt::KeyboardModifiers mods);
    /*! Discard the in-progress polygon without selecting. */
    void cancel();
    /*! Ask the canvas to repaint the overlay channel. */
    void requestRepaint();

    QVector<QPoint> m_vertices;          // committed vertices (widget pixels)
    QPoint          m_cursorPx;          // live cursor position (widget pixels)
    bool            m_haveCursor = false;
    QColor          m_lineColor  = QColor(0, 120, 255);
    QColor          m_fillColor  = QColor(0, 120, 255, 40);
};

#endif // MAPTOOLSELECTPOLYGON_H
