// QML wrapper for the QSG overlay renderers.
//
// Loaded by MapCanvas via QQuickWidget::setSource(qrc:/openswmm/qml/swmmlayer.qml).
// The objectNames let MapCanvas locate the C++ instances via findChild to
// call setLayer / setMapExtent on them.
//
// Sibling order = z-order: the 2D TERRAIN mesh (elevation fill, wireframe,
// contours) renders at the BOTTOM, the 2D results flood map (depth fill,
// contour bands, isolines, velocity arrows) above it, and the 1D network
// glyphs on top so the drainage network stays readable over the inundation
// map (VS.8).
//
// Mesh Tiled LOD plan P1.1 — the mesh renderer is live here; MapCanvas
// hands it the topmost visible SWMM2DMeshLayer (preference
// Rendering/QsgMeshEnabled, env kill-switch OPENSWMM_QSG_MESH=0) and the
// QPainter SWMM2DMeshGraphicsItem path stays compiled as the fallback.
//
// Phase B.RHI of docs/RENDERING_5M_PLAN.md + VS.8 GPU results port.

import QtQuick
import OpenSWMM 1.0

Item {
    SWMM2DMeshQSGRenderer {
        objectName: "mesh2dRenderer"
        anchors.fill: parent
    }
    SWMM2DResultsQSGRenderer {
        objectName: "results2dRenderer"
        anchors.fill: parent
    }
    SWMMLayerQSGRenderer {
        objectName: "swmmRenderer"
        anchors.fill: parent
    }
}
