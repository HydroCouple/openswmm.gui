/*!
 * \file   meshhoverprobe.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshhoverprobe.h"

#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"

#include <cmath>

namespace mesh {

MeshHoverProbe::MeshHoverProbe(QObject *parent)
    : QObject(parent)
{
    m_throttle.start();
}

MeshHoverProbe::~MeshHoverProbe() = default;

void MeshHoverProbe::setCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas) return;
    if (m_canvas) {
        disconnect(m_canvas, &MapCanvas::cursorPositionChanged,
                   this, &MeshHoverProbe::onCursor);
    }
    m_canvas = canvas;
    if (m_canvas) {
        connect(m_canvas, &MapCanvas::cursorPositionChanged,
                this, &MeshHoverProbe::onCursor);
    }
}

void MeshHoverProbe::setActiveMesh(SWMM2DMeshLayer *layer)
{
    if (m_mesh == layer) return;
    m_mesh = layer;
    m_lastValid = false;
    m_lastZ = 0.0;
    emit elevationChanged(std::numeric_limits<double>::quiet_NaN(), false);
}

void MeshHoverProbe::onCursor(double mapX, double mapY)
{
    // 16 ms throttle: matches the typical 60 fps cursor cadence and keeps
    // GUI-thread sampling cost bounded (R-V7).
    if (m_throttle.isValid() && m_throttle.elapsed() < 16) return;
    m_throttle.restart();

    if (!m_mesh) {
        if (m_lastValid) {
            m_lastValid = false;
            emit elevationChanged(std::numeric_limits<double>::quiet_NaN(), false);
        }
        return;
    }

    // Scene-space matches rebuildSceneGeometry: x = transformed mapX,
    // y = -transformed mapY (Y-flip; see swmm2dmeshlayer.cpp:278). The
    // mesh layer already has the OGR transform applied to the cached
    // scene vertices; for an interactive hover probe we sample in the
    // same scene space the canvas reports cursor coords in.
    //
    // MapCanvas::cursorPositionChanged emits MAP-space (no Y-flip), so
    // we mirror the Y-flip here. The OGR transform is layer-internal
    // and operates on the cached vertices; the cursor signal is already
    // in the canvas CRS which matches what the mesh scene was built
    // against (the layer reprojects vertices into the canvas CRS during
    // populateScene; pre-populate hover returns NaN by design).
    const double sx =  mapX;
    const double sy = -mapY;

    const double z = m_mesh->sampleZAt(sx, sy);
    const bool finite = std::isfinite(z);
    if (finite == m_lastValid && (!finite || z == m_lastZ)) return;
    m_lastValid = finite;
    m_lastZ     = finite ? z : 0.0;
    emit elevationChanged(z, finite);
}

} // namespace mesh
