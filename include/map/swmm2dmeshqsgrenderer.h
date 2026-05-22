/*!
 * \file   swmm2dmeshqsgrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 *
 * Qt Quick Scene Graph renderer for SWMM2DMeshLayer.  Replaces the
 * per-triangle QPainter path in MeshGraphicsItem::paint() with a single
 * GPU draw call, eliminating O(N) QPainter state changes.
 *
 * Rendering:
 *   Pass 1 — filled triangles: QSGGeometry::ColoredPoint2D +
 *             QSGVertexColorMaterial.  Per-vertex colors encode elevation
 *             colour ramp × hillshade, computed once per geometry/zoom
 *             change and reused on pan.
 *   Pass 2 — edges: two QSGGeometryNode (thin/wide), thick-segment quads
 *             (DrawTriangles) so line widths are consistent across RHI
 *             backends (Metal/Vulkan/D3D11).
 *
 * Optimisation flags:
 *   m_contentDirty — set on layer change, zoom, or mesh geometry change.
 *   Pure pan costs only a QMatrix4x4 write (same Stage-3 approach as
 *   SWMMLayerQSGRenderer).
 */
#ifndef SWMM2DMESHQSGRENDERER_H
#define SWMM2DMESHQSGRENDERER_H

#include "map/mapextent.h"

#include <QPointer>
#include <QQuickItem>

class SWMM2DMeshLayer;

class SWMM2DMeshQSGRenderer : public QQuickItem
{
    Q_OBJECT

public:
    explicit SWMM2DMeshQSGRenderer(QQuickItem *parent = nullptr);
    ~SWMM2DMeshQSGRenderer() override;

    void setLayer(SWMM2DMeshLayer *layer);
    void setMapExtent(const MapExtent &extent);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    MapExtent                 m_extent;

    // Fixed scene-space anchor (bbox centre) — keeps float vertex coords
    // small even in UTM coordinates; stable across pans.
    double m_anchorX = 0.0;
    double m_anchorY = 0.0;

    // Set on geometry, symbology, or zoom change; cleared after full rebuild.
    // Pure pan does NOT set this — only the transform matrix is updated.
    bool m_contentDirty = true;
};

#endif // SWMM2DMESHQSGRENDERER_H
