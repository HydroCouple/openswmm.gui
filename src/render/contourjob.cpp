/**
 * @file      contourjob.cpp
 * @brief     QSG-2D-1M Phase 7 — thread-safe contour job input/output for asynchronous
 *            marching-triangles recomputation.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 7 — see contourjob.h.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/contourjob.h"

#include <QPointF>

#include <cstddef>

namespace OpenSWMM::Render
{

namespace
{

/*! Forward range of triangle indices [0, n) — the opaque handles the
 *  marching templates iterate; the extractor resolves them against the
 *  snapshot arrays. */
struct IndexRange {
    struct It {
        size_t i;
        /** @brief Read the opaque triangle index without accessing snapshot storage.
         * @return Current zero-based index; dereferencing an end iterator is not range-checked. */
        size_t operator*() const { return i; }
        /** @brief Advance one index without bounds checks.
         * @return This iterator after increment; callers keep the index below size_t overflow. */
        It &operator++()
        {
            ++i;
            return *this;
        }
        /** @brief Compare only stored index values, with no owner/range identity.
         * @param o Other iterator to inspect without mutation.
         * @return True when the indices differ. */
        bool operator!=(const It &o) const { return i != o.i; }
    };
    size_t n = 0;
    /** @brief Start iteration at triangle zero.
     * @return Zero-valued iterator, equal to end() when n is zero. */
    [[nodiscard]] It begin() const { return It{0}; }
    /** @brief Read the exclusive upper index.
     * @return Iterator containing n, without allocating or referencing an array. */
    [[nodiscard]] It end() const { return It{n}; }
};

} // namespace

void computeContourJob(const ContourJobInput &in, ContourJobOutput &out)
{
    out.bands.clear();
    out.segs.clear();
    size_t packedChunks = 0;
    if (!in.positions || in.positions->empty() || !in.scalars) {
        out.packedBands.clear();
        return;
    }

    const auto &pos = *in.positions;
    const auto &scalars = *in.scalars;
    const size_t n = std::min(pos.size(), scalars.size());
    if (n == 0) {
        out.packedBands.clear();
        return;
    }

    const auto extract = [&pos, &scalars](size_t i, QPointF &p0, QPointF &p1, QPointF &p2,
                                          double &v0, double &v1, double &v2) {
        const ContourJobInput::TriPos &p = pos[i];
        const std::array<float, 3> &s = scalars[i];
        p0 = QPointF(p.ax, p.ay);
        p1 = QPointF(p.bx, p.by);
        p2 = QPointF(p.cx, p.cy);
        v0 = double(s[0]);
        v1 = double(s[1]);
        v2 = double(s[2]);
    };

    const auto march = [&](const auto &range) {
        if (in.bandLevels.size() >= 2) {
            if (in.packBands) {
                WaterGeometryBuilder<PackedBandVertex> builder(
                    [&](const auto &vertices, const auto &indices) {
                        if (indices.empty())
                            return;
                        if (packedChunks == out.packedBands.size())
                            out.packedBands.emplace_back();
                        auto &chunk = out.packedBands[packedChunks++];
                        chunk.vertices.assign(vertices.begin(), vertices.end());
                        chunk.indices.assign(indices.begin(), indices.end());
                    });
                builder.reserve(std::min(n, size_t(waterGeometryChunkLimit / 3)) * 3);
                OpenSWMM::Contour::forEachTriangleIsoband(
                    range, in.bandLevels, extract,
                    [&](const std::vector<QPointF> &points, double, double, int band) {
                        if (in.bandCoverage.isValid()) {
                            double left = points[0].x(), right = left, top = points[0].y(),
                                   bottom = top;
                            for (const auto &point : points) {
                                left = std::min(left, point.x());
                                right = std::max(right, point.x());
                                top = std::min(top, point.y());
                                bottom = std::max(bottom, point.y());
                            }
                            if (right < in.bandCoverage.left() || left > in.bandCoverage.right() ||
                                bottom < in.bandCoverage.top() || top > in.bandCoverage.bottom())
                                return;
                        }
                        for (size_t k = 1; k + 1 < points.size(); ++k)
                            for (size_t j : {size_t(0), k, k + 1})
                                builder.push_back(
                                    {float(points[j].x()), float(points[j].y()), quint32(band)});
                    },
                    in.minimumVisibleValue);
                builder.finish();
            } else
                out.bands = OpenSWMM::Contour::marchingTrianglesIsobands(
                    range, in.bandLevels, extract, in.minimumVisibleValue);
        }
        if (!in.isoLevels.empty())
            out.segs = OpenSWMM::Contour::marchingTriangles(range, in.isoLevels, extract);
    };
    if (in.triangleIndices) {
        std::vector<size_t> valid;
        valid.reserve(in.triangleIndices->size());
        for (size_t i : *in.triangleIndices)
            if (i < n)
                valid.push_back(i);
        march(valid);
    } else {
        march(IndexRange{n});
    }
    out.packedBands.resize(packedChunks);
}

ContourJobOutput computeContourJob(const ContourJobInput &in)
{
    ContourJobOutput out;
    computeContourJob(in, out);
    return out;
}

} // namespace OpenSWMM::Render
