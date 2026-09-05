/*!
 * \file   inletundocommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "inlet/inletundocommands.h"

#include "inlet/inletregistry.h"

#include <QCoreApplication>

namespace openswmmvis::inlet {

// ─── SetInletTypeCommand ────────────────────────────────────────────────────

SetInletTypeCommand::SetInletTypeCommand(InletProvider *provider,
                                           InletType newType,
                                           QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldType(provider ? provider->type() : InletType::Grate)
    , m_newType(newType)
{
    setText(QCoreApplication::translate("inlet", "Set Inlet Type"));
}

void SetInletTypeCommand::redo()
{
    if (m_provider) m_provider->setType(m_newType);
}

void SetInletTypeCommand::undo()
{
    if (m_provider) m_provider->setType(m_oldType);
}

// ─── SetInletParamsCommand ──────────────────────────────────────────────────

SetInletParamsCommand::SetInletParamsCommand(InletProvider *provider,
                                               InletDesignData before,
                                               InletDesignData after,
                                               QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
    setText(QCoreApplication::translate("inlet", "Edit Inlet Parameters"));
}

void SetInletParamsCommand::redo()
{
    if (m_provider) m_provider->setDesign(m_after);
}

void SetInletParamsCommand::undo()
{
    if (m_provider) m_provider->setDesign(m_before);
}

// ─── SetInletCommentsCommand ────────────────────────────────────────────────

SetInletCommentsCommand::SetInletCommentsCommand(InletProvider *provider,
                                                   QString newComments,
                                                   QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldComments(provider ? provider->comments() : QString())
    , m_newComments(std::move(newComments))
{
    setText(QCoreApplication::translate("inlet", "Set Inlet Description"));
}

void SetInletCommentsCommand::redo()
{
    if (m_provider) m_provider->setComments(m_newComments);
}

void SetInletCommentsCommand::undo()
{
    if (m_provider) m_provider->setComments(m_oldComments);
}

// ─── RenameInletCommand ─────────────────────────────────────────────────────

RenameInletCommand::RenameInletCommand(InletRegistry *registry,
                                         InletProvider *provider,
                                         QString newName,
                                         QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_registry(registry)
    , m_provider(provider)
    , m_oldName(provider ? provider->name() : QString())
    , m_newName(std::move(newName))
{
    setText(QCoreApplication::translate("inlet", "Rename Inlet"));
}

void RenameInletCommand::redo()
{
    if (m_registry && m_provider) m_registry->rename(m_provider, m_newName);
}

void RenameInletCommand::undo()
{
    if (m_registry && m_provider) m_registry->rename(m_provider, m_oldName);
}

// ─── AddInletCommand ────────────────────────────────────────────────────────

AddInletCommand::AddInletCommand(InletRegistry *registry,
                                   QString name,
                                   QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_registry(registry)
    , m_name(std::move(name))
{
    setText(QCoreApplication::translate("inlet", "Add Inlet"));
}

InletProvider *AddInletCommand::provider() const
{
    return m_registry ? m_registry->findByName(m_name) : nullptr;
}

void AddInletCommand::redo()
{
    if (m_registry && !m_registry->hasName(m_name)) m_registry->create(m_name);
}

void AddInletCommand::undo()
{
    if (!m_registry) return;
    if (auto *p = m_registry->findByName(m_name)) m_registry->remove(p);
}

// ─── DeleteInletCommand ─────────────────────────────────────────────────────

DeleteInletCommand::DeleteInletCommand(InletRegistry *registry,
                                         InletProvider *provider,
                                         QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_registry(registry)
    , m_name(provider ? provider->name() : QString())
    , m_comments(provider ? provider->comments() : QString())
    , m_design(provider ? provider->design() : InletDesignData{})
{
    setText(QCoreApplication::translate("inlet", "Delete Inlet"));
}

void DeleteInletCommand::redo()
{
    if (!m_registry) return;
    if (auto *p = m_registry->findByName(m_name)) m_registry->remove(p);
}

void DeleteInletCommand::undo()
{
    if (!m_registry || m_name.isEmpty()) return;
    InletProvider *p = m_registry->create(m_name);
    if (!p) return;
    p->setDesign(m_design);
    p->setComments(m_comments);
}

} // namespace openswmmvis::inlet
