/*!
 * \file   swmm2dmeshqsgrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 *
 * Qt Quick Scene Graph renderer for SWMM2DMeshLayer.  Replaces the
 * per-triangle QPainter path in MeshGraphicsItem::paint() with a single
 * GPU draw call, eliminating O(N) QPainter state changes.
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
 */
#ifndef SWMM2DMESHQSGRENDERER_H
#define SWMM2DMESHQSGRENDERER_H

#include "contour/marchingtriangles.h"
#include "map/mapextent.h"
#include "render/colorramp.h"
#include "render/qsg2ddirtystate.h"
#include "render/qsg2dlodpolicy.h"

#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QSet>
#include <QString>

#include <limits>
#include <vector>

class QSGTexture;
class SWMM2DMeshLayer;

class SWMM2DMeshQSGRenderer : public QQuickItem
{
    Q_OBJECT

public:
    explicit SWMM2DMeshQSGRenderer(QQuickItem *parent = nullptr);
    ~SWMM2DMeshQSGRenderer() override;

    void setLayer(SWMM2DMeshLayer *layer);
    void setMapExtent(const MapExtent &extent);

    /*! Monotonic counter bumped every time an external change marks this
     *  renderer's content dirty. MapCanvas compares it against the value
     *  recorded when it last grabbed the QSG framebuffer, so a change that
     *  arrives after the canvas cleared its own dirty flag still forces a
     *  regrab instead of compositing a stale frame. Extent and layer-pointer
     *  changes are keyed separately by the canvas and do not bump this. */
    [[nodiscard]] quint64 contentRevision() const noexcept
    { return m_contentRev; }

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    /*! Mark content dirty from an external signal: bump the revision the
     *  canvas polls, then schedule the QQuickItem update. */
    void noteContentChanged() { ++m_contentRev; update(); }

    QPointer<SWMM2DMeshLayer> m_layer;
    quint64                   m_contentRev = 0;
    MapExtent                 m_extent;

    // Fixed scene-space anchor (bbox centre) — keeps float vertex coords
    // small even in UTM coordinates; stable across pans. Recomputed only
    // when the geometry domain is dirty.
    double m_anchorX = 0.0;
    double m_anchorY = 0.0;

    // ── QSG-2D-1M dirty/LOD state ──────────────────────────────────────
    OpenSWMM::Render::Qsg2DDirtyState m_dirty;

    /*! LOD content key + coverage rect of the last content build. Zoom
     *  within the same key and pans inside the coverage are matrix-only. */
    quint64 m_builtLodKey  = ~quint64(0);
    QRectF  m_builtCoverage;
    int     m_lastBucket   = -1;
    int     m_lastZoomStep = std::numeric_limits<int>::min();

    /*! Snapshots for classifying the layer's catch-all repaintRequested
     *  into Geometry / Selection / Style domains. */
    quint64   m_lastGeomRev = ~quint64(0);
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
    quint64 m_isobandCacheRev    = 0;
    double  m_isobandCacheZMin   = 0.0;
    double  m_isobandCacheZMax   = 0.0;
    int     m_isobandCacheBands  = -1;
    quint64 m_isobandCacheScheme = 0;   ///< Slice US.3 — ClassificationScheme::revision()
    std::vector<OpenSWMM::Contour::IsoBandPolygon> m_cachedBands;

    quint64 m_isolineCacheRev    = 0;
    double  m_isolineCacheZMin   = 0.0;
    double  m_isolineCacheZMax   = 0.0;
    int     m_isolineCacheLevels = -1;
    quint64 m_isolineCacheScheme = 0;   ///< Slice US.3 — ClassificationScheme::revision()
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
    bool                       m_fillCacheValid       = false;
    quint64                    m_fillCacheRev         = 0;
    double                     m_fillCacheZMin        = 0.0;
    double                     m_fillCacheZMax        = 0.0;
    double                     m_fillCacheAzimuth     = 0.0;
    double                     m_fillCacheAltitude    = 0.0;
    double                     m_fillCacheZExag       = 0.0;
    double                     m_fillCacheShadowFloor = 0.0;
    double                     m_fillCacheStrength    = 0.0;
    RasterColorRamp            m_fillCacheRamp;
    std::vector<quint32>       m_cachedFillRgb;       ///< packed 0x01RRGGBB; 0 = not yet computed
};

#endif // SWMM2DMESHQSGRENDERER_H
