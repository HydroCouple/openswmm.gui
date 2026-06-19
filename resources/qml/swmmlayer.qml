// QML wrapper for the QSG overlay renderers.
//
// Loaded by MapCanvas via QQuickWidget::setSource(qrc:/openswmm/qml/swmmlayer.qml).
// The objectNames let MapCanvas locate the C++ instances via findChild to
// call setLayer / setMapExtent on them.
//
// Sibling order = z-order: the 2D results flood map (depth fill, contour
// bands, isolines, velocity arrows) renders BELOW the 1D network glyphs so
// the drainage network stays readable over the inundation map (VS.8).
//
// The 2D TERRAIN mesh layer still renders via QGraphicsScene (QPainter) in
// Layer 2 of MapCanvas::paintEvent — below this whole overlay.
//
// Phase B.RHI of docs/RENDERING_5M_PLAN.md + VS.8 GPU results port.

import QtQuick
import OpenSWMM 1.0

Item {
    SWMM2DResultsQSGRenderer {
        objectName: "results2dRenderer"
        anchors.fill: parent
    }
    SWMMLayerQSGRenderer {
        objectName: "swmmRenderer"
        anchors.fill: parent
    }
}
