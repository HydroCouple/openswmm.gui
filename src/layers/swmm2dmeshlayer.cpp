/*!
 * \file   swmm2dmeshlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "layers/swmm2dmeshlayer.h"

#include "map/mapextent.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QVector>

namespace {

// Convert a layer-CRS map coordinate to scene coords. Standard convention
// across the codebase: scene Y is the negation of map Y (so map +Y is
// "up" while scene Y goes down).
inline QPointF toScene(double mx, double my) { return QPointF(mx, -my); }

/*! \brief Single batched QGraphicsItem rendering all triangle edges of a
 *         MeshResult in one paint call. The item caches a flat
 *         QVector<QLineF> of edges so paint() is O(N) draw with one
 *         drawLines call — same hot-path pattern as SWMMLayerItem.
 */
class MeshGraphicsItem : public QGraphicsItem
{
public:
    explicit MeshGraphicsItem(const mesh::MeshResult &mesh,
                              SWMM2DMeshLayer       *owner,
                              QGraphicsItem         *parent = nullptr)
        : QGraphicsItem(parent), m_owner(owner)
    {
        // Pre-compute scene-space edges. We dedupe (a,b) vs (b,a) by
        // canonicalising vertex-pair ordering — saves ~half the draws on
        // an arbitrary mesh and keeps the line crisp (no double-stroke
        // brightness mismatch).
        QSet<QPair<int, int>> seen;
        seen.reserve(mesh.triangles.size() * 3);
        m_edges.reserve(mesh.triangles.size() * 3);
        auto pushEdge = [&](int a, int b) {
            if (a == b) return;
            const QPair<int, int> key = a < b ? qMakePair(a, b) : qMakePair(b, a);
            if (seen.contains(key)) return;
            seen.insert(key);
            const auto &va = mesh.vertices[a];
            const auto &vb = mesh.vertices[b];
            m_edges.append(QLineF(toScene(va.xy.x(), va.xy.y()),
                                   toScene(vb.xy.x(), vb.xy.y())));
        };
        for (const auto &t : mesh.triangles)
        {
            if (t.v0 < 0 || t.v0 >= mesh.vertices.size()) continue;
            if (t.v1 < 0 || t.v1 >= mesh.vertices.size()) continue;
            if (t.v2 < 0 || t.v2 >= mesh.vertices.size()) continue;
            pushEdge(t.v0, t.v1);
            pushEdge(t.v1, t.v2);
            pushEdge(t.v2, t.v0);
        }

        // Per-vertex coupling markers were prototyped here (small orange
        // dots on every tagged vertex) but the user wants vertex
        // rendering deferred to the styling/theming layer (Slice AC).
        // The data is still preserved on each MeshVertex; the renderer
        // just doesn't draw them yet. Re-enable here once the theming
        // editor lands.

        // Bounding rect — union of the mesh extent in scene coords.
        if (!mesh.vertices.isEmpty())
        {
            const auto &v0 = mesh.vertices.first();
            QPointF s0 = toScene(v0.xy.x(), v0.xy.y());
            qreal minX = s0.x(), maxX = s0.x(), minY = s0.y(), maxY = s0.y();
            for (const auto &v : mesh.vertices)
            {
                const QPointF s = toScene(v.xy.x(), v.xy.y());
                if (s.x() < minX) minX = s.x();
                if (s.x() > maxX) maxX = s.x();
                if (s.y() < minY) minY = s.y();
                if (s.y() > maxY) maxY = s.y();
            }
            m_bbox = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
        }

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
        // Vertex / coupling-marker rendering intentionally omitted —
        // styling lives in Slice AC.
    }

private:
    SWMM2DMeshLayer    *m_owner = nullptr;
    QVector<QLineF>     m_edges;
    QRectF              m_bbox;
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

    // Layer extent in layer-CRS (== map units). Computed from vertices
    // so fullExtent() can fit the canvas to it.
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
}

SWMM2DMeshLayer::~SWMM2DMeshLayer()
{
    // m_item is owned by the scene; QGraphicsScene cleans it on
    // depopulateScene + scene destruction.
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

    auto *item = new MeshGraphicsItem(m_mesh, this);
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

void SWMM2DMeshLayer::onCanvasCRSChanged(const SpatialReferenceSystem *)
{
    // First-cut limitation: the mesh is rendered in its native CRS (the
    // CRS the mesh was generated in, which matches the SWMM model's CRS
    // at generate time). When the canvas CRS differs the mesh would
    // need on-the-fly reprojection per vertex. Deferred to a Slice AU
    // follow-up — not on the demo critical path because the model + mesh
    // share the same CRS during a typical session.
}
