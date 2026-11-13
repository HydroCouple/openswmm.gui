/**
 * @file      swmm2dresultsqsgrenderer.cpp
 * @brief     Qt scene graph rendering of 2D simulation results.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "map/swmm2dresultsqsgrenderer.h"
#include "map/flowparticlenode.h"
#include "map/scalarbandmaterial.h"
#include "map/scalarfillmaterial.h"
#include <QCoreApplication>
#include "render/classificationcolorsampler.h"
#include "render/watergeometrychunks.h"

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
#include "render/sublayers/resultvectorsublayer.h"
#include <QSet>
#include <QJsonDocument>
#include <QSGOpacityNode>

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
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <numeric>
#include <utility>
#include <vector>

using OpenSWMM::Render::ContourJobInput;
using OpenSWMM::Render::ContourJobOutput;
using OpenSWMM::Render::MeshRenderChunkIndex;
using OpenSWMM::Render::PackedBandVertex;
using OpenSWMM::Render::Qsg2DDirtyState;
using OpenSWMM::Render::Qsg2DLodDecision;
using OpenSWMM::Render::Qsg2DLodInputs;
using OpenSWMM::Render::Qsg2DLodPolicy;
using OpenSWMM::Render::Qsg2DRenderStats;
using OpenSWMM::Render::computeContourJob;
using OpenSWMM::Render::premul;

namespace
{

// ---------------------------------------------------------------------------
// Node helpers (same idioms as SWMM2DMeshQSGRenderer)
// ---------------------------------------------------------------------------

/**
 * @return New caller-owned node owning empty DrawTriangles ColoredPoint2D geometry and
 *   QSGVertexColorMaterial. Parent root assumes ownership when attached; caller supplies
 *   premultiplied colours. Allocate/use/destroy in render-resource context.
 */
QSGGeometryNode *makeColoredNode()
{
    auto *node = new QSGGeometryNode();
    auto *geo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geo->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *mat = new QSGVertexColorMaterial();
    node->setMaterial(mat);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

class WaterFillMaterial final : public QSGVertexColorMaterial
{
public:
    /**
     * @return Borrowed process-static unique material-type identity for water fills; never free.
     *   Separates this material family from ordinary vertex-colour materials, no mutation.
     */
    QSGMaterialType *type() const override
    {
        static QSGMaterialType type;
        return &type;
    }
    /**
     * @param[in] other Material pointer for ordering; Qt supplies compatible material family,
     *   pointer is compared without dereference.
     * @return Zero only for identical pointer, otherwise -1 or 1 according to std::less pointer
     *   order. Prevents separate water materials sharing upload staging; no value-equivalence
     *   batching or mutation.
     */
    int compare(const QSGMaterial *other) const override
    {
        if (this == other)
            return 0;
        return std::less<const QSGMaterial *>{}(this, other) ? -1 : 1;
    }
};

/**
 * @return New caller-owned coloured triangle node with unique WaterFillMaterial marked NoBatching.
 *   Node owns geometry/material, parent assumes node ownership on attachment. Unique comparisons
 *   keep upload staging per chunk; render-resource context only.
 */
QSGGeometryNode *makeWaterFillNode()
{
    auto *node = makeColoredNode();
    // NoBatching disables vertex merging, but Qt can still group compatible
    // nodes in one upload buffer. Unique material comparisons also separate
    // those groups, keeping shared upload staging bounded to one chunk.
    auto *material = new WaterFillMaterial;
    material->setFlag(QSGMaterial::NoBatching);
    node->setMaterial(material);
    return node;
}

/**
 * @return New caller-owned node with empty indexed (16-bit) ScalarFillVertex triangle geometry and
 *   its own ScalarBandMaterial; the parent assumes ownership when attached.
 */
QSGGeometryNode *makeScalarBandNode()
{
    auto *node = new QSGGeometryNode();
    auto *geo = new QSGGeometry(scalarFillAttributes(), 0, 0, QSGGeometry::UnsignedShortType);
    geo->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new ScalarBandMaterial());
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

/** @brief Remove and delete every child of node. */
void deleteChildren(QSGNode *node)
{
    while (auto *child = node->firstChild()) {
        node->removeChildNode(child);
        delete child;
    }
}

/**
 * @return False when OPENSWMM_QSG_GPU_BANDS=0: filled contours of the fast surface are then marched
 *   on the CPU as on smaller meshes (parity checks, or a GPU driver without these shaders).
 */
bool gpuBandsEnabled()
{
    return qEnvironmentVariable("OPENSWMM_QSG_GPU_BANDS") != QLatin1String("0");
}

/*! Block size of per-frame passes split over the thread pool. */
constexpr size_t kPassBlock = 65536;

/** @return Number of kPassBlock-sized blocks covering n elements. */
constexpr size_t blockCount(size_t n)
{
    return (n + kPassBlock - 1) / kPassBlock;
}

/**
 * @brief Call body(c) once for every c in [0, n): on the global thread pool when parallel, else
 *   in order on this thread. Each call must touch only its own c's data.
 */
template <typename Body> void forEachIndex(size_t n, bool parallel, const Body &body)
{
    if (!parallel || n < 2) {
        for (size_t c = 0; c < n; ++c)
            body(c);
        return;
    }
    std::vector<size_t> ids(n);
    std::iota(ids.begin(), ids.end(), size_t(0));
    QtConcurrent::blockingMap(ids, [&body](size_t c) { body(c); });
}

/**
 * @param[in] color Flat QColor including alpha, copied into new material.
 * @return New caller-owned node owning empty DrawTriangles Point2D geometry and
 *   QSGFlatColorMaterial. Parent assumes ownership when attached. Render-resource operation; no
 *   model mutation.
 */
QSGGeometryNode *makeFlatNode(QColor color)
{
    auto *node = new QSGGeometryNode();
    auto *geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geo->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geo);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *mat = new QSGFlatColorMaterial();
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
 *  between the indexed and expanded paths).
 * @tparam V Trivially copyable vertex type matching geometry layout.
 * @param[in] geo Required mutable geometry with allocated layout matching stride.
 * @param[in] data Readable n * stride source bytes, null allowed only if n == 0.
 * @param[in] n Nonnegative vertex count fitting QSG allocation.
 * @param[in] stride Bytes per vertex matching source and geometry.
 * @note Reallocates when count differs OR previous indexCount nonzero, thereby removing old
 *   indices. Copies when n > 0, does not mark node dirty/chunk/report GPU completion.
 *   Render-resource caller owns buffers; no rollback guarantee.
 */
template <typename V> void uploadChunk(QSGGeometry *geo, const V *data, int n, size_t stride)
{
    if (geo->vertexCount() != n || geo->indexCount() != 0)
        geo->allocate(n);
    if (n > 0)
        std::memcpy(geo->vertexData(), data, size_t(n) * stride);
}

/*! Chunked upload: the first kMaxVertsPerNode vertices land in \p node
 *  itself; the rest spill into child geometry nodes (created on demand via
 *  \p makeNode, excess children pruned). Works for both vertex layouts.
 * @tparam V Vertex type matching node and every overflow child geometry.
 * @tparam MakeNode Synchronous factory returning a new empty matching geometry/material node.
 * @param[in,out] node Required geometry root; every existing child must be a matching reusable
 *   geometry node.
 * @param[in] verts Immutable triangle stream, size must fit int and be a multiple of3.
 * @param[in] makeNode Factory for overflow children; returned nodes become root-owned.
 * @note Uploads chunks of at most65532vertices, reuses existing overflow children and
 *   removes/deletes surplus children. Marks DirtyGeometry, plus DirtyMaterial on empty-to-positive
 *   transitions. No GPU completion guarantee or exception rollback; render-resource context
 *   required.
 */
template <typename V, typename MakeNode>
void uploadVertsChunked(QSGGeometryNode *node, const std::vector<V> &verts, MakeNode makeNode)
{
    const int total = int(verts.size());
    const int inRoot = std::min(total, kMaxVertsPerNode);
    // An element whose geometry was EMPTY has no batch, and DirtyGeometry on
    // an unbatched element is ignored by Qt's batch renderer — the node would
    // never paint once it fills. DirtyMaterial is what requests a batch
    // rebuild for it (same fix as SWMMLayerQSGRenderer's uploadVerts).
    auto dirtyFor = [](bool wasEmpty, int n) {
        return (wasEmpty && n > 0) ? (QSGNode::DirtyGeometry | QSGNode::DirtyMaterial)
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
            cg = static_cast<QSGGeometryNode *>(child);
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
    while (child) { // shrink: drop no-longer-needed overflow nodes
        QSGNode *next = child->nextSibling();
        node->removeChildNode(child);
        delete child;
        child = next;
    }
}

// Direct fills can contain millions of triangles. Keep just one upload
// chunk on the CPU while preserving the same primitive order and GPU nodes.
// Share only bit-identical GPU XY/RGBA attributes, including at clipped
// boundaries. Hydraulic corners and triangle connectivity never change.
class ColoredVertexUpload
{
public:
    /**
     * @param[in] root Required borrowed water-fill root with ColoredPoint2D/UnsignedShortType
     *   geometry; existing direct children must match family.
     * @brief Start one serialized upload session, retaining root and first overflow child and
     *   binding builder callback to this.
     * @note Does not take node ownership, upload, clear old geometry or auto-finish on destruction.
     *   Root and session must outlive synchronous builder callbacks. Use either push_back path OR
     *   appendIndexedChunk path in one session; mixing can leave pending builder references
     *   unflushed.
     */
    explicit ColoredVertexUpload(QSGGeometryNode *root)
        : root_(root), next_(root->firstChild()),
          builder_([this](const auto &vertices, const auto &indices) { flush(vertices, indices); })
    {
    }
    /**
     * @param[in] count Requested pending capacity, capped by WaterGeometryBuilder at 65532
     *   references/vertices.
     * @brief Reserve one bounded builder chunk without uploading or changing counts; allocation
     *   failure propagates.
     */
    void reserve(size_t count) { builder_.reserve(count); }
    /**
     * @param[in] vertex Finite anchored XY and initialized premultiplied RGBA value, copied/bitwise
     *   deduplicated within current chunk.
     * @brief Append one triangle vertex reference; full chunks flush synchronously to root/overflow
     *   nodes.
     * @note Caller supplies references in triples and calls finish for final chunk. Deduplication
     *   distinguishes exact 12-byte patterns, including signed zero; allocation/callback failure
     *   can leave partial external uploads.
     */
    void push_back(const QSGGeometry::ColoredPoint2D &vertex) { builder_.push_back(vertex); }
    /**
     * @param[in] chunk Immutable packed-band XY/band-index vertices and valid 16 bit triangle
     *   indices; sizes fit int and chunk limit.
     * @param[in] colors Nonempty straight-alpha palette; bandIndex upper-clamped to last colour, no
     *   empty-palette guard.
     * @brief Convert palette vertices to premultiplied colours and synchronously upload supplied
     *   connectivity.
     * @note Empty index list no-op. Root/overflow own GPU copies, input retained by caller. Tracks
     *   direct vertex/reference/byte statistics; does not feed pending builder, so do not mix with
     *   push_back path.
     */
    void appendIndexedChunk(
        const OpenSWMM::Render::WaterGeometryChunk<OpenSWMM::Render::PackedBandVertex> &chunk,
        const QVector<QColor> &colors)
    {
        if (chunk.indices.empty())
            return;
        std::vector<QSGGeometry::ColoredPoint2D> vertices(chunk.vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i) {
            const auto &source = chunk.vertices[i];
            const auto color = colors[std::min(int(source.bandIndex), int(colors.size()) - 1)];
            const quint8 alpha = quint8(color.alpha());
            vertices[i].set(source.x, source.y, premul(quint8(color.red()), alpha),
                            premul(quint8(color.green()), alpha),
                            premul(quint8(color.blue()), alpha), alpha);
        }
        flush(vertices, chunk.indices);
        directVertices_ += vertices.size();
        directReferences_ += chunk.indices.size();
        directBytes_ +=
            vertices.size() * sizeof(vertices[0]) + chunk.indices.size() * sizeof(quint16);
    }
    /**
     * @param[in] vertices Premultiplied anchored vertices, at most 65,532.
     * @param[in] indices Nonempty valid 16 bit triangle references into vertices.
     * @brief Upload one ready indexed chunk directly, like appendIndexedChunk; do not mix with
     *   the push_back path in one session.
     */
    void appendColoredChunk(const std::vector<QSGGeometry::ColoredPoint2D> &vertices,
                            const std::vector<quint16> &indices)
    {
        if (indices.empty())
            return;
        flush(vertices, indices);
        directVertices_ += vertices.size();
        directReferences_ += indices.size();
        directBytes_ += vertices.size() * sizeof(vertices[0]) + indices.size() * sizeof(quint16);
    }
    /**
     * @return Flushed unique builder vertices plus direct uploaded vertices, counting duplicates
     *   again across chunks. Pending builder vertices excluded until finish/automatic flush.
     */
    [[nodiscard]] size_t size() const { return builder_.size() + directVertices_; }
    /**
     * @return Uploaded vertex/index payload bytes across flushed builder and direct chunks,
     *   excluding pending vectors, capacities, hash table and Qt/GPU overhead.
     */
    [[nodiscard]] size_t bytes() const { return builder_.bytes() + directBytes_; }
    /**
     * @return True if neither builder nor direct path has ever accepted a reference in this
     *   session. Does not inspect existing node geometry or pending/flushed distinction.
     */
    [[nodiscard]] bool empty() const { return builder_.empty() && directReferences_ == 0; }
    /**
     * @brief Flush final builder chunk (including one empty clear if unused), then remove/delete
     *   surplus old overflow nodes.
     * @note Builder flush skipped when any direct chunk accepted, so mixed paths are unsupported.
     *   Explicit one-shot end-of-session use expected; repeated direct-path finish is not a new
     *   clear. Render-resource context, no GPU wait.
     */
    void finish()
    {
        if (!directReferences_)
            builder_.finish();
        while (next_) {
            auto *old = next_;
            next_ = next_->nextSibling();
            root_->removeChildNode(old);
            delete old;
        }
    }

private:
    /**
     * @param[in] vertices Immutable distinct ColoredPoint2D values matching 16 bit connectivity,
     *   size fits int.
     * @param[in] indices Valid quint16 triangle references into vertices, size fits int.
     * @brief Upload one indexed chunk to root first, then reused/new root-owned overflow water
     *   nodes.
     * @note Asserts UnsignedShortType, allocates when either count differs, copies both arrays,
     *   marks vertex/index data and node geometry dirty plus material on first nonempty indices. No
     *   input ownership, GPU completion or rollback. Root child layouts are unchecked casts.
     */
    void flush(const std::vector<QSGGeometry::ColoredPoint2D> &vertices,
               const std::vector<quint16> &indices)
    {
        QSGGeometryNode *node = root_;
        if (started_) {
            if (next_) {
                node = static_cast<QSGGeometryNode *>(next_);
                next_ = next_->nextSibling();
            } else {
                node = makeWaterFillNode();
                root_->appendChildNode(node);
            }
        }
        started_ = true;
        auto *geometry = node->geometry();
        const bool wasEmpty = geometry->vertexCount() == 0;
        Q_ASSERT(geometry->indexType() == QSGGeometry::UnsignedShortType);
        if (geometry->vertexCount() != int(vertices.size()) ||
            geometry->indexCount() != int(indices.size()))
            geometry->allocate(int(vertices.size()), int(indices.size()));
        if (!vertices.empty())
            std::memcpy(geometry->vertexData(), vertices.data(),
                        vertices.size() * sizeof(vertices[0]));
        if (!indices.empty())
            std::memcpy(geometry->indexData(), indices.data(), indices.size() * sizeof(indices[0]));
        geometry->markVertexDataDirty();
        geometry->markIndexDataDirty();
        node->markDirty(wasEmpty && !indices.empty()
                            ? QSGNode::DirtyState(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial)
                            : QSGNode::DirtyState(QSGNode::DirtyGeometry));
    }
    QSGGeometryNode *root_;
    QSGNode *next_;
    OpenSWMM::Render::WaterGeometryBuilder<QSGGeometry::ColoredPoint2D> builder_;
    size_t directVertices_ = 0, directReferences_ = 0, directBytes_ = 0;
    bool started_ = false;
};

/**
 * @param[in,out] node Required root with ColoredPoint2D geometry and vertex-colour material;
 *   matching overflow children only.
 * @param[in] verts Premultiplied triangle vertices, size fits int and is a multiple of3.
 * @note Chunked helper creates coloured children and prunes extras. Caller retains input, root owns
 *   resource tree; render-thread only.
 */
void uploadColoredVerts(QSGGeometryNode *node,
                        const std::vector<QSGGeometry::ColoredPoint2D> &verts)
{
    uploadVertsChunked(node, verts, []() { return makeColoredNode(); });
}

/**
 * @param[in,out] node Required Point2D root with actual QSGFlatColorMaterial; every child matches
 *   that family.
 * @param[in] verts Immutable triangle stream, size fits int and is a multiple of3.
 * @note Chunked upload clones root current material colour into newly created children; later
 *   setFlatColor updates reused children. Removes surplus children, marks appropriate dirtiness;
 *   render-resource context only.
 */
void uploadFlatVerts(QSGGeometryNode *node, const std::vector<QSGGeometry::Point2D> &verts)
{
    // Overflow children clone the pass node's current flat colour; a later
    // colour change reaches them through setFlatColor below.
    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    const QColor c = mat->color();
    uploadVertsChunked(node, verts, [c]() { return makeFlatNode(c); });
}

/**
 * @param[in,out] node Required root and direct children with QSGFlatColorMaterial, unchecked casts.
 * @param[in] c New flat QColor including alpha.
 * @note Updates root and each direct overflow child only if colour differs, marking DirtyMaterial.
 *   Does not traverse grandchildren, upload vertices or transfer ownership; render-resource context
 *   required.
 */
void setFlatColor(QSGGeometryNode *node, QColor c)
{
    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    if (mat->color() != c) {
        mat->setColor(c);
        node->markDirty(QSGNode::DirtyMaterial);
    }
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

/**
 * @param[in,out] out Append-only Point2D triangle list; existing vertices remain.
 * @param[in] ax First endpoint X.
 * @param[in] ay First endpoint Y.
 * @param[in] bx Second endpoint X.
 * @param[in] by Second endpoint Y.
 * @param[in] hw Finite nonnegative HALF-width, in the same anchored scene units as endpoints.
 * @note Finite endpoint precondition. Length<1e-9emits nothing; otherwise appends six vertices for
 *   a butt-cap perpendicular quad. No clipping, joins, colour, GPU upload or validation of
 *   negative/nonfinite width; allocation failure can leave a partial append.
 */
void appendThickSeg(std::vector<QSGGeometry::Point2D> &out, float ax, float ay, float bx, float by,
                    float hw)
{
    const float dx = bx - ax, dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9f)
        return;
    const float nx = -dy / len * hw, ny = dx / len * hw;
    auto v = [](float x, float y) {
        QSGGeometry::Point2D p;
        p.x = x;
        p.y = y;
        return p;
    };
    out.push_back(v(ax + nx, ay + ny));
    out.push_back(v(bx + nx, by + ny));
    out.push_back(v(ax - nx, ay - ny));
    out.push_back(v(bx + nx, by + ny));
    out.push_back(v(bx - nx, by - ny));
    out.push_back(v(ax - nx, ay - ny));
}

/**
 * @brief Append a constant-colour butt-cap stroke quad without clearing prior triangles.
 * @param[in,out] out Append-only premultiplied vertex list; six vertices per accepted segment.
 * @param[in] ax First endpoint X in anchored scene units.
 * @param[in] ay First endpoint Y.
 * @param[in] bx Second endpoint X.
 * @param[in] by Second endpoint Y.
 * @param[in] hw Finite nonnegative half-width in endpoint units; not locally validated.
 * @param[in] r Already-premultiplied red byte.
 * @param[in] g Already-premultiplied green byte.
 * @param[in] b Already-premultiplied blue byte.
 * @param[in] a Alpha byte.
 * @note Finite endpoints expected. Length below 1e-9 emits nothing; otherwise colours copied verbatim. No joins, clipping, GPU upload or input validation; allocation failure may leave partial append.
 */
void appendThickSegColored(std::vector<QSGGeometry::ColoredPoint2D> &out, float ax, float ay,
                           float bx, float by, float hw, quint8 r, quint8 g, quint8 b, quint8 a)
{
    const float dx = bx - ax, dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9f)
        return;
    const float nx = -dy / len * hw, ny = dx / len * hw;
    auto v = [&](float x, float y) {
        QSGGeometry::ColoredPoint2D p;
        p.set(x, y, r, g, b, a);
        return p;
    };
    out.push_back(v(ax + nx, ay + ny));
    out.push_back(v(bx + nx, by + ny));
    out.push_back(v(ax - nx, ay - ny));
    out.push_back(v(bx + nx, by + ny));
    out.push_back(v(bx - nx, by - ny));
    out.push_back(v(ax - nx, ay - ny));
}

struct ScalarResultNodes : QSGNode {
    QSGGeometryNode *fill = makeColoredNode();
    QSGGeometryNode *smooth = makeColoredNode();
    QSGGeometryNode *bands = makeColoredNode();
    QSGGeometryNode *lines = makeFlatNode(Qt::black);
    QSGOpacityNode *labels = new QSGOpacityNode;
    /**
     * @brief Attach precreated flat fill, smooth fill, filled contour, contour-line and
     *   opacity-label nodes in that (bottom-up) order.
     * @note Parent ScalarResultNodes owns descendants; fill/line nodes own geometry/material and
     *   label children may own textures. Render-resource context; no layer binding or publication.
     */
    ScalarResultNodes()
    {
        appendChildNode(fill);
        appendChildNode(smooth);
        appendChildNode(bands);
        appendChildNode(lines);
        appendChildNode(labels);
    }
    /**
     * @brief Remove/delete every label child, releasing node-owned scalar label textures with it.
     * @note Keeps opacity parent/fill/lines and its opacity; no shared depth-label texture cache
     *   mutation. Render-resource context only.
     */
    void clearLabels()
    {
        while (auto *node = labels->firstChild()) {
            labels->removeChildNode(node);
            delete node;
        }
    }
};
/// Static streaks under arrows of one vector result row.
struct VectorResultNodes : QSGNode {
    QSGGeometryNode *streaks = makeColoredNode();
    QSGGeometryNode *arrows = makeColoredNode();
    VectorResultNodes()
    {
        appendChildNode(streaks);
        appendChildNode(arrows);
    }
};
struct ResultsRootNode : QSGTransformNode {
    QSGNode *flowNode = nullptr;
    FlowParticleNode *particleNode = nullptr;
    QHash<QString, ScalarResultNodes *> resultScalarNodes;
    QHash<QString, VectorResultNodes *> resultVectorNodes;
    QSGGeometryNode *cellFillNode = nullptr;
    QSGGeometryNode *smoothFillNode = nullptr;
    QSGGeometryNode *bandNode = nullptr;
    QSGNode *gpuFillNode = nullptr; // fast-surface classified smooth fill, ScalarBandMaterial
    QSGNode *gpuBandNode = nullptr; // fast-surface filled contours, ScalarBandMaterial chunks
    QSGNode *gpuIsoNode = nullptr;  // fast-surface isolines, ScalarBandMaterial chunks
    QSGGeometryNode *isoNode = nullptr;
    QSGGeometryNode *isoIndexNode = nullptr;
    QSGOpacityNode *isoLabels = nullptr; // the isoline pass's opacity
    QSGGeometryNode *edgeNode = nullptr;
    QSGGeometryNode *nodeMarkNode = nullptr;
    QSGGeometryNode *velNode = nullptr;
    QSGGeometryNode *hiFillNode = nullptr;
    QSGGeometryNode *hiEdgeNode = nullptr;
};

// ---------------------------------------------------------------------------
// Isoline label rasterisation — parametrised variant of the mesh renderer's
// helper (font size + halo from IsolineStyle).
// ---------------------------------------------------------------------------

/**
 * @param[in] text Glyph string measured with default-family bold font.
 * @param[in] color Straight glyph fill QColor.
 * @param[in] fontPt Positive finite font size in points expected; not validated.
 * @param[in] halo True draws one 3 logical-pixel white 230 alpha outline before glyph fill.
 * @param[in] dpr Positive finite device-pixel ratio; representable image dimensions required.
 * @return Owned ARGB32 premultiplied image with ceil glyph bounds plus 3 logical-pixel padding per
 *   side, scaled by dpr (integer truncation) and tagged with it. Allocation failure has no separate
 *   error channel. Synchronous Qt font/image operation, no GPU texture or cache mutation.
 */
QImage rasteriseLabel(const QString &text, const QColor &color, double fontPt, bool halo,
                      double dpr)
{
    QFont f;
    f.setPointSizeF(fontPt);
    f.setBold(true);
    const QFontMetricsF fm(f);
    const QRectF br = fm.boundingRect(text);
    const int pad = 3;
    const int w = int(std::ceil(br.width())) + pad * 2;
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
 *  triangles); OPENSWMM_QSG_ASYNC_CONTOURS=1 forces it on at any size (useful
 *  for testing on small demos), =0 forces the synchronous path. */
constexpr int kAsyncContourThreshold = 50'000;
/**
 * @param[in] nTri Total display-triangle count, not merely wet/visible triangles.
 * @return False for cached environment mode 0, true for mode >= 1, otherwise nTri >= 50000.
 *   OPENSWMM_QSG_ASYNC_CONTOURS is sampled once on first call; invalid present integer parses as 0.
 *   Does not start a job; sync additionally gates on marching need, compatible frame and size.
 */
bool asyncContoursEnabled(int nTri)
{
    static const int kMode = qEnvironmentVariableIsSet("OPENSWMM_QSG_ASYNC_CONTOURS")
                                 ? qEnvironmentVariableIntValue("OPENSWMM_QSG_ASYNC_CONTOURS")
                                 : -1;
    if (kMode == 0)
        return false;
    if (kMode >= 1)
        return true;
    return nTri >= kAsyncContourThreshold;
}

} // namespace

// ---------------------------------------------------------------------------

struct SWMM2DResultsQSGRenderer::ContourFrame {
    ContourJobKey key;
    std::shared_ptr<const OpenSWMM::Render::FlowParticleNativeInput> particles;
    bool immediate = false;
    bool sharedCorners = false; // captured from the layer's fast surface
    std::shared_ptr<const std::vector<std::array<float, 3>>> scalars;
    // Only dynamic attributes are retained; geometry stays shared by revision.
    std::vector<std::array<float, 4>> cellValues; // depth, vx, vy, speed per display tri
    std::vector<int> immediateTriangles;
    std::shared_ptr<const std::vector<int>> selection;
    /**
     * @return Borrowed shared selection vector when present, otherwise immediateTriangles. IDs
     *   index layer display fan triangles; valid only while frame/selection retained and
     *   unmodified. No copy or topology validation.
     */
    const std::vector<int> &triangles() const
    {
        return selection ? *selection : immediateTriangles;
    }
    QVector<double> depthSamples;
    SWMM2DResultsLayer::VelocityField velocity;
    double maxVelocity = 0;
    bool hasVelocity = false;
    std::shared_ptr<const ContourJobOutput> contourPacket;
    /**
     * @return Borrowed immutable contour packet or process-static empty packet when absent. Caller
     *   retains frame/packet while reading; no compute, copy, wait or publication.
     */
    const ContourJobOutput &contours() const
    {
        static const ContourJobOutput empty;
        return contourPacket ? *contourPacket : empty;
    }
};

size_t SWMM2DResultsQSGRenderer::retainedWaterTriangleCount() const
{
    return m_contourFrame ? m_contourFrame->triangles().size() : 0;
}

size_t SWMM2DResultsQSGRenderer::retainedWaterAttributeBytes() const
{
    if (!m_contourFrame)
        return 0;
    return (m_contourFrame->scalars
                ? m_contourFrame->scalars->capacity() * sizeof(std::array<float, 3>)
                : 0) +
           m_contourFrame->cellValues.capacity() * sizeof(std::array<float, 4>);
}

size_t SWMM2DResultsQSGRenderer::contourWorkingBytes() const
{
    size_t bytes =
        (m_visibleCellsScratch.capacity() + m_contourSelectionScratch.capacity()) * sizeof(int);
    if (m_contourPositions)
        bytes += m_contourPositions->capacity() * sizeof(ContourJobInput::TriPos);
    if (m_contourTriangles)
        bytes += m_contourTriangles->capacity() * sizeof(int);
    for (const auto &buffer : m_contourScalarBuffers)
        if (!m_contourFrame || m_contourFrame->scalars != buffer)
            bytes += buffer->capacity() * sizeof(std::array<float, 3>);
    // Include the displayed packet and spare writer storage: neither is
    // counted by the per-triangle attribute diagnostic.
    // A worker may be changing its vectors. Read only completed capacities.
    for (const auto &buffer : m_contourOutputBuffers)
        bytes += buffer.completedBytes;
    return bytes;
}

void SWMM2DResultsQSGRenderer::finishContourOutput(const ContourJobOutput &packet,
                                                   bool bootstrapSpare)
{
    const auto bytes = [](const ContourJobOutput &output) {
        size_t count =
            output.packedBands.capacity() * sizeof(decltype(output.packedBands)::value_type) +
            output.segs.capacity() * sizeof(decltype(output.segs)::value_type);
        for (const auto &chunk : output.packedBands)
            count += chunk.vertices.capacity() * sizeof(PackedBandVertex) +
                     chunk.indices.capacity() * sizeof(quint16);
        return count;
    };
    bool owned = false;
    for (auto &buffer : m_contourOutputBuffers)
        if (buffer.packet.get() == &packet) {
            buffer.completedBytes = bytes(packet);
            owned = true;
            break;
        }
    if (!owned || !bootstrapSpare || m_contourOutputBuffers.size() != 1 ||
        packet.packedBands.empty())
        return;
    // Producer and display need independent packets. Bootstrap one reusable
    // writer from the first observed output, rather than repeatedly allocating
    // millions of coordinates during navigation. Every job replaces its data.
    auto spare = std::make_shared<ContourJobOutput>();
    spare->packedBands = packet.packedBands;
    const size_t spareBytes = bytes(*spare);
    m_contourOutputBuffers.push_back({std::move(spare), spareBytes});
}

SWMM2DResultsQSGRenderer::SWMM2DResultsQSGRenderer(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    connect(&m_contourWatcher, &QFutureWatcherBase::finished, this, [this]() {
        // Disconnecting a handled future installs an empty canceled future.
        // Its queued notifications must not finish a newer running request.
        if (!m_contourWatcher.isFinished() || m_contourWatcher.isCanceled())
            return;
        m_contourBusy = false;
        const auto result = m_contourWatcher.result();
        // The frame owns accepted output. The watcher must not keep a stale
        // rejected packet alive until the next job needs another writer.
        m_contourWatcher.setFuture({});
        // Accept completed intermediate frames during fast playback, but never
        // cross a geometry/style epoch or replace a newer synchronous frame.
        const bool accept =
            result && result->key.samePresentation(m_requestedContourKey) &&
            (!m_contourFrame || result->key.frameRev > m_contourFrame->key.frameRev ||
             (result->key.frameRev == m_contourFrame->key.frameRev &&
              !(m_contourFrame->key == m_requestedContourKey)));
        if (result && result->contourPacket)
            finishContourOutput(*result->contourPacket, accept);
        if (accept)
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
        if (t)
            t->deleteLater();
    m_labelTextureCache.clear();
}

bool SWMM2DResultsQSGRenderer::ensureSharedFillChunks(double ox, double oy)
{
    auto &sf = m_sharedFill;
    if (!m_layer)
        return false;
    const auto rev = m_layer->geomRevision();
    if (sf.geomRev == rev && sf.anchorX == ox && sf.anchorY == oy)
        return !sf.vertices.empty();
    sf = {};
    const auto &ids = m_layer->triVertexIndices();
    const auto &points = m_layer->m_sceneVerts;
    const size_t nTri = size_t(m_layer->m_sceneTris.size());
    if (ids.size() != nTri || points.isEmpty() || m_chunksRev != rev || m_chunks.chunks().isEmpty())
        return false;
    sf.triChunk.assign(nTri, -1);
    sf.triLocal.resize(nTri);
    std::vector<int> local(size_t(points.size()), -1), used;
    auto startChunk = [&] {
        for (int v : used)
            local[size_t(v)] = -1;
        used.clear();
        sf.vertices.emplace_back();
    };
    startChunk();
    for (const auto &chunk : m_chunks.chunks())
        for (int t : chunk.cellIds) {
            if (t < 0 || size_t(t) >= nTri)
                continue;
            const auto &tri = ids[size_t(t)];
            bool valid = true;
            int fresh = 0;
            for (int v : tri) {
                valid = valid && v >= 0 && v < points.size();
                fresh += valid && local[size_t(v)] < 0;
            }
            if (!valid)
                continue;
            if (int(sf.vertices.back().size()) + fresh > kMaxVertsPerNode)
                startChunk();
            auto &vertices = sf.vertices.back();
            for (size_t k = 0; k < 3; ++k) {
                const int v = tri[k];
                if (local[size_t(v)] < 0) {
                    local[size_t(v)] = int(vertices.size());
                    used.push_back(v);
                    QSGGeometry::ColoredPoint2D p;
                    p.set(float(points[v].x() - ox), float(points[v].y() - oy), 0, 0, 0, 0);
                    vertices.push_back(p);
                }
                sf.triLocal[size_t(t)][k] = quint16(local[size_t(v)]);
            }
            sf.triChunk[size_t(t)] = int(sf.vertices.size()) - 1;
        }
    sf.chunkTris.resize(sf.vertices.size());
    for (size_t t = 0; t < nTri; ++t) // ascending: a frame's wet triangles in id order
        if (sf.triChunk[t] >= 0)
            sf.chunkTris[size_t(sf.triChunk[t])].push_back(int(t));
    sf.scanValues.resize(sf.vertices.size());
    sf.scanIndices.resize(sf.vertices.size());
    sf.frameVertices.resize(sf.vertices.size());
    sf.frameIndices.resize(sf.vertices.size());
    sf.geomRev = rev;
    sf.anchorX = ox;
    sf.anchorY = oy;
    return true;
}

void SWMM2DResultsQSGRenderer::setLayer(SWMM2DResultsLayer *layer)
{
    if (m_layer == layer)
        return;
    if (m_layer) {
        for (auto *base : m_layer->sublayers()) {
            QObject::disconnect(base, nullptr, this, nullptr);
            if (auto *sub = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(base))
                sub->publishPresentedFrame({});
        }
        QObject::disconnect(m_layer, nullptr, this, nullptr);
    }
    m_layer = layer;
    m_sharedFill = {}; // keyed by geometry revision, which is per layer
    if (m_layer) {
        // Edge styling must refresh even in the same frame as selection or
        // animation. Pure animation keeps its existing Data-only fast path.
        if (auto *edges = m_layer->meshEdgeSublayer())
            connect(edges, &OpenSWMM::Render::ISublayer::invalidated, this, [this] {
                m_dirty.noteStyleChanged();
                noteContentChanged();
            });
        connect(m_layer, &SWMM2DResultsLayer::flowStreaksChanged, this, [this] {
            if (!m_separateFlow || m_waitingForParticles) {
                if (m_waitingForParticles)
                    m_dirty.noteDataChanged();
                noteContentChanged();
                emit contentReady();
            }
        });
        connect(m_layer, &SWMM2DResultsLayer::waterDisplayPolicyChanged, this,
                &SWMM2DResultsQSGRenderer::forceRebuild);
        // Ambiguous invalidation — style edits, sublayer toggles, etc.
        // funnel into repaintRequested. updatePaintNode classifies it into
        // the narrowest dirty domain by diffing snapshots (geometry
        // revision / highlight set / time index).
        connect(m_layer, &OpenSWMMVisLayer::repaintRequested, this, [this]() {
            if (m_separateFlow && m_layer && m_layer->flowOnlyRepaint())
                return;
            m_dirty.noteExternalChanged();
            noteContentChanged();
        });
        // Animation ticks land via currentTimeChanged (setCurrentTimeIndex
        // does not emit repaintRequested) — a pure Data-domain event.
        connect(m_layer, &SWMM2DResultsLayer::currentTimeChanged, this, [this](int) {
            m_dirty.noteDataChanged();
            noteContentChanged();
        });
        // Selection-domain event: rebuilds ONLY the highlight overlay.
        connect(m_layer, &SWMM2DResultsLayer::highlightedCellsChanged, this, [this]() {
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
    if (extent == m_extent)
        return;
    const bool zoomChanged = !qFuzzyCompare(extent.width(), m_extent.width()) ||
                             !qFuzzyCompare(extent.height(), m_extent.height());
    m_extent = extent;
    // Content stays untouched here: updatePaintNode decides — via the LOD
    // key and the coverage rect — whether this extent change needs any
    // rebuild at all. Pure pans and same-LOD zooms end up matrix-only.
    m_dirty.noteExtentChanged(zoomChanged);
    update();
}

void SWMM2DResultsQSGRenderer::setSeparateFlow(bool separate)
{
    if (m_separateFlow == separate)
        return;
    m_separateFlow = separate;
    noteContentChanged();
}

void SWMM2DResultsQSGRenderer::finishPendingCapture()
{
    if (!m_contourBusy)
        return;
    m_contourWatcher.waitForFinished();
    QCoreApplication::sendPostedEvents(&m_contourWatcher);
}

bool SWMM2DResultsQSGRenderer::captureReady() const
{
    return !m_contourBusy && (!m_contourFrame || m_contourFrame->key == m_requestedContourKey);
}

void SWMM2DResultsQSGRenderer::forceRebuild()
{
    clearLabelTextureCache();
    ++m_contourEpoch;
    m_requestedContourKey.epoch = m_contourEpoch;
    m_contourFrame.reset();
    m_directFrame.reset();
    m_contourPositions.reset();
    m_contourTriangles.reset();
    m_contourScalarBuffers.clear();
    m_contourOutputBuffers.clear();
    std::vector<int>().swap(m_visibleCellsScratch);
    std::vector<int>().swap(m_contourSelectionScratch);
    m_builtCoverage = QRectF();
    m_builtLodKey = ~quint64(0);
    m_dirty.noteLayerChanged();
    update();
}

QSGNode *SWMM2DResultsQSGRenderer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Scrub-diagnosis probe (OPENSWMM_2D_RENDER_DEBUG=1): logs every sync so a
    // "layer advanced but view never re-rendered" report can be localized to
    // (a) sync not running, (b) a null-return gate, (c) the bbox cull, or
    // (d) a rebuild that produced empty geometry.
    static const bool kUpnDebug = qEnvironmentVariableIsSet("OPENSWMM_2D_RENDER_DEBUG");
    if (kUpnDebug)
        qDebug("[2D-qsg] sync: layer=%p vis=%d t=%d lastT=%d pending=0x%x "
               "extentValid=%d itemWH=%gx%g",
               static_cast<void *>(m_layer), m_layer ? int(m_layer->isVisible()) : -1,
               m_layer ? m_layer->currentTimeIndex() : -999, m_lastRenderedTime, m_dirty.pending(),
               int(m_extent.isValid()), width(), height());

    auto clearPresentedFrames = [&]() {
        if (!m_layer)
            return;
        for (auto *base : m_layer->sublayers())
            if (auto *sub = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(base))
                sub->publishPresentedFrame({});
    };

    if (!m_layer || !m_layer->isVisible() || !m_extent.isValid() || width() <= 0 || height() <= 0) {
        if (kUpnDebug)
            qDebug("[2D-qsg]   -> null-return (gate 1)");
        clearPresentedFrames();
        delete oldNode;
        return nullptr;
    }

    // Issue 2 master gate: a LIVE (streaming) layer with live rendering switched
    // off stops drawing entirely (deletes the node tree so no stale geometry
    // lingers). Non-live static results are unaffected and keep drawing.
    if (m_layer->source() && m_layer->source()->isLive() && !m_layer->liveRenderEnabled()) {
        if (kUpnDebug)
            qDebug("[2D-qsg]   -> null-return (live gate: "
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
    QSGGeometryNode *cellFillNode = nullptr;   // flat per-cell depth fill
    QSGGeometryNode *smoothFillNode = nullptr; // per-vertex Gouraud depth fill
    QSGGeometryNode *bandNode = nullptr;
    QSGGeometryNode *isoNode = nullptr;
    QSGGeometryNode *isoIndexNode = nullptr;
    QSGOpacityNode *isoLabels = nullptr;
    QSGGeometryNode *edgeNode = nullptr;     // mesh wireframe overlay
    QSGGeometryNode *nodeMarkNode = nullptr; // mesh-vertex markers
    QSGGeometryNode *velNode = nullptr;
    QSGGeometryNode *hiFillNode = nullptr;
    QSGGeometryNode *hiEdgeNode = nullptr;

    // CF.3 highlight colours — match the CPU pass.
    const QColor kHiFillColor(0, 200, 255, 110); // translucent cyan
    const QColor kHiEdgeColor(255, 215, 0, 235); // gold

    if (!root) {
        // Fresh tree (first frame, or the layer was hidden and the old tree
        // deleted) — every domain is stale regardless of pending notes.
        m_dirty.noteLayerChanged();
        root = new ResultsRootNode();
        root->flowNode = new QSGNode();
        root->appendChildNode(root->flowNode);
        root->particleNode = new FlowParticleNode(window());
        root->flowNode->appendChildNode(root->particleNode);
        cellFillNode = root->cellFillNode = makeWaterFillNode();
        smoothFillNode = root->smoothFillNode = makeWaterFillNode();
        bandNode = root->bandNode = makeWaterFillNode();
        root->gpuFillNode = new QSGNode();
        root->gpuBandNode = new QSGNode();
        root->gpuIsoNode = new QSGNode();
        isoNode = root->isoNode = makeFlatNode(QColor(10, 10, 10, 220));
        isoIndexNode = root->isoIndexNode = makeFlatNode(QColor(10, 10, 10, 220));
        isoLabels = root->isoLabels = new QSGOpacityNode();
        edgeNode = root->edgeNode = makeFlatNode(QColor(0, 0, 0, 130));
        nodeMarkNode = root->nodeMarkNode = makeFlatNode(QColor(40, 40, 40, 220));
        velNode = root->velNode = makeColoredNode();
        hiFillNode = root->hiFillNode = makeFlatNode(kHiFillColor);
        hiEdgeNode = root->hiEdgeNode = makeFlatNode(kHiEdgeColor);
        // Fills first → they paint UNDER the bands/edges/isolines.
        root->appendChildNode(cellFillNode);
        root->appendChildNode(smoothFillNode);
        root->appendChildNode(root->gpuFillNode);
        root->appendChildNode(bandNode);
        root->appendChildNode(root->gpuBandNode);
        root->appendChildNode(isoNode);
        root->appendChildNode(isoIndexNode);
        root->appendChildNode(root->gpuIsoNode);
        root->appendChildNode(isoLabels);
        root->appendChildNode(edgeNode);
        root->appendChildNode(nodeMarkNode);
        root->appendChildNode(velNode);
        root->appendChildNode(hiFillNode);
        root->appendChildNode(hiEdgeNode);
    } else {
        cellFillNode = root->cellFillNode;
        smoothFillNode = root->smoothFillNode;
        bandNode = root->bandNode;
        isoNode = root->isoNode;
        isoIndexNode = root->isoIndexNode;
        isoLabels = root->isoLabels;
        edgeNode = root->edgeNode;
        nodeMarkNode = root->nodeMarkNode;
        velNode = root->velNode;
        hiFillNode = root->hiFillNode;
        hiEdgeNode = root->hiEdgeNode;
    }

    // ---- Shared render params ---------------------------------------------
    const float sx_r = float(width()) / float(m_extent.width());
    const float invView = (sx_r > 0.0f) ? 1.0f / sx_r : 1.0f;

    const double cullMargin = double(2.0f * invView);
    const double cullX0 = m_extent.xMin() - cullMargin;
    const double cullX1 = m_extent.xMax() + cullMargin;
    const double cullY0 = -m_extent.yMax() - cullMargin;
    const double cullY1 = -m_extent.yMin() + cullMargin;
    const QRectF viewRect(QPointF(cullX0, cullY0), QPointF(cullX1, cullY1));

    auto applyTransform = [&]() {
        const float msx = float(width()) / float(m_extent.width());
        const float msy = float(height()) / float(m_extent.height());
        QMatrix4x4 mat;
        mat.scale(msx, msy);
        mat.translate(float(m_anchorX - m_extent.xMin()), float(m_anchorY + m_extent.yMax()));
        if (root->matrix() != mat)
            root->setMatrix(mat);
    };

    const double requestedDpr = property("canvasDevicePixelRatio").toDouble();
    const double dpr = requestedDpr > 0 ? requestedDpr : 1.;
    const double logicalScale = double(sx_r) / dpr;
    m_layer->setHydraulicView(viewRect, logicalScale);
    auto syncStreaks = [&]() {
        if (m_separateFlow) {
            root->particleNode->sync({}, 1, 1);
            return;
        }
        auto *sub = m_layer->flowStreakSublayer();
        const auto *style = sub->streakStyle();
        const auto frame = m_layer->flowParticles(viewRect, logicalScale, m_lastRenderedFrame);
        FlowParticleAppearance appearance;
        appearance.anchor = {m_anchorX, m_anchorY};
        const double stroke = 2 * std::max(style->widthPx(), style->headWidthPx()) / logicalScale;
        appearance.viewport = viewRect.adjusted(-stroke, -stroke, stroke, stroke);
        appearance.color = style->color();
        appearance.opacity = sub->opacity() * m_layer->flowParticleOpacity();
        appearance.fade = true;
        appearance.taper = true;
        appearance.revision = qHash(QJsonDocument(style->toJson()).toJson(QJsonDocument::Compact));
        if (style->colorByMagnitude())
            appearance.colorForSpeed = [style](double v) { return style->colorForSpeed(v); };
        root->particleNode->sync(frame, m_layer->flowParticleInterpolation(frame), logicalScale,
                                 style->trailDurationSeconds(), style->widthPx(),
                                 style->headWidthPx(), style->lifetimeFadeSeconds(), 0, appearance);
    };
    auto clearAll = [&]() {
        root->particleNode->sync({}, 1, 1);
        clearPresentedFrames();
        const std::vector<QSGGeometry::ColoredPoint2D> empty_c;
        const std::vector<QSGGeometry::Point2D> empty_p;
        uploadColoredVerts(cellFillNode, empty_c);
        uploadColoredVerts(smoothFillNode, empty_c);
        uploadColoredVerts(bandNode, empty_c);
        deleteChildren(root->gpuFillNode);
        deleteChildren(root->gpuBandNode);
        deleteChildren(root->gpuIsoNode);
        for (auto *node : root->resultScalarNodes) {
            uploadColoredVerts(node->fill, empty_c);
            uploadColoredVerts(node->smooth, empty_c);
            uploadColoredVerts(node->bands, empty_c);
            uploadFlatVerts(node->lines, empty_p);
            node->clearLabels();
        }
        for (auto *node : root->resultVectorNodes) {
            uploadColoredVerts(node->streaks, empty_c);
            uploadColoredVerts(node->arrows, empty_c);
        }
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
        QSet<QString> currentResultIds, currentVectorIds;
        for (auto *sub : m_layer->sublayers()) {
            if (qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(sub)) {
                currentResultIds.insert(sub->id());
                if (!root->resultScalarNodes.contains(sub->id()))
                    root->resultScalarNodes.insert(sub->id(), new ScalarResultNodes);
            } else if (qobject_cast<OpenSWMM::Render::ResultVectorSublayer *>(sub)) {
                currentVectorIds.insert(sub->id());
                if (!root->resultVectorNodes.contains(sub->id()))
                    root->resultVectorNodes.insert(sub->id(), new VectorResultNodes);
            }
        }
        const auto prune = [](auto &nodes, const QSet<QString> &current) {
            for (auto it = nodes.begin(); it != nodes.end();) {
                if (current.contains(it.key())) {
                    ++it;
                    continue;
                }
                if (it.value()->parent())
                    it.value()->parent()->removeChildNode(it.value());
                delete it.value();
                it = nodes.erase(it);
            }
        };
        prune(root->resultScalarNodes, currentResultIds);
        prune(root->resultVectorNodes, currentVectorIds);
        std::vector<QSGNode *> desired;
        desired.reserve(size_t(m_layer->sublayers().size()) + 12);
        auto appendNode = [&](QSGNode *node) {
            if (node)
                desired.push_back(node);
        };

        bool addedCell = false;
        bool addedSmooth = false;
        bool addedBand = false;
        bool addedIso = false;
        bool addedEdge = false;
        bool addedNodeMarkers = false;
        bool addedVelocity = false;
        bool addedStreaks = false;

        auto addCell = [&]() {
            if (addedCell)
                return;
            appendNode(cellFillNode);
            addedCell = true;
        };
        auto addSmooth = [&]() {
            if (addedSmooth)
                return;
            appendNode(smoothFillNode);
            addedSmooth = true;
        };
        auto addBand = [&]() {
            if (addedBand)
                return;
            appendNode(bandNode);
            addedBand = true;
        };
        auto addIso = [&]() {
            if (addedIso)
                return;
            appendNode(isoNode);
            appendNode(isoIndexNode);
            appendNode(isoLabels);
            addedIso = true;
        };
        auto addEdge = [&]() {
            if (addedEdge)
                return;
            appendNode(edgeNode);
            addedEdge = true;
        };
        auto addNodeMarkers = [&]() {
            if (addedNodeMarkers)
                return;
            appendNode(nodeMarkNode);
            addedNodeMarkers = true;
        };
        auto addVelocity = [&]() {
            if (addedVelocity)
                return;
            appendNode(velNode);
            addedVelocity = true;
        };

        // The depth and velocity rows place the depth passes and the animated
        // streaks at their own position; without those rows the passes keep
        // theirs.
        const auto *depthRow = m_layer->depthResultSublayer();
        const auto *velocityRow = m_layer->velocityResultSublayer();
        const auto depthPassId = [](const QString &id) {
            return id == QLatin1String("results2d.cellDepthFill") ||
                   id == QLatin1String("results2d.smoothDepthFill") ||
                   id == QLatin1String("results2d.bands") ||
                   id == QLatin1String("results2d.isolines");
        };
        for (OpenSWMM::Render::ISublayer *sub : m_layer->sublayers()) {
            if (!sub)
                continue;
            const QString id = sub->id();
            if (qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(sub)) {
                if (sub == depthRow) {
                    addCell();
                    addSmooth();
                    addBand();
                    addIso();
                }
                appendNode(root->resultScalarNodes.value(id));
            } else if (qobject_cast<OpenSWMM::Render::ResultVectorSublayer *>(sub)) {
                if (sub == velocityRow && !addedStreaks) {
                    appendNode(root->flowNode);
                    addedStreaks = true;
                }
                appendNode(root->resultVectorNodes.value(id));
            } else if ((depthRow && depthPassId(id)) ||
                       (velocityRow && id == QLatin1String("results2d.flowStreaks"))) {
                continue;
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
            } else if (id == QLatin1String("results2d.flowStreaks")) {
                appendNode(root->flowNode);
                addedStreaks = true;
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
        if (!addedStreaks)
            appendNode(root->flowNode);

        appendNode(hiFillNode);
        appendNode(hiEdgeNode);
        // Reattaching unchanged passes invalidates Qt's retained batches and
        // cached water texture. Move only nodes whose actual order differs.
        QSGNode *previous = nullptr;
        for (auto *node : desired) {
            auto *expected = previous ? previous->nextSibling() : root->firstChild();
            if (expected != node) {
                if (node->parent())
                    node->parent()->removeChildNode(node);
                if (previous)
                    root->insertChildNodeAfter(node, previous);
                else
                    root->prependChildNode(node);
                ++m_orderMutations;
            }
            previous = node;
        }
    };
    syncSublayerOrder();

    // Early-out if bbox entirely outside view. Pending dirty notes are NOT
    // consumed; invalidating the coverage/LOD key guarantees a pan-back
    // resolves to an Lod rebuild.
    {
        const QRectF &bb = m_layer->m_sceneBBox;
        if (bb.isNull() || m_layer->m_sceneTris.isEmpty() || bb.right() < cullX0 ||
            bb.left() > cullX1 || bb.bottom() < cullY0 || bb.top() > cullY1) {
            if (kUpnDebug)
                qDebug("[2D-qsg]   -> bbox CULL: bb=(%g,%g %gx%g) null=%d "
                       "tris=%d cull=(%g..%g, %g..%g)",
                       bb.left(), bb.top(), bb.width(), bb.height(), int(bb.isNull()),
                       int(m_layer->m_sceneTris.size()), cullX0, cullX1, cullY0, cullY1);
            clearAll();
            m_builtCoverage = QRectF();
            m_builtLodKey = ~quint64(0);
            applyTransform();
            syncStreaks();
            return root;
        }
    }

    // ---- QSG-2D-1M: LOD decision + dirty-domain resolution ------------------
    const auto &tris = m_layer->m_sceneTris;
    const int nTri = int(tris.size());
    // A fast surface's corner depths in 12 bytes per triangle (the same values as tris): the
    // per-frame wet sweep and chunk scan read these, not the whole triangle records.
    const auto *fastCorners = m_layer->fastSceneCorners();
    const QRectF &bb = m_layer->m_sceneBBox;
    // Display triangle → owning cell (a quad owns two consecutive fan
    // triangles); identity when the layer has no map yet.
    const auto &triCellMap = m_layer->triCellMap();
    const bool haveTriCellMap = int(triCellMap.size()) == nTri;
    auto cellOfTri = [&](int i) { return haveTriCellMap ? triCellMap[size_t(i)] : i; };

    auto *bandSub = m_layer->contourBandSublayer();
    auto *isoSub = m_layer->isolineSublayer();
    auto *velSub = m_layer->velocityVectorSublayer();
    auto *edgeSub = m_layer->meshEdgeSublayer();
    auto *nodeSub = m_layer->meshNodeSublayer();
    auto *cellSub = m_layer->cellDepthFillSublayer();
    auto *smoothSub = m_layer->smoothDepthFillSublayer();

    Qsg2DLodInputs li;
    li.viewportWidthPx = width();
    li.viewportHeightPx = height();
    li.extentWidth = m_extent.width();
    li.extentHeight = m_extent.height();
    li.cellCount = nTri;
    li.meshBBoxArea = bb.width() * bb.height();
    {
        const QRectF vis = viewRect.intersected(bb);
        li.visibleFraction =
            (li.meshBBoxArea > 0.0) ? (vis.width() * vis.height()) / li.meshBBoxArea : 1.0;
    }
    li.wantFill = (bandSub && bandSub->isVisible()) || (cellSub && cellSub->isVisible()) ||
                  (smoothSub && smoothSub->isVisible());
    li.wantEdges = edgeSub && edgeSub->isVisible();
    li.wantVertexMarkers = nodeSub && nodeSub->isVisible();
    li.wantContours = isoSub && isoSub->isVisible();
    li.wantContourLabels = li.wantContours;
    li.wantVelocity = velSub && velSub->isVisible();
    li.haveSelection = !m_layer->highlightedCells().isEmpty();
    li.previousBucket = m_lastBucket;
    li.previousZoomStep = m_lastZoomStep;

    const Qsg2DLodDecision lod = Qsg2DLodPolicy::decide(li);
    m_lastBucket = lod.bucket;
    m_lastZoomStep = lod.zoomStep;

    // Snapshot diffs — ground truth for classifying ambiguous invalidations.
    const bool geomChanged = m_layer->geomRevision() != m_lastGeomRev;
    const bool selChanged = m_layer->highlightedCells() != m_lastHighlight;
    const bool timeChanged = m_layer->currentTimeIndex() != m_lastRenderedTime ||
                             m_layer->frameRevision() != m_lastRenderedFrame;

    const bool lodKeyChanged = lod.contentKey() != m_builtLodKey;
    const bool insideCoverage = m_builtCoverage.isValid() && m_builtCoverage.contains(viewRect);

    const bool wasZoom = m_dirty.zoomChangePending();
    const bool wasPan = m_dirty.extentChangePending() && !wasZoom;

    using D = Qsg2DDirtyState;
    const quint32 bits =
        m_dirty.resolve(geomChanged, selChanged, timeChanged, lodKeyChanged, insideCoverage);

    // Per-pass rebuild verdicts. Mesh edges and vertex markers are static
    // across time — a scrub leaves them untouched.
    const bool rebuildFills = bits & (D::Geometry | D::Style | D::Data | D::Lod);
    const bool rebuildEdges = bits & (D::Geometry | D::Style | D::Lod);
    const bool rebuildMarkers = rebuildEdges;
    const bool rebuildVel = rebuildFills;
    const bool rebuildSel = bits & (D::Geometry | D::Selection | D::Lod);
    const bool anyContent =
        rebuildFills || rebuildEdges || rebuildMarkers || rebuildVel || rebuildSel;

    // Perf stats (Phase 1) — all accounting is skipped unless enabled.
    const bool statsOn = Qsg2DRenderStats::loggingEnabled();
    Qsg2DRenderStats stats;
    if (statsOn) {
        stats.rendererName = QStringLiteral("results2d");
        quint32 r = 0;
        if (wasPan)
            r |= Qsg2DRenderStats::DirtyPan;
        if (wasZoom)
            r |= Qsg2DRenderStats::DirtyZoom;
        if (bits & D::Data)
            r |= Qsg2DRenderStats::DirtyTime;
        if (bits & D::Style)
            r |= Qsg2DRenderStats::DirtyStyle;
        if (bits & D::Selection)
            r |= Qsg2DRenderStats::DirtySelection;
        if (bits & D::Geometry)
            r |= Qsg2DRenderStats::DirtyGeometry;
        if (bits & D::Lod)
            r |= Qsg2DRenderStats::DirtyLod;
        stats.dirtyReasons = r;
    }

    if (kUpnDebug && anyContent)
        qDebug("[2D-qsg]   rebuild bits=0x%x bucket=%d key=%llu "
               "(fills=%d edges=%d vel=%d sel=%d)",
               bits, int(lod.bucket), (unsigned long long)lod.contentKey(), int(rebuildFills),
               int(rebuildEdges), int(rebuildVel), int(rebuildSel));

    // ---- Content rebuild (per-pass, dirty-domain guarded) --------------------
    if (anyContent) {

        // Anchor — bbox centre; derived purely from geometry, so recompute
        // only on the Geometry domain (all passes rebuild then, keeping
        // every anchor-relative buffer consistent).
        if (bits & D::Geometry) {
            m_anchorX = (bb.left() + bb.right()) * 0.5;
            m_anchorY = (bb.top() + bb.bottom()) * 0.5;
            ++m_contourEpoch;
            m_requestedContourKey.epoch = m_contourEpoch;
            m_contourFrame.reset();
            m_directFrame.reset();
            m_contourPositions.reset();
            m_contourTriangles.reset();
            m_contourScalarBuffers.clear();
            m_contourOutputBuffers.clear();
            std::vector<int>().swap(m_visibleCellsScratch);
            std::vector<int>().swap(m_contourSelectionScratch);
        }
        const double ox = m_anchorX, oy = m_anchorY;

        // Coverage rect: content is culled to the viewport plus half a
        // viewport of margin on every side, so pans inside it stay
        // matrix-only. Refreshed only when the LOD/geometry epoch changes —
        // Data/Style/Selection rebuilds keep culling against the rect their
        // sibling passes were built for, so all passes stay consistent.
        QRectF cullRect = m_builtCoverage;
        if ((bits & (D::Geometry | D::Lod)) || !cullRect.isValid()) {
            const double cmx = m_extent.width() * 0.5;
            const double cmy = m_extent.height() * 0.5;
            cullRect = viewRect.adjusted(-cmx, -cmy, cmx, cmy);
        }
        const QRectF cullLocal = cullRect.translated(-ox, -oy);

        // The snapshot already excludes dry cells and hidden films. Keep
        // the exact zero-depth boundary of every remaining partial cell.
        const double dryDepth = 0.0;
        double maxDepth = std::max(m_layer->maxDepth(), dryDepth + 1e-9);

        const OpenSWMM::Render::ContourBandStyle *bs = bandSub ? bandSub->bandStyle() : nullptr;
        const OpenSWMM::Render::IsolineStyle *is = isoSub ? isoSub->isolineStyle() : nullptr;
        const OpenSWMM::Render::VelocityVectorStyle *vs = velSub ? velSub->vectorStyle() : nullptr;

        const bool bandsVisible = bandSub && bandSub->isVisible();
        const bool isoVisible = isoSub && isoSub->isVisible() && lod.drawContours;
        const bool smoothBands =
            bandsVisible && (!bs || bs->smoothBands()) && lod.exactContourBands;
        std::vector<double> bandLevels, isoLevels;
        if (bandsVisible && maxDepth > dryDepth) {
            if (bs) {
                const auto edges = bs->scheme().levelEdges(
                    dryDepth, maxDepth, m_layer->depthClassificationSamples(bs->scheme()));
                bandLevels.assign(edges.cbegin(), edges.cend());
            } else {
                bandLevels = OpenSWMM::Contour::evenlySpacedLevelsInclusive(dryDepth, maxDepth, 9);
            }
        }
        if (isoVisible && maxDepth > dryDepth) {
            isoLevels = is ? is->levelsForRange(dryDepth, maxDepth,
                                                m_layer->depthClassificationSamples(is->scheme()))
                           : OpenSWMM::Contour::evenlySpacedLevels(dryDepth, maxDepth, 8);
            isoLevels.erase(
                std::remove_if(isoLevels.begin(), isoLevels.end(),
                               [dryDepth](double v) { return !std::isfinite(v) || v < dryDepth; }),
                isoLevels.end());
        }
        // ---- Chunk index (Phase 4) — rebuilt once per geometry revision ----
        // A Selection-only rebuild touches just the highlight nodes; skip
        // the chunk query / visible-cell materialisation entirely then.
        const bool needChunks = rebuildFills || rebuildVel || rebuildEdges || rebuildMarkers;
        if (needChunks && (m_chunksRev != m_layer->geomRevision() || m_chunks.isEmpty())) {
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
                edgeBB[i] = QRectF(QPointF(std::min(e.a.x(), e.b.x()), std::min(e.a.y(), e.b.y())),
                                   QPointF(std::max(e.a.x(), e.b.x()), std::max(e.a.y(), e.b.y())));
            }
            m_chunks.build(triBB, edgeBB);
            m_chunksRev = m_layer->geomRevision();
        }

        const MeshRenderChunkIndex::QueryResult chunkQ =
            needChunks ? m_chunks.query(cullRect) : MeshRenderChunkIndex::QueryResult{};

        // Visible cell ids (chunk-batched: fully-inside chunks skip the
        // per-tri bbox test). Reuse capacity across hydraulic publications.
        auto &visibleCells = m_visibleCellsScratch;
        visibleCells.clear();
        if (rebuildFills || rebuildVel || rebuildMarkers) {
            for (int cid : chunkQ.fullyInside) {
                const auto &ids = m_chunks.chunks()[cid].cellIds;
                visibleCells.insert(visibleCells.end(), ids.cbegin(), ids.cend());
            }
            for (int cid : chunkQ.boundary) {
                for (int t : m_chunks.chunks()[cid].cellIds) {
                    const auto &tt = tris[t];
                    const double minX = std::min({tt.a.x(), tt.b.x(), tt.c.x()});
                    const double maxX = std::max({tt.a.x(), tt.b.x(), tt.c.x()});
                    const double minY = std::min({tt.a.y(), tt.b.y(), tt.c.y()});
                    const double maxY = std::max({tt.a.y(), tt.b.y(), tt.c.y()});
                    if (maxX < cullRect.left() || minX > cullRect.right() ||
                        maxY < cullRect.top() || minY > cullRect.bottom())
                        continue;
                    visibleCells.push_back(t);
                }
            }
            if (statsOn)
                stats.visibleCells = qint64(visibleCells.size());
        }

        // Fast vertex surface: classify the filled contours per fragment on the GPU
        // (ScalarBandMaterial) instead of marching them. Same boundaries on linearly
        // interpolated triangles, without the positions/scalars snapshot or a job.
        const bool gpuBands = smoothBands && bandLevels.size() >= 2 &&
                              bandLevels.size() <= size_t(ScalarBandMaterial::kMaxBands) + 1 &&
                              m_layer->fastSurfacePublished() && gpuBandsEnabled() &&
                              ensureSharedFillChunks(ox, oy);
        // Isolines likewise, unless labels are shown: those are placed along marched lines.
        const bool gpuIsolines = isoVisible && !isoLevels.empty() &&
                                 isoLevels.size() <= size_t(ScalarBandMaterial::kMaxBands) &&
                                 !(is && is->labels() && lod.drawContourLabels) &&
                                 m_layer->fastSurfacePublished() && gpuBandsEnabled() &&
                                 ensureSharedFillChunks(ox, oy);
        // Per-frame passes over millions of triangles use the global pool for saved results; a
        // live run keeps its cores for the engine.
        const bool parallelPasses = !(m_layer->source() && m_layer->source()->isLive());
        ContourJobKey key;
        key.time = m_layer->currentTimeIndex();
        key.frameRev = m_layer->frameRevision();
        key.epoch = m_contourEpoch;
        key.dryDepth = dryDepth;
        key.maxDepth = maxDepth;
        key.velocity = velSub && velSub->isVisible();
        key.bandLevels = bandLevels;
        key.smoothBands = smoothBands;
        key.snapshotTriangles = (cellSub && cellSub->isVisible()) ||
                                (smoothSub && smoothSub->isVisible()) ||
                                (bandsVisible && !smoothBands) || gpuBands || gpuIsolines ||
                                key.velocity;
        key.isoLevels = isoLevels;
        if (bs)
            key.contourStyle.insert(QStringLiteral("bands"), bs->toJson());
        if (is)
            key.contourStyle.insert(QStringLiteral("isolines"), is->toJson());
        key.contourStyle.insert(QStringLiteral("bandsVisible"), bandsVisible);
        key.contourStyle.insert(QStringLiteral("isolinesVisible"), isoVisible);
        key.contourStyle.insert(QStringLiteral("gpuBands"), gpuBands);
        key.contourStyle.insert(QStringLiteral("gpuIsolines"), gpuIsolines);
        key.coverage = cullRect;
        m_requestedContourKey = key;
        const bool compatible = m_contourFrame && m_contourFrame->key.samePresentation(key) &&
                                (!m_contourFrame->immediate || m_contourFrame->key == key);
        const bool needsMarching =
            (smoothBands && !gpuBands && key.bandLevels.size() >= 2) ||
            (!gpuIsolines && !key.isoLevels.empty());
        // Large bootstrap and range/style changes must not march inline.
        // Keep the historical small-mesh bootstrap for immediate previews.
        const bool background = asyncContoursEnabled(nTri) && needsMarching &&
                                (compatible || nTri >= kAsyncContourThreshold);
        // Finish the water/particle handoff before replacing its candidate.
        // Otherwise fast ticks keep changing the revision the particle worker
        // is preparing and can starve presentation indefinitely.
        const bool awaitingPresentation = compatible &&
            m_contourFrame->key.frameRev != m_lastRenderedFrame &&
            m_layer->flowStreaksEnabled() && m_contourFrame->particles;
        if (!awaitingPresentation && (!m_contourFrame || !(m_contourFrame->key == key)) &&
            (!background || !m_contourBusy)) {
            std::shared_ptr<std::vector<std::array<float, 3>>> scalars;
            std::shared_ptr<ContourJobOutput> output;
            if (needsMarching) {
                for (const auto &buffer : m_contourScalarBuffers)
                    if (buffer.use_count() == 1) {
                        scalars = buffer;
                        break;
                    }
                if (!scalars && m_contourScalarBuffers.size() < 3) {
                    scalars = std::make_shared<std::vector<std::array<float, 3>>>();
                    m_contourScalarBuffers.push_back(scalars);
                }
                for (const auto &buffer : m_contourOutputBuffers)
                    if (buffer.packet.use_count() == 1) {
                        output = buffer.packet;
                        break;
                    }
                if (!output && m_contourOutputBuffers.size() < 3) {
                    output = std::make_shared<ContourJobOutput>();
                    m_contourOutputBuffers.push_back({output, 0});
                }
                if (!scalars || !output) {
                    // The displayed frame, watcher result and worker may all
                    // still have readers. Coalesce rather than overwrite one.
                    if (bits & D::Geometry)
                        clearAll();
                    m_lastGeomRev = m_layer->geomRevision();
                    m_builtCoverage = cullRect;
                    m_dirty.noteStyleChanged();
                    update();
                    applyTransform();
                    syncStreaks();
                    return root;
                }
            }
            // Immediate fills read the published layer only during this sync;
            // no worker borrows that data. Retain selection/metadata only.
            // Marching jobs still capture independent immutable attributes.
            if (needsMarching)
                m_directFrame.reset();
            auto frame = needsMarching ? std::make_shared<ContourFrame>() : m_directFrame;
            if (!frame)
                frame = m_directFrame = std::make_shared<ContourFrame>();
            frame->immediate = !needsMarching;
            auto &waterCells =
                needsMarching ? m_contourSelectionScratch : frame->immediateTriangles;
            waterCells.clear();
            const auto wet = [&tris, fastCorners](int i) {
                if (fastCorners) {
                    const auto &d = (*fastCorners)[size_t(i)];
                    return std::isfinite(d[0]) && std::isfinite(d[1]) && std::isfinite(d[2]);
                }
                const auto &t = tris[i];
                return std::isfinite(t.dv0) && std::isfinite(t.dv1) && std::isfinite(t.dv2);
            };
            if (visibleCells.size() * 8 >= size_t(nTri)) {
                // A view holding much of the mesh: mark and sweep in index
                // order. Sorting millions of chunk-ordered IDs every frame
                // dominated six-million-cell syncs; the sweep also reads the
                // triangles sequentially.
                // Chunk IDs are unique, so a full-size list is every triangle.
                const bool all = visibleCells.size() == size_t(nTri);
                std::vector<unsigned char> shown(all ? 0 : size_t(nTri), 0);
                if (!all)
                    for (int i : visibleCells)
                        shown[size_t(i)] = 1;
                // Fixed blocks, swept in parallel for saved results and joined in order.
                auto &blocks = m_sweepBlocks;
                blocks.resize(blockCount(size_t(nTri)));
                forEachIndex(blocks.size(), parallelPasses, [&](size_t b) {
                    auto &out = blocks[b];
                    out.clear();
                    const int last = int(std::min(size_t(nTri), (b + 1) * kPassBlock));
                    for (int i = int(b * kPassBlock); i < last; ++i)
                        if ((all || shown[size_t(i)]) && wet(i))
                            out.push_back(i);
                });
                size_t total = 0;
                for (const auto &out : blocks)
                    total += out.size();
                waterCells.reserve(total);
                for (const auto &out : blocks)
                    waterCells.insert(waterCells.end(), out.cbegin(), out.cend());
            } else {
                for (int i : visibleCells)
                    if (wet(i))
                        waterCells.push_back(i);
                // Only water snapshots need ordered IDs.
                std::sort(waterCells.begin(), waterCells.end());
            }
            std::shared_ptr<std::vector<ContourJobInput::TriPos>> positions;
            if (needsMarching &&
                (!m_contourPositions || !m_contourTriangles || *m_contourTriangles != waterCells)) {
                // The rendered packet owns its coordinates. Only an active
                // worker can still read this source array across a pan/zoom.
                // Keep capacity when it has no readers, rather than repeatedly
                // freeing/reallocating full-domain positions on zoom changes.
                positions = m_contourPositions && m_contourPositions.use_count() == 1
                                ? m_contourPositions
                                : std::make_shared<std::vector<ContourJobInput::TriPos>>();
                positions->resize(waterCells.size());
            }
            if (needsMarching) {
                scalars->resize(waterCells.size());
                if (key.snapshotTriangles)
                    frame->cellValues.resize(waterCells.size());
                // One pass over the triangles for every per-frame copy (positions
                // only when the wet set changed, as it does while water spreads).
                for (size_t i = 0; i < waterCells.size(); ++i) {
                    const auto &t = tris[waterCells[i]];
                    if (positions)
                        (*positions)[i] = {float(t.a.x() - ox), float(t.a.y() - oy),
                                           float(t.b.x() - ox), float(t.b.y() - oy),
                                           float(t.c.x() - ox), float(t.c.y() - oy)};
                    (*scalars)[i] = {t.dv0, t.dv1, t.dv2};
                    if (key.snapshotTriangles)
                        frame->cellValues[i] = {t.depth, t.vx, t.vy, t.vmag};
                }
            }
            if (positions) {
                m_contourPositions = std::move(positions);
                m_contourTriangles = std::make_shared<const std::vector<int>>(waterCells);
            }
            frame->selection = needsMarching ? m_contourTriangles : nullptr;
            if (!needsMarching) {
                m_contourPositions.reset();
                m_contourTriangles.reset();
            }
            frame->key = key;
            frame->sharedCorners = m_layer->fastSurfacePublished();
            frame->depthSamples.clear();
            frame->scalars = key.snapshotTriangles ? scalars : nullptr;
            const OpenSWMM::Render::ScalarFillStyle *fillStyles[] = {
                cellSub && cellSub->isVisible() ? cellSub->fillStyle() : nullptr,
                smoothSub && smoothSub->isVisible() ? smoothSub->fillStyle() : nullptr};
            for (const auto *style : fillStyles)
                if (style && style->classified() &&
                    style->scheme().rangeMode() ==
                        OpenSWMM::Render::RangeMode::PerFrameAutoStretch) {
                    frame->depthSamples = m_layer->currentDepthSamples(true);
                    break;
                }
            if (key.velocity)
                frame->velocity = m_layer->captureVelocityField();
            else
                frame->velocity = {};
            frame->maxVelocity = m_layer->maxVelocity();
            frame->hasVelocity = m_layer->hasVelocityData();
            ContourJobInput input;
            input.positions = m_contourPositions;
            input.scalars = std::move(scalars);
            input.bandLevels =
                smoothBands && !gpuBands ? key.bandLevels : std::vector<double>{};
            input.isoLevels = gpuIsolines ? std::vector<double>{} : key.isoLevels;
            input.minimumVisibleValue = dryDepth;
            input.packBands = true;
            input.bandCoverage = cullLocal;
            // Capture the same live field before it is superseded. Waiting
            // until this contour is presented can otherwise leave no matching
            // velocity data when the producer is ahead of the display.
            m_layer->flowParticles(viewRect, logicalScale, key.frameRev, false);
            frame->particles = m_layer->flowStreaksEnabled()
                                   ? m_layer->captureFlowParticleInput(viewRect, logicalScale)
                                   : nullptr;
            frame->contourPacket = output;
            auto compute = [frame, output,
                            input =
                                std::move(input)]() mutable -> std::shared_ptr<const ContourFrame> {
                computeContourJob(input, *output);
                // Release writer/input leases before Qt reports completion;
                // runnable destruction may lag behind the queued GUI callback.
                input = {};
                output.reset();
                return std::move(frame);
            };
            if (!needsMarching) {
                m_contourFrame = frame;
            } else if (background) {
                m_contourBusy = true;
                m_contourWatcher.setFuture(QtConcurrent::run(std::move(compute)));
            } else {
                // Small-mesh bootstrap and non-marching passes stay immediate.
                // Never substitute cell colours or blank contour lines.
                m_contourFrame = compute();
                finishContourOutput(*m_contourFrame->contourPacket);
            }
        }
        // No complete compatible frame yet: return to the event loop. Keep
        // an existing same-mesh image until its replacement is ready, and
        // clear it on geometry/source changes. The completion schedules sync.
        if (!m_contourFrame || !m_contourFrame->key.samePresentation(key) ||
            (m_contourFrame->immediate && !(m_contourFrame->key == key))) {
            if (bits & D::Geometry)
                clearAll();
            // Static inputs were captured: do not reset their epoch on each
            // tick while the first worker is still running.
            m_lastGeomRev = m_layer->geomRevision();
            m_builtCoverage = cullRect;
            m_dirty.noteStyleChanged(); // deferred passes must rebuild on completion
            applyTransform();
            syncStreaks();
            return root;
        }
        const auto &frame = *m_contourFrame;
        // Publish a depth change and its wet-supported trails together. The
        // old coherent pair stays visible while the serial worker prepares;
        // no GUI wait or blank particle node is needed at the handoff.
        m_waitingForParticles = frame.key.frameRev != m_lastRenderedFrame &&
                                m_lastRenderedFrame != ~quint64(0) && !(bits & D::Geometry) &&
                                m_layer->flowStreaksEnabled() && frame.particles &&
                                !m_layer->prepareFlowParticlesForPresentation(frame.particles);
        if (m_waitingForParticles) {
            m_dirty.noteDataChanged();
            applyTransform();
            syncStreaks();
            return root;
        }
        const auto &frameTriangles = frame.triangles();
        // A fast producer often synchronizes repeatedly without a newly
        // completed contour frame. Reuse its GPU buffers rather than clipping
        // and uploading exactly the same geometry on every tick.
        if (background && compatible && frame.key.frameRev == m_lastRenderedFrame &&
            !(bits & (D::Geometry | D::Style | D::Lod | D::Selection))) {
            applyTransform();
            syncStreaks();
            return root;
        }
        // Classify the displayed snapshot with its own automatic breaks.
        // Mixing current levels with older polygons assigns wrong colors.
        bandLevels = frame.key.bandLevels;
        isoLevels = frame.key.isoLevels;
        maxDepth = frame.key.maxDepth;
        size_t nextFrameTriangle = 0;
        auto frameTri = [&](int i) {
            auto t = tris[i];
            if (frame.immediate)
                return t;
            Q_ASSERT(frame.scalars);
            auto at = frameTriangles.begin() + std::ptrdiff_t(nextFrameTriangle);
            if (at == frameTriangles.end() || *at != i)
                at = std::lower_bound(frameTriangles.begin(), frameTriangles.end(), i);
            if (at == frameTriangles.end() || *at != i) {
                t.depth = t.dv0 = t.dv1 = t.dv2 = std::numeric_limits<float>::quiet_NaN();
                t.vx = t.vy = t.vmag = 0;
                return t;
            }
            const size_t local = size_t(at - frameTriangles.begin());
            nextFrameTriangle = local + 1;
            const auto &d = (*frame.scalars)[local];
            const auto &c = frame.cellValues[local];
            t.dv0 = d[0];
            t.dv1 = d[1];
            t.dv2 = d[2];
            t.depth = c[0];
            t.vx = c[1];
            t.vy = c[2];
            t.vmag = c[3];
            return t;
        };

        // Issue 3B diagnostic — off by default. Set OPENSWMM_2D_RENDER_DEBUG=1
        // to log, per content rebuild, the guard states that decide whether
        // velocity vectors / contour bands / isolines / mesh edges / vertices
        // draw in the live GPU view.
        if (kUpnDebug) {
            const bool live = m_layer->source() && m_layer->source()->isLive();
            auto vis = [](OpenSWMM::Render::ISublayer *s) {
                return s ? (s->isVisible() ? 1 : 0) : -1;
            };
            qDebug("[2D-render] live=%d nTri=%d hasVel=%d t=%d | "
                   "band(vis=%d) iso(vis=%d) vel(vis=%d) edge(vis=%d) vert(vis=%d)",
                   live ? 1 : 0, nTri, m_layer->hasVelocityData() ? 1 : 0,
                   m_layer->currentTimeIndex(), vis(bandSub), vis(isoSub), vis(velSub),
                   vis(edgeSub), vis(nodeSub));
        }

        // Filled contours of a fast-surface frame on the GPU: one ScalarBandMaterial node per
        // shared-vertex chunk holding wet triangles; each frame writes those chunks' positions,
        // the wet corners' depths and the wet triangles' indices straight into the nodes.
        const bool frameGpuBands = frame.sharedCorners &&
                                   frame.key.contourStyle.value(QStringLiteral("gpuBands")).toBool();
        const bool frameGpuIsolines =
            frame.sharedCorners &&
            frame.key.contourStyle.value(QStringLiteral("gpuIsolines")).toBool();
        auto premultipliedOf = [](const QColor &c) -> std::array<float, 4> {
            const float a = float(c.alphaF());
            return {float(c.redF()) * a, float(c.greenF()) * a, float(c.blueF()) * a, a};
        };
        // Shared-vertex chunks of a fast-surface frame, scanned once per sync for every pass
        // below: each chunk's frame triangles with finite corners above the dry cutoff
        // (scanIndices, ascending triangle id) and its vertex depths (scanValues; corners are
        // shared vertex values, so every triangle writes the same depth). Chunks are scanned
        // in parallel for saved results; a live run keeps its cores for the engine.
        auto &sf = m_sharedFill;
        bool scanned = false;
        auto scanSharedChunks = [&] {
            if (scanned)
                return;
            scanned = true;
            // Frame triangles are unique, so blocks write disjoint slots.
            const size_t nSlots = size_t(nTri), nFrame = frameTriangles.size();
            sf.frameSlot.resize(nSlots);
            forEachIndex(blockCount(nSlots), parallelPasses, [&](size_t b) {
                const auto first = sf.frameSlot.begin() + std::ptrdiff_t(b * kPassBlock);
                std::fill(first, first + std::ptrdiff_t(std::min(kPassBlock, nSlots - b * kPassBlock)),
                          -1);
            });
            forEachIndex(blockCount(nFrame), parallelPasses, [&](size_t b) {
                const size_t last = std::min(nFrame, (b + 1) * kPassBlock);
                for (size_t k = b * kPassBlock; k < last; ++k)
                    sf.frameSlot[size_t(frameTriangles[k])] = int(k);
            });
            forEachIndex(sf.chunkTris.size(), parallelPasses, [&](size_t c) {
                auto &values = sf.scanValues[c];
                auto &indices = sf.scanIndices[c];
                values.assign(sf.vertices[c].size(), 0.f);
                indices.clear();
                for (int i : sf.chunkTris[c]) {
                    const int k = sf.frameSlot[size_t(i)];
                    if (k < 0)
                        continue;
                    // The layer's triangles for an immediate frame, else the frame's
                    // snapshot (stored in frame order).
                    std::array<float, 3> d;
                    if (frame.immediate && fastCorners) {
                        d = (*fastCorners)[size_t(i)];
                    } else if (frame.immediate) {
                        const auto &t = tris[i];
                        d = {t.dv0, t.dv1, t.dv2};
                    } else {
                        d = (*frame.scalars)[size_t(k)];
                    }
                    if (!std::isfinite(d[0]) || !std::isfinite(d[1]) || !std::isfinite(d[2]) ||
                        std::max({d[0], d[1], d[2]}) <= dryDepth)
                        continue;
                    for (size_t j = 0; j < 3; ++j) {
                        const quint16 local = sf.triLocal[size_t(i)][j];
                        values[local] = d[j];
                        indices.push_back(local);
                    }
                }
            });
        };
        // out = the scanned triangles of chunk c whose top corner depth passes keep(top).
        auto keepScanned = [&](size_t c, const auto &keep, std::vector<quint16> &out) {
            const auto &values = sf.scanValues[c];
            const auto &indices = sf.scanIndices[c];
            out.clear();
            for (size_t k = 0; k + 2 < indices.size(); k += 3)
                if (keep(std::max({values[indices[k]], values[indices[k + 1]],
                                   values[indices[k + 2]]})))
                    out.insert(out.end(), indices.begin() + std::ptrdiff_t(k),
                               indices.begin() + std::ptrdiff_t(k + 3));
        };
        // Draw the frame's wet triangles into container with one ScalarBandMaterial node per
        // touched chunk; configure(material) sets bands or isolines. Triangles whose corners are
        // all below lower or at/below the dry cutoff draw no fragment and are skipped.
        auto drawGpuScalar = [&](QSGNode *container, const auto &configure, double lower,
                                 const QString &pass) {
            scanSharedChunks();
            // The scan already dropped triangles at/below the dry cutoff.
            const bool filter = lower > dryDepth;
            if (filter)
                forEachIndex(sf.scanIndices.size(), parallelPasses, [&](size_t c) {
                    keepScanned(c, [lower](double top) { return top >= lower; },
                                sf.frameIndices[c]);
                });
            auto drawnIndices = [&](size_t c) -> const std::vector<quint16> & {
                return filter ? sf.frameIndices[c] : sf.scanIndices[c];
            };
            std::vector<QSGGeometryNode *> nodeOf(sf.vertices.size(), nullptr);
            QSGNode *reuse = container->firstChild();
            qint64 vertices = 0, bytes = 0;
            for (size_t chunk = 0; chunk < sf.vertices.size(); ++chunk) {
                const auto &indices = drawnIndices(chunk);
                if (indices.empty())
                    continue;
                QSGGeometryNode *node;
                if (reuse) {
                    node = static_cast<QSGGeometryNode *>(reuse);
                    reuse = reuse->nextSibling();
                } else {
                    node = makeScalarBandNode();
                    container->appendChildNode(node);
                }
                const size_t count = sf.vertices[chunk].size();
                node->geometry()->allocate(int(count), int(indices.size()));
                configure(static_cast<ScalarBandMaterial *>(node->material()));
                // Material too: an emptied node has no batch, and the classification changes.
                node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
                nodeOf[chunk] = node;
                vertices += qint64(count);
                bytes += qint64(count * sizeof(ScalarFillVertex) +
                                indices.size() * sizeof(quint16));
            }
            while (reuse) { // surplus nodes from a wetter frame
                QSGNode *next = reuse->nextSibling();
                container->removeChildNode(reuse);
                delete reuse;
                reuse = next;
            }
            forEachIndex(nodeOf.size(), parallelPasses, [&](size_t c) {
                if (!nodeOf[c])
                    return;
                auto *geo = nodeOf[c]->geometry();
                const auto &positions = sf.vertices[c];
                const auto &values = sf.scanValues[c];
                auto *data = static_cast<ScalarFillVertex *>(geo->vertexData());
                for (size_t v = 0; v < positions.size(); ++v)
                    data[v] = {positions[v].x, positions[v].y, values[v]};
                const auto &indices = drawnIndices(c);
                std::memcpy(geo->indexDataAsUShort(), indices.data(),
                            indices.size() * sizeof(quint16));
            });
            for (auto *node : nodeOf)
                if (node) {
                    node->geometry()->markVertexDataDirty();
                    node->geometry()->markIndexDataDirty();
                }
            if (statsOn)
                stats.addPass(pass, vertices, bytes);
        };
        auto drawGpuBands = [&](const std::vector<double> &levels, const QVector<QColor> &colors) {
            std::vector<std::array<float, 4>> premultiplied;
            premultiplied.reserve(size_t(colors.size()));
            for (const auto &c : colors)
                premultiplied.push_back(premultipliedOf(c));
            drawGpuScalar(
                root->gpuBandNode,
                [&](ScalarBandMaterial *m) { m->setBands(levels, dryDepth, premultiplied); },
                std::max(levels.front(), dryDepth), QStringLiteral("bands(gpu)"));
        };

        // ---- Pass 2: filled contour bands -------------------------------
        if (rebuildFills) {
            if (bandsVisible && maxDepth > dryDepth) {
                const auto &levels = bandLevels;
                ColoredVertexUpload bandVerts(bandNode);
                if (levels.size() >= 2) {
                    const int bandCount = int(levels.size()) - 1;
                    const qreal bandOp = std::clamp<qreal>(bandSub->opacity(), 0.0, 1.0);
                    QVector<QColor> bandColors;
                    bandColors.reserve(bandCount);
                    for (int i = 0; i < bandCount; ++i) {
                        QColor c = bs ? bs->colorForBand(i, bandCount)
                                      : OpenSWMM::Contour::viridisAt((double(i) + 0.5) / bandCount);
                        c.setAlphaF(c.alphaF() * bandOp);
                        bandColors.append(c);
                    }
                    auto bandColor = [&](int idx) { return bandColors[idx]; };

                    // Flat per-cell classification (bucket each cell against the
                    // scheme edges), clipped to the wet footprint.
                    auto emitFlatCells = [&]() {
                        for (int i : frameTriangles) {
                            const auto t = frameTri(i);
                            if (!std::isfinite(t.depth) || t.depth < levels.front())
                                continue;
                            const auto wet = CellWaterGeometry::clipTriangle(
                                t.a, t.b, t.c, t.dv0, t.dv1, t.dv2, dryDepth);
                            if (wet.size < 3)
                                continue;
                            int idx = int(std::upper_bound(levels.begin() + 1, levels.end() - 1,
                                                           double(t.depth)) -
                                          (levels.begin() + 1));
                            idx = std::clamp(idx, 0, bandCount - 1);
                            const QColor col = bandColor(idx);
                            const quint8 a = quint8(col.alpha());
                            const quint8 r = premul(quint8(col.red()), a);
                            const quint8 g = premul(quint8(col.green()), a);
                            const quint8 b = premul(quint8(col.blue()), a);
                            for (int k = 1; k + 1 < wet.size; ++k)
                                for (int j : {0, k, k + 1}) {
                                    const auto &pt = wet.vertices[j].point;
                                    QSGGeometry::ColoredPoint2D v;
                                    v.set(float(pt.x() - ox), float(pt.y() - oy), r, g, b, a);
                                    bandVerts.push_back(v);
                                }
                        }
                    };

                    if (smoothBands && frameGpuBands) {
                        drawGpuBands(levels, bandColors);
                    } else if (smoothBands) {
                        for (const auto &chunk : frame.contours().packedBands)
                            bandVerts.appendIndexedChunk(chunk, bandColors);
                    } else {
                        bandVerts.reserve(visibleCells.size() * 3);
                        emitFlatCells();
                    }
                }
                bandVerts.finish();
                if (!(smoothBands && frameGpuBands && levels.size() >= 2))
                    deleteChildren(root->gpuBandNode);
                if (statsOn)
                    stats.addPass(QStringLiteral("bands"), qint64(bandVerts.size()),
                                  qint64(bandVerts.bytes()));
            } else {
                uploadColoredVerts(bandNode, std::vector<QSGGeometry::ColoredPoint2D>{});
                deleteChildren(root->gpuBandNode);
            }
        }

        // ---- Direct mesh-fill depth passes ------------------------------
        // Fill the triangle mesh directly (no marching-squares) — the
        // seam-free alternative to contour bands. `perVertex=false` flat-fills
        // each cell from its cell-centre depth; `perVertex=true` colours each
        // mesh vertex from its (interpolated) depth and lets the GPU
        // interpolate across the triangle (Gouraud).
        bool gpuFillDrawn = false; // this frame's smooth fill went to root->gpuFillNode
        auto buildFillPass = [&](const OpenSWMM::Render::ScalarFillStyle *style, qreal subOpacity,
                                 bool perVertex, ColoredVertexUpload &out) {
            double vMin = dryDepth, vMax = maxDepth;
            if (style->useCustomRange() && style->rangeMax() > style->rangeMin()) {
                vMin = style->rangeMin();
                vMax = style->rangeMax();
            }
            const bool classified = style->classified();
            std::vector<double> levels;
            int bandCount = 1;
            if (classified) {
                const auto samples =
                    style->scheme().rangeMode() == OpenSWMM::Render::RangeMode::PerFrameAutoStretch
                        ? frame.depthSamples
                        : m_layer->depthClassificationSamples(style->scheme());
                const QVector<double> edges = style->scheme().levelEdges(vMin, vMax, samples);
                levels.assign(edges.cbegin(), edges.cend());
                bandCount = std::max(1, int(levels.size()) - 1);
            }
            const qreal op = std::clamp<qreal>(subOpacity, 0.0, 1.0);
            const OpenSWMM::Render::ClassificationColorSampler colors(style->scheme(), vMin, vMax,
                                                                      classified ? bandCount : 0);
            auto colorAt = [&](double value) -> QColor {
                QColor c;
                if (classified && levels.size() >= 2) {
                    int idx =
                        int(std::upper_bound(levels.begin() + 1, levels.end() - 1, double(value)) -
                            (levels.begin() + 1));
                    idx = std::clamp(idx, 0, bandCount - 1);
                    c = colors.classColor(idx);
                } else {
                    c = colors.valueColor(double(value));
                }
                c.setAlphaF(c.alphaF() * op);
                return c;
            };
            // Fast surface: corners are shared vertex values, so colour each shared vertex
            // once from a lookup table and draw the wet triangles indexed, without clipping
            // or per-vertex deduplication. A corner at or below the visible minimum is
            // transparent, so the waterline fades across its triangle.
            if (perVertex && frame.sharedCorners && ensureSharedFillChunks(ox, oy)) {
                // A classified fill is classified per fragment on the GPU, so class edges stay
                // crisp; colouring vertices would blend neighbouring classes across triangles.
                if (classified && levels.size() >= 2 &&
                    levels.size() <= size_t(ScalarBandMaterial::kMaxBands) + 1 &&
                    gpuBandsEnabled()) {
                    std::vector<std::array<float, 4>> classColors;
                    for (int idx = 0; idx < bandCount; ++idx) {
                        QColor c = colors.classColor(idx);
                        c.setAlphaF(c.alphaF() * op);
                        classColors.push_back(premultipliedOf(c));
                    }
                    const double floor = std::max(dryDepth, vMin);
                    drawGpuScalar(
                        root->gpuFillNode,
                        [&](ScalarBandMaterial *m) { m->setBands(levels, floor, classColors); },
                        std::max(levels.front(), floor), QStringLiteral("smoothFill(gpu)"));
                    gpuFillDrawn = true;
                    return;
                }
                constexpr int kLut = 1024;
                std::array<std::array<quint8, 4>, kLut> lut;
                const double span = vMax > vMin ? vMax - vMin : 0.;
                for (int j = 0; j < kLut; ++j) {
                    const QColor c = colorAt(vMin + span * (j + .5) / kLut);
                    const quint8 a = quint8(c.alpha());
                    lut[size_t(j)] = {premul(quint8(c.red()), a), premul(quint8(c.green()), a),
                                      premul(quint8(c.blue()), a), a};
                }
                const double floor = std::max(dryDepth, vMin);
                scanSharedChunks();
                // Triangles whose top corner is at/below floor are not drawn; the scan already
                // dropped those at/below the dry cutoff.
                const bool filter = floor > dryDepth;
                auto drawnIndices = [&](size_t c) -> const std::vector<quint16> & {
                    return filter ? sf.frameIndices[c] : sf.scanIndices[c];
                };
                forEachIndex(sf.scanIndices.size(), parallelPasses, [&](size_t c) {
                    if (filter)
                        keepScanned(c, [floor](double top) { return top > floor; },
                                    sf.frameIndices[c]);
                    const auto &indices = drawnIndices(c);
                    if (indices.empty())
                        return;
                    const auto &values = sf.scanValues[c];
                    auto &vertices = sf.frameVertices[c];
                    vertices = sf.vertices[c];
                    for (const quint16 local : indices) {
                        auto &v = vertices[local];
                        const float d = values[local];
                        if (d <= floor)
                            continue; // transparent, as copied
                        const int j = span > 0 ? std::clamp(int((d - vMin) / span * kLut), 0,
                                                            kLut - 1)
                                               : 0;
                        const auto &c = lut[size_t(j)];
                        v.r = c[0];
                        v.g = c[1];
                        v.b = c[2];
                        v.a = c[3];
                    }
                });
                for (size_t c = 0; c < sf.scanIndices.size(); ++c)
                    out.appendColoredChunk(sf.frameVertices[c], drawnIndices(c));
                return;
            }
            out.reserve(visibleCells.size() * 3);
            auto pushV = [&](const QPointF &p, const QColor &c) {
                QSGGeometry::ColoredPoint2D v;
                const quint8 a = quint8(c.alpha());
                v.set(float(p.x() - ox), float(p.y() - oy), premul(quint8(c.red()), a),
                      premul(quint8(c.green()), a), premul(quint8(c.blue()), a), a);
                out.push_back(v);
            };
            std::vector<QPointF> points, clipped, part;
            std::vector<double> values, clippedValues, partValues;
            for (auto *buffer : {&points, &clipped, &part})
                buffer->reserve(6);
            for (auto *buffer : {&values, &clippedValues, &partValues})
                buffer->reserve(6);
            auto emitPolygon = [&](const auto &pts, const auto &vals) {
                for (size_t k = 1; k + 1 < pts.size(); ++k)
                    for (size_t j : {size_t(0), k, k + 1})
                        pushV(pts[j], colorAt(std::max(vals[j], vMin)));
            };
            for (int i : frameTriangles) {
                const auto t = frameTri(i);
                if (!perVertex && (!std::isfinite(t.depth) || t.depth < vMin))
                    continue;
                const auto wet =
                    CellWaterGeometry::clipTriangle(t.a, t.b, t.c, t.dv0, t.dv1, t.dv2, dryDepth);
                if (!perVertex) {
                    const QColor color = colorAt(t.depth);
                    for (int k = 1; k + 1 < wet.size; ++k)
                        for (int j : {0, k, k + 1})
                            pushV(wet.vertices[j].point, color);
                    continue;
                }
                points.clear();
                values.clear();
                for (int j = 0; j < wet.size; ++j) {
                    points.push_back(wet.vertices[j].point);
                    values.push_back(wet.vertices[j].depth);
                }
                // Clip the scalar field, not its colors: transparent endpoints
                // bleed below the minimum and saturated endpoints shift the
                // maximum crossing when the GPU interpolates the triangle.
                OpenSWMM::Contour::detail::clipHalfplane(points, values, vMin, true, clipped,
                                                         clippedValues);
                if (clipped.empty())
                    continue;
                const auto bounds = std::minmax_element(clippedValues.begin(), clippedValues.end());
                if (*bounds.first < vMax && *bounds.second > vMax) {
                    OpenSWMM::Contour::detail::clipHalfplane(clipped, clippedValues, vMax, false,
                                                             part, partValues);
                    emitPolygon(part, partValues);
                    OpenSWMM::Contour::detail::clipHalfplane(clipped, clippedValues, vMax, true,
                                                             part, partValues);
                    emitPolygon(part, partValues);
                } else
                    emitPolygon(clipped, clippedValues);
            }
        };

        if (rebuildFills) {
            ColoredVertexUpload cellVerts(cellFillNode);
            if (cellSub && cellSub->isVisible() && cellSub->fillStyle() && maxDepth > dryDepth)
                buildFillPass(cellSub->fillStyle(), cellSub->opacity(),
                              /*perVertex=*/false, cellVerts);
            cellVerts.finish();
            if (statsOn && !cellVerts.empty())
                stats.addPass(QStringLiteral("cellFill"), qint64(cellVerts.size()),
                              qint64(cellVerts.bytes()));

            const bool smoothWanted = smoothSub && smoothSub->isVisible() &&
                                      smoothSub->fillStyle() && maxDepth > dryDepth;
            // Cell-owned corners and moving wet boundaries require clipped,
            // per-cell geometry; global vertex slots cannot represent them.
            ColoredVertexUpload smoothVerts(smoothFillNode);
            if (smoothWanted)
                buildFillPass(smoothSub->fillStyle(), smoothSub->opacity(), true, smoothVerts);
            if (!gpuFillDrawn)
                deleteChildren(root->gpuFillNode);
            smoothVerts.finish();
            if (statsOn && !smoothVerts.empty())
                stats.addPass(QStringLiteral("smoothFill"), qint64(smoothVerts.size()),
                              qint64(smoothVerts.bytes()));
        }

        // Result rows. Scalars draw every display switched on, bottom-up: cell
        // fill (flat or smooth), filled contours, isolines, value labels. The
        // depth row's fills, contours and isolines are the depth passes, so
        // only its value labels draw here. Vectors draw static streaks under
        // arrows; the surface velocity row's streaks are the particle pass.
        // Use the displayed hydraulic frame while a newer contour job is
        // pending, so overlays cannot show different reporting times.
        if (rebuildFills) {
            const auto *depthRow = m_layer->depthResultSublayer();
            const QTransform toScreen(width() / m_extent.width(), 0, 0,
                                      height() / m_extent.height(),
                                      -m_extent.xMin() * width() / m_extent.width(),
                                      m_extent.yMax() * height() / m_extent.height());
            const auto fromScreen = toScreen.inverted();
            const auto pushColored = [&](std::vector<QSGGeometry::ColoredPoint2D> &out,
                                         const QPointF &point, const QColor &color) {
                const quint8 alpha = quint8(color.alpha());
                QSGGeometry::ColoredPoint2D vertex;
                vertex.set(float(point.x() - ox), float(point.y() - oy),
                           premul(quint8(color.red()), alpha), premul(quint8(color.green()), alpha),
                           premul(quint8(color.blue()), alpha), alpha);
                out.push_back(vertex);
            };
            const auto inCull = [&](const QPointF &a, const QPointF &b, const QPointF &c) {
                return std::max({a.x(), b.x(), c.x()}) >= cullRect.left() &&
                       std::min({a.x(), b.x(), c.x()}) <= cullRect.right() &&
                       std::max({a.y(), b.y(), c.y()}) >= cullRect.top() &&
                       std::min({a.y(), b.y(), c.y()}) <= cullRect.bottom();
            };
            for (auto *base : m_layer->sublayers()) {
                if (auto *vector = qobject_cast<OpenSWMM::Render::ResultVectorSublayer *>(base)) {
                    auto *nodes = root->resultVectorNodes.value(vector->id());
                    if (!nodes)
                        continue;
                    std::vector<QSGGeometry::ColoredPoint2D> streakVerts, arrowVerts;
                    if (vector->isVisible() && vector->opacity() > 0) {
                        const auto *style = vector->vectorStyle();
                        // The displayed frame's time, like the scalar rows.
                        const auto geometry = m_layer->resultVectorGeometry(
                            vector, cullRect, double(sx_r), nullptr, nullptr, nullptr,
                            frame.key.time);
                        // Legends and colour ranges read the time drawn here.
                        vector->publishPresentedTime(frame.key.time);
                        const float streakHalf = float(0.5 * style->streakWidthPx()) * invView;
                        for (const auto &streak : geometry.streaks) {
                            const size_t n = streak.points.size();
                            for (size_t i = 1; i < n; ++i) {
                                // Fade from the oldest point to the head.
                                QColor color = streak.color;
                                color.setAlphaF(color.alphaF() *
                                                (0.25 + 0.75 * double(i) / double(n - 1)));
                                const quint8 a = quint8(color.alpha());
                                const auto &p0 = streak.points[i - 1], &p1 = streak.points[i];
                                appendThickSegColored(
                                    streakVerts, float(p0.x() - ox), float(p0.y() - oy),
                                    float(p1.x() - ox), float(p1.y() - oy), streakHalf,
                                    premul(quint8(color.red()), a),
                                    premul(quint8(color.green()), a),
                                    premul(quint8(color.blue()), a), a);
                            }
                        }
                        const float shaftHalf = float(0.5 * style->shaftWidthPx()) * invView;
                        const double headScene = style->headSizePx() * double(invView);
                        for (const auto &arrow : geometry.arrows) {
                            const QPointF axis = arrow.tip - arrow.tail;
                            const double length = std::hypot(axis.x(), axis.y());
                            if (length <= 0)
                                continue;
                            const QPointF unit = axis / length;
                            const double headLength = std::min(headScene, 0.6 * length);
                            const QPointF headBase = arrow.tip - unit * headLength;
                            const QPointF normal(-unit.y(), unit.x());
                            const quint8 a = quint8(arrow.color.alpha());
                            appendThickSegColored(arrowVerts, float(arrow.tail.x() - ox),
                                                  float(arrow.tail.y() - oy),
                                                  float(headBase.x() - ox),
                                                  float(headBase.y() - oy), shaftHalf,
                                                  premul(quint8(arrow.color.red()), a),
                                                  premul(quint8(arrow.color.green()), a),
                                                  premul(quint8(arrow.color.blue()), a), a);
                            pushColored(arrowVerts, arrow.tip, arrow.color);
                            pushColored(arrowVerts, headBase + normal * (0.55 * headLength),
                                        arrow.color);
                            pushColored(arrowVerts, headBase - normal * (0.55 * headLength),
                                        arrow.color);
                        }
                    }
                    uploadColoredVerts(nodes->streaks, streakVerts);
                    uploadColoredVerts(nodes->arrows, arrowVerts);
                    if (statsOn && !(streakVerts.empty() && arrowVerts.empty()))
                        stats.addPass(vector->id(), qint64(streakVerts.size() + arrowVerts.size()),
                                      qint64((streakVerts.size() + arrowVerts.size()) *
                                             sizeof(QSGGeometry::ColoredPoint2D)));
                    continue;
                }
                auto *sub = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(base);
                if (!sub)
                    continue;
                std::vector<QSGGeometry::ColoredPoint2D> vertices, smoothVerts, bandVerts;
                std::vector<QSGGeometry::Point2D> scalarLines;
                auto *nodes = root->resultScalarNodes.value(sub->id());
                if (!nodes)
                    continue;
                nodes->clearLabels();
                nodes->labels->setOpacity(sub->opacity());
                std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> presented;
                if (sub->isVisible() && sub->opacity() > 0) {
                    const auto *scalarStyle = sub->fillStyle();
                    const bool depthPasses = sub == depthRow;
                    // Fills, filled contours and labels share the fill scheme's frame;
                    // isolines read their own, so a whole-run isoline range neither
                    // blanks nor rescales the fills. The depth row draws only labels.
                    using FramePtr = std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame>;
                    const bool fills = !depthPasses && (scalarStyle->cellFill() ||
                                                        scalarStyle->filledContours());
                    FramePtr scalarFrame, lineFrame;
                    if (fills || scalarStyle->cellLabels())
                        scalarFrame = m_layer->resultFrame(
                            sub->variableKey(), frame.key.time,
                            !depthPasses && scalarStyle->fillNeedsWholeRunRange());
                    if (!depthPasses && scalarStyle->isolines())
                        lineFrame = m_layer->resultFrame(
                            sub->variableKey(), frame.key.time,
                            scalarStyle->isolinesNeedWholeRunRange());
                    const bool fillOk = scalarFrame && scalarFrame->error.isEmpty();
                    const bool lineOk = lineFrame && lineFrame->error.isEmpty();
                    presented = fillOk ? scalarFrame : lineOk ? lineFrame : FramePtr();
                    // Contour lines (contours = true) or cell value labels of a frame.
                    const auto drawLines = [&](const FramePtr &source, bool contours) {
                        const auto *style = scalarStyle->contourStyle();
                        const auto levels =
                            contours ? scalarStyle->contourLevels(source->minimum,
                                                                  source->maximum, source->samples)
                                     : std::vector<double>();
                        const auto geometry =
                            m_layer->resultScalarGeometry(source, levels, !contours);
                        QColor color = style->color();
                        color.setAlphaF(color.alphaF() * sub->opacity());
                        setFlatColor(nodes->lines, color);
                        for (const auto &line : geometry->contours) {
                            const auto index =
                                std::lower_bound(levels.begin(), levels.end(), line.level) -
                                levels.begin();
                            const double lineWidth =
                                style->indexEvery() > 0 && (index + 1) % style->indexEvery() == 0
                                    ? style->indexWidthPx()
                                    : style->lineWidthPx();
                            const auto a = toScreen.map(line.a), b = toScreen.map(line.b);
                            const double length = QLineF(a, b).length();
                            if (length <= 0 || style->dashPattern() == Qt::NoPen)
                                continue;
                            QPen pen;
                            pen.setStyle(style->dashPattern());
                            const auto dash = pen.dashPattern();
                            auto segment = [&](double start, double finish) {
                                const auto x = fromScreen.map(a + (b - a) * (start / length)),
                                           y = fromScreen.map(a + (b - a) * (finish / length));
                                appendThickSeg(scalarLines, float(x.x() - ox), float(x.y() - oy),
                                               float(y.x() - ox), float(y.y() - oy),
                                               float(lineWidth * .5 * invView));
                            };
                            if (dash.isEmpty())
                                segment(0, length);
                            else {
                                double at = 0;
                                int part = 0;
                                while (at < length) {
                                    const double next = std::min(
                                        length, at + std::max(.1, double(dash[part % dash.size()]) *
                                                                      std::max(.1, lineWidth)));
                                    if (part % 2 == 0)
                                        segment(at, next);
                                    at = next;
                                    ++part;
                                }
                            }
                        }
                        if (window() && (!contours || style->labels())) {
                            QFont font;
                            font.setBold(true);
                            font.setPointSizeF(style->labelFontPt());
                            const auto labels = OpenSWMM::Render::placeResultScalarLabels(
                                *geometry, toScreen, QRectF(0, 0, width(), height()), font,
                                style->labelDecimals(),
                                source->descriptor.unitsKnown ? source->descriptor.units
                                                              : QString(),
                                contours);
                            const double dpr = window()->devicePixelRatio();
                            for (const auto &label : labels) {
                                const auto image =
                                    rasteriseLabel(label.text, style->color(), style->labelFontPt(),
                                                   style->labelHalo(), dpr);
                                auto *texture = window()->createTextureFromImage(
                                    image, QQuickWindow::TextureHasAlphaChannel);
                                if (!texture)
                                    continue;
                                auto *node = new QSGSimpleTextureNode;
                                node->setTexture(texture);
                                node->setOwnsTexture(true);
                                node->setFiltering(QSGTexture::Linear);
                                node->setRect(fromScreen.mapRect(label.rect).translated(-ox, -oy));
                                nodes->labels->appendChildNode(node);
                            }
                        }
                    };
                    if (lineOk)
                        drawLines(lineFrame, true);
                    if (fillOk && scalarStyle->cellLabels())
                        drawLines(scalarFrame, false);
                    // Flat cell fill. Groundwater and chemistry have their own
                    // availability status; a dry surface is not a clipping criterion.
                    if (!depthPasses && scalarStyle->cellFill() && !scalarStyle->smoothFill() &&
                        scalarFrame) {
                        const auto colors = sub->cellColors(*scalarFrame);
                        vertices.reserve(visibleCells.size() * 3);
                        for (int triangle : visibleCells) {
                            const int cell = cellOfTri(triangle);
                            if (cell < 0 || cell >= colors.size() || colors[cell].alpha() == 0)
                                continue;
                            const auto &tri = tris[triangle];
                            for (const QPointF &point : {tri.a, tri.b, tri.c})
                                pushColored(vertices, point, colors[cell]);
                        }
                    }
                    // Smooth fill: per-vertex colours of the nodal reconstruction
                    // over valid cells.
                    if (fillOk && !depthPasses && scalarStyle->cellFill() &&
                        scalarStyle->smoothFill()) {
                        const auto geometry =
                            m_layer->resultScalarGeometry(scalarFrame, {}, false, {}, true);
                        const auto colors = sub->valueColors(*scalarFrame, geometry->vertexValues);
                        const auto &triVerts = m_layer->triVertexIndices();
                        smoothVerts.reserve(visibleCells.size() * 3);
                        for (int triangle : visibleCells) {
                            const int cell = cellOfTri(triangle);
                            if (cell < 0 || size_t(cell) >= scalarFrame->status.size() ||
                                scalarFrame->status[size_t(cell)] !=
                                    openswmmvis::io::Mesh2DValueStatus::Valid ||
                                size_t(triangle) >= triVerts.size())
                                continue;
                            const auto &ids = triVerts[size_t(triangle)];
                            bool ok = true;
                            for (int v : ids)
                                ok = ok && v >= 0 && v < colors.size() && colors[v].alpha() > 0;
                            if (!ok)
                                continue;
                            const auto &tri = tris[triangle];
                            pushColored(smoothVerts, tri.a, colors[ids[0]]);
                            pushColored(smoothVerts, tri.b, colors[ids[1]]);
                            pushColored(smoothVerts, tri.c, colors[ids[2]]);
                        }
                    }
                    // Filled contours: marching-triangle bands over the nodal
                    // reconstruction, classes from the fill scheme, for the
                    // triangles in view. Zoomed far out, each cell takes its
                    // band's colour instead (cells are about a pixel there).
                    if (fillOk && !depthPasses && scalarStyle->filledContours()) {
                        // The cull rect spans more than the screen: compare the
                        // triangles in view with the on-screen threshold (as the
                        // painter does) by their share of the cull rect.
                        const double coverage = std::max(
                            1.0, (cullRect.width() * cullRect.height()) /
                                     std::max(1e-300, viewRect.width() * viewRect.height()));
                        std::vector<double> levels;
                        QVector<QColor> colors;
                        if (!sub->bandStyling(*scalarFrame, levels, colors)) {
                        } else if (double(visibleCells.size()) >
                                   double(OpenSWMM::Render::kMaxContouredBandTriangles) *
                                       coverage) {
                            bandVerts.reserve(visibleCells.size() * 3);
                            for (int triangle : visibleCells) {
                                const int cell = cellOfTri(triangle);
                                if (cell < 0 || size_t(cell) >= scalarFrame->status.size() ||
                                    scalarFrame->status[size_t(cell)] !=
                                        openswmmvis::io::Mesh2DValueStatus::Valid)
                                    continue;
                                const int band = OpenSWMM::Render::resultBandIndex(
                                    levels, scalarFrame->values[size_t(cell)]);
                                if (band < 0 || band >= colors.size() || colors[band].alpha() == 0)
                                    continue;
                                const auto &tri = tris[triangle];
                                for (const QPointF &point : {tri.a, tri.b, tri.c})
                                    pushColored(bandVerts, point, colors[band]);
                            }
                        } else {
                            const auto geometry = m_layer->resultScalarGeometry(
                                scalarFrame, {}, false, levels, false, cullRect);
                            for (const auto &band : geometry->bands) {
                                if (band.verts.size() < 3 || band.bandIndex < 0 ||
                                    band.bandIndex >= colors.size() ||
                                    colors[band.bandIndex].alpha() == 0)
                                    continue;
                                const auto &v = band.verts;
                                QPointF lo = v[0], hi = v[0];
                                for (const auto &point : v) {
                                    lo = QPointF(std::min(lo.x(), point.x()),
                                                 std::min(lo.y(), point.y()));
                                    hi = QPointF(std::max(hi.x(), point.x()),
                                                 std::max(hi.y(), point.y()));
                                }
                                if (!inCull(lo, hi, lo)) // every vertex counts
                                    continue;
                                for (size_t k = 1; k + 1 < v.size(); ++k) {
                                    pushColored(bandVerts, v[0], colors[band.bandIndex]);
                                    pushColored(bandVerts, v[k], colors[band.bandIndex]);
                                    pushColored(bandVerts, v[k + 1], colors[band.bandIndex]);
                                }
                            }
                        }
                    }
                }
                uploadColoredVerts(nodes->fill, vertices);
                uploadColoredVerts(nodes->smooth, smoothVerts);
                uploadColoredVerts(nodes->bands, bandVerts);
                uploadFlatVerts(nodes->lines, scalarLines);
                sub->publishPresentedFrame(std::move(presented));
                if (statsOn && !(vertices.empty() && smoothVerts.empty() && bandVerts.empty()))
                    stats.addPass(sub->id(),
                                  qint64(vertices.size() + smoothVerts.size() + bandVerts.size()),
                                  qint64((vertices.size() + smoothVerts.size() + bandVerts.size()) *
                                         sizeof(QSGGeometry::ColoredPoint2D)));
            }
        }

        // ---- Pass 3 + 3b: isolines, index contours, labels ---------------
        if (rebuildFills) {
            // Fast-surface isolines without labels: drawn per fragment on the GPU with the
            // same width, index-contour and colour rules as the marched lines below.
            const bool gpuIsoDrawn = isoVisible && maxDepth > dryDepth && frameGpuIsolines &&
                                     !isoLevels.empty();
            if (gpuIsoDrawn) {
                const int idxEvery = is ? is->indexEvery() : 0;
                const double widthPx = is ? std::max(0.5, is->lineWidthPx()) : 1.0;
                const double indexPx = is ? std::max(widthPx, is->indexWidthPx()) : widthPx;
                // The shader measures device pixels; the marched lines are logical pixels wide.
                const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
                std::vector<float> halfWidths(isoLevels.size());
                for (size_t i = 0; i < isoLevels.size(); ++i) {
                    bool index = false;
                    if (is && idxEvery >= 2) {
                        if (is->levelMode() ==
                            OpenSWMM::Render::IsolineStyle::LevelMode::FixedInterval)
                            index = std::lround((isoLevels[i] - is->baseLevel()) /
                                                std::max(is->levelInterval(), 1e-12)) %
                                        idxEvery ==
                                    0;
                        else
                            index = (int(i) + 1) % idxEvery == 0;
                    }
                    halfWidths[i] = float(0.5 * (index ? indexPx : widthPx) * dpr);
                }
                QColor isoColor = is ? is->color() : QColor(10, 10, 10, 220);
                isoColor.setAlpha(int(qBound(0.0, isoColor.alpha() * isoSub->opacity(), 255.0)));
                const auto color = premultipliedOf(isoColor);
                drawGpuScalar(
                    root->gpuIsoNode,
                    [&](ScalarBandMaterial *m) { m->setIsolines(isoLevels, halfWidths, color); },
                    -std::numeric_limits<double>::infinity(), QStringLiteral("isolines(gpu)"));
            } else {
                deleteChildren(root->gpuIsoNode);
            }
            if (isoVisible && maxDepth > dryDepth && !gpuIsoDrawn) {
                using namespace OpenSWMM::Contour;
                const auto &levels = isoLevels;
                if (!levels.empty()) {
                    const auto &segs = frame.contours().segs;

                    // Coverage-culled view of the cached segments (anchor-
                    // relative → cullLocal). Shared by the draw + label passes.
                    std::vector<const IsoLineSegment *> visSegs;
                    visSegs.reserve(segs.size());
                    for (const auto &s : segs) {
                        const double sMinX = std::min(s.a.x(), s.b.x());
                        const double sMaxX = std::max(s.a.x(), s.b.x());
                        const double sMinY = std::min(s.a.y(), s.b.y());
                        const double sMaxY = std::max(s.a.y(), s.b.y());
                        if (sMaxX < cullLocal.left() || sMinX > cullLocal.right() ||
                            sMaxY < cullLocal.top() || sMinY > cullLocal.bottom())
                            continue;
                        visSegs.push_back(&s);
                    }

                    // Index-contour predicate (same convention as the CPU pass).
                    const int idxEvery = is ? is->indexEvery() : 0;
                    QHash<double, int> ordinalOf;
                    for (int i = 0; i < int(levels.size()); ++i)
                        ordinalOf.insert(levels[i], i);
                    auto isIndexLevel = [&](double level) -> bool {
                        if (!is || idxEvery < 2)
                            return false;
                        if (is->levelMode() ==
                            OpenSWMM::Render::IsolineStyle::LevelMode::FixedInterval) {
                            const long k = std::lround((level - is->baseLevel()) /
                                                       std::max(is->levelInterval(), 1e-12));
                            return (k % idxEvery) == 0;
                        }
                        return ((ordinalOf.value(level, 0) + 1) % idxEvery) == 0;
                    };

                    const double widthPx = is ? std::max(0.5, is->lineWidthPx()) : 1.0;
                    const double indexPx = is ? std::max(widthPx, is->indexWidthPx()) : widthPx;
                    const float lineHW = float(0.5 * widthPx) * invView;
                    const float indexHW = float(0.5 * indexPx) * invView;

                    std::vector<QSGGeometry::Point2D> lineVerts, indexVerts;
                    lineVerts.reserve(visSegs.size() * 6);
                    for (const auto *sp : visSegs) {
                        const auto &s = *sp;
                        if (isIndexLevel(s.level))
                            appendThickSeg(indexVerts, float(s.a.x()), float(s.a.y()),
                                           float(s.b.x()), float(s.b.y()), indexHW);
                        else
                            appendThickSeg(lineVerts, float(s.a.x()), float(s.a.y()),
                                           float(s.b.x()), float(s.b.y()), lineHW);
                    }
                    uploadFlatVerts(isoNode, lineVerts);
                    uploadFlatVerts(isoIndexNode, indexVerts);
                    if (statsOn)
                        stats.addPass(QStringLiteral("isolines"),
                                      qint64(lineVerts.size() + indexVerts.size()),
                                      qint64((lineVerts.size() + indexVerts.size()) *
                                             sizeof(QSGGeometry::Point2D)));

                    QColor isoColor = is ? is->color() : QColor(10, 10, 10, 220);
                    isoColor.setAlpha(
                        int(qBound(0.0, isoColor.alpha() * isoSub->opacity(), 255.0)));
                    setFlatColor(isoNode, isoColor);
                    setFlatColor(isoIndexNode, isoColor);

                    // ---- Pass 3b: along-line labels ----------------------
                    // The labels draw at the pass's opacity, like its lines.
                    isoLabels->setOpacity(std::clamp(isoSub->opacity(), qreal(0), qreal(1)));
                    while (auto *c = isoLabels->firstChild()) {
                        isoLabels->removeChildNode(c);
                        delete c;
                    }
                    if (is && is->labels() && lod.drawContourLabels && window()) {
                        QHash<double, QVector<QLineF>> byLevel;
                        for (const auto *sp : visSegs)
                            byLevel[sp->level].append(QLineF(sp->a, sp->b));

                        const double dpr = window()->effectiveDevicePixelRatio();
                        const int decimals = is->labelDecimals();
                        const bool halo = is->labelHalo();
                        const double fontPt = is->labelFontPt();
                        // Spacing measured in screen px, converted to the
                        // anchor-shifted scene units the chains live in.
                        const double spacingScene = 250.0 * double(invView);
                        const double quantum = 1e-9 * std::max({bb.width(), bb.height(), 1.0});
                        int labelBudget = lod.maxContourLabels;

                        // Bounded: a live run keeps minting new level strings as
                        // the depth range grows, and every one used to stay a GPU
                        // texture for the rest of the session. Evict wholesale
                        // (before the pass, so nodes built below hold fresh ones).
                        if (m_labelTextureCache.size() > 512)
                            clearLabelTextureCache();

                        for (auto it = byLevel.constBegin();
                             it != byLevel.constEnd() && labelBudget > 0; ++it) {
                            const QString text = QString::number(it.key(), 'f', decimals);
                            const QString cacheKey = text + QLatin1Char('|') +
                                                     QString::number(fontPt) + QLatin1Char('|') +
                                                     QString::number(int(halo)) + QLatin1Char('|') +
                                                     (is->color().name(QColor::HexArgb));

                            QSGTexture *tex = m_labelTextureCache.value(cacheKey, nullptr);
                            if (!tex) {
                                const QImage img =
                                    rasteriseLabel(text, is->color(), fontPt, halo, dpr);
                                tex = window()->createTextureFromImage(
                                    img, QQuickWindow::TextureHasAlphaChannel);
                                if (tex)
                                    m_labelTextureCache.insert(cacheKey, tex);
                            }
                            if (!tex)
                                continue;

                            const QSizeF szPx = tex->textureSize() / dpr; // logical px
                            const double wScene = szPx.width() * double(invView);
                            const double hScene = szPx.height() * double(invView);

                            const auto chains =
                                OpenSWMM::Contour::chainIsoSegments(it.value(), quantum);
                            for (const QPolygonF &chain : chains) {
                                if (labelBudget <= 0)
                                    break;
                                if (chain.size() < 2)
                                    continue;
                                double total = 0.0;
                                for (int i = 1; i < chain.size(); ++i)
                                    total += QLineF(chain[i - 1], chain[i]).length();
                                if (total < wScene * 1.5)
                                    continue;

                                double acc = 0.0;
                                double nextAt = std::max(spacingScene * 0.5, wScene);
                                for (int i = 1; i < chain.size() && labelBudget > 0; ++i) {
                                    const QPointF a = chain[i - 1];
                                    const QPointF b = chain[i];
                                    const double segLen = QLineF(a, b).length();
                                    if (segLen <= 0.0)
                                        continue;
                                    while (acc + segLen >= nextAt && labelBudget > 0) {
                                        const double tf = (nextAt - acc) / segLen;
                                        const QPointF pos = a + tf * (b - a);
                                        // Scene coords are y-down (scene y =
                                        // -world y) and the item transform
                                        // preserves orientation, so the screen
                                        // angle equals the scene angle.
                                        double angle = qRadiansToDegrees(
                                            std::atan2(b.y() - a.y(), b.x() - a.x()));
                                        if (angle > 90.0)
                                            angle -= 180.0;
                                        if (angle < -90.0)
                                            angle += 180.0;

                                        auto *tnWrap = new QSGTransformNode();
                                        QMatrix4x4 m;
                                        m.translate(float(pos.x()), float(pos.y()));
                                        m.rotate(float(angle), 0.0f, 0.0f, 1.0f);
                                        tnWrap->setMatrix(m);

                                        auto *tn = new QSGSimpleTextureNode();
                                        tn->setTexture(tex);
                                        tn->setOwnsTexture(false);
                                        tn->setRect(
                                            QRectF(-0.5 * wScene, -0.5 * hScene, wScene, hScene));
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
        const bool velVisible =
            velSub && velSub->isVisible() && frame.hasVelocity && lod.drawVelocityVectors;
        if (rebuildVel) {
            if (velVisible) {
                const double dryCut = vs ? std::max(vs->dryDepthCutoff(), dryDepth) : dryDepth;
                const double maxVel = std::max(frame.maxVelocity, 1e-6);
                const double vmagSkip = std::max(maxVel * 1e-4, 1e-9);
                const qreal velOp = std::clamp<qreal>(velSub->opacity(), 0.0, 1.0);
                const float shaftHW = float(0.5 * (vs ? vs->shaftWidthPx() : 1.5)) * invView;

                std::vector<QSGGeometry::ColoredPoint2D> glyphVerts;

                // Emit one arrow glyph centred at scene point (cxs,cys) for velocity
                // (gvx,gvy), |v| = gvmag. Shared by the grid-sampled (V2) and the
                // dense per-cell paths so both render identical glyphs.
                auto emitGlyphAt = [&](double cxs, double cys, float gvx, float gvy, float gvmag) {
                    if (gvmag <= 0.0f)
                        return;
                    const double lenPx =
                        vs ? vs->glyphLengthPxForSpeed(gvmag)
                           : m_layer->velocityArrowScale() *
                                 std::log1p(std::clamp(double(gvmag) / maxVel, 0.0, 1.0)) /
                                 std::log1p(1.0);
                    const double lenScene = lenPx * double(invView);
                    if (lenScene <= 0.0)
                        return;

                    QColor col = vs ? vs->colorForSpeed(gvmag) : QColor(20, 20, 20, 220);
                    const quint8 a = quint8(qBound(0, int(col.alpha() * velOp + 0.5), 255));
                    const quint8 r = premul(quint8(col.red()), a);
                    const quint8 g = premul(quint8(col.green()), a);
                    const quint8 b = premul(quint8(col.blue()), a);

                    const double inv_vmag = 1.0 / double(gvmag);
                    const float ux = float(gvx * inv_vmag);
                    const float uy = float(gvy * inv_vmag);
                    const float tailX = float(cxs - ox);
                    const float tailY = float(cys - oy);
                    const float lenS = float(lenScene);

                    // Arrowhead length along the axis, capped so the shaft (tail)
                    // always stays clearly visible (head ≤ 60% of the glyph).
                    const double headLenPx = vs ? vs->headSizePx() : 7.0;
                    const float hLen = float(std::min(headLenPx * double(invView), lenScene * 0.6));
                    const float tipX = tailX + ux * lenS; // arrow tip
                    const float tipY = tailY + uy * lenS;
                    const float baseX = tailX + ux * (lenS - hLen); // head base
                    const float baseY = tailY + uy * (lenS - hLen);

                    // Shaft: tail → arrowhead base, so the tail reads as a line.
                    appendThickSegColored(glyphVerts, tailX, tailY, baseX, baseY, shaftHW, r, g, b,
                                          a);

                    // Sharp filled-triangle arrowhead (tip + two base corners).
                    const float px = -uy, py = ux;    // unit perpendicular
                    const float hHalf = hLen * 0.55f; // arrowhead half-width
                    auto cv = [&](float x, float y) {
                        QSGGeometry::ColoredPoint2D p;
                        p.set(x, y, r, g, b, a);
                        return p;
                    };
                    glyphVerts.push_back(cv(tipX, tipY));
                    glyphVerts.push_back(cv(baseX + px * hHalf, baseY + py * hHalf));
                    glyphVerts.push_back(cv(baseX - px * hHalf, baseY - py * hHalf));
                };

                // Dense per-cell mode only at Near and under the visible-cell
                // cap (Phase 3); otherwise screen-space grid sampling with a
                // floor on the spacing so a "dense" request degrades gracefully
                // instead of emitting one glyph per subpixel cell.
                const double spacingPxCfg = vs ? vs->glyphSpacingPx() : 30.0;
                const bool denseMode = spacingPxCfg <= 1.0 && lod.denseVelocityAllowed;
                const double spacingPx =
                    (spacingPxCfg > 1.0) ? spacingPxCfg : 8.0; // fallback sampling pitch
                if (!denseMode) {
                    // V2 (Issue 5) — sample the interpolated per-vertex velocity
                    // field (velocityAtScene) on a uniform SCREEN-SPACE grid over
                    // the coverage rect, so arrows are evenly spaced, independent
                    // of mesh density, and survive pans within the coverage.
                    const double x0 = std::max(cullRect.left(), bb.left());
                    const double y0 = std::max(cullRect.top(), bb.top());
                    const double x1 = std::min(cullRect.right(), bb.right());
                    const double y1 = std::min(cullRect.bottom(), bb.bottom());
                    const double gridStep = spacingPx * double(invView);
                    if (gridStep > 0.0 && x1 > x0 && y1 > y0) {
                        for (double gy = y0; gy <= y1; gy += gridStep) {
                            for (double gx = x0; gx <= x1; gx += gridStep) {
                                float svx, svy;
                                if (!m_layer->velocityAtScene(QPointF(gx, gy), svx, svy,
                                                              frame.velocity))
                                    continue;
                                const float smag = std::sqrt(svx * svx + svy * svy);
                                if (smag < vmagSkip)
                                    continue;
                                emitGlyphAt(gx, gy, svx, svy, smag);
                            }
                        }
                    }
                } else {
                    // Dense per-cell mode: one arrow at each wet visible cell
                    // centroid, using the cell's own RT0 vector.
                    for (int i : frameTriangles) {
                        const auto t = frameTri(i);
                        if (t.depth < dryCut)
                            continue;
                        if (t.vmag < vmagSkip)
                            continue;
                        emitGlyphAt(t.centroid.x(), t.centroid.y(), t.vx, t.vy, t.vmag);
                    }
                }
                uploadColoredVerts(velNode, glyphVerts);
                if (statsOn)
                    stats.addPass(QStringLiteral("velocity"), qint64(glyphVerts.size()),
                                  qint64(glyphVerts.size() * sizeof(QSGGeometry::ColoredPoint2D)));
            } else {
                uploadColoredVerts(velNode, std::vector<QSGGeometry::ColoredPoint2D>{});
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
                QColor col = es ? es->color() : QColor(0, 0, 0, 130);
                const double w = es ? std::max(0.1, es->lineWidthPx()) : 0.5;
                const qreal op = std::clamp<qreal>(edgeSub->opacity(), 0.0, 1.0);
                col.setAlpha(int(qBound(0.0, col.alpha() * op, 255.0)));
                const float hw = float(0.5 * w) * invView;

                const auto &edges = m_layer->m_sceneEdges;
                std::vector<QSGGeometry::Point2D> edgeSegs;
                qint64 visEdgeCount = 0;
                auto emitEdge = [&](int i) {
                    const auto &e = edges[i];
                    appendThickSeg(edgeSegs, float(e.a.x() - ox), float(e.a.y() - oy),
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
                            maxY < cullRect.top() || minY > cullRect.bottom())
                            continue;
                        emitEdge(i);
                    }
                }
                uploadFlatVerts(edgeNode, edgeSegs);
                setFlatColor(edgeNode, col);
                if (statsOn) {
                    stats.visibleEdges = visEdgeCount;
                    stats.addPass(QStringLiteral("edges"), qint64(edgeSegs.size()),
                                  qint64(edgeSegs.size() * sizeof(QSGGeometry::Point2D)));
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
                QColor col = ns ? ns->color() : QColor(40, 40, 40, 220);
                const double sz = ns ? std::max(0.5, ns->markerSizePx()) : 3.0;
                const qreal op = std::clamp<qreal>(nodeSub->opacity(), 0.0, 1.0);
                col.setAlpha(int(qBound(0.0, col.alpha() * op, 255.0)));
                const float r = float(0.5 * sz) * invView;

                std::vector<QSGGeometry::Point2D> markVerts;
                qint64 visVertCount = 0;
                auto emitQuad = [&](float cx, float cy) {
                    auto V = [](float x, float y) {
                        QSGGeometry::Point2D p;
                        p.x = x;
                        p.y = y;
                        return p;
                    };
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
                            p.y() < cullRect.top() || p.y() > cullRect.bottom())
                            continue;
                        emitQuad(float(p.x() - ox), float(p.y() - oy));
                    }
                } else {
                    for (int i : visibleCells) {
                        const auto &t = tris[i];
                        emitQuad(float(t.a.x() - ox), float(t.a.y() - oy));
                        emitQuad(float(t.b.x() - ox), float(t.b.y() - oy));
                        emitQuad(float(t.c.x() - ox), float(t.c.y() - oy));
                    }
                }
                uploadFlatVerts(nodeMarkNode, markVerts);
                setFlatColor(nodeMarkNode, col);
                if (statsOn) {
                    stats.visibleVertices = visVertCount;
                    stats.addPass(QStringLiteral("markers"), qint64(markVerts.size()),
                                  qint64(markVerts.size() * sizeof(QSGGeometry::Point2D)));
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
                const float edgeHW = 1.0f * invView; // ~2 px outline
                // Highlights are CELL indices: fill every display sub-triangle
                // of the cell, outline the true cell boundary — a quad's VFR
                // diagonal (the edge its two sub-triangles share) is not a
                // mesh edge and is never stroked.
                const auto &cellRange = m_layer->cellTriRange();
                const auto &triIdx = m_layer->triVertexIndices();
                const int nCell = m_layer->cellCount();
                auto P = [&](const QPointF &p) {
                    QSGGeometry::Point2D q;
                    q.x = float(p.x() - ox);
                    q.y = float(p.y() - oy);
                    return q;
                };
                for (int cell : hi) {
                    if (cell < 0 || cell >= nCell || cell + 1 >= int(cellRange.size()))
                        continue;
                    const int t0 = cellRange[size_t(cell)];
                    const int t1 = std::min(cellRange[size_t(cell) + 1], nTri);
                    for (int idx = t0; idx < t1; ++idx) {
                        const auto &t = tris[idx];
                        fillVertsH.push_back(P(t.a));
                        fillVertsH.push_back(P(t.b));
                        fillVertsH.push_back(P(t.c));
                        const QSGGeometry::Point2D pts[3] = {P(t.a), P(t.b), P(t.c)};
                        const auto &vid = triIdx[size_t(idx)];
                        for (int e = 0; e < 3; ++e) {
                            const int va = vid[e], vb = vid[(e + 1) % 3];
                            // Shared with the cell's other sub-triangle → diagonal.
                            bool diagonal = false;
                            for (int o = t0; o < t1 && !diagonal; ++o) {
                                if (o == idx)
                                    continue;
                                const auto &ov = triIdx[size_t(o)];
                                int shared = 0;
                                for (int k = 0; k < 3; ++k)
                                    if (ov[k] == va || ov[k] == vb)
                                        ++shared;
                                diagonal = (shared == 2);
                            }
                            if (diagonal)
                                continue;
                            appendThickSeg(edgeVertsH, pts[e].x, pts[e].y, pts[(e + 1) % 3].x,
                                           pts[(e + 1) % 3].y, edgeHW);
                        }
                    }
                }
            }
            uploadFlatVerts(hiFillNode, fillVertsH);
            uploadFlatVerts(hiEdgeNode, edgeVertsH);
            setFlatColor(hiFillNode, kHiFillColor);
            setFlatColor(hiEdgeNode, kHiEdgeColor);
            if (statsOn)
                stats.addPass(
                    QStringLiteral("selection"), qint64(fillVertsH.size() + edgeVertsH.size()),
                    qint64((fillVertsH.size() + edgeVertsH.size()) * sizeof(QSGGeometry::Point2D)));
            m_lastHighlight = hi;
        }

        // ---- Snapshot / key bookkeeping ----------------------------------
        if (rebuildFills) {
            m_lastRenderedTime = frame.key.time;
            m_lastRenderedFrame = frame.key.frameRev;
            if (!(frame.key == key) && !m_contourBusy) {
                // A candidate held for its particles can outlive the final
                // playback tick. Once it is presented, explicitly resume the
                // coalesced request, even when playback has just been paused.
                QMetaObject::invokeMethod(this, [this] {
                    m_dirty.noteDataChanged();
                    noteContentChanged();
                    emit contentReady();
                }, Qt::QueuedConnection);
            }
        }
        if (bits & D::Geometry)
            m_lastGeomRev = m_layer->geomRevision();
        m_builtLodKey = lod.contentKey();
        m_builtCoverage = cullRect;
    }

    applyTransform();
    if (statsOn)
        stats.logIfEnabled();
    syncStreaks();
    return root;
}
