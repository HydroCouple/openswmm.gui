/*!
 * \file   gageblend.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "core/gageblend.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace GageBlend
{

namespace
{
// Longest span mismatch tolerated before two series are judged to be on
// different time bases (a relative-hours series loads near 1899-12-30).
constexpr qint64 kMaxSpanGapSec = 365LL * 24 * 3600;

constexpr qint64 kMaxIntervalSec = 24LL * 3600;

qint64 gcd64(qint64 a, qint64 b)
{
    while (b != 0)
    {
        const qint64 t = a % b;
        a = b;
        b = t;
    }
    return a < 0 ? -a : a;
}
} // namespace

QVector<Box> toBoxes(const SourceGage &source)
{
    QVector<Box> boxes;
    const int n = static_cast<int>(source.points.size());
    if (n == 0 || source.intervalSec <= 0)
        return boxes;

    const double interval = static_cast<double>(source.intervalSec);
    boxes.reserve(n);

    double accum = 0.0;   // CUMULATIVE running total
    for (int k = 0; k < n; ++k)
    {
        const SeriesPoint &pt = source.points[k];
        if (!std::isfinite(pt.value))
            continue;

        double intensity = 0.0;
        switch (source.rainType)
        {
        case RainType::Intensity:
            intensity = pt.value;
            break;
        case RainType::Volume:
            // Depth over one interval -> depth/hour.
            intensity = pt.value / interval * 3600.0;
            break;
        case RainType::Cumulative:
        {
            // A decrease is a counter reset: the new value is the whole depth.
            const double depth = (pt.value < accum) ? pt.value : (pt.value - accum);
            accum = pt.value;
            intensity = depth / interval * 3600.0;
            break;
        }
        }

        // Preemption: the next entry cuts this box short if it starts early.
        qint64 end = pt.t + source.intervalSec;
        if (k + 1 < n)
            end = std::min(end, source.points[k + 1].t);
        if (end <= pt.t)
            continue;

        intensity *= source.scaleFactor;
        if (!std::isfinite(intensity))
            continue;
        boxes.append({pt.t, end, intensity});
    }
    return boxes;
}

double boxDepth(const QVector<Box> &boxes)
{
    double depth = 0.0;
    for (const Box &b : boxes)
        depth += b.intensity * static_cast<double>(b.t1 - b.t0) / 3600.0;
    return depth;
}

double engineDepth(const QVector<SeriesPoint> &points,
                   qint64 intervalSec,
                   RainType rainType,
                   double scaleFactor)
{
    SourceGage s;
    s.points      = points;
    s.intervalSec = intervalSec;
    s.rainType    = rainType;
    s.scaleFactor = scaleFactor;
    return boxDepth(toBoxes(s));
}

BlendResult blend(const QVector<SourceGage> &sources, const QVector<double> &weights)
{
    BlendResult r;

    const int n = static_cast<int>(sources.size());
    if (n == 0 || weights.size() != n)
    {
        r.error = QStringLiteral("No contributing gages.");
        return r;
    }

    // ── Validate, expand to boxes ───────────────────────────────────────
    QVector<QVector<Box>> boxes;
    boxes.reserve(n);
    qint64 pitch = 0;
    qint64 spanLo = 0, spanHi = 0;
    bool haveSpan = false;

    for (int i = 0; i < n; ++i)
    {
        const SourceGage &s = sources[i];
        const QString who = s.name.isEmpty() ? QStringLiteral("gage %1").arg(i) : s.name;

        if (s.intervalSec <= 0 || s.intervalSec > kMaxIntervalSec
            || s.intervalSec % 60 != 0)
        {
            // The INP writer stores the interval as h:mm, so anything that is
            // not a whole number of minutes is destroyed on round-trip.
            r.error = QStringLiteral(
                          "%1 has a recording interval of %2 s. Only whole "
                          "minutes up to 24 hours can be written to an INP file.")
                          .arg(who)
                          .arg(s.intervalSec);
            return r;
        }

        const QVector<Box> b = toBoxes(s);
        if (b.isEmpty())
        {
            r.error = QStringLiteral("%1 has no usable rainfall data.").arg(who);
            return r;
        }

        const qint64 lo = b.first().t0;
        const qint64 hi = b.last().t1;
        if (haveSpan)
        {
            // Disjoint by more than a year: almost certainly a relative-hours
            // series (epoch ~1899-12-30) mixed with absolute-dated ones.
            if (lo - spanHi > kMaxSpanGapSec || spanLo - hi > kMaxSpanGapSec)
            {
                r.error = QStringLiteral(
                              "%1 covers a period disjoint from the other gages "
                              "by more than a year — the series are on different "
                              "time bases and cannot be combined.")
                              .arg(who);
                return r;
            }
            spanLo = std::min(spanLo, lo);
            spanHi = std::max(spanHi, hi);
        }
        else
        {
            spanLo = lo;
            spanHi = hi;
            haveSpan = true;
        }

        pitch = (pitch == 0) ? s.intervalSec : gcd64(pitch, s.intervalSec);
        boxes.append(b);
    }

    if (pitch <= 0 || spanHi <= spanLo)
    {
        r.error = QStringLiteral("The contributing gages span no time.");
        return r;
    }

    const qint64 cellCount = (spanHi - spanLo + pitch - 1) / pitch;
    if (cellCount > kMaxGridCells)
    {
        r.error = QStringLiteral(
                      "Combining these gages needs %1 intervals of %2 s "
                      "(their recording intervals have a common divisor of only "
                      "%2 s over %3 days). The limit is %4.")
                      .arg(cellCount)
                      .arg(pitch)
                      .arg((spanHi - spanLo) / 86400)
                      .arg(kMaxGridCells);
        return r;
    }

    // ── Exact-integral rebin ────────────────────────────────────────────
    // Every box is distributed over the grid cells it overlaps, weighted by the
    // overlap length. Because the contribution is an integral rather than a
    // sample, depth is preserved regardless of how the box aligns to the grid.
    QVector<double> cellDepth(static_cast<int>(cellCount), 0.0);
    const double pitchHours = static_cast<double>(pitch) / 3600.0;

    for (int i = 0; i < n; ++i)
    {
        const double w = weights[i];
        if (!std::isfinite(w) || w == 0.0)
            continue;
        for (const Box &b : boxes[i])
        {
            const qint64 first = (b.t0 - spanLo) / pitch;
            const qint64 last  = (b.t1 - 1 - spanLo) / pitch;
            for (qint64 j = first; j <= last && j < cellCount; ++j)
            {
                if (j < 0)
                    continue;
                const qint64 cs = spanLo + j * pitch;
                const qint64 ce = cs + pitch;
                const qint64 lo = std::max(b.t0, cs);
                const qint64 hi = std::min(b.t1, ce);
                if (hi > lo)
                    cellDepth[static_cast<int>(j)] +=
                        w * b.intensity * static_cast<double>(hi - lo) / 3600.0;
            }
        }
    }

    // ── Emit ────────────────────────────────────────────────────────────
    // Zero cells are dropped: the engine already reads zero both in the gaps
    // between boxes and after the last entry, so omitting them is equivalent
    // and shrinks a long record dramatically. The first and last cell are kept
    // regardless, so the series' extent stays unambiguous.
    r.intervalSec = pitch;
    r.points.reserve(static_cast<int>(cellCount));
    for (int j = 0; j < cellCount; ++j)
    {
        const double intensity = cellDepth[j] / pitchHours;
        const bool edge = (j == 0 || j == cellCount - 1);
        if (intensity == 0.0 && !edge)
            continue;
        r.points.append({spanLo + static_cast<qint64>(j) * pitch, intensity});
    }

    // A single-point table is read by the engine as its value at ALL times
    // before the first entry (table_step_cursor short-circuits for n == 1),
    // so never emit one.
    if (r.points.size() < 2)
    {
        r.error = QStringLiteral(
            "The blended series collapsed to fewer than two entries and would "
            "be misread by the engine.");
        r.points.clear();
        return r;
    }

    // ── Volume + peak figures ───────────────────────────────────────────
    for (int i = 0; i < n; ++i)
    {
        const double w = weights[i];
        if (!std::isfinite(w))
            continue;
        r.referenceDepth += w * boxDepth(boxes[i]);

        double peak = 0.0;
        for (const Box &b : boxes[i])
            peak = std::max(peak, b.intensity);
        r.peakReference += w * peak;
    }

    r.blendedDepth = engineDepth(r.points, pitch);
    for (const SeriesPoint &p : std::as_const(r.points))
        r.peakBlended = std::max(r.peakBlended, p.value);

    r.relativeError = std::abs(r.blendedDepth - r.referenceDepth)
                      / std::max(std::abs(r.referenceDepth), 1e-9);
    return r;
}

} // namespace GageBlend
