/*!
 * \file   curveundocommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QUndoCommand subclasses for graphical edits on a CurveProvider.
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
 */
#ifndef OPENSWMMVIS_CURVE_CURVEUNDOCOMMANDS_H
#define OPENSWMMVIS_CURVE_CURVEUNDOCOMMANDS_H

#include "curve/curveprovider.h"

#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::curve {

/*! \brief Set one or more point coordinates atomically. Indices and the
 *  pre/post coordinate vectors must be the same length. Out-of-range
 *  indices on apply are skipped. */
class BulkSetCurvePointsCommand : public QUndoCommand
{
public:
    BulkSetCurvePointsCommand(CurveProvider *provider,
                              QVector<int> indices,
                              QVector<CurvePoint> oldPoints,
                              QVector<CurvePoint> newPoints,
                              QString description,
                              QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    void apply_(const QVector<CurvePoint> &target);

    CurveProvider       *m_provider;
    QVector<int>         m_indices;
    QVector<CurvePoint>  m_oldPoints;
    QVector<CurvePoint>  m_newPoints;
};

} // namespace openswmmvis::curve

#endif // OPENSWMMVIS_CURVE_CURVEUNDOCOMMANDS_H
