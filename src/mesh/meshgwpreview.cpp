/**
 * @file      meshgwpreview.cpp
 * @brief     openswmm.engine/plans/INFILTRATION_TO_2D_AQUIFER_AND_REMAP_PLAN_2026-10-03.md Step 2
 *            — read-only preview, behind "Remap 1D↔2D", of where 1D losses land in the 2D aquifer.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Read-only 2D-aquifer mapping preview behind "Remap 1D↔2D" — see header.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/meshgwpreview.h"

#include "layers/meshspatialgrid.h"

#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh
{

namespace
{

/**
 * @brief Compute an axis-aligned extent from a borrowed planar ring.
 * @param[in] p Borrowed finite points in common planar units, unchanged.
 * @return Owned bbox, null QRectF for empty input. No finite/topology validation or padding;
 * zero-width/height extents remain degenerate. Linear read-only scan, no allocation or state
 * mutation.
 */
QRectF bounds(const Ring &p)
{
    double x0 = std::numeric_limits<double>::max(), x1 = -x0, y0 = x0, y1 = -x0;
    for (const QPointF &q : p) {
        x0 = std::min(x0, q.x());
        x1 = std::max(x1, q.x());
        y0 = std::min(y0, q.y());
        y1 = std::max(y1, q.y());
    }
    return p.isEmpty() ? QRectF() : QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

/**
 * @brief Compute ordinary double shoelace area over an implicitly closed ring.
 * @param[in] p Borrowed finite planar sequence, unchanged; closure duplicate optional.
 * @return Signed squared coordinate units, positive CCW/negative CW, zero for empty or
 * degenerate sequences. No local-origin shift, compensated sum or robust-topology/overflow
 * validation; read-only, allocation free.
 */
double signedArea(const Ring &p)
{
    double a = 0.0;
    for (int i = 0, j = p.size() - 1; i < p.size(); j = i++)
        a += p[j].x() * p[i].y() - p[i].x() * p[j].y();
    return 0.5 * a;
}

/**
 * @brief Normalize an owned ring copy by dropping one exact closing duplicate and reversing
 * negative area.
 * @param[in] cell Ring value copied/moved into local ownership, finite planar arithmetic
 * assumed.
 * @return Owned open CCW sequence when original signed area was negative; zero/NaN area is not
 * rejected or repaired. Caller input unchanged, no convexity guarantee, may detach/allocate
 * storage.
 *
 * @details \p cell made counter-clockwise, closing duplicate dropped.
 */
Ring ccw(Ring cell)
{
    if (cell.size() > 1 && cell.first() == cell.last())
        cell.removeLast();
    if (signedArea(cell) < 0.0)
        std::reverse(cell.begin(), cell.end());
    return cell;
}

// ≥ 0 inside the half-plane left of the CCW edge a→b.
/**
 * @brief Evaluate the signed cross product of an oriented edge and query offset.
 * @param[in] a Borrowed finite first edge endpoint, unchanged.
 * @param[in] b Borrowed finite second endpoint in the same planar units, unchanged.
 * @param[in] p Borrowed finite query point, unchanged.
 * @return Twice oriented area in squared coordinate units, >=0 for the left/on-edge half-plane
 * under representable arithmetic. No epsilon, exact predicate, finite/overflow checks,
 * allocation or mutation.
 */
double side(const QPointF &a, const QPointF &b, const QPointF &p)
{
    return (b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x());
}

/**
 * @brief Normalize a query bbox and pad it for a spatial grid that rejects zero-area rectangles.
 * @param[in] r Rectangle copied by value; finite representable coordinates expected.
 * @return Owned normalized rectangle expanded by 1e-9*max(1,abs(original left),abs(original
 * top)) on all sides, in the same coordinate units. Padding is scale based and is not a
 * geometric clipping tolerance or guaranteed representable at arbitrary magnitudes; no source
 * mutation or allocation.
 */
QRectF inflated(QRectF r)
{
    // MeshSpatialGrid rejects zero-area query rects (a vertical segment, a
    // point) — pad by a hair relative to the coordinates' magnitude.
    const double pad = 1e-9 * std::max({1.0, std::abs(r.left()), std::abs(r.top())});
    return r.normalized().adjusted(-pad, -pad, pad, pad);
}

} // namespace

double clippedArea(const Ring &poly, const Ring &cellIn)
{
    const Ring cell = ccw(cellIn);
    Ring out = poly;
    if (out.size() > 1 && out.first() == out.last())
        out.removeLast();
    for (int i = 0; i < cell.size() && !out.isEmpty(); ++i) {
        const QPointF &a = cell[i];
        const QPointF &b = cell[(i + 1) % cell.size()];
        const Ring in = out;
        out.clear();
        for (int k = 0; k < in.size(); ++k) {
            const QPointF &p = in[k];
            const QPointF &q = in[(k + 1) % in.size()];
            const double sp = side(a, b, p), sq = side(a, b, q);
            if (sp >= 0.0)
                out.append(p);
            if ((sp >= 0.0) != (sq >= 0.0)) {
                const double t = sp / (sp - sq);
                out.append(p + t * (q - p));
            }
        }
    }
    return out.size() < 3 ? 0.0 : std::abs(signedArea(out));
}

double clippedLength(const QPointF &a, const QPointF &b, const Ring &cellIn)
{
    // Cyrus–Beck, as the engine's CellLocator::lengthInside.
    const Ring cell = ccw(cellIn);
    double tLo = 0.0, tHi = 1.0;
    for (int i = 0; i < cell.size(); ++i) {
        const QPointF &v0 = cell[i];
        const QPointF &v1 = cell[(i + 1) % cell.size()];
        const double d0 = side(v0, v1, a), d1 = side(v0, v1, b);
        const double dd = d1 - d0;
        if (std::abs(dd) < 1e-300) {
            if (d0 < 0.0)
                return 0.0;
            continue;
        }
        const double t = -d0 / dd;
        if (dd < 0.0)
            tHi = std::min(tHi, t);
        else
            tLo = std::max(tLo, t);
        if (tLo >= tHi)
            return 0.0;
    }
    const QPointF d = b - a;
    return (tHi - tLo) * std::hypot(d.x(), d.y());
}

GwPreviewResult previewGroundwaterMapping(const MeshResult &mesh, const GwPreviewInput &in)
{
    GwPreviewResult r;

    // Cell polygons + bbox grid (invalid cells keep their index, never match).
    const int nv = mesh.vertices.size();
    QVector<Ring> cells;
    QVector<QRectF> boxes;
    cells.reserve(mesh.triangles.size());
    boxes.reserve(mesh.triangles.size());
    for (const MeshTriangle &t : mesh.triangles) {
        Ring poly;
        bool ok = true;
        for (int k = 0; k < t.vertexCount(); ++k) {
            const int v = t.vertex(k);
            if (v < 0 || v >= nv) {
                ok = false;
                break;
            }
            poly.append(mesh.vertices[v].xy);
        }
        cells.append(ok ? ccw(poly) : Ring());
        boxes.append(ok ? bounds(poly) : QRectF());
    }
    MeshSpatialGrid grid;
    grid.rebuild(boxes);

    auto inCell = [&](const QPointF &p) {
        for (const int c : grid.query(inflated(QRectF(p, p)))) {
            const Ring &cell = cells[c];
            bool inside = !cell.isEmpty();
            for (int i = 0; inside && i < cell.size(); ++i)
                inside = side(cell[i], cell[(i + 1) % cell.size()], p) >= 0.0;
            if (inside)
                return true;
        }
        return false;
    };

    for (const auto &n : in.nodes) {
        if (inCell(n.second))
            ++r.nodesInside;
        else
            r.nodesOutside.append(n.first);
    }

    for (const auto &cd : in.seepingConduits) {
        double total = 0.0, inside = 0.0;
        for (int k = 1; k < cd.path.size(); ++k) {
            const QPointF &a = cd.path[k - 1], &b = cd.path[k];
            const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
            if (len <= 0.0)
                continue;
            total += len;
            double segIn = 0.0;
            for (const int c : grid.query(inflated(QRectF(a, b))))
                if (!cells[c].isEmpty())
                    segIn += clippedLength(a, b, cells[c]);
            inside += std::min(segIn, len); // on-edge double coverage
        }
        r.conduitLengthTotal += total;
        r.conduitLengthInside += inside;
        if (inside > 0.0)
            ++r.conduitsInside;
        else
            r.conduitsOutside.append(cd.id);
    }

    for (const auto &sc : in.subcatchments) {
        if (sc.lumpedGw) {
            r.subcatchLumped.append(sc.id);
            continue;
        }
        if (sc.polygon.size() < 3) {
            r.subcatchNoPolygon.append(sc.id);
            continue;
        }
        const double total = std::abs(signedArea(sc.polygon));
        double inside = 0.0;
        for (const int c : grid.query(inflated(bounds(sc.polygon))))
            if (!cells[c].isEmpty())
                inside += clippedArea(sc.polygon, cells[c]);
        inside = std::min(inside, total);
        r.subcatchAreaTotal += total;
        r.subcatchAreaInside += inside;
        if (inside > 0.0)
            ++r.subcatchTo2D;
        else
            r.subcatchOutside.append(sc.id);
    }
    return r;
}

} // namespace mesh
