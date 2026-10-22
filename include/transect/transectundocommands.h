/**
 * @file      transectundocommands.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.4 — QUndoCommand subclasses for TransectProvider.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_TRANSECT_TRANSECTUNDOCOMMANDS_H
#define OPENSWMMVIS_TRANSECT_TRANSECTUNDOCOMMANDS_H

#include "transect/transectprovider.h"

#include <QString>
#include <QUndoCommand>
#include <QVector>

namespace openswmmvis::transect
{

class TransectRegistry;

// ─── Scalar (group) writes ──────────────────────────────────────────────────

/*! \brief Atomic Manning's-n triple write. */
class SetRoughnessCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture before/after Manning roughness triples.
     * @param[in] nLeft Left-bank n.
     * @param[in] nRight Right-bank n.
     * @param[in] nChannel Main-channel n.
     * @param[in] provider Borrowed raw provider pointer; a non-null provider must outlive all command uses. Null makes later applications no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetRoughnessCommand(TransectProvider *provider, double nLeft, double nRight, double nChannel,
                        QUndoCommand *parent = nullptr);
    /**
     * @brief Restore captured roughness with the provider group setter.
     */
    void undo() override;
    /**
     * @brief Apply captured roughness with the provider group setter.
     */
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
    /**
     * @brief Capture before/after channel bank stations.
     * @param[in] xLeft Left station in project length units.
     * @param[in] xRight Right station in project length units.
     * @param[in] provider Borrowed raw provider pointer; a non-null provider must outlive all command uses. Null makes later applications no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetBankStationsCommand(TransectProvider *provider, double xLeft, double xRight,
                           QUndoCommand *parent = nullptr);
    /**
     * @brief Restore captured bank stations with the provider group setter.
     */
    void undo() override;
    /**
     * @brief Apply captured bank stations with the provider group setter.
     */
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
    /**
     * @brief Capture before/after encroachment limits.
     * @param[in] xLeft Left station in project length units.
     * @param[in] xRight Right station in project length units.
     * @param[in] provider Borrowed raw provider pointer; a non-null provider must outlive all command uses. Null makes later applications no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetEncroachmentStationsCommand(TransectProvider *provider, double xLeft, double xRight,
                                   QUndoCommand *parent = nullptr);
    /**
     * @brief Restore captured encroachment stations with the provider group setter.
     */
    void undo() override;
    /**
     * @brief Apply captured encroachment stations with the provider group setter.
     */
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
    /**
     * @brief Capture before/after coordinate and meander modifiers.
     * @param[in] xFactor Dimensionless station multiplier.
     * @param[in] yFactor Additive elevation offset in project length units.
     * @param[in] lengthFactor Dimensionless meander factor.
     * @param[in] provider Borrowed raw provider pointer; a non-null provider must outlive all command uses. Null makes later applications no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetModifiersCommand(TransectProvider *provider, double xFactor, double yFactor,
                        double lengthFactor, QUndoCommand *parent = nullptr);
    /**
     * @brief Restore captured modifiers without converting point coordinates.
     */
    void undo() override;
    /**
     * @brief Apply captured modifiers without converting point coordinates.
     */
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
    /**
     * @brief Capture previous and replacement comments.
     * @param[in] newComments Replacement text copied verbatim.
     * @param[in] provider Borrowed raw provider pointer; a non-null provider must outlive all command uses. Null makes later applications no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetCommentsCommand(TransectProvider *provider, QString newComments,
                       QUndoCommand *parent = nullptr);
    /**
     * @brief Restore captured comment text.
     */
    void undo() override;
    /**
     * @brief Apply replacement comments.
     */
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
    /**
     * @brief Capture a drag displacement for later live-clamped application.
     * @param[in] index Zero-based point index.
     * @param[in] oldStation Station restored by undo(), in project length units.
     * @param[in] oldElev Elevation restored by undo(), in project length units.
     * @param[in] newStation Station requested by redo(), in project length units.
     * @param[in] newElev Elevation requested by redo(), in project length units.
     * @details Uses setPointLive() on every undo/redo, including the first redo; changed neighbors can clamp the requested restoration.
     * @param[in] provider Borrowed raw provider pointer; a non-null provider must outlive all command uses. Null makes later applications no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    MoveStationPointCommand(TransectProvider *provider, int index, double oldStation,
                            double oldElev, double newStation, double newElev,
                            QUndoCommand *parent = nullptr);
    /**
     * @brief Request the old point position through setPointLive(); current neighbors may clamp it.
     */
    void undo() override;
    /**
     * @brief Request the new point position through setPointLive(); an already-applied drag is normally a no-op.
     */
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
    /**
     * @brief Capture one sorted station insertion.
     * @param[in] station Finite station in project length units.
     * @param[in] elevation Elevation in project length units.
     * @details Redo records the returned insertion index, undo removes that index; insertion rejection is retained as -1.
     * @param[in] provider Borrowed raw provider pointer; a non-null provider must outlive all command uses. Null makes later applications no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    InsertStationCommand(TransectProvider *provider, double station, double elevation,
                         QUndoCommand *parent = nullptr);
    /**
     * @brief Remove the last recorded inserted index and reset it to -1.
     */
    void undo() override;
    /**
     * @brief Insert the captured point and remember its index, or -1 on collision.
     */
    void redo() override;

    /**
     * @brief Inspect the last insertion result.
     * @return Zero-based inserted index after successful redo(), or -1 before insertion, after undo(), or on rejected insertion.
     */
    int insertedIndex() const noexcept { return m_insertedIndex; }

private:
    TransectProvider *m_provider;
    double m_station;
    double m_elevation;
    int m_insertedIndex = -1;
};

/*! \brief Remove one or more rows by index. Stores the removed (station,
 *  elevation) pairs so undo can reinstate them. */
class DeleteStationsCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture sample values at the selected rows for reversible deletion.
     * @param[in] indices Zero-based rows, deduplicated; invalid rows ignored.
     * @details Redo later locates each captured exact station/elevation pair in the current list; undo reinserts captured values and ignores collisions.
     * @param[in] provider Borrowed raw provider pointer; a non-null provider must outlive all command uses. Null makes later applications no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    DeleteStationsCommand(TransectProvider *provider, QVector<int> indices,
                          QUndoCommand *parent = nullptr);
    /**
     * @brief Reinsert captured sample values in order, ignoring individual collision failures.
     */
    void undo() override;
    /**
     * @brief Remove current samples matching captured station/elevation pairs exactly.
     */
    void redo() override;

private:
    TransectProvider *m_provider;
    QVector<TransectPoint> m_removed; ///< Saved before first redo, in station order.
};

// ─── Identity ───────────────────────────────────────────────────────────────

/*! \brief Rename via the registry (uniqueness enforced by the registry). */
class RenameTransectCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture names for registry-managed rename undo/redo.
     * @param[in] registry Borrowed raw registry pointer that must outlive command use and own provider.
     * @param[in] newName New identifier; rename failures are ignored by undo/redo.
     * @param[in] provider Borrowed raw provider pointer; a non-null provider must outlive all command uses. Null makes later applications no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    RenameTransectCommand(TransectRegistry *registry, TransectProvider *provider, QString newName,
                          QUndoCommand *parent = nullptr);
    /**
     * @brief Request the captured old name through the registry; rejection is ignored.
     */
    void undo() override;
    /**
     * @brief Request the captured new name through the registry; rejection is ignored.
     */
    void redo() override;

private:
    TransectRegistry *m_registry;
    TransectProvider *m_provider;
    QString m_oldName;
    QString m_newName;
};

} // namespace openswmmvis::transect

#endif // OPENSWMMVIS_TRANSECT_TRANSECTUNDOCOMMANDS_H
