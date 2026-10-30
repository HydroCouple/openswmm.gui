/**
 * @file      swmm2dmeshqsgrenderer.h
 * @brief     Qt scene graph rendering of 2D mesh geometry.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 *
 * Qt Quick Scene Graph renderer for SWMM2DMeshLayer.  Replaces the
 * per-triangle QPainter path in MeshGraphicsItem::paint() with batched
 * GPU geometry passes, eliminating O(N) QPainter state changes.
 *
 * Rendering:
 *   Pass 1 — filled triangles: QSGGeometry::ColoredPoint2D +
 *             QSGVertexColorMaterial.  Per-vertex colors encode elevation
 *             colour ramp × hillshade, computed once per geometry/zoom
 *             change and reused on pan.
 *   Pass 2 — edges: two QSGGeometryNode (thin/wide), thick-segment quads
 *             (DrawTriangles) so line widths are consistent across RHI
 *             backends (Metal/Vulkan/D3D11).
 *
 * QSG-2D-1M (2026-07-05) — re-architected for ~1M-cell meshes:
 *   - Dirty domains (Qsg2DDirtyState) replace the single m_contentDirty:
 *     pan is matrix-only, zoom rebuilds only across LOD-key changes, and a
 *     selection change rebuilds only the §V overlay nodes.
 *   - A deterministic LOD policy (Qsg2DLodPolicy) gates dense passes: at
 *     Far zoom the fill switches to the layer's coarse overview
 *     (m_overviewTris — the same aggregate the CPU painter uses), and the
 *     wireframe / vertex markers / contour labels are suppressed.
 *   - Content is culled to a coverage rect larger than the viewport; pans
 *     inside it stay matrix-only, leaving it triggers one LOD rebuild.
 *   - OPENSWMM_RENDER_PERF=1 logs per-sync dirty reasons and per-pass
 *     built-vertex / uploaded-byte counters (Qsg2DRenderStats).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef SWMM2DMESHQSGRENDERER_H
#define SWMM2DMESHQSGRENDERER_H

#include "contour/marchingtriangles.h"
#include "map/mapextent.h"
#include "render/colorramp.h"
#include "render/qsg2ddirtystate.h"
#include "render/qsg2dlodpolicy.h"

#include <QByteArray>
#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QSet>
#include <QString>

#include <limits>
#include <vector>

class QSGTexture;
class QSGGeometryNode;
class QSGTransformNode;
class SWMM2DMeshLayer;

class SWMM2DMeshQSGRenderer : public QQuickItem
{
    Q_OBJECT

public:
    /**
     * @brief Create an unbound ItemHasContents item with initial dirty rendering state.
     * @param[in] parent Optional Qt Quick visual parent; Qt item ownership rules apply.
     * @note Configuration runs on GUI thread; node/resource callbacks run in scene-graph context.
     *   Host must serialize mutable layer geometry/style/selection access with sync callbacks; no
     *   background snapshot or locking here. Construction creates no mesh, GPU nodes or engine
     *   objects.
     */
    explicit SWMM2DMeshQSGRenderer(QQuickItem *parent = nullptr);
    /**
     * @brief Schedule cached label textures for deferred destruction and release item-owned value
     *   caches/connections.
     * @note Does not destroy the borrowed model. Qt owns the returned scene graph. Deferred texture
     *   deletion relies on the texture owning thread processing DeferredDelete events; destruction
     *   does not wait for completion.
     */
    ~SWMM2DMeshQSGRenderer() override;

    /**
     * @param[in] layer Optional borrowed mesh layer guarded by QPointer; null removes binding.
     * @brief Bind repaint/style notifications and invalidate geometry/selection/coverage state.
     * @note Equal pointer no-op. Disconnects old layer and all its current sublayers from this
     *   item. New edge/BC invalidations explicitly mark Style, while catch-all repaint is
     *   classified at sync using geometry and selection snapshots. Clears label texture cache,
     *   resets geometry epoch/selection snapshots/coverage/LOD key, marks Layer dirty and schedules
     *   update. Pointer change does not bump contentRevision.
     * @note No engine mutation or ownership transfer, no explicit destroyed-to-update hook. Host
     *   must redraw after layer destruction and supply coherent native-to-scene caches and item
     *   opacity/visibility.
     */
    void setLayer(SWMM2DMeshLayer *layer);
    /**
     * @param[in] extent Canvas-map bounds before scene Y inversion; copied without validation.
     * @brief Record changed view bounds, classify fuzzy width/height change as zoom, and schedule
     *   update.
     * @note Equality no-op. Geometry is not built here; sync uses LOD key and cached coverage to
     *   choose a rebuild or transform-only update. Does not bump contentRevision, reproject source
     *   geometry or change item dimensions.
     */
    void setMapExtent(const MapExtent &extent);

    /*! Monotonic counter bumped every time an external change marks this
     *  renderer's content dirty. MapCanvas compares it against the value
     *  recorded when it last grabbed the QSG framebuffer, so a change that
     *  arrives after the canvas cleared its own dirty flag still forces a
     *  regrab instead of compositing a stale frame. Extent and layer-pointer
     *  changes are keyed separately by the canvas and do not bump this.
     * @return External-change counter, incremented by noteContentChanged; layer identity/extent
     *   changes excluded and wrap is unchecked. It does not indicate GPU completion or cache
     *   validity on its own.
     */
    [[nodiscard]] quint64 contentRevision() const noexcept { return m_contentRev; }

protected:
    /**
     * @param[in] oldNode Optional previous transform root owned by Qt and created by this renderer.
     *   Deleted if invalid state, or replaced if it differs from stored root.
     * @brief Synchronize anchored mesh passes from coherent layer caches, current styles,
     *   highlights, extent and item size.
     * @return Root transferred back to Qt ownership, or null after deleting previous tree for
     *   missing layer, invalid extent or nonpositive item width/height. Unnamed UpdatePaintNodeData
     *   is ignored. Layer visibility/overall opacity are host responsibilities, not tested/applied
     *   here.
     * @note Render-resource context only; no concurrent model mutation. New/different root
     *   recreates named pass handles and marks Layer dirty. Every callback updates map-to-item
     *   transform with anchor-relative float geometry. Same-LOD views inside cached coverage reuse
     *   geometry; builds use viewport plus half-viewport margin and geometry-bbox anchor.
     * @note Geometry/Style/LOD rebuild fill, bands, edges, contours and markers;
     *   Geometry/Selection/LOD rebuild highlighted cells, edge slots and vertices. Selection alone
     *   does not retessellate static passes. Far fill uses overview plus native cells at least16
     *   square pixels; BC ring and enabled elevation contours survive dense-wireframe suppression.
     *   Markers/labels obey LOD, selection remains exact at every LOD.
     * @note Fill RGB caches include geometry/style/attribute revisions, classification range,
     *   attribute key, hillshade inputs, ramp and categorical palette identity; alpha is applied
     *   during upload. Isolines/bands cache marching-triangle output by geometry/range/level
     *   count/scheme revision. Geometry buffers split at 65532 vertices; selection invalid indices
     *   are skipped. No engine writes or completion/failure status is returned for individual
     *   passes.
     * @note Offscreen-bbox path empties ordinary fill/line/label/node/selection passes and
     *   invalidates built coverage/LOD without consuming pending dirtiness; it does not explicitly
     *   clear the coupled-marker node. Label texture cache is keyed solely by formatted level text,
     *   so colour/DPR changes do not themselves replace an existing text entry.
     */
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    /*! Mark content dirty from an external signal: bump the revision the
     *  canvas polls, then schedule the QQuickItem update.
     * @note Increments revision and schedules update only; caller must set the corresponding dirty
     *   domain first. No cache clearing, geometry build or model write. Serialized with host
     *   framebuffer revision queries.
     */
    void noteContentChanged()
    {
        ++m_contentRev;
        update();
    }

    QPointer<SWMM2DMeshLayer> m_layer;
    quint64 m_contentRev = 0;
    MapExtent m_extent;

    /*! Named handles to every child of the scene root, populated when the
     *  root is (re)built in updatePaintNode. Replaces the old positional
     *  nextSibling() re-walk, which silently static_cast a plain QSGNode to
     *  QSGGeometryNode (memory corruption, not a loud failure) whenever the
     *  construction order drifted. m_sceneRoot guards staleness: when the
     *  scene graph hands us a different (or null) root, the whole tree and
     *  these pointers are rebuilt together. Pointers are owned by the scene
     *  graph via the root's child list — never delete individually. */
    struct SceneNodes {
        QSGGeometryNode *tri = nullptr;
        QSGGeometryNode *isoband = nullptr;
        QSGGeometryNode *edgeThin = nullptr;
        QSGGeometryNode *edgeWide = nullptr;
        QSGGeometryNode *edgeBc[6] = {}; // non-Wall BC types
        QSGGeometryNode *contour = nullptr;
        QSGNode *contourLabels = nullptr;
        QSGGeometryNode *nodeMark = nullptr;
        QSGGeometryNode *coupledMark = nullptr;
        QSGGeometryNode *selTri = nullptr;
        QSGGeometryNode *selEdge = nullptr;
        QSGGeometryNode *selVert = nullptr;
    };
    SceneNodes m_nodes;
    QSGTransformNode *m_sceneRoot = nullptr;

    // Fixed scene-space anchor (bbox centre) — keeps float vertex coords
    // small even in UTM coordinates; stable across pans. Recomputed only
    // when the geometry domain is dirty.
    double m_anchorX = 0.0;
    double m_anchorY = 0.0;

    // ── QSG-2D-1M dirty/LOD state ──────────────────────────────────────
    OpenSWMM::Render::Qsg2DDirtyState m_dirty;

    /*! LOD content key + coverage rect of the last content build. Zoom
     *  within the same key and pans inside the coverage are matrix-only. */
    quint64 m_builtLodKey = ~quint64(0);
    QRectF m_builtCoverage;
    int m_lastBucket = -1;
    int m_lastZoomStep = std::numeric_limits<int>::min();

    /*! Snapshots for classifying the layer's catch-all repaintRequested
     *  into Geometry / Selection / Style domains. */
    quint64 m_lastGeomRev = ~quint64(0);
    QSet<int> m_lastSelVerts;
    QSet<int> m_lastSelEdges;
    QSet<int> m_lastSelTris;

    // ── Isoline-label texture cache ────────────────────────────────────
    // Keyed by the rendered label string ("12.4", "20.0", …). QSGTexture
    // ownership is held by us; we delete them in ~SWMM2DMeshQSGRenderer
    // via deleteLater() because QSGTextures must be released on the
    // QQuickWindow's render thread. Cached so we don't rasterise the
    // same level value every frame.
    mutable QHash<QString, QSGTexture *> m_labelTextureCache;

    /**
     * @brief Call deleteLater on every cached nonnull label texture, then discard cache entries
     *   immediately.
     * @note Does not delete label scene nodes, wait for GPU use/deletion, or guarantee the owning
     *   thread processes deferred events. Host coordinates render-resource lifetime when
     *   rebinding/destroying; no model mutation.
     */
    void clearLabelTextureCache();

    // ── Contour-geometry cache ─────────────────────────────────────────
    // Marching-triangles output (isobands + isolines) is invariant under
    // pan and zoom — it only depends on the scene-space triangle set, the
    // anchor (which moves with the mesh, not the view), and the level
    // counts derived from the band/line styles. Cache it here and reuse
    // on every paint until any of the key inputs change.
    //
    // Cache key: (geomRevision, zMin, zMax, level count).
    // geomRevision covers the anchor (ox, oy) and the sceneTris vector
    // since both are rewritten in lockstep inside rebuildSceneGeometry().
    // zMin/zMax are belt-and-suspenders for the (unlikely) case where the
    // revision didn't change but z-range did.
    quint64 m_isobandCacheRev = 0;
    double m_isobandCacheZMin = 0.0;
    double m_isobandCacheZMax = 0.0;
    int m_isobandCacheBands = -1;
    quint64 m_isobandCacheScheme = 0; ///< Slice US.3 — ClassificationScheme::revision()
    std::vector<OpenSWMM::Contour::IsoBandPolygon> m_cachedBands;

    quint64 m_isolineCacheRev = 0;
    double m_isolineCacheZMin = 0.0;
    double m_isolineCacheZMax = 0.0;
    int m_isolineCacheLevels = -1;
    quint64 m_isolineCacheScheme = 0; ///< Slice US.3 — ClassificationScheme::revision()
    std::vector<OpenSWMM::Contour::IsoLineSegment> m_cachedSegs;

    // ── Pass 1 per-triangle fill-colour cache ──────────────────────────
    // The ramp branch of Pass 1 computes, for every visible triangle, an
    // elevation-ramp sample × hillshade lighting × alpha blend. The RGB
    // part of that computation is invariant under pan and zoom (it only
    // depends on the mesh geometry, the elevation range, the ramp, and
    // the hillshade direction / strength parameters). Cache it here so
    // zoom interactions and style-edits don't re-run ~15 float ops + a
    // sqrt per triangle every paint tick.
    //
    // Storage is per-scene-triangle (NOT per-visible-triangle), packed as
    // a quint32 holding the RGB in the low 24 bits. Alpha is intentionally
    // outside the key — it depends on the active flag and the sublayer
    // opacity slider, both cheap to apply at upload time.
    //
    // Cache key (compared structurally each paint):
    //   geomRevision   — mesh geometry version from the layer
    //   zMin / zMax    — elevation range used to normalise zAvg
    //   azimuth, altitude, zExaggeration, shadowFloor, strength
    //                  — hillshade lighting inputs
    //   ramp           — RasterColorRamp; compared structurally (minValue,
    //                    maxValue, clampMin/Max, interp, stops)
    //
    // The flat-fill branch (useRamp=false / no elevation) produces a
    // constant colour for every triangle and is not cached. The flat
    // branch also doesn't invalidate the cache, so toggling the ramp off
    // and back on can reuse a still-valid cache if all other inputs
    // matched.
    //
    // Lazy fill: only visible triangles are computed per paint; entries
    // for off-screen triangles stay at the sentinel 0u until their
    // triangle becomes visible. Bit 24 is set on every written entry so
    // a legitimate black triangle (RGB 0,0,0) is still distinguishable
    // from "not yet computed".
    //
    // colorByAttribute (MeshFillStyle) widens two of these fields:
    //   m_fillCacheRev      also XORs the layer's attrRevision(), so a
    //                       per-cell Manning's edit invalidates the colours.
    //   m_fillCacheZMin/Max hold the *classification* range — the attribute's
    //                       observed span in attribute mode, the elevation
    //                       span in the historic elevation mode.
    //   m_fillCacheAttrKey  which attribute the cached colours were built
    //                       from; switching attributes changes nothing else
    //                       in the key, so it must be compared on its own.
    bool m_fillCacheValid = false;
    quint64 m_fillCacheRev = 0;
    QByteArray m_fillCacheAttrKey;
    double m_fillCacheZMin = 0.0;
    double m_fillCacheZMax = 0.0;
    double m_fillCacheAzimuth = 0.0;
    double m_fillCacheAltitude = 0.0;
    double m_fillCacheZExag = 0.0;
    double m_fillCacheShadowFloor = 0.0;
    double m_fillCacheStrength = 0.0;
    RasterColorRamp m_fillCacheRamp;
    std::vector<quint32> m_cachedFillRgb; ///< packed 0x01RRGGBB; 0 = not yet computed
};

#endif // SWMM2DMESHQSGRENDERER_H
