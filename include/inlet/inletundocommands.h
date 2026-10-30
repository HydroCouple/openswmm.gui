/**
 * @file      inletundocommands.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QUndoCommand subclasses for InletProvider / InletRegistry.
 *
 * Mirrors transectundocommands.h: each command operates on one non-owning
 * InletProvider* (or, for the identity/lifetime commands, on the registry).
 * Commands are pushed onto the InletEditorDialog's QUndoStack so a single
 * Cmd-Z unwinds across the list, the property tree and the drawing view in
 * lock-step — every view subscribes to the provider's signals and re-renders
 * on apply, so the commands themselves do no UI work.
 *
 * Add / Delete are name-keyed rather than pointer-keyed because
 * `InletRegistry::remove` destroys the provider: undoing a delete recreates
 * it from the stored snapshot, and the new pointer is republished through
 * `providerAdded`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_INLET_INLETUNDOCOMMANDS_H
#define OPENSWMMVIS_INLET_INLETUNDOCOMMANDS_H

#include "inlet/inletprovider.h"

#include <QPointer>
#include <QString>
#include <QUndoCommand>

namespace openswmmvis::inlet
{

class InletRegistry;

// ─── Design edits ───────────────────────────────────────────────────────────

/*! \brief Switch the inlet type (which groups of parameters apply). */
class SetInletTypeCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture old/new inlet type for provider-only undo/redo.
     * @param[in] newType Replacement local design category.
     * @param[in] provider Borrowed provider guarded by QPointer; destruction turns later applications into no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetInletTypeCommand(InletProvider *provider, InletType newType, QUndoCommand *parent = nullptr);
    /**
     * @brief Restore the captured old type if its guarded provider exists.
     */
    void undo() override;
    /**
     * @brief Apply the captured new type if its guarded provider exists.
     */
    void redo() override;

private:
    QPointer<InletProvider> m_provider;
    InletType m_oldType;
    InletType m_newType;
};

/*! \brief Whole-design snapshot write. The dialog captures the design before
 *  the property bag pushes its edit and passes both halves here, so one
 *  undo entry covers a multi-field group write. */
class SetInletParamsCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture complete before/after design snapshots.
     * @param[in] before Snapshot restored by undo().
     * @param[in] after Snapshot applied by redo().
     * @param[in] provider Borrowed provider guarded by QPointer; destruction turns later applications into no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetInletParamsCommand(InletProvider *provider, InletDesignData before, InletDesignData after,
                          QUndoCommand *parent = nullptr);
    /**
     * @brief Restore the before-design snapshot if its guarded provider exists.
     */
    void undo() override;
    /**
     * @brief Apply the after-design snapshot if its guarded provider exists.
     */
    void redo() override;

private:
    QPointer<InletProvider> m_provider;
    InletDesignData m_before;
    InletDesignData m_after;
};

/*! \brief Free-form description ([INLETS] comment line). */
class SetInletCommentsCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture previous and replacement description text.
     * @param[in] newComments Text applied by redo(), copied verbatim.
     * @param[in] provider Borrowed provider guarded by QPointer; destruction turns later applications into no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    SetInletCommentsCommand(InletProvider *provider, QString newComments,
                            QUndoCommand *parent = nullptr);
    /**
     * @brief Restore captured old comments if its guarded provider exists.
     */
    void undo() override;
    /**
     * @brief Apply new comments if its guarded provider exists.
     */
    void redo() override;

private:
    QPointer<InletProvider> m_provider;
    QString m_oldComments;
    QString m_newComments;
};

// ─── Identity / lifetime ────────────────────────────────────────────────────

/*! \brief Rename via the registry (uniqueness enforced by the registry). */
class RenameInletCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture names for registry-managed rename undo/redo.
     * @param[in] registry Borrowed registry guarded by QPointer; must own provider.
     * @param[in] newName Requested replacement name; rename rejections are ignored by the command.
     * @param[in] provider Borrowed provider guarded by QPointer; destruction turns later applications into no-ops.
     * @param[in] parent Optional QUndoCommand owner.
     */
    RenameInletCommand(InletRegistry *registry, InletProvider *provider, QString newName,
                       QUndoCommand *parent = nullptr);
    /**
     * @brief Request the old name through the registry; rename rejection is ignored.
     */
    void undo() override;
    /**
     * @brief Request the new name through the registry; rename rejection is ignored.
     */
    void redo() override;

private:
    QPointer<InletRegistry> m_registry;
    QPointer<InletProvider> m_provider;
    QString m_oldName;
    QString m_newName;
};

/*! \brief Create a new design with default parameters. */
class AddInletCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture a name for a registry-managed create/remove command.
     * @param[in] registry Borrowed registry guarded by QPointer; destruction makes applications no-ops.
     * @param[in] name Identifier to create when unused; undo removes the current provider found by this name.
     * @param[in] parent Optional QUndoCommand owner.
     */
    AddInletCommand(InletRegistry *registry, QString name, QUndoCommand *parent = nullptr);
    /**
     * @brief Remove the current registry provider found by the stored name, if any.
     */
    void undo() override;
    /**
     * @brief Create a default provider when the stored name is unused; an existing provider is left alone.
     */
    void redo() override;

    /*! \brief The provider created by the most recent redo (null before the
     *  first redo, or after an undo).
     * @return Borrowed current provider resolved by stored name, or null if registry/name is absent; it can resolve another later provider reusing the name.
     */
    InletProvider *provider() const;

private:
    QPointer<InletRegistry> m_registry;
    QString m_name;
};

/*! \brief Delete a design. Undo recreates it from the stored snapshot. */
class DeleteInletCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture name, comments, and design for name-keyed deletion/recreation.
     * @param[in] registry Borrowed QPointer-guarded registry used for later lookup/create/remove.
     * @param[in] provider Borrowed provider read only during construction; null captures an empty name/default snapshot.
     * @param[in] parent Optional QUndoCommand owner.
     * @details Undo creates a new provider only if the name is free; original engine reference sites removed by deletion are not restored by this snapshot.
     */
    DeleteInletCommand(InletRegistry *registry, InletProvider *provider,
                       QUndoCommand *parent = nullptr);
    /**
     * @brief Recreate the captured design/comments under its old name when available; engine parameter flush is separate.
     */
    void undo() override;
    /**
     * @brief Remove the current registry provider found by the captured name, if any.
     */
    void redo() override;

private:
    QPointer<InletRegistry> m_registry;
    QString m_name;
    QString m_comments;
    InletDesignData m_design;
};

} // namespace openswmmvis::inlet

#endif // OPENSWMMVIS_INLET_INLETUNDOCOMMANDS_H
