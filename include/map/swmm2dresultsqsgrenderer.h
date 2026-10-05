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
 *     per-cell velocity glyphs. Exact contour bands remain available at
 *     every zoom level.
 *   - A MeshRenderChunkIndex (keyed by the layer's geomRevision) batches
 *     visibility culling: fully-visible chunks skip per-element bbox tests.
 *   - Content is built for a coverage rect larger than the viewport; pans
 *     inside the coverage are pure transforms, leaving it triggers one
 *     LOD-domain rebuild.
 *   - Depth fills use cell-owned signed VFR depths and polygons clipped at
 *     the wet boundary. Adjacent cells may carry different stages; neither
 *     shared vertex colors nor transparency can represent that boundary.
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
#include "render/qsg2ddirtystate.h"
#include "render/qsg2dlodpolicy.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QJsonObject>
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

    /*! Monotonic counter bumped every time an external change marks this
     *  renderer's content dirty. MapCanvas compares it against the value
     *  recorded when it last grabbed the QSG framebuffer, so a change that
     *  arrives after the canvas cleared its own dirty flag still forces a
     *  regrab instead of compositing a stale frame. Extent and layer-pointer
     *  changes are keyed separately by the canvas and do not bump this. */
    [[nodiscard]] quint64 contentRevision() const noexcept
    { return m_contentRev; }

    /*! Revision actually presented by the last scene-graph sync; may trail
     *  the requested frame while its complete contour geometry is prepared. */
    [[nodiscard]] quint64 displayedFrameRevision() const noexcept
    { return m_lastRenderedFrame; }

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
    /*! Mark content dirty from an external signal: bump the revision the
     *  canvas polls, then schedule the QQuickItem update. */
    void noteContentChanged() { ++m_contentRev; update(); }

    QPointer<SWMM2DResultsLayer> m_layer;
    quint64                      m_contentRev = 0;
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
    quint64 m_lastRenderedFrame = ~quint64(0);

    /*! Render chunk index over tri/edge bboxes, keyed by geomRevision. */
    OpenSWMM::Render::MeshRenderChunkIndex m_chunks;
    quint64 m_chunksRev = ~quint64(0);

    // Large meshes use the worker even for their first frame and changed
    // ranges/settings. Automatic range growth accepts completed intermediate
    // frames with their own class breaks. Contours cover only the viewport
    // plus its pan margin; jobs from different coverage cannot publish.
    // Bands and lines are one complete frame, with matching water/velocity
    // inputs. Only one worker is in flight; newer ticks coalesce to the next
    // request while the last complete frame remains available for pan/zoom.
    struct ContourJobKey {
        int time = -1;
        quint64 frameRev = 0, epoch = 0;
        double dryDepth = 0, maxDepth = 0;
        bool velocity = false, smoothBands = false;
        std::vector<double> bandLevels, isoLevels;
        QJsonObject contourStyle;
        QRectF coverage;
        // Automatic breaks may grow during a live run. A completed frame is
        // still useful if source, geometry, user settings and coverage agree.
        bool samePresentation(const ContourJobKey& o) const {
            return epoch == o.epoch && dryDepth == o.dryDepth
                && velocity == o.velocity && smoothBands == o.smoothBands
                && contourStyle == o.contourStyle && coverage == o.coverage;
        }
        bool compatible(const ContourJobKey& o) const {
            return samePresentation(o)
                && maxDepth == o.maxDepth
                && bandLevels == o.bandLevels && isoLevels == o.isoLevels;
        }
        bool operator==(const ContourJobKey& o) const {
            return compatible(o) && time == o.time && frameRev == o.frameRev;
        }
    };
    struct ContourFrame;
    std::shared_ptr<const ContourFrame> m_contourFrame;
    QFutureWatcher<std::shared_ptr<const ContourFrame>> m_contourWatcher;
    ContourJobKey m_requestedContourKey;
    quint64 m_contourEpoch = 0;
    bool m_contourBusy = false; // includes a queued finished signal
    std::shared_ptr<const std::vector<OpenSWMM::Render::ContourJobInput::TriPos>>
        m_contourPositions;

    // ── Isoline-label texture cache ────────────────────────────────────
    // Keyed by "text|fontPt|halo|color" so style edits re-rasterise.
    // QSGTexture ownership is ours; released via deleteLater() (textures
    // must die on the render thread).
    mutable QHash<QString, QSGTexture *> m_labelTextureCache;
    void clearLabelTextureCache();

};

#endif // SWMM2DRESULTSQSGRENDERER_H
