/*!
 * \file   qsg2ddirtystate.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 2 — dirty-domain separation for the 2D mesh / results
 * QSG renderers.
 *
 * The renderers historically folded every invalidation into a single
 * m_contentDirty flag, so a selection change or a zoom rebuilt every pass.
 * Qsg2DDirtyState splits invalidation into orthogonal domains:
 *
 *   Geometry  — mesh topology or scene coordinates changed
 *   Style     — colour ramp / opacity / widths / marker or contour settings
 *   Data      — result values changed (time-step advance)
 *   Selection — highlighted cells / edges / vertices changed
 *   Lod       — the level-of-detail content key changed, or the view left
 *               the coverage region the content was built for
 *   Transform — extent changed but content itself needs no rebuild
 *
 * Usage contract (single-threaded — Qt GUI thread):
 *
 *   1. Signal handlers / setters call the note*() methods. Ambiguous
 *      invalidations (the layer's catch-all repaintRequested) call
 *      noteExternalChanged().
 *   2. Once per sync, the renderer calls resolve() with snapshot diffs it
 *      computed itself (geometry revision, selection sets, time index) and
 *      with the LOD/coverage verdicts from Qsg2DLodPolicy. resolve()
 *      classifies any pending external change into the narrowest true
 *      domain (falling back to Style when nothing observable moved),
 *      returns the combined bitset, and clears the pending state.
 *
 * Expected classifications (locked by tests/unit/test_qsg2d_dirtystate.cpp):
 *
 *   pan after a clean frame            -> Transform
 *   zoom within the same LOD bucket    -> Transform
 *   zoom across an LOD threshold       -> Transform | Lod
 *   highlighted set changed            -> Selection
 *   time-step changed                  -> Data
 *   geometry revision changed          -> Geometry
 *   style edit (nothing else moved)    -> Style
 *
 * Pure Qt-Core header — no Qt Quick dependency, unit-testable headlessly.
 */
#ifndef OPENSWMM_RENDER_QSG2DDIRTYSTATE_H
#define OPENSWMM_RENDER_QSG2DDIRTYSTATE_H

#include <QtGlobal>

namespace OpenSWMM::Render
{

class Qsg2DDirtyState
{
public:
    enum Domain : quint32 {
        None      = 0,
        Geometry  = 1u << 0,
        Style     = 1u << 1,
        Data      = 1u << 2,
        Selection = 1u << 3,
        Lod       = 1u << 4,
        Transform = 1u << 5,

        /*! Every content-affecting domain (everything except Transform). */
        AllContent = Geometry | Style | Data | Selection | Lod,
        All        = AllContent | Transform,
    };

    // ── Event notes (called from signal handlers / setters) ────────────

    /*! Map extent changed. \p zoomChanged distinguishes zoom from pure pan
     *  (used for perf-log reasons; the content verdict comes from the LOD
     *  inputs given to resolve()). */
    void noteExtentChanged(bool zoomChanged)
    {
        m_extentChanged = true;
        m_zoomChanged   = m_zoomChanged || zoomChanged;
    }

    void noteDataChanged()      { m_pending |= Data; }
    void noteSelectionChanged() { m_pending |= Selection; }
    void noteStyleChanged()     { m_pending |= Style; }
    void noteGeometryChanged()  { m_pending |= Geometry; }

    /*! Layer pointer swapped / renderer force-rebuild: everything is stale. */
    void noteLayerChanged()     { m_pending |= AllContent; }

    /*! Ambiguous invalidation (e.g. the layer's catch-all repaintRequested
     *  signal). Classified by resolve() using the snapshot diffs. */
    void noteExternalChanged()  { m_external = true; }

    // ── Pre-resolve introspection (for perf-log dirty reasons) ─────────

    [[nodiscard]] bool extentChangePending() const { return m_extentChanged; }
    [[nodiscard]] bool zoomChangePending()   const { return m_zoomChanged; }
    [[nodiscard]] bool externalChangePending() const { return m_external; }
    [[nodiscard]] quint32 pending() const { return m_pending; }

    [[nodiscard]] bool hasAnythingPending() const
    {
        return m_pending != None || m_external || m_extentChanged;
    }

    // ── Sync-time resolution ────────────────────────────────────────────

    /*!
     * Turn the pending events plus the renderer's snapshot diffs into a
     * domain bitset, then clear the pending state.
     *
     * \p geomRevisionChanged  layer geometry revision differs from the one
     *                         the current content was built from.
     * \p selectionChanged     highlighted-element sets differ from the
     *                         snapshot the current overlay was built from.
     * \p timeChanged          result time index differs from the last
     *                         rendered frame.
     * \p lodKeyChanged        Qsg2DLodPolicy content key differs from the
     *                         key the current content was built at.
     * \p extentInsideCoverage current extent is fully inside the coverage
     *                         rect the content was built for (false forces
     *                         an Lod rebuild so pans that leave the built
     *                         region repopulate the newly exposed area).
     */
    quint32 resolve(bool geomRevisionChanged,
                    bool selectionChanged,
                    bool timeChanged,
                    bool lodKeyChanged,
                    bool extentInsideCoverage)
    {
        quint32 bits = m_pending;

        // Snapshot diffs always win — they are ground truth regardless of
        // which (or whether a) signal fired.
        if (geomRevisionChanged) bits |= Geometry;
        if (selectionChanged)    bits |= Selection;
        if (timeChanged)         bits |= Data;

        // An ambiguous external change with no observable diff is treated
        // as a style edit: the broadest safe content refresh that still
        // leaves static geometry, selection and transform state alone.
        if (m_external
            && !geomRevisionChanged && !selectionChanged && !timeChanged
            && (bits & (Geometry | Selection | Data)) == 0)
            bits |= Style;

        if (m_extentChanged) {
            bits |= Transform;
            if (lodKeyChanged || !extentInsideCoverage) bits |= Lod;
        } else if (lodKeyChanged || !extentInsideCoverage) {
            // Viewport/DPR resize (or a mesh-stat change) without an extent
            // move still re-keys the content — and a coverage miss without
            // an extent event (view grew past the built region) must
            // likewise repopulate.
            bits |= Lod;
        }

        m_pending       = None;
        m_external      = false;
        m_extentChanged = false;
        m_zoomChanged   = false;
        return bits;
    }

    void clear()
    {
        m_pending       = None;
        m_external      = false;
        m_extentChanged = false;
        m_zoomChanged   = false;
    }

private:
    quint32 m_pending       = None;
    bool    m_external      = false;
    bool    m_extentChanged = false;
    bool    m_zoomChanged   = false;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_QSG2DDIRTYSTATE_H
