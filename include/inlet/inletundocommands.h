/*!
 * \file   inletundocommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QUndoCommand subclasses for InletProvider / InletRegistry.
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
 */
#ifndef OPENSWMMVIS_INLET_INLETUNDOCOMMANDS_H
#define OPENSWMMVIS_INLET_INLETUNDOCOMMANDS_H

#include "inlet/inletprovider.h"

#include <QPointer>
#include <QString>
#include <QUndoCommand>

namespace openswmmvis::inlet {

class InletRegistry;

// ─── Design edits ───────────────────────────────────────────────────────────

/*! \brief Switch the inlet type (which groups of parameters apply). */
class SetInletTypeCommand : public QUndoCommand
{
public:
    SetInletTypeCommand(InletProvider *provider,
                         InletType newType,
                         QUndoCommand *parent = nullptr);
    void undo() override;
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
    SetInletParamsCommand(InletProvider *provider,
                           InletDesignData before,
                           InletDesignData after,
                           QUndoCommand *parent = nullptr);
    void undo() override;
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
    SetInletCommentsCommand(InletProvider *provider,
                             QString newComments,
                             QUndoCommand *parent = nullptr);
    void undo() override;
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
    RenameInletCommand(InletRegistry *registry,
                        InletProvider *provider,
                        QString newName,
                        QUndoCommand *parent = nullptr);
    void undo() override;
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
    AddInletCommand(InletRegistry *registry,
                     QString name,
                     QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

    /*! \brief The provider created by the most recent redo (null before the
     *  first redo, or after an undo). */
    InletProvider *provider() const;

private:
    QPointer<InletRegistry> m_registry;
    QString m_name;
};

/*! \brief Delete a design. Undo recreates it from the stored snapshot. */
class DeleteInletCommand : public QUndoCommand
{
public:
    DeleteInletCommand(InletRegistry *registry,
                        InletProvider *provider,
                        QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    QPointer<InletRegistry> m_registry;
    QString         m_name;
    QString         m_comments;
    InletDesignData m_design;
};

} // namespace openswmmvis::inlet

#endif // OPENSWMMVIS_INLET_INLETUNDOCOMMANDS_H
