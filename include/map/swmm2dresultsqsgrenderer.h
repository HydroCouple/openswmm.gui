/*!
 * \file   swmm2dresultsqsgrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * VS.8 — Qt Quick Scene Graph renderer for SWMM2DResultsLayer. The GPU
 * replacement for the QGraphicsItem QPainter passes (the "final
 * paint-replacement slice" promised in swmm2dresultslayer.h §S5.6).
 *
 * Rendering (node z-order, bottom → top):
 *   Pass 2 — filled contour bands (marching-triangles isobands or flat
 *            per-cell classification, per ContourBandStyle). This is now the
 *            depth fill; dry cells stay transparent so the SWMM2DMeshLayer
 *            terrain shows through.
 *   Pass 3 — isolines (thick-segment quads; separate node for index
 *            contours). Dash patterns are not supported on the GPU path —
 *            lines render solid.
 *   Pass 3b — isoline labels: rasterised textures placed along chained
 *            polylines every ~250 screen px, rotated to the line direction.
 *   Pass 4 — velocity-vector glyphs (per-glyph colour via
 *            VelocityVectorStyle::colorForSpeed); also convey flow direction.
 *   Pass 6 — cell-highlight overlay (translucent cyan fill + gold edges,
 *            matching the CPU CF.3 pass).
 *
 * QSG-2D-1M (2026-07-05) — the renderer was re-architected for ~1M-cell
 * meshes:
 *
 *   - Dirty domains (Qsg2DDirtyState) replace the single m_contentDirty:
 *     pan is matrix-only, zoom rebuilds only across LOD-key changes,
 *     selection touches only the highlight nodes, and a time tick rebuilds
 *     only the data-dependent passes (fills / bands / isolines / vectors)
 *     while mesh edges and vertex markers persist.
 *   - A deterministic LOD policy (Qsg2DLodPolicy) gates dense passes:
 *     at Far zoom no wireframe, no vertex markers, no labels, no dense
 *     per-cell velocity glyphs, and contour bands fall back to the flat
 *     per-cell classification (visually identical once cells are subpixel).
 *   - A MeshRenderChunkIndex (keyed by the layer's geomRevision) batches
 *     visibility culling: fully-visible chunks skip per-element bbox tests.
 *   - Content is built for a coverage rect larger than the viewport; pans
 *     inside the coverage are pure transforms, leaving it triggers one
 *     LOD-domain rebuild.
 *   - The smooth depth fill uses persistent indexed geometry
 *     (MeshStaticGeometryBuffers): shared vertex positions upload once per
 *     geometry revision; time ticks rewrite colors + wet-cell indices in
 *     place. Set OPENSWMM_QSG_INDEXED_FILL=0 to fall back to the expanded
 *     per-corner path.
 *   - OPENSWMM_RENDER_PERF=1 logs per-sync dirty reasons and per-pass
 *     built-vertex / uploaded-byte counters (Qsg2DRenderStats).
 *
 * Hosted as the "results2d" item in resources/qml/swmmlayer.qml, BELOW the
 * 1D SWMMLayerQSGRenderer so the network draws above the flood map.
 * MapCanvas drives it synchronously from paintEvent Layer 2b, exactly like
 * the 1D renderer (repaint() + grabFramebuffer()).
 */
#ifndef SWMM2DRESULTSQSGRENDERER_H
#define SWMM2DRESULTSQSGRENDERER_H

#include "map/mapextent.h"
#include "render/contourjob.h"
#include "render/meshrenderchunkindex.h"
#include "render/meshstaticgeometrybuffers.h"
#include "render/qsg2dasyncresult.h"
#include "render/qsg2ddirtystate.h"
#include "render/qsg2dlodpolicy.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QSet>
#include <QString>

#include <limits>
#include <memory>
#include <vector>

class QSGTexture;
class SWMM2DResultsLayer;

namespace OpenSWMM::Contour {
struct IsoBandPolygon;
struct IsoLineSegment;
}

class SWMM2DResultsQSGRenderer : public QQuickItem
{
    Q_OBJECT

public:
    explicit SWMM2DResultsQSGRenderer(QQuickItem *parent = nullptr);
    ~SWMM2DResultsQSGRenderer() override;

    void setLayer(SWMM2DResultsLayer *layer);
    void setMapExtent(const MapExtent &extent);

    /*! Drop caches and mark content dirty — used by MapCanvas when the
     *  QSG/CPU ownership toggles so a stale frame is never composited. */
    void forceRebuild();

signals:
    /*! QSG-2D-1M Phase 7 — emitted when an asynchronous derived-geometry
     *  job (contour marching) publishes a result. MapCanvas connects this
     *  to its framebuffer-regrab path: without it, the freshly synced
     *  bands would sit in the offscreen QSG widget until the next
     *  layer-driven repaint. */
    void contentReady();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    QPointer<SWMM2DResultsLayer> m_layer;
    MapExtent                    m_extent;

    // Fixed scene-space anchor (bbox centre) — keeps float vertex coords
    // small even in UTM coordinates; stable across pans. Recomputed only
    // when the geometry domain is dirty.
    double m_anchorX = 0.0;
    double m_anchorY = 0.0;

    // ── QSG-2D-1M dirty/LOD state ──────────────────────────────────────
    OpenSWMM::Render::Qsg2DDirtyState m_dirty;

    /*! LOD content key + coverage rect of the last content build. Zoom
     *  within the same key and pans inside the coverage are matrix-only. */
    quint64 m_builtLodKey   = ~quint64(0);
    QRectF  m_builtCoverage;
    int     m_lastBucket    = -1;
    int     m_lastZoomStep  = std::numeric_limits<int>::min();

    /*! Snapshots for classifying ambiguous repaintRequested emissions. */
    quint64   m_lastGeomRev = ~quint64(0);
    QSet<int> m_lastHighlight;

    /*! Layer frame index last rebuilt into the QSG tree — the Data-domain
     *  snapshot (also guards against a consumed currentTimeChanged signal
     *  racing the canvas framebuffer grab). */
    int  m_lastRenderedTime = -1;

    /*! Render chunk index over tri/edge bboxes, keyed by geomRevision. */
    OpenSWMM::Render::MeshRenderChunkIndex m_chunks;
    quint64 m_chunksRev = ~quint64(0);

    /*! Persistent shared-vertex positions + tri indices for the indexed
     *  smooth-fill path, keyed by geomRevision. */
    OpenSWMM::Render::MeshStaticGeometryBuffers m_static;

    /*! Phase 8 — smooth-fill GPU mode actually applied to the node, so a
     *  mode flip (env toggle / fallback) swaps geometry+material once. */
    enum class SmoothFillMode { VertexColor, Shader };
    SmoothFillMode m_smoothFillMode = SmoothFillMode::VertexColor;

    /*! Phase 8 — last baked ramp LUT; a new texture is created only when
     *  the baked pixels actually change (style/ramp/opacity edit). */
    QImage m_smoothLutImage;

    // ── Phase 7: async contour recomputation ───────────────────────────
    /*! Everything that identifies one marching product. Two jobs (bands,
     *  isolines) each track the key in flight and the key published. */
    struct ContourJobKey
    {
        int     time      = -1;
        double  lo        = 0.0;
        double  hi        = 0.0;
        int     bandCount = -1;
        quint64 paramsRev = 0;      ///< scheme revision / iso params hash
        size_t  tris      = 0;
        quint64 geomRev   = ~quint64(0);
        bool    valid     = false;
        bool operator==(const ContourJobKey &o) const
        {
            return valid == o.valid && time == o.time && lo == o.lo
                && hi == o.hi && bandCount == o.bandCount
                && paramsRev == o.paramsRev && tris == o.tris
                && geomRev == o.geomRev;
        }
        bool operator!=(const ContourJobKey &o) const { return !(*this == o); }
    };

    struct AsyncContourJob
    {
        OpenSWMM::Render::Qsg2DAsyncResult<OpenSWMM::Render::ContourJobOutput> buf;
        QFutureWatcher<OpenSWMM::Render::ContourJobOutput> watcher;
        quint64       inflightGen = 0;
        ContourJobKey inflightKey;
        ContourJobKey publishedKey;
    };
    AsyncContourJob m_bandJob;
    AsyncContourJob m_isoJob;

    /*! Immutable per-geometry position snapshot shared with workers. */
    std::shared_ptr<const std::vector<OpenSWMM::Render::ContourJobInput::TriPos>>
        m_contourPositions;
    quint64 m_contourPositionsRev = ~quint64(0);

    /*! Per-frame scalar snapshot (shared by both jobs of that frame). */
    std::shared_ptr<const std::vector<std::array<float, 3>>> m_contourScalars;
    int     m_contourScalarsTime    = -1;
    quint64 m_contourScalarsGeomRev = ~quint64(0);

    void setupAsyncContourJob(AsyncContourJob &job);

    // ── Isoline-label texture cache ────────────────────────────────────
    // Keyed by "text|fontPt|halo|color" so style edits re-rasterise.
    // QSGTexture ownership is ours; released via deleteLater() (textures
    // must die on the render thread).
    mutable QHash<QString, QSGTexture *> m_labelTextureCache;
    void clearLabelTextureCache();

    // ── Per-frame contour geometry caches ──────────────────────────────
    // Marching output depends on the depth frame + classification params
    // but is invariant under pan/zoom, so ticks rebuild and interactions
    // reuse. Key: (time index, range, level params, triangle count).
    int     m_bandCacheTime  = -1;
    double  m_bandCacheLo    = 0.0;
    double  m_bandCacheHi    = 0.0;
    int     m_bandCacheCount = -1;
    size_t  m_bandCacheTris  = 0;
    quint64 m_bandCacheRev   = 0;   ///< Slice US.2 — ClassificationScheme::revision()
    std::vector<OpenSWMM::Contour::IsoBandPolygon> m_cachedBands;

    int     m_isoCacheTime   = -1;
    double  m_isoCacheLo     = 0.0;
    double  m_isoCacheHi     = 0.0;
    quint64 m_isoCacheParams = 0;   ///< hash of (mode, count, interval, base)
    size_t  m_isoCacheTris   = 0;
    std::vector<OpenSWMM::Contour::IsoLineSegment> m_cachedSegs;
};

#endif // SWMM2DRESULTSQSGRENDERER_H
