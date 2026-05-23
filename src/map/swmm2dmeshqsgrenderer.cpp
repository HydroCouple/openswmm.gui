/*!
 * \file   swmm2dmeshqsgrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 *
 * Replaces the per-triangle QPainter path in MeshGraphicsItem::paint()
 * (O(N) setBrush+drawPolygon calls) with a single GPU draw call per pass.
 * All colour and width computations happen in CPU once per dirty frame and
 * are then stable across pans.
 */
#include "map/swmm2dmeshqsgrenderer.h"

#include "contour/marchingtriangles.h"
#include "layers/swmm2dmeshlayer.h"

#include <QMatrix4x4>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Elevation colour ramp  [0,1] → RGB   (matches MeshGraphicsItem)
// ---------------------------------------------------------------------------
void elevationColorRgb(double t, quint8 &r, quint8 &g, quint8 &b)
{
    struct Stop { double t; quint8 r, g, b; };
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
    r = quint8(qRound(lo.r + f*(hi.r - lo.r)));
    g = quint8(qRound(lo.g + f*(hi.g - lo.g)));
    b = quint8(qRound(lo.b + f*(hi.b - lo.b)));
}

// ---------------------------------------------------------------------------
// Thick-segment helper (two triangles = quad) — consistent on all RHI backends
// ---------------------------------------------------------------------------
void appendThickSeg(std::vector<QSGGeometry::Point2D> &out,
                    float ax, float ay, float bx, float by, float hw)
{
    const float dx = bx-ax, dy = by-ay;
    const float len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-9f) return;
    const float nx = -dy/len*hw, ny = dx/len*hw;
    auto v = [](float x, float y){ QSGGeometry::Point2D p; p.x=x; p.y=y; return p; };
    out.push_back(v(ax+nx,ay+ny)); out.push_back(v(bx+nx,by+ny)); out.push_back(v(ax-nx,ay-ny));
    out.push_back(v(bx+nx,by+ny)); out.push_back(v(bx-nx,by-ny)); out.push_back(v(ax-nx,ay-ny));
}

// ---------------------------------------------------------------------------
// Node-tree helpers
// ---------------------------------------------------------------------------

QSGGeometryNode *makeColoredNode()
{
    auto *node = new QSGGeometryNode();
    auto *geo  = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geo->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *mat = new QSGVertexColorMaterial();
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

QSGGeometryNode *makeFlatNode(QColor color)
{
    auto *node = new QSGGeometryNode();
    auto *geo  = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geo->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *mat  = new QSGFlatColorMaterial();
    mat->setColor(color);
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

void uploadColoredVerts(QSGGeometryNode *node,
                        const std::vector<QSGGeometry::ColoredPoint2D> &verts)
{
    auto *geo = node->geometry();
    const int n = int(verts.size());
    if (geo->vertexCount() != n) geo->allocate(n);
    if (n > 0)
        std::memcpy(geo->vertexDataAsColoredPoint2D(), verts.data(),
                    n * sizeof(QSGGeometry::ColoredPoint2D));
    node->markDirty(QSGNode::DirtyGeometry);
}

void uploadFlatVerts(QSGGeometryNode *node,
                     const std::vector<QSGGeometry::Point2D> &verts)
{
    auto *geo = node->geometry();
    const int n = int(verts.size());
    if (geo->vertexCount() != n) geo->allocate(n);
    if (n > 0)
        std::memcpy(geo->vertexData(), verts.data(),
                    n * sizeof(QSGGeometry::Point2D));
    node->markDirty(QSGNode::DirtyGeometry);
}

void setFlatColor(QSGGeometryNode *node, QColor c)
{
    auto *mat = static_cast<QSGFlatColorMaterial*>(node->material());
    if (mat->color() != c) { mat->setColor(c); node->markDirty(QSGNode::DirtyMaterial); }
}

} // namespace

// ---------------------------------------------------------------------------

SWMM2DMeshQSGRenderer::SWMM2DMeshQSGRenderer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

SWMM2DMeshQSGRenderer::~SWMM2DMeshQSGRenderer() = default;

void SWMM2DMeshQSGRenderer::setLayer(SWMM2DMeshLayer *layer)
{
    if (m_layer == layer) return;
    if (m_layer) QObject::disconnect(m_layer, nullptr, this, nullptr);
    m_layer = layer;
    if (m_layer) {
        connect(m_layer, &SWMM2DMeshLayer::repaintRequested,
                this, [this]() {
                    m_contentDirty = true;
                    update();
                });
    }
    m_contentDirty = true;
    update();
}

void SWMM2DMeshQSGRenderer::setMapExtent(const MapExtent &extent)
{
    if (extent == m_extent) return;
    const bool zoomChanged =
        !qFuzzyCompare(extent.width(),  m_extent.width()) ||
        !qFuzzyCompare(extent.height(), m_extent.height());
    m_extent = extent;
    if (zoomChanged) m_contentDirty = true;
    update();
}

QSGNode *SWMM2DMeshQSGRenderer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_layer || !m_extent.isValid() || width() <= 0 || height() <= 0) {
        delete oldNode;
        return nullptr;
    }

    // ---- Node tree, z-order bottom→top:
    //        triNode (Pass 1 hillshade)
    //        isobandNode (Pass 4 filled contours — BJ.2-filled)
    //        edgeThinNode + edgeWideNode (Pass 2 wireframe)
    //        contourNode (Pass 3 isolines — BJ.2-lite)
    auto *root = static_cast<QSGTransformNode *>(oldNode);
    QSGGeometryNode *triNode      = nullptr;
    QSGGeometryNode *isobandNode  = nullptr;
    QSGGeometryNode *edgeThinNode = nullptr;
    QSGGeometryNode *edgeWideNode = nullptr;
    QSGGeometryNode *contourNode  = nullptr;

    if (!root) {
        root         = new QSGTransformNode();
        triNode      = makeColoredNode();
        isobandNode  = makeColoredNode();   // per-vertex colour for band fill
        edgeThinNode = makeFlatNode(QColor(0, 0, 0, 130));
        edgeWideNode = makeFlatNode(QColor(0, 0, 0, 210));
        contourNode  = makeFlatNode(QColor(0x1a, 0x1a, 0x1a, 200));
        root->appendChildNode(triNode);
        root->appendChildNode(isobandNode);
        root->appendChildNode(edgeThinNode);
        root->appendChildNode(edgeWideNode);
        root->appendChildNode(contourNode);
    } else {
        auto *c  = root->firstChild();
        triNode      = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        isobandNode  = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        edgeThinNode = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        edgeWideNode = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        contourNode  = static_cast<QSGGeometryNode*>(c);
    }

    // ---- Shared render params ----------------------------------------------
    const float sx_r    = float(width())  / float(m_extent.width());
    const float invView = (sx_r > 0.0f) ? 1.0f / sx_r : 1.0f;

    // Frustum margins in scene units. Stored as double — projected-CRS
    // coords (6-7 digit magnitudes) lose ~1 m of precision when
    // squeezed into a float, causing edge cases where a mesh tile gets
    // wrongly culled at high zoom.
    const double cullMargin = double(2.0f * invView);
    const double cullX0 =  m_extent.xMin() - cullMargin;
    const double cullX1 =  m_extent.xMax() + cullMargin;
    const double cullY0 = -m_extent.yMax() - cullMargin;
    const double cullY1 = -m_extent.yMin() + cullMargin;

    // Early-out if mesh bbox entirely outside view
    {
        const QRectF &bb = m_layer->m_sceneBBox;
        if (!bb.isNull() &&
            (bb.right() < cullX0 || bb.left() > cullX1 ||
             bb.bottom() < cullY0 || bb.top() > cullY1))
        {
            // Nothing visible — clear GPU geometry to free memory, but keep
            // m_contentDirty=true so a pan-back at the same zoom level still
            // triggers a full rebuild (don't reset it to false here).
            const std::vector<QSGGeometry::ColoredPoint2D> empty_c;
            const std::vector<QSGGeometry::Point2D>        empty_p;
            uploadColoredVerts(triNode,      empty_c);
            uploadColoredVerts(isobandNode,  empty_c);
            uploadFlatVerts(edgeThinNode,    empty_p);
            uploadFlatVerts(edgeWideNode,    empty_p);
            uploadFlatVerts(contourNode,     empty_p);
            m_contentDirty = true;
            // Apply transform so stacking order is maintained, then exit.
            const float msx = float(width())  / float(m_extent.width());
            const float msy = float(height()) / float(m_extent.height());
            QMatrix4x4 mat;
            mat.scale(msx, msy);
            mat.translate(float(m_anchorX - m_extent.xMin()),
                          float(m_anchorY + m_extent.yMax()));
            if (root->matrix() != mat) root->setMatrix(mat);
            return root;
        }
    }

    // ---- Full content rebuild ---------------------------------------------
    if (m_contentDirty) {

        // Anchor — centre of scene bbox for float precision
        {
            const QRectF &bb = m_layer->m_sceneBBox;
            m_anchorX = bb.isNull() ? 0.0 : (bb.left() + bb.right())  * 0.5;
            m_anchorY = bb.isNull() ? 0.0 : (bb.top()  + bb.bottom()) * 0.5;
        }

        const double ox = m_anchorX, oy = m_anchorY;
        const bool active   = m_layer->isActiveMesh();
        const bool hasElev  = (m_layer->m_zMax > m_layer->m_zMin);
        const double zMin   = m_layer->m_zMin;
        const double zMax   = m_layer->m_zMax;
        const float maxSlope = m_layer->m_maxSlope;

        // Style constants (mirrors MeshGraphicsItem). Hillshade params come
        // from the layer now (Slice AU.6.4-lite); defaults reproduce the
        // historic constants kLx=kLy=-0.5774, kLz=+0.5774, kVertExag=3.0,
        // kLitMin=0.15 when the user has not edited them.
        const quint8 kFillAlpha   = quint8(active ? 160 : 110);
        const float  kVertExag    = float(m_layer->hillshadeZExag());
        const double azRad        = qDegreesToRadians(m_layer->hillshadeAzimuth());
        const double altRad       = qDegreesToRadians(m_layer->hillshadeAltitude());
        const float  kLx          = float(std::sin(azRad) * std::cos(altRad));
        const float  kLy          = float(std::cos(azRad) * std::cos(altRad));
        const float  kLz          = float(std::sin(altRad));
        const float  kLitMin      = float(m_layer->hillshadeMinLit());
        const float  kSlopeBreak  = 0.35f;
        const float  kThinHW      = (active ? 0.35f : 0.25f) * invView;
        const float  kWideHW      = (active ? 0.90f : 0.60f) * invView;
        const int    kThinAlpha   = 130;
        const int    kWideAlpha   = 210;

        // ---- Pass 1: filled triangles --------------------------------------
        {
            const auto &tris = m_layer->m_sceneTris;
            std::vector<QSGGeometry::ColoredPoint2D> verts;
            verts.reserve(size_t(tris.size()) * 3);

            if (hasElev) {
                const double invRange = 1.0 / (zMax - zMin);
                for (const auto &t : tris) {
                    // Frustum cull at triangle level (double precision —
                    // float quantises projected-CRS coords ~1 m).
                    const double tMinX = std::min({t.a.x(), t.b.x(), t.c.x()});
                    const double tMaxX = std::max({t.a.x(), t.b.x(), t.c.x()});
                    const double tMinY = std::min({t.a.y(), t.b.y(), t.c.y()});
                    const double tMaxY = std::max({t.a.y(), t.b.y(), t.c.y()});
                    if (tMaxX < cullX0 || tMinX > cullX1 ||
                        tMaxY < cullY0 || tMinY > cullY1) continue;

                    // Elevation colour
                    quint8 cr, cg, cb;
                    elevationColorRgb((t.zAvg - float(zMin)) * float(invRange), cr, cg, cb);

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

                    const quint8 r = quint8(qBound(0, int(float(cr)*lit), 255));
                    const quint8 g = quint8(qBound(0, int(float(cg)*lit), 255));
                    const quint8 b = quint8(qBound(0, int(float(cb)*lit), 255));

                    for (const QPointF *pt : {&t.a, &t.b, &t.c}) {
                        QSGGeometry::ColoredPoint2D v;
                        v.set(float(pt->x() - ox), float(pt->y() - oy), r, g, b, kFillAlpha);
                        verts.push_back(v);
                    }
                }
            } else {
                // No elevation — flat steel blue
                const quint8 fr=70, fg=130, fb=180;
                for (const auto &t : tris) {
                    const double tMinX = std::min({t.a.x(), t.b.x(), t.c.x()});
                    const double tMaxX = std::max({t.a.x(), t.b.x(), t.c.x()});
                    const double tMinY = std::min({t.a.y(), t.b.y(), t.c.y()});
                    const double tMaxY = std::max({t.a.y(), t.b.y(), t.c.y()});
                    if (tMaxX < cullX0 || tMinX > cullX1 ||
                        tMaxY < cullY0 || tMinY > cullY1) continue;
                    for (const QPointF *pt : {&t.a, &t.b, &t.c}) {
                        QSGGeometry::ColoredPoint2D v;
                        v.set(float(pt->x()-ox), float(pt->y()-oy), fr, fg, fb, kFillAlpha);
                        verts.push_back(v);
                    }
                }
            }
            uploadColoredVerts(triNode, verts);
        }

        // ---- Pass 4: filled iso-bands (Slice BJ.2-filled) ----------------
        // Z-ordered above Pass 1 (hillshade) and below Pass 2 (edges) +
        // Pass 3 (lines) so the underlying hillshade can still show through
        // the configurable alpha while the line / wireframe overlays stay
        // crisp on top.
        if (hasElev && m_layer->filledContours()) {
            const int nBands = m_layer->contourIntervalCount();
            const auto levels = OpenSWMM::Contour::evenlySpacedLevelsInclusive(
                zMin, zMax, nBands + 1);
            const quint8 alpha = quint8(qBound(0, int(m_layer->filledContoursOpacity() * 255.0 + 0.5), 255));

            const auto &sceneTris = m_layer->m_sceneTris;
            const auto extract = [ox, oy](const SWMM2DMeshLayer::SceneTri &t,
                                          QPointF &p0, QPointF &p1, QPointF &p2,
                                          double  &v0, double  &v1, double  &v2) {
                p0 = QPointF(t.a.x() - ox, t.a.y() - oy);
                p1 = QPointF(t.b.x() - ox, t.b.y() - oy);
                p2 = QPointF(t.c.x() - ox, t.c.y() - oy);
                v0 = double(t.z0);
                v1 = double(t.z1);
                v2 = double(t.z2);
            };

            const auto bands = OpenSWMM::Contour::marchingTrianglesIsobands(
                sceneTris, levels, extract);

            // Triangulate each convex band polygon as a fan from vertex 0.
            // Per-vertex colour = Viridis sampled at the band's mid-point
            // in [0, 1]. nBands == 1 is a guard against div-by-zero — fall
            // back to a single mid-ramp colour for that degenerate case.
            const double bandDenom = (nBands > 0) ? double(nBands) : 1.0;
            std::vector<QSGGeometry::ColoredPoint2D> bandVerts;
            bandVerts.reserve(bands.size() * 9);   // typical V=4 → 6 verts

            for (const auto &bp : bands) {
                if (bp.verts.size() < 3) continue;
                const double t = (double(bp.bandIndex) + 0.5) / bandDenom;
                const QColor col = OpenSWMM::Contour::viridisAt(t);
                const quint8 r = quint8(col.red());
                const quint8 g = quint8(col.green());
                const quint8 b = quint8(col.blue());
                for (size_t i = 1; i + 1 < bp.verts.size(); ++i) {
                    auto push = [&](const QPointF &p) {
                        QSGGeometry::ColoredPoint2D v;
                        v.set(float(p.x()), float(p.y()), r, g, b, alpha);
                        bandVerts.push_back(v);
                    };
                    push(bp.verts[0]);
                    push(bp.verts[i]);
                    push(bp.verts[i + 1]);
                }
            }
            uploadColoredVerts(isobandNode, bandVerts);
        } else {
            uploadColoredVerts(isobandNode, std::vector<QSGGeometry::ColoredPoint2D>{});
        }

        // ---- Pass 2: edges — split into thin and wide ----------------------
        // Slice AZ.3.4 — when SWMM2DMeshLayer::showEdges() is false, upload
        // empty geometry to both edge nodes so the wireframe disappears
        // without dismantling the QSG node tree. setShowEdges() emits
        // repaintRequested() which sets m_contentDirty, landing us here.
        if (m_layer->showEdges()) {
            const auto &edges = m_layer->m_sceneEdges;
            const float invSlope = (maxSlope > 0.f) ? 1.0f / maxSlope : 0.0f;

            std::vector<QSGGeometry::Point2D> thinSegs, wideSegs;
            thinSegs.reserve(edges.size() * 6);
            wideSegs.reserve(edges.size() / 8 * 6);

            for (const auto &e : edges) {
                // Frustum cull per edge (double precision).
                const double ex0 = qMin(e.line.x1(), e.line.x2());
                const double ex1 = qMax(e.line.x1(), e.line.x2());
                const double ey0 = qMin(e.line.y1(), e.line.y2());
                const double ey1 = qMax(e.line.y1(), e.line.y2());
                if (ex1 < cullX0 || ex0 > cullX1 ||
                    ey1 < cullY0 || ey0 > cullY1) continue;

                const float ax = float(e.line.x1()-ox), ay = float(e.line.y1()-oy);
                const float bx = float(e.line.x2()-ox), by = float(e.line.y2()-oy);

                if (hasElev && (e.slope * invSlope > kSlopeBreak))
                    appendThickSeg(wideSegs, ax, ay, bx, by, kWideHW);
                else
                    appendThickSeg(thinSegs, ax, ay, bx, by, kThinHW);
            }
            uploadFlatVerts(edgeThinNode, thinSegs);
            uploadFlatVerts(edgeWideNode, wideSegs);

            setFlatColor(edgeThinNode, QColor(0, 0, 0, kThinAlpha));
            setFlatColor(edgeWideNode, QColor(0, 0, 0, kWideAlpha));
        } else {
            const std::vector<QSGGeometry::Point2D> empty;
            uploadFlatVerts(edgeThinNode, empty);
            uploadFlatVerts(edgeWideNode, empty);
        }

        // ---- Pass 3: bed-elevation contour lines (Slice BJ.2-lite) -------
        // Marching-triangles over m_sceneTris, N evenly-spaced levels in
        // [zMin, zMax]. Lines drawn as a single thick-segment list so the
        // user-configurable width is honoured on every RHI backend (same
        // pattern as Pass 2).
        if (hasElev && m_layer->showContours()) {
            const int    nLevels = m_layer->contourIntervalCount();
            const float  cHW     = float(0.5 * m_layer->contourLineWidth()) * invView;
            const auto   levels  = OpenSWMM::Contour::evenlySpacedLevels(
                                       zMin, zMax, nLevels);

            // Triangle iterator: extractor pulls (a/b/c, z0/z1/z2) from
            // the existing scene cache and shifts xy by the bbox-centre
            // anchor so the float vertex coords stay small.
            const auto &sceneTris = m_layer->m_sceneTris;
            const auto extract = [ox, oy](const SWMM2DMeshLayer::SceneTri &t,
                                          QPointF &p0, QPointF &p1, QPointF &p2,
                                          double  &v0, double  &v1, double  &v2)
            {
                p0 = QPointF(t.a.x() - ox, t.a.y() - oy);
                p1 = QPointF(t.b.x() - ox, t.b.y() - oy);
                p2 = QPointF(t.c.x() - ox, t.c.y() - oy);
                v0 = double(t.z0);
                v1 = double(t.z1);
                v2 = double(t.z2);
            };

            const auto segs = OpenSWMM::Contour::marchingTriangles(
                sceneTris, levels, extract);

            std::vector<QSGGeometry::Point2D> verts;
            verts.reserve(segs.size() * 6);
            for (const auto &s : segs) {
                appendThickSeg(verts,
                               float(s.a.x()), float(s.a.y()),
                               float(s.b.x()), float(s.b.y()),
                               cHW);
            }
            uploadFlatVerts(contourNode, verts);
            setFlatColor(contourNode, m_layer->contourColor());
        } else {
            uploadFlatVerts(contourNode, std::vector<QSGGeometry::Point2D>{});
        }

        m_contentDirty = false;
    }

    // ---- Transform (always — pan = matrix update only) --------------------
    const float msx = float(width())  / float(m_extent.width());
    const float msy = float(height()) / float(m_extent.height());
    QMatrix4x4 mat;
    mat.scale(msx, msy);
    mat.translate(float(m_anchorX - m_extent.xMin()),
                  float(m_anchorY + m_extent.yMax()));
    if (root->matrix() != mat) root->setMatrix(mat);

    return root;
}
