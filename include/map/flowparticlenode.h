// GPL-3.0-or-later

/**
 * @file      flowparticlenode.h
 * @brief     Scene graph node for animated flow particles.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "render/flowparticles.h"
#include <QSGGeometryNode>
#include <QColor>
#include <functional>

using FlowParticleAppearance = OpenSWMM::Render::FlowParticleAppearance;
class QQuickWindow;

// Joined history ribbons. Completed simulation frames update geometry;
// presentation interpolation changes only uniforms between those frames.
class FlowParticleNode : public QSGGeometryNode
{
public:
    /**
     * @brief Allocate owned dynamic triangle-strip geometry and blended particle material with
     *   initially empty buffers.
     * @param[in] window Optional borrowed QQuickWindow used for timing-texture creation; must
     *   outlive node/render-resource use.
     * @note Construct/sync/destroy on Qt Quick scene-graph render thread with valid resource
     *   lifetime. Node owns geometry/material and material owns timing texture. Null window still
     *   builds vertices but cannot create new timing texture; no worker/model pointer retained.
     */
    explicit FlowParticleNode(QQuickWindow *window = nullptr);
    /**
     * @brief Present immutable particle frame, updating shader uniforms each call and
     *   rebuilding/uploading ribbon geometry only on relevant changes.
     * @param[in] frame Shared immutable completed publication, retained until next frame; null
     *   clears geometry after prior state exists.
     * @param[in] interpolation Expected finite0..1blend between previous/current particle
     *   clock/ages; unchecked and extrapolates outside.
     * @param[in] scale Positive finite pixels per scene unit for tessellation; caller keeps derived
     *   counts representable.
     * @param[in] trailSeconds Visible trail duration in flow-clock seconds, default1; passed raw to
     *   shader.
     * @param[in] width Trail width in pixels, default1.5; raw shader uniform.
     * @param[in] headWidth Head width in pixels, default2; raw uniform.
     * @param[in] fadeSeconds Birth/death/retirement fade duration in flow seconds, default0.5; raw
     *   shader uniform.
     * @param[in] trackedId Birth ID to highlight, zero no tracked particle; geometry dependency.
     * @param[in] appearance Immutable anchor/viewport scene coordinates, colour/palette revision,
     *   opacity/fade/taper and allocation hints. Palette callback must own stable data; bump
     *   revision on callback/style change.
     * @note Same frame/scale/trackedID/anchor/colour/revision/viewport skips geometry but
     *   uniforms/DirtyMaterial still update. Small same-frame scale change<=10% of old positive
     *   scale can defer geometry one call (including view changes), then rebuild. Fully empty
     *   null→null call skips all uniforms.
     * @note Reuses prepared frame ribbon when its scale/revision/colour/viewport/trackedID match,
     *   correcting anchor offset; otherwise builds private ribbon synchronously using prior frame
     *   and retained population/history high-water hints. Frame data never mutated. Exceptions
     *   propagate without transactional rollback.
     * @note Copies packed vertices into owned nonindexed strip, recreates8×particle-row
     *   nearest-filtered RGBA8 timing texture only with window/nonempty timing, marks geometry
     *   dirty/increments uploads and records build ms. Null/empty ribbon does not explicitly free
     *   previous timing texture; destructor/replacement does. Caller marks material/geometry use on
     *   owning render thread; no layer edits/async dispatch.
     */
    void sync(std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame> frame,
              double interpolation, double scale, double trailSeconds = 1, double width = 1.5,
              double headWidth = 2, double fadeSeconds = .5, quint64 trackedId = 0,
              const FlowParticleAppearance &appearance = {});
    /**
     * @return Cumulative geometry-sync/rebuild count, including empty clears, excluding
     *   uniform-only/one-call-deferred paths. Not measured GPU command completion or byte count.
     */
    int uploads() const { return m_uploads; }
    /**
     * @return Always0: packed triangle strips have no index buffer; no state mutation.
     */
    int indexUploads() const { return 0; } // Packed strips have no index buffer.
    /**
     * @return Latest nondeferred rebuild wall milliseconds including CPU ribbon/vertex copy and
     *   timing texture creation,0initial; unchanged uniform-only/no-op. Not GPU frame/render
     *   duration.
     */
    double geometryBuildMs() const { return m_geometryBuildMs; }

private:
    QQuickWindow *m_window = nullptr;
    std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame> m_frame;
    OpenSWMM::Render::FlowParticleRibbon m_ribbon;
    double m_scale = 0;
    quint64 m_trackedId = 0;
    QPointF m_anchor;
    QRectF m_viewport;
    QColor m_color;
    quint64 m_appearanceRevision = 0;
    int m_uploads = 0;
    bool m_deferredView = false;
    double m_geometryBuildMs = 0;
};
