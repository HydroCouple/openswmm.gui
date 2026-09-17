/*!
 * \file   meshstaticgeometrybuffers.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * ensureBuilt() is revision-keyed: calling it again with the same revision
 * is a no-op (returns false), which is the contract that makes pan/zoom
 * and data ticks free of static rebuilds. Locked by
 * tests/unit/test_meshstaticgeometrybuffers.cpp.
 *
 * Header-only, Qt-Core-only.
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
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    static constexpr quint64 kNoRevision = ~quint64(0);

    void clear()
    {
        m_positions.clear();
        m_triIndices.clear();
        m_triCell.clear();
        m_edgeEndpoints.clear();
        m_revision = kNoRevision;
        m_anchorX = m_anchorY = 0.0;
    }

    [[nodiscard]] bool isBuilt() const { return m_revision != kNoRevision; }
    [[nodiscard]] quint64 revision() const { return m_revision; }
    [[nodiscard]] double anchorX() const { return m_anchorX; }
    [[nodiscard]] double anchorY() const { return m_anchorY; }

    [[nodiscard]] const std::vector<Vec2>    &positions()  const { return m_positions; }
    [[nodiscard]] const std::vector<quint32> &triIndices() const { return m_triIndices; }
    /*! Mesh cell index of each emitted triangle (parallel to
     *  triIndices()/3). Identity for an all-triangle mesh built through
     *  ensureBuilt() (minus dropped cells); with ensureBuiltCells() a quad's
     *  two fan triangles both map to the quad's cell. */
    [[nodiscard]] const std::vector<quint32> &triCell()    const { return m_triCell; }
    /*! Unique undirected edges, 2 endpoint ids per edge, built only when
     *  ensureBuilt(..., buildEdges=true). */
    [[nodiscard]] const std::vector<quint32> &edgeEndpoints() const { return m_edgeEndpoints; }

    [[nodiscard]] qint64 vertexCount()   const { return qint64(m_positions.size()); }
    [[nodiscard]] qint64 triangleCount() const { return qint64(m_triIndices.size() / 3); }
    [[nodiscard]] qint64 edgeCount()     const { return qint64(m_edgeEndpoints.size() / 2); }

    /*!
     * Build (or reuse) the static buffers.
     *
     * \p geomRevision  the layer's geometry revision. When it matches the
     *                  revision the buffers were last built for, this is a
     *                  no-op and returns false — style / data / selection /
     *                  pan / zoom events therefore never rebuild.
     * \p anchorX/Y     scene-space anchor subtracted from every position.
     * \p vertices      shared scene-space vertex positions.
     * \p triCount      number of triangles.
     * \p triAccessor   callable `void(qint64 i, int &v0, int &v1, int &v2)`
     *                  filling the vertex ids of triangle \p i.
     * \p buildEdges    also derive the deduplicated undirected edge list.
     *
     * \return true when a rebuild happened.
     */
    template <typename TriAccessor>
    bool ensureBuilt(quint64 geomRevision,
                     double anchorX, double anchorY,
                     const QVector<QPointF> &vertices,
                     qint64 triCount,
                     TriAccessor &&triAccessor,
                     bool buildEdges = false)
    {
        // A triangle is a 3-vertex cell whose fan is itself.
        return ensureBuiltCells(
            geomRevision, anchorX, anchorY, vertices, triCount,
            [&](qint64 i, int poly[4], int &nv, int fan[2][3], int &nFan) {
                int v0 = -1, v1 = -1, v2 = -1;
                triAccessor(i, v0, v1, v2);
                poly[0] = v0; poly[1] = v1; poly[2] = v2; poly[3] = -1;
                nv = 3;
                fan[0][0] = v0; fan[0][1] = v1; fan[0][2] = v2;
                nFan = 1;
            },
            buildEdges);
    }

    /*!
     * Mixed triangle/quad form of ensureBuilt() (workplans/
     * TRI_QUAD_MESHING_PLAN_2026-09-06.md §5): every cell contributes its
     * sub-triangle FAN to triIndices() (one triangle, or a quad's two
     * mesh::cellGeom sub-triangles — each tagged with the cell in triCell())
     * while the deduplicated edge list is derived from the cell's TRUE
     * polygon boundary, so a quad's fan diagonal is never drawn as an edge.
     *
     * \p cellAccessor  callable
     *   `void(qint64 i, int poly[4], int &nv, int fan[2][3], int &nFan)`
     *   filling the cyclic polygon vertex ids (\p nv = 3 or 4) and the fan
     *   triangles (\p nFan = 1 or 2) of cell \p i.
     * Cells with any out-of-range polygon vertex id are dropped.
     */
    template <typename CellAccessor>
    bool ensureBuiltCells(quint64 geomRevision,
                          double anchorX, double anchorY,
                          const QVector<QPointF> &vertices,
                          qint64 cellCount,
                          CellAccessor &&cellAccessor,
                          bool buildEdges = false)
    {
        if (m_revision == geomRevision
            && (!buildEdges || !m_edgeEndpoints.empty() || cellCount == 0))
            return false;

        m_anchorX = anchorX;
        m_anchorY = anchorY;

        m_positions.clear();
        m_positions.reserve(size_t(vertices.size()));
        for (const QPointF &p : vertices)
            m_positions.push_back(Vec2{float(p.x() - anchorX),
                                       float(p.y() - anchorY)});

        const int nVerts = int(vertices.size());
        m_triIndices.clear();
        m_triIndices.reserve(size_t(cellCount) * 3);
        m_triCell.clear();
        m_triCell.reserve(size_t(cellCount));

        // Undirected dedup via sorted (lo,hi) keys — from the polygon
        // boundary, not the fan.
        std::vector<quint64> keys;
        if (buildEdges) keys.reserve(size_t(cellCount) * 3);
        auto pushKey = [&keys](int u, int v) {
            if (u == v) return;
            const quint32 lo = quint32(std::min(u, v));
            const quint32 hi = quint32(std::max(u, v));
            keys.push_back((quint64(lo) << 32) | quint64(hi));
        };

        for (qint64 i = 0; i < cellCount; ++i) {
            int poly[4] = {-1, -1, -1, -1};
            int fan[2][3] = {{-1, -1, -1}, {-1, -1, -1}};
            int nv = 0, nFan = 0;
            cellAccessor(i, poly, nv, fan, nFan);
            if (nv < 3 || nv > 4 || nFan < 1 || nFan > 2) continue;
            bool valid = true;
            for (int k = 0; k < nv; ++k)
                if (poly[k] < 0 || poly[k] >= nVerts) { valid = false; break; }
            if (!valid) continue;   // drop invalid cells — indices stay valid
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
    std::vector<Vec2>    m_positions;
    std::vector<quint32> m_triIndices;
    std::vector<quint32> m_triCell;
    std::vector<quint32> m_edgeEndpoints;
    quint64 m_revision = kNoRevision;
    double  m_anchorX  = 0.0;
    double  m_anchorY  = 0.0;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MESHSTATICGEOMETRYBUFFERS_H
