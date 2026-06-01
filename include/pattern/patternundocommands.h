/*!
 * \file   patternundocommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QUndoCommand subclasses for graphical edits on a PatternProvider.
 *
 * Scope (per user direction): undo coverage is intentionally limited to the
 * **graphical** edits driven from `PatternEditChartView` (vertical drag for
 * factor edit, horizontal drag for slot reorder). Table-cell edits and the
 * Normalize action continue to mutate the provider directly — matching the
 * existing pattern-editor behaviour.
 */
#ifndef OPENSWMMVIS_PATTERN_PATTERNUNDOCOMMANDS_H
#define OPENSWMMVIS_PATTERN_PATTERNUNDOCOMMANDS_H

#include "pattern/patternprovider.h"

#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::pattern {

/*! \brief Set one or more factor values atomically. \p indices and the
 *  pre/post value vectors must be the same length. */
class BulkSetPatternFactorsCommand : public QUndoCommand
{
public:
    BulkSetPatternFactorsCommand(PatternProvider *provider,
                                 QVector<int> indices,
                                 QVector<double> oldValues,
                                 QVector<double> newValues,
                                 QString description,
                                 QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    void apply_(const QVector<double> &target);

    PatternProvider  *m_provider;
    QVector<int>      m_indices;
    QVector<double>   m_oldValues;
    QVector<double>   m_newValues;
};

/*! \brief Swap two slots. Used by the chart view's horizontal-reorder drag. */
class SwapPatternFactorsCommand : public QUndoCommand
{
public:
    SwapPatternFactorsCommand(PatternProvider *provider,
                              int i, int j,
                              QString description,
                              QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    PatternProvider *m_provider;
    int m_i;
    int m_j;
};

} // namespace openswmmvis::pattern

#endif // OPENSWMMVIS_PATTERN_PATTERNUNDOCOMMANDS_H
