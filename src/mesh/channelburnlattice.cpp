/*!
 * \file   channelburnlattice.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The channel corridor as mesh input (CHANNEL_BURN_IN_PLAN_2026-09-21.md
 * §4.6, §4.7, §16.2 — phases P2 and P3).
 */
#include "mesh/channelburnlattice.h"

#include "mesh/meshcellgeom.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

/*! Point at chainage \p t along a polyline with cumulative \p chain. */
QPointF pointAtChainage(const QVector<QPointF> &path, const QVector<double> &chain, double t)
{
    const int n = path.size();
    if (n == 0) return {};
    if (n == 1 || t <= chain.first()) return path.first();
    if (t >= chain.last()) return path.last();

    const auto it = std::upper_bound(chain.cbegin(), chain.cend(), t);
    const int hi = int(it - chain.cbegin());
    const int lo = hi - 1;
    const double d = chain[hi] - chain[lo];
    const double f = (d > 0.0) ? (t - chain[lo]) / d : 0.0;
    return path[lo] + (path[hi] - path[lo]) * f;
}

/*! Unit normal pointing RIGHT of the downstream direction at chainage \p t.
 *
 *  Taken from a central difference half a step either side rather than from the
 *  containing segment: at a bend vertex the segment normal jumps, and offset
 *  rows built on it fold. The averaged direction is the same thing a mitre
 *  joint approximates. */
QPointF rightNormalAt(const QVector<QPointF> &path, const QVector<double> &chain,
                      double t, double h)
{
    const double L = chain.isEmpty() ? 0.0 : chain.last();
    const double a = std::max(0.0, t - h);
    const double b = std::min(L,   t + h);
    QPointF p0 = pointAtChainage(path, chain, a);
    QPointF p1 = pointAtChainage(path, chain, b);
    double dx = p1.x() - p0.x(), dy = p1.y() - p0.y();
    double len = std::hypot(dx, dy);
    if (!(len > 0.0))
    {
        dx = path.last().x() - path.first().x();
        dy = path.last().y() - path.first().y();
        len = std::hypot(dx, dy);
        if (!(len > 0.0)) return QPointF(0.0, -1.0);
    }
    return QPointF(dy / len, -dx / len);   // right of (dx, dy)
}

/*! Manning's n and the sub-region suffix for a cell centred at offset \p m. */
void roughnessFor(const NormalizedSection &s, double m, double *n, QString *suffix)
{
    if (std::isfinite(s.leftBank) && m < s.leftBank)
    { *n = s.nLeft;  *suffix = QStringLiteral(":left");  return; }
    if (std::isfinite(s.rightBank) && m > s.rightBank)
    { *n = s.nRight; *suffix = QStringLiteral(":right"); return; }
    *n = s.nChannel; *suffix = QStringLiteral(":chan");
}

} // namespace

BurnLattice buildCorridorLattice(const BurnProfile &p,
                                 double alongStep, double minCellSize,
                                 QStringList *warnings, QString *err)
{
    BurnLattice lat;
    if (!p.isValid())
    {
        if (err) *err = QStringLiteral("conduit '%1': profile is not valid").arg(p.conduitId);
        return lat;
    }
    lat.conduitId = p.conduitId;
    lat.offsets   = p.offsets;
    lat.nAcross   = int(p.offsets.size());
    if (lat.nAcross < 2)
    {
        if (err) *err = QStringLiteral("conduit '%1': corridor has fewer than 2 offsets")
                            .arg(p.conduitId);
        return lat;
    }

    // Along stations: uniform, both ends exact.
    const double L = p.length();
    if (alongStep > 0.0)
    {
        const int n = std::max(1, int(std::llround(L / alongStep)));
        lat.chainage.reserve(n + 1);
        for (int i = 0; i <= n; ++i) lat.chainage.append(L * double(i) / double(n));
    }
    else
    {
        lat.chainage = p.chainage;
    }
    lat.nAlong = int(lat.chainage.size());
    if (lat.nAlong < 2)
    {
        if (err) *err = QStringLiteral("conduit '%1': corridor has fewer than 2 stations")
                            .arg(p.conduitId);
        return lat;
    }

    const double h = (lat.nAlong > 1) ? 0.5 * (L / double(lat.nAlong - 1)) : L;
    lat.xy.reserve(qsizetype(lat.nAlong) * lat.nAcross);
    lat.z .reserve(qsizetype(lat.nAlong) * lat.nAcross);

    for (int i = 0; i < lat.nAlong; ++i)
    {
        const double  t = lat.chainage[i];
        const QPointF c = pointAtChainage(p.centerline, p.chainage, t);
        const QPointF nrm = rightNormalAt(p.centerline, p.chainage, t, h);
        for (int k = 0; k < lat.nAcross; ++k)
        {
            const double s = lat.offsets[k];
            lat.xy.append(c + nrm * s);
            bool inExtent = false;
            double zv = sectionZAt(p, t, s, &inExtent);
            if (!std::isfinite(zv))
            {
                // Only reachable at a clipped extent's own endpoint through
                // rounding; the bed is always defined, so fall back to it.
                zv = bedZAt(p, t);
            }
            lat.z.append(zv);
        }
    }

    // Spacings, for the densification guard (§4.6).
    lat.minAlongSpacing = std::numeric_limits<double>::infinity();
    for (int i = 1; i < lat.nAlong; ++i)
        lat.minAlongSpacing = std::min(lat.minAlongSpacing, lat.chainage[i] - lat.chainage[i - 1]);
    lat.minAcrossSpacing = std::numeric_limits<double>::infinity();
    for (int k = 1; k < lat.nAcross; ++k)
        lat.minAcrossSpacing = std::min(lat.minAcrossSpacing, lat.offsets[k] - lat.offsets[k - 1]);

    if (minCellSize > 0.0 && warnings)
    {
        const double worst = std::min(lat.minAlongSpacing, lat.minAcrossSpacing);
        if (worst < minCellSize)
            warnings->append(
                QStringLiteral("conduit '%1': corridor lattice spacing %2 is below the mesh "
                               "minimum cell size %3 — the cleanup pass will collapse cells "
                               "inside the channel")
                    .arg(p.conduitId).arg(worst).arg(minCellSize));
    }
    return lat;
}

namespace {

PatchMesh corridorPatchGeometry(const BurnLattice &lat, QString *err)
{
    PatchMesh pm;
    if (!lat.isValid())
    {
        if (err) *err = QStringLiteral("conduit '%1': lattice is not valid").arg(lat.conduitId);
        return pm;
    }

    pm.tag = QStringLiteral("channel:%1").arg(lat.conduitId);
    pm.xy  = lat.xy;

    // Cells. The lattice is conformal, so each (i, k) cell is simply its four
    // lattice corners; winding is fixed per cell rather than assumed, because a
    // reversed centreline would otherwise emit the whole patch clockwise.
    QVector<MeshVertex> verts;
    verts.reserve(pm.xy.size());
    for (const QPointF &p : pm.xy) { MeshVertex v; v.xy = p; verts.append(v); }

    pm.quads.reserve(qsizetype(lat.nAlong - 1) * (lat.nAcross - 1));
    for (int i = 0; i + 1 < lat.nAlong; ++i)
    {
        for (int k = 0; k + 1 < lat.nAcross; ++k)
        {
            MeshTriangle q;
            q.v0 = lat.at(i,     k);
            q.v1 = lat.at(i,     k + 1);
            q.v2 = lat.at(i + 1, k + 1);
            q.v3 = lat.at(i + 1, k);
            if (cellSignedArea(verts, q) < 0.0) std::swap(q.v1, q.v3);
            pm.quads.append(q);
        }
    }

    const QString bad = validate(pm);
    if (!bad.isEmpty())
    {
        if (err) *err = QStringLiteral("conduit '%1': %2 (the corridor folds — smooth the "
                                       "centreline or reduce the corridor width)")
                            .arg(lat.conduitId, bad);
        return PatchMesh();
    }

    // Boundary loop: down the left edge, across the tail, up the right edge,
    // back across the head — the ring Triangle will hold as constraints.
    auto pushSeg = [&pm](int a, int b) { if (a != b) pm.boundarySegments.append(qMakePair(a, b)); };
    for (int i = 0; i + 1 < lat.nAlong;  ++i) pushSeg(lat.at(i, 0), lat.at(i + 1, 0));
    for (int k = 0; k + 1 < lat.nAcross; ++k)
        pushSeg(lat.at(lat.nAlong - 1, k), lat.at(lat.nAlong - 1, k + 1));
    for (int i = lat.nAlong - 1; i > 0; --i)
        pushSeg(lat.at(i, lat.nAcross - 1), lat.at(i - 1, lat.nAcross - 1));
    for (int k = lat.nAcross - 1; k > 0; --k) pushSeg(lat.at(0, k), lat.at(0, k - 1));

    return pm;
}

} // namespace

PatchMesh corridorPatch(const BurnLattice &lat, const BurnProfile &p, const BurnOptions &opt,
                        QString *err)
{
    PatchMesh pm = corridorPatchGeometry(lat, err);
    if (pm.quads.isEmpty() || !opt.roughnessFromTransect) return pm;

    int c = 0;
    for (int i = 0; i + 1 < lat.nAlong; ++i)
        for (int k = 0; k + 1 < lat.nAcross; ++k, ++c)
        {
            const double m = 0.5 * (lat.offsets[k] + lat.offsets[k + 1]);
            double n = qQNaN();
            QString suffix;
            roughnessFor(p.section, m, &n, &suffix);
            pm.quads[c].tag = QStringLiteral("channel:%1%2").arg(lat.conduitId, suffix);
            if (std::isfinite(n) && n > 0.0) pm.quads[c].mannings = n;
        }
    return pm;
}

QVector<ConstraintSegment> corridorStrings(const BurnLattice &lat, int markerBase,
                                           QHash<int, QString> *markerToTag)
{
    QVector<ConstraintSegment> out;
    if (!lat.isValid()) return out;

    out.reserve(lat.nAcross);
    for (int k = 0; k < lat.nAcross; ++k)
    {
        ConstraintSegment cs;
        cs.path.reserve(lat.nAlong);
        for (int i = 0; i < lat.nAlong; ++i) cs.path.append(lat.xy[lat.at(i, k)]);
        cs.marker = markerBase + k;
        cs.tag    = QStringLiteral("burn:%1:%2").arg(lat.conduitId).arg(k);
        if (markerToTag) markerToTag->insert(cs.marker, cs.tag);
        out.append(cs);
    }
    return out;
}

QVector<SteinerPoint> corridorPoints(const BurnLattice &lat, int marker)
{
    QVector<SteinerPoint> out;
    if (!lat.isValid()) return out;

    out.reserve(lat.xy.size());
    for (int idx = 0; idx < lat.xy.size(); ++idx)
    {
        SteinerPoint sp;
        sp.xy   = lat.xy[idx];
        sp.z    = lat.z[idx];
        sp.hasZ = true;
        sp.marker = marker;
        sp.tag  = QStringLiteral("burn:%1").arg(lat.conduitId);
        out.append(sp);
    }
    return out;
}

QPolygonF corridorRing(const BurnLattice &lat)
{
    QPolygonF ring;
    if (!lat.isValid()) return ring;

    ring.reserve(2 * (lat.nAlong + lat.nAcross));
    for (int k = 0; k < lat.nAcross; ++k)            ring << lat.xy[lat.at(0, k)];
    for (int i = 1; i < lat.nAlong; ++i)             ring << lat.xy[lat.at(i, lat.nAcross - 1)];
    for (int k = lat.nAcross - 2; k >= 0; --k)       ring << lat.xy[lat.at(lat.nAlong - 1, k)];
    for (int i = lat.nAlong - 2; i >= 1; --i)        ring << lat.xy[lat.at(i, 0)];
    return normalizeRingCCW(ring);
}

void corridorZSeeds(const BurnLattice &lat, QVector<QPointF> *xy, QVector<double> *z)
{
    if (!lat.isValid() || !xy || !z) return;
    xy->reserve(xy->size() + lat.xy.size());
    z ->reserve(z->size()  + lat.z.size());
    for (int i = 0; i < lat.xy.size(); ++i)
    {
        if (!std::isfinite(lat.z[i])) continue;
        xy->append(lat.xy[i]);
        z ->append(lat.z[i]);
    }
}

} // namespace mesh
