/*!
 * \file   meshcrossfield.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Boundary-aligned 2D cross field on a regular grid (QUAD_MESHING_REDESIGN_PLAN
 * §4.4b). The 4-RoSy representation u = (cos 4θ, sin 4θ) is pinned along the
 * aligned polylines and harmonically extended by SOR on the 5-point Laplacian
 * with mirrored (Neumann) grid edges. Hand-written; no third-party solver.
 */
#include "mesh/meshcrossfield.h"

#include <algorithm>
#include <cmath>

namespace mesh {

namespace {

constexpr double kHalfPi = M_PI / 2.0;

/*! Fold an angle (radians) into [0, π/2). */
double foldQuarter(double theta) noexcept
{
    double t = std::fmod(theta, kHalfPi);
    if (t < 0.0) t += kHalfPi;
    if (t >= kHalfPi) t -= kHalfPi;
    return t;
}

} // namespace

bool CrossField::build(const QRectF &bbox, const QVector<QVector<QPointF>> &alignedPolylines,
                       const Options &opts)
{
    m_valid = false;
    m_constant = false;
    m_sweeps = 0;
    m_cols = m_rows = 0;
    m_ux.clear(); m_uy.clear(); m_pinned.clear();

    if (!(opts.pitch > 0.0) || !std::isfinite(opts.pitch)) return false;
    if (bbox.isEmpty() || !std::isfinite(bbox.width()) || !std::isfinite(bbox.height())) return false;

    const int margin = std::max(0, int(std::ceil(std::max(0.0, opts.margin))));
    m_pitch = opts.pitch;
    m_x0 = bbox.left() - margin * m_pitch;
    m_y0 = bbox.top()  - margin * m_pitch;
    const double colsD = std::ceil(bbox.width()  / m_pitch) + 1.0 + 2.0 * margin;
    const double rowsD = std::ceil(bbox.height() / m_pitch) + 1.0 + 2.0 * margin;
    if (colsD * rowsD > 4e6) return false;
    m_cols = int(colsD);
    m_rows = int(rowsD);
    const int nCells = m_cols * m_rows;

    m_ux.fill(0.0, nCells);
    m_uy.fill(0.0, nCells);
    m_pinned.fill(false, nCells);
    QVector<int> count(nCells, 0);

    // ── Pin: walk every segment in steps of pitch/2 ──────────────────────
    auto cellOf = [this](const QPointF &p, int &c, int &r) {
        c = int(std::lround((p.x() - m_x0) / m_pitch));
        r = int(std::lround((p.y() - m_y0) / m_pitch));
        c = std::clamp(c, 0, m_cols - 1);
        r = std::clamp(r, 0, m_rows - 1);
    };
    int nPinned = 0;
    for (const QVector<QPointF> &pl : alignedPolylines)
    {
        for (int i = 1; i < pl.size(); ++i)
        {
            const QPointF a = pl[i - 1], b = pl[i];
            const QPointF d = b - a;
            const double len = std::hypot(d.x(), d.y());
            if (!(len > 0.0) || !std::isfinite(len)) continue;
            const double theta = std::atan2(d.y(), d.x());
            const double ux = std::cos(4.0 * theta), uy = std::sin(4.0 * theta);
            const int steps = std::max(1, int(std::ceil(len / (0.5 * m_pitch))));
            for (int s = 0; s <= steps; ++s)
            {
                const QPointF p = a + d * (double(s) / steps);
                int c, r;
                cellOf(p, c, r);
                const int idx = r * m_cols + c;
                if (!m_pinned[idx]) { m_pinned[idx] = true; ++nPinned; }
                m_ux[idx] += ux;
                m_uy[idx] += uy;
                ++count[idx];
            }
        }
    }
    if (nPinned == 0) return false;

    double meanX = 0.0, meanY = 0.0;
    for (int i = 0; i < nCells; ++i)
    {
        if (!m_pinned[i]) continue;
        m_ux[i] /= count[i];
        m_uy[i] /= count[i];
        const double nrm = std::hypot(m_ux[i], m_uy[i]);
        if (nrm > 1e-12) { m_ux[i] /= nrm; m_uy[i] /= nrm; }
        meanX += m_ux[i];
        meanY += m_uy[i];
    }
    meanX /= nPinned;
    meanY /= nPinned;
    for (int i = 0; i < nCells; ++i)
        if (!m_pinned[i]) { m_ux[i] = meanX; m_uy[i] = meanY; }

    // ── Solve: SOR on the 5-point Laplacian, Neumann (mirror) at the edges ─
    const double omega = (opts.omega > 0.0 && opts.omega < 2.0) ? opts.omega : 1.0;
    const int maxSweeps = std::max(0, opts.maxSweeps);
    for (int sweep = 0; sweep < maxSweeps; ++sweep)
    {
        double maxDelta = 0.0;
        for (int r = 0; r < m_rows; ++r)
        {
            const int rm = r > 0 ? r - 1 : (m_rows > 1 ? r + 1 : r);
            const int rp = r < m_rows - 1 ? r + 1 : (m_rows > 1 ? r - 1 : r);
            for (int c = 0; c < m_cols; ++c)
            {
                const int idx = r * m_cols + c;
                if (m_pinned[idx]) continue;
                const int cm = c > 0 ? c - 1 : (m_cols > 1 ? c + 1 : c);
                const int cp = c < m_cols - 1 ? c + 1 : (m_cols > 1 ? c - 1 : c);
                const int iL = r * m_cols + cm, iR = r * m_cols + cp;
                const int iD = rm * m_cols + c, iU = rp * m_cols + c;
                const double gx = 0.25 * (m_ux[iL] + m_ux[iR] + m_ux[iD] + m_ux[iU]);
                const double gy = 0.25 * (m_uy[iL] + m_uy[iR] + m_uy[iD] + m_uy[iU]);
                const double nx = m_ux[idx] + omega * (gx - m_ux[idx]);
                const double ny = m_uy[idx] + omega * (gy - m_uy[idx]);
                maxDelta = std::max({maxDelta, std::abs(nx - m_ux[idx]), std::abs(ny - m_uy[idx])});
                m_ux[idx] = nx;
                m_uy[idx] = ny;
            }
        }
        m_sweeps = sweep + 1;
        if (maxDelta < opts.tol) break;
    }

    m_valid = true;
    return true;
}

void CrossField::setConstant(double thetaDeg)
{
    m_constant = true;
    m_valid = true;
    m_constTheta = foldQuarter(thetaDeg * M_PI / 180.0);
}

double CrossField::thetaAt(double x, double y) const
{
    if (m_constant) return m_constTheta;
    if (!m_valid || m_cols <= 0 || m_rows <= 0) return 0.0;

    double gx = (x - m_x0) / m_pitch, gy = (y - m_y0) / m_pitch;
    if (!std::isfinite(gx)) gx = 0.0;
    if (!std::isfinite(gy)) gy = 0.0;
    gx = std::clamp(gx, 0.0, double(m_cols - 1));
    gy = std::clamp(gy, 0.0, double(m_rows - 1));

    int c0 = std::min(int(std::floor(gx)), std::max(0, m_cols - 2));
    int r0 = std::min(int(std::floor(gy)), std::max(0, m_rows - 2));
    const int c1 = std::min(c0 + 1, m_cols - 1), r1 = std::min(r0 + 1, m_rows - 1);
    const double fx = std::clamp(gx - c0, 0.0, 1.0), fy = std::clamp(gy - r0, 0.0, 1.0);

    auto at = [this](const QVector<double> &v, int c, int r) { return v[r * m_cols + c]; };
    const double ux = (1 - fx) * (1 - fy) * at(m_ux, c0, r0) + fx * (1 - fy) * at(m_ux, c1, r0)
                    + (1 - fx) * fy * at(m_ux, c0, r1) + fx * fy * at(m_ux, c1, r1);
    const double uy = (1 - fx) * (1 - fy) * at(m_uy, c0, r0) + fx * (1 - fy) * at(m_uy, c1, r0)
                    + (1 - fx) * fy * at(m_uy, c0, r1) + fx * fy * at(m_uy, c1, r1);
    return foldQuarter(std::atan2(uy, ux) / 4.0);
}

void CrossField::directionsAt(double x, double y, QPointF d[4]) const
{
    const double theta = thetaAt(x, y);
    for (int k = 0; k < 4; ++k)
    {
        const double a = theta + k * kHalfPi;
        d[k] = QPointF(std::cos(a), std::sin(a));
    }
}

QPointF CrossField::cellU(int col, int row) const
{
    if (m_constant)
        return QPointF(std::cos(4.0 * m_constTheta), std::sin(4.0 * m_constTheta));
    if (col < 0 || row < 0 || col >= m_cols || row >= m_rows) return QPointF();
    return QPointF(m_ux[row * m_cols + col], m_uy[row * m_cols + col]);
}

bool CrossField::cellPinned(int col, int row) const
{
    if (m_constant || col < 0 || row < 0 || col >= m_cols || row >= m_rows) return false;
    return m_pinned[row * m_cols + col];
}

} // namespace mesh
