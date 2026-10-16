// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mesh/channelburnmesh.h"
#include "mesh/corridorsource.h"
#include <QUndoCommand>
#include <functional>
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
    /*! Applies the 1D surgery on the GUI thread. \p progress, if given, is called
     *  before each step with (done, total, stage) so a dialog can show it. */
    bool prepare(const mesh::ChannelBurnSurgery &, QString *error,
                 const std::function<void(int, int, const QString &)> &progress = {});
    void redo() override;
    void undo() override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
