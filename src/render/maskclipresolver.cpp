/**
 * @file      maskclipresolver.cpp
 * @brief     Z.14-paint).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/maskclipresolver.h"

#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "project/openswmmvissession.h"
#include "project/openswmmvisworkspace.h"

#include <QHash>
#include <QObject>
#include <QPointer>

namespace OpenSWMM::Render
{

namespace
{

/*! Slice Z.14-paint-cache — per-process cache of resolved clip paths
 *  keyed by source-layer id. The cached entry holds a `QPointer` to
 *  the source layer so we can verify the pointer hasn't been swapped
 *  for a different layer with the same id (rare but possible across
 *  workspace open/close cycles). When the QPointer auto-nulls
 *  (layer destroyed), the next hit detects the staleness and rebuilds. */
struct CacheEntry {
    QPointer<OpenSWMMVisLayer> source;
    QPainterPath path;
};

/** @brief Access unsynchronized process-lifetime cached clip paths.
 * @return Borrowed mutable hash, used only on the owning GUI thread; callers
 * preserve ID/live-pointer validity and explicitly invalidate in-place source edits. */
QHash<QString, CacheEntry> &maskClipCache()
{
    static QHash<QString, CacheEntry> c;
    return c;
}

/*! Walk the layer tree rooted at \p root looking for a layer whose
 *  `layerId()` matches \p id. Recursive descent over children().
 * @param root Optional borrowed root in an acyclic layer tree.
 * @param id Exact case-sensitive layer ID to find.
 * @return Borrowed first depth-first match, including root, or nullptr. No
 * visibility/type filter or ownership transfer occurs. */
[[nodiscard]] OpenSWMMVisLayer *findLayerByIdRecursive(OpenSWMMVisLayer *root, const QString &id)
{
    if (!root)
        return nullptr;
    if (root->layerId() == id)
        return root;
    for (OpenSWMMVisLayer *child : root->children()) {
        if (auto *match = findLayerByIdRecursive(child, id))
            return match;
    }
    return nullptr;
}

/** @brief Find an exact layer ID through workspace sessions, then QObject-owned layers.
 * @param workspace Optional borrowed workspace on its owning GUI thread.
 * @param id Exact ID; empty returns nullptr without searching.
 * @return Borrowed first match, or nullptr; duplicate IDs resolve by traversal
 * order and no source geometry/type/visibility validation occurs here. */
[[nodiscard]] OpenSWMMVisLayer *findLayerById(OpenSWMMVisWorkspace *workspace, const QString &id)
{
    if (!workspace || id.isEmpty())
        return nullptr;
    for (OpenSWMMVisSession *session : workspace->layers()) {
        if (auto *match = findLayerByIdRecursive(session, id))
            return match;
    }
    // Current project windows also own layers directly through the workspace;
    // those layers need not belong to a registered legacy session.
    for (auto *layer : workspace->findChildren<OpenSWMMVisLayer *>()) {
        if (auto *match = findLayerByIdRecursive(layer, id))
            return match;
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------

MaskClipResult resolveMaskClip(OpenSWMMVisLayer *hostLayer, const MaskSpec &spec)
{
    MaskClipResult result;
    result.mode = spec.mode;

    if (!spec.enabled || !hostLayer || spec.sourceLayerId.isEmpty())
        return result; // ok stays false → host skips clipping

    OpenSWMMVisLayer *src = findLayerById(hostLayer->workspace(), spec.sourceLayerId);
    if (!src)
        return result;

    // v1: only GISVectorLayer polygon sources are supported. Other
    // candidate source types (SWMMModelLayer subcatchments, raster
    // outline polygons, ...) are named Z.14-paint-source-types
    // follow-ups. Non-vector sources fall through to "no clip" so the
    // host paints unclipped rather than mysteriously blank.
    auto *vec = qobject_cast<GISVectorLayer *>(src);
    if (!vec)
        return result;

    // Slice Z.14-paint-cache — cache lookup. Hit when the cached
    // entry's QPointer still points at the same source layer we
    // resolved against. Miss otherwise (pointer destroyed, layer
    // swapped, or first encounter) → walk the OGR layer.
    auto &cache = maskClipCache();
    auto it = cache.find(spec.sourceLayerId);
    if (it != cache.end()) {
        if (it->source && it->source.data() == src) {
            // Cache hit — reuse the previously-built path. Empty paths
            // (source had no polygon features) are also cached so we
            // don't re-walk an OGR layer that has nothing to give.
            if (!it->path.isEmpty()) {
                result.path = it->path;
                result.ok = true;
            }
            return result;
        }
        // Stale entry — pointer is null or different layer now. Evict
        // and fall through to rebuild.
        cache.erase(it);
    }

    QPainterPath path;
    vec->appendScenePolygonsTo(path);

    cache.insert(spec.sourceLayerId, CacheEntry{QPointer<OpenSWMMVisLayer>(src), path});

    if (path.isEmpty())
        return result;

    result.path = std::move(path);
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------

void invalidateMaskClipCache(const QString &layerId)
{
    if (layerId.isEmpty()) {
        maskClipCache().clear();
    } else {
        maskClipCache().remove(layerId);
    }
}

} // namespace OpenSWMM::Render
