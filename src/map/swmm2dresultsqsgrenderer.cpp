/*!
 * \file   swmm2dresultsqsgrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * VS.8 — GPU scene-graph renderer for SWMM2DResultsLayer. See the header
 * for the pass list. Structure mirrors SWMM2DMeshQSGRenderer (the terrain
 * template): anchor-shifted float vertices, content-dirty rebuilds, pure
 * pan = matrix-only update.
 */
#include "map/swmm2dresultsqsgrenderer.h"

#include "contour/contourchain.h"
#include "contour/marchingtriangles.h"
#include "layers/swmm2dresultslayer.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshnodesublayer.h"
#include "render/sublayers/velocityvectorsublayer.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QLineF>
#include <QMatrix4x4>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Node helpers (same idioms as SWMM2DMeshQSGRenderer)
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

void appendThickSegColored(std::vector<QSGGeometry::ColoredPoint2D> &out,
                           float ax, float ay, float bx, float by, float hw,
                           quint8 r, quint8 g, quint8 b, quint8 a)
{
    const float dx = bx-ax, dy = by-ay;
    const float len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-9f) return;
    const float nx = -dy/len*hw, ny = dx/len*hw;
    auto v = [&](float x, float y){
        QSGGeometry::ColoredPoint2D p; p.set(x, y, r, g, b, a); return p; };
    out.push_back(v(ax+nx,ay+ny)); out.push_back(v(bx+nx,by+ny)); out.push_back(v(ax-nx,ay-ny));
    out.push_back(v(bx+nx,by+ny)); out.push_back(v(bx-nx,by-ny)); out.push_back(v(ax-nx,ay-ny));
}

struct ResultsRootNode : QSGTransformNode
{
    QSGGeometryNode *cellFillNode   = nullptr;
    QSGGeometryNode *smoothFillNode = nullptr;
    QSGGeometryNode *bandNode       = nullptr;
    QSGGeometryNode *isoNode        = nullptr;
    QSGGeometryNode *isoIndexNode   = nullptr;
    QSGNode         *isoLabels      = nullptr;
    QSGGeometryNode *edgeNode       = nullptr;
    QSGGeometryNode *nodeMarkNode   = nullptr;
    QSGGeometryNode *velNode        = nullptr;
    QSGGeometryNode *hiFillNode     = nullptr;
    QSGGeometryNode *hiEdgeNode     = nullptr;
};

// ---------------------------------------------------------------------------
// Isoline label rasterisation — parametrised variant of the mesh renderer's
// helper (font size + halo from IsolineStyle).
// ---------------------------------------------------------------------------

QImage rasteriseLabel(const QString &text, const QColor &color,
                      double fontPt, bool halo, double dpr)
{
    QFont f;
    f.setPointSizeF(fontPt);
    f.setBold(true);
    const QFontMetricsF fm(f);
    const QRectF br = fm.boundingRect(text);
    const int pad = 3;
    const int w = int(std::ceil(br.width()))  + pad * 2;
    const int h = int(std::ceil(br.height())) + pad * 2;

    QImage img(int(w * dpr), int(h * dpr), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(f);

    QPainterPath path;
    path.addText(pad - br.left(), pad - br.top(), f, text);

    if (halo) {
        QPen haloPen(QColor(255, 255, 255, 230));
        haloPen.setWidthF(3.0);
        haloPen.setJoinStyle(Qt::RoundJoin);
        haloPen.setCapStyle(Qt::RoundCap);
        p.setPen(haloPen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);

    p.end();
    return img;
}

} // namespace

// ---------------------------------------------------------------------------

SWMM2DResultsQSGRenderer::SWMM2DResultsQSGRenderer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

SWMM2DResultsQSGRenderer::~SWMM2DResultsQSGRenderer()
{
    clearLabelTextureCache();
}

void SWMM2DResultsQSGRenderer::clearLabelTextureCache()
{
    for (QSGTexture *t : std::as_const(m_labelTextureCache))
        if (t) t->deleteLater();
    m_labelTextureCache.clear();
}

void SWMM2DResultsQSGRenderer::setLayer(SWMM2DResultsLayer *layer)
{
    if (m_layer == layer) return;
    if (m_layer) QObject::disconnect(m_layer, nullptr, this, nullptr);
    m_layer = layer;
    if (m_layer) {
        // Style/sublayer edits funnel into repaintRequested; animation
        // ticks land via currentTimeChanged (setCurrentTimeIndex does not
        // emit repaintRequested). Both invalidate content.
        auto dirty = [this]() { m_contentDirty = true; update(); };
        connect(m_layer, &OpenSWMMVisLayer::repaintRequested, this, dirty);
        connect(m_layer, &SWMM2DResultsLayer::currentTimeChanged, this,
                [this](int) {
                    m_cachedBands.clear();
                    m_cachedSegs.clear();
                    m_bandCacheTime = -1;
                    m_isoCacheTime  = -1;
                    m_contentDirty = true;
                    update();
                });
        connect(m_layer, &SWMM2DResultsLayer::highlightedCellsChanged,
                this, dirty);
    }
    forceRebuild();
}

void SWMM2DResultsQSGRenderer::setMapExtent(const MapExtent &extent)
{
    if (extent == m_extent) return;
    const bool zoomChanged =
        !qFuzzyCompare(extent.width(),  m_extent.width()) ||
        !qFuzzyCompare(extent.height(), m_extent.height());
    m_extent = extent;
    if (zoomChanged) m_contentDirty = true;
    update();
}

void SWMM2DResultsQSGRenderer::forceRebuild()
{
    clearLabelTextureCache();
    m_cachedBands.clear();
    m_cachedSegs.clear();
    m_bandCacheTime = -1;
    m_isoCacheTime  = -1;
    m_contentDirty  = true;
    update();
}

QSGNode *SWMM2DResultsQSGRenderer::updatePaintNode(QSGNode *oldNode,
                                                    UpdatePaintNodeData *)
{
    // Scrub-diagnosis probe (OPENSWMM_2D_RENDER_DEBUG=1): logs every sync so a
    // "layer advanced but view never re-rendered" report can be localized to
    // (a) sync not running, (b) a null-return gate, (c) the bbox cull, or
    // (d) a rebuild that produced empty geometry.
    static const bool kUpnDebug =
        qEnvironmentVariableIsSet("OPENSWMM_2D_RENDER_DEBUG");
    if (kUpnDebug)
        qDebug("[2D-qsg] sync: layer=%p vis=%d t=%d lastT=%d dirty=%d "
               "extentValid=%d itemWH=%gx%g",
               static_cast<void *>(m_layer),
               m_layer ? int(m_layer->isVisible()) : -1,
               m_layer ? m_layer->currentTimeIndex() : -999, m_lastRenderedTime,
               int(m_contentDirty), int(m_extent.isValid()), width(), height());

    if (!m_layer || !m_layer->isVisible() || !m_extent.isValid()
        || width() <= 0 || height() <= 0) {
        if (kUpnDebug) qDebug("[2D-qsg]   -> null-return (gate 1)");
        delete oldNode;
        return nullptr;
    }

    // Issue 2 master gate: a LIVE (streaming) layer with live rendering switched
    // off stops drawing entirely (deletes the node tree so no stale geometry
    // lingers). Non-live static results are unaffected and keep drawing.
    if (m_layer->source() && m_layer->source()->isLive()
        && !m_layer->liveRenderEnabled()) {
        if (kUpnDebug) qDebug("[2D-qsg]   -> null-return (live gate: "
                              "isLive=1 liveRenderEnabled=0)");
        delete oldNode;
        return nullptr;
    }

    // ---- Node tree, z-order bottom→top:
    //        bandNode        (Pass 2: filled contour bands — depth fill)
    //        isoNode         (Pass 3: isolines)
    //        isoIndexNode    (Pass 3: index contours)
    //        isoLabels       (Pass 3b: along-line labels)
    //        edgeNode        (Pass 3c: mesh wireframe — MeshEdgeSublayer)
    //        nodeMarkNode    (Pass 3d: mesh-vertex markers — MeshNodeSublayer)
    //        velNode         (Pass 4: velocity-vector glyphs)
    //        hiFillNode      (Pass 6: highlight fill)
    //        hiEdgeNode      (Pass 6: highlight outlines)
    // 2026-06-21 — the Gouraud depth-fill pass (depth color ramp) and the
    // flow-arrow passes were removed: contour bands now carry the depth
    // fill, and velocity vectors already convey flow direction.
    // 2026-06-29 — edgeNode/nodeMarkNode added so the live GPU view renders the
    // mesh wireframe + vertex markers that the CPU/QPainter path already draws
    // (swmm2dresultslayer.cpp Pass 4/5); edges under vertices, both above the
    // fills/contours and below the velocity glyphs + highlight overlay.
    auto *root = static_cast<ResultsRootNode *>(oldNode);
    QSGGeometryNode *cellFillNode    = nullptr;  // flat per-cell depth fill
    QSGGeometryNode *smoothFillNode  = nullptr;  // per-vertex Gouraud depth fill
    QSGGeometryNode *bandNode        = nullptr;
    QSGGeometryNode *isoNode         = nullptr;
    QSGGeometryNode *isoIndexNode    = nullptr;
    QSGNode         *isoLabels       = nullptr;
    QSGGeometryNode *edgeNode        = nullptr;  // mesh wireframe overlay
    QSGGeometryNode *nodeMarkNode    = nullptr;  // mesh-vertex markers
    QSGGeometryNode *velNode         = nullptr;
    QSGGeometryNode *hiFillNode      = nullptr;
    QSGGeometryNode *hiEdgeNode      = nullptr;

    // CF.3 highlight colours — match the CPU pass.
    const QColor kHiFillColor(0, 200, 255, 110);     // translucent cyan
    const QColor kHiEdgeColor(255, 215, 0, 235);     // gold

    if (!root) {
        // Fresh tree (first frame, or the layer was hidden and the old tree
        // deleted) — geometry must be rebuilt regardless of the dirty flag.
        m_contentDirty  = true;
        root            = new ResultsRootNode();
        cellFillNode    = root->cellFillNode   = makeColoredNode();
        smoothFillNode  = root->smoothFillNode = makeColoredNode();
        bandNode        = root->bandNode       = makeColoredNode();
        isoNode         = root->isoNode        = makeFlatNode(QColor(10, 10, 10, 220));
        isoIndexNode    = root->isoIndexNode   = makeFlatNode(QColor(10, 10, 10, 220));
        isoLabels       = root->isoLabels      = new QSGNode();
        edgeNode        = root->edgeNode       = makeFlatNode(QColor(0, 0, 0, 130));
        nodeMarkNode    = root->nodeMarkNode   = makeFlatNode(QColor(40, 40, 40, 220));
        velNode         = root->velNode        = makeColoredNode();
        hiFillNode      = root->hiFillNode     = makeFlatNode(kHiFillColor);
        hiEdgeNode      = root->hiEdgeNode     = makeFlatNode(kHiEdgeColor);
        // Fills first → they paint UNDER the bands/edges/isolines.
        root->appendChildNode(cellFillNode);
        root->appendChildNode(smoothFillNode);
        root->appendChildNode(bandNode);
        root->appendChildNode(isoNode);
        root->appendChildNode(isoIndexNode);
        root->appendChildNode(isoLabels);
        root->appendChildNode(edgeNode);
        root->appendChildNode(nodeMarkNode);
        root->appendChildNode(velNode);
        root->appendChildNode(hiFillNode);
        root->appendChildNode(hiEdgeNode);
    } else {
        cellFillNode    = root->cellFillNode;
        smoothFillNode  = root->smoothFillNode;
        bandNode        = root->bandNode;
        isoNode         = root->isoNode;
        isoIndexNode    = root->isoIndexNode;
        isoLabels       = root->isoLabels;
        edgeNode        = root->edgeNode;
        nodeMarkNode    = root->nodeMarkNode;
        velNode         = root->velNode;
        hiFillNode      = root->hiFillNode;
        hiEdgeNode      = root->hiEdgeNode;
    }

    // ---- Shared render params ---------------------------------------------
    const float sx_r    = float(width())  / float(m_extent.width());
    const float invView = (sx_r > 0.0f) ? 1.0f / sx_r : 1.0f;

    const double cullMargin = double(2.0f * invView);
    const double cullX0 =  m_extent.xMin() - cullMargin;
    const double cullX1 =  m_extent.xMax() + cullMargin;
    const double cullY0 = -m_extent.yMax() - cullMargin;
    const double cullY1 = -m_extent.yMin() + cullMargin;

    auto applyTransform = [&]() {
        const float msx = float(width())  / float(m_extent.width());
        const float msy = float(height()) / float(m_extent.height());
        QMatrix4x4 mat;
        mat.scale(msx, msy);
        mat.translate(float(m_anchorX - m_extent.xMin()),
                      float(m_anchorY + m_extent.yMax()));
        if (root->matrix() != mat) root->setMatrix(mat);
    };

    auto clearAll = [&]() {
        const std::vector<QSGGeometry::ColoredPoint2D> empty_c;
        const std::vector<QSGGeometry::Point2D>        empty_p;
        uploadColoredVerts(cellFillNode, empty_c);
        uploadColoredVerts(smoothFillNode, empty_c);
        uploadColoredVerts(bandNode, empty_c);
        uploadFlatVerts(isoNode, empty_p);
        uploadFlatVerts(isoIndexNode, empty_p);
        while (auto *c = isoLabels->firstChild()) {
            isoLabels->removeChildNode(c);
            delete c;
        }
        uploadFlatVerts(edgeNode, empty_p);
        uploadFlatVerts(nodeMarkNode, empty_p);
        uploadColoredVerts(velNode, empty_c);
        uploadFlatVerts(hiFillNode, empty_p);
        uploadFlatVerts(hiEdgeNode, empty_p);
    };

    auto syncSublayerOrder = [&]() {
        auto appendNode = [&](QSGNode *node) {
            if (!node) return;
            if (node->parent() == root)
                root->removeChildNode(node);
            root->appendChildNode(node);
        };

        bool addedCell = false;
        bool addedSmooth = false;
        bool addedBand = false;
        bool addedIso = false;
        bool addedEdge = false;
        bool addedNodeMarkers = false;
        bool addedVelocity = false;

        auto addCell = [&]() {
            if (addedCell) return;
            appendNode(cellFillNode);
            addedCell = true;
        };
        auto addSmooth = [&]() {
            if (addedSmooth) return;
            appendNode(smoothFillNode);
            addedSmooth = true;
        };
        auto addBand = [&]() {
            if (addedBand) return;
            appendNode(bandNode);
            addedBand = true;
        };
        auto addIso = [&]() {
            if (addedIso) return;
            appendNode(isoNode);
            appendNode(isoIndexNode);
            appendNode(isoLabels);
            addedIso = true;
        };
        auto addEdge = [&]() {
            if (addedEdge) return;
            appendNode(edgeNode);
            addedEdge = true;
        };
        auto addNodeMarkers = [&]() {
            if (addedNodeMarkers) return;
            appendNode(nodeMarkNode);
            addedNodeMarkers = true;
        };
        auto addVelocity = [&]() {
            if (addedVelocity) return;
            appendNode(velNode);
            addedVelocity = true;
        };

        for (OpenSWMM::Render::ISublayer *sub : m_layer->sublayers()) {
            if (!sub) continue;
            const QString id = sub->id();
            if (id == QLatin1String("results2d.cellDepthFill")) {
                addCell();
            } else if (id == QLatin1String("results2d.smoothDepthFill")) {
                addSmooth();
            } else if (id == QLatin1String("results2d.bands")) {
                addBand();
            } else if (id == QLatin1String("results2d.meshEdges")) {
                addEdge();
            } else if (id == QLatin1String("results2d.isolines")) {
                addIso();
            } else if (id == QLatin1String("results2d.meshVertices")) {
                addNodeMarkers();
            } else if (id == QLatin1String("results2d.velocity")) {
                addVelocity();
            }
        }

        // Fallback order for any node absent from an older/partial sublayer
        // list. The terrain mesh fill itself is still supplied by the mesh
        // layer/CPU path, so there is intentionally no results2d.mesh node here.
        addCell();
        addSmooth();
        addBand();
        addEdge();
        addIso();
        addNodeMarkers();
        addVelocity();

        appendNode(hiFillNode);
        appendNode(hiEdgeNode);
    };
    syncSublayerOrder();

    // Early-out if bbox entirely outside view (keep dirty so a pan-back
    // rebuilds).
    {
        const QRectF &bb = m_layer->m_sceneBBox;
        if (bb.isNull() || m_layer->m_sceneTris.isEmpty() ||
            bb.right() < cullX0 || bb.left() > cullX1 ||
            bb.bottom() < cullY0 || bb.top() > cullY1)
        {
            if (kUpnDebug)
                qDebug("[2D-qsg]   -> bbox CULL: bb=(%g,%g %gx%g) null=%d "
                       "tris=%d cull=(%g..%g, %g..%g)",
                       bb.left(), bb.top(), bb.width(), bb.height(),
                       int(bb.isNull()), int(m_layer->m_sceneTris.size()),
                       cullX0, cullX1, cullY0, cullY1);
            clearAll();
            m_contentDirty = true;
            applyTransform();
            return root;
        }
    }

    // Frame-advance guard: force a rebuild whenever the layer has moved to a new
    // frame since the last one we rendered. The currentTimeChanged →
    // m_contentDirty connection can be consumed by an interleaved render before
    // the canvas grabs the framebuffer, leaving the bands/fill/vectors a tick
    // stale until the next extent change (zoom) re-dirties via setMapExtent —
    // the "contour bands only appear after zooming" symptom. Comparing the
    // rendered frame index makes the per-tick refresh deterministic.
    if (m_layer->currentTimeIndex() != m_lastRenderedTime) {
        m_contentDirty     = true;
        m_lastRenderedTime = m_layer->currentTimeIndex();
    }

    // ---- Full content rebuild ----------------------------------------------
    if (m_contentDirty) {

        {
            const QRectF &bb = m_layer->m_sceneBBox;
            m_anchorX = (bb.left() + bb.right())  * 0.5;
            m_anchorY = (bb.top()  + bb.bottom()) * 0.5;
        }
        const double ox = m_anchorX, oy = m_anchorY;

        const auto  &tris     = m_layer->m_sceneTris;
        const int    nTri     = int(tris.size());
        const double dryDepth = m_layer->dryDepth();
        const double maxDepth = std::max(m_layer->maxDepth(), dryDepth + 1e-9);

        auto *bandSub  = m_layer->contourBandSublayer();
        auto *isoSub   = m_layer->isolineSublayer();
        auto *velSub   = m_layer->velocityVectorSublayer();

        const OpenSWMM::Render::ContourBandStyle *bs =
            bandSub ? bandSub->bandStyle() : nullptr;
        const OpenSWMM::Render::IsolineStyle *is =
            isoSub ? isoSub->isolineStyle() : nullptr;
        const OpenSWMM::Render::VelocityVectorStyle *vs =
            velSub ? velSub->vectorStyle() : nullptr;

        // Issue 3B diagnostic — off by default. Set OPENSWMM_2D_RENDER_DEBUG=1
        // to log, per content rebuild, the guard states that decide whether
        // velocity vectors / contour bands / isolines / mesh edges / vertices
        // draw in the live GPU view. Lets the false guard / empty producer be
        // localized at runtime without rebuilding (plan Issue 3B step 2).
        static const bool kRenderDebug =
            qEnvironmentVariableIsSet("OPENSWMM_2D_RENDER_DEBUG");
        if (kRenderDebug) {
            const bool live = m_layer->source() && m_layer->source()->isLive();
            auto vis = [](OpenSWMM::Render::ISublayer *s) {
                return s ? (s->isVisible() ? 1 : 0) : -1; };
            qDebug("[2D-render] live=%d nTri=%d hasVel=%d t=%d | "
                   "band(vis=%d) iso(vis=%d) vel(vis=%d) edge(vis=%d) vert(vis=%d)",
                   live ? 1 : 0, nTri, m_layer->hasVelocityData() ? 1 : 0,
                   m_layer->currentTimeIndex(),
                   vis(bandSub), vis(isoSub), vis(velSub),
                   vis(m_layer->meshEdgeSublayer()),
                   vis(m_layer->meshNodeSublayer()));
        }

        // ---- Pass 1 (depth fill): removed (2026-06-21). The Gouraud depth
        // color ramp is gone; contour bands (Pass 2 below) now provide the
        // depth fill, and dry cells stay transparent so the terrain mesh
        // layer shows through.

        // ---- Pass 2: filled contour bands -------------------------------
        const bool bandsVisible = bandSub && bandSub->isVisible();
        if (bandsVisible && maxDepth > dryDepth) {
            using namespace OpenSWMM::Contour;
            // Slice US.2 — class edges come from the sublayer's
            // ClassificationScheme (method-aware: EqualInterval reproduces the
            // legacy even spacing exactly; Quantile/Jenks/StdDev bin against
            // the frame's wet-cell depths). The scheme applies its own custom
            // range internally, so we hand it the data range (dryDepth,
            // maxDepth) unconditionally.
            std::vector<double> levels;
            quint64 schemeRev = 0;
            if (bs) {
                QVector<double> samples;
                const auto m = bs->scheme().method();
                if (m == OpenSWMM::Render::BinMethod::Quantile
                    || m == OpenSWMM::Render::BinMethod::NaturalBreaks
                    || m == OpenSWMM::Render::BinMethod::StdDev) {
                    samples.reserve(nTri);
                    for (int i = 0; i < nTri; ++i)
                        if (tris[i].depth >= dryDepth)
                            samples.push_back(double(tris[i].depth));
                }
                const QVector<double> edges =
                    bs->scheme().levelEdges(dryDepth, maxDepth, samples);
                levels.assign(edges.cbegin(), edges.cend());
                schemeRev = bs->scheme().revision();
            } else {
                levels = evenlySpacedLevelsInclusive(dryDepth, maxDepth, 9);
            }

            std::vector<QSGGeometry::ColoredPoint2D> bandVerts;
            if (levels.size() >= 2) {
                const int bandCount = int(levels.size()) - 1;
                const double bandLo = levels.front();
                const double bandHi = levels.back();
                const qreal bandOp = std::clamp<qreal>(bandSub->opacity(), 0.0, 1.0);
                auto bandColor = [&](int idx) -> QColor {
                    QColor c = bs ? bs->colorForBand(idx, bandCount)
                                  : viridisAt((double(idx) + 0.5) / bandCount);
                    c.setAlphaF(c.alphaF() * bandOp);
                    return c;
                };

                if (!bs || bs->smoothBands()) {
                    const bool cacheHit =
                        m_bandCacheTime  == m_layer->currentTimeIndex()
                        && m_bandCacheLo    == bandLo
                        && m_bandCacheHi    == bandHi
                        && m_bandCacheCount == bandCount
                        && m_bandCacheTris  == size_t(nTri)
                        && m_bandCacheRev   == schemeRev
                        && !m_cachedBands.empty();
                    if (!cacheHit) {
                        const auto extract =
                            [ox, oy](const SWMM2DResultsLayer::SceneTri &t,
                                     QPointF &p0, QPointF &p1, QPointF &p2,
                                     double &v0, double &v1, double &v2) {
                            p0 = QPointF(t.a.x() - ox, t.a.y() - oy);
                            p1 = QPointF(t.b.x() - ox, t.b.y() - oy);
                            p2 = QPointF(t.c.x() - ox, t.c.y() - oy);
                            v0 = double(t.dv0);
                            v1 = double(t.dv1);
                            v2 = double(t.dv2);
                        };
                        m_cachedBands = marchingTrianglesIsobands(
                            tris, levels, extract);
                        m_bandCacheTime  = m_layer->currentTimeIndex();
                        m_bandCacheLo    = bandLo;
                        m_bandCacheHi    = bandHi;
                        m_bandCacheCount = bandCount;
                        m_bandCacheTris  = size_t(nTri);
                        m_bandCacheRev   = schemeRev;
                    }
                    // Per-cell base fill UNDER the smooth bands: guarantees
                    // every wet cell is filled even where the smooth (per-vertex)
                    // pass clips a partially-wet cell short at the shoreline, or
                    // would otherwise leave a hole. Same bucketing/palette as the
                    // flat path below, so the backstop colour matches the band
                    // exactly — seamless, never a transparent hole. Pushed first,
                    // so it lands beneath the smooth polygons. Intentionally NOT
                    // cached (m_cachedBands holds only the smooth geometry): this
                    // pass tracks the live per-cell depth t.depth every frame.
                    bandVerts.reserve(size_t(nTri) * 3 + m_cachedBands.size() * 9);
                    for (int i = 0; i < nTri; ++i) {
                        const auto &t = tris[i];
                        if (t.depth < bandLo) continue;
                        int idx = int(std::upper_bound(
                                          levels.begin() + 1, levels.end() - 1,
                                          double(t.depth))
                                      - (levels.begin() + 1));
                        idx = std::clamp(idx, 0, bandCount - 1);
                        const QColor col = bandColor(idx);
                        const quint8 r = quint8(col.red());
                        const quint8 g = quint8(col.green());
                        const quint8 b = quint8(col.blue());
                        const quint8 a = quint8(col.alpha());
                        for (const QPointF *pt : {&t.a, &t.b, &t.c}) {
                            QSGGeometry::ColoredPoint2D v;
                            v.set(float(pt->x() - ox), float(pt->y() - oy),
                                  r, g, b, a);
                            bandVerts.push_back(v);
                        }
                    }
                    for (const auto &bp : m_cachedBands) {
                        if (bp.verts.size() < 3) continue;
                        const QColor col = bandColor(
                            std::min(bp.bandIndex, bandCount - 1));
                        const quint8 r = quint8(col.red());
                        const quint8 g = quint8(col.green());
                        const quint8 b = quint8(col.blue());
                        const quint8 a = quint8(col.alpha());
                        for (size_t i = 1; i + 1 < bp.verts.size(); ++i) {
                            auto push = [&](const QPointF &p) {
                                QSGGeometry::ColoredPoint2D v;
                                v.set(float(p.x()), float(p.y()), r, g, b, a);
                                bandVerts.push_back(v);
                            };
                            push(bp.verts[0]);
                            push(bp.verts[i]);
                            push(bp.verts[i + 1]);
                        }
                    }
                } else {
                    // Flat per-cell classification — bucket each cell against
                    // the (possibly non-uniform) scheme edges.
                    bandVerts.reserve(size_t(nTri) * 3);
                    for (int i = 0; i < nTri; ++i) {
                        const auto &t = tris[i];
                        if (t.depth < bandLo) continue;
                        int idx = int(std::upper_bound(
                                          levels.begin() + 1, levels.end() - 1,
                                          double(t.depth))
                                      - (levels.begin() + 1));
                        idx = std::clamp(idx, 0, bandCount - 1);
                        const QColor col = bandColor(idx);
                        const quint8 r = quint8(col.red());
                        const quint8 g = quint8(col.green());
                        const quint8 b = quint8(col.blue());
                        const quint8 a = quint8(col.alpha());
                        for (const QPointF *pt : {&t.a, &t.b, &t.c}) {
                            QSGGeometry::ColoredPoint2D v;
                            v.set(float(pt->x() - ox), float(pt->y() - oy),
                                  r, g, b, a);
                            bandVerts.push_back(v);
                        }
                    }
                }
            }
            uploadColoredVerts(bandNode, bandVerts);
        } else {
            uploadColoredVerts(bandNode,
                               std::vector<QSGGeometry::ColoredPoint2D>{});
        }

        // ---- Direct mesh-fill depth passes ------------------------------
        // Fill the triangle mesh directly (no marching-squares) — the
        // seam-free alternative to contour bands. `perVertex=false` flat-fills
        // each cell from its cell-centre depth; `perVertex=true` colours each
        // mesh vertex from its (interpolated) depth and lets the GPU
        // interpolate across the triangle (Gouraud). Continuous samples the
        // ramp by raw value; classified bins against the scheme edges.
        auto buildFillPass =
            [&](const OpenSWMM::Render::ScalarFillStyle *style, qreal subOpacity,
                bool perVertex, std::vector<QSGGeometry::ColoredPoint2D> &out) {
            double vMin = dryDepth, vMax = maxDepth;
            if (style->useCustomRange() && style->rangeMax() > style->rangeMin()) {
                vMin = style->rangeMin();
                vMax = style->rangeMax();
            }
            const bool classified = style->classified();
            std::vector<double> levels;
            int bandCount = 1;
            if (classified) {
                QVector<double> samples;
                const auto mth = style->scheme().method();
                if (mth == OpenSWMM::Render::BinMethod::Quantile
                    || mth == OpenSWMM::Render::BinMethod::NaturalBreaks
                    || mth == OpenSWMM::Render::BinMethod::StdDev) {
                    samples.reserve(nTri);
                    for (int i = 0; i < nTri; ++i)
                        if (tris[i].depth >= dryDepth)
                            samples.push_back(double(tris[i].depth));
                }
                const QVector<double> edges =
                    style->scheme().levelEdges(vMin, vMax, samples);
                levels.assign(edges.cbegin(), edges.cend());
                bandCount = std::max(1, int(levels.size()) - 1);
            }
            const qreal op = std::clamp<qreal>(subOpacity, 0.0, 1.0);
            auto colorAt = [&](float value) -> QColor {
                QColor c;
                if (classified && levels.size() >= 2) {
                    int idx = int(std::upper_bound(levels.begin() + 1,
                                                   levels.end() - 1,
                                                   double(value))
                                  - (levels.begin() + 1));
                    idx = std::clamp(idx, 0, bandCount - 1);
                    c = style->colorForClass(idx, bandCount);
                } else {
                    c = style->colorForValue(double(value), vMin, vMax);
                }
                c.setAlphaF(c.alphaF() * op);
                return c;
            };
            out.reserve(size_t(nTri) * 3);
            auto pushV = [&](const QPointF &p, const QColor &c) {
                QSGGeometry::ColoredPoint2D v;
                v.set(float(p.x() - ox), float(p.y() - oy),
                      quint8(c.red()), quint8(c.green()),
                      quint8(c.blue()), quint8(c.alpha()));
                out.push_back(v);
            };
            for (int i = 0; i < nTri; ++i) {
                const auto &t = tris[i];
                if (t.depth < dryDepth) continue;  // dry → terrain shows through
                if (perVertex) {
                    pushV(t.a, colorAt(t.dv0));
                    pushV(t.b, colorAt(t.dv1));
                    pushV(t.c, colorAt(t.dv2));
                } else {
                    const QColor c = colorAt(t.depth);
                    pushV(t.a, c); pushV(t.b, c); pushV(t.c, c);
                }
            }
        };

        {
            auto *cellSub = m_layer->cellDepthFillSublayer();
            std::vector<QSGGeometry::ColoredPoint2D> cellVerts;
            if (cellSub && cellSub->isVisible() && cellSub->fillStyle()
                && maxDepth > dryDepth)
                buildFillPass(cellSub->fillStyle(), cellSub->opacity(),
                              /*perVertex=*/false, cellVerts);
            uploadColoredVerts(cellFillNode, cellVerts);

            auto *smoothSub = m_layer->smoothDepthFillSublayer();
            std::vector<QSGGeometry::ColoredPoint2D> smoothVerts;
            if (smoothSub && smoothSub->isVisible() && smoothSub->fillStyle()
                && maxDepth > dryDepth)
                buildFillPass(smoothSub->fillStyle(), smoothSub->opacity(),
                              /*perVertex=*/true, smoothVerts);
            uploadColoredVerts(smoothFillNode, smoothVerts);
            if (kUpnDebug)
                qDebug("[2D-qsg]   rebuilt t=%d: cellVerts=%zu smoothVerts=%zu "
                       "(maxDepth=%g dryDepth=%g)",
                       m_layer->currentTimeIndex(), cellVerts.size(),
                       smoothVerts.size(), maxDepth, dryDepth);
        }

        // ---- Pass 3 + 3b: isolines, index contours, labels ---------------
        const bool isoVisible = isoSub && isoSub->isVisible();
        if (isoVisible && maxDepth > dryDepth) {
            using namespace OpenSWMM::Contour;
            QVector<double> isoSamples;
            if (is) {
                const auto m = is->scheme().method();
                if (m == OpenSWMM::Render::BinMethod::Quantile
                    || m == OpenSWMM::Render::BinMethod::NaturalBreaks
                    || m == OpenSWMM::Render::BinMethod::StdDev) {
                    isoSamples.reserve(nTri);
                    for (int i = 0; i < nTri; ++i)
                        if (tris[i].depth >= dryDepth)
                            isoSamples.push_back(double(tris[i].depth));
                }
            }
            const std::vector<double> levels = is
                ? is->levelsForRange(dryDepth, maxDepth, isoSamples)
                : evenlySpacedLevels(dryDepth, maxDepth, 8);

            if (!levels.empty()) {
                // Params hash for the segment cache.
                quint64 params = 0;
                if (is) {
                    params = quint64(int(is->levelMode())) * 1315423911ull
                           ^ quint64(is->isoValueCount()) * 2654435761ull
                           ^ quint64(std::llround(is->levelInterval() * 1e9))
                           ^ (quint64(std::llround(is->baseLevel() * 1e9)) << 1)
                           // Slice US.2 — fold in the classification scheme so a
                           // method / class-count change re-marches.
                           ^ (is->scheme().revision() * 2246822519ull);
                }
                const bool cacheHit =
                    m_isoCacheTime == m_layer->currentTimeIndex()
                    && m_isoCacheLo     == dryDepth
                    && m_isoCacheHi     == maxDepth
                    && m_isoCacheParams == params
                    && m_isoCacheTris   == size_t(nTri)
                    && !m_cachedSegs.empty();
                if (!cacheHit) {
                    const auto extract =
                        [ox, oy](const SWMM2DResultsLayer::SceneTri &t,
                                 QPointF &p0, QPointF &p1, QPointF &p2,
                                 double &v0, double &v1, double &v2) {
                        p0 = QPointF(t.a.x() - ox, t.a.y() - oy);
                        p1 = QPointF(t.b.x() - ox, t.b.y() - oy);
                        p2 = QPointF(t.c.x() - ox, t.c.y() - oy);
                        v0 = double(t.dv0);
                        v1 = double(t.dv1);
                        v2 = double(t.dv2);
                    };
                    m_cachedSegs = marchingTriangles(tris, levels, extract);
                    m_isoCacheTime   = m_layer->currentTimeIndex();
                    m_isoCacheLo     = dryDepth;
                    m_isoCacheHi     = maxDepth;
                    m_isoCacheParams = params;
                    m_isoCacheTris   = size_t(nTri);
                }
                const auto &segs = m_cachedSegs;

                // Index-contour predicate (same convention as the CPU pass).
                const int idxEvery = is ? is->indexEvery() : 0;
                QHash<double, int> ordinalOf;
                for (int i = 0; i < int(levels.size()); ++i)
                    ordinalOf.insert(levels[i], i);
                auto isIndexLevel = [&](double level) -> bool {
                    if (!is || idxEvery < 2) return false;
                    if (is->levelMode() ==
                        OpenSWMM::Render::IsolineStyle::LevelMode::FixedInterval) {
                        const long k = std::lround(
                            (level - is->baseLevel())
                            / std::max(is->levelInterval(), 1e-12));
                        return (k % idxEvery) == 0;
                    }
                    return ((ordinalOf.value(level, 0) + 1) % idxEvery) == 0;
                };

                const double widthPx = is ? std::max(0.5, is->lineWidthPx()) : 1.0;
                const double indexPx = is ? std::max(widthPx, is->indexWidthPx())
                                          : widthPx;
                const float lineHW  = float(0.5 * widthPx) * invView;
                const float indexHW = float(0.5 * indexPx) * invView;

                std::vector<QSGGeometry::Point2D> lineVerts, indexVerts;
                lineVerts.reserve(segs.size() * 6);
                for (const auto &s : segs) {
                    if (isIndexLevel(s.level))
                        appendThickSeg(indexVerts,
                                       float(s.a.x()), float(s.a.y()),
                                       float(s.b.x()), float(s.b.y()), indexHW);
                    else
                        appendThickSeg(lineVerts,
                                       float(s.a.x()), float(s.a.y()),
                                       float(s.b.x()), float(s.b.y()), lineHW);
                }
                uploadFlatVerts(isoNode, lineVerts);
                uploadFlatVerts(isoIndexNode, indexVerts);

                QColor isoColor = is ? is->color() : QColor(10, 10, 10, 220);
                isoColor.setAlpha(int(qBound(0.0,
                    isoColor.alpha() * isoSub->opacity(), 255.0)));
                setFlatColor(isoNode, isoColor);
                setFlatColor(isoIndexNode, isoColor);

                // ---- Pass 3b: along-line labels ----------------------
                while (auto *c = isoLabels->firstChild()) {
                    isoLabels->removeChildNode(c);
                    delete c;
                }
                if (is && is->labels() && window()) {
                    QHash<double, QVector<QLineF>> byLevel;
                    for (const auto &s : segs)
                        byLevel[s.level].append(QLineF(s.a, s.b));

                    const double dpr = window()->effectiveDevicePixelRatio();
                    const int  decimals = is->labelDecimals();
                    const bool halo     = is->labelHalo();
                    const double fontPt = is->labelFontPt();
                    // Spacing measured in screen px, converted to the
                    // anchor-shifted scene units the chains live in.
                    const double spacingScene = 250.0 * double(invView);
                    const QRectF &bb = m_layer->m_sceneBBox;
                    const double quantum = 1e-9 * std::max(
                        {bb.width(), bb.height(), 1.0});

                    for (auto it = byLevel.constBegin();
                         it != byLevel.constEnd(); ++it) {
                        const QString text =
                            QString::number(it.key(), 'f', decimals);
                        const QString cacheKey = text
                            + QLatin1Char('|') + QString::number(fontPt)
                            + QLatin1Char('|') + QString::number(int(halo))
                            + QLatin1Char('|')
                            + (is->color().name(QColor::HexArgb));

                        QSGTexture *tex =
                            m_labelTextureCache.value(cacheKey, nullptr);
                        if (!tex) {
                            const QImage img = rasteriseLabel(
                                text, is->color(), fontPt, halo, dpr);
                            tex = window()->createTextureFromImage(
                                img, QQuickWindow::TextureHasAlphaChannel);
                            if (tex) m_labelTextureCache.insert(cacheKey, tex);
                        }
                        if (!tex) continue;

                        const QSizeF szPx =
                            tex->textureSize() / dpr;       // logical px
                        const double wScene = szPx.width()  * double(invView);
                        const double hScene = szPx.height() * double(invView);

                        const auto chains = OpenSWMM::Contour::chainIsoSegments(
                            it.value(), quantum);
                        for (const QPolygonF &chain : chains) {
                            if (chain.size() < 2) continue;
                            double total = 0.0;
                            for (int i = 1; i < chain.size(); ++i)
                                total += QLineF(chain[i-1], chain[i]).length();
                            if (total < wScene * 1.5) continue;

                            double acc    = 0.0;
                            double nextAt = std::max(spacingScene * 0.5, wScene);
                            for (int i = 1; i < chain.size(); ++i) {
                                const QPointF a = chain[i - 1];
                                const QPointF b = chain[i];
                                const double segLen = QLineF(a, b).length();
                                if (segLen <= 0.0) continue;
                                while (acc + segLen >= nextAt) {
                                    const double tf = (nextAt - acc) / segLen;
                                    const QPointF pos = a + tf * (b - a);
                                    // Scene coords are y-down (scene y =
                                    // -world y) and the item transform
                                    // preserves orientation, so the screen
                                    // angle equals the scene angle.
                                    double angle = qRadiansToDegrees(
                                        std::atan2(b.y() - a.y(),
                                                   b.x() - a.x()));
                                    if (angle >  90.0) angle -= 180.0;
                                    if (angle < -90.0) angle += 180.0;

                                    auto *tnWrap = new QSGTransformNode();
                                    QMatrix4x4 m;
                                    m.translate(float(pos.x()), float(pos.y()));
                                    m.rotate(float(angle), 0.0f, 0.0f, 1.0f);
                                    tnWrap->setMatrix(m);

                                    auto *tn = new QSGSimpleTextureNode();
                                    tn->setTexture(tex);
                                    tn->setOwnsTexture(false);
                                    tn->setRect(QRectF(-0.5 * wScene,
                                                       -0.5 * hScene,
                                                       wScene, hScene));
                                    tn->setFiltering(QSGTexture::Linear);
                                    tnWrap->appendChildNode(tn);
                                    isoLabels->appendChildNode(tnWrap);
                                    nextAt += spacingScene;
                                }
                                acc += segLen;
                            }
                        }
                    }
                }
            } else {
                uploadFlatVerts(isoNode, std::vector<QSGGeometry::Point2D>{});
                uploadFlatVerts(isoIndexNode, std::vector<QSGGeometry::Point2D>{});
                while (auto *c = isoLabels->firstChild()) {
                    isoLabels->removeChildNode(c);
                    delete c;
                }
            }
        } else {
            uploadFlatVerts(isoNode, std::vector<QSGGeometry::Point2D>{});
            uploadFlatVerts(isoIndexNode, std::vector<QSGGeometry::Point2D>{});
            while (auto *c = isoLabels->firstChild()) {
                isoLabels->removeChildNode(c);
                delete c;
            }
        }

        // ---- Pass 4: velocity-vector glyphs ------------------------------
        const bool velVisible = velSub && velSub->isVisible()
                                && m_layer->hasVelocityData();
        if (velVisible) {
            const double dryCut = vs
                ? std::max(vs->dryDepthCutoff(), dryDepth) : dryDepth;
            const double maxVel = std::max(m_layer->maxVelocity(), 1e-6);
            const double vmagSkip = std::max(maxVel * 1e-4, 1e-9);
            const qreal velOp = std::clamp<qreal>(velSub->opacity(), 0.0, 1.0);
            const float shaftHW = float(0.5 * (vs ? vs->shaftWidthPx() : 1.5))
                                  * invView;

            std::vector<QSGGeometry::ColoredPoint2D> glyphVerts;

            // Emit one arrow glyph centred at scene point (cxs,cys) for velocity
            // (gvx,gvy), |v| = gvmag. Shared by the grid-sampled (V2) and the
            // dense per-cell paths so both render identical glyphs.
            auto emitGlyphAt = [&](double cxs, double cys,
                                   float gvx, float gvy, float gvmag) {
                if (gvmag <= 0.0f) return;
                const double lenPx = vs
                    ? vs->glyphLengthPxForSpeed(gvmag)
                    : m_layer->velocityArrowScale()
                          * std::log1p(std::clamp(double(gvmag) / maxVel, 0.0, 1.0))
                          / std::log1p(1.0);
                const double lenScene = lenPx * double(invView);
                if (lenScene <= 0.0) return;

                QColor col = vs ? vs->colorForSpeed(gvmag)
                                : QColor(20, 20, 20, 220);
                const quint8 r = quint8(col.red());
                const quint8 g = quint8(col.green());
                const quint8 b = quint8(col.blue());
                const quint8 a = quint8(qBound(0,
                    int(col.alpha() * velOp + 0.5), 255));

                const double inv_vmag = 1.0 / double(gvmag);
                const float ux = float(gvx * inv_vmag);
                const float uy = float(gvy * inv_vmag);
                const float tailX = float(cxs - ox);
                const float tailY = float(cys - oy);
                const float lenS  = float(lenScene);

                // Arrowhead length along the axis, capped so the shaft (tail)
                // always stays clearly visible (head ≤ 60% of the glyph).
                const double headLenPx = vs ? vs->headSizePx() : 7.0;
                const float  hLen = float(std::min(headLenPx * double(invView),
                                                   lenScene * 0.6));
                const float tipX  = tailX + ux * lenS;             // arrow tip
                const float tipY  = tailY + uy * lenS;
                const float baseX = tailX + ux * (lenS - hLen);    // head base
                const float baseY = tailY + uy * (lenS - hLen);

                // Shaft: tail → arrowhead base, so the tail reads as a line.
                appendThickSegColored(glyphVerts, tailX, tailY, baseX, baseY,
                                      shaftHW, r, g, b, a);

                // Sharp filled-triangle arrowhead (tip + two base corners).
                const float px = -uy, py = ux;          // unit perpendicular
                const float hHalf = hLen * 0.55f;       // arrowhead half-width
                auto cv = [&](float x, float y) {
                    QSGGeometry::ColoredPoint2D p; p.set(x, y, r, g, b, a); return p; };
                glyphVerts.push_back(cv(tipX, tipY));
                glyphVerts.push_back(cv(baseX + px * hHalf, baseY + py * hHalf));
                glyphVerts.push_back(cv(baseX - px * hHalf, baseY - py * hHalf));
            };

            const double spacingPx = vs ? vs->glyphSpacingPx() : 30.0;
            if (spacingPx > 1.0) {
                // V2 (Issue 5) — sample the interpolated per-vertex velocity
                // field (velocityAtScene) on a uniform SCREEN-SPACE grid, so
                // arrows are evenly spaced and independent of mesh density (no
                // empty patches over coarse triangles) and their direction
                // varies smoothly across cell boundaries. Replaces the old
                // one-arrow-per-grid-cell-keyed-to-a-centroid placement, which
                // left grid cells containing no centroid empty.
                const QRectF &bb = m_layer->m_sceneBBox;
                const double x0 = std::max(cullX0, bb.left());
                const double y0 = std::max(cullY0, bb.top());
                const double x1 = std::min(cullX1, bb.right());
                const double y1 = std::min(cullY1, bb.bottom());
                const double gridStep = spacingPx * double(invView);
                if (gridStep > 0.0 && x1 > x0 && y1 > y0) {
                    for (double gy = y0; gy <= y1; gy += gridStep) {
                        for (double gx = x0; gx <= x1; gx += gridStep) {
                            float svx, svy;
                            if (!m_layer->velocityAtScene(QPointF(gx, gy),
                                                          svx, svy))
                                continue;
                            const float smag = std::sqrt(svx * svx + svy * svy);
                            if (smag < vmagSkip) continue;
                            emitGlyphAt(gx, gy, svx, svy, smag);
                        }
                    }
                }
            } else {
                // Dense per-cell mode (spacing ≤ 1 px): one arrow at each wet
                // cell centroid, using the cell's own RT0 vector.
                for (int i = 0; i < nTri; ++i) {
                    const auto &t = tris[i];
                    if (t.depth < dryCut)  continue;
                    if (t.vmag < vmagSkip) continue;
                    const double cxp = t.centroid.x();
                    const double cyp = t.centroid.y();
                    if (cxp < cullX0 || cxp > cullX1 ||
                        cyp < cullY0 || cyp > cullY1) continue;
                    emitGlyphAt(cxp, cyp, t.vx, t.vy, t.vmag);
                }
            }
            uploadColoredVerts(velNode, glyphVerts);
        } else {
            uploadColoredVerts(velNode,
                               std::vector<QSGGeometry::ColoredPoint2D>{});
        }

        // ---- Pass 3c: mesh wireframe overlay (MeshEdgeSublayer) ----------
        // Issue 3 — GPU mirror of the CPU pass, now iterating the DEDUPLICATED
        // edge set (m_layer->m_sceneEdges) so each undirected edge is emitted
        // exactly once. Previously every triangle emitted all three of its edges
        // as filled translucent quads, so each shared edge was rasterised twice
        // (and stacked 5–7× around each vertex); the overlapping alpha
        // composited the wireframe into a dark wash — the reported "dark
        // artifact". The results layer uses a single uniform width + flat colour
        // (the slope-emphasis thin/wide split needs per-edge colour, so that
        // style group is hidden for results — see Swmm2DResultsStylePanel).
        if (auto *edgeSub = m_layer->meshEdgeSublayer();
            edgeSub && edgeSub->isVisible())
        {
            const auto *es = edgeSub->edgeStyle();
            QColor col     = es ? es->color() : QColor(0, 0, 0, 130);
            const double w = es ? std::max(0.1, es->lineWidthPx()) : 0.5;
            const qreal op = std::clamp<qreal>(edgeSub->opacity(), 0.0, 1.0);
            col.setAlpha(int(qBound(0.0, col.alpha() * op, 255.0)));
            const float hw = float(0.5 * w) * invView;

            const auto &edges = m_layer->m_sceneEdges;
            std::vector<QSGGeometry::Point2D> edgeSegs;
            edgeSegs.reserve(size_t(edges.size()) * 6);
            for (const auto &e : edges) {
                const double minX = std::min(e.a.x(), e.b.x());
                const double maxX = std::max(e.a.x(), e.b.x());
                const double minY = std::min(e.a.y(), e.b.y());
                const double maxY = std::max(e.a.y(), e.b.y());
                if (maxX < cullX0 || minX > cullX1 ||
                    maxY < cullY0 || minY > cullY1) continue;
                appendThickSeg(edgeSegs,
                               float(e.a.x() - ox), float(e.a.y() - oy),
                               float(e.b.x() - ox), float(e.b.y() - oy), hw);
            }
            uploadFlatVerts(edgeNode, edgeSegs);
            setFlatColor(edgeNode, col);
        } else {
            uploadFlatVerts(edgeNode, std::vector<QSGGeometry::Point2D>{});
        }

        // ---- Pass 3d: mesh-vertex markers (MeshNodeSublayer) ------------
        // GPU mirror of the CPU/QPainter pass (swmm2dresultslayer.cpp Pass 5):
        // a filled marker at every triangle corner, sized by markerSizePx. The
        // CPU pass draws an ellipse; here a centred quad — visually identical
        // at marker sizes ≤8 px — and a single flat colour (the results layer
        // does not consult the tagged-vertex knobs). Corners repeat across
        // shared vertices, matching the CPU pass.
        if (auto *nodeSub = m_layer->meshNodeSublayer();
            nodeSub && nodeSub->isVisible())
        {
            const auto *ns = nodeSub->nodeStyle();
            QColor col      = ns ? ns->color() : QColor(40, 40, 40, 220);
            const double sz = ns ? std::max(0.5, ns->markerSizePx()) : 3.0;
            const qreal op  = std::clamp<qreal>(nodeSub->opacity(), 0.0, 1.0);
            col.setAlpha(int(qBound(0.0, col.alpha() * op, 255.0)));
            const float r = float(0.5 * sz) * invView;

            std::vector<QSGGeometry::Point2D> markVerts;
            markVerts.reserve(size_t(nTri) * 3 * 6);
            auto emitQuad = [&](float cx, float cy) {
                auto V = [](float x, float y) {
                    QSGGeometry::Point2D p; p.x = x; p.y = y; return p; };
                markVerts.push_back(V(cx - r, cy - r));
                markVerts.push_back(V(cx + r, cy - r));
                markVerts.push_back(V(cx - r, cy + r));
                markVerts.push_back(V(cx + r, cy - r));
                markVerts.push_back(V(cx + r, cy + r));
                markVerts.push_back(V(cx - r, cy + r));
            };
            for (int i = 0; i < nTri; ++i) {
                const auto &t = tris[i];
                const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
                const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
                const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
                const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
                if (maxX < cullX0 || minX > cullX1 ||
                    maxY < cullY0 || minY > cullY1) continue;
                emitQuad(float(t.a.x()-ox), float(t.a.y()-oy));
                emitQuad(float(t.b.x()-ox), float(t.b.y()-oy));
                emitQuad(float(t.c.x()-ox), float(t.c.y()-oy));
            }
            uploadFlatVerts(nodeMarkNode, markVerts);
            setFlatColor(nodeMarkNode, col);
        } else {
            uploadFlatVerts(nodeMarkNode, std::vector<QSGGeometry::Point2D>{});
        }

        // ---- Pass 5 (flow-direction arrows): removed (2026-06-21,
        // redundant with velocity vectors, which already convey direction).

        // ---- Pass 6: cell-highlight overlay (CF.3) ------------------------
        {
            const QSet<int> &hi = m_layer->highlightedCells();
            std::vector<QSGGeometry::Point2D> fillVertsH, edgeVertsH;
            if (!hi.isEmpty()) {
                const float edgeHW = 1.0f * invView;   // ~2 px outline
                for (int idx : hi) {
                    if (idx < 0 || idx >= nTri) continue;
                    const auto &t = tris[idx];
                    auto P = [&](const QPointF &p) {
                        QSGGeometry::Point2D q;
                        q.x = float(p.x() - ox);
                        q.y = float(p.y() - oy);
                        return q;
                    };
                    fillVertsH.push_back(P(t.a));
                    fillVertsH.push_back(P(t.b));
                    fillVertsH.push_back(P(t.c));
                    const QSGGeometry::Point2D a = P(t.a), b = P(t.b), c = P(t.c);
                    appendThickSeg(edgeVertsH, a.x, a.y, b.x, b.y, edgeHW);
                    appendThickSeg(edgeVertsH, b.x, b.y, c.x, c.y, edgeHW);
                    appendThickSeg(edgeVertsH, c.x, c.y, a.x, a.y, edgeHW);
                }
            }
            uploadFlatVerts(hiFillNode, fillVertsH);
            uploadFlatVerts(hiEdgeNode, edgeVertsH);
            setFlatColor(hiFillNode, kHiFillColor);
            setFlatColor(hiEdgeNode, kHiEdgeColor);
        }

        m_contentDirty = false;
    }

    applyTransform();
    return root;
}
