/**
 * @file      selectionops.h
 * @brief     Pure selection algebra over a loaded `SWMMModelLayer` — the logic behind the
 *            toolbar's Invert Selection / Select Upstream / Select Downstream actions.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Pure selection algebra over a loaded `SWMMModelLayer` — the logic behind
 * the toolbar's Invert Selection / Select Upstream / Select Downstream
 * actions.
 *
 * The functions here take the current selection set and return the next
 * one; they never touch a `SelectionManager` themselves. That keeps the
 * math headless-testable (see `tests/gui/test_selectionops.cpp`) and lets
 * `SWMMVis`'s slots stay thin: build → apply → log.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SELECTIONOPS_H
#define SELECTIONOPS_H

#include "selection/selectionmanager.h"

#include <QSet>

class SWMMModelLayer;

namespace SelectionOps
{

/*!
 * \brief Invert the selection, scoped to a single category when the current
 *        selection is homogeneous.
 *
 * "Category" here is the *fine* `SWMMModelLayer::Category` — Junctions,
 * Outfalls, Storage, Dividers, Conduits, Pumps, Orifices, Weirs, Outlets,
 * Subcatchments, Rain Gages. So inverting a selection of three junctions
 * yields every *other* junction; a selection of a junction plus a conduit is
 * mixed and inverts across all eleven categories.
 *
 * Rules:
 *  - Empty selection (or one holding only pass-through refs) → select all.
 *  - 2D mesh refs (`MeshVertex` / `MeshEdge` / `MeshCell`) and non-spatial
 *    data refs (Curve / TimeSeries / …) are **passed through unchanged**:
 *    they neither participate in the homogeneity test nor get inverted.
 *
 * \returns the next selection set. Empty (and a no-op for the caller) when
 *          \p model is null or has no engine.
 * @param[in] model Borrowed loaded model, accessed synchronously on its engine-owning thread; null/no engine returns empty.
 * @param[in] current Exact reference set; refs absent from the enumerated spatial categories pass through unchanged.
 * @details Does not apply the returned set to a selection bus; caller must avoid applying an empty invalid-model result if preserving selection is desired.
 */
QSet<SWMMObjectRef> invert(SWMMModelLayer *model, const QSet<SWMMObjectRef> &current);

/*!
 * \brief Result of an upstream / downstream network trace.
 */
struct TraceResult {
    QSet<SWMMObjectRef> refs; ///< nodes + interior links + subcatchments
    int nodeCount = 0;
    int linkCount = 0;
    int subcatchCount = 0;

    /*! True when the seeds contained no traceable object (no node, link or
     *  subcatchment) — the caller should tell the user to select one. */
    bool noSeeds = true;
};

/*!
 * \brief All objects connected upstream (or downstream) of \p seeds.
 *
 * Seeding: a selected node seeds itself; a selected link seeds both its
 * endpoints; a selected subcatchment seeds itself when tracing upstream, and
 * when tracing downstream contributes its outlet chain (each subcatchment
 * hop is included in the result, and the terminal outlet node becomes a node
 * seed). Mesh and data refs in \p seeds are ignored.
 *
 * Traversal is a BFS over the directed link graph — reversed for upstream.
 * A link is included when *both* endpoints land inside the visited node set.
 *
 * Subcatchments: drainage runs subcatchment → node, so a subcatchment is
 * always *upstream* of its outlet. Tracing upstream therefore pulls in every
 * subcatchment draining to a visited node, plus — transitively — every
 * subcatchment draining into one of those. Tracing downstream picks up
 * subcatchments only along the outlet chain of a subcatchment seed. That
 * asymmetry is physical, not an oversight.
 *
 * Both BFS loops are visited-guarded, so a malformed subcatchment→subcatchment
 * cycle terminates instead of hanging.
 * @param[in] model Borrowed loaded model on its engine-owning thread; null/no engine returns default noSeeds result.
 * @param[in] seeds Existing node/link/subcatchment references resolved by engine name; unsupported/missing objects are ignored.
 * @param[in] upstream True reverses the directed graph and includes draining subcatchments; false follows its downstream orientation.
 * @return Reachable topology refs/counts with noSeeds false after any usable seed; counts describe traversed indices and can exceed materialized refs if engine IDs are unavailable.
 * @details Uses model topology direction, not simulated flow reversal, and never changes canonical selection.
 */
TraceResult trace(SWMMModelLayer *model, const QSet<SWMMObjectRef> &seeds, bool upstream);

} // namespace SelectionOps

#endif // SELECTIONOPS_H
