/*!
 * \file   swmm2dmeshlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Slice AU.6 — renders a generated 2D triangular mesh on the canvas.
 * One layer instance per `.2dm` (or per fresh-from-MeshGenerator
 * MeshResult). Multiple mesh layers can coexist in the canvas's layer
 * list; the user picks one as "active" via the Mesh tab in the
 * Simulation Options dialog (which patches `[2D_MESH_FILE]` in the
 * .inp). Inactive layers stay visible as previews — useful for
 * comparing candidate meshes side by side.
 */
#ifndef OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H
#define OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H

#include "layers/openswmmvislayer.h"
#include "mesh/meshresult.h"

#include <QColor>
#include <QLineF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <ogr_spatialref.h>

class QGraphicsScene;
class QGraphicsItem;

class SWMM2DMeshLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

public:
    /*! \brief Construct from a MeshResult held in memory.
     *  \param result    Mesh data — vertices / triangles / boundary edges.
     *  \param sourcePath Optional path to the underlying `.2dm` (used by the
     *                    Mesh tab to match the layer to a sibling file).
     */
    explicit SWMM2DMeshLayer(mesh::MeshResult result,
                              const QString &sourcePath = {},
                              OpenSWMMVisWorkspace *parent = nullptr);
    ~SWMM2DMeshLayer() override;

    /*! \brief Path to the `.2dm` this layer was generated to (or empty
     *         if the mesh was never written). */
    [[nodiscard]] QString sourcePath() const { return m_sourcePath; }
    void setSourcePath(const QString &path) { m_sourcePath = path; }

    /*! \brief Whether the canvas currently treats this layer as the
     *         "active" mesh (i.e. the one referenced via `[2D_MESH_FILE]`).
     *         Drives a brighter wireframe color so the user can tell at
     *         a glance which mesh the engine reads. */
    [[nodiscard]] bool isActiveMesh() const { return m_active; }
    void setActiveMesh(bool active);

    [[nodiscard]] const mesh::MeshResult &mesh() const { return m_mesh; }

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

private:
    /*! Reproject m_mesh vertex coordinates through m_transform (if any)
     *  and rebuild m_sceneEdges / m_sceneBBox. Called whenever the canvas
     *  CRS changes (onCanvasCRSChanged) and at construction. */
    void rebuildSceneEdges();

    mesh::MeshResult                m_mesh;
    QString                         m_sourcePath;
    bool                            m_active    = false;
    QGraphicsItem                  *m_item      = nullptr;
    OGRCoordinateTransformation    *m_transform = nullptr;
    QVector<QLineF>                 m_sceneEdges;
    QRectF                          m_sceneBBox;
};

#endif // OPENSWMMVIS_LAYERS_SWMM2DMESHLAYER_H
