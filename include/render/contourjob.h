/*!
 * \file   contourjob.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 7 — thread-safe contour job input/output for asynchronous
 * marching-triangles recomputation.
 *
 * The results QSG renderer's per-tick marching pass reads the layer's live
 * SceneTri buffer, which applyCurrentDepths_() mutates in place on every
 * animation tick — a worker thread must never touch it. ContourJobInput is
 * an immutable snapshot the GUI thread assembles instead:
 *
 *   - positions: anchor-relative corner coordinates, shared (shared_ptr)
 *     across jobs and rebuilt only per geometry revision,
 *   - scalars:   per-corner values copied per job (the only per-tick cost),
 *   - bandLevels / isoLevels: the class edges to march.
 *
 * computeContourJob() is a pure function of that snapshot — no Qt Quick, no
 * shared state — so it can run on any pool thread and is unit-testable
 * headlessly (tests/unit/test_contourjob.cpp locks its output to the
 * synchronous marching path bit-for-bit).
 *
 * Staleness is handled by the caller via Qsg2DAsyncResult: results carry a
 * generation token and stale output is dropped, never applied.
 */
#ifndef OPENSWMM_RENDER_CONTOURJOB_H
#define OPENSWMM_RENDER_CONTOURJOB_H

#include "contour/marchingtriangles.h"

#include <array>
#include <memory>
#include <vector>

namespace OpenSWMM::Render
{

struct ContourJobInput
{
    /*! Anchor-relative corner positions of one triangle. Float is exact
     *  enough here because the anchor shift keeps magnitudes small. */
    struct TriPos
    {
        float ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
    };

    /*! Shared, immutable per-geometry positions (one entry per triangle). */
    std::shared_ptr<const std::vector<TriPos>> positions;

    /*! Per-corner scalar values, parallel to positions. Copied once per
     *  frame and shared between the band and isoline jobs of that frame. */
    std::shared_ptr<const std::vector<std::array<float, 3>>> scalars;

    /*! Class edges for filled bands; empty = skip the band pass. */
    std::vector<double> bandLevels;

    /*! Iso levels for contour lines; empty = skip the line pass. */
    std::vector<double> isoLevels;

    /*! Forwarded to marchingTrianglesIsobands. Depth renderers pass false
     *  so a flat dry cell outside the class range stays unbanded — must
     *  match the synchronous path exactly. */
    bool clampUniformOutsideRange = true;
};

struct ContourJobOutput
{
    std::vector<OpenSWMM::Contour::IsoBandPolygon> bands;
    std::vector<OpenSWMM::Contour::IsoLineSegment> segs;
};

/*! Pure worker: march the snapshot. Safe on any thread. */
[[nodiscard]] ContourJobOutput computeContourJob(const ContourJobInput &in);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_CONTOURJOB_H
