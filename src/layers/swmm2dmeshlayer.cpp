/*!
 * \file   swmm2dmeshlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "layers/swmm2dmeshlayer.h"

#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QVector>

#include <ogr_spatialref.h>

namespace {

/*! \brief Single batched QGraphicsItem rendering all triangle edges of a
 *         MeshResult in one paint call. Accepts pre-computed scene-space
 *         edges so the layer can supply correctly reprojected coordinates.
 */
class MeshGraphicsItem : public QGraphicsItem
{
public:
    explicit MeshGraphicsItem(QVector<QLineF>  edges,
                              QRectF           bbox,
                              SWMM2DMeshLayer *owner,
                              QGraphicsItem   *parent = nullptr)
        : QGraphicsItem(parent), m_owner(owner),
          m_edges(std::move(edges)), m_bbox(bbox)
    {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, false);
    }

    QRectF boundingRect() const override { return m_bbox; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *,
               QWidget *) override
    {
        if (m_edges.isEmpty()) return;

        QPen pen(m_owner && m_owner->isActiveMesh()
                     ? QColor(0, 200, 220, 220)        // cyan — active
                     : QColor(120, 130, 140, 180));    // muted slate — inactive
        pen.setCosmetic(true);
        pen.setWidthF(m_owner && m_owner->isActiveMesh() ? 1.5 : 1.0);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawLines(m_edges);
    }

private:
    SWMM2DMeshLayer *m_owner = nullptr;
    QVector<QLineF>  m_edges;
    QRectF           m_bbox;
};

} // namespace

// ---------------------------------------------------------------------------
// SWMM2DMeshLayer
// ---------------------------------------------------------------------------

SWMM2DMeshLayer::SWMM2DMeshLayer(mesh::MeshResult     result,
                                  const QString       &sourcePath,
                                  OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(parent),
      m_mesh(std::move(result)),
      m_sourcePath(sourcePath)
{
    setLayerType(OpenSWMMVisLayer::SWMM2DMeshLayer);
    setName(sourcePath.isEmpty() ? QStringLiteral("Mesh") : sourcePath);

    // Layer extent in layer-native CRS. Computed from vertices so
    // fullExtent() can fit the canvas to it (layerExtentInCanvasCRS in
    // MapCanvas reprojects it to canvas CRS when needed).
    if (!m_mesh.vertices.isEmpty())
    {
        const auto &v0 = m_mesh.vertices.first();
        double minX = v0.xy.x(), maxX = v0.xy.x();
        double minY = v0.xy.y(), maxY = v0.xy.y();
        for (const auto &v : m_mesh.vertices)
        {
            if (v.xy.x() < minX) minX = v.xy.x();
            if (v.xy.x() > maxX) maxX = v.xy.x();
            if (v.xy.y() < minY) minY = v.xy.y();
            if (v.xy.y() > maxY) maxY = v.xy.y();
        }
        setExtent(MapExtent(minX, minY, maxX, maxY));
    }
    // Build initial scene edges (identity transform — will be rebuilt by
    // onCanvasCRSChanged once the layer is added to the canvas).
    rebuildSceneEdges();
}

SWMM2DMeshLayer::~SWMM2DMeshLayer()
{
    OGRCoordinateTransformation::DestroyCT(m_transform);
    // m_item is owned by the scene; QGraphicsScene cleans it on
    // depopulateScene + scene destruction.
}

// ---------------------------------------------------------------------------
// Scene-coordinate helpers
// ---------------------------------------------------------------------------

void SWMM2DMeshLayer::rebuildSceneEdges()
{
    // Build deduplicated scene-space edges from the mesh triangles, applying
    // m_transform (layer CRS → canvas CRS) if present. Without the transform
    // the mesh renders at native model coordinates, which differ from the
    // canvas CRS coordinates used by SWMMModelLayer and the basemap — making
    // the mesh invisible at any typical zoom level.
    QSet<QPair<int, int>> seen;
    seen.reserve(m_mesh.triangles.size() * 3);
    m_sceneEdges.clear();
    m_sceneEdges.reserve(m_mesh.triangles.size() * 3);
    m_sceneBBox = QRectF();
    bool first = true;

    auto toScenePt = [&](const QPointF &p) -> QPointF {
        double x = p.x(), y = p.y();
        if (m_transform)
            m_transform->Transform(1, &x, &y);
        return QPointF(x, -y);  // Y-flip: scene Y grows downward
    };

    auto extend = [&](const QPointF &sp) {
        if (first) {
            m_sceneBBox = QRectF(sp, QSizeF(0, 0));
            first = false;
        } else {
            if (sp.x() < m_sceneBBox.left())   m_sceneBBox.setLeft(sp.x());
            if (sp.x() > m_sceneBBox.right())  m_sceneBBox.setRight(sp.x());
            if (sp.y() < m_sceneBBox.top())    m_sceneBBox.setTop(sp.y());
            if (sp.y() > m_sceneBBox.bottom()) m_sceneBBox.setBottom(sp.y());
        }
    };

    auto pushEdge = [&](int a, int b) {
        if (a == b) return;
        const QPair<int, int> key = a < b ? qMakePair(a, b) : qMakePair(b, a);
        if (seen.contains(key)) return;
        seen.insert(key);
        const QPointF sa = toScenePt(m_mesh.vertices[a].xy);
        const QPointF sb = toScenePt(m_mesh.vertices[b].xy);
        m_sceneEdges.append(QLineF(sa, sb));
        extend(sa);
        extend(sb);
    };

    for (const auto &t : m_mesh.triangles) {
        if (t.v0 < 0 || t.v0 >= m_mesh.vertices.size()) continue;
        if (t.v1 < 0 || t.v1 >= m_mesh.vertices.size()) continue;
        if (t.v2 < 0 || t.v2 >= m_mesh.vertices.size()) continue;
        pushEdge(t.v0, t.v1);
        pushEdge(t.v1, t.v2);
        pushEdge(t.v2, t.v0);
    }
}

void SWMM2DMeshLayer::setActiveMesh(bool active)
{
    if (m_active == active) return;
    m_active = active;
    if (m_item) m_item->update();
    emit repaintRequested();
}

void SWMM2DMeshLayer::populateScene(QGraphicsScene *scene,
                                     const MapExtent &,
                                     const SpatialReferenceSystem *)
{
    if (!scene || !isVisible()) return;
    if (m_item)        // already on this (or another) scene — no-op rebuild.
        return;

    // Use the pre-reprojected edges (built by rebuildSceneEdges, which is
    // called in onCanvasCRSChanged). If edges are empty here (canvas CRS
    // hasn't been set yet or the mesh has no triangles) build them now.
    if (m_sceneEdges.isEmpty() && !m_mesh.triangles.isEmpty())
        rebuildSceneEdges();

    auto *item = new MeshGraphicsItem(m_sceneEdges, m_sceneBBox, this);
    item->setZValue(layerZValue());
    item->setOpacity(opacity());
    scene->addItem(item);
    m_item = item;
}

void SWMM2DMeshLayer::depopulateScene(QGraphicsScene *scene)
{
    if (m_item && scene && m_item->scene() == scene)
    {
        scene->removeItem(m_item);
        delete m_item;
    }
    m_item = nullptr;
}

void SWMM2DMeshLayer::refreshScene(QGraphicsScene *scene,
                                    const MapExtent &canvasExtent,
                                    const SpatialReferenceSystem *canvasSRS)
{
    depopulateScene(scene);
    populateScene(scene, canvasExtent, canvasSRS);
}

void SWMM2DMeshLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
    // Rebuild the layer→canvas CRS transform so that mesh vertices stored in
    // the model's native CRS are correctly projected to canvas CRS scene
    // coordinates. Without this the mesh renders at raw model coordinates
    // (e.g. WGS-84 degrees) while the canvas is in EPSG:3857 metres, making
    // the mesh effectively invisible at any typical zoom level.
    OGRCoordinateTransformation::DestroyCT(m_transform);
    m_transform = nullptr;

    if (srs() && newCanvasSRS
        && srs()->ogrSpatialReference()
        && newCanvasSRS->ogrSpatialReference()
        && !srs()->ogrSpatialReference()->IsSame(newCanvasSRS->ogrSpatialReference()))
    {
        m_transform = OGRCreateCoordinateTransformation(
            srs()->ogrSpatialReference(),
            newCanvasSRS->ogrSpatialReference());
    }

    // Reproject all edges into the new canvas CRS.
    rebuildSceneEdges();
}
