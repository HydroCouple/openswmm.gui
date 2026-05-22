// QML wrapper for SWMMLayerQSGRenderer.
//
// Loaded by MapCanvas via QQuickWidget::setSource(qrc:/openswmm/qml/swmmlayer.qml).
// The objectName lets MapCanvas locate the C++ instance via findChild to call
// setLayer / setMapExtent on it.
//
// The 2D mesh layer renders via QGraphicsScene (QPainter) in Layer 2 of
// MapCanvas::paintEvent, so it does NOT need a QSG path here.
//
// Phase B.RHI of docs/RENDERING_5M_PLAN.md.

import QtQuick
import OpenSWMM 1.0

SWMMLayerQSGRenderer {
    objectName: "swmmRenderer"
    anchors.fill: parent
}
