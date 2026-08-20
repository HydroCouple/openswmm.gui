/*!
 * \file   meshenginesync.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Push the GUI mesh layer's edited state (vertex Z, per-edge conveyance,
 * per-edge boundary conditions) into the live engine's in-memory 2D mesh,
 * so a subsequent `swmm_model_write*` serialises the edits. The engine is
 * the source of truth on save: it already round-trips coupling maps,
 * Manning's n, units, and options that the lightweight GUI mesh model does
 * not retain, so syncing the few editable fields back into it is lossless
 * for everything the GUI does not touch.
 */
#ifndef OPENSWMMVIS_MESH_MESHENGINESYNC_H
#define OPENSWMMVIS_MESH_MESHENGINESYNC_H

#include "meshresult.h"
#include "meshedgebc.h"

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef

#include <QLoggingCategory>
#include <QStringList>
#include <QVector>

// Opt-in save-path perf breakdown (per-stage ms, per-element push counts):
// QT_LOGGING_RULES="openswmm.save.perf=true" (lines are qCInfo). Gated because
// the push counters are only meaningful next to the stage timings, and both are
// noise during normal operation.
Q_DECLARE_LOGGING_CATEGORY(lcSavePerf)

namespace mesh {

/*! \brief Copy the layer's editable mesh state into the engine's 2D mesh.
 *
 *  Pushes vertex elevations, per-edge conveyance factors, and per-edge
 *  boundary conditions by index. Indices line up because the layer was
 *  loaded from the same `[2D_*]` sections the engine parsed (file order is
 *  preserved on both sides).
 *
 *  Vertex Z and BC stage heads are converted from the layer's display/file
 *  units into the engine's internal SI metres using a factor derived from
 *  an (unedited) vertex's XY: `engine_x / layer_x`. This is exact and
 *  unit-system-agnostic — for US-FLOW_UNITS projects the engine scales the
 *  whole mesh by 0.3048 on load, so the same factor applies to Z.
 *
 *  Also synced: vertex coupling (MeshVertex::coupledNode -> SWMM node), the
 *  descriptive vertex/triangle TAG-column labels, per-triangle Manning's n,
 *  and SPECIFIED_FLOW discharge magnitudes (converted from the project's
 *  display flow units into the engine's SI m³/s). BC GROUP labels are not
 *  pushed (no engine setter), but the writer re-attaches them from the
 *  retained authored rows, so only a label on a brand-new edited edge would
 *  be lost.
 *
 *  Per-triangle INIT_DEPTH follows the same unit convention as vertex Z (it
 *  is a depth above the bed in mesh length units) and is scaled by the same
 *  derived factor.
 *
 *  \param engine    Open engine handle with an active 2D mesh.
 *  \param mesh      The layer's current MeshResult.
 *  \param bcs       The layer's per-edge BC vector (flat `tri*3 + edge`);
 *                   may be empty (then only Z is synced).
 *  \param warnings  Optional sink for non-fatal diagnostics.
 *  \param outTrianglesSynced  Optional: set to true when the per-triangle
 *                   attributes (Manning's n, initial depth, tag) reached the
 *                   engine, false when the triangle counts disagreed and they
 *                   were skipped. Callers that own the file (the save path)
 *                   use this to fall back to patching the written text — the
 *                   engine's mesh is stale whenever a mesh was generated or
 *                   replaced in-session, and without a fallback every cell
 *                   attribute edit is silently dropped on save.
 *  \returns true when the mesh was synced; false (with a warning) when the
 *           engine has no 2D mesh or its vertex counts do not match the
 *           layer, in which case nothing is written.
 */
bool pushMeshEditsToEngine(SWMM_Engine engine,
                           const MeshResult &mesh,
                           const QVector<MeshEdgeBC> &bcs,
                           QStringList *warnings = nullptr,
                           bool *outTrianglesSynced = nullptr);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHENGINESYNC_H
