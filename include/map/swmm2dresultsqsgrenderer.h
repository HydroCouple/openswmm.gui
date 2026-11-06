/**
 * @file      swmm2dresultsqsgrenderer.h
 * @brief     Qt scene graph rendering of 2D simulation results.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace OpenSWMM::Contour
{
struct IsoBandPolygon;
struct IsoLineSegment;
}

class SWMM2DResultsQSGRenderer : public QQuickItem
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional Qt Quick visual parent; Qt item ownership rules apply.
     * @brief Create an unbound ItemHasContents item and wire contour-worker completion handling.
     * @note Host serializes GUI configuration, scene-graph sync and mutable layer caches. Worker
     *   reads independent immutable geometry/attribute leases, not the live layer. Completion
     *   accepts matching presentation epoch/coverage/settings and a newer or no-longer-current
     *   equal revision, allowing intermediate automatic-range frames; rejected output still frees
     *   writer leases. Completion marks Data, bumps content revision and emits contentReady even
     *   when rejected, scheduling a coalesced latest request.
     */
    explicit SWMM2DResultsQSGRenderer(QQuickItem *parent = nullptr);
    /**
     * @brief Queue cached label texture destruction and release item-owned caches/watcher
     *   connections.
     * @note No explicit worker cancellation/wait here; job captures shared frame/input/output
     *   values, never this. Borrowed layer survives. Qt owns returned scene tree. Deferred texture
     *   deletion requires its owning thread to process events.
     */
    ~SWMM2DResultsQSGRenderer() override;

    /**
     * @param[in] layer Optional borrowed result layer guarded by QPointer; null unbinds.
     * @brief Replace notification bindings, clear old scalar presented-frame publications and reset
     *   derived state.
     * @note Equal pointer no-op. Disconnects old layer/current sublayers; new time changes mark
     *   Data, highlights Selection, edges Style, other repaints ambiguous External. Separate-flow
     *   mode suppresses flow-only repaints unless waiting for matching particles. Policy change
     *   forceRebuild resets presentation epoch. Clears chunk index/geometry/highlight snapshot and
     *   calls forceRebuild.
     * @note Does not own layer, cancel in-flight worker, bump contentRevision for pointer change or
     *   explicitly schedule on layer destruction. Epoch rejects old worker publication; shared
     *   buffers outlive removal while readers retain them.
     */
    void setLayer(SWMM2DResultsLayer *layer);
    /**
     * @param[in] extent Canvas-map bounds before scene Y inversion, copied without validation.
     * @brief Store changed bounds, classify fuzzy size change as zoom and schedule update.
     * @note Equality no-op. Sync decides LOD/coverage rebuild versus transform-only; no immediate
     *   geometry job or contentRevision increment. Host supplies coherent item size,
     *   canvasDevicePixelRatio property and source scene caches.
     */
    void setMapExtent(const MapExtent &extent);

    /*! Drop caches and mark content dirty — used by MapCanvas when the
     *  QSG/CPU ownership toggles so a stale frame is never composited.
     * @note Queues label texture deletion, increments contour epoch, drops current/direct frames,
     *   positions, triangle IDs, scalar/output pool and scratch capacities, clears coverage/LOD key
     *   and marks Layer dirty/update. In-flight jobs retain their own leases and are not
     *   canceled/waited. Does not clear chunk index or last displayed revision/time here, and does
     *   not bump contentRevision; subsequent sync rebuilds before publication.
     */
    void forceRebuild();
    // Settle a contour requested by a capture paint without delivering live
    // model packets or timer events. Normal rendering remains asynchronous.
    /**
     * @brief Wait for the current contour future and deliver posted events addressed to its
     *   watcher.
     * @note Busy=false no-op. Synchronous GUI capture helper may block; does not process arbitrary
     *   live-model/timer events, render new geometry, loop until newest request, or wait for
     *   particle workers. Completion callback updates frame/dirtiness and can emit contentReady.
     *   Host must request/sync the desired capture first and recheck captureReady afterward.
     */
    void finishPendingCapture();
    /**
     * @return True when no contour busy flag and either no retained frame or retained key exactly
     *   equals latest requested key. No frame therefore counts ready; this does not assert a
     *   framebuffer exists, requested revision was displayed, or particles/GPU have finished.
     */
    [[nodiscard]] bool captureReady() const;
    /*! Dynamic water snapshot size; dry mesh geometry is not copied here.
     * @return Retained frame triangle-ID count (display fan triangles, not hydraulic cells), zero
     *   without frame. Does not count all dry geometry, GPU vertices or pool capacity. No mutation.
     */
    [[nodiscard]] size_t retainedWaterTriangleCount() const;
    /*! Copied water attributes needed by deferred contour frames.
     * @return Capacity bytes of displayed scalar-corner array, when present, plus four-float
     *   per-triangle cellValues. Zero without frame; excludes geometry, IDs, velocity, depth
     *   samples, particle inputs, contour packets and allocator overhead. Capacity can exceed
     *   current element count.
     */
    [[nodiscard]] size_t retainedWaterAttributeBytes() const;
    /*! Contour scratch and cached geometry, excluding displayed attributes.
     * @return Capacity bytes of visible/selection scratch, shared contour positions/IDs, scalar
     *   pools except displayed scalar buffer, and sampled completed output-packet capacities. Not
     *   exact process/GPU memory: excludes active unsampled growth and allocator/hash/metadata
     *   overhead; shared storage may be retained elsewhere.
     * @note Reads completed output sizes only to avoid racing worker vector writes; serialized
     *   owner-side diagnostic, no locking of renderer state.
     */
    [[nodiscard]] size_t contourWorkingBytes() const;
    /*! Actual scene-order mutations, for static-cache regression checks.
     * @return Cumulative number of actual root-child insert/reorder operations during
     *   sublayer-order sync, including initial placement when different; unchecked wrap. Excludes
     *   unchanged order, most node creation/removal and all geometry uploads.
     */
    [[nodiscard]] quint64 sceneOrderMutationCount() const { return m_orderMutations; }

    /*! Monotonic counter bumped every time an external change marks this
     *  renderer's content dirty. MapCanvas compares it against the value
     *  recorded when it last grabbed the QSG framebuffer, so a change that
     *  arrives after the canvas cleared its own dirty flag still forces a
     *  regrab instead of compositing a stale frame. Extent and layer-pointer
     *  changes are keyed separately by the canvas and do not bump this.
     * @return Counter incremented by noteContentChanged callbacks and separate-flow change.
     *   Layer/extent changes and forceRebuild itself do not bump it. Unchecked wrap; no GPU
     *   completion or displayed-frame guarantee.
     */
    [[nodiscard]] quint64 contentRevision() const noexcept { return m_contentRev; }

    /*! Revision actually presented by the last scene-graph sync; may trail
     *  the requested frame while its complete contour geometry is prepared.
     * @return Frame revision last committed by fill rebuild, initially all-bits-one sentinel. Can
     *   lag requested frame while contour/particle preparation runs, and is not reset immediately
     *   by forceRebuild or hidden/invalid gates. No framebuffer completion guarantee.
     */
    [[nodiscard]] quint64 displayedFrameRevision() const noexcept { return m_lastRenderedFrame; }
    // Composition can cache the water independently when flow is the top
    // visible sublayer. The separate particle item follows this revision.
    /**
     * @param[in] separate True removes internal particle geometry so host can composite its
     *   separate particle item; false enables internal sync.
     * @brief Change flow-composition mode, bump content revision and schedule update on actual
     *   transition.
     * @note Does not change layer streak settings, contour dirty domains or model ownership.
     *   Internal particle sync still runs each valid callback and clears particle node when
     *   separate=true.
     */
    void setSeparateFlow(bool separate);

signals:
    /*! QSG-2D-1M Phase 7 — emitted when an asynchronous derived-geometry
     *  job (contour marching) publishes a result. MapCanvas connects this
     *  to its framebuffer-regrab path: without it, the freshly synced
     *  bands would sit in the offscreen QSG widget until the next
     *  layer-driven repaint.
     * @note Also emitted after a rejected completed contour and selected flow-streak callbacks
     *   while waiting; indicates redraw/coalesced-request opportunity, not necessarily a newly
     *   displayed frame or all capture work finished. Receivers must arrange framebuffer regrab.
     */
    void contentReady();

protected:
    /**
     * @param[in] oldNode Optional previous ResultsRootNode created here and owned by Qt; matching
     *   exact derived type required.
     * @brief Synchronize water, scalar overlays, static mesh passes, highlights and particle
     *   presentation from current layer/view state.
     * @return Root returned to Qt ownership, or null after clearing scalar presented
     *   publications/deleting old tree for absent/hidden layer, invalid extent, nonpositive item
     *   size, or live source with live rendering disabled. Unnamed UpdatePaintNodeData ignored.
     * @note Render-resource context; host serializes all live layer/cache access. Fresh root
     *   dirties Layer. Geometry changes reset precision anchor/contour epoch/pools; chunk index
     *   keyed by geometry revision culls coverage plus half-viewport margin. Pan/same-LOD views
     *   inside coverage reuse buffers and update transform. Actual sublayer order controls passes;
     *   highlights remain last, unchanged order avoids reattaching nodes.
     * @note Data rebuilds water/scalar/contour/velocity passes while retaining mesh edges/markers;
     *   Selection alone targets highlights, Geometry/Style/LOD broaden work. Deferred contour
     *   frames lease exact water scalars, matching velocity/particle inputs and their own
     *   classification breaks. One contour worker is active; newer requests coalesce, stale
     *   epochs/coverage cannot publish. Large marching jobs use worker; small bootstrap may run
     *   inline. Buffer exhaustion defers without overwriting readers.
     * @note Existing coherent frame may stay visible until contours and matching particles can
     *   present together; sync may return before requested frame is rendered. Wet fills clip
     *   cell-owned signed corner depths at zero; additional groundwater/chemistry scalars use their
     *   own availability and matching reporting index, with no surface-dry clipping. Publishes
     *   scalar presented frames, hydraulic view and particle preparation into layer caches; no
     *   hydraulic engine edit.
     * @note Ordinary geometry splits at 65532 vertices; water uploads deduplicate exact XY/RGBA per
     *   indexed chunk. Depth isolines render solid, whereas additional scalar contours honor dash
     *   style. Labels use GPU texture caches with text/font/halo/colour keys, no DPR key; depth
     *   cache evicts wholesale above 512 entries before a labeling pass. Host applies overall item
     *   opacity, no reprojection occurs here.
     * @note Offscreen/empty bbox clears all passes/publications and coverage without consuming
     *   pending dirtiness, then updates transform/particle sync. Per-pass invalid or unavailable
     *   data can yield empty geometry without an error return. Performance/debug environment gates
     *   optionally log CPU accounting.
     */
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    /*! Mark content dirty from an external signal: bump the revision the
     *  canvas polls, then schedule the QQuickItem update.
     * @note Increment contentRevision and schedule update only; caller supplies appropriate
     *   dirty-domain note. No immediate cache invalidation, worker start or engine write.
     */
    void noteContentChanged()
    {
        ++m_contentRev;
        update();
    }

    QPointer<SWMM2DResultsLayer> m_layer;
    bool m_separateFlow = false;
    quint64 m_contentRev = 0;
    quint64 m_orderMutations = 0;
    MapExtent m_extent;

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

    /*! Snapshots for classifying ambiguous repaintRequested emissions. */
    quint64 m_lastGeomRev = ~quint64(0);
    QSet<int> m_lastHighlight;

    /*! Layer frame index last rebuilt into the QSG tree — the Data-domain
     *  snapshot (also guards against a consumed currentTimeChanged signal
     *  racing the canvas framebuffer grab). */
    int m_lastRenderedTime = -1;
    quint64 m_lastRenderedFrame = ~quint64(0);
    bool m_waitingForParticles = false;

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
        bool velocity = false, smoothBands = false, snapshotTriangles = false;
        std::vector<double> bandLevels, isoLevels;
        QJsonObject contourStyle;
        QRectF coverage;
        // Automatic breaks may grow during a live run. A completed frame is
        // still useful if source, geometry, user settings and coverage agree.
        /**
         * @param[in] o Borrowed other contour-request key.
         * @return Exact equality of epoch, dry cutoff, velocity/smoothing/snapshot flags,
         *   contour-style JSON and coverage; deliberately ignores reporting time/frame revision,
         *   maximum depth and computed class breaks so compatible intermediate automatic-range
         *   frames can publish. Pure comparison, no generation or layer lookup.
         */
        bool samePresentation(const ContourJobKey &o) const
        {
            return epoch == o.epoch && dryDepth == o.dryDepth && velocity == o.velocity &&
                   smoothBands == o.smoothBands && snapshotTriangles == o.snapshotTriangles &&
                   contourStyle == o.contourStyle && coverage == o.coverage;
        }
        /**
         * @param[in] o Borrowed other contour-request key.
         * @return samePresentation plus exact maxDepth, bandLevels and isoLevels equality;
         *   reporting time/frame revision ignored. Pure comparison, no generation or layer lookup.
         */
        bool compatible(const ContourJobKey &o) const
        {
            return samePresentation(o) && maxDepth == o.maxDepth && bandLevels == o.bandLevels &&
                   isoLevels == o.isoLevels;
        }
        /**
         * @param[in] o Borrowed other contour-request key.
         * @return compatible plus exact reporting time and frame revision equality. No tolerance;
         *   NaN numeric fields compare unequal. Pure comparison, no generation or layer lookup.
         */
        bool operator==(const ContourJobKey &o) const
        {
            return compatible(o) && time == o.time && frameRev == o.frameRev;
        }
    };
    struct ContourFrame;
    std::shared_ptr<const ContourFrame> m_contourFrame;
    std::shared_ptr<ContourFrame> m_directFrame; // no worker borrows non-marching snapshots
    QFutureWatcher<std::shared_ptr<const ContourFrame>> m_contourWatcher;
    ContourJobKey m_requestedContourKey;
    quint64 m_contourEpoch = 0;
    bool m_contourBusy = false; // includes a queued finished signal
    std::shared_ptr<std::vector<OpenSWMM::Render::ContourJobInput::TriPos>> m_contourPositions;
    std::shared_ptr<const std::vector<int>> m_contourTriangles;
    std::vector<int> m_visibleCellsScratch, m_contourSelectionScratch;
    // A buffer is writable only while the pool is its sole owner. Completed
    // frames and workers may keep independent immutable readers across seeks.
    std::vector<std::shared_ptr<std::vector<std::array<float, 3>>>> m_contourScalarBuffers;
    struct ContourOutputBuffer {
        std::shared_ptr<OpenSWMM::Render::ContourJobOutput> packet;
        size_t completedBytes = 0; // sampled only after the writer finishes
    };
    std::vector<ContourOutputBuffer> m_contourOutputBuffers;
    /**
     * @param[in] packet Borrowed completed output; may belong to pool or external storage. Must no
     *   longer have a concurrent writer.
     * @param[in] bootstrapSpare True allows one spare packed-band packet to be cloned when pool
     *   currently has exactly one entry.
     * @brief Sample pooled packet capacity bytes and optionally preallocate independent reusable
     *   writer storage.
     * @note Matching packet pointer updates completedBytes. Unknown packet, disabled bootstrap,
     *   pool size other than 1 or no packed bands prevents clone. Clone copies packed bands only,
     *   not isoline segments; allocation may throw. No frame publication, revision change or worker
     *   scheduling.
     */
    void finishContourOutput(const OpenSWMM::Render::ContourJobOutput &packet,
                             bool bootstrapSpare = true);

    // ── Isoline-label texture cache ────────────────────────────────────
    // Keyed by "text|fontPt|halo|color" so style edits re-rasterise.
    // QSGTexture ownership is ours; released via deleteLater() (textures
    // must die on the render thread).
    mutable QHash<QString, QSGTexture *> m_labelTextureCache;
    /**
     * @brief Queue deleteLater for every nonnull cached label texture and immediately clear hash
     *   entries.
     * @note Does not delete referencing label nodes, wait for GPU/deletion, or guarantee
     *   render-thread event processing; host coordinates resource lifecycle. Scalar label textures
     *   are independently node-owned and excluded.
     */
    void clearLabelTextureCache();
};

#endif // SWMM2DRESULTSQSGRENDERER_H
