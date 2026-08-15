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
        if (m_revision == geomRevision
            && (!buildEdges || !m_edgeEndpoints.empty() || triCount == 0))
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
        m_triIndices.reserve(size_t(triCount) * 3);
        for (qint64 i = 0; i < triCount; ++i) {
            int v0 = -1, v1 = -1, v2 = -1;
            triAccessor(i, v0, v1, v2);
            if (v0 < 0 || v1 < 0 || v2 < 0
                || v0 >= nVerts || v1 >= nVerts || v2 >= nVerts)
                continue;   // drop invalid cells — indices stay valid
            m_triIndices.push_back(quint32(v0));
            m_triIndices.push_back(quint32(v1));
            m_triIndices.push_back(quint32(v2));
        }

        m_edgeEndpoints.clear();
        if (buildEdges) {
            // Undirected dedup via sorted (lo,hi) keys.
            std::vector<quint64> keys;
            keys.reserve(m_triIndices.size());
            for (size_t t = 0; t + 2 < m_triIndices.size(); t += 3) {
                const quint32 a = m_triIndices[t];
                const quint32 b = m_triIndices[t + 1];
                const quint32 c = m_triIndices[t + 2];
                auto push = [&keys](quint32 u, quint32 v) {
                    if (u == v) return;
                    const quint32 lo = std::min(u, v);
                    const quint32 hi = std::max(u, v);
                    keys.push_back((quint64(lo) << 32) | quint64(hi));
                };
                push(a, b);
                push(b, c);
                push(c, a);
            }
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
    std::vector<quint32> m_edgeEndpoints;
    quint64 m_revision = kNoRevision;
    double  m_anchorX  = 0.0;
    double  m_anchorY  = 0.0;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MESHSTATICGEOMETRYBUFFERS_H
