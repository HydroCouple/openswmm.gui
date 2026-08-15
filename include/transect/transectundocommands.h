/*!
 * \file   transectundocommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.4 — QUndoCommand subclasses for TransectProvider.
 *
 * Each command operates on one non-owning TransectProvider*. Commands are
 * pushed onto the dialog's QUndoStack so a single Cmd-Z unwinds across the
 * list, the property tree, the station-elevation grid, and the chart in
 * lock-step (every view subscribes to the provider's signals and
 * re-renders on apply).
 *
 * Chart-drag batching follows the timeseries Phase 6.7.3.5 pattern:
 *   - mouse press   → capture the original (station, elevation)
 *   - mouse move    → `setPointLive` (no undo push, no monotonicity break)
 *   - mouse release → push one `MoveStationPointCommand` whose redo() is
 *                     a no-op (state already current) and whose undo()
 *                     restores the original position.
 *
 * Each command emits no UI work directly — the provider's signals drive
 * the redraw. That keeps the redo / undo paths perfectly symmetric and
 * lets tests assert correctness without needing the dialog at all.
 */
#ifndef OPENSWMMVIS_TRANSECT_TRANSECTUNDOCOMMANDS_H
#define OPENSWMMVIS_TRANSECT_TRANSECTUNDOCOMMANDS_H

#include "transect/transectprovider.h"

#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::transect {

class TransectRegistry;

// ─── Scalar (group) writes ──────────────────────────────────────────────────

/*! \brief Atomic Manning's-n triple write. */
class SetRoughnessCommand : public QUndoCommand
{
public:
    SetRoughnessCommand(TransectProvider *provider,
                         double nLeft, double nRight, double nChannel,
                         QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TransectProvider *m_provider;
    double m_oldNL, m_oldNR, m_oldNCh;
    double m_newNL, m_newNR, m_newNCh;
};

/*! \brief Bank stations (xLeft, xRight). */
class SetBankStationsCommand : public QUndoCommand
{
public:
    SetBankStationsCommand(TransectProvider *provider,
                            double xLeft, double xRight,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TransectProvider *m_provider;
    double m_oldL, m_oldR;
    double m_newL, m_newR;
};

/*! \brief Encroachment stations (BQ-TR-02 Reading B). */
class SetEncroachmentStationsCommand : public QUndoCommand
{
public:
    SetEncroachmentStationsCommand(TransectProvider *provider,
                                     double xLeft, double xRight,
                                     QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TransectProvider *m_provider;
    double m_oldL, m_oldR;
    double m_newL, m_newR;
};

/*! \brief Modifier triple (xFactor, yFactor, lengthFactor). */
class SetModifiersCommand : public QUndoCommand
{
public:
    SetModifiersCommand(TransectProvider *provider,
                         double xFactor, double yFactor, double lengthFactor,
                         QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TransectProvider *m_provider;
    double m_oldXF, m_oldYF, m_oldLF;
    double m_newXF, m_newYF, m_newLF;
};

/*! \brief Free-form rich-text comments. */
class SetCommentsCommand : public QUndoCommand
{
public:
    SetCommentsCommand(TransectProvider *provider,
                        QString newComments,
                        QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TransectProvider *m_provider;
    QString m_oldComments;
    QString m_newComments;
};

// ─── Station-list edits ─────────────────────────────────────────────────────

/*! \brief Single-point move (station + elevation). Used by chart drag-end. */
class MoveStationPointCommand : public QUndoCommand
{
public:
    MoveStationPointCommand(TransectProvider *provider,
                              int index,
                              double oldStation, double oldElev,
                              double newStation, double newElev,
                              QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TransectProvider *m_provider;
    int m_index;
    double m_oldStation, m_oldElev;
    double m_newStation, m_newElev;
};

/*! \brief Insert a single (station, elevation). Redo re-inserts; undo
 *  removes it. Stores the inserted index after the first redo. */
class InsertStationCommand : public QUndoCommand
{
public:
    InsertStationCommand(TransectProvider *provider,
                          double station, double elevation,
                          QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

    int insertedIndex() const noexcept { return m_insertedIndex; }

private:
    TransectProvider *m_provider;
    double m_station;
    double m_elevation;
    int    m_insertedIndex = -1;
};

/*! \brief Remove one or more rows by index. Stores the removed (station,
 *  elevation) pairs so undo can reinstate them. */
class DeleteStationsCommand : public QUndoCommand
{
public:
    DeleteStationsCommand(TransectProvider *provider,
                            QVector<int> indices,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TransectProvider *m_provider;
    QVector<TransectPoint> m_removed;   ///< Saved before first redo, in station order.
};

// ─── Identity ───────────────────────────────────────────────────────────────

/*! \brief Rename via the registry (uniqueness enforced by the registry). */
class RenameTransectCommand : public QUndoCommand
{
public:
    RenameTransectCommand(TransectRegistry *registry,
                            TransectProvider *provider,
                            QString newName,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    TransectRegistry *m_registry;
    TransectProvider *m_provider;
    QString m_oldName;
    QString m_newName;
};

} // namespace openswmmvis::transect

#endif // OPENSWMMVIS_TRANSECT_TRANSECTUNDOCOMMANDS_H
