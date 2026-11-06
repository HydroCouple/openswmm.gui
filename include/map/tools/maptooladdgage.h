/**
 * @file      maptooladdgage.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Click-to-create tool for SWMM rain gages.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPTOOLADDGAGE_H
#define MAPTOOLADDGAGE_H

#include "map/tools/maptool.h"
#include <QString>

class SWMMModelLayer;

class OpenSWMMVisMapToolAddGage : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /**
     * @brief Create inactive Add Rain Gage tool without installing it or editing model.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events; QObject ownership
     *   separate.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread event/painter use only; constructor does not connect external
     *   selection/status consumers.
     */
    explicit OpenSWMMVisMapToolAddGage(MapCanvas *canvas, QObject *parent = nullptr);
    /**
     * @return Crosshair independent of hover/gesture state; no direct widget cursor change.
     */
    [[nodiscard]] QCursor cursor() const override;
    /**
     * @param[in] event Required borrowed mouse event; left press with live canvas handles
     *   placement.
     * @brief Transform clicked canvas point to first model layer, enqueue add-gage command and
     *   announce placement.
     * @note Chooses first model regardless of visibility/editability/active project. Transform
     *   status ignored using canvas values as fallback. Requires undo stack to actually create;
     *   absent stack deletes command without redo, yet selection, gageAdded and Scene/Overlay
     *   invalidation still happen. Engine add/default rejection is not checked before announcing.
     *   Does not accept event or apply snapping.
     */
    void mousePressEvent(QMouseEvent *event) override;

signals:
    /**
     * @param[in] name Generated desired ID; existence is not verified before signal.
     * @param[in] x Requested native layer X after attempted canvas transform.
     * @param[in] y Requested native layer Y.
     * @note Emitted after attempted command push and typed selection, even absent stack or rejected
     *   add. Consumer must query model if actual creation matters.
     */
    void gageAdded(const QString &name, double x, double y);

private:
    /**
     * @return Borrowed first SWMMModelLayer in canvas layer list, null no canvas/model. Does not
     *   check visibility, opacity, engine-open/editable state or selection despite helper name.
     */
    [[nodiscard]] SWMMModelLayer *activeModelLayer() const;
    /**
     * @param[in] layer Required live model with engine, borrowed during lookup.
     * @return First engine-absent raingage preference prefix+integer1..99999; unchecked prefix_X
     *   exhausted. No reservation or engine mutation.
     */
    [[nodiscard]] QString nextAvailableName(SWMMModelLayer *layer) const;
};

#endif // MAPTOOLADDGAGE_H
