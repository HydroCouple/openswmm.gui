/*!
 * \file   maptoolpan.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#ifndef MAPTOOLPAN_H
#define MAPTOOLPAN_H

#include "map/tools/maptool.h"
#include "map/mapextent.h"

#include <QPoint>

/*!
 * \class OpenSWMMVisMapToolPan
 * \brief Tool that pans the map canvas by click-dragging.
 * \details On mouse press the tool records the anchor point.  As the mouse moves,
 *          the canvas extent is shifted so the anchor follows the cursor.
 *          Releasing the mouse finalises the pan and pushes an undo command.
 */
class OpenSWMMVisMapToolPan : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:

    explicit OpenSWMMVisMapToolPan(MapCanvas *canvas, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event)   override;
    void mouseMoveEvent(QMouseEvent *event)    override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool      m_panning        = false;
    QPoint    m_lastPixel;
    MapExtent m_panStartExtent; /*!< Extent at the start of the current drag (for undo). */
};

#endif // MAPTOOLPAN_H
