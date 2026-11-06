/**
 * @file      maptoolpan.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Click-and-drag canvas pan tool.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /**
     * @param[in] canvas Optional borrowed raw canvas; must outlive tool and event/paint use.
     *   QObject parent ownership is separate.
     * @param[in] parent Optional QObject owner; caller owns tool if null.
     * @note GUI-thread only. Sets identity Pan, inactive/nonpanning; no drag started or view edit
     *   at construction.
     */
    explicit OpenSWMMVisMapToolPan(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return Closed hand while stored pan gesture active, otherwise open hand; does not set canvas
     *   cursor.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief Clear gesture flag then activate base; no view movement or direct cursor signal.
     */
    void activate() override;
    /**
     * @brief Clear gesture flag then deactivate base.
     * @note Does not call canvas endPan or create undo for an interrupted drag; host must manage
     *   any interrupted canvas pan session.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event; inspected without null guard.
     * @brief Arm left/middle drag, snapshot current extent/pixel and begin canvas pan.
     * @note Accepts matching button even without canvas, storing invalid default extent; with
     *   canvas sets closed hand. Other buttons untouched. No model edit.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event when active drag/live canvas; widget coordinates.
     * @brief Translate view by incremental pixel delta while panning, retaining last cursor point.
     * @note Does not check current button mask, accept event, sync extent or push undo here;
     *   delegates fast canvas translation.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed release; left/middle ends any armed pan.
     * @brief End pan, restore open hand and push one full-drag extent command if starting extent
     *   valid.
     * @note Canvas endPan synchronizes extent/tiles. Uses required canvas undoStack without null
     *   guard; initial redo may reapply extent and Qt can merge navigation commands. Does not
     *   accept release or skip zero displacement. Null canvas only clears gesture.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool m_panning = false;
    QPoint m_lastPixel;
    MapExtent m_panStartExtent; /*!< Extent at the start of the current drag (for undo). */
};

#endif // MAPTOOLPAN_H
