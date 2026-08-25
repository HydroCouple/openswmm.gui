/*!
 * \file   meshminsizecleanup.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Post-generation sliver removal
 * (MIN_CELL_SIZE_ENFORCEMENT_PLAN_2026-08-17.md §6 Phase 5).
 *
 * This is the JUNIOR partner to PSLG conditioning (pslgminsize.h), and the
 * division of labour is worth stating plainly because it is easy to expect too
 * much of this file:
 *
 *   - conditioning removes slivers the INPUT demanded;
 *   - cleanup removes slivers Triangle ADDED.
 *
 * Cleanup cannot do conditioning's job.  Every edge that carries a marker, and
 * every vertex that carries a tag or a coupled node, is protected — those are
 * coupling identities and the whole point of constraining the mesh — so a
 * sliver wedged between two constrained edges is untouchable here.  When that
 * happens the cell is reported in \ref CleanupReport::unfixable rather than
 * silently left behind, because it is actionable: it means the PSLG, not the
 * mesh, needs attention.
 *
 * Safety posture: any validity failure abandons the pass and restores the
 * pre-pass mesh.  A mesh with slivers is a slow simulation; a mesh with
 * inverted or non-manifold cells is a wrong one.
 */
#ifndef OPENSWMMVIS_MESH_MESHMINSIZECLEANUP_H
#define OPENSWMMVIS_MESH_MESHMINSIZECLEANUP_H

#include "meshresult.h"

#include <QPointF>
#include <QString>
#include <QVector>

namespace mesh {

/*! Knobs for \ref collapseSubScaleCells. */
struct CleanupPolicy
{
    /*! h — the minimum cell size in map units.  0 disables cleanup. */
    double minCellSize = 0.0;

    /*! Collapse only edges shorter than beta * h.  Deliberately well below 1:
     *  at beta = 1 every edge of every legitimately h-sized cell becomes a
     *  candidate and the pass starts dismantling good geometry. */
    double beta = 0.35;

    int maxPasses    = 2;
    int maxUnfixable = 200;   ///< cap on the reported list

    /*!
     * \brief Collapse slivers the default pass refuses to touch.
     *
     * OFF by default.  The default pass freezes the endpoints of EVERY
     * constrained edge, which on a conduit-dense model is essentially every
     * interior vertex of consequence — so it declines far more than it fixes.
     * With this on, an interior constrained edge may collapse, and a sliver
     * touching ONE identity vertex may be absorbed INTO that vertex.
     *
     * Two rules are never relaxed: two distinct identities are never merged
     * into each other, and a 1D-2D coupling is never lost (a dropped coupling
     * is worse than a sliver, and still aborts the whole pass).
     */
    bool allowIdentityCollapse = false;

    /*! A ring may not be shortened below this many vertices.  The mesh-output
     *  side of the same self-folding hazard pslgminsize guards on the input
     *  side: a ring collapsed to two vertices bounds no area. */
    int minRingVertices = 3;

    /*! Relaxing the default protections multiplies how many edges qualify on a
     *  large mesh.  Only consulted in aggressive mode. */
    int maxCandidatesPerPass = 2000000;
};

/*! What cleanup did. */
struct CleanupReport
{
    int edgesCollapsed  = 0;
    int cellsRemoved    = 0;
    int passesUsed      = 0;
    /*! Sub-threshold edges left alone — protected identity, link-condition
     *  failure, or an orientation flip.  These are the ones conditioning
     *  should have prevented. */
    int skippedProtected = 0;

    // ── Aggressive mode only; all zero/false when the opt-in is off ──────
    int  identityAbsorptions      = 0;  ///< sliver absorbed into an identity
    int  interiorConstraintsLost  = 0;  ///< interior constrained edges collapsed
    int  ringEdgesShortened       = 0;
    int  identityConflictsSkipped = 0;  ///< two identities, declined
    int  ringGuardSkipped         = 0;  ///< would breach minRingVertices
    int  candidatesDropped        = 0;  ///< over maxCandidatesPerPass
    bool exhaustedPasses          = false;  ///< ran out of budget mid-work

    double minAreaBefore = 0.0;
    double minAreaAfter  = 0.0;

    /*! True when a validity check failed and the mesh was restored. */
    bool abandoned = false;

    QVector<QPointF> unfixable;   ///< centroids of protected sub-scale cells

    [[nodiscard]] QString summary() const;
};

/*!
 * \brief Collapse sub-scale edges in \p mesh, protecting all constrained and
 *        coupled geometry.
 *
 * Returns true when the mesh was left in a valid state — including the no-op
 * cases (policy disabled, nothing to do).  Returns false only when a pass had
 * to be abandoned, in which case \p mesh holds the pre-pass geometry and is
 * still perfectly usable.
 *
 * Must run BEFORE mesh::reorderMeshHilbert: reorder is a pure permutation for
 * locality, and collapsing cells afterwards would waste it.  Downstream
 * elevation fill and node mapping are coordinate-keyed and the coupling map is
 * marker-keyed, so all three tolerate the vertex renumbering this performs —
 * provided marker-bearing vertices survive, which the protection rules
 * guarantee.
 */
bool collapseSubScaleCells(MeshResult *mesh,
                           const CleanupPolicy &policy,
                           CleanupReport *report);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHMINSIZECLEANUP_H
