// GPL-3.0-or-later

/**
 * @file      flowparticleresidency.h
 * @brief     Resource residency tracking for flow particle rendering.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "render/flowparticles.h"
#include <QSet>
#include <algorithm>

namespace OpenSWMM::Render
{
struct FlowParticleResidency {
    std::vector<quint64> tiles;
    QVector<QRectF> rectangles;
    bool limited = false;
};
// Residency needs only tile geometry, never the source index or its mutable
// query scratch. Copying this description to the worker is constant-sized.
struct FlowParticleResidencyGrid {
    QRectF extent;
    double cellW = 0, cellH = 0;
    int cols = 0, rows = 0, maxTiles = 4096;
    /** @brief Construct an empty geometry descriptor with a 4,096-quarter-tile budget. */
    FlowParticleResidencyGrid() = default;
    /** @brief Copy spatial-grid geometry without sharing its mutable locator scratch.
     * @param grid Borrowed geometry source; its extent, cell dimensions and counts
     * are copied. maxTiles retains this type's default budget rather than reading one. */
    FlowParticleResidencyGrid(const MeshSpatialGrid &grid)
        : extent(grid.extent), cellW(grid.cellW), cellH(grid.cellH), cols(grid.cols),
          rows(grid.rows)
    {
    }
    /** @brief Check for nonpositive grid counts or cell dimensions.
     * @return True if any tested count/dimension is <=0. Extent validity, finiteness,
     * quarter-grid integer overflow and maxTiles are not validated here. */
    bool isEmpty() const { return cols <= 0 || rows <= 0 || cellW <= 0 || cellH <= 0; }
};
struct FlowParticleResidencySupport {
    FlowParticleResidency required, prefetch;
    FlowParticleResidencyGrid grid;
    quint64 geometry = 0, revision = 0;
    double clock = 0;
};

/** @brief Replace residency rectangles by merging consecutive quarter tiles within each row.
 * @param[in,out] out Sorted unique row-major tile IDs for this grid. IDs and limited
 * are retained; rectangles are replaced. Tiles must be valid for the grid.
 * @param grid Nonempty geometry descriptor with four-times counts fitting int.
 * Rectangles use the same scene units and Y direction as extent.
 * Only horizontal runs are merged, with no vertical coalescing or validation. */
inline void flowParticleResidencyRectangles(FlowParticleResidency &out,
                                            const FlowParticleResidencyGrid &grid)
{
    out.rectangles.clear();
    out.rectangles.reserve(out.tiles.size());
    const int cols = grid.cols * 4;
    const double dx = grid.cellW / 4, dy = grid.cellH / 4;
    for (size_t i = 0; i < out.tiles.size();) {
        const quint64 start = out.tiles[i];
        quint64 end = start;
        ++i;
        while (i < out.tiles.size() && out.tiles[i] == end + 1 &&
               out.tiles[i] / cols == start / cols)
            end = out.tiles[i++];
        out.rectangles.append({grid.extent.left() + (start % cols) * dx,
                               grid.extent.top() + (start / cols) * dy, (end - start + 1) * dx,
                               dy});
    }
}

// Quarter-sized source-index tiles give precise sparse support without a
// domain-sized locator. Heads prefetch two animation seconds; trails pin every
// retained segment, including segments whose intermediate samples were reduced.
/** @brief Compute sparse quarter-tile support for a view, moving heads and retained trails.
 * @tparam Particles Iterable of particles exposing stop, position, history and visibleAfterAge.
 * @param view Valid scene-space view rectangle, required even when includeView is false.
 * @param grid Grid geometry; positive finite cell sizes, valid finite extent,
 * nonnegative budget and four-times counts fitting int are caller requirements.
 * @param particles Borrowed particles read synchronously; no references are retained.
 * @param lookahead Animation seconds of linear head prediction, using the last two
 * history ages when separated by >1e-9. Supply finite values; negative extrapolation
 * is accepted. Stopped heads are excluded, but eligible history segments remain.
 * @param includeView Include the padded view in addition to particle support.
 * @param viewPadding Nonnegative scene-unit padding, or a negative value for half
 * the view's largest dimension. Head segments pad by one quarter tile; trails pad
 * by one eighth of that size plus the retained sample's nonnegative error bound.
 * @return Sorted unique quarter tiles and horizontally merged rectangles. Empty
 * for an empty grid or invalid view. A budget/segment-length limit returns limited
 * true with no published tiles/rectangles; callers must select a fallback instead
 * of treating that result as complete. A segment is included only when its older
 * sample age is strictly greater than visibleAfterAge. No particle state is changed. */
template <class Particles>
inline FlowParticleResidency
flowParticleResidencyForParticles(QRectF view, const FlowParticleResidencyGrid &grid,
                                  const Particles &particles, double lookahead = 2,
                                  bool includeView = true, double viewPadding = -1)
{
    FlowParticleResidency out;
    if (grid.isEmpty() || !view.isValid())
        return out;
    const double dx = grid.cellW / 4, dy = grid.cellH / 4;
    const int cols = grid.cols * 4, rows = grid.rows * 4;
    QSet<quint64> keys;
    const auto add = [&](QRectF rect) {
        rect = rect.intersected(grid.extent);
        if (!rect.isValid() || out.limited)
            return;
        const auto index = [](double value, double origin, double step, int n) {
            return int(std::clamp(std::floor((value - origin) / step), 0., double(n - 1)));
        };
        const int x0 = index(rect.left(), grid.extent.left(), dx, cols),
                  x1 = index(rect.right(), grid.extent.left(), dx, cols);
        const int y0 = index(rect.top(), grid.extent.top(), dy, rows),
                  y1 = index(rect.bottom(), grid.extent.top(), dy, rows);
        if (quint64(x1 - x0 + 1) * quint64(y1 - y0 + 1) > quint64(grid.maxTiles)) {
            out.limited = true;
            return;
        }
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                keys.insert(quint64(y) * quint64(cols) + quint64(x));
                if (keys.size() > grid.maxTiles) {
                    out.limited = true;
                    return;
                }
            }
    };
    const auto segment = [&](QPointF a, QPointF b, double padX, double padY) {
        const double length = std::max(std::abs(b.x() - a.x()) / dx, std::abs(b.y() - a.y()) / dy);
        if (!std::isfinite(length) || length > 2 * grid.maxTiles) {
            out.limited = true;
            return;
        }
        const int steps = std::max(1, int(std::ceil(length * 2)));
        for (int i = 0; i < steps && !out.limited; ++i) {
            const auto p = a + (b - a) * (double(i) / steps),
                       q = a + (b - a) * (double(i + 1) / steps);
            add(QRectF(p, q).normalized().adjusted(-padX, -padY, padX, padY));
        }
    };
    const double pad = viewPadding >= 0 ? viewPadding : std::max(view.width(), view.height()) * .5;
    if (includeView)
        add(view.adjusted(-pad, -pad, pad, pad));
    for (const auto &p : particles) {
        if (out.limited)
            break;
        if (p.stop == FlowParticleStop::None) {
            QPointF velocity;
            if (p.history.size() > 1) {
                const auto &a = p.history[p.history.size() - 2], &b = p.history.back();
                if (b.age - a.age > 1e-9)
                    velocity = (b.position - a.position) / (b.age - a.age);
            }
            const QPointF predicted(std::clamp(p.position.x() + lookahead * velocity.x(),
                                               grid.extent.left(), grid.extent.right()),
                                    std::clamp(p.position.y() + lookahead * velocity.y(),
                                               grid.extent.top(), grid.extent.bottom()));
            segment(p.position, predicted, dx, dy);
        }
        for (size_t i = 1; i < p.history.size(); ++i) {
            if (p.history[i - 1].age <= p.visibleAfterAge)
                continue;
            const double error = std::max(0., p.history[i].error);
            segment(p.history[i - 1].position, p.history[i].position, dx / 8 + error,
                    dy / 8 + error);
            if (out.limited)
                break;
        }
    }
    if (out.limited)
        return out;
    out.tiles.reserve(keys.size());
    for (quint64 key : keys)
        out.tiles.push_back(key);
    std::sort(out.tiles.begin(), out.tiles.end());
    flowParticleResidencyRectangles(out, grid);
    return out;
}
/** @brief Compute residency from an optional immutable particle-frame snapshot.
 * @param view Valid scene-space view rectangle, required even without a frame.
 * @param grid Geometry and budget satisfying flowParticleResidencyForParticles()
 * preconditions; this overload does not validate them further.
 * @param frame Borrowed frame, or null to compute only the optional view support.
 * @param lookahead Finite animation seconds of head prediction, default two.
 * @param includeView Include the padded view even when the frame is null.
 * @param viewPadding Scene-unit padding; a negative value selects half the larger
 * view dimension.
 * @return Residency as defined by flowParticleResidencyForParticles(), including
 * its limited-result fallback requirement; neither input is retained. */
inline FlowParticleResidency flowParticleResidency(QRectF view,
                                                   const FlowParticleResidencyGrid &grid,
                                                   const FlowParticleFrame *frame,
                                                   double lookahead = 2, bool includeView = true,
                                                   double viewPadding = -1)
{
    return frame ? flowParticleResidencyForParticles(view, grid, frame->particles, lookahead,
                                                     includeView, viewPadding)
                 : flowParticleResidencyForParticles(view, grid, FlowParticleSlots<FlowParticle>{},
                                                     lookahead, includeView, viewPadding);
}
/** @brief Union sorted quarter-tile support and rebuild its merged row rectangles.
 * @param[in,out] out Destination with sorted unique tile IDs for grid. If either
 * input is limited or the union exceeds the budget, only out.limited changes;
 * previous tiles/rectangles are retained and must not be treated as complete.
 * @param support Borrowed sorted unique support using the same grid; may alias out.
 * @param grid Nonempty shared geometry with nonnegative maxTiles and valid IDs.
 * Successful union retains the false limited state and replaces tiles/rectangles. */
inline void mergeFlowParticleResidency(FlowParticleResidency &out,
                                       const FlowParticleResidency &support,
                                       const FlowParticleResidencyGrid &grid)
{
    if (out.limited || support.limited) {
        out.limited = true;
        return;
    }
    std::vector<quint64> tiles;
    tiles.reserve(out.tiles.size() + support.tiles.size());
    std::set_union(out.tiles.begin(), out.tiles.end(), support.tiles.begin(), support.tiles.end(),
                   std::back_inserter(tiles));
    if (tiles.size() > size_t(grid.maxTiles)) {
        out.limited = true;
        return;
    }
    out.tiles = std::move(tiles);
    flowParticleResidencyRectangles(out, grid);
}
} // namespace OpenSWMM::Render
