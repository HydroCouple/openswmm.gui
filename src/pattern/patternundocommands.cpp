/*!
 * \file   patternundocommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "pattern/patternundocommands.h"

namespace openswmmvis::pattern {

BulkSetPatternFactorsCommand::BulkSetPatternFactorsCommand(
        PatternProvider *provider,
        QVector<int> indices,
        QVector<double> oldValues,
        QVector<double> newValues,
        QString description,
        QUndoCommand *parent)
    : QUndoCommand(description, parent)
    , m_provider(provider)
    , m_indices(std::move(indices))
    , m_oldValues(std::move(oldValues))
    , m_newValues(std::move(newValues))
{
}

void BulkSetPatternFactorsCommand::apply_(const QVector<double> &target)
{
    if (!m_provider) return;
    for (int k = 0; k < m_indices.size(); ++k)
        m_provider->setFactor(m_indices.at(k), target.at(k), nullptr);
}

void BulkSetPatternFactorsCommand::undo() { apply_(m_oldValues); }
void BulkSetPatternFactorsCommand::redo() { apply_(m_newValues); }

// ─────────────────────────────────────────────────────────────────────────────

SwapPatternFactorsCommand::SwapPatternFactorsCommand(PatternProvider *provider,
                                                     int i, int j,
                                                     QString description,
                                                     QUndoCommand *parent)
    : QUndoCommand(description, parent)
    , m_provider(provider)
    , m_i(i)
    , m_j(j)
{
}

void SwapPatternFactorsCommand::undo()
{
    if (m_provider) m_provider->swapFactors(m_i, m_j, nullptr);
}

void SwapPatternFactorsCommand::redo()
{
    if (m_provider) m_provider->swapFactors(m_i, m_j, nullptr);
}

} // namespace openswmmvis::pattern
