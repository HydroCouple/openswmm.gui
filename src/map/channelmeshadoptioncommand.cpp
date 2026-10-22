// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      channelmeshadoptioncommand.cpp
 * @brief     Undoable adoption of generated channel meshes and network changes.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "map/channelmeshadoptioncommand.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "swmmvisprojectwindow.h"
#include "project/generatedmeshartifacts.h"
#include <QFileInfo>
#include <QPointer>
#include <QScopeGuard>
#include <vector>

struct ChannelMeshAdoptionCommand::Impl {
    struct Previous {
        QPointer<SWMM2DMeshLayer> mesh;
        int index;
        bool active;
        bool remove;
    };
    QPointer<SWMMVisProjectWindow> window;
    QPointer<SWMMModelLayer> model;
    QPointer<MapCanvas> canvas;
    QPointer<SWMM2DMeshLayer> generated;
    QPointer<SWMM2DMeshLayer> burnSource;
    QVector<Previous> previous;
    std::vector<std::unique_ptr<QUndoCommand>> changes;
    QVector<mesh::CorridorSource> oldSources, newSources;
    bool networkApplied = false, adopted = false;
    SWMM_Engine sourceEngine = nullptr;
    bool invalidated = false;
    QMetaObject::Connection engineClosing;
    /**
     * @brief Check that preparation/replay still targets the original live engine.
     * @return Nonnull guarded model with same nonnull source handle and no observed engine-close
     *   signal. Does not prove canvas/generated layer exists, engine is editable or topology is
     *   unchanged. GUI-thread query, no mutation.
     */
    bool current() const
    {
        return !invalidated && model && model->engine() == sourceEngine && sourceEngine;
    }

public:
    /**
     * @param[in] pw Required live window with model/canvas/open engine.
     * @param[in] mesh Required generated mesh whose detached lifetime is managed until adoption.
     * @param[in] sources Target corridor-source vector stored by value.
     * @param[in] replaced Optional previous burn-source mesh.
     * @brief Snapshot layer-stack mesh indices/active/removal state and source lists, then connect
     *   permanent engine-close invalidation.
     * @note QObject pointers guarded; no network or stack edit at construction. Removal includes
     *   designated source, active meshes, duplicate internal meshes for internal output, and
     *   nonempty matching absolute source paths. Access GUI thread only.
     */
    Impl(SWMMVisProjectWindow *pw, SWMM2DMeshLayer *mesh, QVector<mesh::CorridorSource> sources,
         SWMM2DMeshLayer *replaced)
        : window(pw), model(pw->modelLayer()), canvas(pw->canvas()), generated(mesh),
          burnSource(replaced), oldSources(pw->corridorSources()), newSources(std::move(sources))
    {
        sourceEngine = model->engine();
        engineClosing = QObject::connect(model, &SWMMModelLayer::engineAboutToClose, canvas,
                                         [this] { invalidated = true; });
        const QString path = QFileInfo(mesh->sourcePath()).absoluteFilePath();
        for (int i = 0; i < canvas->layers().size(); ++i) {
            auto *old = qobject_cast<SWMM2DMeshLayer *>(canvas->layers()[i]);
            if (!old)
                continue;
            const bool remove = old == replaced || old->isActiveMesh() ||
                                (!mesh->isExternalMesh() && !old->isExternalMesh()) ||
                                (!old->sourcePath().isEmpty() &&
                                 QFileInfo(old->sourcePath()).absoluteFilePath() == path);
            previous.append({old, i, old->isActiveMesh(), remove});
        }
    }
    /**
     * @brief Release invalidation connection and any detached layers retained for undo, rolling
     *   back abandoned preparation.
     * @note When networkApplied and not adopted with original engine current, undo children reverse
     *   in BulkEdit. If canvas survives, deleteLater detached generated and removal-marked prior
     *   meshes. Live canvas layers remain owned externally; absent canvas skips explicit layer
     *   cleanup.
     */
    ~Impl()
    {
        QObject::disconnect(engineClosing);
        // Preparation is transactional even if adoption is abandoned.
        if (networkApplied && !adopted && current()) {
            SWMMModelLayer::BulkEdit guard(model);
            for (auto it = changes.rbegin(); it != changes.rend(); ++it)
                (*it)->undo();
        }
        if (!canvas)
            return;
        if (generated && !canvas->layers().contains(generated))
            generated->deleteLater();
        for (const auto &old : previous)
            if (old.remove && old.mesh && !canvas->layers().contains(old.mesh))
                old.mesh->deleteLater();
    }
    /**
     * @brief Apply and validate one-shot reversible channel surgery before any layer adoption.
     * @param[in] plan Prevalidated split/delete/outfall specification using current model IDs.
     * @param[out] error Optional destination assigned on failure; retained on success.
     * @param[in] progress Optional synchronous GUI callback before each zero-based step; caller
     *   prevents reentry/model changes.
     * @return True with networkApplied and child commands retained, false with attempted reverse
     *   rollback/child list clear. Invalid current engine returns false before editing.
     * @note One call required: existing prepared state is not cleared/rejected. BulkEdit coalesces
     *   model refresh but does not itself provide transactional setter rollback; child commands
     *   supply replay. Progress callbacks may observe intermediate engine state.
     */
    bool prepare(const mesh::ChannelBurnSurgery &plan, QString *error,
                 const std::function<void(int, int, const QString &)> &progress)
    {
        if (!current()) {
            if (error)
                *error = QObject::tr("The model is unavailable.");
            return false;
        }
        SWMMModelLayer::BulkEdit guard(model);
        changes.reserve(plan.splits.size() + plan.nodePlans.size() + 2);
        int outfalls = 0, removed = 0;
        for (const auto &node : plan.nodePlans) {
            if (node.role == mesh::BurnNodeRole::Outfall)
                ++outfalls;
            if (node.role == mesh::BurnNodeRole::Removed)
                ++removed;
        }
        // Steps: splits, the two delete batches, outfall conversions, and the
        // one-shot model rebuild when the bulk edit closes.
        const int total = plan.splits.size() + (plan.burnedConduits.isEmpty() ? 0 : 1) +
                          (removed ? 1 : 0) + outfalls + 1;
        int done = 0;
        auto step = [&](const QString &stage) {
            if (progress)
                progress(done, total, stage);
            ++done;
        };
        auto rollback = qScopeGuard([&] {
            if (networkApplied)
                return;
            for (auto it = changes.rbegin(); it != changes.rend(); ++it)
                (*it)->undo();
            changes.clear();
        });
        auto fail = [&](const QString &message) {
            if (error)
                *error = message;
            return false;
        };
        auto execute = [&](std::unique_ptr<QUndoCommand> command) {
            command->redo();
            changes.push_back(std::move(command));
        };
        for (const auto &split : plan.splits) {
            step(QObject::tr("Splitting channel %1…").arg(split.linkId));
            auto command = std::make_unique<InsertNodeSplitCommand>(
                model, split.linkId, split.t, split.nodeId, split.downstreamId, SWMM_NODE_JUNCTION,
                canvas);
            auto *raw = command.get();
            execute(std::move(command));
            if (!raw->retyped())
                return fail(QObject::tr("Cannot split channel %1 at the domain boundary.")
                                .arg(split.linkId));
        }
        // Separate batches avoid duplicate cascade snapshots on Undo.
        QList<BatchDeleteCommand::Target> links, nodes;
        for (const auto &id : plan.burnedConduits)
            links.append({id, DeleteObjectCommand::DeleteLink});
        if (!links.isEmpty()) {
            step(QObject::tr("Removing %1 replaced channel intervals…").arg(links.size()));
            execute(std::make_unique<BatchDeleteCommand>(
                model, links, canvas, QObject::tr("Remove replaced channel intervals")));
        }
        for (const auto &id : plan.burnedConduits)
            if (swmm_link_index(model->engine(), id.toUtf8().constData()) >= 0)
                return fail(QObject::tr("Cannot remove replaced interval %1.").arg(id));
        for (const auto &node : plan.nodePlans)
            if (node.role == mesh::BurnNodeRole::Removed)
                nodes.append({node.nodeId, DeleteObjectCommand::DeleteNode});
        if (!nodes.isEmpty()) {
            step(QObject::tr("Removing %1 redundant channel nodes…").arg(nodes.size()));
            execute(std::make_unique<BatchDeleteCommand>(
                model, nodes, canvas, QObject::tr("Remove redundant channel nodes")));
        }
        for (const auto &node : plan.nodePlans) {
            const int index = swmm_node_index(model->engine(), node.nodeId.toUtf8().constData());
            if (node.role == mesh::BurnNodeRole::Removed) {
                if (index >= 0)
                    return fail(QObject::tr("Cannot remove redundant node %1.").arg(node.nodeId));
                continue;
            }
            if (index < 0)
                return fail(QObject::tr("Missing channel interface %1.").arg(node.nodeId));
            if (node.role != mesh::BurnNodeRole::Outfall)
                continue;
            int type = -1;
            swmm_node_get_type(model->engine(), index, &type);
            if (type != SWMM_NODE_JUNCTION)
                return fail(QObject::tr("Refusing to change special node %1.").arg(node.nodeId));
            step(QObject::tr("Creating coupled outfall %1…").arg(node.nodeId));
            GeneratedOutfallSpec spec;
            spec.applyOutfall = true;
            spec.tag = QStringLiteral("burn:outfall");
            // Keep invert and offsets: preserve the connected pipe's physical elevation.
            auto command = std::make_unique<ConvertNodeTypeCommand>(
                model, node.nodeId, SWMM_NODE_OUTFALL, spec, canvas);
            auto *raw = command.get();
            execute(std::move(command));
            if (!raw->converted())
                return fail(QObject::tr("Cannot create coupled outfall %1.").arg(node.nodeId));
        }
        networkApplied = true;
        step(QObject::tr("Rebuilding the model display…")); // endBulkEdit runs when guard closes
        return true;
    }
    /**
     * @brief Mark a burn replacement/restored mesh as complete generated topology for subsequent
     *   save.
     * @param[in] layer Optional guarded mesh target; no-op if null or original burnSource no longer
     *   exists.
     * @note Enables ownsGeneratedTopology, disables preserveImportedSections. Internal mesh source
     *   path becomes model INP path when model lives; external mesh with published artifacts uses
     *   published path and clears SaveAs-rebase need. No file writes; arbitrary removed layers do
     *   not receive policy unless caller selects them.
     */
    void restoreBurnOwnership(SWMM2DMeshLayer *layer)
    {
        if (!burnSource || !layer)
            return;
        // Burn inputs passed the topology-replacement guard. A Save of the
        // other undo state may already have replaced this same file, so both
        // states must serialize their complete geometry, not patch by row.
        // Do not apply this policy to arbitrary layers removed by generation.
        layer->setOwnsGeneratedTopology(true);
        layer->setPreservesImportedSections(false);
        if (!layer->isExternalMesh()) {
            if (model)
                layer->setSourcePath(model->modelFilePath());
        } else if (const auto &artifacts = layer->generatedArtifacts();
                   artifacts && !artifacts->publishedMeshPath().isEmpty()) {
            if (!artifacts->publishedMeshPath().isEmpty())
                layer->setSourcePath(artifacts->publishedMeshPath());
            layer->setImportNeedsSaveAsRebase(false);
        }
    }
    /**
     * @brief Apply stored surgery if needed and replace the captured mesh stack with generated
     *   active mesh.
     * @note Requires current source, canvas/generated and not adopted. Clears old active flags,
     *   detaches only removal-marked meshes, adds generated without nested undo, swaps sources and
     *   marks window dirty. Child failures/stack-operation results ignored. One BulkEdit coalesces
     *   network changes; ownership retained for undo.
     */
    void redo()
    {
        if (!canvas || !current() || !generated || adopted)
            return;
        restoreBurnOwnership(generated);
        SWMMModelLayer::BulkEdit guard(model);
        if (!networkApplied) {
            for (auto &command : changes)
                command->redo();
            networkApplied = true;
        }
        for (const auto &old : previous)
            if (old.mesh) {
                old.mesh->setActiveMesh(false);
                if (old.remove) {
                    const int index = canvas->layers().indexOf(old.mesh);
                    if (index >= 0)
                        canvas->takeLayer(index, false);
                }
            }
        generated->setActiveMesh(true);
        canvas->addLayer(generated, false);
        if (window) {
            window->setCorridorSources(newSources);
            window->setHasChanges(true);
        }
        adopted = true;
    }
    /**
     * @brief Restore captured stack/source state and reverse all saved surgery commands.
     * @note Current source/live canvas/adopted required. Detaches generated without destroying,
     *   reinserts removal-marked surviving old meshes at captured indices, restores active flags
     *   and burn save ownership, undoes children reverse, clears prepared/adopted flags and marks
     *   window dirty. Does not validate replay success.
     */
    void undo()
    {
        if (!canvas || !current() || !adopted)
            return;
        SWMMModelLayer::BulkEdit guard(model);
        const int index = canvas->layers().indexOf(generated);
        if (index >= 0)
            canvas->takeLayer(index, false);
        for (const auto &old : previous)
            if (old.mesh) {
                if (old.mesh == burnSource)
                    restoreBurnOwnership(old.mesh);
                if (old.remove)
                    canvas->insertLayer(old.index, old.mesh, false);
                old.mesh->setActiveMesh(old.active);
            }
        for (auto it = changes.rbegin(); it != changes.rend(); ++it)
            (*it)->undo();
        networkApplied = false;
        adopted = false;
        if (window) {
            window->setCorridorSources(oldSources);
            window->setHasChanges(true);
        }
    }
};

ChannelMeshAdoptionCommand::ChannelMeshAdoptionCommand(SWMMVisProjectWindow *pw,
                                                       SWMM2DMeshLayer *layer,
                                                       QVector<mesh::CorridorSource> sources,
                                                       SWMM2DMeshLayer *replaced)
    : QUndoCommand(QObject::tr("Replace mesh and channel intervals")),
      d(std::make_unique<Impl>(pw, layer, std::move(sources), replaced))
{
}
ChannelMeshAdoptionCommand::~ChannelMeshAdoptionCommand() = default;
bool ChannelMeshAdoptionCommand::prepare(
    const mesh::ChannelBurnSurgery &plan, QString *error,
    const std::function<void(int, int, const QString &)> &progress)
{
    return d->prepare(plan, error, progress);
}
void ChannelMeshAdoptionCommand::redo()
{
    if (d->current())
        d->redo();
    else
        setObsolete(true);
}
void ChannelMeshAdoptionCommand::undo()
{
    if (d->current())
        d->undo();
    else
        setObsolete(true);
}
