/*!
 * \file   channelburnprofile.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Open-channel bathymetry reconstruction (CHANNEL_BURN_IN_PLAN_2026-09-21.md
 * §4.1-§4.3, phase P0).  Pure geometry: no GDAL, no engine handle, no widgets.
 */
#include "mesh/channelburnprofile.h"

#include <algorithm>
#include <cmath>

namespace mesh {

namespace {

/*! Stations closer than this (relative to the section width) are the same
 *  station.  Sections are authored in survey units, so an absolute floor of a
 *  micrometre-scale value would be meaningless; the tolerance scales. */
double stationEps(double width) noexcept
{
    return std::max(1e-9, std::abs(width) * 1e-9);
}

bool finitePt(const QPointF &p) noexcept
{
    return std::isfinite(p.x()) && std::isfinite(p.y());
}

void warn(QStringList *out, const QString &msg)
{
    if (out) out->append(msg);
}

/*! Linear interpolation of \p y over strictly-ascending \p x at \p at.
 *  Outside the range returns NaN — every caller wants an explicit extent test,
 *  not a silent clamp. */
double interpAt(const QVector<double> &x, const QVector<double> &y, double at)
{
    const int n = x.size();
    if (n < 2 || y.size() != n || !std::isfinite(at)) return qQNaN();
    if (at < x.first() || at > x.last())              return qQNaN();

    const auto it = std::upper_bound(x.cbegin(), x.cend(), at);
    if (it == x.cbegin()) return y.first();
    if (it == x.cend())   return y.last();
    const int   hi = int(it - x.cbegin());
    const int   lo = hi - 1;
    const double dx = x[hi] - x[lo];
    if (!(dx > 0.0)) return y[lo];
    const double t = (at - x[lo]) / dx;
    return y[lo] + t * (y[hi] - y[lo]);
}

/*! Same, but clamped at both ends — for the bed, which is defined everywhere
 *  along the conduit. */
double interpClamped(const QVector<double> &x, const QVector<double> &y, double at)
{
    const int n = x.size();
    if (n == 0 || y.size() != n) return qQNaN();
    if (n == 1 || !std::isfinite(at)) return y.first();
    if (at <= x.first()) return y.first();
    if (at >= x.last())  return y.last();
    return interpAt(x, y, at);
}

/*! Insert \p s into the ascending, deduplicated ladder \p out. */
void pushOffset(QVector<double> &out, double s, double eps)
{
    if (!std::isfinite(s)) return;
    for (const double have : out)
        if (std::abs(have - s) <= eps) return;
    out.append(s);
}

} // namespace

// ───────────────────────────── Section producers ──────────────────────────

QVector<double> cosineDepthLadder(double yFull, int samples)
{
    QVector<double> out;
    if (!(yFull > 0.0) || !std::isfinite(yFull)) return out;
    const int n = std::clamp(samples, 8, 512);
    out.reserve(n + 1);
    for (int i = 0; i <= n; ++i)
        out.append(yFull * 0.5 * (1.0 - std::cos(M_PI * double(i) / double(n))));
    return out;
}

SectionGeometry sectionFromWidths(const QVector<double> &depths,
                                  const QVector<double> &widths)
{
    SectionGeometry g;
    const int n = std::min(depths.size(), widths.size());
    if (n < 1) return g;

    // Keep the FIRST depth at which each half-width occurs.  The station ladder
    // has to stay strictly ascending, so a width that repeats (a vertical wall)
    // contributes one station, and a width that shrinks with depth — a closed
    // shape, which the open-section gate excludes — contributes none.
    QVector<double> d, hw;
    d.reserve(n);
    hw.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        if (!std::isfinite(depths[i]) || !std::isfinite(widths[i]) || widths[i] < 0.0) continue;
        const double h = widths[i] * 0.5;
        if (!d.isEmpty() && (depths[i] <= d.last() || h <= hw.last())) continue;
        d.append(depths[i]);
        hw.append(h);
    }
    if (d.isEmpty()) return g;

    const double eps = stationEps(hw.last() * 2.0);
    if (hw.last() <= eps) return g;   // a section with no width
    g.station.reserve(d.size() * 2);
    g.elevation.reserve(d.size() * 2);
    for (int i = d.size() - 1; i >= 0; --i)           // left bank down to the invert
    {
        g.station.append(-hw[i]);
        g.elevation.append(d[i]);
    }
    for (int i = 0; i < d.size(); ++i)                // invert up to the right bank
    {
        if (i == 0 && hw[0] <= eps) continue;         // a point invert (triangle) is one station
        g.station.append(hw[i]);
        g.elevation.append(d[i]);
    }
    return g;
}

SectionGeometry sectionFromTransect(const QVector<double> &stations,
                                    const QVector<double> &elevations,
                                    double leftBank, double rightBank,
                                    double nLeft, double nChannel, double nRight,
                                    double stationMultiplier,
                                    double elevationOffset)
{
    SectionGeometry g;
    const int n = std::min(stations.size(), elevations.size());
    if (n < 2) return g;

    const double xm = (std::isfinite(stationMultiplier) && stationMultiplier != 0.0)
                          ? stationMultiplier : 1.0;
    const double yo = std::isfinite(elevationOffset) ? elevationOffset : 0.0;

    g.station.reserve(n);
    g.elevation.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        g.station.append(stations[i] * xm);
        g.elevation.append(elevations[i] + yo);
    }
    g.leftBank  = std::isfinite(leftBank)  ? leftBank  * xm : qQNaN();
    g.rightBank = std::isfinite(rightBank) ? rightBank * xm : qQNaN();
    g.nLeft     = nLeft;
    g.nChannel  = nChannel;
    g.nRight    = nRight;

    // A negative multiplier mirrors the section; restore the ascending-station
    // invariant rather than rejecting it downstream.
    if (xm < 0.0)
    {
        std::reverse(g.station.begin(), g.station.end());
        std::reverse(g.elevation.begin(), g.elevation.end());
        std::swap(g.leftBank, g.rightBank);
        std::swap(g.nLeft, g.nRight);
    }
    return g;
}

QString validateSection(const SectionGeometry &s)
{
    if (s.station.size() < 2)
        return QStringLiteral("section needs at least 2 station/elevation pairs");
    if (s.station.size() != s.elevation.size())
        return QStringLiteral("section station and elevation counts differ (%1 vs %2)")
            .arg(s.station.size()).arg(s.elevation.size());
    for (int i = 0; i < s.station.size(); ++i)
    {
        if (!std::isfinite(s.station[i]) || !std::isfinite(s.elevation[i]))
            return QStringLiteral("section point %1 is not finite").arg(i);
        if (i > 0 && !(s.station[i] > s.station[i - 1]))
            return QStringLiteral("section stations must be strictly ascending "
                                  "(point %1 = %2 follows %3)")
                .arg(i).arg(s.station[i]).arg(s.station[i - 1]);
    }
    const double span = s.station.last() - s.station.first();
    if (!(span > 0.0))
        return QStringLiteral("section has zero width");
    return QString();
}

// ─────────────────────── Normalisation and lateral queries ────────────────

NormalizedSection normalizeSection(const SectionGeometry &s,
                                   const BurnOptions &opt,
                                   QStringList *warnings,
                                   QString *err)
{
    NormalizedSection ns;
    const QString bad = validateSection(s);
    if (!bad.isEmpty())
    {
        if (err) *err = bad;
        return ns;
    }

    const int n = s.station.size();
    const double zMin = *std::min_element(s.elevation.cbegin(), s.elevation.cend());
    const double span = s.station.last() - s.station.first();
    const double eps  = stationEps(span);

    // Anchor.  The thalweg is the MIDPOINT of the minimum-elevation run: a
    // trapezoid's whole bed is minimal, so taking the first minimal station
    // would shift the section by half the bed width.
    double shift = 0.0;
    SectionAnchor anchor = opt.anchor;
    if (anchor == SectionAnchor::BankMidpoint
        && !(std::isfinite(s.leftBank) && std::isfinite(s.rightBank)))
    {
        warn(warnings, QStringLiteral("bank-midpoint anchor requested but the section has no "
                                      "bank stations; using the thalweg"));
        anchor = SectionAnchor::Thalweg;
    }
    switch (anchor)
    {
    case SectionAnchor::Thalweg:
    {
        // Tolerance is relative to the section's vertical relief, so a survey
        // datum of 800 ft does not make every point "minimal".
        const double zMax   = *std::max_element(s.elevation.cbegin(), s.elevation.cend());
        const double relief = zMax - zMin;
        const double zTol   = std::max(1e-12, relief * 1e-9);
        double lo = qQNaN(), hi = qQNaN();
        for (int i = 0; i < n; ++i)
        {
            if (s.elevation[i] - zMin > zTol) continue;
            if (!std::isfinite(lo)) lo = s.station[i];
            hi = s.station[i];
        }
        shift = 0.5 * (lo + hi);
        break;
    }
    case SectionAnchor::BankMidpoint:
        shift = 0.5 * (s.leftBank + s.rightBank);
        break;
    case SectionAnchor::StationZero:
        shift = 0.0;
        break;
    }

    ns.station.reserve(n);
    ns.relZ.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        ns.station.append(s.station[i] - shift);
        ns.relZ.append(s.elevation[i] - zMin);
    }
    ns.leftBank  = std::isfinite(s.leftBank)  ? s.leftBank  - shift : qQNaN();
    ns.rightBank = std::isfinite(s.rightBank) ? s.rightBank - shift : qQNaN();
    ns.nLeft     = s.nLeft;
    ns.nChannel  = s.nChannel;
    ns.nRight    = s.nRight;

    // Extent: the section, then the banks, then the hard half-width cap.
    ns.sMin = ns.station.first();
    ns.sMax = ns.station.last();
    if (opt.clipToBanks && std::isfinite(ns.leftBank) && std::isfinite(ns.rightBank))
    {
        const double pad = std::max(0.0, opt.bankPad);
        ns.sMin = std::max(ns.sMin, ns.leftBank  - pad);
        ns.sMax = std::min(ns.sMax, ns.rightBank + pad);
    }
    if (opt.maxHalfWidth > 0.0)
    {
        ns.sMin = std::max(ns.sMin, -opt.maxHalfWidth);
        ns.sMax = std::min(ns.sMax,  opt.maxHalfWidth);
    }

    if (!(ns.sMax - ns.sMin > eps))
    {
        if (err) *err = QStringLiteral("the clipped section extent is empty "
                                       "(check bank stations and maxHalfWidth)");
        ns.sMin = ns.sMax = qQNaN();
        return ns;
    }
    if (ns.sMin > 0.0 || ns.sMax < 0.0)
    {
        if (err) *err = QStringLiteral("the anchor (s = 0) falls outside the clipped extent "
                                       "[%1, %2]").arg(ns.sMin).arg(ns.sMax);
        ns.sMin = ns.sMax = qQNaN();
        return ns;
    }
    const double halfExtent = std::min(-ns.sMin, ns.sMax);
    if (opt.forceHalfWidth > halfExtent + eps)
        warn(warnings, QStringLiteral("forceHalfWidth %1 is wider than the section's clipped "
                                      "half-extent %2; the forced corridor stops at the section")
                           .arg(opt.forceHalfWidth).arg(halfExtent));
    return ns;
}

double relZAt(const NormalizedSection &ns, double s)
{
    if (!ns.isValid() || !std::isfinite(s)) return qQNaN();
    if (s < ns.sMin || s > ns.sMax)         return qQNaN();
    return interpAt(ns.station, ns.relZ, s);
}

QVector<double> corridorOffsets(const NormalizedSection &ns, const BurnOptions &opt)
{
    QVector<double> out;
    if (!ns.isValid()) return out;

    const double eps = stationEps(ns.sMax - ns.sMin);
    const double R   = opt.forceHalfWidth;

    pushOffset(out, 0.0, eps);
    pushOffset(out, ns.sMin, eps);
    pushOffset(out, ns.sMax, eps);
    if (R > 0.0)
    {
        pushOffset(out, std::max(ns.sMin, -R), eps);
        pushOffset(out, std::min(ns.sMax,  R), eps);
    }
    if (std::isfinite(ns.leftBank)  && ns.leftBank  > ns.sMin && ns.leftBank  < ns.sMax)
        pushOffset(out, ns.leftBank, eps);
    if (std::isfinite(ns.rightBank) && ns.rightBank > ns.sMin && ns.rightBank < ns.sMax)
        pushOffset(out, ns.rightBank, eps);

    for (int j = 1; j <= std::max(0, opt.stringCount); ++j)
    {
        const double f = double(j) / double(opt.stringCount + 1);
        pushOffset(out, ns.sMin * f, eps);
        pushOffset(out, ns.sMax * f, eps);
    }

    std::sort(out.begin(), out.end());

    // lateralStep is a MAXIMUM gap, not a spacing: subdivide whatever the
    // ladder above left wider than it.
    if (opt.lateralStep > 0.0)
    {
        QVector<double> dense;
        dense.reserve(out.size() * 2);
        for (int i = 0; i < out.size(); ++i)
        {
            dense.append(out[i]);
            if (i + 1 >= out.size()) break;
            const double gap = out[i + 1] - out[i];
            const int    k   = int(std::ceil(gap / opt.lateralStep)) - 1;
            for (int j = 1; j <= k; ++j)
                dense.append(out[i] + gap * double(j) / double(k + 1));
        }
        out = dense;
    }
    return out;
}

// ──────────────────────────── Profile assembly ────────────────────────────

QVector<QPointF> densifyPolyline(const QVector<QPointF> &path, double step)
{
    if (path.size() < 2 || !(step > 0.0)) return path;

    QVector<QPointF> out;
    out.reserve(path.size());
    out.append(path.first());
    for (int i = 1; i < path.size(); ++i)
    {
        const QPointF &a = path[i - 1];
        const QPointF &b = path[i];
        const double   len = std::hypot(b.x() - a.x(), b.y() - a.y());
        const int      k   = (len > step) ? int(std::ceil(len / step)) - 1 : 0;
        for (int j = 1; j <= k; ++j)
        {
            const double t = double(j) / double(k + 1);
            out.append(a + (b - a) * t);
        }
        out.append(b);
    }
    return out;
}

QVector<double> polylineChainage(const QVector<QPointF> &path)
{
    QVector<double> out;
    out.reserve(path.size());
    double acc = 0.0;
    for (int i = 0; i < path.size(); ++i)
    {
        if (i > 0)
            acc += std::hypot(path[i].x() - path[i - 1].x(), path[i].y() - path[i - 1].y());
        out.append(acc);
    }
    return out;
}

BurnProfile buildBurnProfile(const ChannelInput &in, const BurnOptions &opt,
                             QStringList *warnings, QString *err)
{
    BurnProfile p;
    p.conduitId = in.conduitId;

    if (in.centerline.size() < 2)
    {
        if (err) *err = QStringLiteral("conduit '%1': centreline needs at least 2 points")
                            .arg(in.conduitId);
        return p;
    }
    for (const QPointF &pt : in.centerline)
        if (!finitePt(pt))
        {
            if (err) *err = QStringLiteral("conduit '%1': centreline has a non-finite vertex")
                                .arg(in.conduitId);
            return p;
        }
    if (!std::isfinite(in.zUp) || !std::isfinite(in.zDn))
    {
        if (err) *err = QStringLiteral("conduit '%1': end inverts are not finite").arg(in.conduitId);
        return p;
    }

    QString sErr;
    p.section = normalizeSection(in.section, opt, warnings, &sErr);
    if (!p.section.isValid())
    {
        if (err) *err = QStringLiteral("conduit '%1': %2").arg(in.conduitId, sErr);
        return p;
    }

    // Chainage resolution.  The DEM-aware auto rule (min(demPixel/2, R/4)) needs
    // the raster, so it is resolved by the caller in P1; here 0 falls back to
    // R/4, and to the raw vertices when there is no R either.
    double step = opt.chainageStep;
    if (!(step > 0.0)) step = (opt.forceHalfWidth > 0.0) ? opt.forceHalfWidth * 0.25 : 0.0;

    p.centerline = densifyPolyline(in.centerline, step);
    p.chainage   = polylineChainage(p.centerline);
    const double L = p.chainage.isEmpty() ? 0.0 : p.chainage.last();
    if (!(L > 0.0))
    {
        if (err) *err = QStringLiteral("conduit '%1': centreline has zero length").arg(in.conduitId);
        return p;
    }

    // Bed: linear in chainage between the two end inverts (§4.2).
    double zDn = in.zDn;
    if (zDn > in.zUp)
    {
        if (opt.enforceMonotone)
        {
            warn(warnings, QStringLiteral("conduit '%1': adverse slope (%2 -> %3) clamped flat")
                               .arg(in.conduitId).arg(in.zUp).arg(in.zDn));
            zDn = in.zUp;
        }
        else
        {
            warn(warnings, QStringLiteral("conduit '%1': adverse slope, bed rises %2 from "
                                          "upstream to downstream")
                               .arg(in.conduitId).arg(in.zDn - in.zUp));
        }
    }

    const int ns = p.chainage.size();
    p.bedZ.reserve(ns);
    for (int i = 0; i < ns; ++i)
        p.bedZ.append(in.zUp + (zDn - in.zUp) * (p.chainage[i] / L));

    // Lateral ladder, prismatic along the conduit until blending runs.
    p.offsets = corridorOffsets(p.section, opt);
    if (p.offsets.size() < 2)
    {
        if (err) *err = QStringLiteral("conduit '%1': corridor has fewer than 2 offsets")
                            .arg(in.conduitId);
        return p;
    }
    QVector<double> row;
    row.reserve(p.offsets.size());
    for (const double s : p.offsets) row.append(relZAt(p.section, s));
    p.relZ.reserve(ns);
    for (int i = 0; i < ns; ++i) p.relZ.append(row);

    return p;
}

double bedZAt(const BurnProfile &p, double chainage)
{
    return interpClamped(p.chainage, p.bedZ, chainage);
}

double sectionZAt(const BurnProfile &p, double chainage, double offset, bool *inExtent)
{
    if (inExtent) *inExtent = false;
    if (!p.isValid() || !std::isfinite(offset)) return qQNaN();
    if (offset < p.offsets.first() || offset > p.offsets.last()) return qQNaN();

    // Bracket the chainage, interpolate across offsets on each bounding
    // station, then between them — bilinear on the (chainage, offset) lattice.
    const int n = p.chainage.size();
    int lo = 0, hi = 0;
    double t = 0.0;
    if (chainage <= p.chainage.first())      { lo = hi = 0; }
    else if (chainage >= p.chainage.last())  { lo = hi = n - 1; }
    else
    {
        const auto it = std::upper_bound(p.chainage.cbegin(), p.chainage.cend(), chainage);
        hi = int(it - p.chainage.cbegin());
        lo = hi - 1;
        const double dx = p.chainage[hi] - p.chainage[lo];
        t = (dx > 0.0) ? (chainage - p.chainage[lo]) / dx : 0.0;
    }

    const double zLo = interpAt(p.offsets, p.relZ[lo], offset);
    const double zHi = (hi == lo) ? zLo : interpAt(p.offsets, p.relZ[hi], offset);
    if (!std::isfinite(zLo) || !std::isfinite(zHi)) return qQNaN();

    if (inExtent) *inExtent = true;
    return bedZAt(p, chainage) + zLo + t * (zHi - zLo);
}

// ───────────────────────────── Section blending ───────────────────────────

NormalizedSection blendSections(const NormalizedSection &a, const NormalizedSection &b, double w)
{
    if (!a.isValid()) return b;
    if (!b.isValid()) return a;
    if (w <= 0.0) return a;
    if (w >= 1.0) return b;

    // Normalised station: u in [-1, 0] maps onto [sMin, 0], u in [0, 1] onto
    // [0, sMax].  Sampling on the union of both sections' own u positions
    // keeps every authored break point of both.
    auto uOf = [](const NormalizedSection &s, double station) {
        return station < 0.0 ? -(station / s.sMin) : (station / s.sMax);
    };
    auto sOf = [](double sMin, double sMax, double u) {
        return u < 0.0 ? -u * sMin : u * sMax;
    };

    QVector<double> us;
    us.reserve(a.station.size() + b.station.size() + 2);
    for (const double st : a.station)
        if (st >= a.sMin && st <= a.sMax) us.append(uOf(a, st));
    for (const double st : b.station)
        if (st >= b.sMin && st <= b.sMax) us.append(uOf(b, st));
    us.append(-1.0);
    us.append(0.0);
    us.append(1.0);
    std::sort(us.begin(), us.end());

    NormalizedSection out;
    out.sMin      = (1.0 - w) * a.sMin + w * b.sMin;
    out.sMax      = (1.0 - w) * a.sMax + w * b.sMax;
    out.leftBank  = (1.0 - w) * a.leftBank  + w * b.leftBank;
    out.rightBank = (1.0 - w) * a.rightBank + w * b.rightBank;
    out.nLeft     = (1.0 - w) * a.nLeft     + w * b.nLeft;
    out.nChannel  = (1.0 - w) * a.nChannel  + w * b.nChannel;
    out.nRight    = (1.0 - w) * a.nRight    + w * b.nRight;

    const double eps = stationEps(out.sMax - out.sMin);
    out.station.reserve(us.size());
    out.relZ.reserve(us.size());
    for (const double u : us)
    {
        const double s = sOf(out.sMin, out.sMax, std::clamp(u, -1.0, 1.0));
        if (!out.station.isEmpty() && s - out.station.last() <= eps) continue;
        const double za = relZAt(a, sOf(a.sMin, a.sMax, std::clamp(u, -1.0, 1.0)));
        const double zb = relZAt(b, sOf(b.sMin, b.sMax, std::clamp(u, -1.0, 1.0)));
        if (!std::isfinite(za) || !std::isfinite(zb)) continue;
        out.station.append(s);
        out.relZ.append((1.0 - w) * za + w * zb);
    }

    // The thalweg of a blend of two thalweg-anchored sections is still 0, but
    // rounding can leave it a hair above; re-normalise so relZ's min is exact.
    if (!out.relZ.isEmpty())
    {
        const double m = *std::min_element(out.relZ.cbegin(), out.relZ.cend());
        if (m != 0.0)
            for (double &z : out.relZ) z -= m;
    }
    return out;
}

void blendAtSharedNode(BurnProfile &upstream, BurnProfile &downstream, double blend)
{
    if (!(blend > 0.0) || !upstream.isValid() || !downstream.isValid()) return;

    const NormalizedSection a = upstream.section;
    const NormalizedSection b = downstream.section;

    // Weight runs 0 -> 0.5 across the upstream tail and 0.5 -> 0 along the
    // downstream head, so both sides evaluate the same 50/50 blend AT the node.
    const double Lu = upstream.length();
    const double B  = std::min(blend, std::min(Lu, downstream.length()));
    if (!(B > 0.0)) return;

    for (int i = 0; i < upstream.chainage.size(); ++i)
    {
        const double d = Lu - upstream.chainage[i];
        if (d >= B) continue;
        const NormalizedSection m = blendSections(a, b, 0.5 * (1.0 - d / B));
        for (int k = 0; k < upstream.offsets.size(); ++k)
            upstream.relZ[i][k] = relZAt(m, std::clamp(upstream.offsets[k], m.sMin, m.sMax));
    }
    for (int i = 0; i < downstream.chainage.size(); ++i)
    {
        const double d = downstream.chainage[i];
        if (d >= B) continue;
        const NormalizedSection m = blendSections(a, b, 0.5 * (1.0 + d / B));
        for (int k = 0; k < downstream.offsets.size(); ++k)
            downstream.relZ[i][k] = relZAt(m, std::clamp(downstream.offsets[k], m.sMin, m.sMax));
    }
}

} // namespace mesh
