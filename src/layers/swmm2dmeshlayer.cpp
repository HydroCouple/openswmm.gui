/*!
 * \file   swmm2dmeshlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Rendering uses SWMM2DMeshGraphicsItem — a QGraphicsItem that paints
 * the triangulated mesh via QPainter, exactly like SWMMLayerItem does for
 * the SWMM network.  rebuildSceneGeometry() populates the scene-space caches
 * (m_sceneTris / m_sceneEdges / m_sceneNodes); the graphics item reads them
 * and calls update() on itself whenever repaintRequested() fires.
 */
#include "layers/swmm2dmeshlayer.h"

#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QPainter>
#include <QSet>
#include <QStyleOptionGraphicsItem>

#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
#include <limits>

// ---------------------------------------------------------------------------
// Elevation colour ramp — same 5-stop ramp as SWMM2DMeshQSGRenderer
// ---------------------------------------------------------------------------
namespace {

void elevationColorRgb(double t, int &r, int &g, int &b)
{
    struct Stop { double t; int r, g, b; };
    static const Stop stops[] = {
        {0.00, 0x1a, 0x3d, 0x6b},
        {0.20, 0x2e, 0x8b, 0x57},
        {0.50, 0xc8, 0xd9, 0x4e},
        {0.75, 0xc8, 0xa0, 0x00},
        {1.00, 0xf0, 0xf0, 0xe8},
    };
    constexpr int N = int(sizeof(stops)/sizeof(*stops));
    t = qBound(0.0, t, 1.0);
    int i = 0;
    while (i < N - 2 && stops[i+1].t <= t) ++i;
    const Stop &lo = stops[i], &hi = stops[i+1];
    const double f = (hi.t > lo.t) ? (t - lo.t)/(hi.t - lo.t) : 0.0;
    r = qRound(lo.r + f*(hi.r - lo.r));
    g = qRound(lo.g + f*(hi.g - lo.g));
    b = qRound(lo.b + f*(hi.b - lo.b));
}

} // namespace

// ---------------------------------------------------------------------------
// SWMM2DMeshGraphicsItem
// ---------------------------------------------------------------------------

class SWMM2DMeshGraphicsItem : public QGraphicsItem
{
public:
    explicit SWMM2DMeshGraphicsItem(SWMM2DMeshLayer *layer, QGraphicsItem *parent = nullptr)
        : QGraphicsItem(parent), m_layer(layer)
    {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
        setCacheMode(QGraphicsItem::NoCache);
        setZValue(layer->layerZValue());
    }

    void geometryChanged() { prepareGeometryChange(); update(); }

    QRectF boundingRect() const override
    {
        return m_layer->m_sceneBBox;
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        if (!m_layer->isVisible()) return;

        const auto &tris  = m_layer->m_sceneTris;
        const auto &edges = m_layer->m_sceneEdges;

        if (tris.isEmpty()) return;

        const QRectF exposed = option->exposedRect;
        const bool hasElev   = (m_layer->m_zMax > m_layer->m_zMin);
        const double zMin    = m_layer->m_zMin;
        const double zRange  = m_layer->m_zMax - zMin;
        const bool active    = m_layer->isActiveMesh();
        const int  fillAlpha = active ? 160 : 110;

        // Hillshade light direction (NW at ~35° elevation — matches QSG renderer)
        constexpr float kLx = -0.5774f, kLy = -0.5774f, kLz = 0.5774f;
        constexpr float kVertExag = 3.0f;
        constexpr float kLitMin   = 0.15f;

        p->save();
        p->setPen(Qt::NoPen);

        // ---- Pass 1: filled triangles ----------------------------------------
        for (const auto &t : tris) {
            // Bounding-box cull against the exposed rect
            const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
            const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
            const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
            const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
            if (!exposed.isNull() &&
                (maxX < exposed.left()  || minX > exposed.right() ||
                 maxY < exposed.top()   || minY > exposed.bottom())) continue;

            int cr, cg, cb;
            if (hasElev) {
                elevationColorRgb((t.zAvg - zMin) / zRange, cr, cg, cb);

                // Hillshade: face normal from edge vectors
                const float ax = float(t.b.x()-t.a.x()), ay = float(t.b.y()-t.a.y());
                const float bx = float(t.c.x()-t.a.x()), by = float(t.c.y()-t.a.y());
                const float az = (t.z1 - t.z0) * kVertExag;
                const float bz = (t.z2 - t.z0) * kVertExag;
                float nx = ay*bz - az*by;
                float ny = az*bx - ax*bz;
                float nz = ax*by - ay*bx;
                if (nz < 0.f) { nx=-nx; ny=-ny; nz=-nz; }
                const float nlen = std::sqrt(nx*nx+ny*ny+nz*nz);
                if (nlen > 1e-12f) { nx/=nlen; ny/=nlen; nz/=nlen; }
                const float lit = qBound(kLitMin, nx*kLx + ny*kLy + nz*kLz, 1.0f);

                cr = qBound(0, int(float(cr)*lit), 255);
                cg = qBound(0, int(float(cg)*lit), 255);
                cb = qBound(0, int(float(cb)*lit), 255);
            } else {
                cr = 70; cg = 130; cb = 180; // flat steel blue
            }

            p->setBrush(QColor(cr, cg, cb, fillAlpha));
            const QPointF pts[3] = {t.a, t.b, t.c};
            p->drawConvexPolygon(pts, 3);
        }

        // ---- Pass 2: edges ---------------------------------------------------
        const float maxSlope = m_layer->m_maxSlope;
        const float invSlope = (maxSlope > 0.f) ? 1.0f / maxSlope : 0.0f;
        constexpr float kSlopeBreak = 0.35f;

        for (const auto &e : edges) {
            const double ex0 = qMin(e.line.x1(), e.line.x2());
            const double ex1 = qMax(e.line.x1(), e.line.x2());
            const double ey0 = qMin(e.line.y1(), e.line.y2());
            const double ey1 = qMax(e.line.y1(), e.line.y2());
            if (!exposed.isNull() &&
                (ex1 < exposed.left()  || ex0 > exposed.right() ||
                 ey1 < exposed.top()   || ey0 > exposed.bottom())) continue;

            const bool wide = hasElev && (e.slope * invSlope > kSlopeBreak);
            const int  alpha = wide ? 210 : 130;
            const qreal lw  = wide ? (active ? 0.9 : 0.6) : (active ? 0.35 : 0.25);

            // Use a cosmetic pen so width is in pixels regardless of zoom
            QPen pen(QColor(0, 0, 0, alpha));
            pen.setWidthF(lw);
            pen.setCosmetic(true);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawLine(e.line);
        }

        p->restore();
    }

private:
    SWMM2DMeshLayer *m_layer;
};

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
    rebuildSceneGeometry();
}

SWMM2DMeshLayer::~SWMM2DMeshLayer()
{
    OGRCoordinateTransformation::DestroyCT(m_transform);
}

// ---------------------------------------------------------------------------
// rebuildSceneGeometry — populates all scene caches from m_mesh
// ---------------------------------------------------------------------------

void SWMM2DMeshLayer::rebuildSceneGeometry()
{
    const int nVerts = m_mesh.vertices.size();

    m_sceneTris.clear();
    m_sceneEdges.clear();
    m_sceneNodes.clear();
    m_sceneBBox = QRectF();
    m_zMin = std::numeric_limits<double>::max();
    m_zMax = std::numeric_limits<double>::lowest();
    m_maxSlope = 0.0f;

    if (nVerts == 0 || m_mesh.triangles.isEmpty()) {
        ++m_geomRevision;
        return;
    }

    bool hasElevation = false;
    for (const auto &v : m_mesh.vertices)
        if (v.z != 0.0) { hasElevation = true; break; }

    // ── Scene-space vertex positions (with OGR reprojection + Y-flip) ──────
    QVector<QPointF> scenePts;
    scenePts.reserve(nVerts);
    for (const auto &v : m_mesh.vertices)
    {
        double x = v.xy.x(), y = v.xy.y();
        if (m_transform) m_transform->Transform(1, &x, &y);
        scenePts.append(QPointF(x, -y));  // Y-flip: scene grows downward
        if (hasElevation)
        {
            if (v.z < m_zMin) m_zMin = v.z;
            if (v.z > m_zMax) m_zMax = v.z;
        }
    }
    if (!hasElevation) { m_zMin = 0.0; m_zMax = 0.0; }

    // ── Node dots ────────────────────────────────────────────────────────────
    m_sceneNodes.reserve(nVerts);
    bool first = true;
    for (int i = 0; i < nVerts; ++i)
    {
        SceneNode n;
        n.pt     = scenePts[i];
        n.z      = static_cast<float>(m_mesh.vertices[i].z);
        n.tagged = !m_mesh.vertices[i].tag.isEmpty();
        m_sceneNodes.append(n);

        if (first) { m_sceneBBox = QRectF(n.pt, QSizeF(0,0)); first = false; }
        else {
            if (n.pt.x() < m_sceneBBox.left())   m_sceneBBox.setLeft(n.pt.x());
            if (n.pt.x() > m_sceneBBox.right())  m_sceneBBox.setRight(n.pt.x());
            if (n.pt.y() < m_sceneBBox.top())    m_sceneBBox.setTop(n.pt.y());
            if (n.pt.y() > m_sceneBBox.bottom()) m_sceneBBox.setBottom(n.pt.y());
        }
    }

    // ── Edges + triangles ───────────────────────────────────────────────────
    QSet<QPair<int,int>> seen;
    seen.reserve(m_mesh.triangles.size() * 3);
    m_sceneEdges.reserve(m_mesh.triangles.size() * 3);
    m_sceneTris.reserve(m_mesh.triangles.size());

    auto pushEdge = [&](int a, int b) {
        if (a == b) return;
        const QPair<int,int> key = (a < b) ? qMakePair(a,b) : qMakePair(b,a);
        if (seen.contains(key)) return;
        seen.insert(key);

        const double za = m_mesh.vertices[a].z;
        const double zb = m_mesh.vertices[b].z;
        const double dx = m_mesh.vertices[a].xy.x() - m_mesh.vertices[b].xy.x();
        const double dy = m_mesh.vertices[a].xy.y() - m_mesh.vertices[b].xy.y();
        const double dist = std::sqrt(dx*dx + dy*dy);
        const float  slope = (dist > 1e-9) ? static_cast<float>(std::abs(za-zb) / dist) : 0.0f;
        if (slope > m_maxSlope) m_maxSlope = slope;

        SceneEdge e;
        e.line  = QLineF(scenePts[a], scenePts[b]);
        e.zAvg  = static_cast<float>((za + zb) * 0.5);
        e.slope = slope;
        m_sceneEdges.append(e);
    };

    for (const auto &t : m_mesh.triangles)
    {
        if (t.v0 < 0 || t.v0 >= nVerts) continue;
        if (t.v1 < 0 || t.v1 >= nVerts) continue;
        if (t.v2 < 0 || t.v2 >= nVerts) continue;
        pushEdge(t.v0, t.v1);
        pushEdge(t.v1, t.v2);
        pushEdge(t.v2, t.v0);

        SceneTri st;
        st.a    = scenePts[t.v0];
        st.b    = scenePts[t.v1];
        st.c    = scenePts[t.v2];
        st.z0   = static_cast<float>(m_mesh.vertices[t.v0].z);
        st.z1   = static_cast<float>(m_mesh.vertices[t.v1].z);
        st.z2   = static_cast<float>(m_mesh.vertices[t.v2].z);
        st.zAvg = (st.z0 + st.z1 + st.z2) / 3.0f;
        m_sceneTris.append(st);
    }

    ++m_geomRevision;

    // Notify the graphics item (if any) that its geometry changed.
    if (m_graphicsItem)
        m_graphicsItem->geometryChanged();
}

// ---------------------------------------------------------------------------
// OpenSWMMVisLayer interface
// ---------------------------------------------------------------------------

void SWMM2DMeshLayer::setActiveMesh(bool active)
{
    if (m_active == active) return;
    m_active = active;
    emit repaintRequested();
}

void SWMM2DMeshLayer::setShowMeshNodes(bool show)
{
    if (m_showMeshNodes == show) return;
    m_showMeshNodes = show;
    emit repaintRequested();
}

void SWMM2DMeshLayer::populateScene(QGraphicsScene *scene,
                                     const MapExtent &,
                                     const SpatialReferenceSystem *)
{
    if (!scene || m_graphicsItem) return;
    m_graphicsItem = new SWMM2DMeshGraphicsItem(this);
    scene->addItem(m_graphicsItem);
}

void SWMM2DMeshLayer::depopulateScene(QGraphicsScene *scene)
{
    if (!m_graphicsItem) return;
    if (scene) scene->removeItem(m_graphicsItem);
    delete m_graphicsItem;
    m_graphicsItem = nullptr;
}

void SWMM2DMeshLayer::refreshScene(QGraphicsScene *scene,
                                    const MapExtent &extent,
                                    const SpatialReferenceSystem *canvasSRS)
{
    if (!scene) return;

    if (!isVisible()) {
        depopulateScene(scene);
        return;
    }

    if (!m_graphicsItem)
        populateScene(scene, extent, canvasSRS);
    else
        m_graphicsItem->update();
}

void SWMM2DMeshLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
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

    rebuildSceneGeometry();
    emit repaintRequested();
}
