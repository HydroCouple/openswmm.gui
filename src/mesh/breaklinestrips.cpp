/*!
 * \file   breaklinestrips.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Facing break-line pairs — see breaklinestrips.h.
 */
#include "mesh/breaklinestrips.h"

#include <QHash>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

struct Seg
{
    QPointF a, b;
    int     line = -1;    ///< -1 = obstacle.
    int     index = -1;   ///< Segment index within its line.
    double  s0 = 0.0;     ///< Arc length at a along the line.
    double  len = 0.0;
};

inline double cross(const QPointF &u, const QPointF &v) { return u.x() * v.y() - u.y() * v.x(); }
inline double norm(const QPointF &u) { return std::hypot(u.x(), u.y()); }

/*! Uniform bucket grid over segment bounding boxes. */
class SegIndex
{
public:
    SegIndex(const QVector<Seg> &segs, double cell) : m_segs(segs), m_cell(cell > 0.0 ? cell : 1.0)
    {
        m_stamp.fill(0, segs.size());
        for (int i = 0; i < segs.size(); ++i)
        {
            const QRectF r = QRectF(segs[i].a, segs[i].b).normalized();
            for (qint64 y = cellOf(r.top()); y <= cellOf(r.bottom()); ++y)
                for (qint64 x = cellOf(r.left()); x <= cellOf(r.right()); ++x)
                    m_index[qMakePair(x, y)].append(i);
        }
    }
    template <class F> void visit(const QRectF &box, F &&f)
    {
        ++m_tick;
        for (qint64 y = cellOf(box.top()); y <= cellOf(box.bottom()); ++y)
            for (qint64 x = cellOf(box.left()); x <= cellOf(box.right()); ++x)
            {
                const auto it = m_index.constFind(qMakePair(x, y));
                if (it == m_index.constEnd()) continue;
                for (int i : it.value())
                {
                    if (m_stamp[i] == m_tick) continue;
                    m_stamp[i] = m_tick;
                    f(i);
                }
            }
    }
private:
    qint64 cellOf(double v) const { return qint64(std::floor(v / m_cell)); }
    const QVector<Seg> &m_segs;
    double m_cell;
    QHash<QPair<qint64, qint64>, QVector<int>> m_index;
    QVector<quint32> m_stamp;
    quint32 m_tick = 0;
};

struct Station { QPointF p, t; double s = 0.0; int seg = -1; };
struct Hit { bool ok = false; int line = -1; double dist = 0.0, sB = 0.0; QPointF q; };

/*! Portion of \p pts between arc lengths s0 < s1 (endpoints interpolated). */
QVector<QPointF> subPolyline(const QVector<QPointF> &pts, double s0, double s1)
{
    QVector<QPointF> out;
    double arc = 0.0;
    for (int k = 0; k + 1 < pts.size(); ++k)
    {
        const double len = norm(pts[k + 1] - pts[k]);
        const double e0 = arc, e1 = arc + len;
        arc = e1;
        if (!(len > 0.0) || e1 < s0) continue;
        if (e0 > s1) break;
        if (out.isEmpty()) out.append(pts[k] + (pts[k + 1] - pts[k]) * (std::max(0.0, s0 - e0) / len));
        if (e1 < s1) { if (out.last() != pts[k + 1]) out.append(pts[k + 1]); }
        else { const QPointF q = pts[k] + (pts[k + 1] - pts[k]) * ((s1 - e0) / len); if (out.last() != q) out.append(q); break; }
    }
    return out;
}

struct Candidate
{
    int a = -1, b = -1;
    double sA0 = 0.0, sA1 = 0.0, sB0 = 0.0, sB1 = 0.0, width = 0.0;
};

bool overlaps(const QVector<QPair<double, double>> &used, double s0, double s1)
{
    for (const auto &u : used)
        if (s0 < u.second && u.first < s1) return true;
    return false;
}

} // namespace

QVector<FacingPair> findFacingPairs(const QVector<QVector<QPointF>> &lines,
                                    const QVector<QPair<QPointF, QPointF>> &obstacles,
                                    const FacingPairOptions &opt)
{
    QVector<FacingPair> out;
    if (!(opt.minWidth > 0.0) || !(opt.maxWidth > opt.minWidth) || !(opt.station > 0.0)) return out;

    QVector<Seg> segs;
    QVector<double> lineLen(lines.size(), 0.0);
    QVector<int> firstSeg(lines.size() + 1, 0);   // segs of line i: [firstSeg[i], firstSeg[i+1])
    for (int i = 0; i < lines.size(); ++i)
    {
        firstSeg[i] = segs.size();
        double arc = 0.0;
        for (int k = 0; k + 1 < lines[i].size(); ++k)
        {
            Seg s;
            s.a = lines[i][k]; s.b = lines[i][k + 1];
            s.line = i; s.index = k; s.s0 = arc; s.len = norm(s.b - s.a);
            arc += s.len;
            if (s.len > 0.0) segs.append(s);
        }
        lineLen[i] = arc;
    }
    firstSeg[lines.size()] = segs.size();
    for (const auto &o : obstacles)
    {
        Seg s;
        s.a = o.first; s.b = o.second; s.len = norm(s.b - s.a);
        segs.append(s);
    }
    SegIndex index(segs, 0.5 * opt.maxWidth);
    const double sinMax = std::sin(opt.maxAngleDeg * M_PI / 180.0);

    auto cast = [&](const Station &st, const QPointF &dir, int self) {
        Hit hit;
        const QPointF end = st.p + dir * opt.maxWidth;
        double best = std::numeric_limits<double>::infinity(), bestU = 0.0;
        int bestSeg = -1;
        index.visit(QRectF(st.p, end).normalized(), [&](int i) {
            const Seg &S = segs[i];
            if (S.line == self && std::abs(S.index - segs[st.seg].index) <= 1) return;
            const QPointF e = S.b - S.a;
            const double den = cross(dir, e);
            if (std::abs(den) < 1e-12 * S.len) return;
            const double t = cross(S.a - st.p, e) / den;
            const double u = cross(S.a - st.p, dir) / den;
            if (t > 1e-9 * opt.maxWidth && t <= opt.maxWidth && u >= 0.0 && u <= 1.0 && t < best)
            { best = t; bestSeg = i; bestU = u; }
        });
        if (bestSeg < 0) return hit;
        const Seg &S = segs[bestSeg];
        if (S.line < 0 || S.line == self || best < opt.minWidth) return hit;
        const QPointF tb = (S.b - S.a) / S.len;
        if (std::abs(cross(st.t, tb)) > sinMax) return hit;
        hit.ok = true;
        hit.line = S.line;
        hit.dist = best;
        hit.sB = S.s0 + bestU * S.len;
        hit.q = st.p + dir * best;
        return hit;
    };

    QVector<Candidate> cands;
    for (int i = 0; i < lines.size(); ++i)
    {
        const double L = lineLen[i];
        if (L < opt.minStations * opt.station) continue;
        // Stations at the centre of each step along the line.
        QVector<Station> st;
        {
            int seg = firstSeg[i];
            const int segEnd = firstSeg[i + 1];
            if (seg >= segEnd) continue;
            double segStart = 0.0;
            for (double s = 0.5 * opt.station; s < L; s += opt.station)
            {
                while (seg + 1 < segEnd && segStart + segs[seg].len < s) { segStart += segs[seg].len; ++seg; }
                const Seg &S = segs[seg];
                Station x;
                x.s = s;
                x.seg = seg;
                x.t = (S.b - S.a) / S.len;
                x.p = S.a + x.t * std::clamp(s - segStart, 0.0, S.len);
                st.append(x);
            }
        }
        for (int side : {1, -1})
        {
            QVector<Hit> hits(st.size());
            for (int k = 0; k < st.size(); ++k)
                hits[k] = cast(st[k], QPointF(-st[k].t.y(), st[k].t.x()) * double(side), i);
            int k = 0;
            while (k < st.size())
            {
                if (!hits[k].ok) { ++k; continue; }
                int e = k + 1;
                int dirB = 0;
                while (e < st.size() && hits[e].ok && hits[e].line == hits[k].line
                       && std::abs(hits[e].dist - hits[e - 1].dist) <= opt.widthChange * hits[e - 1].dist)
                {
                    const double dsB = hits[e].sB - hits[e - 1].sB;
                    const int d = dsB > 0.0 ? 1 : (dsB < 0.0 ? -1 : 0);
                    if (std::abs(dsB) > 3.0 * opt.station || (dirB != 0 && d != 0 && d != dirB)) break;
                    if (d != 0) dirB = d;
                    ++e;
                }
                const int runEnd = e;
                QVector<double> w;
                for (int m = k; m < e; ++m) w.append(hits[m].dist);
                std::sort(w.begin(), w.end());
                const double width = w[w.size() / 2];
                bool keep = (st[e - 1].s - st[k].s) >= 3.0 * width;
                // Give up the run's ends (intersections).
                const int trim = int(std::ceil(opt.endTrimFraction * width / opt.station));
                const int k0 = k;
                k += trim;
                e -= trim;
                const int n = e - k;
                keep = keep && n >= opt.minStations;
                if (keep && opt.elevationAt)
                {
                    double zMid = 0.0, zA = 0.0, zB = 0.0;
                    int valid = 0;
                    for (int m = k; m < e; ++m)
                    {
                        const QPointF dir = QPointF(-st[m].t.y(), st[m].t.x()) * double(side);
                        const double off = std::min(0.5 * hits[m].dist, 2.0 * opt.station);
                        const QPointF mid = st[m].p + dir * (0.5 * hits[m].dist);
                        const QPointF outA = st[m].p - dir * off, outB = hits[m].q + dir * off;
                        const double a = opt.elevationAt(mid.x(), mid.y());
                        const double b = opt.elevationAt(outA.x(), outA.y());
                        const double c = opt.elevationAt(outB.x(), outB.y());
                        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) continue;
                        zMid += a; zA += b; zB += c;
                        ++valid;
                    }
                    keep = 2 * valid >= n && zMid < zA && zMid < zB;
                }
                if (keep)
                {
                    Candidate c;
                    c.a = i; c.b = hits[k].line;
                    c.sA0 = st[k].s; c.sA1 = st[e - 1].s;
                    c.sB0 = std::min(hits[k].sB, hits[e - 1].sB);
                    c.sB1 = std::max(hits[k].sB, hits[e - 1].sB);
                    c.width = width;
                    cands.append(c);
                }
                k = std::max(runEnd, k0 + 1);
            }
        }
    }

    std::stable_sort(cands.begin(), cands.end(), [](const Candidate &x, const Candidate &y) {
        return (x.sA1 - x.sA0) > (y.sA1 - y.sA0);
    });
    QVector<QVector<QPair<double, double>>> used(lines.size());
    for (const Candidate &c : std::as_const(cands))
    {
        if (overlaps(used[c.a], c.sA0, c.sA1) || overlaps(used[c.b], c.sB0, c.sB1)) continue;
        FacingPair fp;
        fp.lineA = c.a; fp.lineB = c.b;
        fp.bankA = subPolyline(lines[c.a], c.sA0, c.sA1);
        fp.bankB = subPolyline(lines[c.b], c.sB0, c.sB1);
        fp.width = c.width;
        fp.length = c.sA1 - c.sA0;
        if (fp.bankA.size() < 2 || fp.bankB.size() < 2) continue;
        used[c.a].append(qMakePair(c.sA0, c.sA1));
        used[c.b].append(qMakePair(c.sB0, c.sB1));
        out.append(fp);
    }
    return out;
}

} // namespace mesh
