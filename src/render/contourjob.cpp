/*!
 * \file   contourjob.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 7 — see contourjob.h.
 */
#include "render/contourjob.h"

#include <QPointF>

#include <cstddef>

namespace OpenSWMM::Render
{

namespace {

/*! Forward range of triangle indices [0, n) — the opaque handles the
 *  marching templates iterate; the extractor resolves them against the
 *  snapshot arrays. */
struct IndexRange
{
    struct It
    {
        size_t i;
        size_t operator*() const { return i; }
        It &operator++() { ++i; return *this; }
        bool operator!=(const It &o) const { return i != o.i; }
    };
    size_t n = 0;
    [[nodiscard]] It begin() const { return It{0}; }
    [[nodiscard]] It end()   const { return It{n}; }
};

} // namespace

ContourJobOutput computeContourJob(const ContourJobInput &in)
{
    ContourJobOutput out;
    if (!in.positions || in.positions->empty() || !in.scalars) return out;

    const auto &pos     = *in.positions;
    const auto &scalars = *in.scalars;
    const size_t n = std::min(pos.size(), scalars.size());
    if (n == 0) return out;

    const auto extract = [&pos, &scalars](size_t i,
                                          QPointF &p0, QPointF &p1, QPointF &p2,
                                          double &v0, double &v1, double &v2) {
        const ContourJobInput::TriPos &p = pos[i];
        const std::array<float, 3>    &s = scalars[i];
        p0 = QPointF(p.ax, p.ay);
        p1 = QPointF(p.bx, p.by);
        p2 = QPointF(p.cx, p.cy);
        v0 = double(s[0]);
        v1 = double(s[1]);
        v2 = double(s[2]);
    };

    const IndexRange range{n};
    if (in.bandLevels.size() >= 2)
        out.bands = OpenSWMM::Contour::marchingTrianglesIsobands(
            range, in.bandLevels, extract, in.clampUniformOutsideRange);
    if (!in.isoLevels.empty())
        out.segs = OpenSWMM::Contour::marchingTriangles(
            range, in.isoLevels, extract);
    return out;
}

} // namespace OpenSWMM::Render
