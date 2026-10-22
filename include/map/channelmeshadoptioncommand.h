// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      channelmeshadoptioncommand.h
 * @brief     Undoable adoption of generated channel meshes and network changes.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

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
    /**
     * @brief Capture existing mesh stack/source state and take responsibility for detached
     *   generated/replaced layers.
     * @param[in] pw Required live project window with nonnull canvas/model and open engine;
     *   subsequent QObject pointers guarded.
     * @param[in] layer Required generated mesh layer; must be detached until redo adoption. Command
     *   deletes it later when detached and canvas survives.
     * @param[in] sources New corridor-source definitions copied/moved into command; old window
     *   sources snapshotted.
     * @param[in] replaced Optional prior burn-source layer, used both for removal and full-topology
     *   save ownership on restored states.
     * @note GUI-thread only. Snapshots all current mesh indices/active flags; marks replacement,
     *   active meshes, other internal meshes when new internal, and same absolute source-path
     *   meshes for removal. Does not apply surgery or adopt at construction. Source engine identity
     *   is pinned; engineAboutToClose permanently invalidates further application even if same
     *   pointer reused.
     */
    ChannelMeshAdoptionCommand(SWMMVisProjectWindow *pw, SWMM2DMeshLayer *layer,
                               QVector<mesh::CorridorSource> sources,
                               SWMM2DMeshLayer *replaced = nullptr);
    /**
     * @brief Disconnect invalidation, undo prepared-but-unadopted surgery when source engine
     *   survives, and clean up detached owned meshes.
     * @note Rollback runs reverse child commands in model BulkEdit only when prepared and not
     *   adopted. Adopted surgery remains committed when command discarded. With live canvas,
     *   generated and removal-marked previous meshes absent from stack receive deleteLater; if
     *   canvas destroyed this explicit cleanup is skipped. GUI-thread destruction required.
     */
    ~ChannelMeshAdoptionCommand() override;
    /*! Applies the 1D surgery on the GUI thread. \p progress, if given, is called
     *  before each step with (done, total, stage) so a dialog can show it.
     * @param[in] plan Prevalidated one-shot channel burn surgery: native model split fractions/IDs,
     *   removed intervals/nodes and outfall roles. Call once before initial redo; repeated prepare
     *   is not guarded.
     * @param[out] error Optional error string, set on failure; not cleared on success.
     * @param[in] progress Optional synchronous GUI-thread callback before each step with zero-based
     *   done, total and translated stage; must not close/change model or throw/reenter command.
     * @return True after all split/delete/conversion checks and setting prepared state; false when
     *   source unavailable or a checked surgery operation fails. Completed child operations are
     *   undone in reverse on failure, subject to their own replay/error behavior.
     * @note Mutates 1D engine immediately inside model BulkEdit, prior to mesh adoption. Keeps
     *   reversible child commands, verifies removed IDs absent and outfall candidates ordinary
     *   junctions, preserves physical invert/offset values and tags generated outfalls. Does not
     *   create/adopt generated mesh or update corridor sources. Last progress callback still runs
     *   before bulk guard closes/rebuilds.
     */
    bool prepare(const mesh::ChannelBurnSurgery &plan, QString *error,
                 const std::function<void(int, int, const QString &)> &progress = {});
    /**
     * @brief Adopt generated mesh and prepared network changes as active state, replacing marked
     *   prior meshes and corridor sources.
     * @note Invalid source engine marks command obsolete; null canvas/generated or already adopted
     *   no-op. Replays saved child changes only after prior undo, without revalidating their
     *   success, then deactivates old meshes/removes marked ones, adds generated active layer
     *   without nested undo, sets new sources and project dirty. Requires successful one-shot
     *   prepare before first redo for intended surgery.
     */
    void redo() override;
    /**
     * @brief Detach generated mesh, restore prior layer order/active flags and corridor sources,
     *   then undo surgery in reverse.
     * @note Invalid source engine marks obsolete; absent canvas/not adopted no-op. Uses BulkEdit
     *   and child replay statuses are not checked. Restored burn source gets full-topology
     *   ownership policy; window is marked dirty. Detached generated mesh retained for redo until
     *   command destruction.
     */
    void undo() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
