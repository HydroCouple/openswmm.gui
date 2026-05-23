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

    // ----- Slice AZ.3.4 — mesh wireframe (edges) toggle -------------------
    // Defaults to true to preserve the slope-coloured wireframe shipped in
    // AU.6 (thin+wide segments in SWMM2DMeshQSGRenderer Pass 2). Turning it
    // off skips Pass 2 entirely — fills, hillshade and nodes remain.
    [[nodiscard]] bool showEdges() const { return m_showEdges; }
    void setShowEdges(bool show);

    // ----- Slice AU.6.4-lite — tunable hillshade ---------------------------
    // Defaults reproduce SWMM2DMeshQSGRenderer's previously-hardcoded values
    // (kLx=kLy=-0.5774, kLz=+0.5774, kVertExag=3.0, kLitMin=0.15). Existing
    // visuals are preserved when the user has not changed anything. Live
    // setters emit repaintRequested() which marks the QSG content dirty.
    //
    // azimuth   — compass bearing the light comes FROM (0=N, 90=E, clockwise)
    // altitude  — sun angle above horizon (0 = horizon, 90 = zenith)
    // zExag     — vertical-exaggeration factor used when computing face normals
    // minLit    — shadow-side brightness floor (0 = full black, 1 = no shadow)
    [[nodiscard]] double hillshadeAzimuth()    const { return m_hillshadeAz; }
    [[nodiscard]] double hillshadeAltitude()   const { return m_hillshadeAlt; }
    [[nodiscard]] double hillshadeZExag()      const { return m_hillshadeZExag; }
    [[nodiscard]] double hillshadeMinLit()     const { return m_hillshadeMinLit; }
    void setHillshadeAzimuth(double degrees);
    void setHillshadeAltitude(double degrees);
    void setHillshadeZExag(double factor);
    void setHillshadeMinLit(double minLit);

    // ----- Slice BJ.2-lite — mesh-bed elevation contour lines --------------
    // Lite cut ships isolines only (no filled isobands, no labels, no
    // per-level colour ramp — those come in BJ.2 full once BB ColorRamp +
    // BI.2 LabelExpression land). N evenly-spaced levels between zMin and
    // zMax; single user-pickable colour + line width applied to all levels.
    [[nodiscard]] bool   showContours()         const { return m_showContours; }
    [[nodiscard]] int    contourIntervalCount() const { return m_contourIntervals; }
    [[nodiscard]] QColor contourColor()         const { return m_contourColor; }
    [[nodiscard]] double contourLineWidth()     const { return m_contourLineWidth; }
    void setShowContours(bool show);
    void setContourIntervalCount(int n);
    void setContourColor(const QColor &c);
    void setContourLineWidth(double widthPx);

    // BJ.2-filled — filled iso-bands (categorical Viridis until BB ColorRamp ships)
    [[nodiscard]] bool   filledContours()        const { return m_contourFilled; }
    [[nodiscard]] double filledContoursOpacity() const { return m_contourFilledOpacity; }
    void setFilledContours(bool filled);
    void setFilledContoursOpacity(double a);

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
    bool                         m_showEdges     = true;   // AZ.3.4

    // AU.6.4-lite — hillshade params (defaults reproduce historic constants)
    double                       m_hillshadeAz     = 225.0;   // compass deg
    double                       m_hillshadeAlt    =  35.264; // deg above horizon (asin(1/√3))
    double                       m_hillshadeZExag  =   3.0;
    double                       m_hillshadeMinLit =   0.15;

    // BJ.2-lite — mesh-bed elevation contour lines
    bool                         m_showContours      = false;
    int                          m_contourIntervals  = 10;
    QColor                       m_contourColor      = QColor(0x1a, 0x1a, 0x1a, 200);
    double                       m_contourLineWidth  = 1.0;   // px
    // BJ.2-filled — filled iso-bands (Viridis until BB ColorRamp ships)
    bool                         m_contourFilled        = false;
    double                       m_contourFilledOpacity = 0.55;
    quint64                      m_geomRevision  = 0;
    OGRCoordinateTransformation *m_transform     = nullptr;
    SWMM2DMeshGraphicsItem      *m_graphicsItem  = nullptr;

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Initialised eagerly in
    // the ctor (default SingleSymbolRenderer) so renderer() never returns
    // null.  Paint refactor deferred until Slice BB ColorRamp ships.
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> m_renderer;
};

#endif // OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H
