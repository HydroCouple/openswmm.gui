/**
 * @file      meshstaticgeometrybuffers.h
 * @brief     Shared static geometry buffers for mesh rendering.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 5 — persistent static geometry buffers for the 2D
 * mesh / results QSG renderers.
 *
 * The historical QSG path re-expanded every triangle into three
 * ColoredPoint2D vertices on every content rebuild (pan-back, zoom, time
 * tick, style edit) — ~84 MB of CPU-side vertex assembly per rebuild on a
 * 1M-cell mesh. This builder assembles, ONCE per geometry revision:
 *
 *   - a shared, anchor-relative float position array (one entry per mesh
 *     vertex — shared corners are stored once),
 *   - a static triangle index array (3 × quint32 per cell; cells with
 *     out-of-range vertex ids are dropped),
 *   - optionally a deduplicated undirected edge endpoint array
 *     (2 × quint32 per unique edge).
 *
 * Positions are anchor-relative so they stay small in float precision and
 * are invariant under pan/zoom (the renderer's root transform carries the
 * view). Dynamic state (colors / scalars / selection overlays) lives in
 * separate per-pass arrays owned by the renderers — style, data, and
 * selection changes never touch these buffers.
 *
 * ensureBuilt() is revision-keyed: a matching revision normally reuses buffers,
 * but requesting edges rebuilds if the cached edge list is empty and the cell
 * count is nonzero, which is the contract that makes pan/zoom
 * and data ticks free of static rebuilds. Locked by
 * tests/unit/test_meshstaticgeometrybuffers.cpp.
 *
 * Header-only, Qt-Core-only.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_MESHSTATICGEOMETRYBUFFERS_H
#define OPENSWMM_RENDER_MESHSTATICGEOMETRYBUFFERS_H

#include <QPointF>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace OpenSWMM::Render
{

class MeshStaticGeometryBuffers
{
public:
    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    static constexpr quint64 kNoRevision = ~quint64(0);

    /** @brief Clear every buffer, reset the anchor to zero and mark the revision unbuilt.
     * Vector capacity may be retained; previously borrowed elements are invalidated. */
    void clear()
    {
        m_positions.clear();
        m_triIndices.clear();
        m_triCell.clear();
        m_edgeEndpoints.clear();
        m_revision = kNoRevision;
        m_anchorX = m_anchorY = 0.0;
    }

    /** @brief Test whether the stored revision differs from the unbuilt sentinel.
     * @return False initially/after clear(); the sentinel must not be used as a real revision. */
    [[nodiscard]] bool isBuilt() const { return m_revision != kNoRevision; }
    /** @brief Read the geometry revision assigned after the last successful rebuild.
     * @return Stored key, or kNoRevision before construction/after clear(). */
    [[nodiscard]] quint64 revision() const { return m_revision; }
    /** @brief Read the X scene-coordinate origin subtracted from stored float positions.
     * @return Last rebuild anchor in scene units, initially zero. */
    [[nodiscard]] double anchorX() const { return m_anchorX; }
    /** @brief Read the Y scene-coordinate origin subtracted from stored float positions.
     * @return Last rebuild anchor in scene units, initially zero. */
    [[nodiscard]] double anchorY() const { return m_anchorY; }

    /** @brief Borrow anchor-relative float coordinates for every input vertex.
     * @return Buffer owned by this object, replaced by clear()/rebuild. Unused vertices
     * are retained; coordinates are not checked for finiteness before conversion. */
    [[nodiscard]] const std::vector<Vec2> &positions() const { return m_positions; }
    /** @brief Borrow consecutive triples of vertex indices emitted by accepted cells.
     * @return Owned buffer view, replaced by clear()/rebuild. Each triple maps to one
     * triCell() entry; caller-provided fan indices are not independently validated. */
    [[nodiscard]] const std::vector<quint32> &triIndices() const { return m_triIndices; }
    /*! Mesh cell index of each emitted triangle (parallel to
     *  triIndices()/3). Identity for an all-triangle mesh built through
     *  ensureBuilt() (minus dropped cells); with ensureBuiltCells() a quad's
     *  two fan triangles both map to the quad's cell. */
    /** @return Borrowed source-cell index per emitted triangle, replaced by clear()/rebuild. */
    [[nodiscard]] const std::vector<quint32> &triCell() const { return m_triCell; }
    /*! Unique undirected edges, 2 endpoint ids per edge, built only when
     *  ensureBuilt(..., buildEdges=true). */
    /** @return Borrowed sorted endpoint pairs, replaced by clear()/rebuild. Each pair
     * is ordered low-id then high-id; repeated and self edges are omitted. Empty when
     * edge generation was disabled, or when no accepted boundary contributes an edge. */
    [[nodiscard]] const std::vector<quint32> &edgeEndpoints() const { return m_edgeEndpoints; }

    /** @brief Count retained input positions, including unreferenced ones.
     * @return Current position count, initially zero. */
    [[nodiscard]] qint64 vertexCount() const { return qint64(m_positions.size()); }
    /** @brief Count emitted fan triangles, including both triangles of accepted quads.
     * @return Current index count divided by three. */
    [[nodiscard]] qint64 triangleCount() const { return qint64(m_triIndices.size() / 3); }
    /** @brief Count deduplicated undirected polygon-boundary edges.
     * @return Endpoint count divided by two; zero when no edges have been generated. */
    [[nodiscard]] qint64 edgeCount() const { return qint64(m_edgeEndpoints.size() / 2); }

    /** @brief Build or reuse buffers by adapting every input triangle to a three-vertex cell.
     * @tparam TriAccessor Callable accepting (qint64 index, int &v0, int &v1, int &v2).
     * @param geomRevision Geometry key, distinct from kNoRevision. Bump it whenever
     * vertices, triangle topology or anchors change; those inputs are not compared.
     * @param anchorX X scene origin subtracted before float conversion.
     * @param anchorY Y scene origin subtracted before float conversion.
     * @param vertices Borrowed shared scene positions, read synchronously without Y inversion.
     * @param triCount Nonnegative triangle count fitting allocation and quint32 source indices.
     * @param triAccessor Borrowed callable invoked for each triangle only during a rebuild.
     * Fills three indices; any negative/out-of-range index drops that triangle.
     * @param buildEdges Also derive sorted unique undirected triangle-boundary edges.
     * @return True after rebuilding, false for a matching revision unless edges are
     * requested while the cache has none and triCount is nonzero. See ensureBuiltCells()
     * for exception, ownership and revision-reuse limitations. */
    template <typename TriAccessor>
    bool ensureBuilt(quint64 geomRevision, double anchorX, double anchorY,
                     const QVector<QPointF> &vertices, qint64 triCount, TriAccessor &&triAccessor,
                     bool buildEdges = false)
    {
        // A triangle is a 3-vertex cell whose fan is itself.
        return ensureBuiltCells(
            geomRevision, anchorX, anchorY, vertices, triCount,
            [&](qint64 i, int poly[4], int &nv, int fan[2][3], int &nFan) {
                int v0 = -1, v1 = -1, v2 = -1;
                triAccessor(i, v0, v1, v2);
                poly[0] = v0;
                poly[1] = v1;
                poly[2] = v2;
                poly[3] = -1;
                nv = 3;
                fan[0][0] = v0;
                fan[0][1] = v1;
                fan[0][2] = v2;
                nFan = 1;
            },
            buildEdges);
    }

    /** @brief Build shared positions, fan triangles and optional polygon-boundary edges.
     * @tparam CellAccessor Callable accepting (qint64 i, int poly[4], int &nv,
     * int fan[2][3], int &nFan), filling bounded polygon and fan arrays.
     * @param geomRevision Geometry key, distinct from kNoRevision. Equal keys reuse
     * buffers without examining any other input, except an empty edge cache triggers
     * a rebuild when buildEdges is true and cellCount is nonzero. A mesh with no
     * accepted edges can therefore rebuild repeatedly under the same key.
     * @param anchorX X scene origin subtracted before float position conversion.
     * @param anchorY Y scene origin subtracted before float position conversion.
     * @param vertices Borrowed positions copied synchronously, including unused vertices.
     * Supply finite coordinates and anchors representable at the required precision.
     * @param cellCount Nonnegative cell count fitting allocations and quint32 cell indices.
     * @param cellAccessor Borrowed callable invoked once per cell on rebuild; write
     * only within poly[4]/fan[2][3]. nv must be 3 or 4 and nFan 1 or 2; other counts
     * drop the cell. Every polygon index is range-checked, but fan indices are copied
     * unchecked and must reference valid vertices. Degenerate cells are not rejected.
     * @param buildEdges Generate sorted unique boundary edges, excluding fan diagonals
     * and self edges. False clears any previously generated edge list during rebuild.
     * @return True after rebuilding; false for cache reuse under the condition above.
     * All access is externally serialized. Allocation/accessor exceptions propagate
     * with partially changed buffers/anchors and the previous revision; no rollback
     * is provided. Successful rebuilds replace borrowed buffer elements. */
    template <typename CellAccessor>
    bool ensureBuiltCells(quint64 geomRevision, double anchorX, double anchorY,
                          const QVector<QPointF> &vertices, qint64 cellCount,
                          CellAccessor &&cellAccessor, bool buildEdges = false)
    {
        if (m_revision == geomRevision &&
            (!buildEdges || !m_edgeEndpoints.empty() || cellCount == 0))
            return false;

        m_anchorX = anchorX;
        m_anchorY = anchorY;

        m_positions.clear();
        m_positions.reserve(size_t(vertices.size()));
        for (const QPointF &p : vertices)
            m_positions.push_back(Vec2{float(p.x() - anchorX), float(p.y() - anchorY)});

        const int nVerts = int(vertices.size());
        m_triIndices.clear();
        m_triIndices.reserve(size_t(cellCount) * 3);
        m_triCell.clear();
        m_triCell.reserve(size_t(cellCount));

        // Undirected dedup via sorted (lo,hi) keys — from the polygon
        // boundary, not the fan.
        std::vector<quint64> keys;
        if (buildEdges)
            keys.reserve(size_t(cellCount) * 3);
        auto pushKey = [&keys](int u, int v) {
            if (u == v)
                return;
            const quint32 lo = quint32(std::min(u, v));
            const quint32 hi = quint32(std::max(u, v));
            keys.push_back((quint64(lo) << 32) | quint64(hi));
        };

        for (qint64 i = 0; i < cellCount; ++i) {
            int poly[4] = {-1, -1, -1, -1};
            int fan[2][3] = {{-1, -1, -1}, {-1, -1, -1}};
            int nv = 0, nFan = 0;
            cellAccessor(i, poly, nv, fan, nFan);
            if (nv < 3 || nv > 4 || nFan < 1 || nFan > 2)
                continue;
            bool valid = true;
            for (int k = 0; k < nv; ++k)
                if (poly[k] < 0 || poly[k] >= nVerts) {
                    valid = false;
                    break;
                }
            if (!valid)
                continue; // drop invalid cells — indices stay valid
            for (int s = 0; s < nFan; ++s) {
                m_triIndices.push_back(quint32(fan[s][0]));
                m_triIndices.push_back(quint32(fan[s][1]));
                m_triIndices.push_back(quint32(fan[s][2]));
                m_triCell.push_back(quint32(i));
            }
            if (buildEdges)
                for (int k = 0; k < nv; ++k)
                    pushKey(poly[k], poly[(k + 1) % nv]);
        }

        m_edgeEndpoints.clear();
        if (buildEdges) {
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            m_edgeEndpoints.reserve(keys.size() * 2);
            for (quint64 k : keys) {
                m_edgeEndpoints.push_back(quint32(k >> 32));
                m_edgeEndpoints.push_back(quint32(k & 0xffffffffu));
            }
        }

        m_revision = geomRevision;
        return true;
    }

private:
    std::vector<Vec2> m_positions;
    std::vector<quint32> m_triIndices;
    std::vector<quint32> m_triCell;
    std::vector<quint32> m_edgeEndpoints;
    quint64 m_revision = kNoRevision;
    double m_anchorX = 0.0;
    double m_anchorY = 0.0;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MESHSTATICGEOMETRYBUFFERS_H
