/*!
 * \file   swmmlayerqsgrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 *
 * Phase B.RHI of docs/RENDERING_5M_PLAN.md — Qt Quick Scene Graph
 * renderer for SWMMModelLayer. Renders ALL vector layers (lines,
 * subcatchments, node glyphs, gages) via QSGGeometryNode, with
 * selection coloring per class. Native Metal / Vulkan / D3D11 via
 * QRhi underneath; no QPainter, no GL paint engine quirks.
 */
#include "map/swmmlayerqsgrenderer.h"

#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "render/markershape.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------
// Lightweight frame-time sampler — active only when SWMMVIS_RENDER_PERF is set.
// ---------------------------------------------------------------------------
namespace {

constexpr int kReportInterval = 60;

struct PerfSampler {
    bool   enabled    = false;
    qint64 geomTotal  = 0;
    int    geomCount  = 0;
    qint64 panTotal   = 0;
    int    panCount   = 0;
    int    frameCount = 0;

    void init() { enabled = qEnvironmentVariableIsSet("SWMMVIS_RENDER_PERF"); }

    void record(qint64 ms, bool wasRebuild) {
        if (!enabled) return;
        if (wasRebuild) { geomTotal += ms; ++geomCount; }
        else            { panTotal  += ms; ++panCount;  }
        if (++frameCount >= kReportInterval) report();
    }

    void report() {
        const double ga = geomCount ? double(geomTotal)/geomCount : 0.0;
        const double pa = panCount  ? double(panTotal) /panCount  : 0.0;
        qDebug().noquote()
            << "[SWMMVis render]"
            << "rebuild_frames=" << geomCount
            << QString("avg_rebuild_ms=%1").arg(ga, 0,'f',1)
            << "pan_frames="     << panCount
            << QString("avg_pan_ms=%1").arg(pa, 0,'f',2);
        geomTotal = panTotal = 0;
        geomCount = panCount = frameCount = 0;
    }
};

PerfSampler &sampler() { static PerfSampler s; return s; }

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

// Simple ear-clipping triangulator for SWMM subcatchment polygons (O(n²)).
QVector<int> earcutTriangulate(const QVector<QPointF> &poly)
{
    QVector<int> tris;
    const int n = poly.size();
    if (n < 3) return tris;

    auto cross = [](QPointF a, QPointF b, QPointF c) -> double {
        return (b.x()-a.x())*(c.y()-a.y()) - (b.y()-a.y())*(c.x()-a.x());
    };

    double signedArea = 0.0;
    for (int i = 0; i < n; ++i) {
        const QPointF &p0 = poly[i], &p1 = poly[(i+1)%n];
        signedArea += (p1.x()-p0.x())*(p1.y()+p0.y());
    }
    const bool reverseToCcw = signedArea > 0.0;

    QVector<int> indices;
    indices.reserve(n);
    if (reverseToCcw)
        for (int i = n-1; i >= 0; --i) indices.append(i);
    else
        for (int i = 0;   i <  n; ++i) indices.append(i);

    auto pointInTri = [&](QPointF p, QPointF a, QPointF b, QPointF c) {
        const double d1 = cross(a,b,p), d2 = cross(b,c,p), d3 = cross(c,a,p);
        return !((d1<0||d2<0||d3<0) && (d1>0||d2>0||d3>0));
    };

    int safety = n*n;
    while (indices.size() > 2 && safety-- > 0) {
        bool earFound = false;
        for (int idx = 0; idx < indices.size(); ++idx) {
            const int prev = (idx-1+indices.size())%indices.size();
            const int next = (idx+1)%indices.size();
            const int iP = indices[prev], iC = indices[idx], iN = indices[next];
            if (cross(poly[iP],poly[iC],poly[iN]) <= 0.0) continue;
            bool ok = true;
            for (int k = 0; k < indices.size() && ok; ++k) {
                if (k==idx||k==prev||k==next) continue;
                if (pointInTri(poly[indices[k]],poly[iP],poly[iC],poly[iN])) ok=false;
            }
            if (!ok) continue;
            tris.append(iP); tris.append(iC); tris.append(iN);
            indices.removeAt(idx);
            earFound = true;
            break;
        }
        if (!earFound) break;
    }
    return tris;
}

// Shape-agnostic glyph emitter.
//
// Emits a triangle fan (or triangle pair / quad) that fills the requested
// MarkerShape inside a (2 r) bounding box centred at (sx, sy). The
// pixel-fan approach keeps geometry uniform across shapes and avoids
// per-shape vertex-attribute juggling.
//
// VertexT is either QSGGeometry::Point2D (no colour) or
// QSGGeometry::ColoredPoint2D (per-vertex colour). The MakeVert lambda
// builds the right vertex type for the caller.
//
// Cross-cap arms (Plus, Cross, XCross) are emitted as two thin
// rectangles. Star is a 10-vertex two-triangle-fan (outer + inner
// vertices) — visually convincing for ~6 px markers without a custom
// shader. Pentagon/Hexagon/Arrow/HalfCircle are emitted as triangle fans
// from the centre.
//
// Circle segment count scales with the requested radius so small
// markers stay cheap and big markers stay round. A 24-segment fan at
// the default 8 px radius is indistinguishable from a smooth disk at
// typical zoom levels with MSAA active.
template <typename VertexT, typename MakeVert>
void appendMarkerShapeImpl(std::vector<VertexT> &out,
                           float sx, float sy, float r,
                           OpenSWMM::Render::MarkerShape shape,
                           MakeVert &&v)
{
    using Shape = OpenSWMM::Render::MarkerShape;
    constexpr float kTau = 6.28318530718f;

    auto fan = [&](int segments) {
        const float k = kTau / float(segments);
        for (int s = 0; s < segments; ++s) {
            const float a0 = k * float(s);
            const float a1 = k * float(s + 1);
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r * std::cos(a0), sy + r * std::sin(a0)));
            out.push_back(v(sx + r * std::cos(a1), sy + r * std::sin(a1)));
        }
    };

    auto quad = [&](float x0, float y0, float x1, float y1,
                    float x2, float y2, float x3, float y3) {
        out.push_back(v(x0, y0)); out.push_back(v(x1, y1)); out.push_back(v(x2, y2));
        out.push_back(v(x0, y0)); out.push_back(v(x2, y2)); out.push_back(v(x3, y3));
    };

    switch (shape) {
    case Shape::Circle: {
        // Fixed 16-segment fan. A previous attempt scaled segments by
        // `r` — but `r` is in scene units (after invView scaling), so
        // at full extent on large models it would clamp to 48 segments
        // per glyph, pushing the junction vertex buffer past 6 M verts
        // on West-Whiteland-scale models. That hit a per-buffer limit
        // on the OpenGL RHI backend and showed up as stray lines /
        // triangles between distant points (stale buffer tail being
        // rendered as DrawTriangles). 16 segments is plenty for any
        // reasonable on-screen size and bounds the per-glyph vertex
        // cost at 48 vertices — total fits in safe-range regardless of
        // node count.
        constexpr int segments = 16;
        fan(segments);
        break;
    }
    case Shape::Square:
        quad(sx - r, sy - r, sx + r, sy - r, sx + r, sy + r, sx - r, sy + r);
        break;
    case Shape::Triangle:
        // Canonical: right-pointing isoceles.
        out.push_back(v(sx + r, sy));
        out.push_back(v(sx - r, sy + r));
        out.push_back(v(sx - r, sy - r));
        break;
    case Shape::Diamond:
        out.push_back(v(sx, sy - r)); out.push_back(v(sx + r, sy)); out.push_back(v(sx, sy + r));
        out.push_back(v(sx, sy - r)); out.push_back(v(sx, sy + r)); out.push_back(v(sx - r, sy));
        break;
    case Shape::Star: {
        // Five-pointed star: triangle fan from centre to alternating
        // outer / inner vertices around the circle.
        constexpr int kPoints = 5;
        const float inner = r * 0.382f; // golden-ratio-ish inset
        const float aStart = -kTau * 0.25f; // tip up
        for (int s = 0; s < kPoints * 2; ++s) {
            const float a0 = aStart + kTau * float(s)     / float(kPoints * 2);
            const float a1 = aStart + kTau * float(s + 1) / float(kPoints * 2);
            const float r0 = (s % 2 == 0) ? r : inner;
            const float r1 = (s % 2 == 0) ? inner : r;
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r0 * std::cos(a0), sy + r0 * std::sin(a0)));
            out.push_back(v(sx + r1 * std::cos(a1), sy + r1 * std::sin(a1)));
        }
        break;
    }
    case Shape::Cross:
    case Shape::Plus: {
        const float w = (shape == Shape::Plus) ? r * 0.45f : r * 0.25f;
        quad(sx - r, sy - w, sx + r, sy - w, sx + r, sy + w, sx - r, sy + w);
        quad(sx - w, sy - r, sx + w, sy - r, sx + w, sy + r, sx - w, sy + r);
        break;
    }
    case Shape::XCross: {
        // Two rotated bars (× shape). Half-width and half-length of
        // each bar in local coords, then rotated into world coords.
        const float w = r * 0.25f;
        const float c = 0.70710678f; // cos/sin 45°
        const float ax = r * c;      // bar length × cos45
        const float wx = w * c;      // bar half-width × cos45
        // Bar 1: along (+1,+1) — long axis tilted up-right.
        quad(sx + (-ax + wx), sy + (-ax - wx),
             sx + ( ax + wx), sy + ( ax - wx),
             sx + ( ax - wx), sy + ( ax + wx),
             sx + (-ax - wx), sy + (-ax + wx));
        // Bar 2: along (+1,-1) — long axis tilted up-left.
        quad(sx + (-ax - wx), sy + ( ax - wx),
             sx + ( ax - wx), sy + (-ax - wx),
             sx + ( ax + wx), sy + (-ax + wx),
             sx + (-ax + wx), sy + ( ax + wx));
        break;
    }
    case Shape::Pentagon: {
        constexpr int n = 5;
        const float aStart = -kTau * 0.25f;
        for (int s = 0; s < n; ++s) {
            const float a0 = aStart + kTau * float(s)     / float(n);
            const float a1 = aStart + kTau * float(s + 1) / float(n);
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r * std::cos(a0), sy + r * std::sin(a0)));
            out.push_back(v(sx + r * std::cos(a1), sy + r * std::sin(a1)));
        }
        break;
    }
    case Shape::Hexagon: {
        constexpr int n = 6;
        for (int s = 0; s < n; ++s) {
            const float a0 = kTau * float(s)     / float(n);
            const float a1 = kTau * float(s + 1) / float(n);
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r * std::cos(a0), sy + r * std::sin(a0)));
            out.push_back(v(sx + r * std::cos(a1), sy + r * std::sin(a1)));
        }
        break;
    }
    case Shape::Arrow:
        // Right-pointing arrow head — single triangle.
        out.push_back(v(sx + r, sy));
        out.push_back(v(sx - r, sy + r * 0.7f));
        out.push_back(v(sx - r, sy - r * 0.7f));
        break;
    case Shape::EquilateralTriangle: {
        // Up-pointing: apex at top.
        const float h = r * 1.1547f; // 2/sqrt(3) so flat-base is r-aligned
        out.push_back(v(sx, sy - h));
        out.push_back(v(sx + r, sy + h * 0.5f));
        out.push_back(v(sx - r, sy + h * 0.5f));
        break;
    }
    case Shape::HalfCircle: {
        // Top half: arc from -π through 0. Fixed segment count for the
        // same reason as Circle above — scene-unit `r` is not a valid
        // scale here.
        constexpr int segments = 12;
        for (int s = 0; s < segments; ++s) {
            const float a0 = float(M_PI) + kTau * 0.5f * float(s)     / float(segments);
            const float a1 = float(M_PI) + kTau * 0.5f * float(s + 1) / float(segments);
            out.push_back(v(sx, sy));
            out.push_back(v(sx + r * std::cos(a0), sy + r * std::sin(a0)));
            out.push_back(v(sx + r * std::cos(a1), sy + r * std::sin(a1)));
        }
        break;
    }
    }
}

void appendNodeGlyphTriangles(std::vector<QSGGeometry::Point2D> &out,
                              float sx, float sy, float r,
                              OpenSWMM::Render::MarkerShape shape)
{
    auto v = [](float x, float y) {
        QSGGeometry::Point2D p; p.x = x; p.y = y; return p;
    };
    appendMarkerShapeImpl(out, sx, sy, r, shape, v);
}

void appendGageTriangles(std::vector<QSGGeometry::Point2D> &out,
                         float sx, float sy, float r,
                         OpenSWMM::Render::MarkerShape shape =
                             OpenSWMM::Render::MarkerShape::Diamond)
{
    appendNodeGlyphTriangles(out, sx, sy, r, shape);
}

void appendThickSegment(std::vector<QSGGeometry::Point2D> &out,
                        float ax, float ay, float bx, float by, float hw)
{
    const float dx=bx-ax, dy=by-ay, len=std::sqrt(dx*dx+dy*dy);
    if (len < 1e-9f) return;
    const float nx=-dy/len*hw, ny=dx/len*hw;
    auto v=[](float x,float y){QSGGeometry::Point2D p;p.x=x;p.y=y;return p;};
    out.push_back(v(ax+nx,ay+ny)); out.push_back(v(bx+nx,by+ny)); out.push_back(v(ax-nx,ay-ny));
    out.push_back(v(bx+nx,by+ny)); out.push_back(v(bx-nx,by-ny)); out.push_back(v(ax-nx,ay-ny));
}

QSGGeometryNode *makeFlatColorNode(QSGGeometry::DrawingMode mode, QColor color, float lw=1.0f)
{
    auto *node = new QSGGeometryNode();
    auto *geo  = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geo->setDrawingMode(mode);
    if (mode==QSGGeometry::DrawLines||mode==QSGGeometry::DrawLineStrip) geo->setLineWidth(lw);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *mat = new QSGFlatColorMaterial();
    mat->setColor(color);
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

// §QSG-3 — per-vertex coloured node (mirrors swmm2dmeshqsgrenderer.cpp's
// makeColoredNode). Used for nodesSel so each glyph carries its own
// colour (selection brush, or per-feature override) without needing a
// separate QSGGeometryNode per colour.
QSGGeometryNode *makeColoredNode(QSGGeometry::DrawingMode mode)
{
    auto *node = new QSGGeometryNode();
    auto *geo  = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geo->setDrawingMode(mode);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *mat = new QSGVertexColorMaterial();
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

// Coloured variant of appendNodeGlyphTriangles — bakes the colour into
// every emitted vertex so the QSGVertexColorMaterial pipeline picks it
// up. Shape selection is identical to the flat-colour version so
// positions stay consistent between the two paths.
void appendNodeGlyphTrianglesColored(std::vector<QSGGeometry::ColoredPoint2D> &out,
                                     float sx, float sy, float r,
                                     OpenSWMM::Render::MarkerShape shape,
                                     uchar cr, uchar cg, uchar cb, uchar ca)
{
    auto v = [&](float x, float y) {
        QSGGeometry::ColoredPoint2D p;
        p.x = x; p.y = y;
        p.r = cr; p.g = cg; p.b = cb; p.a = ca;
        return p;
    };
    appendMarkerShapeImpl(out, sx, sy, r, shape, v);
}

void uploadVerts(QSGGeometryNode *node, const std::vector<QSGGeometry::Point2D> &verts)
{
    auto *geo = node->geometry();
    const int n = int(verts.size());
    if (geo->vertexCount() != n) geo->allocate(n);
    if (n > 0) std::memcpy(geo->vertexData(), verts.data(), n*sizeof(QSGGeometry::Point2D));
    node->markDirty(QSGNode::DirtyGeometry);
}

void setNodeColor(QSGGeometryNode *node, QColor color)
{
    auto *mat = static_cast<QSGFlatColorMaterial*>(node->material());
    if (mat->color() != color) { mat->setColor(color); node->markDirty(QSGNode::DirtyMaterial); }
}

void setLineWidth(QSGGeometryNode *node, float w)
{
    auto *geo = node->geometry();
    if (geo->lineWidth() != w) { geo->setLineWidth(w); node->markDirty(QSGNode::DirtyGeometry); }
}

// Selection helpers. The QSG renderer drives QSGFlatColorMaterial nodes,
// which take a single QColor — so we surface pen colour for outlines /
// line nodes and brush colour for fills. Width/cap/join from the
// selection pen aren't representable here (no per-vertex line width on
// QSGGeometry::DrawTriangles).
QColor selPenColor(const char *key)
{
    auto *p = PreferencesManager::instance();
    return p ? p->selectionPen(QString::fromLatin1(key)).color()
             : QColor(255, 255, 0);
}
QColor selBrushColor(const char *key)
{
    auto *p = PreferencesManager::instance();
    return p ? p->selectionBrush(QString::fromLatin1(key)).color()
             : QColor(255, 255, 0);
}

} // namespace

// ---------------------------------------------------------------------------

SWMMLayerQSGRenderer::SWMMLayerQSGRenderer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    sampler().init();
}

SWMMLayerQSGRenderer::~SWMMLayerQSGRenderer() = default;

void SWMMLayerQSGRenderer::setLayer(SWMMModelLayer *layer)
{
    if (m_layer == layer) return;
    if (m_layer) QObject::disconnect(m_layer, nullptr, this, nullptr);
    m_layer = layer;
    if (m_layer) {
        // Selection change: only the 5 overlay buffers need rebuilding.
        // Set m_selectionPending so the repaintRequested that always follows
        // selectionChanged is absorbed without triggering a full rebuild.
        connect(m_layer, &SWMMModelLayer::selectionChanged,
                this, [this](const QStringList &) {
                    // §QSG-3 — selection updates need a full rebuild
                    // because selection colour is baked per-vertex into
                    // the base buckets. The selection-only branch in
                    // updatePaintNode is therefore unused for nodes.
                    m_contentDirty     = true;
                    m_selDirty         = true;
                    m_selectionPending = true;
                    update();
                });
        // All other model changes (geometry, symbology, visibility): full rebuild.
        connect(m_layer, &SWMMModelLayer::repaintRequested,
                this, [this]() {
                    if (m_selectionPending) {
                        m_selectionPending = false;
                        return;
                    }
                    m_contentDirty = true;
                    update();
                });
    }
    m_catchTriCache.revision = std::numeric_limits<quint64>::max();
    m_selDirty        = false;
    m_selectionPending = false;
    m_contentDirty    = true;
    update();
}

void SWMMLayerQSGRenderer::setMapExtent(const MapExtent &extent)
{
    if (extent == m_extent) return;
    const bool zoomChanged =
        !qFuzzyCompare(extent.width(),  m_extent.width()) ||
        !qFuzzyCompare(extent.height(), m_extent.height());
    m_extent = extent;
    if (zoomChanged) {
        // Zoom changes invalidate the cull bounds AND the precision
        // anchor — full content rebuild required.
        m_contentDirty = true;
    } else if (m_lastBuiltExtent.isValid()) {
        // Pan-only path: only re-cull/upload vertices when the new
        // viewport drifts more than half the width/height of the
        // viewport the cached vertices were built against. Up to that
        // point the wider cullMargin guarantees the cached vertex set
        // already covers what's on screen, so we just update the
        // transform matrix in updatePaintNode().
        const double dx = std::abs(extent.centerX() - m_lastBuiltExtent.centerX());
        const double dy = std::abs(extent.centerY() - m_lastBuiltExtent.centerY());
        if (dx > m_lastBuiltExtent.width()  * 0.5
         || dy > m_lastBuiltExtent.height() * 0.5)
            m_contentDirty = true;
    } else {
        // First pan before any successful build — must rebuild.
        m_contentDirty = true;
    }
    update();
}

QSGNode *SWMMLayerQSGRenderer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // VS.1 — render nothing when the layer is hidden. MapCanvas only pushes
    // the layer to this renderer inside its qsgActive (visible-layer) block,
    // so on a whole-layer toggle-off the renderer is never told to clear and
    // its node tree would otherwise keep showing the full network (the
    // "glyphs remain after turning the layer off" artifact). The renderer's
    // repaintRequested → update() connection guarantees updatePaintNode runs
    // on the toggle; dropping the node here empties the FBO immediately and
    // it round-trips back to a full build when the layer is shown again.
    if (!m_layer || !m_layer->isVisible()
        || !m_extent.isValid() || width() <= 0 || height() <= 0) {
        delete oldNode;
        return nullptr;
    }

    QElapsedTimer frameTimer;
    if (sampler().enabled) frameTimer.start();
    const bool wasRebuild = m_contentDirty || m_selDirty;

    // ---- Node tree (draw order = back to front) ----------------------------
    auto *root = static_cast<QSGTransformNode *>(oldNode);
    QSGGeometryNode *catchFill=nullptr, *catchSelFill=nullptr;
    QSGGeometryNode *catchEdge=nullptr, *catchSelEdge=nullptr;
    QSGGeometryNode *junctionsBase=nullptr, *outfallsBase=nullptr;
    QSGGeometryNode *storageBase=nullptr,  *dividersBase=nullptr;
    QSGGeometryNode *gagesBase=nullptr;
    QSGGeometryNode *lines=nullptr, *linesSel=nullptr;
    QSGGeometryNode *nodesSel=nullptr, *gagesSel=nullptr;

    if (!root) {
        // Selection style — pens carry outline colour, brushes carry
        // fill colour (incl. alpha). selPoly is the polygon outline;
        // selPolyFill, selNode, selGage are fills. selLine is the link
        // halo's stroke colour. cap/join/width from the pens aren't
        // representable on a QSGFlatColorMaterial.
        const QColor selPoly     = selPenColor("subcatchment");
        const QColor selPolyFill = selBrushColor("subcatchment");
        const QColor selNode     = selBrushColor("node");
        const QColor selGage     = selBrushColor("gage");
        const QColor selLine     = selPenColor("link");

        root          = new QSGTransformNode();
        catchFill     = makeFlatColorNode(QSGGeometry::DrawTriangles, m_layer->subcatchmentSymbol().fillColor);
        catchSelFill  = makeFlatColorNode(QSGGeometry::DrawTriangles, selPolyFill);
        catchEdge     = makeFlatColorNode(QSGGeometry::DrawLines,     m_layer->subcatchmentSymbol().outlineColor, 1.0f);
        catchSelEdge  = makeFlatColorNode(QSGGeometry::DrawTriangles, selPoly);
        // §QSG-3 — vertex-coloured base node buckets. Per-vertex colour
        // lets each glyph carry its own colour: kind's fillColor for
        // unselected nodes, selection brush colour for selected nodes,
        // per-feature override colour where applicable. Eliminates the
        // separate `nodesSel` overlay (which had a latent rendering bug
        // — its geometry uploads silently didn't paint despite correct
        // vertex/material/parent state).
        junctionsBase = makeColoredNode(QSGGeometry::DrawTriangles);
        outfallsBase  = makeColoredNode(QSGGeometry::DrawTriangles);
        storageBase   = makeColoredNode(QSGGeometry::DrawTriangles);
        dividersBase  = makeColoredNode(QSGGeometry::DrawTriangles);
        gagesBase     = makeFlatColorNode(QSGGeometry::DrawTriangles, m_layer->rainGageSymbol().fillColor);
        lines         = makeFlatColorNode(QSGGeometry::DrawTriangles, m_layer->conduitSymbol().fillColor);
        linesSel      = makeFlatColorNode(QSGGeometry::DrawTriangles, selLine);
        // §QSG-3 — nodesSel uses vertex-coloured material so each
        // selected glyph carries its own colour in the vertex stream.
        // The same QSGFlatColorMaterial node used to silently swallow
        // post-creation geometry uploads on the macOS Metal backend
        // (verified by FBO dump: 24 verts in the geometry, opaque
        // yellow material, valid widget coords, but zero yellow pixels
        // in the rendered frame).  The vertex-coloured path uses the
        // exact same code shape as SWMM2DMeshQSGRenderer's coloured
        // triangle node, which paints correctly in the same offscreen
        // FBO setup.
        nodesSel      = makeColoredNode(QSGGeometry::DrawTriangles);
        gagesSel      = makeFlatColorNode(QSGGeometry::DrawTriangles, selGage);
        // QSG paints children in tree order: earlier siblings draw
        // first (back), later siblings draw on top (front). Layer
        // stacking (back → front):
        //   1. Subcatchment fill / outline (background)
        //   2. Link lines (above catchments, below nodes)
        //   3. Selected-link halo (above base lines)
        //   4. Node glyphs by kind (above all link geometry)
        //   5. Rain-gage glyphs
        //   6. Node + gage selection overlays (top)
        // Previously the node buckets were appended *before* the line
        // nodes, which made conduits paint *over* junctions — a
        // standard GIS Z-order bug. The order below matches QGIS /
        // ArcMap conventions for point-over-line.
        for (auto *n : {catchFill, catchSelFill, catchEdge, catchSelEdge,
                        lines, linesSel,
                        junctionsBase, outfallsBase, storageBase, dividersBase,
                        gagesBase,
                        nodesSel, gagesSel})
            root->appendChildNode(n);
    } else {
        auto *c = root->firstChild();
        catchFill     = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        catchSelFill  = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        catchEdge     = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        catchSelEdge  = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        lines         = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        linesSel      = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        junctionsBase = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        outfallsBase  = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        storageBase   = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        dividersBase  = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        gagesBase     = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        nodesSel      = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        gagesSel      = static_cast<QSGGeometryNode*>(c);
    }

    // ---- Shared render params (used by both full and selection-only paths) --
    // sx_r / invView are pixel-scale ratios so they're safe as float;
    // cull bounds, however, are scene-space coordinates and must stay
    // in double — float quantises projected-CRS coords (6-7 digit
    // magnitudes) to ~1 m, which trips false-positive / false-negative
    // cull decisions at high zoom and can flash links in and out as
    // the viewport scrolls.
    const float sx_r    = float(width())  / float(m_extent.width());
    const float invView = (sx_r > 0.0f) ? 1.0f / sx_r : 1.0f;
    const double ox = m_anchorX, oy = m_anchorY;
    // Wide cull margin (half the viewport on each side) so a pan that
    // shifts the viewport by up to half its width/height stays within
    // the cached vertex set — setMapExtent() exploits this to skip
    // m_contentDirty on small pans, leaving updatePaintNode() to just
    // refresh the transform matrix.
    const double cullMarginX = m_extent.width()  * 0.5;
    const double cullMarginY = m_extent.height() * 0.5;
    const double cullX0 =  m_extent.xMin() - cullMarginX;
    const double cullX1 =  m_extent.xMax() + cullMarginX;
    const double cullY0 = -m_extent.yMax() - cullMarginY;
    const double cullY1 = -m_extent.yMin() + cullMarginY;

    // ---- Content rebuild (full) --------------------------------------------
    if (m_contentDirty) {
        // Precision anchor — recomputed from node scene bounds.
        {
            double minX=1e18,maxX=-1e18,minY=1e18,maxY=-1e18;
            for (const QPointF &p : m_layer->m_nodeScenePts) {
                if(p.x()<minX)minX=p.x(); if(p.x()>maxX)maxX=p.x();
                if(p.y()<minY)minY=p.y(); if(p.y()>maxY)maxY=p.y();
            }
            m_anchorX = (minX<=maxX) ? (minX+maxX)*0.5 : 0.0;
            m_anchorY = (minY<=maxY) ? (minY+maxY)*0.5 : 0.0;
        }

        // Material refresh.
        // Selection style — pens carry outline colour, brushes carry
        // fill colour (incl. alpha). selPoly is the polygon outline;
        // selPolyFill, selNode, selGage are fills. selLine is the link
        // halo's stroke colour. cap/join/width from the pens aren't
        // representable on a QSGFlatColorMaterial.
        const QColor selPoly     = selPenColor("subcatchment");
        const QColor selPolyFill = selBrushColor("subcatchment");
        const QColor selNode     = selBrushColor("node");
        const QColor selGage     = selBrushColor("gage");
        const QColor selLine     = selPenColor("link");
        setNodeColor(catchFill,     m_layer->subcatchmentSymbol().fillColor);
        setNodeColor(catchSelFill,  selPolyFill);
        setNodeColor(catchEdge,     m_layer->subcatchmentSymbol().outlineColor);
        setNodeColor(catchSelEdge,  selPoly);
        // §QSG-3 — junctionsBase/outfallsBase/storageBase/dividersBase
        // use QSGVertexColorMaterial; colours are baked per-vertex at
        // upload time, no material-level setColor required.
        setNodeColor(gagesBase,     m_layer->rainGageSymbol().fillColor);
        setNodeColor(lines,         m_layer->conduitSymbol().fillColor);
        setNodeColor(linesSel,      selLine);
        // §QSG-3 — nodesSel uses QSGVertexColorMaterial; the colour is
        // baked into each vertex at upload time, so no material-level
        // setColor is needed (and would crash trying to static_cast).
        setNodeColor(gagesSel,      selGage);
        setLineWidth(catchEdge, 1.0f);

        // ---- Subcatchments -------------------------------------------------
        // §QSG-1: only render kinds owned by the QSG scope; uploading
        // empty vertex buffers for un-owned kinds keeps the geometry
        // node in the tree but makes the GPU draw zero triangles, so
        // the CPU SWMMLayerItem path can own that kind without
        // doubling-up.
        if (m_layer->showSubcatchments()
            && m_layer->qsgOwnsKind(SWMMModelLayer::QsgCatch)) {
            const auto &cps    = m_layer->m_catchScenePts;
            const auto &cBboxes= m_layer->m_catchSceneBBoxes;
            const auto &cHid   = m_layer->m_catchHiddenFlag;
            const auto &cSel   = m_layer->m_catchSelectedFlag;

            // Triangulation cache — only re-triangulate on geometry change.
            const quint64 rev = m_layer->geomRevision();
            if (m_catchTriCache.revision != rev) {
                m_catchTriCache.tris.resize(cps.size());
                for (int i = 0; i < cps.size(); ++i)
                    m_catchTriCache.tris[i] = earcutTriangulate(cps[i]);
                m_catchTriCache.revision = rev;
            }

            std::vector<QSGGeometry::Point2D> fillBase,fillSel,edgeBase,edgeSelTris;
            const float selEdgeHW = 1.5f * invView;
            for (int i = 0; i < cps.size(); ++i) {
                if (size_t(i)<cHid.size() && cHid[i]) continue;
                if (size_t(i)<size_t(cBboxes.size())) {
                    const QRectF &bb = cBboxes[i];
                    if (bb.right()<cullX0||bb.left()>cullX1||
                        bb.bottom()<cullY0||bb.top()>cullY1) continue;
                }
                const auto &poly = cps[i];
                if (poly.size() < 3) continue;
                const QVector<int> &tris = m_catchTriCache.tris[i];
                const bool sel = size_t(i)<cSel.size() && cSel[i];
                for (int idx : tris) {
                    QSGGeometry::Point2D p;
                    p.x=float(poly[idx].x()-ox); p.y=float(poly[idx].y()-oy);
                    fillBase.push_back(p);
                }
                for (int j = 0; j < poly.size(); ++j) {
                    QSGGeometry::Point2D va,vb;
                    va.x=float(poly[j].x()-ox); va.y=float(poly[j].y()-oy);
                    vb.x=float(poly[(j+1)%poly.size()].x()-ox);
                    vb.y=float(poly[(j+1)%poly.size()].y()-oy);
                    edgeBase.push_back(va); edgeBase.push_back(vb);
                }
                if (sel) {
                    for (int idx : tris) {
                        QSGGeometry::Point2D p;
                        p.x=float(poly[idx].x()-ox); p.y=float(poly[idx].y()-oy);
                        fillSel.push_back(p);
                    }
                    for (int j = 0; j < poly.size(); ++j) {
                        const float ax=float(poly[j].x()-ox), ay=float(poly[j].y()-oy);
                        const float bx=float(poly[(j+1)%poly.size()].x()-ox);
                        const float by=float(poly[(j+1)%poly.size()].y()-oy);
                        appendThickSegment(edgeSelTris,ax,ay,bx,by,selEdgeHW);
                    }
                }
            }
            uploadVerts(catchFill,    fillBase);
            uploadVerts(catchSelFill, fillSel);
            uploadVerts(catchEdge,    edgeBase);
            uploadVerts(catchSelEdge, edgeSelTris);
        } else {
            // Subcatchments not in QSG scope — clear so the CPU path
            // owns this kind without the GPU drawing on top of it.
            uploadVerts(catchFill,    {});
            uploadVerts(catchSelFill, {});
            uploadVerts(catchEdge,    {});
            uploadVerts(catchSelEdge, {});
        }

        // ---- Links ---------------------------------------------------------
        if (m_layer->showLinks()
            && m_layer->qsgOwnsKind(SWMMModelLayer::QsgLinks)) {
            const std::vector<double>   &flat    = m_layer->m_linkSceneFlat;
            const std::vector<uint32_t> &offsets = m_layer->m_linkVertexOffset;
            const std::vector<uint32_t> &counts  = m_layer->m_linkVertexCount;
            const auto &lSel    = m_layer->m_linkSelectedFlag;
            const auto &lHid    = m_layer->m_linkHiddenFlag;
            const QVector<QRectF> &lBboxes = m_layer->m_linkSceneBBoxes;
            const float baseHW = 1.0f*invView, selHW = 2.0f*invView;

            std::vector<QSGGeometry::Point2D> baseTri,selTri;
            size_t baseSegs=0, selSegs=0;
            for (size_t i = 0; i < counts.size(); ++i) {
                if (counts[i]<2) continue;
                if (i<lHid.size()&&lHid[i]) continue;
                if (size_t(i)<size_t(lBboxes.size())) {
                    const QRectF &bb=lBboxes[int(i)];
                    if(bb.right()<cullX0||bb.left()>cullX1||
                       bb.bottom()<cullY0||bb.top()>cullY1) continue;
                }
                baseSegs += counts[i]-1;
                if (i<lSel.size()&&lSel[i]) selSegs += counts[i]-1;
            }
            baseTri.reserve(baseSegs*6); selTri.reserve(selSegs*6);

            for (size_t i = 0; i < counts.size(); ++i) {
                const uint32_t cnt=counts[i];
                if (cnt<2) continue;
                if (i<lHid.size()&&lHid[i]) continue;
                if (size_t(i)<size_t(lBboxes.size())) {
                    const QRectF &bb=lBboxes[int(i)];
                    if(bb.right()<cullX0||bb.left()>cullX1||
                       bb.bottom()<cullY0||bb.top()>cullY1) continue;
                }
                const double *p=flat.data()+size_t(offsets[i])*2;
                const bool sel=i<lSel.size()&&lSel[i];
                for (uint32_t j=1; j<cnt; ++j) {
                    const float ax=float(p[(j-1)*2]-ox), ay=float(p[(j-1)*2+1]-oy);
                    const float bx=float(p[j*2]-ox),     by=float(p[j*2+1]-oy);
                    appendThickSegment(baseTri,ax,ay,bx,by,baseHW);
                    if (sel) appendThickSegment(selTri,ax,ay,bx,by,selHW);
                }
            }
            uploadVerts(lines,    baseTri);
            uploadVerts(linesSel, selTri);
        } else {
            uploadVerts(lines,    {});
            uploadVerts(linesSel, {});
        }

        // ---- Nodes ---------------------------------------------------------
        if (m_layer->showNodes()
            && m_layer->qsgOwnsKind(SWMMModelLayer::QsgNodes)) {
            constexpr float kMinPx = 1.0f;
            // §QSG-3 — vertex-coloured buckets. Each glyph emits 3..24
            // verts in its kind's bucket with the chosen colour baked
            // into each vertex. Selected nodes use selBrushColor;
            // unselected nodes use the kind's fillColor (no per-feature
            // override yet — wire to layer->featureColor() in a later
            // pass).
            std::vector<QSGGeometry::ColoredPoint2D> junc,outf,stor,divr;
            const uchar selR = uchar(selNode.red());
            const uchar selG = uchar(selNode.green());
            const uchar selB = uchar(selNode.blue());
            const uchar selA = uchar(selNode.alpha());
            auto unpack = [](const QColor &c, uchar &r, uchar &g, uchar &b, uchar &a) {
                r = uchar(c.red()); g = uchar(c.green());
                b = uchar(c.blue()); a = uchar(c.alpha());
            };
            uchar jR,jG,jB,jA; unpack(m_layer->junctionSymbol().fillColor,jR,jG,jB,jA);
            uchar oR,oG,oB,oA; unpack(m_layer->outfallSymbol().fillColor, oR,oG,oB,oA);
            uchar sR,sG,sB,sA; unpack(m_layer->storageSymbol().fillColor, sR,sG,sB,sA);
            uchar dR,dG,dB,dA; unpack(m_layer->dividerSymbol().fillColor, dR,dG,dB,dA);
            // Per-kind marker shape, looked up once per frame to keep
            // the inner loop branch-free on the symbol struct.
            const auto jShape = m_layer->junctionSymbol().markerShape;
            const auto oShape = m_layer->outfallSymbol().markerShape;
            const auto sShape = m_layer->storageSymbol().markerShape;
            const auto dShape = m_layer->dividerSymbol().markerShape;
            const auto &nps   = m_layer->m_nodeScenePts;
            const auto &nodes = m_layer->m_nodes;
            const auto &nHid  = m_layer->m_nodeHiddenFlag;
            const auto &nSel  = m_layer->m_nodeSelectedFlag;
            for (int i = 0; i < nodes.size(); ++i) {
                if (size_t(i)<nHid.size()&&nHid[i]) continue;
                if (i>=nps.size()) continue;
                const QPointF &p=nps[i];
                if (p.x()<cullX0||p.x()>cullX1||
                    p.y()<cullY0||p.y()>cullY1) continue;
                const int nt=(nodes[i].nodeType>=0&&nodes[i].nodeType<4)?nodes[i].nodeType:0;
                float pxR=float(m_layer->junctionSymbol().size)*0.5f;
                if(nt==1) pxR=float(m_layer->outfallSymbol().size)*0.5f;
                else if(nt==2) pxR=float(m_layer->storageSymbol().size)*0.5f;
                else if(nt==3) pxR=float(m_layer->dividerSymbol().size)*0.5f;
                if (pxR<kMinPx) continue;
                const float r=pxR*invView;
                const float fx=float(p.x()-ox), fy=float(p.y()-oy);
                auto *bucket=&junc;
                uchar cR=jR,cG=jG,cB=jB,cA=jA;
                auto shape = jShape;
                if      (nt==1) { bucket=&outf; cR=oR; cG=oG; cB=oB; cA=oA; shape=oShape; }
                else if (nt==2) { bucket=&stor; cR=sR; cG=sG; cB=sB; cA=sA; shape=sShape; }
                else if (nt==3) { bucket=&divr; cR=dR; cG=dG; cB=dB; cA=dA; shape=dShape; }
                // Selection: replace the per-kind base colour with the
                // selection brush colour. Same shape and size, just a
                // recolour — matches CPU painter behaviour.
                if (size_t(i)<nSel.size()&&nSel[i]) {
                    cR=selR; cG=selG; cB=selB; cA=selA;
                }
                appendNodeGlyphTrianglesColored(*bucket,fx,fy,r,shape,cR,cG,cB,cA);
            }
            uploadColoredVerts(junctionsBase,junc);
            uploadColoredVerts(outfallsBase, outf);
            uploadColoredVerts(storageBase,  stor);
            uploadColoredVerts(dividersBase, divr);
            // nodesSel is no longer used for nodes — keep it empty so
            // the QSG tree slot stays consistent for the else-branch
            // child lookup but draws nothing.
            uploadColoredVerts(nodesSel, {});
        } else {
            uploadColoredVerts(junctionsBase, {});
            uploadColoredVerts(outfallsBase,  {});
            uploadColoredVerts(storageBase,   {});
            uploadColoredVerts(dividersBase,  {});
            uploadColoredVerts(nodesSel,      {});
        }

        // ---- Gages ---------------------------------------------------------
        if (m_layer->showRainGages()
            && m_layer->qsgOwnsKind(SWMMModelLayer::QsgGages)) {
            constexpr float kMinPx = 1.0f;
            const float gagePxR = float(m_layer->rainGageSymbol().size)*0.5f;
            const auto gShape   = m_layer->rainGageSymbol().markerShape;
            std::vector<QSGGeometry::Point2D> base,sel;
            if (gagePxR >= kMinPx) {
                const auto &gps  = m_layer->m_gageScenePts;
                const auto &gages= m_layer->m_gages;
                const auto &gHid = m_layer->m_gageHiddenFlag;
                const auto &gSel = m_layer->m_gageSelectedFlag;
                const float r=gagePxR*invView;
                for (int i = 0; i < gages.size(); ++i) {
                    if (size_t(i)<gHid.size()&&gHid[i]) continue;
                    if (i>=gps.size()) continue;
                    const QPointF &p=gps[i];
                    if(p.x()<cullX0||p.x()>cullX1||
                       p.y()<cullY0||p.y()>cullY1) continue;
                    const float fx=float(p.x()-ox), fy=float(p.y()-oy);
                    appendGageTriangles(base,fx,fy,r,gShape);
                    if (size_t(i)<gSel.size()&&gSel[i])
                        appendGageTriangles(sel,fx,fy,r,gShape);  // §QSG-3 — same size as base
                }
            }
            uploadVerts(gagesBase,base);
            uploadVerts(gagesSel, sel);
        } else {
            uploadVerts(gagesBase, {});
            uploadVerts(gagesSel,  {});
        }

        m_contentDirty   = false;
        m_selDirty       = false;  // base rebuild covers selection too
        // Stamp the extent so setMapExtent() can decide pan-only vs.
        // full rebuild based on how far the next viewport drifts.
        m_lastBuiltExtent = m_extent;

    // ---- Selection-only rebuild (base geometry unchanged) ------------------
    } else if (m_selDirty) {
        // Only the 5 overlay buffers need updating. The 8 base-geometry
        // buffers (fills, edges, base glyphs, base lines) are untouched.
        const float selEdgeHW = 1.5f * invView;
        const float selHW     = 2.0f * invView;
        // Selection style — pens carry outline colour, brushes carry
        // fill colour (incl. alpha). selPoly is the polygon outline;
        // selPolyFill, selNode, selGage are fills. selLine is the link
        // halo's stroke colour. cap/join/width from the pens aren't
        // representable on a QSGFlatColorMaterial.
        const QColor selPoly     = selPenColor("subcatchment");
        const QColor selPolyFill = selBrushColor("subcatchment");
        const QColor selNode     = selBrushColor("node");
        const QColor selGage     = selBrushColor("gage");
        const QColor selLine     = selPenColor("link");

        // Subcatchment selection overlays
        if (m_layer->showSubcatchments()
            && m_layer->qsgOwnsKind(SWMMModelLayer::QsgCatch)) {
            const auto &cps  = m_layer->m_catchScenePts;
            const auto &cBboxes = m_layer->m_catchSceneBBoxes;
            const auto &cHid = m_layer->m_catchHiddenFlag;
            const auto &cSel = m_layer->m_catchSelectedFlag;
            std::vector<QSGGeometry::Point2D> fillSel, edgeSelTris;
            for (int i = 0; i < cps.size(); ++i) {
                if (size_t(i)<cSel.size() && !cSel[i]) continue;
                if (size_t(i)<cHid.size() &&  cHid[i]) continue;
                if (size_t(i)<size_t(cBboxes.size())) {
                    const QRectF &bb=cBboxes[i];
                    if(bb.right()<cullX0||bb.left()>cullX1||
                       bb.bottom()<cullY0||bb.top()>cullY1) continue;
                }
                const auto &poly = cps[i];
                if (poly.size() < 3 || size_t(i) >= size_t(m_catchTriCache.tris.size())) continue;
                const QVector<int> &tris = m_catchTriCache.tris[i];
                for (int idx : tris) {
                    QSGGeometry::Point2D p;
                    p.x=float(poly[idx].x()-ox); p.y=float(poly[idx].y()-oy);
                    fillSel.push_back(p);
                }
                for (int j = 0; j < poly.size(); ++j) {
                    const float ax=float(poly[j].x()-ox), ay=float(poly[j].y()-oy);
                    const float bx=float(poly[(j+1)%poly.size()].x()-ox);
                    const float by=float(poly[(j+1)%poly.size()].y()-oy);
                    appendThickSegment(edgeSelTris,ax,ay,bx,by,selEdgeHW);
                }
            }
            setNodeColor(catchSelFill, selPolyFill);
            setNodeColor(catchSelEdge, selPoly);
            uploadVerts(catchSelFill, fillSel);
            uploadVerts(catchSelEdge, edgeSelTris);
        }

        // Link selection overlay
        if (m_layer->showLinks()
            && m_layer->qsgOwnsKind(SWMMModelLayer::QsgLinks)) {
            const std::vector<double>   &flat    = m_layer->m_linkSceneFlat;
            const std::vector<uint32_t> &offsets = m_layer->m_linkVertexOffset;
            const std::vector<uint32_t> &counts  = m_layer->m_linkVertexCount;
            const auto &lSel    = m_layer->m_linkSelectedFlag;
            const auto &lHid    = m_layer->m_linkHiddenFlag;
            const QVector<QRectF> &lBboxes = m_layer->m_linkSceneBBoxes;
            std::vector<QSGGeometry::Point2D> selTri;
            for (size_t i = 0; i < counts.size(); ++i) {
                if (i>=lSel.size() || !lSel[i]) continue;
                if (counts[i]<2) continue;
                if (i<lHid.size()&&lHid[i]) continue;
                if (size_t(i)<size_t(lBboxes.size())) {
                    const QRectF &bb=lBboxes[int(i)];
                    if(bb.right()<cullX0||bb.left()>cullX1||
                       bb.bottom()<cullY0||bb.top()>cullY1) continue;
                }
                const double *p=flat.data()+size_t(offsets[i])*2;
                for (uint32_t j=1; j<counts[i]; ++j) {
                    const float ax=float(p[(j-1)*2]-ox), ay=float(p[(j-1)*2+1]-oy);
                    const float bx=float(p[j*2]-ox),     by=float(p[j*2+1]-oy);
                    appendThickSegment(selTri,ax,ay,bx,by,selHW);
                }
            }
            setNodeColor(linesSel, selLine);
            uploadVerts(linesSel, selTri);
        }

        // Node selection overlay
        if (m_layer->showNodes()
            && m_layer->qsgOwnsKind(SWMMModelLayer::QsgNodes)) {
            const auto &nps   = m_layer->m_nodeScenePts;
            const auto &nodes = m_layer->m_nodes;
            const auto &nHid  = m_layer->m_nodeHiddenFlag;
            const auto &nSel  = m_layer->m_nodeSelectedFlag;
            std::vector<QSGGeometry::ColoredPoint2D> sel;
            const uchar selR = uchar(selNode.red());
            const uchar selG = uchar(selNode.green());
            const uchar selB = uchar(selNode.blue());
            const uchar selA = uchar(selNode.alpha());
            for (int i = 0; i < nodes.size(); ++i) {
                if (size_t(i)>=nSel.size() || !nSel[i]) continue;
                if (size_t(i)<nHid.size()&&nHid[i]) continue;
                if (i>=nps.size()) continue;
                const QPointF &p=nps[i];
                if(p.x()<cullX0||p.x()>cullX1||
                   p.y()<cullY0||p.y()>cullY1) continue;
                const int nt=(nodes[i].nodeType>=0&&nodes[i].nodeType<4)?nodes[i].nodeType:0;
                float pxR=float(m_layer->junctionSymbol().size)*0.5f;
                auto shape = m_layer->junctionSymbol().markerShape;
                if      (nt==1) { pxR=float(m_layer->outfallSymbol().size)*0.5f; shape=m_layer->outfallSymbol().markerShape; }
                else if (nt==2) { pxR=float(m_layer->storageSymbol().size)*0.5f; shape=m_layer->storageSymbol().markerShape; }
                else if (nt==3) { pxR=float(m_layer->dividerSymbol().size)*0.5f; shape=m_layer->dividerSymbol().markerShape; }
                if (pxR < 1.0f) continue;
                appendNodeGlyphTrianglesColored(sel,
                    float(p.x()-ox),float(p.y()-oy),pxR*invView,shape,
                    selR,selG,selB,selA);
            }
            uploadColoredVerts(nodesSel, sel);
        }

        // Gage selection overlay
        if (m_layer->showRainGages()
            && m_layer->qsgOwnsKind(SWMMModelLayer::QsgGages)) {
            const auto &gps  = m_layer->m_gageScenePts;
            const auto &gages= m_layer->m_gages;
            const auto &gHid = m_layer->m_gageHiddenFlag;
            const auto &gSel = m_layer->m_gageSelectedFlag;
            // §QSG-3 — same size as base.
            const float r       = float(m_layer->rainGageSymbol().size)*0.5f*invView;
            const auto  gShape  = m_layer->rainGageSymbol().markerShape;
            std::vector<QSGGeometry::Point2D> sel;
            for (int i = 0; i < gages.size(); ++i) {
                if (size_t(i)>=gSel.size() || !gSel[i]) continue;
                if (size_t(i)<gHid.size()&&gHid[i]) continue;
                if (i>=gps.size()) continue;
                const QPointF &p=gps[i];
                if(p.x()<cullX0||p.x()>cullX1||
                   p.y()<cullY0||p.y()>cullY1) continue;
                appendGageTriangles(sel,float(p.x()-ox),float(p.y()-oy),r,gShape);
            }
            setNodeColor(gagesSel, selGage);
            uploadVerts(gagesSel, sel);
        }

        m_selDirty = false;
    }

    // ---- Transform (always — pan changes only the translate) ---------------
    const float msx=float(width())/float(m_extent.width());
    const float msy=float(height())/float(m_extent.height());
    QMatrix4x4 mat;
    mat.scale(msx,msy);
    mat.translate(float(m_anchorX-m_extent.xMin()), float(m_anchorY+m_extent.yMax()));
    if (root->matrix() != mat) root->setMatrix(mat);

    if (sampler().enabled)
        sampler().record(frameTimer.elapsed(), wasRebuild);

    return root;
}
