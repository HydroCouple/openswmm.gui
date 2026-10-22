/**
 * @file      swmmlayerqsgrenderer.h
 * @brief     Qt scene graph rendering of SWMM network layers.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 *
 * Phase B.RHI of docs/RENDERING_5M_PLAN.md — Qt Quick Scene Graph
 * renderer for `SWMMModelLayer`. Hosted in a `QQuickWidget` overlay
 * on `MapCanvas`. Replaces the abandoned OpenGL renderer
 * (`SWMMLayerGLRenderer`) which couldn't fill polygons via Qt's GL
 * paint engine on macOS Apple Silicon.
 *
 * Rendering architecture (see docs/QSG_PAN_ZOOM_OPTIMIZATIONS.md):
 *
 *   Stage 1–3: fixed anchor + m_contentDirty flag — pan costs one matrix write,
 *              geometry only rebuilt on model change or zoom.
 *   Stage 4:   subcatchment tri cache keyed off geomRevision — O(n²) ear-clip
 *              runs once per geometry change, reused on zoom/selection/symbology.
 *   Stage 5:   view-frustum culling — off-screen features skipped entirely.
 *   Stage 6:   LOD — sub-pixel node/gage glyphs suppressed.
 *
 * NOTE: Stage 7 (async build) was reverted. MapCanvas drives rendering via
 * repaint() + grabFramebuffer() synchronously, so async building races with
 * the grab and produces empty frames. All geometry is built synchronously
 * inside updatePaintNode (on the render thread, which is the main thread for
 * QQuickWidget). Stage 1-6 optimisations still eliminate the O(N) cost for
 * pan and reduce rebuild cost significantly.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef SWMMLAYERQSGRENDERER_H
#define SWMMLAYERQSGRENDERER_H

#include "map/mapextent.h"

#include <QPointer>
#include <QQuickItem>
#include <QVector>

#include <limits>

class SWMMModelLayer;

class SWMMLayerQSGRenderer : public QQuickItem
{
    Q_OBJECT

public:
    /**
     * @brief Create ItemHasContents QQuickItem with dirty initial content and initialize shared
     *   performance environment gate.
     * @param[in] parent Optional Qt Quick visual parent/owner under Qt item lifetime rules.
     * @note GUI-thread configuration, scene-graph sync/render callback for node/resource access.
     *   This implementation synchronously reads mutable model caches during updatePaintNode, so
     *   host must serialize model access (QQuickWidget basic loop in current architecture). Does
     *   not own/bind model, create nodes or install in canvas until host acts.
     */
    explicit SWMMLayerQSGRenderer(QQuickItem *parent = nullptr);
    /**
     * @brief Destroy item value caches and QObject connections; bound model stays externally owned.
     *   Qt scene graph owns returned node tree/resource disposal.
     */
    ~SWMMLayerQSGRenderer() override;

    /*! Bind the renderer to a SWMMModelLayer. Disconnects the previous
     *  layer's signals; connects the new one's repaintRequested to
     *  trigger a geometry rebuild. Pass nullptr to clear.
     * @param[in] layer Optional borrowed model guarded by QPointer; null clears binding.
     * @note Equal pointer no-op. Disconnects old model→this connections, binds
     *   selection/repaint/geometry signals, resets triangulation/selection/pending flags and marks
     *   content dirty/update. Selection currently requests FULL geometry rebuild because colour
     *   baked in base vertices, absorbing immediately following repaint; geometry always
     *   invalidates independently.
     * @note Does not alter model qsgRenderKinds, geometry, opacity or ownership. Does not bump
     *   contentRevision for pointer change; host keys layer identity separately. No explicit
     *   destroyed→update connection, so caller schedules redraw when deleting/clearing layer.
     */
    void setLayer(SWMMModelLayer *layer);

    /*! Update the visible map extent. A small same-size pan schedules only a matrix update; drifting beyond
     *  the cached cull margin triggers a full rebuild. A zoom (changed dimensions) sets
     *  m_contentDirty so line widths and glyph sizes are recomputed.
     * @param[in] extent Canvas-map finite valid bounds expected, no scene Y flip; copied without
     *   validation.
     * @note Equality no-op. Width/height fuzzy change marks full content dirty. Same-size pan only
     *   matrix update until center drift exceeds half last-built width/height, then rebuild; no
     *   valid previous build also dirty. Always update on changed extent, no contentRevision bump
     *   or engine reprojection. Host supplies item size/opacity/CRS caches coherently.
     */
    void setMapExtent(const MapExtent &extent);

    /*! Force a full geometry rebuild on the next updatePaintNode, even when
     *  the bound layer pointer is unchanged. Needed when the GPU/CPU render
     *  toggle flips qsgRenderKinds back on: setLayer() no-ops on the same
     *  layer, and a self-render while the overlay was off may have already
     *  consumed m_contentDirty against empty (un-owned) geometry.
     * @note Invalidates triangulation revision, sets content/selection dirty, clears
     *   selection-repaint absorb state, bumps contentRevision and update. No immediate geometry
     *   build, node replacement or engine mutation; caller uses after QSG ownership/resource
     *   resets.
     */
    void forceRebuild();

    /*! Monotonic counter bumped every time an external change marks this
     *  renderer's content dirty. MapCanvas caches the grabbed QSG framebuffer
     *  and must regrab whenever the overlay's content has moved on; comparing
     *  this revision against the one recorded at grab time closes the window
     *  where a renderer learns of a change after the canvas has already
     *  consumed (and cleared) its own dirty flag. Extent and layer-pointer
     *  changes are keyed separately by the canvas and deliberately do NOT
     *  bump this — bumping there would force a regrab on the paint that
     *  pushed them, one frame late, every frame.
     * @return External-change counter, incremented by bound model callbacks/forceRebuild through
     *   noteContentChanged; pointer/extent changes excluded and wrap not handled. No GPU
     *   completion/readiness guarantee.
     */
    [[nodiscard]] quint64 contentRevision() const noexcept { return m_contentRev; }

protected:
    /**
     * @param[in] oldNode Optional owned previous root created by this renderer, exact15child layout
     *   required; deleted on invalid render state.
     * @brief Build/reuse scene graph synchronously from coherent model scene caches and current
     *   view, returning anchored transform root.
     * @return Owned-to-Qt root, or null after deleting old tree for missing/deleted/hidden layer,
     *   invalid extent or nonpositive item size. Unnamed UpdatePaintNodeData ignored. Node
     *   allocation/resource operations on scene-graph render context; no cancellation/background
     *   builder.
     * @note Full dirty pass recomputes precision anchor from node-scene bbox, culls at
     *   viewport+half-width/height margin and emits QSG-owned shown kinds only, clearing others.
     *   Cached catch ear-clipping keyed by geomRevision and explicit geometry signal invalidation.
     *   Selection currently dirties full pass; dormant selection-only branch updates overlays if
     *   reached without full flag. New root alone does not independently set contentDirty; host
     *   forces rebuild after resource resets when needed.
     * @note 15top-level geometry children: catch fill/selection/outline, outlet
     *   connectors/selection, link base/selection, four node buckets, gage base,
     *   unused-or-selection node overlay and gage overlay. Per-vertex coloured streams split
     *   at60000vertices into child nodes; flat streams remain single allocation. Base colours
     *   premultiplied, category alpha/hidden flags applied. Layer opacity belongs to host
     *   QQuickItem, not read/applied here.
     * @note Links tessellate continuous stroke/round caps plus original-polyline midpoint arrows;
     *   selection width uses conduit size+1pixel, not each kind width. Current GPU path does not
     *   apply CPU perpendicular line offsets/dash style. Node/gage radii below1pixel suppressed.
     *   Virtual/inlet symbol handling and node feature size/shape/colour applied; gage shape
     *   remains kind default with size/colour override. Labels/inlet capture connectors/LID badges
     *   remain CPU item responsibilities.
     * @note Always update root matrix for current extent/item dimensions using anchor-relative
     *   floats. Cached margins permit pan-only matrix update; canvas CRS/source geometry must
     *   already agree, no OGR transformation here. Mutates item/node/caches and optional shared
     *   timing logs only, no model engine writes. No geometry-generation validation against
     *   external concurrent edits.
     */
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    /*! After caller marks dirty flags, bump the revision the
     *  canvas polls, then schedule the QQuickItem update.
     * @note Increment contentRevision and schedule update ONLY; caller sets content/selection dirty
     *   flags beforehand. Does not itself invalidate triangulation or mark contentDirty despite
     *   original helper summary. Serialized with host framebuffer cache queries.
     */
    void noteContentChanged()
    {
        ++m_contentRev;
        update();
    }

    QPointer<SWMMModelLayer> m_layer;
    quint64 m_contentRev = 0;
    MapExtent m_extent;
    // Extent at the last full content rebuild. Used by setMapExtent()
    // to decide pan-only (transform refresh) vs. full rebuild based on
    // how far the viewport has drifted from the cached cull window.
    MapExtent m_lastBuiltExtent;

    // Fixed scene-space centre of the dataset bounding box, recomputed at
    // the start of each content rebuild.  Vertex coords stored as
    // float(sceneCoord - anchor) keep float magnitudes small (UTM precision)
    // and are stable across pans — only the matrix translate changes on pan.
    double m_anchorX = 0.0;
    double m_anchorY = 0.0;

    // m_contentDirty — set on geometry / symbology / visibility / zoom changes.
    // Triggers a full rebuild of all 15 top-level geometry buffers in updatePaintNode.
    // Pure pan does NOT set this flag.
    bool m_contentDirty = true;

    // m_selDirty — set when only the selection changes (setSelectedElementNames).
    // Triggers a cheap rebuild of the 5 selection-overlay buffers only; the 8
    // base-geometry buffers are left untouched.
    // m_selectionPending absorbs the repaintRequested that always follows
    // selectionChanged so it does not additionally set m_contentDirty.
    bool m_selDirty = false;
    bool m_selectionPending = false;

    // Subcatchment triangulation cache (Stage 4).
    // Ear-clipping O(n²) result cached per geomRevision; reused on every
    // rebuild that does not change subcatchment geometry.
    struct CatchTriCache {
        quint64 revision = std::numeric_limits<quint64>::max();
        QVector<QVector<int>> tris;
    };
    CatchTriCache m_catchTriCache;
};

#endif // SWMMLAYERQSGRENDERER_H
