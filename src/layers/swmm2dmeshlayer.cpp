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

#include "mesh/meshobjectref.h"

#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include "render/ifeaturerenderer.h"
// Contour generation for the iso-band / iso-line sublayer passes.
#include "contour/marchingtriangles.h"
// Slice B.5b — Rule Model mirror for 2D mesh layers.
#include "render/rastersymbollayers.h"
#include "render/rule.h"
#include "render/rulelist.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"
// Slice Z.14-paint — polygon clip mask.
#include "render/maskclipresolver.h"
#include "ui/dialogs/ilayerstylesubject.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/meshnodesublayer.h"

#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QHash>
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
        const double zMax    = m_layer->m_zMax;
        const double zRange  = zMax - zMin;
        const bool active    = m_layer->isActiveMesh();
        const int  fillAlpha = active ? 160 : 110;

        // Per-sublayer visibility + opacity. This is the active render path
        // (the QSG renderer is inactive), so the layer-tree checkboxes and
        // opacity edits flow through these sublayer flags into the passes
        // below. fill + edges default on; nodes / bands / isolines default
        // off (see SWMM2DMeshLayer ctor). A null sublayer falls back to the
        // historic "always visible" behaviour for fill/edges.
        const auto *fillSub = m_layer->meshFillSublayer();
        const auto *edgeSub = m_layer->meshEdgeSublayer();
        const auto *nodeSub = m_layer->meshNodeSublayer();
        const auto *bandSub = m_layer->contourBandSublayer();
        const auto *isoSub  = m_layer->isolineSublayer();

        const bool  fillVisible  = !fillSub || fillSub->isVisible();
        const bool  edgesVisible = !edgeSub || edgeSub->isVisible();
        const bool  nodesVisible = nodeSub && nodeSub->isVisible();
        const bool  bandsVisible = bandSub && bandSub->isVisible();
        const bool  isoVisible   = isoSub  && isoSub->isVisible();

        const qreal fillOpacity = fillSub ? fillSub->opacity() : 1.0;
        const qreal edgeOpacity = edgeSub ? edgeSub->opacity() : 1.0;

        // Multiply a colour's alpha channel by a 0..1 sublayer opacity.
        auto withOpacity = [](QColor c, qreal op) {
            c.setAlpha(qBound(0, int(c.alpha() * op + 0.5), 255));
            return c;
        };

        // Hillshade light direction (NW at ~35° elevation — matches QSG renderer)
        constexpr float kLx = -0.5774f, kLy = -0.5774f, kLz = 0.5774f;
        constexpr float kVertExag = 3.0f;
        constexpr float kLitMin   = 0.15f;

        p->save();
        p->setPen(Qt::NoPen);

        // Slice Z.14-paint — apply the layer's mask if one is configured.
        // The resolver returns ok=false on disabled / unresolvable masks
        // so paint continues unclipped; otherwise we install a clip path
        // for the duration of this paint pass (p->save above will
        // restore on the p->restore below).
        {
            const auto clip = OpenSWMM::Render::resolveMaskClip(
                m_layer, m_layer->maskSpec());
            if (clip.ok && !clip.path.isEmpty()) {
                if (clip.mode == OpenSWMM::Render::MaskMode::ClipInside) {
                    p->setClipPath(clip.path, Qt::IntersectClip);
                } else {
                    // ClipOutside: invert by subtracting the polygon from
                    // the painter's current clip region (≈ exposed rect).
                    QPainterPath all;
                    all.addRect(exposed.isNull()
                                    ? m_layer->m_sceneBBox.adjusted(-1, -1, 1, 1)
                                    : exposed);
                    p->setClipPath(all.subtracted(clip.path),
                                    Qt::IntersectClip);
                }
            }
        }

        // Use the spatial grid to walk only the triangles / edges whose
        // bbox intersects the exposed rect — paint cost goes from O(N)
        // to O(visible). Fall back to the full vector if the grid is
        // empty (no rebuild yet) or no exposed rect is given (full paint).
        const bool haveExposed = !exposed.isNull();
        const QVector<int> visibleTris  = (haveExposed && !m_layer->m_triGrid.isEmpty())
                                          ? m_layer->m_triGrid.query(exposed)
                                          : QVector<int>{};
        const QVector<int> visibleEdges = (haveExposed && !m_layer->m_edgeGrid.isEmpty())
                                          ? m_layer->m_edgeGrid.query(exposed)
                                          : QVector<int>{};
        const bool useTriIdx  = haveExposed && !m_layer->m_triGrid.isEmpty();
        const bool useEdgeIdx = haveExposed && !m_layer->m_edgeGrid.isEmpty();
        const int triCount  = useTriIdx  ? visibleTris.size()  : tris.size();
        const int edgeCount = useEdgeIdx ? visibleEdges.size() : edges.size();

        // ---- Pass 1: filled triangles (MeshFillSublayer) ---------------------
        if (fillVisible) {
            const int fa = qBound(0, int(fillAlpha * fillOpacity + 0.5), 255);
            for (int i = 0; i < triCount; ++i) {
                const SWMM2DMeshLayer::SceneTri &t =
                    useTriIdx ? tris[visibleTris[i]] : tris[i];

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

                p->setBrush(QColor(cr, cg, cb, fa));
                const QPointF pts[3] = {t.a, t.b, t.c};
                p->drawConvexPolygon(pts, 3);
            }
        }

        // ---- Pass 4: filled iso-bands (ContourBandSublayer) ------------------
        // Z-ordered above the hillshade fill and below the wireframe / lines.
        // Marching-triangles over the scene triangles; one fan-triangulated
        // convex polygon per (triangle, band). Off by default.
        if (hasElev && bandsVisible) {
            const auto *bandStyle = bandSub->bandStyle();
            const int   nBands   = bandStyle ? bandStyle->bandCount() : 8;
            const bool  smooth   = bandStyle ? bandStyle->smoothBands() : true;
            const QColor lowC    = bandStyle ? bandStyle->lowColor()  : QColor( 60, 100, 200);
            const QColor highC   = bandStyle ? bandStyle->highColor() : QColor(200, 220, 255);
            const int   bandAlpha = qBound(0, int(bandSub->opacity() * 255.0 + 0.5), 255);

            const auto levels = OpenSWMM::Contour::evenlySpacedLevelsInclusive(
                zMin, zMax, nBands + 1);
            const auto extract = [](const SWMM2DMeshLayer::SceneTri &t,
                                    QPointF &p0, QPointF &p1, QPointF &p2,
                                    double  &v0, double  &v1, double  &v2) {
                p0 = t.a; p1 = t.b; p2 = t.c;
                v0 = t.z0; v1 = t.z1; v2 = t.z2;
            };
            const auto bands = OpenSWMM::Contour::marchingTrianglesIsobands(
                tris, levels, extract);

            const double bandDenom = (nBands > 0) ? double(nBands) : 1.0;
            p->setPen(Qt::NoPen);
            for (const auto &bp : bands) {
                if (bp.verts.size() < 3) continue;
                const double tt = (double(bp.bandIndex) + 0.5) / bandDenom;
                QColor col = smooth
                    ? QColor::fromRgbF(lowC.redF()   * (1.0 - tt) + highC.redF()   * tt,
                                       lowC.greenF() * (1.0 - tt) + highC.greenF() * tt,
                                       lowC.blueF()  * (1.0 - tt) + highC.blueF()  * tt)
                    : OpenSWMM::Contour::viridisAt(tt);
                col.setAlpha(bandAlpha);
                p->setBrush(col);
                QPolygonF poly;
                poly.reserve(int(bp.verts.size()));
                for (const QPointF &v : bp.verts) poly << v;
                p->drawPolygon(poly);
            }
        }

        // ---- Pass 2: edges (MeshEdgeSublayer) --------------------------------
        const float maxSlope = m_layer->m_maxSlope;
        const float invSlope = (maxSlope > 0.f) ? 1.0f / maxSlope : 0.0f;
        constexpr float kSlopeBreak = 0.35f;

        if (edgesVisible) {
            for (int i = 0; i < edgeCount; ++i) {
                const SWMM2DMeshLayer::SceneEdge &e =
                    useEdgeIdx ? edges[visibleEdges[i]] : edges[i];

                const bool wide = hasElev && (e.slope * invSlope > kSlopeBreak);
                const int  alpha = wide ? 210 : 130;
                const qreal lw  = wide ? (active ? 0.9 : 0.6) : (active ? 0.35 : 0.25);

                // Use a cosmetic pen so width is in pixels regardless of zoom
                QPen pen(withOpacity(QColor(0, 0, 0, alpha), edgeOpacity));
                pen.setWidthF(lw);
                pen.setCosmetic(true);
                p->setPen(pen);
                p->setBrush(Qt::NoBrush);
                p->drawLine(e.line);
            }
        }

        // ---- Pass 3: bed-elevation iso-lines (IsolineSublayer) ---------------
        // Marching-triangles, N evenly-spaced levels in [zMin, zMax]. Off by
        // default; line colour / width / count come from IsolineStyle.
        if (hasElev && isoVisible) {
            const auto *isoStyle = isoSub->isolineStyle();
            const int   nLevels  = isoStyle ? isoStyle->isoValueCount() : 8;
            const double widthPx = isoStyle ? isoStyle->lineWidthPx() : 1.0;
            const QColor lineCol = isoStyle ? isoStyle->color() : QColor(10, 10, 10, 220);

            const auto levels = OpenSWMM::Contour::evenlySpacedLevels(
                zMin, zMax, nLevels);
            const auto extract = [](const SWMM2DMeshLayer::SceneTri &t,
                                    QPointF &p0, QPointF &p1, QPointF &p2,
                                    double  &v0, double  &v1, double  &v2) {
                p0 = t.a; p1 = t.b; p2 = t.c;
                v0 = t.z0; v1 = t.z1; v2 = t.z2;
            };
            const auto segs = OpenSWMM::Contour::marchingTriangles(
                tris, levels, extract);

            QPen pen(withOpacity(lineCol, isoSub->opacity()));
            pen.setWidthF(widthPx);
            pen.setCosmetic(true);
            pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            for (const auto &s : segs)
                p->drawLine(s.a, s.b);
        }

        // ---- Pass 5: mesh-vertex markers (MeshNodeSublayer) ------------------
        // Stylable replacement for the historic showMeshNodes toggle. Tagged
        // (SWMM-coupled) vertices get the highlight colour / size. Off by
        // default. Marker radius is specified in pixels, so convert to scene
        // units via the painter's current scale.
        if (nodesVisible) {
            const auto &nodes = m_layer->m_sceneNodes;
            const auto *nodeStyle = nodeSub->nodeStyle();
            const QColor baseC   = nodeStyle ? nodeStyle->color() : QColor(40, 40, 40, 220);
            const QColor taggedC = nodeStyle ? nodeStyle->taggedColor()
                                             : QColor(0xff, 0x8c, 0x00, 235);
            const double baseSzPx   = nodeStyle ? nodeStyle->markerSizePx() : 3.0;
            const double taggedSzPx = nodeStyle ? nodeStyle->taggedSizePx() : 5.0;
            const bool   highlight  = nodeStyle ? nodeStyle->highlightTagged() : true;
            const qreal  nodeOp     = nodeSub->opacity();

            const QTransform wt = p->worldTransform();
            const double scale  = wt.m11();
            const double pxToScene = (scale > 0.0) ? (1.0 / scale) : 1.0;
            const QColor baseUsed   = withOpacity(baseC, nodeOp);
            const QColor taggedUsed = withOpacity(taggedC, nodeOp);

            p->setPen(Qt::NoPen);
            for (const SWMM2DMeshLayer::SceneNode &n : nodes) {
                if (haveExposed && !exposed.contains(n.pt)) continue;
                const bool tag = highlight && n.tagged;
                const double r = 0.5 * (tag ? taggedSzPx : baseSzPx) * pxToScene;
                p->setBrush(tag ? taggedUsed : baseUsed);
                p->drawEllipse(n.pt, r, r);
            }
        }

        // ---- Pass 3: selection highlight (cells / edges / vertices) ----------
        // Cyan, drawn on top of fill + edges. This is the active render path
        // (the QSG renderer is currently inactive), so all mesh-element
        // highlights are rendered here.
        const auto &nodes = m_layer->m_sceneNodes;
        const QSet<int> &selT = m_layer->highlightedTriangles();
        const QSet<int> &selE = m_layer->highlightedEdges();
        const QSet<int> &selV = m_layer->highlightedVertices();

        if (!selT.isEmpty()) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0, 200, 255, 110));
            const int nt = tris.size();
            for (int t : selT) {
                if (t < 0 || t >= nt) continue;
                const SWMM2DMeshLayer::SceneTri &tr = tris[t];
                const QPointF pts[3] = { tr.a, tr.b, tr.c };
                p->drawConvexPolygon(pts, 3);
            }
        }

        if (!selE.isEmpty()) {
            QPen epen(QColor(0, 200, 255, 235));
            epen.setWidthF(2.6);
            epen.setCosmetic(true);
            epen.setCapStyle(Qt::RoundCap);
            p->setPen(epen);
            p->setBrush(Qt::NoBrush);
            const int nt = tris.size();
            for (int flat : selE) {
                const int t = flat / 3, e = flat % 3;
                if (t < 0 || t >= nt) continue;
                const SWMM2DMeshLayer::SceneTri &tr = tris[t];
                // edge 0 → (b,c), 1 → (c,a), 2 → (a,b)
                QPointF p0, p1;
                switch (e) {
                case 0: p0 = tr.b; p1 = tr.c; break;
                case 1: p0 = tr.c; p1 = tr.a; break;
                default: p0 = tr.a; p1 = tr.b; break;
                }
                p->drawLine(p0, p1);
            }
        }

        if (!selV.isEmpty()) {
            const QTransform wt = p->worldTransform();
            const double scale = wt.m11();
            const double r = (scale > 0.0) ? (5.0 / scale) : 1.0;   // ~5 px
            p->setPen(QPen(QColor(0, 120, 200, 240), (scale > 0.0) ? 1.5 / scale : 1.0));
            p->setBrush(QColor(0, 200, 255, 200));
            for (int vi : selV) {
                if (vi < 0 || vi >= nodes.size()) continue;
                p->drawEllipse(nodes[vi].pt, r, r);
            }
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

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Default to a
    // SingleSymbolRenderer so renderer() never returns null.  Paint loop
    // still uses the hillshade ramp directly; refactor deferred to
    // 8.13.6.4.
    m_renderer = std::make_unique<OpenSWMM::Render::SingleSymbolRenderer>();

    // Sublayer mix. QObject parent-child keeps them alive for the layer's
    // lifetime; sublayers() returns them in paint order. Defaults preserve
    // the historic visual: fill + edges on, nodes/contours/bands off.
    m_meshFillSublayer    = new OpenSWMM::Render::MeshFillSublayer(
        QStringLiteral("mesh.fill"), this);
    m_meshEdgeSublayer    = new OpenSWMM::Render::MeshEdgeSublayer(
        QStringLiteral("mesh.edges"), this);
    m_meshNodeSublayer    = new OpenSWMM::Render::MeshNodeSublayer(
        QStringLiteral("mesh.vertices"), this);
    m_contourBandSublayer = new OpenSWMM::Render::ContourBandSublayer(
        QStringLiteral("mesh.contourBands"), this);
    m_isolineSublayer     = new OpenSWMM::Render::IsolineSublayer(
        QStringLiteral("mesh.isolines"), this);

    // Bind contour/isoline attribute to elevation so the dialog shows a
    // meaningful default and the renderer pulls z values when iterating
    // SceneTri.
    if (auto *bs = m_contourBandSublayer->bandStyle())
        bs->setAttribute(QStringLiteral("elevation"));
    if (auto *is = m_isolineSublayer->isolineStyle()) {
        is->setAttribute(QStringLiteral("elevation"));
        is->setColor(QColor(0x1a, 0x1a, 0x1a, 200));
    }

    // Route any sublayer (visibility / style) change to the renderer.
    // repaintRequested() is what the QSG renderer connects to in
    // SWMM2DMeshQSGRenderer::setLayer, so this single hop covers every
    // sublayer state mutation.
    auto wire = [this](OpenSWMM::Render::ISublayer *s) {
        if (!s) return;
        QObject::connect(s, &OpenSWMM::Render::ISublayer::invalidated,
                         this, [this]() { emit repaintRequested(); });
    };
    wire(m_meshFillSublayer);
    wire(m_meshEdgeSublayer);
    wire(m_meshNodeSublayer);
    wire(m_contourBandSublayer);
    wire(m_isolineSublayer);

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
    resizeBCsToMesh();
    rebuildVertexAdjacency();
}

SWMM2DMeshLayer::~SWMM2DMeshLayer()
{
    OGRCoordinateTransformation::DestroyCT(m_transform);
}

QString SWMM2DMeshLayer::sourcePath() const
{
    // Absolute path so the Properties window shows the full on-disk
    // mesh location.
    return m_sourcePath.isEmpty()
               ? m_sourcePath
               : QFileInfo(m_sourcePath).absoluteFilePath();
}

// ---------------------------------------------------------------------------
// Renderer (Slice BI Phase 8.13.6.6)
// ---------------------------------------------------------------------------

OpenSWMM::Render::IFeatureRenderer *SWMM2DMeshLayer::renderer() const
{
    return m_renderer.get();
}

void SWMM2DMeshLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r)
{
    if (!r)
        return;
    if (r.get() == m_renderer.get())
        return;
    m_renderer = std::move(r);
    emit rendererChanged();
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
    m_triBBoxes.clear();
    m_edgeBBoxes.clear();
    m_triGrid.clear();
    m_edgeGrid.clear();
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

    // ── Spatial-grid build for paint-time culling ───────────────────────────
    // Without this every paint loops over all triangles/edges and runs the
    // bbox check inline — on a 100K-triangle mesh that's millions of float
    // ops per frame even when only a handful of triangles are visible.
    m_triBBoxes.resize(m_sceneTris.size());
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        const SceneTri &t = m_sceneTris[i];
        const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
        const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
        const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
        const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
        m_triBBoxes[i] = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    }
    m_edgeBBoxes.resize(m_sceneEdges.size());
    for (int i = 0; i < m_sceneEdges.size(); ++i) {
        const QLineF &ln = m_sceneEdges[i].line;
        const double x0 = qMin(ln.x1(), ln.x2()), x1 = qMax(ln.x1(), ln.x2());
        const double y0 = qMin(ln.y1(), ln.y2()), y1 = qMax(ln.y1(), ln.y2());
        m_edgeBBoxes[i] = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    }
    m_triGrid.rebuild(m_triBBoxes);
    m_edgeGrid.rebuild(m_edgeBBoxes);

    ++m_geomRevision;

    // Notify the graphics item (if any) that its geometry changed.
    if (m_graphicsItem)
        m_graphicsItem->geometryChanged();
}

// MeshSpatialGrid — definitions live in src/layers/meshspatialgrid.cpp so
// the spatial-index logic can be unit-tested without linking the whole
// layer. See include/layers/meshspatialgrid.h for the storage layout and
// threading contract.

// ---------------------------------------------------------------------------
// OpenSWMMVisLayer interface
// ---------------------------------------------------------------------------

void SWMM2DMeshLayer::setActiveMesh(bool active)
{
    if (m_active == active) return;
    m_active = active;
    emit activeMeshChanged(m_active);
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Slice §V.VA — mesh-editing foundation
// ---------------------------------------------------------------------------

void SWMM2DMeshLayer::resizeBCsToMesh()
{
    // NB: `slots` is a Qt keyword macro — pick a different local name.
    const int nt = m_mesh.triangles.size();
    const int nslots = nt * 3;
    if (m_bc.size() != nslots) {
        m_bc.resize(nslots);
        // Default-constructed MeshEdgeBC == Wall + zero params + empty group.
    }

    // Precompute boundary status from triangle adjacency: walk every
    // triangle's three edges, count how many triangles share each
    // sorted-vertex-pair edge. count == 1 → boundary (no neighbour
    // across that edge); count >= 2 → interior. Engine-loaded meshes
    // often arrive with m_mesh.boundaryEdges empty (only meshes the GUI
    // generates fill that vector), so deriving from topology is the
    // robust path.
    QHash<QPair<int,int>, int> edgeUseCount;
    edgeUseCount.reserve(nt * 3);
    for (int t = 0; t < nt; ++t) {
        const auto &tri = m_mesh.triangles[t];
        const int va[3] = {tri.v1, tri.v2, tri.v0};
        const int vb[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) {
            const QPair<int,int> key = (va[e] < vb[e]) ? qMakePair(va[e], vb[e])
                                                       : qMakePair(vb[e], va[e]);
            ++edgeUseCount[key];
        }
    }

    // Any edge explicitly listed in m_mesh.boundaryEdges is also a
    // boundary — defensive for degenerate meshes where the topology
    // pass might miss something (e.g. a single triangle on its own).
    QSet<QPair<int,int>> markerBoundary;
    for (const auto &e : m_mesh.boundaryEdges) {
        const QPair<int,int> key = (e.v0 < e.v1) ? qMakePair(e.v0, e.v1)
                                                 : qMakePair(e.v1, e.v0);
        markerBoundary.insert(key);
    }

    m_isBoundary.resize(nslots);
    std::fill(m_isBoundary.begin(), m_isBoundary.end(), false);
    for (int t = 0; t < nt; ++t) {
        const auto &tri = m_mesh.triangles[t];
        const int va[3] = {tri.v1, tri.v2, tri.v0};
        const int vb[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) {
            const QPair<int,int> key = (va[e] < vb[e]) ? qMakePair(va[e], vb[e])
                                                       : qMakePair(vb[e], va[e]);
            const int uses = edgeUseCount.value(key, 0);
            if (uses <= 1 || markerBoundary.contains(key))
                m_isBoundary[t * 3 + e] = true;
        }
    }
}

void SWMM2DMeshLayer::rebuildVertexAdjacency()
{
    const int nv = m_mesh.vertices.size();
    const int nt = m_mesh.triangles.size();

    QVector<int> counts(nv, 0);
    for (const auto &tri : m_mesh.triangles) {
        if (tri.v0 >= 0 && tri.v0 < nv) ++counts[tri.v0];
        if (tri.v1 >= 0 && tri.v1 < nv) ++counts[tri.v1];
        if (tri.v2 >= 0 && tri.v2 < nv) ++counts[tri.v2];
    }

    m_vertTriPtr.resize(nv + 1);
    m_vertTriPtr[0] = 0;
    for (int v = 0; v < nv; ++v)
        m_vertTriPtr[v + 1] = m_vertTriPtr[v] + counts[v];

    m_vertTriIdx.resize(m_vertTriPtr[nv]);
    QVector<int> cursor = m_vertTriPtr;  // copy; advance per-vertex insert cursor
    for (int t = 0; t < nt; ++t) {
        const auto &tri = m_mesh.triangles[t];
        if (tri.v0 >= 0 && tri.v0 < nv) m_vertTriIdx[cursor[tri.v0]++] = t;
        if (tri.v1 >= 0 && tri.v1 < nv) m_vertTriIdx[cursor[tri.v1]++] = t;
        if (tri.v2 >= 0 && tri.v2 < nv) m_vertTriIdx[cursor[tri.v2]++] = t;
    }
}

int SWMM2DMeshLayer::pickVertexAt(double sx, double sy,
                                   double tolPx, double pxPerSceneUnit) const
{
    if (m_sceneNodes.isEmpty() || pxPerSceneUnit <= 0.0) return -1;
    const double tolScene = tolPx / pxPerSceneUnit;
    const double tolSq = tolScene * tolScene;
    int best = -1;
    double bestSq = tolSq;
    for (int i = 0; i < m_sceneNodes.size(); ++i) {
        const double dx = m_sceneNodes[i].pt.x() - sx;
        const double dy = m_sceneNodes[i].pt.y() - sy;
        const double d2 = dx * dx + dy * dy;
        if (d2 <= bestSq) {
            bestSq = d2;
            best = i;
        }
    }
    return best;
}

int SWMM2DMeshLayer::pickEdgeAt(double sx, double sy,
                                 double tolPx, double pxPerSceneUnit,
                                 bool boundaryOnly) const
{
    if (m_sceneNodes.isEmpty() || pxPerSceneUnit <= 0.0) return -1;
    const double tolScene = tolPx / pxPerSceneUnit;
    const double tolSq = tolScene * tolScene;

    const int nt = m_mesh.triangles.size();
    int best = -1;
    double bestSq = tolSq;
    for (int t = 0; t < nt; ++t) {
        const auto &tri = m_mesh.triangles[t];
        if (tri.v0 < 0 || tri.v0 >= m_sceneNodes.size()) continue;
        if (tri.v1 < 0 || tri.v1 >= m_sceneNodes.size()) continue;
        if (tri.v2 < 0 || tri.v2 >= m_sceneNodes.size()) continue;

        const QPointF &p0 = m_sceneNodes[tri.v0].pt;
        const QPointF &p1 = m_sceneNodes[tri.v1].pt;
        const QPointF &p2 = m_sceneNodes[tri.v2].pt;
        // Edge local e is opposite vertex e (matches engine convention).
        const QPointF *endpoints[3][2] = {
            {&p1, &p2}, {&p2, &p0}, {&p0, &p1}
        };
        for (int e = 0; e < 3; ++e) {
            const int flat = t * 3 + e;
            if (boundaryOnly && (flat >= m_isBoundary.size() || !m_isBoundary[flat])) continue;
            const QPointF &a = *endpoints[e][0];
            const QPointF &b = *endpoints[e][1];
            const double dx = b.x() - a.x();
            const double dy = b.y() - a.y();
            const double lenSq = dx * dx + dy * dy;
            if (lenSq <= 0.0) continue;
            double u = ((sx - a.x()) * dx + (sy - a.y()) * dy) / lenSq;
            u = qBound(0.0, u, 1.0);
            const double px = a.x() + u * dx;
            const double py = a.y() + u * dy;
            const double ex = sx - px, ey = sy - py;
            const double d2 = ex * ex + ey * ey;
            if (d2 <= bestSq) {
                bestSq = d2;
                best = flat;
            }
        }
    }
    return best;
}

int SWMM2DMeshLayer::locateTriangleAt(double sx, double sy) const
{
    // Linear scan with bbox cull. Acceptable for the §V.VA cut; replace
    // with a uniform grid index in §V.VG if hover lag (R-V2).
    const int nt = m_sceneTris.size();
    for (int t = 0; t < nt; ++t) {
        const SceneTri &tri = m_sceneTris[t];
        const double minX = std::min({tri.a.x(), tri.b.x(), tri.c.x()});
        const double maxX = std::max({tri.a.x(), tri.b.x(), tri.c.x()});
        if (sx < minX || sx > maxX) continue;
        const double minY = std::min({tri.a.y(), tri.b.y(), tri.c.y()});
        const double maxY = std::max({tri.a.y(), tri.b.y(), tri.c.y()});
        if (sy < minY || sy > maxY) continue;

        // Barycentric in (sx,sy):
        const double v0x = tri.c.x() - tri.a.x(), v0y = tri.c.y() - tri.a.y();
        const double v1x = tri.b.x() - tri.a.x(), v1y = tri.b.y() - tri.a.y();
        const double v2x = sx - tri.a.x(),        v2y = sy - tri.a.y();
        const double d00 = v0x * v0x + v0y * v0y;
        const double d01 = v0x * v1x + v0y * v1y;
        const double d11 = v1x * v1x + v1y * v1y;
        const double d20 = v2x * v0x + v2y * v0y;
        const double d21 = v2x * v1x + v2y * v1y;
        const double denom = d00 * d11 - d01 * d01;
        if (denom == 0.0) continue;
        const double u = (d11 * d20 - d01 * d21) / denom;
        const double v = (d00 * d21 - d01 * d20) / denom;
        const double w = 1.0 - u - v;
        // Tolerate tiny negative on the boundary.
        if (u >= -1e-9 && v >= -1e-9 && w >= -1e-9) return t;
    }
    return -1;
}

int SWMM2DMeshLayer::pickCellAt(const QPointF &scenePt) const
{
    return locateTriangleAt(scenePt.x(), scenePt.y());
}

QVector<int> SWMM2DMeshLayer::pickCellsInRect(const QRectF &sceneRect) const
{
    QVector<int> hits;
    if (sceneRect.isNull() || m_sceneTris.isEmpty()) return hits;
    hits.reserve(m_sceneTris.size() / 4);
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        const SceneTri &t = m_sceneTris[i];
        const QPointF centroid((t.a.x() + t.b.x() + t.c.x()) / 3.0,
                               (t.a.y() + t.b.y() + t.c.y()) / 3.0);
        if (sceneRect.contains(centroid))
            hits.push_back(i);
    }
    return hits;
}

QVector<int> SWMM2DMeshLayer::pickCellsInPolygon(const QPolygonF &scenePoly) const
{
    QVector<int> hits;
    if (scenePoly.size() < 3 || m_sceneTris.isEmpty()) return hits;
    hits.reserve(m_sceneTris.size() / 4);
    for (int i = 0; i < m_sceneTris.size(); ++i) {
        const SceneTri &t = m_sceneTris[i];
        const QPointF centroid((t.a.x() + t.b.x() + t.c.x()) / 3.0,
                               (t.a.y() + t.b.y() + t.c.y()) / 3.0);
        if (scenePoly.containsPoint(centroid, Qt::OddEvenFill))
            hits.push_back(i);
    }
    return hits;
}

double SWMM2DMeshLayer::sampleZAt(double sx, double sy) const
{
    const int t = locateTriangleAt(sx, sy);
    if (t < 0) return std::numeric_limits<double>::quiet_NaN();
    const SceneTri &tri = m_sceneTris[t];
    // Recompute barycentric weights and blend Z.
    const double v0x = tri.c.x() - tri.a.x(), v0y = tri.c.y() - tri.a.y();
    const double v1x = tri.b.x() - tri.a.x(), v1y = tri.b.y() - tri.a.y();
    const double v2x = sx - tri.a.x(),        v2y = sy - tri.a.y();
    const double d00 = v0x * v0x + v0y * v0y;
    const double d01 = v0x * v1x + v0y * v1y;
    const double d11 = v1x * v1x + v1y * v1y;
    const double d20 = v2x * v0x + v2y * v0y;
    const double d21 = v2x * v1x + v2y * v1y;
    const double denom = d00 * d11 - d01 * d01;
    if (denom == 0.0) return std::numeric_limits<double>::quiet_NaN();
    const double u = (d11 * d20 - d01 * d21) / denom;
    const double v = (d00 * d21 - d01 * d20) / denom;
    const double w = 1.0 - u - v;
    // Weights: a→w, b→v, c→u (mirrors the v0/v1 construction above).
    return w * tri.z0 + v * tri.z1 + u * tri.z2;
}

bool SWMM2DMeshLayer::isBoundaryEdge(int triIdx, int edgeLocal) const
{
    if (triIdx < 0 || edgeLocal < 0 || edgeLocal > 2) return false;
    const int flat = triIdx * 3 + edgeLocal;
    if (flat < 0 || flat >= m_isBoundary.size()) return false;
    return m_isBoundary[flat];
}

bool SWMM2DMeshLayer::applyMeshVertexZ(int vertexIdx, double z)
{
    if (vertexIdx < 0 || vertexIdx >= m_mesh.vertices.size()) return false;
    if (m_mesh.vertices[vertexIdx].z == z) return true;
    m_mesh.vertices[vertexIdx].z = z;
    rebuildSceneGeometry();
    emit attributeChanged(mesh::MeshObjectRef::vertex(m_sourcePath, vertexIdx).name);
    emit repaintRequested();
    return true;
}

bool SWMM2DMeshLayer::applyMeshEdgeBC(int triIdx, int edgeLocal, const mesh::MeshEdgeBC &bc)
{
    if (triIdx < 0 || edgeLocal < 0 || edgeLocal > 2) return false;
    if (triIdx >= m_mesh.triangles.size()) return false;
    const int flat = triIdx * 3 + edgeLocal;
    if (flat >= m_bc.size()) return false;
    if (m_bc[flat] == bc) return true;
    m_bc[flat] = bc;
    emit attributeChanged(mesh::MeshObjectRef::edge(m_sourcePath, triIdx, edgeLocal).name);
    emit repaintRequested();
    return true;
}

QPair<int,int> SWMM2DMeshLayer::findEdgeNeighbour(int triIdx, int edgeLocal) const
{
    if (triIdx < 0 || edgeLocal < 0 || edgeLocal > 2) return {-1, -1};
    if (triIdx >= m_mesh.triangles.size())             return {-1, -1};
    const auto &tri = m_mesh.triangles[triIdx];
    // Local edge convention matches resizeBCsToMesh's edge-use scan:
    //   edge 0 = (v1, v2),  edge 1 = (v2, v0),  edge 2 = (v0, v1).
    int va = -1, vb = -1;
    switch (edgeLocal) {
    case 0: va = tri.v1; vb = tri.v2; break;
    case 1: va = tri.v2; vb = tri.v0; break;
    case 2: va = tri.v0; vb = tri.v1; break;
    }
    if (va < 0 || vb < 0 || va >= m_vertTriPtr.size() - 1) return {-1, -1};
    // Walk triangles incident to va; the neighbour must also be incident to vb.
    const int beg = m_vertTriPtr[va];
    const int end = m_vertTriPtr[va + 1];
    for (int k = beg; k < end; ++k) {
        const int t2 = m_vertTriIdx[k];
        if (t2 == triIdx || t2 < 0 || t2 >= m_mesh.triangles.size()) continue;
        const auto &t = m_mesh.triangles[t2];
        // Edge (va, vb) in t2 — direction doesn't matter.
        if      ((t.v1 == va && t.v2 == vb) || (t.v1 == vb && t.v2 == va)) return {t2, 0};
        else if ((t.v2 == va && t.v0 == vb) || (t.v2 == vb && t.v0 == va)) return {t2, 1};
        else if ((t.v0 == va && t.v1 == vb) || (t.v0 == vb && t.v1 == va)) return {t2, 2};
    }
    return {-1, -1};  // boundary edge — no neighbour
}

bool SWMM2DMeshLayer::applyMeshEdgeConveyance(int triIdx, int edgeLocal, double conveyance)
{
    if (triIdx < 0 || edgeLocal < 0 || edgeLocal > 2)        return false;
    if (triIdx >= m_mesh.triangles.size())                    return false;
    if (!(conveyance >= 0.0 && conveyance <= 1.0))            return false;
    const int flat = triIdx * 3 + edgeLocal;
    if (flat >= m_bc.size())                                  return false;

    bool changed = false;
    if (m_bc[flat].conveyance != conveyance) {
        m_bc[flat].conveyance = conveyance;
        changed = true;
    }
    // Mirror to the neighbour slot when this is an interior edge — keeps
    // the GUI in sync with the engine's symmetry invariant (the engine's
    // post-parse drain does the same thing).
    const auto nbr = findEdgeNeighbour(triIdx, edgeLocal);
    if (nbr.first >= 0 && nbr.second >= 0) {
        const int nflat = nbr.first * 3 + nbr.second;
        if (nflat < m_bc.size() && m_bc[nflat].conveyance != conveyance) {
            m_bc[nflat].conveyance = conveyance;
            changed = true;
        }
    }
    if (!changed) return true;
    emit attributeChanged(mesh::MeshObjectRef::edge(m_sourcePath, triIdx, edgeLocal).name);
    emit repaintRequested();
    return true;
}

void SWMM2DMeshLayer::setHighlightedVertices(const QSet<int> &indices)
{
    if (m_selVertices == indices) return;
    m_selVertices = indices;
    emit repaintRequested();
}

void SWMM2DMeshLayer::setHighlightedEdges(const QSet<int> &flatIndices)
{
    if (m_selEdges == flatIndices) return;
    m_selEdges = flatIndices;
    emit repaintRequested();
}

void SWMM2DMeshLayer::setHighlightedTriangles(const QSet<int> &indices)
{
    if (m_selTriangles == indices) return;
    m_selTriangles = indices;
    emit repaintRequested();
}

bool SWMM2DMeshLayer::applyMeshVertexTag(int vertexIdx, const QString &tag)
{
    if (vertexIdx < 0 || vertexIdx >= m_mesh.vertices.size()) return false;
    if (m_mesh.vertices[vertexIdx].tag == tag) return true;
    m_mesh.vertices[vertexIdx].tag = tag;
    // The scene node "tagged" flag follows this — repaint so the
    // SWMM-coupled-vertex glyph reflects the change.
    if (vertexIdx < m_sceneNodes.size())
        m_sceneNodes[vertexIdx].tagged = !tag.isEmpty();
    emit attributeChanged(mesh::MeshObjectRef::vertex(m_sourcePath, vertexIdx).name);
    emit repaintRequested();
    return true;
}

// ---------------------------------------------------------------------------
// Layer-level Q_PROPERTYs are now thin shims onto sublayer state. The
// sublayer's setVisible() / style->setX() emits invalidated(), which the
// ctor wired to repaintRequested(), so external observers continue to
// see exactly one repaint per logical edit.
// ---------------------------------------------------------------------------

bool SWMM2DMeshLayer::showMeshNodes() const
{
    return m_meshNodeSublayer && m_meshNodeSublayer->isVisible();
}

void SWMM2DMeshLayer::setShowMeshNodes(bool show)
{
    if (m_meshNodeSublayer) m_meshNodeSublayer->setVisible(show);
}

bool SWMM2DMeshLayer::showEdges() const
{
    return m_meshEdgeSublayer && m_meshEdgeSublayer->isVisible();
}

void SWMM2DMeshLayer::setShowEdges(bool show)
{
    if (m_meshEdgeSublayer) m_meshEdgeSublayer->setVisible(show);
}

void SWMM2DMeshLayer::setHillshadeAzimuth(double degrees)
{
    // Wrap into [0, 360) so the UI can spin past the boundary cleanly.
    while (degrees <  0.0) degrees += 360.0;
    while (degrees >= 360.0) degrees -= 360.0;
    if (qFuzzyCompare(m_hillshadeAz, degrees)) return;
    m_hillshadeAz = degrees;
    emit repaintRequested();
}

void SWMM2DMeshLayer::setHillshadeAltitude(double degrees)
{
    degrees = qBound(0.0, degrees, 90.0);
    if (qFuzzyCompare(m_hillshadeAlt, degrees)) return;
    m_hillshadeAlt = degrees;
    emit repaintRequested();
}

// Slice U-? — hillshadeZExag and hillshadeMinLit are stored as a single
// hillshadeStrength on the mesh-fill sublayer style. We map the existing
// (zExag, minLit) API onto two virtual axes of that knob so the existing
// dialog and saved JSON continue to work without exposing two near-
// duplicate sliders. zExag drives visible relief, minLit clamps the
// shadow floor; we round-trip through the style by storing zExag in
// hillshadeStrength (0..1 := zExag/10) and keeping minLit in a local
// shadow field that the renderer reads back from the layer. The shim
// keeps the existing dialog editable while we phase in a richer style.
namespace { thread_local double g_meshHillshadeMinLit = 0.15; }

double SWMM2DMeshLayer::hillshadeZExag() const
{
    if (m_meshFillSublayer && m_meshFillSublayer->fillStyle())
        return m_meshFillSublayer->fillStyle()->hillshadeStrength() * 10.0;
    return 3.0;
}

double SWMM2DMeshLayer::hillshadeMinLit() const
{
    return g_meshHillshadeMinLit;
}

void SWMM2DMeshLayer::setHillshadeZExag(double factor)
{
    factor = qBound(0.0, factor, 100.0);
    if (m_meshFillSublayer && m_meshFillSublayer->fillStyle())
        m_meshFillSublayer->fillStyle()->setHillshadeStrength(factor / 10.0);
    else
        emit repaintRequested();
}

void SWMM2DMeshLayer::setHillshadeMinLit(double minLit)
{
    minLit = qBound(0.0, minLit, 1.0);
    if (qFuzzyCompare(g_meshHillshadeMinLit, minLit)) return;
    g_meshHillshadeMinLit = minLit;
    emit repaintRequested();
}

// Bed-elevation contour setters — forward to the isoline sublayer.
bool SWMM2DMeshLayer::showContours() const
{
    return m_isolineSublayer && m_isolineSublayer->isVisible();
}

int SWMM2DMeshLayer::contourIntervalCount() const
{
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        return m_isolineSublayer->isolineStyle()->isoValueCount();
    return 10;
}

QColor SWMM2DMeshLayer::contourColor() const
{
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        return m_isolineSublayer->isolineStyle()->color();
    return QColor(0x1a, 0x1a, 0x1a, 200);
}

double SWMM2DMeshLayer::contourLineWidth() const
{
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        return m_isolineSublayer->isolineStyle()->lineWidthPx();
    return 1.0;
}

void SWMM2DMeshLayer::setShowContours(bool show)
{
    if (m_isolineSublayer) m_isolineSublayer->setVisible(show);
}

void SWMM2DMeshLayer::setContourIntervalCount(int n)
{
    n = qBound(1, n, 200);
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        m_isolineSublayer->isolineStyle()->setIsoValueCount(n);
    if (m_contourBandSublayer && m_contourBandSublayer->bandStyle())
        m_contourBandSublayer->bandStyle()->setBandCount(n);
}

void SWMM2DMeshLayer::setContourColor(const QColor &c)
{
    if (!c.isValid()) return;
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        m_isolineSublayer->isolineStyle()->setColor(c);
}

void SWMM2DMeshLayer::setContourLineWidth(double widthPx)
{
    widthPx = qBound(0.25, widthPx, 10.0);
    if (m_isolineSublayer && m_isolineSublayer->isolineStyle())
        m_isolineSublayer->isolineStyle()->setLineWidthPx(widthPx);
}

// Filled iso-bands — forward to the contour-band sublayer.
bool SWMM2DMeshLayer::filledContours() const
{
    return m_contourBandSublayer && m_contourBandSublayer->isVisible();
}

double SWMM2DMeshLayer::filledContoursOpacity() const
{
    return m_contourBandSublayer ? m_contourBandSublayer->opacity() : 0.55;
}

void SWMM2DMeshLayer::setFilledContours(bool filled)
{
    if (m_contourBandSublayer) m_contourBandSublayer->setVisible(filled);
}

void SWMM2DMeshLayer::setFilledContoursOpacity(double a)
{
    a = qBound(0.0, a, 1.0);
    if (m_contourBandSublayer) m_contourBandSublayer->setOpacity(a);
}

QList<OpenSWMM::Render::ISublayer *> SWMM2DMeshLayer::sublayers() const
{
    // Paint order is list order (bottom-up):
    //   fill (static)         → terrain base
    //   contour bands         → filled isobands above hillshade
    //   edges                 → mesh wireframe
    //   isolines              → labelled contour lines
    //   vertex markers (top)  → coupled-node glyphs
    // Slice GUI-2026-05-30 §2 — order is user-customisable and cached in
    // m_sublayerOrder; seeded once from the defaults above.
    if (m_sublayerOrder.isEmpty()) {
        OpenSWMM::Render::ISublayer *defaults[] = {
            m_meshFillSublayer,
            m_contourBandSublayer,
            m_meshEdgeSublayer,
            m_isolineSublayer,
            m_meshNodeSublayer,
        };
        for (auto *s : defaults)
            if (s) m_sublayerOrder.append(s);
    }
    return m_sublayerOrder;
}

bool SWMM2DMeshLayer::moveSublayer(int from, int to)
{
    (void) sublayers();      // force lazy seed
    if (from < 0 || from >= m_sublayerOrder.size()
        || to   < 0 || to   >= m_sublayerOrder.size()
        || from == to)
        return false;
    m_sublayerOrder.move(from, to);
    emit repaintRequested();
    return true;
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

// ---------------------------------------------------------------------------
// Mesh layer's own Q_PROPERTYs form the first subject (the legacy
// Display/Hillshade/Contours tabs). Each individually-stylable sublayer
// is added as a separate subject so the dialog surfaces its full style
// bag (colours, marker sizes, dash patterns, label toggles, ...) rather
// than just the layer-level boolean shims.
// ---------------------------------------------------------------------------

// ─── Slice B.5b — Rule Model mirror over mesh decoration state ─────────

OpenSWMM::Render::RuleList *SWMM2DMeshLayer::ruleList()
{
    if (!m_ruleList)
        buildRuleListLazy();
    return m_ruleList.get();
}

const OpenSWMM::Render::RuleList *SWMM2DMeshLayer::ruleList() const
{
    if (!m_ruleList)
        buildRuleListLazy();
    return m_ruleList.get();
}

void SWMM2DMeshLayer::buildRuleListLazy() const
{
    using namespace OpenSWMM::Render;

    auto *self = const_cast<SWMM2DMeshLayer *>(this);
    m_ruleList = std::make_unique<RuleList>(self);

    // Seed one Rule per archetype the mesh layer paints. Each Rule
    // wraps a SingleSymbolRenderer whose SymbolStyle layers carry the
    // Z.6 typed spec props. The QSG renderer (Slice Z.6a step 2) reads
    // these as specs from the legacy sublayer style fields each frame;
    // the connect handlers below (Slice Z.6a step 3) translate Rule
    // edits back into those legacy fields so dialog changes drive paint.
    //
    // Six seeds — "Contour bands" and "Contour lines" are separate Rules
    // because the QSG renderer paints them as two passes (filled iso-
    // bands + iso-line strokes), each driven by its own ContourSpec.
    struct Seed { const char *name; SymbolLayer (*build)(); };
    const Seed seeds[] = {
        {"Mesh fill",
            [] { return RasterColorRampSymbolLayerSpec{}.toSymbolLayer(); }},
        {"Hillshade",
            [] { return HillshadeSymbolLayerSpec{}.toSymbolLayer(); }},
        {"Contour bands",
            [] {
                ContourSymbolLayerSpec s;
                s.mode = ContourMode::Filled;
                return s.toSymbolLayer();
            }},
        {"Contour lines",
            [] {
                ContourSymbolLayerSpec s;
                s.mode = ContourMode::Lines;
                return s.toSymbolLayer();
            }},
        {"Mesh edges",
            [] { return MeshEdgeSymbolLayerSpec{}.toSymbolLayer(); }},
        {"Mesh nodes",
            [] { return MeshNodeSymbolLayerSpec{}.toSymbolLayer(); }},
    };

    QVector<Rule *> ruleHandles;
    ruleHandles.reserve(int(sizeof(seeds) / sizeof(*seeds)));
    for (const Seed &s : seeds) {
        auto single = std::make_unique<SingleSymbolRenderer>();
        SymbolStyle style = single->symbol();
        style.layers.clear();
        style.layers.append(s.build());
        single->setSymbol(style);
        Rule *r = m_ruleList->append(std::make_unique<Rule>(
            QString::fromLatin1(s.name), std::move(single)));
        ruleHandles.append(r);
    }

    // ── Slice Z.6a step 3 — Rule → legacy-style propagation ─────────────
    //
    // When the user edits a Rule's SymbolStyle props through the dialog
    // (SymbolStyleAdapter → notifyRendererStateChanged), rendererReplaced
    // fires. Each handler below extracts the typed spec from the Rule's
    // first SymbolLayer and writes the relevant fields back onto the
    // matching legacy sublayer style (or onto the mesh layer's hillshade
    // members). Setting those fields emits invalidated() on the SublayerStyle,
    // which the ctor's wire() lambda routes to repaintRequested(); the
    // SWMM2DMeshQSGRenderer then re-samples those legacy fields into its
    // per-frame specs on the next rebuild.
    //
    // Renderer swaps (graduated / categorised) silently no-op: the typed
    // mesh-decoration specs don't apply to those classes, and the paint
    // pipeline continues reading the current legacy field values.
    auto firstLayer = [](Rule *r) -> const SymbolLayer * {
        if (!r) return nullptr;
        auto *single = dynamic_cast<const SingleSymbolRenderer *>(r->renderer());
        if (!single) return nullptr;
        if (single->symbol().layers.isEmpty()) return nullptr;
        return &single->symbol().layers.first();
    };

    // ── Mesh fill (RasterColorRampSpec) ────────────────────────────────
    QObject::connect(ruleHandles[0], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[0]);
            if (!layer || layer->kind != SymbolLayerKind::RasterColorRamp) return;
            const auto spec = RasterColorRampSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_meshFillSublayer) {
                sub->setOpacity(spec.opacity);
                if (auto *st = sub->fillStyle())
                    st->setFillColor(spec.noDataColor);
            }
        });

    // ── Hillshade (HillshadeSpec) — drives both the mesh-layer hillshade
    //    transform (azimuth / altitude / zexag / shadow floor) and the
    //    fill-style's hillshade-strength multiplier. ─────────────────────
    QObject::connect(ruleHandles[1], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[1]);
            if (!layer || layer->kind != SymbolLayerKind::Hillshade) return;
            const auto spec = HillshadeSymbolLayerSpec::fromSymbolLayer(*layer);
            self->setHillshadeAzimuth(spec.azimuthDeg);
            self->setHillshadeAltitude(spec.altitudeDeg);
            self->setHillshadeZExag(spec.zExaggeration);
            self->setHillshadeMinLit(spec.shadowFloor);
            if (auto *fill = self->m_meshFillSublayer; fill && fill->fillStyle())
                fill->fillStyle()->setHillshadeStrength(spec.strength);
        });

    // ── Contour bands (ContourSpec mode=Filled) ─────────────────────────
    QObject::connect(ruleHandles[2], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[2]);
            if (!layer || layer->kind != SymbolLayerKind::Contour) return;
            const auto spec = ContourSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_contourBandSublayer; sub && sub->bandStyle()) {
                auto *st = sub->bandStyle();
                st->setBandCount(spec.binner.binCount());
                st->setSmoothBands(spec.smoothBands);
                if (!spec.ramp.stops.isEmpty()) {
                    st->setLowColor(spec.ramp.stops.first().second);
                    st->setHighColor(spec.ramp.stops.last().second);
                }
            }
        });

    // ── Contour lines (ContourSpec mode=Lines) ──────────────────────────
    QObject::connect(ruleHandles[3], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[3]);
            if (!layer || layer->kind != SymbolLayerKind::Contour) return;
            const auto spec = ContourSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_isolineSublayer; sub && sub->isolineStyle()) {
                auto *st = sub->isolineStyle();
                st->setIsoValueCount(spec.binner.binCount());
                st->setColor(spec.lineColor);
                st->setLineWidthPx(spec.lineWidthPx);
                st->setLabels(spec.labelEveryN > 0);
            }
        });

    // ── Mesh edges (MeshEdgeSpec) ───────────────────────────────────────
    QObject::connect(ruleHandles[4], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[4]);
            if (!layer || layer->kind != SymbolLayerKind::MeshEdge) return;
            const auto spec = MeshEdgeSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_meshEdgeSublayer; sub && sub->edgeStyle()) {
                auto *st = sub->edgeStyle();
                st->setColor(spec.color);
                st->setLineWidthPx(spec.width);
                st->setDashPattern(spec.penStyle);
                st->setUseSlopeDrivenWidth(spec.useSlopeDrivenWidth);
                st->setSlopeBreak(spec.slopeBreak);
                st->setWideWidthPx(spec.wideWidthPx);
                st->setWideColor(spec.wideColor);
            }
        });

    // ── Mesh nodes (MeshNodeSpec wraps MarkerSpec) ──────────────────────
    //
    // MeshNodeStyle::MarkerShape only models 4 shapes (Circle / Square /
    // Triangle / Diamond — integer values 0..3 match Render::MarkerShape).
    // Richer Z.4 shapes round-trip through the SymbolLayer props but fall
    // back to Circle on the legacy sublayer; a future mesh-node renderer
    // that consumes the spec directly will pick up the richer value.
    QObject::connect(ruleHandles[5], &Rule::rendererReplaced, self,
        [self, ruleHandles, firstLayer]() {
            const SymbolLayer *layer = firstLayer(ruleHandles[5]);
            if (!layer || layer->kind != SymbolLayerKind::MeshNode) return;
            const auto spec = MeshNodeSymbolLayerSpec::fromSymbolLayer(*layer);
            if (auto *sub = self->m_meshNodeSublayer; sub && sub->nodeStyle()) {
                auto *st = sub->nodeStyle();
                st->setColor(spec.marker.fillColor);
                st->setMarkerSizePx(spec.marker.sizePx);
                st->setOutlineColor(spec.marker.outlineColor);
                st->setOutlineWidthPx(spec.marker.outlineWidth);
                int shapeIdx = static_cast<int>(spec.marker.shape);
                if (shapeIdx < 0 || shapeIdx > 3) shapeIdx = 0;
                st->setShape(static_cast<MeshNodeStyle::MarkerShape>(shapeIdx));
            }
        });
}

std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
SWMM2DMeshLayer::styleSubjects()
{
    using openswmmvis::ui::ILayerStyleSubject;
    using openswmmvis::ui::LayerStyleSubject;
    std::vector<std::unique_ptr<ILayerStyleSubject>> out;

    out.push_back(std::make_unique<LayerStyleSubject>(
        tr("Mesh / TIN"), this,
        QStringLiteral("mesh.layer"),
        QString()));

    const QString sect = tr("Sublayers");
    auto add = [&](OpenSWMM::Render::ISublayer *sub) {
        if (!sub || !sub->style()) return;
        out.push_back(std::make_unique<LayerStyleSubject>(
            sub->displayName(), sub->style(), sub->id(), sect));
    };
    add(m_meshFillSublayer);
    add(m_meshEdgeSublayer);
    add(m_meshNodeSublayer);
    add(m_contourBandSublayer);
    add(m_isolineSublayer);

    return out;
}
