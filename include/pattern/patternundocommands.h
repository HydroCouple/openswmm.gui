/**
 * @file      patternundocommands.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QUndoCommand subclasses for graphical edits on a PatternProvider.
 *
 * Scope (per user direction): undo coverage is intentionally limited to the
 * **graphical** edits driven from `PatternEditChartView` (vertical drag for
 * factor edit, horizontal drag for slot reorder). Table-cell edits and the
 * Normalize action continue to mutate the provider directly — matching the
 * existing pattern-editor behaviour.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PATTERN_PATTERNUNDOCOMMANDS_H
#define OPENSWMMVIS_PATTERN_PATTERNUNDOCOMMANDS_H

#include "pattern/patternprovider.h"

#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::pattern
{

/*! \brief Set one or more factor values through sequential provider mutations. \p indices and the
 *  pre/post value vectors must be the same length. */
class BulkSetPatternFactorsCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture old/new factor values for an undoable graphical edit.
     * @param[in] provider Borrowed provider that must outlive command use; null makes application a no-op.
     * @param[in] indices Zero-based factor indices, one per value in both vectors.
     * @param[in] oldValues Dimensionless values restored by undo().
     * @param[in] newValues Dimensionless values applied by redo().
     * @param[in] description User-visible undo text.
     * @param[in] parent Optional QUndoCommand owner.
     * @details Application is sequential with ordinary per-factor notifications; a rejected value does not roll back earlier factors.
     */
    BulkSetPatternFactorsCommand(PatternProvider *provider, QVector<int> indices,
                                 QVector<double> oldValues, QVector<double> newValues,
                                 QString description, QUndoCommand *parent = nullptr);
    /** @brief Restore the captured old factors (bulk) or swap the captured slots again; provider signals and attached-engine mirroring apply. */
    void undo() override;
    /** @brief Apply captured new factors (bulk) or swap the captured slots; failed provider mutations are not rolled back. */
    void redo() override;

private:
    /**
     * @brief Apply one captured side of the graphical edit.
     * @param[in] target Value vector with at least as many entries as stored indices.
     * @details Calls setFactor() for every index, ignoring individual rejections; a null provider is a no-op.
     */
    void apply_(const QVector<double> &target);

    PatternProvider *m_provider;
    QVector<int> m_indices;
    QVector<double> m_oldValues;
    QVector<double> m_newValues;
};

/*! \brief Swap two slots. Used by the chart view's horizontal-reorder drag. */
class SwapPatternFactorsCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture two slots for a reversible graphical reorder.
     * @param[in] provider Borrowed provider that must outlive command use; null makes undo/redo no-ops.
     * @param[in] i First zero-based factor slot.
     * @param[in] j Second zero-based factor slot.
     * @param[in] description User-visible undo text.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SwapPatternFactorsCommand(PatternProvider *provider, int i, int j, QString description,
                              QUndoCommand *parent = nullptr);
    /** @brief Restore the captured old factors (bulk) or swap the captured slots again; provider signals and attached-engine mirroring apply. */
    void undo() override;
    /** @brief Apply captured new factors (bulk) or swap the captured slots; failed provider mutations are not rolled back. */
    void redo() override;

private:
    PatternProvider *m_provider;
    int m_i;
    int m_j;
};

} // namespace openswmmvis::pattern

#endif // OPENSWMMVIS_PATTERN_PATTERNUNDOCOMMANDS_H
