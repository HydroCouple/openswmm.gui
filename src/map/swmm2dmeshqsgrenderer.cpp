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
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/meshnodesublayer.h"
// Slice Z.6a — paint reads from typed Symbol Layer specs.
#include "render/colorramp.h"
#include "render/rastersymbollayers.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QMatrix4x4>
#include <QPainter>
#include <QPainterPath>
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
#include <cmath>
#include <limits>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Elevation colour ramp  [0,1] → RGB   (matches MeshGraphicsItem)
// ---------------------------------------------------------------------------
// Slice Z.6a — moved from a hardcoded function to a RasterColorRamp factory
// + a per-vertex sample helper. The factory reproduces the legacy 5-stop
// elevation palette exactly so paint output is unchanged when no Rule
// Model edits have been applied yet. The Rule Model dialog can swap the
// ramp via the RuleSymbologyTab → MeshFill Rule path; the per-vertex
// sample then picks up the new colours on the next paint.

RasterColorRamp legacyElevationRamp()
{
    // Single source of truth: the historic 5-stop palette now lives in
    // RasterColorRamp::terrain() (registered as the "Terrain" builtin) so the
    // mesh-fill editor's ramp combo and this renderer agree on the default.
    return RasterColorRamp::terrain();
}

/*! Sample a normalised position [0,1] from a RasterColorRamp and unpack
 *  the result into quint8 RGB. Mirrors the historic elevationColorRgb
 *  function — same linear-interp algorithm, same RGB output — but the
 *  stops come from the spec so user edits via the Rule Model dialog can
 *  override the look without touching this function. */
void colorFromRamp(double t,
                   const RasterColorRamp &ramp,
                   quint8 &r, quint8 &g, quint8 &b)
{
    const QColor c = ramp.colorAt(qBound(0.0, t, 1.0));
    r = quint8(c.red());
    g = quint8(c.green());
    b = quint8(c.blue());
}

/*! Structural equality on RasterColorRamp.
 *  Used by the Pass 1 fill-colour cache to detect when the ramp has been
 *  edited via the style dialog. RasterColorRamp has no operator== of its
 *  own; QGradientStops (QVector<QPair<qreal, QColor>>) does, and QColor
 *  does, so the per-field comparison falls through to value semantics
 *  the whole way down. */
bool rampEqual(const RasterColorRamp &a, const RasterColorRamp &b)
{
    return a.minValue == b.minValue
        && a.maxValue == b.maxValue
        && a.clampMin == b.clampMin
        && a.clampMax == b.clampMax
        && a.interp   == b.interp
        && a.stops    == b.stops;
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

// ---------------------------------------------------------------------------
// Isoline label rasterisation
// ---------------------------------------------------------------------------
//
// Render `text` to a small QImage with a white halo so the label reads on
// both light (high-elev terrain) and dark (deep-water) underlays. The image
// is sized to fit the text plus a 2 px pad on every side. devicePixelRatio
// is applied so the texture stays crisp on Hi-DPI displays.

QImage rasteriseLabel(const QString &text, const QColor &color, double dpr)
{
    QFont f;
    f.setPointSizeF(9.0);
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

    // White halo via QPainterPath stroke. We outline the glyph path twice
    // (3 px and 2 px) to produce a soft glow that doesn't blur the glyph.
    QPainterPath path;
    path.addText(pad - br.left(), pad - br.top(), f, text);

    QPen halo(QColor(255, 255, 255, 230));
    halo.setWidthF(3.0);
    halo.setJoinStyle(Qt::RoundJoin);
    halo.setCapStyle(Qt::RoundCap);
    p.setPen(halo);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Fill the glyph with the user-configured colour.
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);

    p.end();
    return img;
}

} // namespace

// ---------------------------------------------------------------------------

SWMM2DMeshQSGRenderer::SWMM2DMeshQSGRenderer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

SWMM2DMeshQSGRenderer::~SWMM2DMeshQSGRenderer()
{
    clearLabelTextureCache();
}

void SWMM2DMeshQSGRenderer::clearLabelTextureCache()
{
    // QSGTextures must be released on the render thread; deleteLater() is
    // safe because the next render-thread iteration will process the
    // pending delete queue before we re-create new textures here.
    for (QSGTexture *t : std::as_const(m_labelTextureCache))
        if (t) t->deleteLater();
    m_labelTextureCache.clear();
}

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
    clearLabelTextureCache();
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
    //        triNode       (Pass 1: mesh fill — MeshFillSublayer)
    //        isobandNode   (Pass 4: filled contour bands — ContourBandSublayer)
    //        edgeThinNode  (Pass 2: thin wireframe — MeshEdgeSublayer)
    //        edgeWideNode  (Pass 2: wide wireframe — slope emphasis)
    //        contourNode   (Pass 3: isolines — IsolineSublayer)
    //        contourLabels (Pass 3b: per-level isoline labels — IsolineStyle::labels)
    //        nodeMarkNode  (Pass 5: vertex markers — MeshNodeSublayer)
    //        selEdgeNode + selVertNode (§V selection-overlay pass — cyan)
    auto *root = static_cast<QSGTransformNode *>(oldNode);
    QSGGeometryNode *triNode       = nullptr;
    QSGGeometryNode *isobandNode   = nullptr;
    QSGGeometryNode *edgeThinNode  = nullptr;
    QSGGeometryNode *edgeWideNode  = nullptr;
    QSGGeometryNode *contourNode   = nullptr;
    QSGNode         *contourLabels = nullptr;
    QSGGeometryNode *nodeMarkNode  = nullptr;
    QSGGeometryNode *selTriNode    = nullptr;
    QSGGeometryNode *selEdgeNode   = nullptr;
    QSGGeometryNode *selVertNode   = nullptr;

    // §V selection-overlay colours.
    const QColor kSelTriColor (0x00, 0xc8, 0xff, 90);    // translucent cyan fill
    const QColor kSelEdgeColor(0x00, 0xc8, 0xff, 235);   // bright cyan
    const QColor kSelVertColor(0x00, 0xff, 0xff, 245);   // brighter cyan

    if (!root) {
        root          = new QSGTransformNode();
        triNode       = makeColoredNode();
        isobandNode   = makeColoredNode();   // per-vertex colour for band fill
        edgeThinNode  = makeFlatNode(QColor(0, 0, 0, 130));
        edgeWideNode  = makeFlatNode(QColor(0, 0, 0, 210));
        contourNode   = makeFlatNode(QColor(0x1a, 0x1a, 0x1a, 200));
        contourLabels = new QSGNode();       // parent for QSGSimpleTextureNode labels
        nodeMarkNode  = makeColoredNode();
        selTriNode    = makeFlatNode(kSelTriColor);
        selEdgeNode   = makeFlatNode(kSelEdgeColor);
        selVertNode   = makeFlatNode(kSelVertColor);
        root->appendChildNode(triNode);
        root->appendChildNode(isobandNode);
        root->appendChildNode(edgeThinNode);
        root->appendChildNode(edgeWideNode);
        root->appendChildNode(contourNode);
        root->appendChildNode(contourLabels);
        root->appendChildNode(nodeMarkNode);
        root->appendChildNode(selTriNode);
        root->appendChildNode(selEdgeNode);
        root->appendChildNode(selVertNode);
    } else {
        auto *c  = root->firstChild();
        triNode       = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        isobandNode   = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        edgeThinNode  = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        edgeWideNode  = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        contourNode   = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        contourLabels = c;                                c = c->nextSibling();
        nodeMarkNode  = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        selTriNode    = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        selEdgeNode   = static_cast<QSGGeometryNode*>(c); c = c->nextSibling();
        selVertNode   = static_cast<QSGGeometryNode*>(c);
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
            // Drop any cached label children — nothing visible.
            while (auto *c = contourLabels->firstChild()) {
                contourLabels->removeChildNode(c);
                delete c;
            }
            uploadColoredVerts(nodeMarkNode, empty_c);
            uploadFlatVerts(selTriNode,      empty_p);
            uploadFlatVerts(selEdgeNode,     empty_p);
            uploadFlatVerts(selVertNode,     empty_p);
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

        // Resolve sublayer styles once per rebuild. Each is non-null in
        // practice (constructed in SWMM2DMeshLayer's ctor), but we still
        // guard so a partially-constructed layer doesn't crash the
        // renderer during early QSG ticks.
        const auto *fillSub  = m_layer->meshFillSublayer();
        const auto *edgeSub  = m_layer->meshEdgeSublayer();
        const auto *nodeSub  = m_layer->meshNodeSublayer();
        const auto *bandSub  = m_layer->contourBandSublayer();
        const auto *isoSub   = m_layer->isolineSublayer();
        const auto *fillStyle = fillSub ? fillSub->fillStyle() : nullptr;
        const auto *edgeStyle = edgeSub ? edgeSub->edgeStyle() : nullptr;
        const auto *nodeStyle = nodeSub ? nodeSub->nodeStyle() : nullptr;
        const auto *bandStyle = bandSub ? bandSub->bandStyle() : nullptr;
        const auto *isoStyle  = isoSub  ? isoSub->isolineStyle() : nullptr;

        // Slice Z.6a — build Z.6 typed specs from the legacy sublayer
        // styles once at the top of the rebuild. Every pass below
        // reads from the specs instead of the style getters; the spec
        // values exactly reproduce the historic defaults so visual
        // output is unchanged. Rule Model dialog edits flow into the
        // legacy sublayer style fields (via the layer's connect
        // handlers in buildRuleListLazy), which feed back into the
        // specs on the next paint tick.
        using OpenSWMM::Render::RasterColorRampSymbolLayerSpec;
        using OpenSWMM::Render::HillshadeSymbolLayerSpec;
        using OpenSWMM::Render::ContourSymbolLayerSpec;
        using OpenSWMM::Render::MeshEdgeSymbolLayerSpec;
        using OpenSWMM::Render::MeshNodeSymbolLayerSpec;

        RasterColorRampSymbolLayerSpec rampSpec;
        rampSpec.ramp = legacyElevationRamp();

        HillshadeSymbolLayerSpec hillSpec;
        hillSpec.azimuthDeg    = m_layer->hillshadeAzimuth();
        hillSpec.altitudeDeg   = m_layer->hillshadeAltitude();
        hillSpec.zExaggeration = m_layer->hillshadeZExag();
        hillSpec.shadowFloor   = m_layer->hillshadeMinLit();
        hillSpec.strength      = fillStyle ? fillStyle->hillshadeStrength() : 0.5;

        MeshEdgeSymbolLayerSpec edgeSpec;
        edgeSpec.color               = edgeStyle ? edgeStyle->color()
                                                 : QColor(0, 0, 0, 130);
        edgeSpec.width               = edgeStyle ? edgeStyle->lineWidthPx() : 0.35;
        edgeSpec.useSlopeDrivenWidth = edgeStyle ? edgeStyle->useSlopeDrivenWidth() : true;
        edgeSpec.slopeBreak          = edgeStyle ? edgeStyle->slopeBreak() : 0.35;
        edgeSpec.wideWidthPx         = edgeStyle ? edgeStyle->wideWidthPx() : 0.9;
        edgeSpec.wideColor           = edgeStyle ? edgeStyle->wideColor()
                                                 : QColor(0, 0, 0, 210);

        // Slice US.3 — the band/iso passes now read class edges + colours
        // straight from the sublayer ClassificationScheme (method-aware ramp
        // classification), so only the line-symbology spec remains.
        ContourSymbolLayerSpec isoSpec;
        isoSpec.mode        = OpenSWMM::Render::ContourMode::Lines;
        isoSpec.lineColor   = isoStyle ? isoStyle->color() : QColor(10, 10, 10, 220);
        isoSpec.lineWidthPx = isoStyle ? isoStyle->lineWidthPx() : 1.0;
        isoSpec.labelEveryN = (isoStyle && isoStyle->labels()) ? 1 : 0;

        MeshNodeSymbolLayerSpec nodeSpec;
        nodeSpec.marker.fillColor = nodeStyle ? nodeStyle->color()
                                              : QColor(40, 40, 40, 220);
        nodeSpec.marker.sizePx    = nodeStyle ? nodeStyle->markerSizePx() : 3.0;
        nodeSpec.marker.shape     = static_cast<OpenSWMM::Render::MarkerShape>(
                                        nodeStyle ? int(nodeStyle->shape()) : 0);

        // ---- Pre-computed paint constants derived from the specs ---------
        // Hillshade direction vectors derive from azimuth/altitude — same
        // formulae as before, now reading from hillSpec.
        const quint8 kFillAlpha   = quint8(active ? 160 : 110);
        const float  kVertExag    = float(hillSpec.zExaggeration);
        const double azRad        = qDegreesToRadians(hillSpec.azimuthDeg);
        const double altRad       = qDegreesToRadians(hillSpec.altitudeDeg);
        const float  kLx          = float(std::sin(azRad) * std::cos(altRad));
        const float  kLy          = float(std::cos(azRad) * std::cos(altRad));
        const float  kLz          = float(std::sin(altRad));
        const float  kLitMin      = float(hillSpec.shadowFloor);
        // Edge spec values folded into pre-computed widths. The inactive-
        // mesh edgeMute multiplier (0.714) is preserved from the historic
        // visual.
        const bool   useSlopeWidth  = edgeSpec.useSlopeDrivenWidth;
        const float  kSlopeBreak    = float(edgeSpec.slopeBreak);
        const float  edgeMute       = active ? 1.0f : 0.714f;
        const float  kThinHW        = float(edgeSpec.width) * edgeMute * invView;
        const float  kWideHW        = float(edgeSpec.wideWidthPx) * edgeMute * invView;
        const QColor kEdgeColorThin = edgeSpec.color;
        const QColor kEdgeColorWide = edgeSpec.wideColor;

        // Spatial-grid pre-filter — replace the O(N) inline bbox cull in
        // Pass 1 / Pass 2 with an O(visible) query against the uniform grid
        // built in rebuildSceneGeometry(). The grid returns the set of
        // triangle / edge indices whose bbox overlaps the cull rect, which
        // is exactly the set the inline test would have accepted, so the
        // inline tests are dropped below. Grid may be empty if the mesh
        // hasn't been rebuilt yet — fall back to a full scan in that case
        // to preserve correctness.
        const QRectF cullRect(QPointF(cullX0, cullY0), QPointF(cullX1, cullY1));
        const auto &triGrid  = m_layer->m_triGrid;
        const auto &edgeGrid = m_layer->m_edgeGrid;
        const bool useTriIdx  = !triGrid.isEmpty();
        const bool useEdgeIdx = !edgeGrid.isEmpty();
        const QVector<int> visibleTris  = useTriIdx  ? triGrid.query(cullRect)  : QVector<int>{};
        const QVector<int> visibleEdges = useEdgeIdx ? edgeGrid.query(cullRect) : QVector<int>{};

        // ---- Pass 1: filled triangles --------------------------------------
        // Slice Z.6a — paint reads from rampSpec / hillSpec built above.
        const bool fillVisible = !fillSub || fillSub->isVisible();
        if (fillVisible) {
            const auto &tris = m_layer->m_sceneTris;
            const int triCount = useTriIdx ? visibleTris.size() : tris.size();
            std::vector<QSGGeometry::ColoredPoint2D> verts;
            verts.reserve(size_t(triCount) * 3);

            const bool   useRamp = fillStyle ? fillStyle->useElevationRamp() : true;
            const QColor flat    = fillStyle ? fillStyle->fillColor() : QColor(70, 130, 180);
            const float  shade   = float(hillSpec.strength);
            const float  fillOp  = fillSub  ? float(fillSub->opacity())  : 1.0f;
            const quint8 alpha   = quint8(qBound(0, int(kFillAlpha * fillOp + 0.5f), 255));

            if (hasElev && useRamp) {
                const double invRange = 1.0 / (zMax - zMin);

                // Slice US (mesh) — terrain fill is classified by bed
                // elevation through the sublayer's ClassificationScheme.
                // Legacy alignment: the default scheme is Continuous with an
                // empty ramp name and no inversion, for which we keep the
                // historic 5-stop legacyElevationRamp() look (rampSpec.ramp)
                // untouched. Once the user picks a named ramp, inverts it, or
                // switches to Classified mode, the per-triangle colour is
                // sampled from the scheme instead.
                const OpenSWMM::Render::ClassificationScheme scheme =
                    fillStyle ? fillStyle->scheme() : OpenSWMM::Render::ClassificationScheme();
                const bool schemeClassified =
                    scheme.mode() == OpenSWMM::Render::ClassificationScheme::ClassMode::Classified;
                // The default scheme names the "Terrain" ramp (== rampSpec.ramp ==
                // legacyElevationRamp()); keep it on the byte-identical legacy
                // colorFromRamp path below so the default terrain fill is
                // unchanged. Any *other* named ramp, an inversion, or Classified
                // mode routes the per-triangle colour through the scheme.
                const QString rampName = scheme.rampName();
                const bool isDefaultTerrainRamp =
                    rampName.isEmpty()
                    || rampName.compare(QLatin1String("terrain"), Qt::CaseInsensitive) == 0;
                const bool schemeDrivesColor =
                    schemeClassified || scheme.invertRamp() || !isDefaultTerrainRamp;
                // NOTE: per-quad zSamples are not assembled here (the data-
                // driven Quantile/Jenks/StdDev methods then degrade to equal
                // spacing — acceptable for the static terrain default). The
                // band pass above does sample when needed; mirror that if
                // exact data-driven edges are ever required for the fill.
                const QVector<double> classEdges =
                    schemeClassified ? scheme.levelEdges(zMin, zMax, {}) : QVector<double>{};

                // Per-triangle fill-colour cache — see header for the full
                // story. Key compare runs every paint; structural compare
                // is cheap (one quint64, seven doubles, plus the ramp).
                // On miss, drop the cache and let the per-triangle loop
                // below populate entries lazily for visible triangles.
                // The scheme revision is folded into the cache revision so a
                // classification edit invalidates the per-triangle colours
                // without needing a dedicated cache member.
                const quint64 curRev = m_layer->geomRevision() ^ scheme.revision();
                const bool fillCacheHit =
                    m_fillCacheValid
                    && m_fillCacheRev         == curRev
                    && m_fillCacheZMin        == zMin
                    && m_fillCacheZMax        == zMax
                    && m_fillCacheAzimuth     == hillSpec.azimuthDeg
                    && m_fillCacheAltitude    == hillSpec.altitudeDeg
                    && m_fillCacheZExag       == hillSpec.zExaggeration
                    && m_fillCacheShadowFloor == hillSpec.shadowFloor
                    && m_fillCacheStrength    == hillSpec.strength
                    && rampEqual(m_fillCacheRamp, rampSpec.ramp)
                    && m_cachedFillRgb.size() == size_t(tris.size());

                if (!fillCacheHit) {
                    m_cachedFillRgb.assign(size_t(tris.size()), 0u);
                    m_fillCacheRev         = curRev;
                    m_fillCacheZMin        = zMin;
                    m_fillCacheZMax        = zMax;
                    m_fillCacheAzimuth     = hillSpec.azimuthDeg;
                    m_fillCacheAltitude    = hillSpec.altitudeDeg;
                    m_fillCacheZExag       = hillSpec.zExaggeration;
                    m_fillCacheShadowFloor = hillSpec.shadowFloor;
                    m_fillCacheStrength    = hillSpec.strength;
                    m_fillCacheRamp        = rampSpec.ramp;
                    m_fillCacheValid       = true;
                }

                // Entries are 0u until first computed; bit 24 marks
                // "valid" so a real black triangle (RGB 0,0,0) is still
                // distinguishable from "not yet computed".
                constexpr quint32 kValidBit = 0x01000000u;

                for (int ii = 0; ii < triCount; ++ii) {
                    const int   idx = useTriIdx ? visibleTris[ii] : ii;
                    const auto &t   = tris[idx];

                    quint32 packed = m_cachedFillRgb[size_t(idx)];
                    if (packed == 0u) {
                        // Slice Z.6a / US (mesh) — colour sampling. When the
                        // ClassificationScheme is at its default (Continuous,
                        // unnamed ramp) we keep the legacy 5-stop palette;
                        // otherwise the scheme supplies the colour (Continuous
                        // ramp sample, or Classified band by elevation class).
                        quint8 cr, cg, cb;
                        if (schemeDrivesColor) {
                            const QColor sc = schemeClassified
                                ? scheme.colorForClass(
                                      OpenSWMM::Render::ClassificationScheme::classIndexFor(
                                          double(t.zAvg), classEdges),
                                      scheme.classCount())
                                : scheme.colorForValue(double(t.zAvg), zMin, zMax);
                            cr = quint8(sc.red());
                            cg = quint8(sc.green());
                            cb = quint8(sc.blue());
                        } else {
                            colorFromRamp(
                                double(t.zAvg - float(zMin)) * invRange,
                                rampSpec.ramp, cr, cg, cb);
                        }

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
                        const float litRaw = qBound(kLitMin, nx*kLx + ny*kLy + nz*kLz, 1.0f);
                        // Blend between 'no shading' (1.0) and 'full
                        // historic shading' (litRaw) by hillshadeStrength
                        // so the slider smoothly mutes the relief without
                        // changing colours.
                        const float lit = 1.0f - shade * (1.0f - litRaw);

                        const quint8 r = quint8(qBound(0, int(float(cr)*lit), 255));
                        const quint8 g = quint8(qBound(0, int(float(cg)*lit), 255));
                        const quint8 b = quint8(qBound(0, int(float(cb)*lit), 255));

                        packed = kValidBit
                               | (quint32(r) << 16)
                               | (quint32(g) << 8)
                               |  quint32(b);
                        m_cachedFillRgb[size_t(idx)] = packed;
                    }

                    const quint8 r = quint8((packed >> 16) & 0xFFu);
                    const quint8 g = quint8((packed >> 8)  & 0xFFu);
                    const quint8 b = quint8( packed        & 0xFFu);

                    for (const QPointF *pt : {&t.a, &t.b, &t.c}) {
                        QSGGeometry::ColoredPoint2D v;
                        v.set(float(pt->x() - ox), float(pt->y() - oy),
                              r, g, b, alpha);
                        verts.push_back(v);
                    }
                }
            } else {
                // Flat fill — either the elevation ramp is off in the
                // style or the mesh has no elevation range to remap.
                const quint8 fr = quint8(flat.red());
                const quint8 fg = quint8(flat.green());
                const quint8 fb = quint8(flat.blue());
                for (int ii = 0; ii < triCount; ++ii) {
                    const auto &t = useTriIdx ? tris[visibleTris[ii]] : tris[ii];
                    for (const QPointF *pt : {&t.a, &t.b, &t.c}) {
                        QSGGeometry::ColoredPoint2D v;
                        v.set(float(pt->x()-ox), float(pt->y()-oy), fr, fg, fb, alpha);
                        verts.push_back(v);
                    }
                }
            }
            uploadColoredVerts(triNode, verts);
        } else {
            uploadColoredVerts(triNode, std::vector<QSGGeometry::ColoredPoint2D>{});
        }

        // ---- Pass 4: filled iso-bands (ContourBandSublayer) --------------
        // Z-ordered above Pass 1 (hillshade) and below Pass 2 (edges) +
        // Pass 3 (lines) so the underlying hillshade can still show through
        // the configurable alpha while the line / wireframe overlays stay
        // crisp on top. lowColor / highColor on ContourBandStyle drive a
        // linear gradient between the bottom and top band; smoothBands
        // toggles between the per-band viridis (categorical) and the
        // smooth ramp.
        const bool bandsVisible = bandSub && bandSub->isVisible();
        if (hasElev && bandsVisible) {
            // Slice US.3 — class edges from the band sublayer's
            // ClassificationScheme over the elevation range (method-aware:
            // EqualInterval reproduces the legacy even spacing; Quantile /
            // Jenks / StdDev bin the mesh's vertex elevations). Band colours
            // come from colorForBand (named ramp / two-colour / per-class
            // override), not the old two-stop lerp.
            std::vector<double> levels;
            quint64 schemeRev = 0;
            if (bandStyle) {
                QVector<double> zSamples;
                const auto m = bandStyle->scheme().method();
                if (m == OpenSWMM::Render::BinMethod::Quantile
                    || m == OpenSWMM::Render::BinMethod::NaturalBreaks
                    || m == OpenSWMM::Render::BinMethod::StdDev) {
                    const auto &st = m_layer->m_sceneTris;
                    zSamples.reserve(st.size() * 3);
                    for (const auto &t : st) {
                        zSamples.push_back(double(t.z0));
                        zSamples.push_back(double(t.z1));
                        zSamples.push_back(double(t.z2));
                    }
                }
                const QVector<double> edges =
                    bandStyle->scheme().levelEdges(zMin, zMax, zSamples);
                levels.assign(edges.cbegin(), edges.cend());
                schemeRev = bandStyle->scheme().revision();
            } else {
                levels = OpenSWMM::Contour::evenlySpacedLevelsInclusive(zMin, zMax, 9);
            }
            const int    nBands = std::max(1, int(levels.size()) - 1);
            const quint8 alpha  = quint8(qBound(0, int(bandSub->opacity() * 255.0 + 0.5), 255));

            // Contour cache — marching-triangles output is invariant to
            // pan/zoom. Recompute only when (geomRevision, zMin, zMax,
            // nBands, scheme revision) changes; otherwise reuse m_cachedBands.
            const quint64 geomRev = m_layer->geomRevision();
            const bool bandCacheHit =
                !m_cachedBands.empty()
                && m_isobandCacheRev    == geomRev
                && m_isobandCacheZMin   == zMin
                && m_isobandCacheZMax   == zMax
                && m_isobandCacheBands  == nBands
                && m_isobandCacheScheme == schemeRev;

            if (!bandCacheHit) {
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

                m_cachedBands = OpenSWMM::Contour::marchingTrianglesIsobands(
                    sceneTris, levels, extract);
                m_isobandCacheRev    = geomRev;
                m_isobandCacheZMin   = zMin;
                m_isobandCacheZMax   = zMax;
                m_isobandCacheBands  = nBands;
                m_isobandCacheScheme = schemeRev;
            }
            const auto &bands = m_cachedBands;

            std::vector<QSGGeometry::ColoredPoint2D> bandVerts;
            bandVerts.reserve(bands.size() * 9);

            for (const auto &bp : bands) {
                if (bp.verts.size() < 3) continue;
                const int idx = std::min(bp.bandIndex, nBands - 1);
                QColor col = bandStyle
                    ? bandStyle->colorForBand(idx, nBands)
                    : OpenSWMM::Contour::viridisAt((double(idx) + 0.5) / double(nBands));
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

        // ---- Pass 2: edges — split into thin and wide --------------------
        // MeshEdgeSublayer drives this pass. Visibility, colours, widths,
        // and the slope-driven thin/wide split all come from MeshEdgeStyle
        // now. Defaults reproduce the historic visual exactly.
        const bool edgesVisible = edgeSub && edgeSub->isVisible();
        if (edgesVisible) {
            const auto &edges = m_layer->m_sceneEdges;
            const int edgeCount = useEdgeIdx ? visibleEdges.size() : edges.size();
            const float invSlope = (maxSlope > 0.f) ? 1.0f / maxSlope : 0.0f;

            // Slice US (mesh) — slope classification via the edge sublayer's
            // ClassificationScheme. The renderer carries exactly two edge
            // nodes (thin / wide), each a single flat colour, so this pass
            // remains a two-tier split: low (thin) vs high (wide). The legacy
            // seed (2-class, Manual break at slopeBreak, colours = color /
            // wideColor) reproduces the historic look bit-for-bit. When the
            // user customizes the scheme we take the split threshold from the
            // first interior class edge and the thin/wide colours from the
            // first / last class colours.
            //
            // NOTE: schemes with >2 classes cannot be rendered as distinct
            // per-class colours here without a vertex-coloured edge node
            // (would require a renderer-header change, out of this slice's
            // scope). They degrade to the first-edge two-tier split above —
            // documented limitation for the testing agent.
            //
            // Slope is normalised to [0,1] as (slope * invSlope) to match the
            // legacy kSlopeBreak fraction-of-maxSlope contract; the scheme's
            // levelEdges are computed over [0,1] to align with it.
            // The loose legacy slopeBreak / color / wideColor remain the
            // source of truth while the scheme is at its untouched 2-class
            // Manual seed (so editing those grid properties still works and
            // the historic look is bit-for-bit). Only once the user reshapes
            // the scheme (different method, or > 2 classes) do we read split +
            // colours from it.
            float        kSplit     = kSlopeBreak;
            QColor       thinColor  = kEdgeColorThin;
            QColor       wideColor  = kEdgeColorWide;
            if (edgeStyle) {
                const auto &scheme = edgeStyle->scheme();
                const bool schemeCustomized =
                    scheme.mode() ==
                        OpenSWMM::Render::ClassificationScheme::ClassMode::Classified
                    && (scheme.classCount() != 2
                        || scheme.method() != OpenSWMM::Render::BinMethod::Manual);
                if (schemeCustomized && scheme.classCount() >= 2) {
                    const QVector<double> edgesV = scheme.levelEdges(0.0, 1.0, {});
                    if (edgesV.size() >= 3)
                        kSplit = float(edgesV[1]);              // first interior break
                    thinColor = scheme.colorForClass(0, scheme.classCount());
                    wideColor = scheme.colorForClass(scheme.classCount() - 1,
                                                     scheme.classCount());
                }
            }

            std::vector<QSGGeometry::Point2D> thinSegs, wideSegs;
            thinSegs.reserve(size_t(edgeCount) * 6);
            wideSegs.reserve(size_t(edgeCount) / 8 * 6);

            for (int ii = 0; ii < edgeCount; ++ii) {
                const auto &e = useEdgeIdx ? edges[visibleEdges[ii]] : edges[ii];

                const float ax = float(e.line.x1()-ox), ay = float(e.line.y1()-oy);
                const float bx = float(e.line.x2()-ox), by = float(e.line.y2()-oy);

                if (useSlopeWidth && hasElev && (e.slope * invSlope > kSplit))
                    appendThickSeg(wideSegs, ax, ay, bx, by, kWideHW);
                else
                    appendThickSeg(thinSegs, ax, ay, bx, by, kThinHW);
            }
            uploadFlatVerts(edgeThinNode, thinSegs);
            uploadFlatVerts(edgeWideNode, wideSegs);

            // Apply sublayer opacity uniformly across both nodes.
            const qreal edgeOp = edgeSub->opacity();
            auto withOp = [edgeOp](const QColor &c) {
                QColor r = c;
                r.setAlpha(int(qBound(0.0, c.alpha() * edgeOp, 255.0)));
                return r;
            };
            setFlatColor(edgeThinNode, withOp(thinColor));
            setFlatColor(edgeWideNode, withOp(wideColor));
        } else {
            const std::vector<QSGGeometry::Point2D> empty;
            uploadFlatVerts(edgeThinNode, empty);
            uploadFlatVerts(edgeWideNode, empty);
        }

        // ---- Pass 3: bed-elevation contour lines (IsolineSublayer) -------
        // Marching-triangles over m_sceneTris, N evenly-spaced levels in
        // [zMin, zMax]. Line colour and width come from IsolineStyle; the
        // labels property is reserved for the labelled-contour pass once
        // the label engine lands.
        const bool isoVisible = isoSub && isoSub->isVisible();
        if (hasElev && isoVisible) {
            // Slice US.3 — interior levels from the isoline sublayer's
            // ClassificationScheme over the elevation range (method-aware).
            const double widthPx = isoSpec.lineWidthPx;
            const float  cHW     = float(0.5 * widthPx) * invView;
            std::vector<double> levels;
            quint64 isoSchemeRev = 0;
            if (isoStyle) {
                QVector<double> zSamples;
                const auto m = isoStyle->scheme().method();
                if (m == OpenSWMM::Render::BinMethod::Quantile
                    || m == OpenSWMM::Render::BinMethod::NaturalBreaks
                    || m == OpenSWMM::Render::BinMethod::StdDev) {
                    const auto &st = m_layer->m_sceneTris;
                    zSamples.reserve(st.size() * 3);
                    for (const auto &t : st) {
                        zSamples.push_back(double(t.z0));
                        zSamples.push_back(double(t.z1));
                        zSamples.push_back(double(t.z2));
                    }
                }
                const auto lv = isoStyle->levelsForRange(zMin, zMax, zSamples);
                levels.assign(lv.cbegin(), lv.cend());
                isoSchemeRev = isoStyle->scheme().revision();
            } else {
                levels = OpenSWMM::Contour::evenlySpacedLevels(zMin, zMax, 8);
            }
            const int nLevels = int(levels.size());

            // Contour cache — same memoisation pattern as Pass 4.
            const quint64 geomRev = m_layer->geomRevision();
            const bool isoCacheHit =
                !m_cachedSegs.empty()
                && m_isolineCacheRev    == geomRev
                && m_isolineCacheZMin   == zMin
                && m_isolineCacheZMax   == zMax
                && m_isolineCacheLevels == nLevels
                && m_isolineCacheScheme == isoSchemeRev;

            if (!isoCacheHit) {
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

                m_cachedSegs = OpenSWMM::Contour::marchingTriangles(
                    sceneTris, levels, extract);
                m_isolineCacheRev    = geomRev;
                m_isolineCacheZMin   = zMin;
                m_isolineCacheZMax   = zMax;
                m_isolineCacheLevels = nLevels;
                m_isolineCacheScheme = isoSchemeRev;
            }
            const auto &segs = m_cachedSegs;

            std::vector<QSGGeometry::Point2D> verts;
            verts.reserve(segs.size() * 6);
            for (const auto &s : segs) {
                appendThickSeg(verts,
                               float(s.a.x()), float(s.a.y()),
                               float(s.b.x()), float(s.b.y()),
                               cHW);
            }
            uploadFlatVerts(contourNode, verts);
            // Slice Z.6a — line colour from spec; per-sublayer opacity
            // still applied so users can fade isolines independently.
            QColor isoColor = isoSpec.lineColor;
            if (isoSub) {
                isoColor.setAlpha(int(qBound(0.0, isoColor.alpha() * isoSub->opacity(), 255.0)));
            }
            setFlatColor(contourNode, isoColor);

            // ---- Pass 3b: per-level labels ---------------------------
            // Drop the previous label children first so toggling labels
            // off mid-session releases the textures.
            while (auto *c = contourLabels->firstChild()) {
                contourLabels->removeChildNode(c);
                delete c;
            }
            // Slice Z.6a — labelEveryN > 0 ⇒ wantLabels. Z.6a doesn't
            // yet thread the every-N-segments mode; preserves the
            // historic "one label per level at centroid" placement.
            const bool wantLabels = (isoSpec.labelEveryN > 0);
            if (wantLabels && window()) {
                // One label per level, placed at the centroid of all
                // segment midpoints at that level. Cheap and stable;
                // long contours just get one label each. Labels every-N-
                // pixels along the polyline is a follow-up.
                struct LevelAccum { double sumX = 0.0, sumY = 0.0; int count = 0; };
                QHash<double, LevelAccum> byLevel;
                byLevel.reserve(int(segs.size() / 4 + 1));
                for (const auto &s : segs) {
                    auto &acc = byLevel[s.level];
                    acc.sumX += 0.5 * (s.a.x() + s.b.x());
                    acc.sumY += 0.5 * (s.a.y() + s.b.y());
                    acc.count += 1;
                }
                const double dpr = window()->effectiveDevicePixelRatio();
                const QColor labelColor = isoSpec.lineColor;
                for (auto it = byLevel.constBegin(); it != byLevel.constEnd(); ++it) {
                    if (it.value().count <= 0) continue;
                    const double cx = it.value().sumX / double(it.value().count);
                    const double cy = it.value().sumY / double(it.value().count);
                    const QString text = QString::number(it.key(), 'g', 4);

                    QSGTexture *tex = m_labelTextureCache.value(text, nullptr);
                    if (!tex) {
                        const QImage img = rasteriseLabel(text, labelColor, dpr);
                        tex = window()->createTextureFromImage(
                            img, QQuickWindow::TextureHasAlphaChannel);
                        if (tex) m_labelTextureCache.insert(text, tex);
                    }
                    if (!tex) continue;

                    const QSizeF sz = tex->textureSize() / dpr;
                    const float w = float(sz.width())  * invView;
                    const float h = float(sz.height()) * invView;

                    auto *tn = new QSGSimpleTextureNode();
                    tn->setTexture(tex);
                    tn->setOwnsTexture(false); // cache owns it
                    tn->setRect(QRectF(cx - 0.5 * w, cy - 0.5 * h, w, h));
                    tn->setFiltering(QSGTexture::Linear);
                    contourLabels->appendChildNode(tn);
                }
            }
        } else {
            uploadFlatVerts(contourNode, std::vector<QSGGeometry::Point2D>{});
            // Hide labels too.
            while (auto *c = contourLabels->firstChild()) {
                contourLabels->removeChildNode(c);
                delete c;
            }
        }

        // ---- Pass 5: mesh-vertex markers (MeshNodeSublayer) --------------
        // Stylable replacement for the historic showMeshNodes toggle. Each
        // vertex is rendered as a triangle pair (quad / diamond) sized by
        // markerSizePx in screen pixels. Tagged vertices (SWMM-coupled)
        // get the taggedColor and taggedSizePx so coupled nodes stand out
        // when the user enables the layer.
        const bool nodesVisible = nodeSub && nodeSub->isVisible();
        if (nodesVisible) {
            const auto &nodes = m_layer->m_sceneNodes;
            // Slice Z.6a — base marker driven by nodeSpec (wraps Z.4's
            // MarkerSymbolLayerSpec). Tagged-vertex highlighting stays
            // on the legacy nodeStyle since MarkerSymbolLayerSpec
            // doesn't model the tagged variant — that's a deliberate
            // Rule Model design (tagged emphasis is layer-state, not
            // symbol style).
            const QColor baseC    = nodeSpec.marker.fillColor;
            const QColor taggedC  = nodeStyle ? nodeStyle->taggedColor()
                                              : QColor(0xff, 0x8c, 0, 235);
            const float  baseR    = float(nodeSpec.marker.sizePx) * 0.5f * invView;
            const float  taggedR  = nodeStyle ? float(nodeStyle->taggedSizePx()) * 0.5f * invView
                                              : 2.5f * invView;
            const bool   highlightTag = nodeStyle ? nodeStyle->highlightTagged() : true;
            const int    shape    = static_cast<int>(nodeSpec.marker.shape);
            const qreal  nodeOp   = nodeSub->opacity();
            const auto withOp = [nodeOp](QColor c) {
                c.setAlpha(int(qBound(0.0, c.alpha() * nodeOp, 255.0)));
                return c;
            };
            const QColor baseUsed   = withOp(baseC);
            const QColor taggedUsed = withOp(taggedC);

            std::vector<QSGGeometry::ColoredPoint2D> nodeVerts;
            nodeVerts.reserve(size_t(nodes.size()) * 6);

            auto emitCenteredQuad = [&](float cx, float cy, float r, const QColor &c) {
                const quint8 cr = quint8(c.red());
                const quint8 cg = quint8(c.green());
                const quint8 cb = quint8(c.blue());
                const quint8 ca = quint8(c.alpha());
                auto V = [&](float x, float y) {
                    QSGGeometry::ColoredPoint2D v;
                    v.set(x, y, cr, cg, cb, ca);
                    return v;
                };
                // shape 0/1 = square (fastest path covers Circle/Square at this size),
                // shape 2 = triangle (upward), shape 3 = diamond (square rotated 45°).
                if (shape == 2) {
                    nodeVerts.push_back(V(cx,     cy - r));
                    nodeVerts.push_back(V(cx + r, cy + r));
                    nodeVerts.push_back(V(cx - r, cy + r));
                } else if (shape == 3) {
                    nodeVerts.push_back(V(cx,     cy - r));
                    nodeVerts.push_back(V(cx + r, cy));
                    nodeVerts.push_back(V(cx - r, cy));
                    nodeVerts.push_back(V(cx + r, cy));
                    nodeVerts.push_back(V(cx,     cy + r));
                    nodeVerts.push_back(V(cx - r, cy));
                } else {
                    nodeVerts.push_back(V(cx - r, cy - r));
                    nodeVerts.push_back(V(cx + r, cy - r));
                    nodeVerts.push_back(V(cx - r, cy + r));
                    nodeVerts.push_back(V(cx + r, cy - r));
                    nodeVerts.push_back(V(cx + r, cy + r));
                    nodeVerts.push_back(V(cx - r, cy + r));
                }
            };

            for (const auto &n : nodes) {
                const double nx = n.pt.x();
                const double ny = n.pt.y();
                if (nx < cullX0 || nx > cullX1 || ny < cullY0 || ny > cullY1) continue;
                const bool tagged = n.tagged && highlightTag;
                emitCenteredQuad(float(nx - ox), float(ny - oy),
                                 tagged ? taggedR : baseR,
                                 tagged ? taggedUsed : baseUsed);
            }
            uploadColoredVerts(nodeMarkNode, nodeVerts);
        } else {
            uploadColoredVerts(nodeMarkNode, std::vector<QSGGeometry::ColoredPoint2D>{});
        }

        // ---- §V selection-overlay pass ------------------------------------
        // Cyan glyphs / cyan edge highlights drawn on top of every other
        // pass so the user always sees the selected element regardless of
        // the underlying fill/contour state. Two flat-coloured nodes:
        //   selEdgeNode — thick-segment quads for selected edges
        //   selVertNode — diamond marker (4-vertex screen-pixel quad) at
        //                 each selected vertex's scene position
        {
            const auto &selV = m_layer->highlightedVertices();
            const auto &selE = m_layer->highlightedEdges();
            const auto &selT = m_layer->highlightedTriangles();
            const auto &nodes  = m_layer->m_sceneNodes;
            const auto &triangles = m_layer->mesh().triangles;

            // Selected cells (triangles) — translucent fill drawn under the
            // edge / vertex glyphs. One filled triangle (3 verts) per cell.
            std::vector<QSGGeometry::Point2D> selTriVerts;
            selTriVerts.reserve(selT.size() * 3);
            for (int t : selT) {
                if (t < 0 || t >= triangles.size()) continue;
                const auto &tri = triangles[t];
                if (tri.v0 < 0 || tri.v1 < 0 || tri.v2 < 0 ||
                    tri.v0 >= nodes.size() || tri.v1 >= nodes.size() || tri.v2 >= nodes.size())
                    continue;
                auto pt = [&](int v) {
                    QSGGeometry::Point2D p;
                    p.x = float(nodes[v].pt.x() - ox);
                    p.y = float(nodes[v].pt.y() - oy);
                    return p;
                };
                selTriVerts.push_back(pt(tri.v0));
                selTriVerts.push_back(pt(tri.v1));
                selTriVerts.push_back(pt(tri.v2));
            }
            uploadFlatVerts(selTriNode, selTriVerts);
            setFlatColor(selTriNode, kSelTriColor);

            std::vector<QSGGeometry::Point2D> selEdgeSegs;
            std::vector<QSGGeometry::Point2D> selVertQuads;
            const float kSelEdgeHW = 1.6f * invView;   // ~3.2 px wide
            const float kSelVertHR = 5.0f * invView;   // ~5 px diamond half-extent

            selEdgeSegs.reserve(selE.size() * 6);
            selVertQuads.reserve(selV.size() * 6);

            // Selected edges first (under the vertex glyphs).
            for (int flat : selE) {
                const int t = flat / 3;
                const int e = flat % 3;
                if (t < 0 || t >= triangles.size()) continue;
                const auto &tri = triangles[t];
                int va = -1, vb = -1;
                switch (e) {
                case 0: va = tri.v1; vb = tri.v2; break;
                case 1: va = tri.v2; vb = tri.v0; break;
                case 2: va = tri.v0; vb = tri.v1; break;
                default: continue;
                }
                if (va < 0 || vb < 0 || va >= nodes.size() || vb >= nodes.size()) continue;
                const float ax = float(nodes[va].pt.x() - ox);
                const float ay = float(nodes[va].pt.y() - oy);
                const float bx = float(nodes[vb].pt.x() - ox);
                const float by = float(nodes[vb].pt.y() - oy);
                appendThickSeg(selEdgeSegs, ax, ay, bx, by, kSelEdgeHW);
            }

            // Selected vertices — diamond marker (axis-aligned bowtie, two
            // triangles forming a square rotated 45°). Cheap and visible
            // against both light and dark backgrounds.
            auto pushQuad = [&](float cx, float cy, float r) {
                auto v = [](float x, float y) {
                    QSGGeometry::Point2D p; p.x = x; p.y = y; return p;
                };
                // Top → right, top → left, right → bottom, left → bottom.
                selVertQuads.push_back(v(cx,     cy - r));
                selVertQuads.push_back(v(cx + r, cy));
                selVertQuads.push_back(v(cx - r, cy));
                selVertQuads.push_back(v(cx + r, cy));
                selVertQuads.push_back(v(cx,     cy + r));
                selVertQuads.push_back(v(cx - r, cy));
            };
            for (int vi : selV) {
                if (vi < 0 || vi >= nodes.size()) continue;
                pushQuad(float(nodes[vi].pt.x() - ox),
                         float(nodes[vi].pt.y() - oy),
                         kSelVertHR);
            }

            uploadFlatVerts(selEdgeNode, selEdgeSegs);
            uploadFlatVerts(selVertNode, selVertQuads);
            setFlatColor(selEdgeNode, kSelEdgeColor);
            setFlatColor(selVertNode, kSelVertColor);
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
