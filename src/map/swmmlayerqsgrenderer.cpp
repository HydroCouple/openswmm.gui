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

#include <QDebug>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGTransformNode>

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

void appendNodeGlyphTriangles(std::vector<QSGGeometry::Point2D> &out,
                              float sx, float sy, float r, int nodeType)
{
    auto v = [](float x, float y) { QSGGeometry::Point2D p; p.x=x; p.y=y; return p; };
    switch (nodeType) {
    case 1: // outfall — triangle
        out.push_back(v(sx, sy-r)); out.push_back(v(sx-r, sy+r*0.8f)); out.push_back(v(sx+r, sy+r*0.8f));
        break;
    case 2: // storage — square
        out.push_back(v(sx-r,sy-r)); out.push_back(v(sx+r,sy-r)); out.push_back(v(sx+r,sy+r));
        out.push_back(v(sx-r,sy-r)); out.push_back(v(sx+r,sy+r)); out.push_back(v(sx-r,sy+r));
        break;
    case 3: // divider — diamond
        out.push_back(v(sx,sy-r)); out.push_back(v(sx+r,sy)); out.push_back(v(sx,sy+r));
        out.push_back(v(sx,sy-r)); out.push_back(v(sx,sy+r)); out.push_back(v(sx-r,sy));
        break;
    default: { // junction — 8-segment fan
        constexpr int kSeg = 8;
        constexpr float kTau = 6.28318530718f;
        for (int s = 0; s < kSeg; ++s) {
            const float a0 = kTau*float(s)/float(kSeg), a1 = kTau*float(s+1)/float(kSeg);
            out.push_back(v(sx,sy));
            out.push_back(v(sx+r*std::cos(a0),sy+r*std::sin(a0)));
            out.push_back(v(sx+r*std::cos(a1),sy+r*std::sin(a1)));
        }
        break;
    }
    }
}

void appendGageTriangles(std::vector<QSGGeometry::Point2D> &out, float sx, float sy, float r)
{ appendNodeGlyphTriangles(out, sx, sy, r, 3); }

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
                    m_selDirty = true;
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
    if (zoomChanged) m_contentDirty = true;
    update();
}

QSGNode *SWMMLayerQSGRenderer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_layer || !m_extent.isValid() || width() <= 0 || height() <= 0) {
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
        junctionsBase = makeFlatColorNode(QSGGeometry::DrawTriangles, m_layer->junctionSymbol().fillColor);
        outfallsBase  = makeFlatColorNode(QSGGeometry::DrawTriangles, m_layer->outfallSymbol().fillColor);
        storageBase   = makeFlatColorNode(QSGGeometry::DrawTriangles, m_layer->storageSymbol().fillColor);
        dividersBase  = makeFlatColorNode(QSGGeometry::DrawTriangles, m_layer->dividerSymbol().fillColor);
        gagesBase     = makeFlatColorNode(QSGGeometry::DrawTriangles, m_layer->rainGageSymbol().fillColor);
        lines         = makeFlatColorNode(QSGGeometry::DrawTriangles, m_layer->conduitSymbol().fillColor);
        linesSel      = makeFlatColorNode(QSGGeometry::DrawTriangles, selLine);
        nodesSel      = makeFlatColorNode(QSGGeometry::DrawTriangles, selNode);
        gagesSel      = makeFlatColorNode(QSGGeometry::DrawTriangles, selGage);
        for (auto *n : {catchFill,catchSelFill,catchEdge,catchSelEdge,
                        junctionsBase,outfallsBase,storageBase,dividersBase,
                        gagesBase,lines,linesSel,nodesSel,gagesSel})
            root->appendChildNode(n);
    } else {
        auto *c = root->firstChild();
        catchFill     = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        catchSelFill  = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        catchEdge     = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        catchSelEdge  = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        junctionsBase = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        outfallsBase  = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        storageBase   = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        dividersBase  = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        gagesBase     = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        lines         = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
        linesSel      = static_cast<QSGGeometryNode*>(c); c=c->nextSibling();
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
    const double cullMargin = double(2.0f * invView);
    const double cullX0 =  m_extent.xMin() - cullMargin;
    const double cullX1 =  m_extent.xMax() + cullMargin;
    const double cullY0 = -m_extent.yMax() - cullMargin;
    const double cullY1 = -m_extent.yMin() + cullMargin;

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
        setNodeColor(junctionsBase, m_layer->junctionSymbol().fillColor);
        setNodeColor(outfallsBase,  m_layer->outfallSymbol().fillColor);
        setNodeColor(storageBase,   m_layer->storageSymbol().fillColor);
        setNodeColor(dividersBase,  m_layer->dividerSymbol().fillColor);
        setNodeColor(gagesBase,     m_layer->rainGageSymbol().fillColor);
        setNodeColor(lines,         m_layer->conduitSymbol().fillColor);
        setNodeColor(linesSel,      selLine);
        setNodeColor(nodesSel,      selNode);
        setNodeColor(gagesSel,      selGage);
        setLineWidth(catchEdge, 1.0f);

        // ---- Subcatchments -------------------------------------------------
        if (m_layer->showSubcatchments()) {
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
        }

        // ---- Links ---------------------------------------------------------
        if (m_layer->showLinks()) {
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
        }

        // ---- Nodes ---------------------------------------------------------
        if (m_layer->showNodes()) {
            constexpr float kMinPx = 1.0f;
            std::vector<QSGGeometry::Point2D> junc,outf,stor,divr,sel;
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
                if(nt==1)bucket=&outf; else if(nt==2)bucket=&stor; else if(nt==3)bucket=&divr;
                appendNodeGlyphTriangles(*bucket,fx,fy,r,nt);
                if (size_t(i)<nSel.size()&&nSel[i])
                    appendNodeGlyphTriangles(sel,fx,fy,r*1.5f,nt);
            }
            uploadVerts(junctionsBase,junc);
            uploadVerts(outfallsBase, outf);
            uploadVerts(storageBase,  stor);
            uploadVerts(dividersBase, divr);
            uploadVerts(nodesSel,     sel);
        }

        // ---- Gages ---------------------------------------------------------
        if (m_layer->showRainGages()) {
            constexpr float kMinPx = 1.0f;
            const float gagePxR = float(m_layer->rainGageSymbol().size)*0.5f;
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
                    appendGageTriangles(base,fx,fy,r);
                    if (size_t(i)<gSel.size()&&gSel[i])
                        appendGageTriangles(sel,fx,fy,r*1.5f);
                }
            }
            uploadVerts(gagesBase,base);
            uploadVerts(gagesSel, sel);
        }

        m_contentDirty = false;
        m_selDirty     = false;  // base rebuild covers selection too

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
        if (m_layer->showSubcatchments()) {
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
        if (m_layer->showLinks()) {
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
        if (m_layer->showNodes()) {
            const auto &nps   = m_layer->m_nodeScenePts;
            const auto &nodes = m_layer->m_nodes;
            const auto &nHid  = m_layer->m_nodeHiddenFlag;
            const auto &nSel  = m_layer->m_nodeSelectedFlag;
            std::vector<QSGGeometry::Point2D> sel;
            for (int i = 0; i < nodes.size(); ++i) {
                if (size_t(i)>=nSel.size() || !nSel[i]) continue;
                if (size_t(i)<nHid.size()&&nHid[i]) continue;
                if (i>=nps.size()) continue;
                const QPointF &p=nps[i];
                if(p.x()<cullX0||p.x()>cullX1||
                   p.y()<cullY0||p.y()>cullY1) continue;
                const int nt=(nodes[i].nodeType>=0&&nodes[i].nodeType<4)?nodes[i].nodeType:0;
                float pxR=float(m_layer->junctionSymbol().size)*0.5f;
                if(nt==1) pxR=float(m_layer->outfallSymbol().size)*0.5f;
                else if(nt==2) pxR=float(m_layer->storageSymbol().size)*0.5f;
                else if(nt==3) pxR=float(m_layer->dividerSymbol().size)*0.5f;
                if (pxR < 1.0f) continue;
                appendNodeGlyphTriangles(sel,float(p.x()-ox),float(p.y()-oy),pxR*invView*1.5f,nt);
            }
            setNodeColor(nodesSel, selNode);
            uploadVerts(nodesSel, sel);
        }

        // Gage selection overlay
        if (m_layer->showRainGages()) {
            const auto &gps  = m_layer->m_gageScenePts;
            const auto &gages= m_layer->m_gages;
            const auto &gHid = m_layer->m_gageHiddenFlag;
            const auto &gSel = m_layer->m_gageSelectedFlag;
            const float r = float(m_layer->rainGageSymbol().size)*0.5f*invView*1.5f;
            std::vector<QSGGeometry::Point2D> sel;
            for (int i = 0; i < gages.size(); ++i) {
                if (size_t(i)>=gSel.size() || !gSel[i]) continue;
                if (size_t(i)<gHid.size()&&gHid[i]) continue;
                if (i>=gps.size()) continue;
                const QPointF &p=gps[i];
                if(p.x()<cullX0||p.x()>cullX1||
                   p.y()<cullY0||p.y()>cullY1) continue;
                appendGageTriangles(sel,float(p.x()-ox),float(p.y()-oy),r);
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
