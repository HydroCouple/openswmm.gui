/*!
 * \file   channelburn.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Corridor projection and the per-pixel burn rule
 * (CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.4, phase P1).
 */
#include "mesh/channelburn.h"

#include <QCryptographicHash>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

/*! Nearest point on segment a→b to p, as the parameter t in [0, 1]. */
double projectOnSegment(const QPointF &a, const QPointF &b, const QPointF &p, double *len)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double l2 = dx * dx + dy * dy;
    if (len) *len = std::sqrt(l2);
    if (!(l2 > 0.0)) return 0.0;
    const double t = ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / l2;
    return std::clamp(t, 0.0, 1.0);
}

/*! Signed lateral distance of p from the INFINITE line a→b, positive to the
 *  RIGHT of a→b.  [TRANSECTS] stations ascend from the left bank to the right
 *  looking downstream, so "right is positive" is what makes the section's own
 *  station axis and the corridor's offset axis the same axis. */
double signedSide(const QPointF &a, const QPointF &b, const QPointF &p, double len)
{
    if (!(len > 0.0)) return 0.0;
    const double cross = (b.x() - a.x()) * (p.y() - a.y())
                       - (b.y() - a.y()) * (p.x() - a.x());
    return -cross / len;   // cross > 0 is LEFT, so negate
}

double profileExtent(const BurnProfile &p)
{
    if (!p.section.isValid()) return 0.0;
    return std::max(std::abs(p.section.sMin), std::abs(p.section.sMax));
}

} // namespace

// ─────────────────────────────── The index ────────────────────────────────

void BurnCorridorIndex::build(const QVector<BurnProfile> &profiles)
{
    m_seg.clear();
    m_grid.clear();
    m_extent.clear();
    m_bounds  = QRectF();
    m_cell    = 0.0;
    m_radius  = 0.0;

    m_extent.reserve(profiles.size());
    double segLenSum = 0.0;
    qsizetype segCount = 0;

    for (int pi = 0; pi < profiles.size(); ++pi)
    {
        const BurnProfile &p = profiles[pi];
        const double ext = profileExtent(p);
        m_extent.append(ext);
        if (!p.isValid()) continue;
        m_radius = std::max(m_radius, ext);

        for (int i = 0; i + 1 < p.centerline.size(); ++i)
        {
            Seg s;
            s.a = p.centerline[i];
            s.b = p.centerline[i + 1];
            s.chainageA = p.chainage[i];
            s.profile   = pi;
            const double len = std::hypot(s.b.x() - s.a.x(), s.b.y() - s.a.y());
            if (!(len > 0.0)) continue;
            segLenSum += len;
            ++segCount;
            m_seg.append(s);
        }
    }
    if (m_seg.isEmpty()) return;

    // One cell must cover the widest corridor, so a 3x3 neighbourhood around a
    // query always holds every segment that can claim it.  A degenerate R of 0
    // falls back to the mean segment length so the grid stays useful.
    const double meanLen = (segCount > 0) ? segLenSum / double(segCount) : 1.0;
    m_cell = std::max(m_radius, std::max(meanLen, 1e-9));

    double xMin = std::numeric_limits<double>::infinity(), yMin = xMin;
    double xMax = -xMin, yMax = -yMin;
    for (int si = 0; si < m_seg.size(); ++si)
    {
        const Seg &s = m_seg[si];
        const double sx0 = std::min(s.a.x(), s.b.x()), sx1 = std::max(s.a.x(), s.b.x());
        const double sy0 = std::min(s.a.y(), s.b.y()), sy1 = std::max(s.a.y(), s.b.y());
        xMin = std::min(xMin, sx0); xMax = std::max(xMax, sx1);
        yMin = std::min(yMin, sy0); yMax = std::max(yMax, sy1);

        const int i0 = int(std::floor(sx0 / m_cell)), i1 = int(std::floor(sx1 / m_cell));
        const int j0 = int(std::floor(sy0 / m_cell)), j1 = int(std::floor(sy1 / m_cell));
        for (int i = i0; i <= i1; ++i)
            for (int j = j0; j <= j1; ++j)
                m_grid[cellKey(i, j)].append(si);
    }
    const double ext = m_radius;
    m_bounds = QRectF(QPointF(xMin - ext, yMin - ext), QPointF(xMax + ext, yMax + ext));
}

void BurnCorridorIndex::projectAll(const QPointF &p, QVector<BurnProjection> *out) const
{
    if (!out) return;
    out->clear();
    if (m_seg.isEmpty() || !(m_cell > 0.0)) return;
    if (!m_bounds.contains(p)) return;

    // Best (smallest |offset|) segment per profile.
    QHash<int, int> bestSeg;      // profile -> segment index
    QHash<int, double> bestD2;

    const int ic = int(std::floor(p.x() / m_cell));
    const int jc = int(std::floor(p.y() / m_cell));
    const int reach = std::max(1, int(std::ceil(m_radius / m_cell)));
    for (int i = ic - reach; i <= ic + reach; ++i)
    {
        for (int j = jc - reach; j <= jc + reach; ++j)
        {
            const auto it = m_grid.constFind(cellKey(i, j));
            if (it == m_grid.constEnd()) continue;
            for (const int si : it.value())
            {
                const Seg &s = m_seg[si];
                double len = 0.0;
                const double t = projectOnSegment(s.a, s.b, p, &len);
                const QPointF q = s.a + (s.b - s.a) * t;
                const double d2 = (q.x() - p.x()) * (q.x() - p.x())
                                + (q.y() - p.y()) * (q.y() - p.y());
                const auto had = bestD2.constFind(s.profile);
                if (had != bestD2.constEnd() && had.value() <= d2) continue;
                bestD2.insert(s.profile, d2);
                bestSeg.insert(s.profile, si);
            }
        }
    }

    out->reserve(bestSeg.size());
    for (auto it = bestSeg.constBegin(); it != bestSeg.constEnd(); ++it)
    {
        const int pi = it.key();
        const Seg &s = m_seg[it.value()];
        const double ext = (pi < m_extent.size()) ? m_extent[pi] : 0.0;
        if (bestD2.value(pi) > ext * ext) continue;      // outside this corridor

        double len = 0.0;
        const double t = projectOnSegment(s.a, s.b, p, &len);
        BurnProjection pr;
        pr.profile  = pi;
        pr.chainage = s.chainageA + t * len;
        pr.offset   = signedSide(s.a, s.b, p, len);
        out->append(pr);
    }
}

// ──────────────────────────────── The rule ────────────────────────────────

BurnOutcome burnPixel(double zDem, bool demNoData, double zSec, double s,
                      const BurnRule &rule, double *zOut)
{
    if (!std::isfinite(zSec)) return BurnOutcome::Outside;

    const bool forced = std::abs(s) <= rule.forceHalfWidth;

    if (demNoData || !std::isfinite(zDem))
    {
        // Inside the forced corridor the section IS the answer, so a NoData gap
        // is bridged. Outside it the rule is min(), which is undefined against
        // NaN — so the gap stays a gap.
        if (!forced) return BurnOutcome::NoDataKept;
        if (zOut) *zOut = zSec;
        return BurnOutcome::Replaced;
    }

    double z = forced ? zSec : std::min(zDem, zSec);
    if (rule.maxIncision > 0.0) z = std::max(z, zDem - rule.maxIncision);

    if (zOut) *zOut = z;
    if (z == zDem)  return BurnOutcome::Unchanged;
    return forced ? BurnOutcome::Replaced : BurnOutcome::Lowered;
}

bool bestBurnAt(const BurnCorridorIndex &index, const QVector<BurnProfile> &profiles,
                const QPointF &p, BurnProjection *projection, double *zSec)
{
    QVector<BurnProjection> hits;
    index.projectAll(p, &hits);
    if (hits.isEmpty()) return false;

    bool    have = false;
    double  bestZ = 0.0;
    BurnProjection best;

    for (const BurnProjection &pr : hits)
    {
        if (pr.profile < 0 || pr.profile >= profiles.size()) continue;
        bool inExtent = false;
        const double z = sectionZAt(profiles[pr.profile], pr.chainage, pr.offset, &inExtent);
        if (!inExtent || !std::isfinite(z)) continue;

        // Lowest wins; a tie goes to the conduit whose id sorts first, so the
        // result cannot depend on the order the profiles were built in.
        if (!have || z < bestZ
            || (z == bestZ && profiles[pr.profile].conduitId < profiles[best.profile].conduitId))
        {
            have  = true;
            bestZ = z;
            best  = pr;
        }
    }
    if (!have) return false;
    if (projection) *projection = best;
    if (zSec)       *zSec       = bestZ;
    return true;
}

// ─────────────────────────────────  Frames ────────────────────────────────

BurnProfile toRasterFrame(const BurnProfile &p, const QVector<QPointF> &rasterCenterline,
                          double hScale, double vScale)
{
    BurnProfile out;
    if (!p.isValid() || rasterCenterline.size() != p.centerline.size()) return out;
    if (!(hScale > 0.0) || !std::isfinite(hScale)) return out;
    if (!std::isfinite(vScale)) return out;

    out.conduitId  = p.conduitId;
    out.centerline = rasterCenterline;
    out.chainage   = polylineChainage(rasterCenterline);

    out.bedZ.reserve(p.bedZ.size());
    for (const double z : p.bedZ) out.bedZ.append(z * vScale);

    out.offsets.reserve(p.offsets.size());
    for (const double s : p.offsets) out.offsets.append(s * hScale);

    out.relZ.reserve(p.relZ.size());
    for (const QVector<double> &row : p.relZ)
    {
        QVector<double> r;
        r.reserve(row.size());
        for (const double z : row) r.append(z * vScale);
        out.relZ.append(r);
    }

    out.section = p.section;
    for (double &s : out.section.station) s *= hScale;
    for (double &z : out.section.relZ)    z *= vScale;
    out.section.sMin      *= hScale;
    out.section.sMax      *= hScale;
    out.section.leftBank  *= hScale;
    out.section.rightBank *= hScale;
    return out;
}

BurnRule toRasterRule(const BurnOptions &opt, double hScale, double vScale)
{
    BurnRule r;
    r.forceHalfWidth = opt.forceHalfWidth * hScale;
    r.maxIncision    = opt.maxIncision    * vScale;
    return r;
}

// ──────────────────────────────── Identity ────────────────────────────────

QString burnFingerprint(const QString &demIdentity, const BurnOptions &opt,
                        const QVector<BurnProfile> &profiles)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    auto feed = [&h](double v) { h.addData(QByteArrayView(reinterpret_cast<const char *>(&v),
                                                          sizeof(v))); };
    auto feedI = [&h](int v) { h.addData(QByteArrayView(reinterpret_cast<const char *>(&v),
                                                        sizeof(v))); };

    h.addData(demIdentity.toUtf8());

    feed(opt.forceHalfWidth);  feed(opt.maxHalfWidth);
    feedI(opt.clipToBanks);    feed(opt.bankPad);
    feed(opt.chainageStep);    feed(opt.lateralStep);   feedI(opt.stringCount);
    feedI(int(opt.anchor));    feed(opt.sectionBlend);
    feedI(opt.enforceMonotone);feed(opt.maxIncision);
    feedI(opt.burnStreets);

    for (const BurnProfile &p : profiles)
    {
        h.addData(p.conduitId.toUtf8());
        feedI(int(p.centerline.size()));
        for (const QPointF &pt : p.centerline) { feed(pt.x()); feed(pt.y()); }
        for (const double z : p.bedZ)    feed(z);
        for (const double s : p.offsets) feed(s);
        for (const QVector<double> &row : p.relZ)
            for (const double z : row) feed(z);
    }
    return QString::fromLatin1(h.result().toHex().left(8));
}

} // namespace mesh
