/*!
 * \file   swmm2dresultsqsgrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * VS.8 — GPU scene-graph renderer for SWMM2DResultsLayer. See the header
 * for the pass list. Structure mirrors SWMM2DMeshQSGRenderer (the terrain
 * template): anchor-shifted float vertices, pure pan = matrix-only update.
 *
 * QSG-2D-1M (2026-07-05) — re-architected for ~1M-cell meshes: dirty
 * domains (Qsg2DDirtyState), a deterministic LOD policy (Qsg2DLodPolicy),
 * chunked render culling (MeshRenderChunkIndex), coverage-rect content
 * builds, persistent indexed geometry for the smooth depth fill
 * (MeshStaticGeometryBuffers), and OPENSWMM_RENDER_PERF=1 sync stats.
 */
#include "map/swmm2dresultsqsgrenderer.h"

#include "contour/contourchain.h"
#include "contour/marchingtriangles.h"
#include "layers/swmm2dresultslayer.h"
#include "render/qsg2drenderstats.h"
#include "render/qsgpremultiply.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshnodesublayer.h"
#include "render/sublayers/velocityvectorsublayer.h"
#include "render/sublayers/resultscalarsublayer.h"
#include <QSet>

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
#include <QtConcurrent/QtConcurrentRun>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

using OpenSWMM::Render::ContourJobInput;
using OpenSWMM::Render::ContourJobOutput;
using OpenSWMM::Render::MeshRenderChunkIndex;
using OpenSWMM::Render::Qsg2DDirtyState;
using OpenSWMM::Render::Qsg2DLodDecision;
using OpenSWMM::Render::Qsg2DLodInputs;
using OpenSWMM::Render::Qsg2DLodPolicy;
using OpenSWMM::Render::Qsg2DRenderStats;
using OpenSWMM::Render::computeContourJob;
using OpenSWMM::Render::premul;

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

// Per-geometry-node vertex cap. Qt's scene-graph batch renderer addresses
// batched vertices with 16-bit indices, so a single node holding more than
// ~65k vertices wraps those indices and draws garbage triangles spanning
// unrelated primitives — long slender triangles connecting points that are
// nowhere near each other. SWMM2DMeshQSGRenderer (§kMaxVertsPerNode) and
// SWMM2DLayerQSGRenderer (§kMaxPerNode) already cap their nodes; the results
// renderer did not, so its edge, isoline, band, cell-fill, velocity and
// highlight passes all wrapped as soon as they crossed the cap — which on a
// real mesh is immediately.
//
// The indexed smooth fill is immune (its UnsignedIntType index buffer makes
// the batch renderer refuse to merge it), which is why the artifact appeared
// in the edges and contours but not there.
//
// 65532 is the largest value below 2^16 - 1 that is a multiple of BOTH 3
// (bare triangles) and 6 (thick-segment quads), so a chunk boundary never
// splits a primitive.
constexpr int kMaxVertsPerNode = 65532;

/*! Upload one chunk of \p n vertices into \p geo.
 *  Reallocates when the vertex count changes or the geometry still carries
 *  indices from a previous indexed upload (the smooth-fill node alternates
 *  between the indexed and expanded paths). */
template <typename V>
void uploadChunk(QSGGeometry *geo, const V *data, int n, size_t stride)
{
    if (geo->vertexCount() != n || geo->indexCount() != 0) geo->allocate(n);
    if (n > 0)
        std::memcpy(geo->vertexData(), data, size_t(n) * stride);
}

/*! Chunked upload: the first kMaxVertsPerNode vertices land in \p node
 *  itself; the rest spill into child geometry nodes (created on demand via
 *  \p makeNode, excess children pruned). Works for both vertex layouts. */
template <typename V, typename MakeNode>
void uploadVertsChunked(QSGGeometryNode *node, const std::vector<V> &verts,
                        MakeNode makeNode)
{
    const int total  = int(verts.size());
    const int inRoot = std::min(total, kMaxVertsPerNode);
    // An element whose geometry was EMPTY has no batch, and DirtyGeometry on
    // an unbatched element is ignored by Qt's batch renderer — the node would
    // never paint once it fills. DirtyMaterial is what requests a batch
    // rebuild for it (same fix as SWMMLayerQSGRenderer's uploadVerts).
    auto dirtyFor = [](bool wasEmpty, int n) {
        return (wasEmpty && n > 0)
            ? (QSGNode::DirtyGeometry | QSGNode::DirtyMaterial)
            : QSGNode::DirtyState(QSGNode::DirtyGeometry);
    };
    const bool rootWasEmpty = node->geometry()->vertexCount() == 0;
    uploadChunk(node->geometry(), verts.data(), inRoot, sizeof(V));
    node->markDirty(dirtyFor(rootWasEmpty, inRoot));

    int offset = inRoot;
    QSGNode *child = node->firstChild();
    while (offset < total) {
        QSGGeometryNode *cg;
        if (child) {
            cg    = static_cast<QSGGeometryNode *>(child);
            child = child->nextSibling();
        } else {
            cg = makeNode();
            node->appendChildNode(cg);
        }
        const int n = std::min(kMaxVertsPerNode, total - offset);
        const bool wasEmpty = cg->geometry()->vertexCount() == 0;
        uploadChunk(cg->geometry(), verts.data() + offset, n, sizeof(V));
        cg->markDirty(dirtyFor(wasEmpty, n));
        offset += n;
    }
    while (child) {   // shrink: drop no-longer-needed overflow nodes
        QSGNode *next = child->nextSibling();
        node->removeChildNode(child);
        delete child;
        child = next;
    }
}

void uploadColoredVerts(QSGGeometryNode *node,
                        const std::vector<QSGGeometry::ColoredPoint2D> &verts)
{
    uploadVertsChunked(node, verts, []() { return makeColoredNode(); });
}

void uploadFlatVerts(QSGGeometryNode *node,
                     const std::vector<QSGGeometry::Point2D> &verts)
{
    // Overflow children clone the pass node's current flat colour; a later
    // colour change reaches them through setFlatColor below.
    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    const QColor c = mat->color();
    uploadVertsChunked(node, verts, [c]() { return makeFlatNode(c); });
}

void setFlatColor(QSGGeometryNode *node, QColor c)
{
    auto *mat = static_cast<QSGFlatColorMaterial*>(node->material());
    if (mat->color() != c) { mat->setColor(c); node->markDirty(QSGNode::DirtyMaterial); }
    // Keep overflow chunks (see uploadVertsChunked) in the same colour.
    for (QSGNode *ch = node->firstChild(); ch; ch = ch->nextSibling()) {
        auto *cg = static_cast<QSGGeometryNode *>(ch);
        auto *cm = static_cast<QSGFlatColorMaterial *>(cg->material());
        if (cm->color() != c) {
            cm->setColor(c);
            cg->markDirty(QSGNode::DirtyMaterial);
        }
    }
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
    QHash<QString, QSGGeometryNode *> resultScalarNodes;
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

/*! Phase 7 — async contour recomputation gate. Defaults to meshes big
 *  enough for the per-tick marching to be felt (≥ kAsyncContourThreshold
 *  cells); OPENSWMM_QSG_ASYNC_CONTOURS=1 forces it on at any size (useful
 *  for testing on small demos), =0 forces the synchronous path. */
constexpr int kAsyncContourThreshold = 50'000;
bool asyncContoursEnabled(int nTri)
{
    static const int kMode =
        qEnvironmentVariableIsSet("OPENSWMM_QSG_ASYNC_CONTOURS")
            ? qEnvironmentVariableIntValue("OPENSWMM_QSG_ASYNC_CONTOURS")
            : -1;
    if (kMode == 0) return false;
    if (kMode >= 1) return true;
    return nTri >= kAsyncContourThreshold;
}

} // namespace

// ---------------------------------------------------------------------------

struct SWMM2DResultsQSGRenderer::ContourFrame {
    ContourJobKey key;
    std::shared_ptr<const std::vector<std::array<float, 3>>> scalars;
    // Only dynamic attributes are retained; geometry stays shared by revision.
    std::vector<std::array<float, 4>> cellValues; // depth, vx, vy, speed per display tri
    SWMM2DResultsLayer::VelocityField velocity;
    double maxVelocity = 0;
    bool hasVelocity = false;
    ContourJobOutput contours;
};

SWMM2DResultsQSGRenderer::SWMM2DResultsQSGRenderer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    connect(&m_contourWatcher, &QFutureWatcherBase::finished, this, [this]() {
        m_contourBusy = false;
        const auto result = m_contourWatcher.result();
        // Accept completed intermediate frames during fast playback, but never
        // cross a geometry/style epoch or replace a newer synchronous frame.
        if (result && result->key.compatible(m_requestedContourKey)
            && (!m_contourFrame || result->key.frameRev > m_contourFrame->key.frameRev))
            m_contourFrame = result;
        m_dirty.noteDataChanged();
        noteContentChanged();
        emit contentReady(); // also schedules the coalesced latest request
    });
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
    if (m_layer) {
        for (auto *base : m_layer->sublayers())
            if (auto *sub = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(base))
                sub->publishPresentedFrame({});
        QObject::disconnect(m_layer, nullptr, this, nullptr);
    }
    m_layer = layer;
    if (m_layer) {
        // Ambiguous invalidation — style edits, sublayer toggles, etc.
        // funnel into repaintRequested. updatePaintNode classifies it into
        // the narrowest dirty domain by diffing snapshots (geometry
        // revision / highlight set / time index).
        connect(m_layer, &OpenSWMMVisLayer::repaintRequested, this,
                [this]() {
                    m_dirty.noteExternalChanged();
                    noteContentChanged();
                });
        // Animation ticks land via currentTimeChanged (setCurrentTimeIndex
        // does not emit repaintRequested) — a pure Data-domain event.
        connect(m_layer, &SWMM2DResultsLayer::currentTimeChanged, this,
                [this](int) {
                    m_dirty.noteDataChanged();
                    noteContentChanged();
                });
        // Selection-domain event: rebuilds ONLY the highlight overlay.
        connect(m_layer, &SWMM2DResultsLayer::highlightedCellsChanged,
                this, [this]() {
                    m_dirty.noteSelectionChanged();
                    noteContentChanged();
                });
    }
    // Per-layer derived state is invalid for the new layer.
    m_chunksRev = ~quint64(0);
    m_chunks.clear();
    m_lastGeomRev = ~quint64(0);
    m_lastHighlight.clear();
    forceRebuild();
}

void SWMM2DResultsQSGRenderer::setMapExtent(const MapExtent &extent)
{
    if (extent == m_extent) return;
    const bool zoomChanged =
        !qFuzzyCompare(extent.width(),  m_extent.width()) ||
        !qFuzzyCompare(extent.height(), m_extent.height());
    m_extent = extent;
    // Content stays untouched here: updatePaintNode decides — via the LOD
    // key and the coverage rect — whether this extent change needs any
    // rebuild at all. Pure pans and same-LOD zooms end up matrix-only.
    m_dirty.noteExtentChanged(zoomChanged);
    update();
}

void SWMM2DResultsQSGRenderer::forceRebuild()
{
    clearLabelTextureCache();
    ++m_contourEpoch;
    m_requestedContourKey.epoch = m_contourEpoch;
    m_contourFrame.reset();
    m_contourPositions.reset();
    m_builtCoverage  = QRectF();
    m_builtLodKey    = ~quint64(0);
    m_dirty.noteLayerChanged();
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
        qDebug("[2D-qsg] sync: layer=%p vis=%d t=%d lastT=%d pending=0x%x "
               "extentValid=%d itemWH=%gx%g",
               static_cast<void *>(m_layer),
               m_layer ? int(m_layer->isVisible()) : -1,
               m_layer ? m_layer->currentTimeIndex() : -999, m_lastRenderedTime,
               m_dirty.pending(), int(m_extent.isValid()), width(), height());

    auto clearPresentedFrames = [&]() {
        if (!m_layer) return;
        for (auto *base : m_layer->sublayers())
            if (auto *sub = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(base))
                sub->publishPresentedFrame({});
    };

    if (!m_layer || !m_layer->isVisible() || !m_extent.isValid()
        || width() <= 0 || height() <= 0) {
        if (kUpnDebug) qDebug("[2D-qsg]   -> null-return (gate 1)");
        clearPresentedFrames();
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
        clearPresentedFrames();
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
        // deleted) — every domain is stale regardless of pending notes.
        m_dirty.noteLayerChanged();
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
    const QRectF viewRect(QPointF(cullX0, cullY0), QPointF(cullX1, cullY1));

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
        clearPresentedFrames();
        const std::vector<QSGGeometry::ColoredPoint2D> empty_c;
        const std::vector<QSGGeometry::Point2D>        empty_p;
        uploadColoredVerts(cellFillNode, empty_c);
        uploadColoredVerts(smoothFillNode, empty_c);
        uploadColoredVerts(bandNode, empty_c);
        for (auto *node : root->resultScalarNodes) uploadColoredVerts(node, empty_c);
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
        QSet<QString> currentResultIds;
        for (auto *sub : m_layer->sublayers()) {
            if (!qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(sub)) continue;
            currentResultIds.insert(sub->id());
            if (!root->resultScalarNodes.contains(sub->id()))
                root->resultScalarNodes.insert(sub->id(), makeColoredNode());
        }
        for (auto it = root->resultScalarNodes.begin(); it != root->resultScalarNodes.end();) {
            if (currentResultIds.contains(it.key())) { ++it; continue; }
            if (it.value()->parent()) it.value()->parent()->removeChildNode(it.value());
            delete it.value();
            it = root->resultScalarNodes.erase(it);
        }
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
            if (qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(sub)) {
                appendNode(root->resultScalarNodes.value(id));
            } else if (id == QLatin1String("results2d.cellDepthFill")) {
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

    // Early-out if bbox entirely outside view. Pending dirty notes are NOT
    // consumed; invalidating the coverage/LOD key guarantees a pan-back
    // resolves to an Lod rebuild.
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
            m_builtCoverage = QRectF();
            m_builtLodKey   = ~quint64(0);
            applyTransform();
            return root;
        }
    }

    // ---- QSG-2D-1M: LOD decision + dirty-domain resolution ------------------
    const auto  &tris = m_layer->m_sceneTris;
    const int    nTri = int(tris.size());
    const QRectF &bb  = m_layer->m_sceneBBox;
    // Display triangle → owning cell (a quad owns two consecutive fan
    // triangles); identity when the layer has no map yet.
    const auto  &triCellMap = m_layer->triCellMap();
    const bool   haveTriCellMap = int(triCellMap.size()) == nTri;
    auto cellOfTri = [&](int i) {
        return haveTriCellMap ? triCellMap[size_t(i)] : i;
    };

    auto *bandSub  = m_layer->contourBandSublayer();
    auto *isoSub   = m_layer->isolineSublayer();
    auto *velSub   = m_layer->velocityVectorSublayer();
    auto *edgeSub  = m_layer->meshEdgeSublayer();
    auto *nodeSub  = m_layer->meshNodeSublayer();
    auto *cellSub  = m_layer->cellDepthFillSublayer();
    auto *smoothSub = m_layer->smoothDepthFillSublayer();

    Qsg2DLodInputs li;
    li.viewportWidthPx  = width();
    li.viewportHeightPx = height();
    li.extentWidth      = m_extent.width();
    li.extentHeight     = m_extent.height();
    li.cellCount        = nTri;
    li.meshBBoxArea     = bb.width() * bb.height();
    {
        const QRectF vis = viewRect.intersected(bb);
        li.visibleFraction = (li.meshBBoxArea > 0.0)
            ? (vis.width() * vis.height()) / li.meshBBoxArea : 1.0;
    }
    li.wantFill  = (bandSub && bandSub->isVisible())
                || (cellSub && cellSub->isVisible())
                || (smoothSub && smoothSub->isVisible());
    li.wantEdges         = edgeSub && edgeSub->isVisible();
    li.wantVertexMarkers = nodeSub && nodeSub->isVisible();
    li.wantContours      = isoSub && isoSub->isVisible();
    li.wantContourLabels = li.wantContours;
    li.wantVelocity      = velSub && velSub->isVisible();
    li.haveSelection     = !m_layer->highlightedCells().isEmpty();
    li.previousBucket    = m_lastBucket;
    li.previousZoomStep  = m_lastZoomStep;

    const Qsg2DLodDecision lod = Qsg2DLodPolicy::decide(li);
    m_lastBucket   = lod.bucket;
    m_lastZoomStep = lod.zoomStep;

    // Snapshot diffs — ground truth for classifying ambiguous invalidations.
    const bool geomChanged = m_layer->geomRevision() != m_lastGeomRev;
    const bool selChanged  = m_layer->highlightedCells() != m_lastHighlight;
    const bool timeChanged = m_layer->currentTimeIndex() != m_lastRenderedTime
                          || m_layer->frameRevision() != m_lastRenderedFrame;

    const bool lodKeyChanged = lod.contentKey() != m_builtLodKey;
    const bool insideCoverage =
        m_builtCoverage.isValid() && m_builtCoverage.contains(viewRect);

    const bool wasZoom = m_dirty.zoomChangePending();
    const bool wasPan  = m_dirty.extentChangePending() && !wasZoom;

    using D = Qsg2DDirtyState;
    const quint32 bits = m_dirty.resolve(geomChanged, selChanged, timeChanged,
                                         lodKeyChanged, insideCoverage);

    // Per-pass rebuild verdicts. Mesh edges and vertex markers are static
    // across time — a scrub leaves them untouched.
    const bool rebuildFills   = bits & (D::Geometry | D::Style | D::Data | D::Lod);
    const bool rebuildEdges   = bits & (D::Geometry | D::Style | D::Lod);
    const bool rebuildMarkers = rebuildEdges;
    const bool rebuildVel     = rebuildFills;
    const bool rebuildSel     = bits & (D::Geometry | D::Selection | D::Lod);
    const bool anyContent     = rebuildFills || rebuildEdges || rebuildMarkers
                             || rebuildVel || rebuildSel;

    // Perf stats (Phase 1) — all accounting is skipped unless enabled.
    const bool statsOn = Qsg2DRenderStats::loggingEnabled();
    Qsg2DRenderStats stats;
    if (statsOn) {
        stats.rendererName = QStringLiteral("results2d");
        quint32 r = 0;
        if (wasPan)               r |= Qsg2DRenderStats::DirtyPan;
        if (wasZoom)              r |= Qsg2DRenderStats::DirtyZoom;
        if (bits & D::Data)       r |= Qsg2DRenderStats::DirtyTime;
        if (bits & D::Style)      r |= Qsg2DRenderStats::DirtyStyle;
        if (bits & D::Selection)  r |= Qsg2DRenderStats::DirtySelection;
        if (bits & D::Geometry)   r |= Qsg2DRenderStats::DirtyGeometry;
        if (bits & D::Lod)        r |= Qsg2DRenderStats::DirtyLod;
        stats.dirtyReasons = r;
    }

    if (kUpnDebug && anyContent)
        qDebug("[2D-qsg]   rebuild bits=0x%x bucket=%d key=%llu "
               "(fills=%d edges=%d vel=%d sel=%d)",
               bits, int(lod.bucket),
               (unsigned long long) lod.contentKey(),
               int(rebuildFills), int(rebuildEdges),
               int(rebuildVel), int(rebuildSel));

    // ---- Content rebuild (per-pass, dirty-domain guarded) --------------------
    if (anyContent) {

        // Anchor — bbox centre; derived purely from geometry, so recompute
        // only on the Geometry domain (all passes rebuild then, keeping
        // every anchor-relative buffer consistent).
        if (bits & D::Geometry) {
            m_anchorX = (bb.left() + bb.right())  * 0.5;
            m_anchorY = (bb.top()  + bb.bottom()) * 0.5;
            ++m_contourEpoch;
            m_requestedContourKey.epoch = m_contourEpoch;
            m_contourFrame.reset();
            m_contourPositions.reset();
        }
        const double ox = m_anchorX, oy = m_anchorY;

        // Coverage rect: content is culled to the viewport plus half a
        // viewport of margin on every side, so pans inside it stay
        // matrix-only. Refreshed only when the LOD/geometry epoch changes —
        // Data/Style/Selection rebuilds keep culling against the rect their
        // sibling passes were built for, so all passes stay consistent.
        QRectF cullRect = m_builtCoverage;
        if ((bits & (D::Geometry | D::Lod)) || !cullRect.isValid()) {
            const double cmx = m_extent.width()  * 0.5;
            const double cmy = m_extent.height() * 0.5;
            cullRect = viewRect.adjusted(-cmx, -cmy, cmx, cmy);
        }
        const QRectF cullLocal = cullRect.translated(-ox, -oy);

        const double dryDepth = m_layer->dryDepth();
        const double maxDepth = std::max(m_layer->maxDepth(), dryDepth + 1e-9);

        const OpenSWMM::Render::ContourBandStyle *bs =
            bandSub ? bandSub->bandStyle() : nullptr;
        const OpenSWMM::Render::IsolineStyle *is =
            isoSub ? isoSub->isolineStyle() : nullptr;
        const OpenSWMM::Render::VelocityVectorStyle *vs =
            velSub ? velSub->vectorStyle() : nullptr;

        const bool bandsVisible = bandSub && bandSub->isVisible();
        const bool isoVisible = isoSub && isoSub->isVisible() && lod.drawContours;
        const bool smoothBands = bandsVisible && (!bs || bs->smoothBands())
                              && lod.exactContourBands;
        std::vector<double> bandLevels, isoLevels;
        if (bandsVisible && maxDepth > dryDepth) {
            if (bs) {
                const auto edges = bs->scheme().levelEdges(dryDepth, maxDepth,
                    m_layer->depthClassificationSamples(bs->scheme()));
                bandLevels.assign(edges.cbegin(), edges.cend());
            } else {
                bandLevels = OpenSWMM::Contour::evenlySpacedLevelsInclusive(dryDepth, maxDepth, 9);
            }

        }
        if (isoVisible && maxDepth > dryDepth) {
            isoLevels = is ? is->levelsForRange(dryDepth, maxDepth,
                m_layer->depthClassificationSamples(is->scheme()))
                : OpenSWMM::Contour::evenlySpacedLevels(dryDepth, maxDepth, 8);
            isoLevels.erase(std::remove_if(isoLevels.begin(), isoLevels.end(),
                [dryDepth](double v) { return !std::isfinite(v) || v < dryDepth; }), isoLevels.end());
        }
        ContourJobKey key;
        key.time = m_layer->currentTimeIndex();
        key.frameRev = m_layer->frameRevision();
        key.epoch = m_contourEpoch;
        key.dryDepth = dryDepth;
        key.maxDepth = maxDepth;
        key.velocity = velSub && velSub->isVisible();
        key.bandLevels = bandLevels;
        key.smoothBands = smoothBands;
        key.isoLevels = isoLevels;
        m_requestedContourKey = key;
        const bool compatible = m_contourFrame && m_contourFrame->key.compatible(key);
        const bool needsMarching = (smoothBands && key.bandLevels.size() >= 2) || !key.isoLevels.empty();
        const bool background = asyncContoursEnabled(nTri) && needsMarching && compatible;
        if ((!m_contourFrame || !(m_contourFrame->key == key))
            && (!background || !m_contourBusy)) {
            if (!m_contourPositions) {
                auto positions = std::make_shared<std::vector<ContourJobInput::TriPos>>(nTri);
                for (int i = 0; i < nTri; ++i) {
                    const auto& t = tris[i];
                    (*positions)[i] = {float(t.a.x()-ox), float(t.a.y()-oy),
                        float(t.b.x()-ox), float(t.b.y()-oy),
                        float(t.c.x()-ox), float(t.c.y()-oy)};
                }
                m_contourPositions = std::move(positions);
            }
            auto frame = std::make_shared<ContourFrame>();
            frame->key = key;
            auto scalars = std::make_shared<std::vector<std::array<float,3>>>(nTri);
            frame->cellValues.resize(nTri);
            for (int i = 0; i < nTri; ++i) {
                const auto& t = tris[i];
                (*scalars)[i] = {t.dv0, t.dv1, t.dv2};
                frame->cellValues[i] = {t.depth, t.vx, t.vy, t.vmag};
            }
            frame->scalars = scalars;
            if (key.velocity) frame->velocity = m_layer->captureVelocityField();
            frame->maxVelocity = m_layer->maxVelocity();
            frame->hasVelocity = m_layer->hasVelocityData();
            ContourJobInput input;
            input.positions = m_contourPositions;
            input.scalars = std::move(scalars);
            input.bandLevels = smoothBands ? key.bandLevels : std::vector<double>{};
            input.isoLevels = key.isoLevels;
            input.minimumVisibleValue = dryDepth;
            auto compute = [frame, input = std::move(input)]() -> std::shared_ptr<const ContourFrame> {
                frame->contours = computeContourJob(input);
                return frame;
            };
            if (background) {
                m_contourBusy = true;
                m_contourWatcher.setFuture(QtConcurrent::run(std::move(compute)));
            } else {
                // Bootstrap/style changes get a complete frame immediately.
                // Never substitute cell colours or blank contour lines.
                m_contourFrame = compute();
            }
        }
        const auto& frame = *m_contourFrame;
        auto frameTri = [&](int i) {
            auto t = tris[i];
            const auto& d = (*frame.scalars)[i];
            const auto& c = frame.cellValues[i];
            t.dv0=d[0]; t.dv1=d[1]; t.dv2=d[2];
            t.depth=c[0]; t.vx=c[1]; t.vy=c[2]; t.vmag=c[3];
            return t;
        };

        // Issue 3B diagnostic — off by default. Set OPENSWMM_2D_RENDER_DEBUG=1
        // to log, per content rebuild, the guard states that decide whether
        // velocity vectors / contour bands / isolines / mesh edges / vertices
        // draw in the live GPU view.
        if (kUpnDebug) {
            const bool live = m_layer->source() && m_layer->source()->isLive();
            auto vis = [](OpenSWMM::Render::ISublayer *s) {
                return s ? (s->isVisible() ? 1 : 0) : -1; };
            qDebug("[2D-render] live=%d nTri=%d hasVel=%d t=%d | "
                   "band(vis=%d) iso(vis=%d) vel(vis=%d) edge(vis=%d) vert(vis=%d)",
                   live ? 1 : 0, nTri, m_layer->hasVelocityData() ? 1 : 0,
                   m_layer->currentTimeIndex(),
                   vis(bandSub), vis(isoSub), vis(velSub),
                   vis(edgeSub), vis(nodeSub));
        }

        // ---- Chunk index (Phase 4) — rebuilt once per geometry revision ----
        // A Selection-only rebuild touches just the highlight nodes; skip
        // the chunk query / visible-cell materialisation entirely then.
        const bool needChunks = rebuildFills || rebuildVel || rebuildEdges
                             || rebuildMarkers;
        if (needChunks
            && (m_chunksRev != m_layer->geomRevision() || m_chunks.isEmpty())) {
            QVector<QRectF> triBB(nTri);
            for (int i = 0; i < nTri; ++i) {
                const auto &t = tris[i];
                const double minX = std::min({t.a.x(), t.b.x(), t.c.x()});
                const double maxX = std::max({t.a.x(), t.b.x(), t.c.x()});
                const double minY = std::min({t.a.y(), t.b.y(), t.c.y()});
                const double maxY = std::max({t.a.y(), t.b.y(), t.c.y()});
                triBB[i] = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
            }
            const auto &edges = m_layer->m_sceneEdges;
            QVector<QRectF> edgeBB(edges.size());
            for (int i = 0; i < edges.size(); ++i) {
                const auto &e = edges[i];
                edgeBB[i] = QRectF(QPointF(std::min(e.a.x(), e.b.x()),
                                           std::min(e.a.y(), e.b.y())),
                                   QPointF(std::max(e.a.x(), e.b.x()),
                                           std::max(e.a.y(), e.b.y())));
            }
            m_chunks.build(triBB, edgeBB);
            m_chunksRev = m_layer->geomRevision();
        }

        const MeshRenderChunkIndex::QueryResult chunkQ =
            needChunks ? m_chunks.query(cullRect)
                       : MeshRenderChunkIndex::QueryResult{};

        // Visible cell ids (chunk-batched: fully-inside chunks skip the
        // per-tri bbox test). Local per rebuild — fine at Mid/Near, and at
        // Far it's one build per LOD epoch, not per pan frame.
        std::vector<int> visibleCells;
        if (rebuildFills || rebuildVel || rebuildMarkers) {
            visibleCells.reserve(size_t(nTri));
            for (int cid : chunkQ.fullyInside)
                for (int t : m_chunks.chunks()[cid].cellIds)
                    visibleCells.push_back(t);
            for (int cid : chunkQ.boundary) {
                for (int t : m_chunks.chunks()[cid].cellIds) {
                    const auto &tt = tris[t];
                    const double minX = std::min({tt.a.x(), tt.b.x(), tt.c.x()});
                    const double maxX = std::max({tt.a.x(), tt.b.x(), tt.c.x()});
                    const double minY = std::min({tt.a.y(), tt.b.y(), tt.c.y()});
                    const double maxY = std::max({tt.a.y(), tt.b.y(), tt.c.y()});
                    if (maxX < cullRect.left() || minX > cullRect.right() ||
                        maxY < cullRect.top()  || minY > cullRect.bottom())
                        continue;
                    visibleCells.push_back(t);
                }
            }
            if (statsOn) stats.visibleCells = qint64(visibleCells.size());
        }

        // ---- Pass 2: filled contour bands -------------------------------
        if (rebuildFills) {
        if (bandsVisible && maxDepth > dryDepth) {
            const auto& levels = bandLevels;
            std::vector<QSGGeometry::ColoredPoint2D> bandVerts;
            if (levels.size() >= 2) {
                const int bandCount = int(levels.size()) - 1;
                const qreal bandOp = std::clamp<qreal>(bandSub->opacity(), 0.0, 1.0);
                auto bandColor = [&](int idx) -> QColor {
                    QColor c = bs ? bs->colorForBand(idx, bandCount)
                                  : OpenSWMM::Contour::viridisAt((double(idx) + 0.5) / bandCount);
                    c.setAlphaF(c.alphaF() * bandOp);
                    return c;
                };

                // Flat per-cell classification (bucket each cell against the
                // scheme edges), clipped to the wet footprint.
                auto emitFlatCells = [&]() {
                    for (int i : visibleCells) {
                        const auto t = frameTri(i);
                        const auto wet = CellWaterGeometry::clipTriangle(
                            t.a,t.b,t.c,t.dv0,t.dv1,t.dv2,dryDepth);
                        if (wet.size < 3) continue;
                        int idx = int(std::upper_bound(
                                          levels.begin() + 1, levels.end() - 1,
                                          double(t.depth))
                                      - (levels.begin() + 1));
                        idx = std::clamp(idx, 0, bandCount - 1);
                        const QColor col = bandColor(idx);
                        const quint8 a = quint8(col.alpha());
                        const quint8 r = premul(quint8(col.red()), a);
                        const quint8 g = premul(quint8(col.green()), a);
                        const quint8 b = premul(quint8(col.blue()), a);
                        for (int k = 1; k+1 < wet.size; ++k)
                            for (int j : {0,k,k+1}) {
                                const auto& pt = wet.vertices[j].point;
                                QSGGeometry::ColoredPoint2D v;
                                v.set(float(pt.x()-ox),float(pt.y()-oy),r,g,b,a);
                                bandVerts.push_back(v);
                            }
                    }
                };

                if (smoothBands) {
                    const auto& bandsRef = frame.contours.bands;
                    bandVerts.reserve(bandsRef.size()*9);
                    // Smooth band polygons, culled per-poly to the coverage
                    // rect (poly verts are anchor-relative → cullLocal).
                    for (const auto &bp : bandsRef) {
                        if (bp.verts.size() < 3) continue;
                        double pMinX = bp.verts[0].x(), pMaxX = pMinX;
                        double pMinY = bp.verts[0].y(), pMaxY = pMinY;
                        for (const QPointF &p : bp.verts) {
                            pMinX = std::min(pMinX, p.x());
                            pMaxX = std::max(pMaxX, p.x());
                            pMinY = std::min(pMinY, p.y());
                            pMaxY = std::max(pMaxY, p.y());
                        }
                        if (pMaxX < cullLocal.left() || pMinX > cullLocal.right()
                            || pMaxY < cullLocal.top() || pMinY > cullLocal.bottom())
                            continue;
                        const QColor col = bandColor(
                            std::min(bp.bandIndex, bandCount - 1));
                        const quint8 a = quint8(col.alpha());
                        const quint8 r = premul(quint8(col.red()), a);
                        const quint8 g = premul(quint8(col.green()), a);
                        const quint8 b = premul(quint8(col.blue()), a);
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
                    bandVerts.reserve(visibleCells.size() * 3);
                    emitFlatCells();
                }
            }
            uploadColoredVerts(bandNode, bandVerts);
            if (statsOn)
                stats.addPass(QStringLiteral("bands"),
                              qint64(bandVerts.size()),
                              qint64(bandVerts.size()
                                     * sizeof(QSGGeometry::ColoredPoint2D)));
        } else {
            uploadColoredVerts(bandNode,
                               std::vector<QSGGeometry::ColoredPoint2D>{});
        }
        }

        // ---- Direct mesh-fill depth passes ------------------------------
        // Fill the triangle mesh directly (no marching-squares) — the
        // seam-free alternative to contour bands. `perVertex=false` flat-fills
        // each cell from its cell-centre depth; `perVertex=true` colours each
        // mesh vertex from its (interpolated) depth and lets the GPU
        // interpolate across the triangle (Gouraud).
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
                auto samples = m_layer->depthClassificationSamples(style->scheme());
                if (style->scheme().rangeMode() == OpenSWMM::Render::RangeMode::PerFrameAutoStretch) {
                    samples.clear();
                    for (int i = 0; i < nTri; ++i) {
                        if (i > 0 && cellOfTri(i) == cellOfTri(i-1)) continue;
                        const auto d = frame.cellValues[i][0];
                        if (std::isfinite(d) && d >= dryDepth) samples.push_back(d);
                    }
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
            out.reserve(visibleCells.size() * 3);
            auto pushV = [&](const QPointF &p, const QColor &c) {
                QSGGeometry::ColoredPoint2D v;
                const quint8 a = quint8(c.alpha());
                v.set(float(p.x() - ox), float(p.y() - oy),
                      premul(quint8(c.red()), a),
                      premul(quint8(c.green()), a),
                      premul(quint8(c.blue()), a), a);
                out.push_back(v);
            };
            for (int i : visibleCells) {
                const auto t = frameTri(i);
                const auto wet = CellWaterGeometry::clipTriangle(
                    t.a,t.b,t.c,t.dv0,t.dv1,t.dv2,dryDepth);
                for (int k = 1; k+1 < wet.size; ++k)
                    for (int j : {0,k,k+1}) {
                        const auto& v = wet.vertices[j];
                        pushV(v.point,colorAt(perVertex ? float(v.depth) : t.depth));
                    }
            }
        };

        if (rebuildFills) {
            std::vector<QSGGeometry::ColoredPoint2D> cellVerts;
            if (cellSub && cellSub->isVisible() && cellSub->fillStyle()
                && maxDepth > dryDepth)
                buildFillPass(cellSub->fillStyle(), cellSub->opacity(),
                              /*perVertex=*/false, cellVerts);
            uploadColoredVerts(cellFillNode, cellVerts);
            if (statsOn && !cellVerts.empty())
                stats.addPass(QStringLiteral("cellFill"),
                              qint64(cellVerts.size()),
                              qint64(cellVerts.size()
                                     * sizeof(QSGGeometry::ColoredPoint2D)));

            const bool smoothWanted = smoothSub && smoothSub->isVisible()
                                   && smoothSub->fillStyle()
                                   && maxDepth > dryDepth;
            // Cell-owned corners and moving wet boundaries require clipped,
            // per-cell geometry; global vertex slots cannot represent them.
            std::vector<QSGGeometry::ColoredPoint2D> smoothVerts;
            if (smoothWanted)
                buildFillPass(smoothSub->fillStyle(),smoothSub->opacity(),true,smoothVerts);
            uploadColoredVerts(smoothFillNode,smoothVerts);
            if (statsOn && !smoothVerts.empty())
                stats.addPass(QStringLiteral("smoothFill"),qint64(smoothVerts.size()),
                    qint64(smoothVerts.size()*sizeof(QSGGeometry::ColoredPoint2D)));
        }

        // Additional cell scalars share CPU classification and availability
        // colors. Use the displayed hydraulic frame while a newer contour
        // job is pending, so overlays cannot show different reporting times.
        if (rebuildFills) {
            for (auto *base : m_layer->sublayers()) {
                auto *sub = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(base);
                if (!sub) continue;
                std::vector<QSGGeometry::ColoredPoint2D> vertices;
                std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> presented;
                if (sub->isVisible() && sub->opacity() > 0) {
                    const auto &scheme = sub->fillStyle()->scheme();
                    const bool wholeRun = !scheme.useCustomRange()
                        && scheme.rangeMode() == OpenSWMM::Render::RangeMode::FixedOverRun;
                    const auto scalarFrame = m_layer->resultFrame(sub->variableKey(), frame.key.time, wholeRun);
                    const auto colors = scalarFrame ? sub->cellColors(*scalarFrame) : QVector<QColor>();
                    if (!colors.isEmpty()) presented = scalarFrame;
                    vertices.reserve(visibleCells.size() * 3);
                    for (int triangle : visibleCells) {
                        const int cell = cellOfTri(triangle);
                        if (cell < 0 || cell >= colors.size() || colors[cell].alpha() == 0) continue;
                        const auto &color = colors[cell];
                        const quint8 alpha = quint8(color.alpha());
                        const auto &tri = tris[triangle];
                        // Groundwater and chemistry have their own availability
                        // status; a dry surface is not a clipping criterion.
                        for (const QPointF &point : {tri.a, tri.b, tri.c}) {
                            QSGGeometry::ColoredPoint2D vertex;
                            vertex.set(float(point.x() - ox), float(point.y() - oy),
                                premul(quint8(color.red()), alpha), premul(quint8(color.green()), alpha),
                                premul(quint8(color.blue()), alpha), alpha);
                            vertices.push_back(vertex);
                        }
                    }
                }
                uploadColoredVerts(root->resultScalarNodes.value(sub->id()), vertices);
                sub->publishPresentedFrame(std::move(presented));
                if (statsOn && !vertices.empty())
                    stats.addPass(sub->id(), qint64(vertices.size()),
                        qint64(vertices.size() * sizeof(QSGGeometry::ColoredPoint2D)));
            }
        }

        // ---- Pass 3 + 3b: isolines, index contours, labels ---------------
        if (rebuildFills) {
        if (isoVisible && maxDepth > dryDepth) {
            using namespace OpenSWMM::Contour;
            const auto& levels = isoLevels;
            if (!levels.empty()) {
                const auto& segs = frame.contours.segs;

                // Coverage-culled view of the cached segments (anchor-
                // relative → cullLocal). Shared by the draw + label passes.
                std::vector<const IsoLineSegment *> visSegs;
                visSegs.reserve(segs.size());
                for (const auto &s : segs) {
                    const double sMinX = std::min(s.a.x(), s.b.x());
                    const double sMaxX = std::max(s.a.x(), s.b.x());
                    const double sMinY = std::min(s.a.y(), s.b.y());
                    const double sMaxY = std::max(s.a.y(), s.b.y());
                    if (sMaxX < cullLocal.left() || sMinX > cullLocal.right()
                        || sMaxY < cullLocal.top() || sMinY > cullLocal.bottom())
                        continue;
                    visSegs.push_back(&s);
                }

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
                lineVerts.reserve(visSegs.size() * 6);
                for (const auto *sp : visSegs) {
                    const auto &s = *sp;
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
                if (statsOn)
                    stats.addPass(QStringLiteral("isolines"),
                                  qint64(lineVerts.size() + indexVerts.size()),
                                  qint64((lineVerts.size() + indexVerts.size())
                                         * sizeof(QSGGeometry::Point2D)));

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
                if (is && is->labels() && lod.drawContourLabels && window()) {
                    QHash<double, QVector<QLineF>> byLevel;
                    for (const auto *sp : visSegs)
                        byLevel[sp->level].append(QLineF(sp->a, sp->b));

                    const double dpr = window()->effectiveDevicePixelRatio();
                    const int  decimals = is->labelDecimals();
                    const bool halo     = is->labelHalo();
                    const double fontPt = is->labelFontPt();
                    // Spacing measured in screen px, converted to the
                    // anchor-shifted scene units the chains live in.
                    const double spacingScene = 250.0 * double(invView);
                    const double quantum = 1e-9 * std::max(
                        {bb.width(), bb.height(), 1.0});
                    int labelBudget = lod.maxContourLabels;

                    // Bounded: a live run keeps minting new level strings as
                    // the depth range grows, and every one used to stay a GPU
                    // texture for the rest of the session. Evict wholesale
                    // (before the pass, so nodes built below hold fresh ones).
                    if (m_labelTextureCache.size() > 512)
                        clearLabelTextureCache();

                    for (auto it = byLevel.constBegin();
                         it != byLevel.constEnd() && labelBudget > 0; ++it) {
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
                            if (labelBudget <= 0) break;
                            if (chain.size() < 2) continue;
                            double total = 0.0;
                            for (int i = 1; i < chain.size(); ++i)
                                total += QLineF(chain[i-1], chain[i]).length();
                            if (total < wScene * 1.5) continue;

                            double acc    = 0.0;
                            double nextAt = std::max(spacingScene * 0.5, wScene);
                            for (int i = 1; i < chain.size()
                                            && labelBudget > 0; ++i) {
                                const QPointF a = chain[i - 1];
                                const QPointF b = chain[i];
                                const double segLen = QLineF(a, b).length();
                                if (segLen <= 0.0) continue;
                                while (acc + segLen >= nextAt
                                       && labelBudget > 0) {
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
                                    --labelBudget;
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
        }

        // ---- Pass 4: velocity-vector glyphs ------------------------------
        const bool velVisible = velSub && velSub->isVisible()
                                && frame.hasVelocity
                                && lod.drawVelocityVectors;
        if (rebuildVel) {
        if (velVisible) {
            const double dryCut = vs
                ? std::max(vs->dryDepthCutoff(), dryDepth) : dryDepth;
            const double maxVel = std::max(frame.maxVelocity, 1e-6);
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
                const quint8 a = quint8(qBound(0,
                    int(col.alpha() * velOp + 0.5), 255));
                const quint8 r = premul(quint8(col.red()), a);
                const quint8 g = premul(quint8(col.green()), a);
                const quint8 b = premul(quint8(col.blue()), a);

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

            // Dense per-cell mode only at Near and under the visible-cell
            // cap (Phase 3); otherwise screen-space grid sampling with a
            // floor on the spacing so a "dense" request degrades gracefully
            // instead of emitting one glyph per subpixel cell.
            const double spacingPxCfg = vs ? vs->glyphSpacingPx() : 30.0;
            const bool   denseMode    = spacingPxCfg <= 1.0
                                        && lod.denseVelocityAllowed;
            const double spacingPx    = (spacingPxCfg > 1.0)
                                            ? spacingPxCfg
                                            : 8.0;   // fallback sampling pitch
            if (!denseMode) {
                // V2 (Issue 5) — sample the interpolated per-vertex velocity
                // field (velocityAtScene) on a uniform SCREEN-SPACE grid over
                // the coverage rect, so arrows are evenly spaced, independent
                // of mesh density, and survive pans within the coverage.
                const double x0 = std::max(cullRect.left(),  bb.left());
                const double y0 = std::max(cullRect.top(),   bb.top());
                const double x1 = std::min(cullRect.right(), bb.right());
                const double y1 = std::min(cullRect.bottom(), bb.bottom());
                const double gridStep = spacingPx * double(invView);
                if (gridStep > 0.0 && x1 > x0 && y1 > y0) {
                    for (double gy = y0; gy <= y1; gy += gridStep) {
                        for (double gx = x0; gx <= x1; gx += gridStep) {
                            float svx, svy;
                            if (!m_layer->velocityAtScene(QPointF(gx, gy),
                                                          svx, svy, frame.velocity))
                                continue;
                            const float smag = std::sqrt(svx * svx + svy * svy);
                            if (smag < vmagSkip) continue;
                            emitGlyphAt(gx, gy, svx, svy, smag);
                        }
                    }
                }
            } else {
                // Dense per-cell mode: one arrow at each wet visible cell
                // centroid, using the cell's own RT0 vector.
                for (int i : visibleCells) {
                    const auto t = frameTri(i);
                    if (t.depth < dryCut)  continue;
                    if (t.vmag < vmagSkip) continue;
                    emitGlyphAt(t.centroid.x(), t.centroid.y(),
                                t.vx, t.vy, t.vmag);
                }
            }
            uploadColoredVerts(velNode, glyphVerts);
            if (statsOn)
                stats.addPass(QStringLiteral("velocity"),
                              qint64(glyphVerts.size()),
                              qint64(glyphVerts.size()
                                     * sizeof(QSGGeometry::ColoredPoint2D)));
        } else {
            uploadColoredVerts(velNode,
                               std::vector<QSGGeometry::ColoredPoint2D>{});
        }
        }

        // ---- Pass 3c: mesh wireframe overlay (MeshEdgeSublayer) ----------
        // GPU mirror of the CPU pass over the DEDUPLICATED edge set
        // (m_layer->m_sceneEdges). LOD-gated: no dense wireframe at Far;
        // chunk-batched culling at Mid/Near (fully-visible chunks skip the
        // per-edge bbox test). Static across time — Data rebuilds skip it.
        if (rebuildEdges) {
        if (edgeSub && edgeSub->isVisible() && lod.drawEdges) {
            const auto *es = edgeSub->edgeStyle();
            QColor col     = es ? es->color() : QColor(0, 0, 0, 130);
            const double w = es ? std::max(0.1, es->lineWidthPx()) : 0.5;
            const qreal op = std::clamp<qreal>(edgeSub->opacity(), 0.0, 1.0);
            col.setAlpha(int(qBound(0.0, col.alpha() * op, 255.0)));
            const float hw = float(0.5 * w) * invView;

            const auto &edges = m_layer->m_sceneEdges;
            std::vector<QSGGeometry::Point2D> edgeSegs;
            qint64 visEdgeCount = 0;
            auto emitEdge = [&](int i) {
                const auto &e = edges[i];
                appendThickSeg(edgeSegs,
                               float(e.a.x() - ox), float(e.a.y() - oy),
                               float(e.b.x() - ox), float(e.b.y() - oy), hw);
                ++visEdgeCount;
            };
            for (int cid : chunkQ.fullyInside)
                for (int i : m_chunks.chunks()[cid].edgeIds)
                    emitEdge(i);
            for (int cid : chunkQ.boundary) {
                for (int i : m_chunks.chunks()[cid].edgeIds) {
                    const auto &e = edges[i];
                    const double minX = std::min(e.a.x(), e.b.x());
                    const double maxX = std::max(e.a.x(), e.b.x());
                    const double minY = std::min(e.a.y(), e.b.y());
                    const double maxY = std::max(e.a.y(), e.b.y());
                    if (maxX < cullRect.left() || minX > cullRect.right() ||
                        maxY < cullRect.top()  || minY > cullRect.bottom())
                        continue;
                    emitEdge(i);
                }
            }
            uploadFlatVerts(edgeNode, edgeSegs);
            setFlatColor(edgeNode, col);
            if (statsOn) {
                stats.visibleEdges = visEdgeCount;
                stats.addPass(QStringLiteral("edges"),
                              qint64(edgeSegs.size()),
                              qint64(edgeSegs.size()
                                     * sizeof(QSGGeometry::Point2D)));
            }
        } else {
            uploadFlatVerts(edgeNode, std::vector<QSGGeometry::Point2D>{});
        }
        }

        // ---- Pass 3d: mesh-vertex markers (MeshNodeSublayer) ------------
        // One centred quad per UNIQUE mesh vertex (m_sceneVerts) — the
        // historical per-corner emission drew every shared vertex ~6×.
        // LOD-gated to Near; static across time. Falls back to the corner
        // loop when the shared-vertex cache is absent.
        if (rebuildMarkers) {
        if (nodeSub && nodeSub->isVisible() && lod.drawVertexMarkers) {
            const auto *ns = nodeSub->nodeStyle();
            QColor col      = ns ? ns->color() : QColor(40, 40, 40, 220);
            const double sz = ns ? std::max(0.5, ns->markerSizePx()) : 3.0;
            const qreal op  = std::clamp<qreal>(nodeSub->opacity(), 0.0, 1.0);
            col.setAlpha(int(qBound(0.0, col.alpha() * op, 255.0)));
            const float r = float(0.5 * sz) * invView;

            std::vector<QSGGeometry::Point2D> markVerts;
            qint64 visVertCount = 0;
            auto emitQuad = [&](float cx, float cy) {
                auto V = [](float x, float y) {
                    QSGGeometry::Point2D p; p.x = x; p.y = y; return p; };
                markVerts.push_back(V(cx - r, cy - r));
                markVerts.push_back(V(cx + r, cy - r));
                markVerts.push_back(V(cx - r, cy + r));
                markVerts.push_back(V(cx + r, cy - r));
                markVerts.push_back(V(cx + r, cy + r));
                markVerts.push_back(V(cx - r, cy + r));
                ++visVertCount;
            };
            const auto &verts = m_layer->m_sceneVerts;
            if (!verts.isEmpty()) {
                for (const QPointF &p : verts) {
                    if (p.x() < cullRect.left() || p.x() > cullRect.right() ||
                        p.y() < cullRect.top()  || p.y() > cullRect.bottom())
                        continue;
                    emitQuad(float(p.x() - ox), float(p.y() - oy));
                }
            } else {
                for (int i : visibleCells) {
                    const auto t = frameTri(i);
                    emitQuad(float(t.a.x()-ox), float(t.a.y()-oy));
                    emitQuad(float(t.b.x()-ox), float(t.b.y()-oy));
                    emitQuad(float(t.c.x()-ox), float(t.c.y()-oy));
                }
            }
            uploadFlatVerts(nodeMarkNode, markVerts);
            setFlatColor(nodeMarkNode, col);
            if (statsOn) {
                stats.visibleVertices = visVertCount;
                stats.addPass(QStringLiteral("markers"),
                              qint64(markVerts.size()),
                              qint64(markVerts.size()
                                     * sizeof(QSGGeometry::Point2D)));
            }
        } else {
            uploadFlatVerts(nodeMarkNode, std::vector<QSGGeometry::Point2D>{});
        }
        }

        // ---- Pass 6: cell-highlight overlay (CF.3) ------------------------
        // Always exact, never LOD-suppressed, and rebuilt ONLY on the
        // Selection (or Geometry/Lod) domain — a highlight change no longer
        // touches fills, contours, edges, or vectors.
        if (rebuildSel) {
            const QSet<int> &hi = m_layer->highlightedCells();
            std::vector<QSGGeometry::Point2D> fillVertsH, edgeVertsH;
            if (!hi.isEmpty()) {
                const float edgeHW = 1.0f * invView;   // ~2 px outline
                // Highlights are CELL indices: fill every display sub-triangle
                // of the cell, outline the true cell boundary — a quad's VFR
                // diagonal (the edge its two sub-triangles share) is not a
                // mesh edge and is never stroked.
                const auto &cellRange = m_layer->cellTriRange();
                const auto &triIdx    = m_layer->triVertexIndices();
                const int   nCell     = m_layer->cellCount();
                auto P = [&](const QPointF &p) {
                    QSGGeometry::Point2D q;
                    q.x = float(p.x() - ox);
                    q.y = float(p.y() - oy);
                    return q;
                };
                for (int cell : hi) {
                    if (cell < 0 || cell >= nCell ||
                        cell + 1 >= int(cellRange.size())) continue;
                    const int t0 = cellRange[size_t(cell)];
                    const int t1 = std::min(cellRange[size_t(cell) + 1], nTri);
                    for (int idx = t0; idx < t1; ++idx) {
                        const auto &t = tris[idx];
                        fillVertsH.push_back(P(t.a));
                        fillVertsH.push_back(P(t.b));
                        fillVertsH.push_back(P(t.c));
                        const QSGGeometry::Point2D pts[3] = { P(t.a), P(t.b), P(t.c) };
                        const auto &vid = triIdx[size_t(idx)];
                        for (int e = 0; e < 3; ++e) {
                            const int va = vid[e], vb = vid[(e + 1) % 3];
                            // Shared with the cell's other sub-triangle → diagonal.
                            bool diagonal = false;
                            for (int o = t0; o < t1 && !diagonal; ++o) {
                                if (o == idx) continue;
                                const auto &ov = triIdx[size_t(o)];
                                int shared = 0;
                                for (int k = 0; k < 3; ++k)
                                    if (ov[k] == va || ov[k] == vb) ++shared;
                                diagonal = (shared == 2);
                            }
                            if (diagonal) continue;
                            appendThickSeg(edgeVertsH, pts[e].x, pts[e].y,
                                           pts[(e + 1) % 3].x, pts[(e + 1) % 3].y,
                                           edgeHW);
                        }
                    }
                }
            }
            uploadFlatVerts(hiFillNode, fillVertsH);
            uploadFlatVerts(hiEdgeNode, edgeVertsH);
            setFlatColor(hiFillNode, kHiFillColor);
            setFlatColor(hiEdgeNode, kHiEdgeColor);
            if (statsOn)
                stats.addPass(QStringLiteral("selection"),
                              qint64(fillVertsH.size() + edgeVertsH.size()),
                              qint64((fillVertsH.size() + edgeVertsH.size())
                                     * sizeof(QSGGeometry::Point2D)));
            m_lastHighlight = hi;
        }

        // ---- Snapshot / key bookkeeping ----------------------------------
        if (rebuildFills) {
            m_lastRenderedTime = frame.key.time;
            m_lastRenderedFrame = frame.key.frameRev;
        }
        if (bits & D::Geometry)
            m_lastGeomRev = m_layer->geomRevision();
        m_builtLodKey   = lod.contentKey();
        m_builtCoverage = cullRect;
    }

    applyTransform();
    if (statsOn) stats.logIfEnabled();
    return root;
}
