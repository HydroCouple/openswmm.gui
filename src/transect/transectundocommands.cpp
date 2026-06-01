/*!
 * \file   transectundocommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "transect/transectundocommands.h"

#include "transect/transectregistry.h"

#include <QCoreApplication>

#include <algorithm>

namespace openswmmvis::transect {

// ─── SetRoughnessCommand ────────────────────────────────────────────────────

SetRoughnessCommand::SetRoughnessCommand(TransectProvider *provider,
                                          double nLeft, double nRight, double nChannel,
                                          QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldNL(provider ? provider->nLeftBank()  : 0.0)
    , m_oldNR(provider ? provider->nRightBank() : 0.0)
    , m_oldNCh(provider ? provider->nChannel()  : 0.0)
    , m_newNL(nLeft), m_newNR(nRight), m_newNCh(nChannel)
{
    setText(QCoreApplication::translate("transect", "Set Roughness"));
}

void SetRoughnessCommand::redo()
{
    if (m_provider) m_provider->setRoughness(m_newNL, m_newNR, m_newNCh);
}
void SetRoughnessCommand::undo()
{
    if (m_provider) m_provider->setRoughness(m_oldNL, m_oldNR, m_oldNCh);
}

// ─── SetBankStationsCommand ─────────────────────────────────────────────────

SetBankStationsCommand::SetBankStationsCommand(TransectProvider *provider,
                                                 double xLeft, double xRight,
                                                 QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldL(provider ? provider->xLeftBank()  : 0.0)
    , m_oldR(provider ? provider->xRightBank() : 0.0)
    , m_newL(xLeft), m_newR(xRight)
{
    setText(QCoreApplication::translate("transect", "Set Bank Stations"));
}

void SetBankStationsCommand::redo()
{
    if (m_provider) m_provider->setBankStations(m_newL, m_newR);
}
void SetBankStationsCommand::undo()
{
    if (m_provider) m_provider->setBankStations(m_oldL, m_oldR);
}

// ─── SetEncroachmentStationsCommand ─────────────────────────────────────────

SetEncroachmentStationsCommand::SetEncroachmentStationsCommand(
    TransectProvider *provider, double xLeft, double xRight, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldL(provider ? provider->xLeftEncroachment()  : 0.0)
    , m_oldR(provider ? provider->xRightEncroachment() : 0.0)
    , m_newL(xLeft), m_newR(xRight)
{
    setText(QCoreApplication::translate("transect", "Set Encroachment Stations"));
}

void SetEncroachmentStationsCommand::redo()
{
    if (m_provider) m_provider->setEncroachmentStations(m_newL, m_newR);
}
void SetEncroachmentStationsCommand::undo()
{
    if (m_provider) m_provider->setEncroachmentStations(m_oldL, m_oldR);
}

// ─── SetModifiersCommand ────────────────────────────────────────────────────

SetModifiersCommand::SetModifiersCommand(TransectProvider *provider,
                                          double xFactor, double yFactor, double lengthFactor,
                                          QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldXF(provider ? provider->stationMultiplier() : 1.0)
    , m_oldYF(provider ? provider->elevationOffset()   : 0.0)
    , m_oldLF(provider ? provider->meanderFactor()     : 1.0)
    , m_newXF(xFactor), m_newYF(yFactor), m_newLF(lengthFactor)
{
    setText(QCoreApplication::translate("transect", "Set Modifiers"));
}

void SetModifiersCommand::redo()
{
    if (m_provider) m_provider->setModifiers(m_newXF, m_newYF, m_newLF);
}
void SetModifiersCommand::undo()
{
    if (m_provider) m_provider->setModifiers(m_oldXF, m_oldYF, m_oldLF);
}

// ─── SetCommentsCommand ─────────────────────────────────────────────────────

SetCommentsCommand::SetCommentsCommand(TransectProvider *provider,
                                        QString newComments,
                                        QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_oldComments(provider ? provider->comments() : QString())
    , m_newComments(std::move(newComments))
{
    setText(QCoreApplication::translate("transect", "Edit Comments"));
}

void SetCommentsCommand::redo()
{
    if (m_provider) m_provider->setComments(m_newComments);
}
void SetCommentsCommand::undo()
{
    if (m_provider) m_provider->setComments(m_oldComments);
}

// ─── MoveStationPointCommand ────────────────────────────────────────────────

MoveStationPointCommand::MoveStationPointCommand(TransectProvider *provider,
                                                   int index,
                                                   double oldStation, double oldElev,
                                                   double newStation, double newElev,
                                                   QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_index(index)
    , m_oldStation(oldStation), m_oldElev(oldElev)
    , m_newStation(newStation), m_newElev(newElev)
{
    setText(QCoreApplication::translate("transect", "Move Station Point"));
}

void MoveStationPointCommand::redo()
{
    if (!m_provider) return;
    // Use the live variant so identical-to-current state is a no-op (the
    // chart drag handler will already have applied this same target via
    // setPointLive during the drag — pushing the command after release
    // shouldn't observably move the point again).
    m_provider->setPointLive(m_index, m_newStation, m_newElev);
}

void MoveStationPointCommand::undo()
{
    if (m_provider) m_provider->setPointLive(m_index, m_oldStation, m_oldElev);
}

// ─── InsertStationCommand ───────────────────────────────────────────────────

InsertStationCommand::InsertStationCommand(TransectProvider *provider,
                                            double station, double elevation,
                                            QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
    , m_station(station), m_elevation(elevation)
{
    setText(QCoreApplication::translate("transect", "Insert Station"));
}

void InsertStationCommand::redo()
{
    if (!m_provider) return;
    m_insertedIndex = m_provider->insertPoint(m_station, m_elevation);
}

void InsertStationCommand::undo()
{
    if (!m_provider || m_insertedIndex < 0) return;
    m_provider->removePointsAt({m_insertedIndex});
    m_insertedIndex = -1;
}

// ─── DeleteStationsCommand ──────────────────────────────────────────────────

DeleteStationsCommand::DeleteStationsCommand(TransectProvider *provider,
                                              QVector<int> indices,
                                              QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_provider(provider)
{
    setText(QCoreApplication::translate("transect", "Delete Station(s)"));
    if (!provider) return;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (int i : indices) {
        if (i >= 0 && i < provider->pointCount())
            m_removed.push_back(provider->pointAt(i));
    }
}

void DeleteStationsCommand::redo()
{
    if (!m_provider) return;
    // Recompute current indices from station values — the indices captured
    // at construction time may have shifted if another command ran since.
    QVector<int> toRemove;
    const auto &pts = m_provider->points();
    for (const auto &saved : m_removed) {
        for (int i = 0; i < pts.size(); ++i) {
            if (pts.at(i).station == saved.station
                && pts.at(i).elevation == saved.elevation) {
                toRemove.push_back(i);
                break;
            }
        }
    }
    m_provider->removePointsAt(toRemove);
}

void DeleteStationsCommand::undo()
{
    if (!m_provider) return;
    for (const auto &p : m_removed)
        m_provider->insertPoint(p.station, p.elevation);
}

// ─── RenameTransectCommand ──────────────────────────────────────────────────

RenameTransectCommand::RenameTransectCommand(TransectRegistry *registry,
                                              TransectProvider *provider,
                                              QString newName,
                                              QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_registry(registry)
    , m_provider(provider)
    , m_oldName(provider ? provider->name() : QString())
    , m_newName(std::move(newName))
{
    setText(QCoreApplication::translate("transect", "Rename Transect"));
}

void RenameTransectCommand::redo()
{
    if (m_registry && m_provider) m_registry->rename(m_provider, m_newName);
}

void RenameTransectCommand::undo()
{
    if (m_registry && m_provider) m_registry->rename(m_provider, m_oldName);
}

} // namespace openswmmvis::transect
