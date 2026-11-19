/**
 * @file      maskclipresolver.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Resolve a MaskSpec to a QPainterPath in scene coords (Slice
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
 *         Coordinates come from the source vector layer's existing scene
 *         transform and Y flip. The resolver does not compare host/source CRSes
 *         or independently validate transformation success.
 *
 *         A process-local cache retains paths, including empty paths, by source
 *         layer ID and live object pointer. In-place geometry/transform changes
 *         require explicit invalidation. Cache access and source traversal run
 *         on the layer's owning GUI thread; no synchronization is provided.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
struct MaskClipResult {
    bool ok = false;
    QPainterPath path;
    MaskMode mode = MaskMode::ClipInside;
};

/*!
 * \brief Resolve \p spec against \p hostLayer's workspace.
 *
 *        Walks the source layer's polygon features and builds a
 *        single `QPainterPath` by appending rings, without a Boolean union. Returns
 *        `ok=false` when the spec is disabled, the source can't be
 *        found, the source isn't a polygon vector layer, or no
 *        polygon features have geometry.
 *
 *        Uses the process-local source-ID/pointer cache; misses traverse the
 *        source layer's polygon rings through appendScenePolygonsTo().
 *
 *        Consumed by QPainter-based hosts (SWMM1D / 2D mesh / 2D
 *        results graphics items) via `QPainter::setClipPath`. The
 *        QSG-rendered SWMM2DMeshQSGRenderer doesn't apply the clip
 *        in v1 — QSGClipNode requires triangulating the path into a
 *        stencil mesh, which is the named Z.14-paint-qsg follow-up.
 * @param hostLayer Optional borrowed layer whose workspace is searched by exact
 * source ID. Null or unavailable workspaces yield no clip.
 * @param spec Mask settings; disabled/empty-source specifications yield no clip.
 * @return Owned result with mode copied verbatim and ok true only for a nonempty
 * path. ClipOutside does not invert the path here; the paint host interprets
 * mode. Source layers are not owned, and host visibility is not checked. Ring
 * overlaps/holes follow the resulting Qt path's fill rule. Perform calls and
 * invalidation on the owning GUI thread; no transactional/source-cursor guarantee is added.
 */
[[nodiscard]] MaskClipResult resolveMaskClip(OpenSWMMVisLayer *hostLayer, const MaskSpec &spec);

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
 * @param layerId Exact cached source ID to remove; empty clears every entry.
 * No lookup/rebuild, layer mutation or repaint notification occurs. Call on the
 * same GUI thread used by resolveMaskClip().
 */
void invalidateMaskClipCache(const QString &layerId = QString());

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MASKCLIPRESOLVER_H
