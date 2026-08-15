/*!
 * \file   meshrenderchunkindex.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 4 — see meshrenderchunkindex.h.
 */
#include "render/meshrenderchunkindex.h"

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

namespace {

QRectF unionOf(const QRectF &a, const QRectF &b)
{
    return a.isNull() ? b : a.united(b);
}

bool usableBBox(const QRectF &r)
{
    // Zero-size bboxes (degenerate cells) are still locatable by centre;
    // only negative-size / non-finite / null rects are unusable.
    return !r.isNull()
        && r.width() >= 0.0 && r.height() >= 0.0
        && std::isfinite(r.left()) && std::isfinite(r.top())
        && std::isfinite(r.right()) && std::isfinite(r.bottom());
}

} // namespace

void MeshRenderChunkIndex::clear()
{
    m_chunks.clear();
}

void MeshRenderChunkIndex::build(const QVector<QRectF> &cellBBoxes,
                                 const QVector<QRectF> &edgeBBoxes,
                                 int targetCellsPerChunk)
{
    m_chunks.clear();

    // Overall extent from every usable bbox.
    QRectF extent;
    for (const QRectF &r : cellBBoxes)
        if (usableBBox(r)) extent = unionOf(extent, r);
    for (const QRectF &r : edgeBBoxes)
        if (usableBBox(r)) extent = unionOf(extent, r);
    if (extent.isNull()) return;   // empty mesh

    const qint64 n = std::max<qint64>(cellBBoxes.size(), edgeBBoxes.size());
    const int    target = std::max(1, targetCellsPerChunk);
    const qint64 wantedChunks =
        std::max<qint64>(1, (n + target - 1) / target);
    const int side = std::max(1, int(std::ceil(std::sqrt(double(wantedChunks)))));
    const int cols = side;
    const int rows = side;

    const double cw = extent.width()  > 0.0 ? extent.width()  / cols : 1.0;
    const double ch = extent.height() > 0.0 ? extent.height() / rows : 1.0;

    // Grid cell of a bbox centre. Clamped so borderline centres (on the
    // extent edge) land in the outermost row/column.
    auto gridCellOf = [&](const QRectF &r) -> int {
        const double cx = r.left() + r.width()  * 0.5;
        const double cy = r.top()  + r.height() * 0.5;
        const int gx = std::clamp(int(std::floor((cx - extent.left()) / cw)), 0, cols - 1);
        const int gy = std::clamp(int(std::floor((cy - extent.top())  / ch)), 0, rows - 1);
        return gy * cols + gx;
    };

    struct Bucket { QVector<int> cells, edges; QRectF bbox; };
    QVector<Bucket> buckets(cols * rows);

    for (int i = 0; i < cellBBoxes.size(); ++i) {
        const QRectF &r = cellBBoxes[i];
        if (!usableBBox(r)) continue;
        Bucket &b = buckets[gridCellOf(r)];
        b.cells.append(i);
        b.bbox = unionOf(b.bbox, r);
    }
    for (int i = 0; i < edgeBBoxes.size(); ++i) {
        const QRectF &r = edgeBBoxes[i];
        if (!usableBBox(r)) continue;
        Bucket &b = buckets[gridCellOf(r)];
        b.edges.append(i);
        b.bbox = unionOf(b.bbox, r);
    }

    // Compact occupied buckets into dense, deterministic chunk ids
    // (row-major sweep order).
    for (Bucket &b : buckets) {
        if (b.cells.isEmpty() && b.edges.isEmpty()) continue;
        Chunk c;
        c.id      = int(m_chunks.size());
        c.bbox    = b.bbox;
        c.cellIds = std::move(b.cells);
        c.edgeIds = std::move(b.edges);
        m_chunks.append(std::move(c));
    }
}

MeshRenderChunkIndex::QueryResult
MeshRenderChunkIndex::query(const QRectF &rect) const
{
    QueryResult out;
    if (m_chunks.isEmpty() || rect.isEmpty()) return out;

    for (const Chunk &c : m_chunks) {
        if (!rect.intersects(c.bbox)) continue;
        if (rect.contains(c.bbox)) out.fullyInside.append(c.id);
        else                       out.boundary.append(c.id);
    }
    return out;
}

} // namespace OpenSWMM::Render
