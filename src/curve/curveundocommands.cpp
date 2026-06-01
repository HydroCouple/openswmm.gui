/*!
 * \file   curveundocommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "curve/curveundocommands.h"

namespace openswmmvis::curve {

BulkSetCurvePointsCommand::BulkSetCurvePointsCommand(
        CurveProvider *provider,
        QVector<int> indices,
        QVector<CurvePoint> oldPoints,
        QVector<CurvePoint> newPoints,
        QString description,
        QUndoCommand *parent)
    : QUndoCommand(description, parent)
    , m_provider(provider)
    , m_indices(std::move(indices))
    , m_oldPoints(std::move(oldPoints))
    , m_newPoints(std::move(newPoints))
{
}

void BulkSetCurvePointsCommand::apply_(const QVector<CurvePoint> &target)
{
    if (!m_provider) return;
    // The order of writes matters when an X-shift would temporarily violate
    // monotonicity against a not-yet-shifted neighbour. The provider's
    // setPointAt validates against neighbours and refuses such an intermediate
    // state. A forward pass + reverse pass resolves both ascending and
    // descending shifts without surfacing transient rejections.
    const int n = m_indices.size();
    for (int k = 0; k < n; ++k) {
        const int idx = m_indices.at(k);
        const CurvePoint &p = target.at(k);
        m_provider->setPointAt(idx, p.x, p.y, nullptr);
    }
    for (int k = n - 1; k >= 0; --k) {
        const int idx = m_indices.at(k);
        const CurvePoint &p = target.at(k);
        m_provider->setPointAt(idx, p.x, p.y, nullptr);
    }
}

void BulkSetCurvePointsCommand::undo() { apply_(m_oldPoints); }
void BulkSetCurvePointsCommand::redo() { apply_(m_newPoints); }

} // namespace openswmmvis::curve
