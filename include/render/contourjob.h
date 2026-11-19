/**
 * @file      contourjob.h
 * @brief     QSG-2D-1M Phase 7 — thread-safe contour job input/output for asynchronous
 *            marching-triangles recomputation.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * Staleness is handled by the caller via Qsg2DAsyncResult: the caller associates
 * a generation token with the result and drops stale output before applying it.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_CONTOURJOB_H
#define OPENSWMM_RENDER_CONTOURJOB_H

#include "contour/marchingtriangles.h"
#include "render/watergeometrychunks.h"
#include <QRectF>

#include <array>
#include <memory>
#include <vector>

namespace OpenSWMM::Render
{

struct ContourJobInput {
    /*! Anchor-relative corner positions of one triangle. Float is exact
     *  enough here because the anchor shift keeps magnitudes small. */
    struct TriPos {
        float ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
    };

    /*! Shared, immutable per-geometry positions (one entry per triangle). */
    std::shared_ptr<const std::vector<TriPos>> positions;

    /*! Per-corner scalar values, parallel to positions. Copied once per
     *  frame and shared between the band and isoline jobs of that frame. */
    std::shared_ptr<const std::vector<std::array<float, 3>>> scalars;

    /*! Optional triangle selection from the renderer's coverage index. Null
     * means the full snapshot; an empty selection means no visible triangles.
     * Indices outside either array are ignored. */
    std::shared_ptr<const std::vector<size_t>> triangleIndices;

    /*! Class edges for filled bands; empty = skip the band pass. */
    std::vector<double> bandLevels;

    /*! Iso levels for contour lines; empty = skip the line pass. */
    std::vector<double> isoLevels;

    /*! Additional physical visibility cutoff; the color minimum also clips.
     *  Depth renderers mask films first, then clip at zero; upper values saturate. */
    double minimumVisibleValue = -std::numeric_limits<double>::infinity();
    /*! Optional exact fan geometry for the GPU consumer; legacy callers
     *  keep their polygon output. Coverage is anchor-relative, if valid. */
    bool packBands = false;
    QRectF bandCoverage;
};

struct PackedBandVertex {
    float x = 0, y = 0;
    quint32 bandIndex = 0;
};

struct ContourJobOutput {
    std::vector<OpenSWMM::Contour::IsoBandPolygon> bands;
    std::vector<OpenSWMM::Contour::IsoLineSegment> segs;
    std::vector<WaterGeometryChunk<PackedBandVertex>> packedBands;
};

/** @brief Compute filled contour bands and isolines from an immutable triangle snapshot.
 * @param in Borrowed job input. Shared position/scalar/selection arrays must stay
 * immutable throughout the call, including through other aliases. Coordinates
 * are anchor-relative scene units; levels/cutoff use the scalar values' units.
 * Finite, ascending band edges and finite isoline levels follow the marching
 * helper contracts. Missing arrays or no paired triangles return empty outputs.
 * @return Owned output without a generation token. Pairs only the shorter of
 * the position/scalar arrays; optional out-of-range triangle indices are ignored
 * and duplicate indices remain duplicated. packBands selects indexed triangle
 * chunks instead of polygon bands. Band coverage only culls packed polygons by
 * bounding-box overlap; it does not clip them or apply to isolines. The additional
 * minimum-visible cutoff applies only to bands. No cancellation or stale-result
 * check occurs here; allocation exceptions propagate. Safe on a worker thread
 * when the snapshot has no concurrent mutation. */
[[nodiscard]] ContourJobOutput computeContourJob(const ContourJobInput &in);
/** @brief Replace a caller-owned contour result, reusing packed chunk slots where possible.
 * @param in Immutable input with the return-by-value overload's unit, ordering,
 * lifetime and selection requirements.
 * @param[in,out] out Exclusively owned output; existing bands/segments are cleared
 * before computation and stale packed chunks are removed. The output must not
 * alias input storage or be read concurrently. Allocation failures can leave a
 * partial replacement; no transactional rollback or cancellation is provided. */
void computeContourJob(const ContourJobInput &in, ContourJobOutput &out);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_CONTOURJOB_H
