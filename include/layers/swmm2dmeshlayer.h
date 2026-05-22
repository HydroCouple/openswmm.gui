/*!
 * \file   swmm2dmeshlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU.6 — renders a generated 2D triangular mesh on the canvas.
 *
 * Rendering is handled entirely by SWMM2DMeshQSGRenderer (a QQuickItem
 * inside MapCanvas's QQuickWidget), which replaces the former per-triangle
 * QPainter path.  All scene-space geometry is pre-computed into the public
 * m_scene* caches by rebuildSceneGeometry(); the renderer reads them directly.
 *
 * Elevation visualisation:
 *  - Filled triangles: terrain colour ramp (deep-blue → near-white) ×
 *    slope-based hillshade.
 *  - Edges: colour-mapped by slope; steep breaks rendered wider.
 *  - Nodes: SWMM-coupled (tagged) vertices; off by default (setShowMeshNodes).
 */
#ifndef OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H
#define OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H

#include "layers/openswmmvislayer.h"
#include "mesh/meshresult.h"

#include <QColor>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <ogr_spatialref.h>

#include <memory>

class QGraphicsScene;
class QGraphicsItem;
class SWMM2DMeshGraphicsItem;

namespace OpenSWMM::Render { class IFeatureRenderer; }

class SWMM2DMeshLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

public:
    explicit SWMM2DMeshLayer(mesh::MeshResult result,
                              const QString &sourcePath = {},
                              OpenSWMMVisWorkspace *parent = nullptr);
    ~SWMM2DMeshLayer() override;

    [[nodiscard]] QString sourcePath() const { return m_sourcePath; }
    void setSourcePath(const QString &path)  { m_sourcePath = path; }

    [[nodiscard]] bool isActiveMesh() const { return m_active; }
    void setActiveMesh(bool active);

    [[nodiscard]] bool showMeshNodes() const { return m_showMeshNodes; }
    void setShowMeshNodes(bool show);

    [[nodiscard]] const mesh::MeshResult &mesh() const { return m_mesh; }

    [[nodiscard]] quint64 geomRevision() const { return m_geomRevision; }

    // ----- Renderer (Slice BI Phase 8.13.6.6) -----------------------------
    // API plumbing only — paint loop still uses the per-vertex hillshade
    // ramp directly.  Sub-phase 8.13.6.4 (deferred until Slice BB
    // ColorRamp lands) will refactor paint to consult m_renderer.

    /*!
     * \brief The IFeatureRenderer that will drive this layer's paint pass.
     * \details Constructed eagerly as a default SingleSymbolRenderer so
     *          callers never have to null-check.  Owned by the layer.
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  Null pointers are silently
     *          rejected.  Emits \ref rendererChanged() when the pointer
     *          actually changes.
     */
    void setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    // ----- OpenSWMMVisLayer interface ----------------------------------------

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    // ----- Scene-geometry structs (public for SWMM2DMeshQSGRenderer) ---------

    /*! Per-triangle: scene-space vertices + per-vertex z (for hillshade). */
    struct SceneTri
    {
        QPointF a, b, c;
        float   zAvg;       ///< Average vertex z — elevation colour.
        float   z0, z1, z2; ///< Per-vertex z — hillshade face normal.
    };

    /*! Per-edge: scene-space line + elevation + slope.
     *  slope = |Δz| / horizontal_distance_in_map_units. */
    struct SceneEdge
    {
        QLineF  line;
        float   zAvg;
        float   slope;
    };

    /*! Per-vertex node dot. */
    struct SceneNode
    {
        QPointF pt;
        float   z;
        bool    tagged; ///< true = SWMM-coupled vertex.
    };

    // Scene-geometry caches — written by rebuildSceneGeometry(),
    // read by SWMM2DMeshQSGRenderer::updatePaintNode().
    QRectF             m_sceneBBox;
    QVector<SceneTri>  m_sceneTris;
    QVector<SceneEdge> m_sceneEdges;
    QVector<SceneNode> m_sceneNodes;
    double             m_zMin     = 0.0;
    double             m_zMax     = 0.0;
    float              m_maxSlope = 0.0f;

signals:
    /*! \brief Emitted when setRenderer() swaps the renderer pointer. */
    void rendererChanged();

private:
    void rebuildSceneGeometry();

    mesh::MeshResult             m_mesh;
    QString                      m_sourcePath;
    bool                         m_active        = false;
    bool                         m_showMeshNodes = false;
    quint64                      m_geomRevision  = 0;
    OGRCoordinateTransformation *m_transform     = nullptr;
    SWMM2DMeshGraphicsItem      *m_graphicsItem  = nullptr;

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Initialised eagerly in
    // the ctor (default SingleSymbolRenderer) so renderer() never returns
    // null.  Paint refactor deferred until Slice BB ColorRamp ships.
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> m_renderer;
};

#endif // OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H
