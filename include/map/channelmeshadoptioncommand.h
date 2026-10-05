// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mesh/channelburnmesh.h"
#include "mesh/corridorsource.h"
#include <QUndoCommand>
#include <memory>
class SWMMVisProjectWindow;
class SWMM2DMeshLayer;

// Owns detached layers and rolls back prepared network changes until adopted.
class ChannelMeshAdoptionCommand final : public QUndoCommand
{
public:
    ChannelMeshAdoptionCommand(SWMMVisProjectWindow *, SWMM2DMeshLayer *,
                              QVector<mesh::CorridorSource>, SWMM2DMeshLayer *replaced = nullptr);
    ~ChannelMeshAdoptionCommand() override;
    bool prepare(const mesh::ChannelBurnSurgery &, QString *error);
    void redo() override;
    void undo() override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
