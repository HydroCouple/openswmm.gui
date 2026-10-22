/**
 * @file      curveundocommands.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QUndoCommand subclasses for graphical edits on a CurveProvider.
 *
 * Scope (per user direction): undo coverage is intentionally limited to the
 * **graphical** edits driven from `CurveEditChartView` (drag completion).
 * Right-click insert / delete on the chart and table-cell edits continue to
 * mutate the provider directly — matching the existing curve-editor
 * behaviour. Adding undo for those code paths is a future slice.
 *
 * Live in-flight drag uses `CurveProvider::setPointAt` per frame (validated,
 * MVC signals fire so every view re-renders). On mouse release the chart
 * view rewinds the provider to its pre-drag state then pushes a single
 * `BulkSetCurvePointsCommand` so Cmd-Z reverts the entire drag in one step.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CURVE_CURVEUNDOCOMMANDS_H
#define OPENSWMMVIS_CURVE_CURVEUNDOCOMMANDS_H

#include "curve/curveprovider.h"

#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::curve
{

/*! \brief Set point coordinates using validated sequential mutations. Indices and the
 *  pre/post coordinate vectors must be the same length. Out-of-range
 *  indices on apply are skipped. */
class BulkSetCurvePointsCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture coordinate replacements for one undoable graphical drag.
     * @param[in] provider Borrowed raw pointer that must outlive command use; null makes apply a no-op.
     * @param[in] indices Zero-based existing point indices.
     * @param[in] oldPoints Old coordinates parallel to indices.
     * @param[in] newPoints New coordinates parallel to indices.
     * @param[in] description User-visible undo text.
     * @param[in] parent Optional command owner.
     * @details Uses sequential provider calls in forward then reverse order; rejected intermediate moves may emit mutationRejected() and are not rolled back.
     */
    BulkSetCurvePointsCommand(CurveProvider *provider, QVector<int> indices,
                              QVector<CurvePoint> oldPoints, QVector<CurvePoint> newPoints,
                              QString description, QUndoCommand *parent = nullptr);
    /**
     * @brief Apply captured old coordinates with ordinary provider validation and notifications.
     */
    void undo() override;
    /**
     * @brief Apply captured new coordinates with ordinary provider validation and notifications.
     */
    void redo() override;

private:
    /**
     * @brief Try the requested coordinates in forward then reverse index order.
     * @param[in] target Coordinate vector containing one entry per stored index.
     * @details Individual setPointAt() results are ignored. Invalid indices are rejected by the provider; partial changes persist and a non-null provider must remain alive.
     */
    void apply_(const QVector<CurvePoint> &target);

    CurveProvider *m_provider;
    QVector<int> m_indices;
    QVector<CurvePoint> m_oldPoints;
    QVector<CurvePoint> m_newPoints;
};

} // namespace openswmmvis::curve

#endif // OPENSWMMVIS_CURVE_CURVEUNDOCOMMANDS_H
