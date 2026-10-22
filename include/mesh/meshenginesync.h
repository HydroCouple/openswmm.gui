/**
 * @file      meshenginesync.h
 * @brief     Push the GUI mesh layer's edited state (vertex Z, per-edge conveyance, per-edge
 *            boundary conditions) into the live engine's in-memory 2D mesh, so a subsequent
 *            `swmm_model_write*` serialises the edits.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Push the GUI mesh layer's edited state (vertex Z, per-edge conveyance,
 * per-edge boundary conditions) into the live engine's in-memory 2D mesh,
 * so a subsequent `swmm_model_write*` serialises the edits. The engine is
 * the source of truth on save: it already round-trips coupling maps,
 * Manning's n, units, and options that the lightweight GUI mesh model does
 * not retain, so syncing the few editable fields back into it is lossless
 * for everything the GUI does not touch.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHENGINESYNC_H
#define OPENSWMMVIS_MESH_MESHENGINESYNC_H

#include "meshresult.h"
#include "meshedgebc.h"

#include <openswmm/engine/openswmm_callbacks.h> // SWMM_Engine typedef

#include <QLoggingCategory>
#include <QPair>
#include <QPointF>
#include <QStringList>
#include <QVector>

// Opt-in save-path perf breakdown (per-stage ms, per-element push counts):
// QT_LOGGING_RULES="openswmm.save.perf=true" (lines are qCInfo). Gated because
// the push counters are only meaningful next to the stage timings, and both are
// noise during normal operation.
Q_DECLARE_LOGGING_CATEGORY(lcSavePerf)

namespace mesh
{

/**
 * @brief Check locally authorable mesh geometry/hydraulic/coupling/boundary values before
 * saving.
 * @param[in] mesh Borrowed stable mesh in its stored planar/vertical/project units, unchanged;
 * counts and stride-four slot multiplication must fit int. Requires nonempty vertices and cells.
 * @param[in] bcs Borrowed assignments with exactly four slots per cell, unchanged. Checks every
 * real edge even if interior, ignores triangle padding and validates type-specific
 * values/referenced-name presence.
 * @return Owned first translated error with one-based item/edge/row numbers, empty on accepted
 * local data. Checks finite xy/z, distinct valid cell IDs, triangles-before-quads, consistent
 * nonzero convex turns, positive coupled Cd/area, roughness positive or NaN-unset, depth
 * nonnegative or NaN-unset, conveyance [0,1], and required boundary names/scalars. Authoring
 * flags do not bypass coupling numeric checks. Does not certify manifold/overlap topology,
 * actual engine mesh identity/name resolution, infiltration/ groundwater/transport values or
 * file validity. Allocates diagnostic strings only, no engine/file mutation.
 *
 * @details Read-only Save preflight for geometry, hydraulic attributes, couplings and boundary
 * values. Returns the first error with one-based item/edge numbers, or an empty string. NaN
 * roughness/depth means unset; triangle edge padding is ignored. Does not certify manifold
 * topology, model reference resolution, infiltration/groundwater/transport parameters, or engine
 * mesh identity.
 */
QString validateMeshSaveData(const MeshResult &mesh, const QVector<MeshEdgeBC> &bcs);

/**
 * @brief Sequentially push editable GUI mesh attributes into a matching engine mesh for later
 * serialization.
 * @param[in,out] engine Borrowed live engine handle with editable 2D mesh, same vertex/cell
 * identities/order as the GUI mesh. Counts alone do not prove identity. Caller owns handle and
 * excludes solver/concurrent access; function prepares engine editing and does not close,
 * initialize or serialize the engine.
 * @param[in] mesh Borrowed stable locally validated geometry/attributes, unchanged. Call
 * validateMeshSaveData() separately; this routine does not run comprehensive preflight or
 * validate spatial identity. xy must still correspond to engine xy for derived unit conversion;
 * per-cell NaN/invalid roughness/depth is skipped.
 * @param[in] bcs Borrowed stride-four real-edge assignments, normally edgeSlotCount(cellCount)
 * entries. Empty skips edge updates only; vertex attributes, cells and couplings can still be
 * pushed. Nonmatching edge/cell counts warn and skip affected state, not necessarily global
 * failure.
 * @param[in,out] warnings Optional borrowed list, diagnostics appended without clearing, no
 * pointer retained.
 * @param[out] outTrianglesSynced Optional borrowed flag, reset false; true after matching-count
 * cell roughness/depth/tag pass, reset false on later API rejection. Does not alone certify
 * edge/coupling sync.
 * @param[out] outWriteRejected Optional borrowed flag, reset false and set true for checked API
 * rejection; no-engine/vertex-count mismatch leaves false. Use distinct output flags; rejection
 * requires caller to stop serialization and retain dirty state, because previous setters may
 * already have committed in memory.
 * @return False for absent engine mesh, vertex-count mismatch or checked API rejection; true can
 * still mean cell/edge updates were skipped, inspect flags/warnings. No rollback. Bulk z and
 * stage/depth use engine/layer xy length ratio (fallback 1), coupling area retains authored
 * mesh-length-squared convention. Constant boundary flow converts display units per metre to SI
 * only when the post-prepare engine state is initialized; otherwise stays display units per
 * metre. Clears/reauthors vertex names/tags, optional coupling Cd/area and matching-count cell
 * coupling rows; clears edge name slots before type-specific assignment and pushes conveyance
 * even when resetting defaults. BC group labels have no setter and only retained authored-row
 * writer reattachment can preserve them. Allocates temporary arrays/strings, can log per-call
 * performance on every exit, retains no mesh buffers and performs no file writes.
 */
bool pushMeshEditsToEngine(SWMM_Engine engine, const MeshResult &mesh,
                           const QVector<MeshEdgeBC> &bcs, QStringList *warnings = nullptr,
                           bool *outTrianglesSynced = nullptr, bool *outWriteRejected = nullptr);

/**
 * @brief Determine default coupling authoring policy from the model's COUPLING_LAW option.
 * @param[in] engine Optional borrowed live handle, unchanged and not closed; caller synchronizes
 * API reads.
 * @return False only for successfully read case-insensitive trimmed WEIR_ORIFICE; true for other
 * values, null handle or option-read failure. Uses a 64-byte local option buffer, does not
 * resolve structures, validate nodes, modify engine/project state or retain handle ownership.
 *
 * @details Whether new 1D↔2D map rows should author CD/AREA: true unless the model's
 * [2D_OPTIONS] COUPLING_LAW is WEIR_ORIFICE, whose node structures govern the exchange — an
 * authored CD/AREA would override their orifice terms.
 */
bool couplingAuthorsCdArea(SWMM_Engine engine);

/**
 * @brief Filter only nodes the engine explicitly identifies as coupling type NONE.
 * @param[in] engine Optional borrowed live handle, unchanged; null returns input unchanged, not
 * retained.
 * @param[in] nodes Borrowed node-ID/map-position pairs, unchanged; coordinates and duplicate IDs
 * are not validated.
 * @return Owned/implicitly shared order-preserving subset. Known nodes with successful
 * coupling-type lookup NONE are removed; unknown IDs and API-read failures remain. No coupling
 * assignments, coordinate conversion, engine edits or file writes; caller synchronizes engine
 * reads.
 *
 * @details \a nodes without those the engine reports as not coupleable (2D coupling type NONE —
 * authored in [2D_NODE_COUPLING] or the inlet / virtual junction default). Names the engine does
 * not know are kept.
 */
QVector<QPair<QString, QPointF>> coupleableNodes(SWMM_Engine engine,
                                                 const QVector<QPair<QString, QPointF>> &nodes);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHENGINESYNC_H
