/*!
 * \file   swmmlayerqsgrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
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
    explicit SWMMLayerQSGRenderer(QQuickItem *parent = nullptr);
    ~SWMMLayerQSGRenderer() override;

    /*! Bind the renderer to a SWMMModelLayer. Disconnects the previous
     *  layer's signals; connects the new one's repaintRequested to
     *  trigger a geometry rebuild. Pass nullptr to clear. */
    void setLayer(SWMMModelLayer *layer);

    /*! Update the visible map extent. A pure pan (same extent dimensions)
     *  only schedules a matrix update. A zoom (changed dimensions) sets
     *  m_contentDirty so line widths and glyph sizes are recomputed. */
    void setMapExtent(const MapExtent &extent);

    /*! Force a full geometry rebuild on the next updatePaintNode, even when
     *  the bound layer pointer is unchanged. Needed when the GPU/CPU render
     *  toggle flips qsgRenderKinds back on: setLayer() no-ops on the same
     *  layer, and a self-render while the overlay was off may have already
     *  consumed m_contentDirty against empty (un-owned) geometry. */
    void forceRebuild();

    /*! Monotonic counter bumped every time an external change marks this
     *  renderer's content dirty. MapCanvas caches the grabbed QSG framebuffer
     *  and must regrab whenever the overlay's content has moved on; comparing
     *  this revision against the one recorded at grab time closes the window
     *  where a renderer learns of a change after the canvas has already
     *  consumed (and cleared) its own dirty flag. Extent and layer-pointer
     *  changes are keyed separately by the canvas and deliberately do NOT
     *  bump this — bumping there would force a regrab on the paint that
     *  pushed them, one frame late, every frame. */
    [[nodiscard]] quint64 contentRevision() const noexcept
    { return m_contentRev; }

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *) override;

private:
    /*! Mark content dirty from an external signal: bump the revision the
     *  canvas polls, then schedule the QQuickItem update. */
    void noteContentChanged() { ++m_contentRev; update(); }

    QPointer<SWMMModelLayer> m_layer;
    quint64                  m_contentRev = 0;
    MapExtent                m_extent;
    // Extent at the last full content rebuild. Used by setMapExtent()
    // to decide pan-only (transform refresh) vs. full rebuild based on
    // how far the viewport has drifted from the cached cull window.
    MapExtent                m_lastBuiltExtent;

    // Fixed scene-space centre of the dataset bounding box, recomputed at
    // the start of each content rebuild.  Vertex coords stored as
    // float(sceneCoord - anchor) keep float magnitudes small (UTM precision)
    // and are stable across pans — only the matrix translate changes on pan.
    double m_anchorX = 0.0;
    double m_anchorY = 0.0;

    // m_contentDirty — set on geometry / symbology / visibility / zoom changes.
    // Triggers a full rebuild of all 13 geometry buffers in updatePaintNode.
    // Pure pan does NOT set this flag.
    bool m_contentDirty = true;

    // m_selDirty — set when only the selection changes (setSelectedElementNames).
    // Triggers a cheap rebuild of the 5 selection-overlay buffers only; the 8
    // base-geometry buffers are left untouched.
    // m_selectionPending absorbs the repaintRequested that always follows
    // selectionChanged so it does not additionally set m_contentDirty.
    bool m_selDirty        = false;
    bool m_selectionPending = false;

    // Subcatchment triangulation cache (Stage 4).
    // Ear-clipping O(n²) result cached per geomRevision; reused on every
    // rebuild that does not change subcatchment geometry.
    struct CatchTriCache {
        quint64               revision = std::numeric_limits<quint64>::max();
        QVector<QVector<int>> tris;
    };
    CatchTriCache m_catchTriCache;
};

#endif // SWMMLAYERQSGRENDERER_H
