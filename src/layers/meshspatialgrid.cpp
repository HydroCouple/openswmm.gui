/*!
 * \file   meshspatialgrid.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See meshspatialgrid.h for the storage layout and threading contract.
 */
#include "layers/meshspatialgrid.h"

#include <algorithm>
#include <cmath>

namespace {

// A rect the grid can index: finite, non-negative extents. Deliberately
// accepts zero-area rects (axis-aligned edges, point queries) that
// QRectF::isValid()/isEmpty() would reject.
inline bool usable(const QRectF &b)
{
    return b.width() >= 0.0 && b.height() >= 0.0
        && std::isfinite(b.left()) && std::isfinite(b.top())
        && std::isfinite(b.right()) && std::isfinite(b.bottom());
}

} // namespace

// ---------------------------------------------------------------------------
// rebuild — sizes the grid from the bbox set's united extent, then walks
// the bbox list twice to build the CSR occupancy table.
//
// Pass 1 counts memberships per cell (stored at cellOffsets[k+1] so the
// prefix-sum can run in place).
// Pass 2 places each bbox index into every cell it overlaps, using a
// scratch write-position vector so insertions land contiguously per cell
// and preserve insertion order within a cell.
// ---------------------------------------------------------------------------

void MeshSpatialGrid::rebuild(const QVector<QRectF> &bboxes)
{
    clear();
    if (bboxes.isEmpty()) return;

    // ── Pick a uniform cell size from the median bbox diagonal ────────
    // 16x the median diagonal keeps most bboxes inside a single cell while
    // still giving the index meaningful resolution. Pathologically skewed
    // bbox distributions (one giant box + many tiny ones) get clamped
    // later by the (cols * rows) cap.
    //
    // Degenerate (zero-width or zero-height) bboxes are LEGITIMATE members:
    // an axis-aligned mesh edge — the common case on the outer boundary of
    // a generated rectangular mesh — has a zero-area bbox. QRectF::isValid()
    // rejects those, and QRectF::united() ignores empty rects, so this
    // function must not use either: doing so silently dropped every
    // axis-aligned edge from the index (unrenderable and unpickable).
    bool seeded = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    QVector<double> diagonals;
    diagonals.reserve(bboxes.size());
    for (const QRectF &b : bboxes) {
        if (!usable(b)) continue;
        if (!seeded) {
            minX = b.left();  maxX = b.right();
            minY = b.top();   maxY = b.bottom();
            seeded = true;
        } else {
            minX = std::min(minX, b.left());
            maxX = std::max(maxX, b.right());
            minY = std::min(minY, b.top());
            maxY = std::max(maxY, b.bottom());
        }
        const double diag = std::hypot(b.width(), b.height());
        if (diag > 0.0) diagonals.append(diag);
    }
    if (!seeded || diagonals.isEmpty()) return;
    extent = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));

    std::nth_element(diagonals.begin(),
                     diagonals.begin() + diagonals.size() / 2,
                     diagonals.end());
    const double median   = diagonals[diagonals.size() / 2];
    const double cellSize = std::max(median * 16.0, 1e-6);

    cellW = cellSize;
    cellH = cellSize;
    cols  = std::max(1, int(std::ceil(extent.width()  / cellW)));
    rows  = std::max(1, int(std::ceil(extent.height() / cellH)));

    // Hard cap on grid resolution — without this a degenerate bbox set
    // (e.g. one huge box + many tiny ones) blows past 1024 × 1024 cells
    // and balloons the offsets vector.
    if (qint64(cols) * rows > qint64(1024) * 1024) {
        const double scale = std::sqrt(double(cols) * rows / (1024.0 * 1024.0));
        cellW *= scale;
        cellH *= scale;
        cols   = std::max(1, int(std::ceil(extent.width()  / cellW)));
        rows   = std::max(1, int(std::ceil(extent.height() / cellH)));
    }

    const int nCells = cols * rows;

    // ── Pass 1: count memberships per cell ────────────────────────────
    // Write the count for cell k into cellOffsets[k + 1]; the prefix-sum
    // below converts the count array in place to exclusive offsets.
    cellOffsets.assign(nCells + 1, 0);
    auto cellRange = [&](const QRectF &b,
                         int &cx0, int &cx1, int &cy0, int &cy1) {
        cx0 = std::clamp(int(std::floor((b.left()   - extent.left()) / cellW)), 0, cols - 1);
        cx1 = std::clamp(int(std::floor((b.right()  - extent.left()) / cellW)), 0, cols - 1);
        cy0 = std::clamp(int(std::floor((b.top()    - extent.top())  / cellH)), 0, rows - 1);
        cy1 = std::clamp(int(std::floor((b.bottom() - extent.top())  / cellH)), 0, rows - 1);
    };

    for (const QRectF &b : bboxes) {
        if (!usable(b)) continue;
        int cx0, cx1, cy0, cy1;
        cellRange(b, cx0, cx1, cy0, cy1);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx)
                ++cellOffsets[cy * cols + cx + 1];
    }

    // ── Prefix-sum: counts → exclusive offsets ────────────────────────
    for (int k = 1; k <= nCells; ++k)
        cellOffsets[k] += cellOffsets[k - 1];
    const int totalMemberships = cellOffsets[nCells];

    // ── Pass 2: fill cellIndices using a scratch write head per cell ──
    // Initialise writePos[k] = cellOffsets[k], then post-increment per
    // insertion. Insertion order within a cell is preserved (smaller bbox
    // indices appear before larger ones), which makes the test fixtures
    // deterministic.
    cellIndices.resize(totalMemberships);
    QVector<int> writePos = cellOffsets;
    writePos.resize(nCells);    // drop the sentinel — only need starts

    for (int i = 0; i < bboxes.size(); ++i) {
        const QRectF &b = bboxes[i];
        if (!usable(b)) continue;
        int cx0, cx1, cy0, cy1;
        cellRange(b, cx0, cx1, cy0, cy1);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx) {
                const int k = cy * cols + cx;
                cellIndices[writePos[k]++] = i;
            }
    }

    // ── query() scratch state ─────────────────────────────────────────
    // `seen` is sized to the input vector so seen[idx] for any valid idx
    // is in bounds. `epoch` starts at 0; the first query() bumps to 1.
    seen.assign(size_t(bboxes.size()), 0);
    epoch = 0;

    // Compute the average density across non-empty cells so query() can
    // reserve a reasonable output buffer up front.
    int nonEmpty = 0;
    for (int k = 0; k < nCells; ++k)
        if (cellOffsets[k + 1] > cellOffsets[k]) ++nonEmpty;
    avgPerCell = (nonEmpty > 0)
                     ? int((qint64(totalMemberships) + nonEmpty - 1) / nonEmpty)
                     : 0;
}

// ---------------------------------------------------------------------------
// query — single-pass sweep over the touched cell range, with epoch-stamped
// dedup so a bbox spanning multiple cells is only reported once.
// ---------------------------------------------------------------------------

QVector<int> MeshSpatialGrid::query(const QRectF &rect) const
{
    QVector<int> out;
    if (cellOffsets.isEmpty() || !usable(rect))
        return out;

    // Degenerate (zero-area) query rects are legitimate — a point pick or a
    // query along an axis-aligned edge. QRectF::intersected()/isEmpty()
    // discard them, so reject fully-disjoint rects manually and let the
    // clamps below bound the cell walk.
    if (rect.right()  < extent.left() || rect.left() > extent.right()
        || rect.bottom() < extent.top() || rect.top() > extent.bottom())
        return out;

    const int cx0 = std::clamp(int(std::floor((rect.left()   - extent.left()) / cellW)), 0, cols - 1);
    const int cx1 = std::clamp(int(std::floor((rect.right()  - extent.left()) / cellW)), 0, cols - 1);
    const int cy0 = std::clamp(int(std::floor((rect.top()    - extent.top())  / cellH)), 0, rows - 1);
    const int cy1 = std::clamp(int(std::floor((rect.bottom() - extent.top())  / cellH)), 0, rows - 1);

    // Bump the epoch — any entry in `seen` not equal to the new value is
    // "not yet visited by this query". O(1) reset. On wrap (every ~4B
    // queries — years of continuous rendering) a single fill is needed.
    ++epoch;
    if (epoch == 0) {
        std::fill(seen.begin(), seen.end(), 0u);
        epoch = 1;
    }

    // Reserve based on touched cells × average density. The 64-floor
    // preserves the historic baseline for tiny visible sets; the
    // seen.size() ceiling caps any pathological estimate at the
    // theoretical maximum.
    const qint64 touchedCells = qint64(cx1 - cx0 + 1) * qint64(cy1 - cy0 + 1);
    const qint64 estimate     = touchedCells * qint64(avgPerCell);
    out.reserve(int(std::min<qint64>(std::max<qint64>(64, estimate),
                                     qint64(seen.size()))));

    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            const int   k     = cy * cols + cx;
            const int   begin = cellOffsets[k];
            const int   end   = cellOffsets[k + 1];
            const int  *data  = cellIndices.constData();
            for (int p = begin; p < end; ++p) {
                const int idx = data[p];
                if (seen[idx] != epoch) {
                    seen[idx] = epoch;
                    out.append(idx);
                }
            }
        }
    }
    return out;
}
