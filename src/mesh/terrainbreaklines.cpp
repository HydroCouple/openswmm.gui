/*!
 * \file   terrainbreaklines.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Terrain break-line extraction — see terrainbreaklines.h.
 */
#include "mesh/terrainbreaklines.h"

#include <algorithm>
#include <cmath>

namespace mesh {

namespace {

constexpr quint8 kNone = 0, kWeak = 1, kStrong = 2, kLine = 3, kTraced = 4;
constexpr quint8 kStateMask = 0x07;
inline quint8 stateOf(quint8 v) { return v & kStateMask; }
inline quint8 withState(quint8 v, quint8 st) { return quint8((v & ~kStateMask) | st); }

// Quantised curvature directions (col, row) for NMS: 0°, 45°, 90°, 135°.
constexpr int kNmsDc[4] = {1, 1, 0, -1};
constexpr int kNmsDr[4] = {0, 1, 1, 1};

// 8-neighbourhood in angular order (k · 45°, rows grow downward).
constexpr int kDc[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int kDr[8] = {0, 1, 1, 1, 0, -1, -1, -1};

} // namespace

void TerrainBreaklineExtractor::begin(int cols, int rows, const TerrainBreaklineOptions &opt)
{
    m_opt = opt;
    m_cols = std::max(0, cols);
    m_rows = std::max(0, rows);
    m_pushed = m_lambdaDone = m_nmsDone = 0;
    for (int i = 0; i < 3; ++i)
    {
        m_z[i].fill(0.0f, m_cols);
        m_mag[i].fill(0.0f, m_cols);
        m_dir[i].fill(0, m_cols);
    }
    m_skipped = qint64(m_cols) * m_rows > opt.maxPixels;
    if (m_skipped) { m_cols = m_rows = 0; m_cls.clear(); }
    else m_cls.fill(kNone, qsizetype(m_cols) * m_rows);
    m_high = float(opt.tolerance);
    m_low  = float(opt.tolerance * std::clamp(opt.lowRatio, 0.0, 1.0));
}

void TerrainBreaklineExtractor::computeLambdaRow(int r)
{
    float *mag = m_mag[r % 3].data();
    quint8 *dir = m_dir[r % 3].data();
    std::fill(mag, mag + m_cols, 0.0f);
    std::fill(dir, dir + m_cols, quint8(0));
    if (r <= 0 || r >= m_rows - 1 || m_cols < 3) return;
    const float *a = m_z[(r - 1) % 3].constData();
    const float *b = m_z[r % 3].constData();
    const float *d = m_z[(r + 1) % 3].constData();
    const float low = m_low;
    for (int c = 1; c + 1 < m_cols; ++c)
    {
        // Second differences with (1,2,1) averaging along the other axis;
        // Dxx touches all nine values, so a NaN anywhere makes it non-finite.
        const float dxx = ((a[c + 1] - 2.0f * a[c] + a[c - 1])
                           + 2.0f * (b[c + 1] - 2.0f * b[c] + b[c - 1])
                           + (d[c + 1] - 2.0f * d[c] + d[c - 1])) * 0.25f;
        if (!std::isfinite(dxx)) continue;
        const float dyy = ((d[c - 1] - 2.0f * b[c - 1] + a[c - 1])
                           + 2.0f * (d[c] - 2.0f * b[c] + a[c])
                           + (d[c + 1] - 2.0f * b[c + 1] + a[c + 1])) * 0.25f;
        const float dxy = (d[c + 1] - d[c - 1] - a[c + 1] + a[c - 1]) * 0.25f;
        // |λ| of the larger-magnitude eigenvalue = |m| + q. Most pixels sit
        // below the hysteresis floor and never need the direction.
        const float m = 0.5f * (dxx + dyy);
        const float h = 0.5f * (dxx - dyy);
        const float q2 = h * h + dxy * dxy;
        const float am = std::abs(m);
        if (am < low && (low - am) * (low - am) > q2) continue;
        const float q = std::sqrt(q2);
        const float v = am + q;
        if (!(v >= low)) continue;
        // Eigenvector of m + q: (h + q, dxy) or, when that degenerates,
        // (dxy, q − h); the m − q eigenvector is its perpendicular.
        float vx = h + q, vy = dxy;
        if (vx * vx + vy * vy < dxy * dxy + (q - h) * (q - h)) { vx = dxy; vy = q - h; }
        if (m < 0.0f) { const float t = vx; vx = -vy; vy = t; }
        // Quantise the axis (mod 180°) to 0°, 45°, 90°, 135° without atan2.
        if (vx < 0.0f) { vx = -vx; vy = -vy; }
        constexpr float kTan22 = 0.41421356f;
        quint8 k;
        if (std::abs(vy) <= kTan22 * vx) k = 0;
        else if (vx <= kTan22 * std::abs(vy)) k = 2;
        else k = vy > 0.0f ? 1 : 3;
        mag[c] = v;
        dir[c] = k;
    }
}

void TerrainBreaklineExtractor::suppressRow(int r)
{
    auto magAt = [&](int rr, int cc) -> float {
        if (rr < 0 || rr >= m_rows || cc < 0 || cc >= m_cols) return 0.0f;
        return m_mag[rr % 3][cc];
    };
    const float *mag = m_mag[r % 3].constData();
    const quint8 *dir = m_dir[r % 3].constData();
    quint8 *cls = m_cls.data() + qsizetype(r) * m_cols;
    for (int c = 0; c < m_cols; ++c)
    {
        const float v = mag[c];
        if (!(v > 0.0f) || v < m_low) continue;
        const int k = dir[c];
        const float prev = magAt(r - kNmsDr[k], c - kNmsDc[k]);
        const float next = magAt(r + kNmsDr[k], c + kNmsDc[k]);
        // Asymmetric: a step's two equal responses keep exactly one pixel.
        if (v >= prev && v > next)
        {
            // Sub-pixel peak of the parabola through (−1, prev), (0, v),
            // (1, next): −0.5 for a step's tie, 0 for a symmetric crease.
            const float den = prev - 2.0f * v + next;
            const float off = den < 0.0f ? 0.5f * (prev - next) / den : 0.0f;
            const int q = std::clamp(int(std::lround(off * 8.0f)) + 4, 0, 7);
            cls[c] = quint8((v >= m_high ? kStrong : kWeak) | (k << 3) | (q << 5));
        }
    }
}

void TerrainBreaklineExtractor::pushRow(const float *z)
{
    if (!z || m_pushed >= m_rows || m_cols <= 0) return;
    std::copy(z, z + m_cols, m_z[m_pushed % 3].begin());
    ++m_pushed;
    // λ(r) needs z(r+1); the first and last rows are zero.
    auto ready = [&](int r) { return r == 0 ? m_pushed >= 1 : (r == m_rows - 1 ? m_pushed >= m_rows : m_pushed >= r + 2); };
    while (m_lambdaDone < m_rows && ready(m_lambdaDone))
    {
        computeLambdaRow(m_lambdaDone++);
        // NMS(r) needs λ(r+1); run it before λ(r+2) reuses the slot of λ(r−1).
        while (m_nmsDone < m_lambdaDone - 1) suppressRow(m_nmsDone++);
    }
}

void TerrainBreaklineExtractor::hysteresis()
{
    const qsizetype n = m_cls.size();
    QVector<qsizetype> stack;
    quint8 *cls = m_cls.data();
    for (qsizetype i = 0; i < n; ++i)
    {
        if (stateOf(cls[i]) != kStrong) continue;
        cls[i] = withState(cls[i], kLine);
        stack.append(i);
        while (!stack.isEmpty())
        {
            const qsizetype p = stack.takeLast();
            const int r = int(p / m_cols), c = int(p % m_cols);
            for (int k = 0; k < 8; ++k)
            {
                const int rr = r + kDr[k], cc = c + kDc[k];
                if (rr < 0 || rr >= m_rows || cc < 0 || cc >= m_cols) continue;
                const qsizetype q = qsizetype(rr) * m_cols + cc;
                if (stateOf(cls[q]) == kWeak || stateOf(cls[q]) == kStrong) { cls[q] = withState(cls[q], kLine); stack.append(q); }
            }
        }
    }
    for (qsizetype i = 0; i < n; ++i) if (stateOf(cls[i]) != kLine) cls[i] = kNone;

    // Bridge one-pixel gaps: suppression drops the corner pixel where a wall
    // turns, which would break a building outline into pieces. A line end
    // (one line neighbour or none) joins the first line pixel two steps away
    // that is not already reachable through its own neighbour, by filling
    // the pixel between them.
    auto isLine = [&](int c, int r) {
        return r >= 0 && r < m_rows && c >= 0 && c < m_cols && stateOf(cls[qsizetype(r) * m_cols + c]) == kLine;
    };
    for (int r = 0; r < m_rows; ++r)
    {
        const quint8 *row = cls + qsizetype(r) * m_cols;
        for (int c = 0; c < m_cols; ++c)
        {
            if (stateOf(row[c]) != kLine) continue;
            int nb = 0, nbc = 0, nbr = 0;
            for (int k = 0; k < 8; ++k)
                if (isLine(c + kDc[k], r + kDr[k])) { ++nb; nbc = c + kDc[k]; nbr = r + kDr[k]; }
            if (nb > 1) continue;
            bool bridged = false;
            for (int dr = -2; dr <= 2 && !bridged; ++dr)
                for (int dc = -2; dc <= 2 && !bridged; ++dc)
                {
                    if (std::max(std::abs(dc), std::abs(dr)) != 2) continue;
                    const int qc = c + dc, qr = r + dr;
                    if (!isLine(qc, qr)) continue;
                    if (nb == 1 && std::abs(qc - nbc) <= 1 && std::abs(qr - nbr) <= 1) continue;   // own chain
                    const int mc = c + dc / 2, mr = r + dr / 2;
                    cls[qsizetype(mr) * m_cols + mc] = quint8(kLine | (4 << 5));   // bridge: no offset
                    bridged = true;
                }
        }
    }
}

QVector<QVector<QPointF>> TerrainBreaklineExtractor::trace()
{
    quint8 *cls = m_cls.data();
    auto isLine = [&](int c, int r) {
        return r >= 0 && r < m_rows && c >= 0 && c < m_cols && stateOf(cls[qsizetype(r) * m_cols + c]) == kLine;
    };
    auto mark = [&](int c, int r) { quint8 &v = cls[qsizetype(r) * m_cols + c]; v = withState(v, kTraced); };
    // Pixel centre moved to the sub-pixel peak across the line.
    auto pointAt = [&](int c, int r) {
        const quint8 v = cls[qsizetype(r) * m_cols + c];
        const int k = (v >> 3) & 3;
        const double off = (((v >> 5) & 7) - 4) / 8.0;
        return QPointF(c + 0.5 + off * kNmsDc[k], r + 0.5 + off * kNmsDr[k]);
    };
    // Greedy walk from (c, r): straightest unvisited continuation first,
    // axis steps before diagonals; a diagonal step absorbs its corner pixels
    // so a staircase does not leave one-pixel stubs behind.
    auto walk = [&](int c, int r, int prevDir, QVector<QPoint> *out) {
        int d = prevDir;
        for (;;)
        {
            int best = -1, bestScore = 1 << 20;
            for (int k = 0; k < 8; ++k)
            {
                if (!isLine(c + kDc[k], r + kDr[k])) continue;
                int score;
                if (d < 0) score = (k & 1);
                else
                {
                    const int t = std::abs(k - d);
                    score = 2 * std::min(t, 8 - t) + (k & 1);
                }
                if (score < bestScore) { bestScore = score; best = k; }
            }
            if (best < 0) return;
            if (best & 1)
            {
                if (isLine(c + kDc[best], r)) mark(c + kDc[best], r);
                if (isLine(c, r + kDr[best])) mark(c, r + kDr[best]);
            }
            c += kDc[best]; r += kDr[best];
            mark(c, r);
            out->append(QPoint(c, r));
            d = best;
        }
    };

    QVector<QVector<QPointF>> chains;
    for (int r = 0; r < m_rows; ++r)
        for (int c = 0; c < m_cols; ++c)
        {
            if (stateOf(cls[qsizetype(r) * m_cols + c]) != kLine) continue;
            mark(c, r);
            QVector<QPoint> fwd, bwd;
            walk(c, r, -1, &fwd);
            int back = -1;
            if (!fwd.isEmpty())
            {
                const QPoint s = fwd.first() - QPoint(c, r);
                for (int k = 0; k < 8; ++k) if (kDc[k] == s.x() && kDr[k] == s.y()) { back = (k + 4) % 8; break; }
            }
            walk(c, r, back, &bwd);
            const int total = int(fwd.size() + bwd.size()) + 1;
            if (total < std::max(2, m_opt.minPixels)) continue;
            QVector<QPointF> chain;
            chain.reserve(total + 1);
            for (int i = bwd.size() - 1; i >= 0; --i) chain.append(pointAt(bwd[i].x(), bwd[i].y()));
            chain.append(pointAt(c, r));
            for (const QPoint &p : fwd) chain.append(pointAt(p.x(), p.y()));
            // A loop comes back next to where it started (its end pixels are
            // 8-neighbours): close it.
            const QPoint first = bwd.isEmpty() ? QPoint(c, r) : bwd.last();
            const QPoint last = fwd.isEmpty() ? QPoint(c, r) : fwd.last();
            if (chain.size() >= 8 && std::abs(last.x() - first.x()) <= 1 && std::abs(last.y() - first.y()) <= 1)
                chain.append(chain.first());
            chains.append(std::move(chain));
        }
    return chains;
}

QVector<QVector<QPointF>> TerrainBreaklineExtractor::finish()
{
    if (m_skipped) return {};
    if (m_pushed < m_rows) m_rows = m_pushed;
    if (m_rows < 3 || m_cols < 3 || !(m_high > 0.0f)) { m_cls.clear(); return {}; }
    while (m_lambdaDone < m_rows)
    {
        computeLambdaRow(m_lambdaDone++);
        while (m_nmsDone < m_lambdaDone - 1) suppressRow(m_nmsDone++);
    }
    while (m_nmsDone < m_rows) suppressRow(m_nmsDone++);
    hysteresis();
    QVector<QVector<QPointF>> chains = trace();
    m_cls.clear();
    m_cls.squeeze();
    return chains;
}

QVector<QVector<QPointF>> TerrainBreaklineExtractor::extractFromGrid(const float *z, int cols, int rows,
                                                                     const TerrainBreaklineOptions &opt)
{
    TerrainBreaklineExtractor e;
    e.begin(cols, rows, opt);
    if (z)
        for (int r = 0; r < rows; ++r) e.pushRow(z + qsizetype(r) * cols);
    return e.finish();
}

} // namespace mesh
