/*!
 * \file   maskclipresolver.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Resolve a MaskSpec to a QPainterPath in scene coords (Slice
 *         Z.14-paint).
 *
 *         The MaskSpec on a layer names a polygon source layer by id.
 *         At paint time the host layer asks this resolver to look up
 *         that source through the workspace, walk its polygon
 *         geometry, project into scene coords (Y-flipped), and return
 *         a `QPainterPath` ready for `QPainter::setClipPath`.
 *
 *         v1 supports GISVectorLayer polygon sources only. Non-polygon
 *         source geometry and non-GISVectorLayer source types return
 *         an empty path so the host falls back to unclipped paint.
 *         CRS-aware reprojection happens against the host's known
 *         transform stack; if the source and host CRSes diverge in a
 *         way the resolver can't handle (no GDAL transform available),
 *         the resolver again returns empty so paint isn't silently
 *         wrong.
 *
 *         No caching for v1 — the resolver is called per paint and
 *         walks the source layer's features each frame. A
 *         revision-keyed cache is the named Z.14-paint-cache follow-up;
 *         the present implementation is correct but linear in source-
 *         feature count.
 */

#ifndef OPENSWMM_RENDER_MASKCLIPRESOLVER_H
#define OPENSWMM_RENDER_MASKCLIPRESOLVER_H

#include "render/maskspec.h"

#include <QPainterPath>

class OpenSWMMVisLayer;

namespace OpenSWMM::Render
{

/*!
 * \struct MaskClipResult
 * \brief Outcome of resolving a MaskSpec.
 *
 *        `path` is the clip path in scene coords (Y-flipped, same
 *        space the host's paint() draws into). When the resolver
 *        bails — disabled spec, unknown source, non-polygon source —
 *        `path` is empty and `ok` is false; the host should skip
 *        clipping in that case.
 */
struct MaskClipResult
{
    bool         ok = false;
    QPainterPath path;
    MaskMode     mode = MaskMode::ClipInside;
};

/*!
 * \brief Resolve \p spec against \p hostLayer's workspace.
 *
 *        Walks the source layer's polygon features and builds a
 *        single `QPainterPath` (union of all features). Returns
 *        `ok=false` when the spec is disabled, the source can't be
 *        found, the source isn't a polygon vector layer, or no
 *        polygon features have geometry.
 *
 *        Pure function — no caching, no observer registration. The
 *        host calls this once per paint; small / moderate source
 *        layers (study-area / watershed boundaries are typically
 *        single polygons) make this cheap.
 *
 *        Consumed by QPainter-based hosts (SWMM1D / 2D mesh / 2D
 *        results graphics items) via `QPainter::setClipPath`. The
 *        QSG-rendered SWMM2DMeshQSGRenderer doesn't apply the clip
 *        in v1 — QSGClipNode requires triangulating the path into a
 *        stencil mesh, which is the named Z.14-paint-qsg follow-up.
 */
[[nodiscard]] MaskClipResult
resolveMaskClip(OpenSWMMVisLayer *hostLayer, const MaskSpec &spec);

/*!
 * \brief Slice Z.14-paint-cache — clear cached clip paths.
 *
 *        The resolver caches each source layer's resolved
 *        `QPainterPath` keyed by layer id, so repeated paint passes
 *        skip the OGR walk. Callers should invoke this when a source
 *        layer's geometry changes (e.g. a vector layer reload) so the
 *        next paint rebuilds. Passing an empty \p layerId clears the
 *        whole cache.
 *
 *        Cached entries also auto-evict when the source layer is
 *        destroyed (the cache stores a `QPointer` and re-verifies on
 *        each hit). External invalidation is only needed for in-place
 *        geometry edits — destruction is handled automatically.
 */
void invalidateMaskClipCache(const QString &layerId = QString());

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MASKCLIPRESOLVER_H
