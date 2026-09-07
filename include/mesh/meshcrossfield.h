/*!
 * \file   meshcrossfield.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Boundary-aligned 2D cross field on a regular grid
 * (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.4b).
 *
 * A cross (4-RoSy) at angle θ is represented by u = (cos 4θ, sin 4θ), which
 * is invariant under θ → θ + 90°. The field is the discrete harmonic
 * interpolant of Dirichlet data laid along "aligned polylines" (the region
 * ring, interior constraint segments, an optional guide): every grid cell a
 * polyline passes through is pinned to that segment's tangent; every other
 * cell of the bounding-box grid is an unknown solved by SOR on the 5-point
 * Laplacian. No inside/outside mask is needed — exterior cells simply carry
 * harmless values. No third-party solver (plan D2/D9).
 *
 * Query: bilinear interpolation of u, then θ = atan2(u_y, u_x) / 4 folded to
 * [0, π/2). Where |u| collapses (a singularity) θ is still returned — callers
 * accept the resulting irregular lattice there.
 */
#ifndef OPENSWMMVIS_MESH_MESHCROSSFIELD_H
#define OPENSWMMVIS_MESH_MESHCROSSFIELD_H

#include <QPointF>
#include <QRectF>
#include <QVector>

namespace mesh {

class CrossField
{
public:
    struct Options
    {
        double pitch     = 0.0;    ///< Grid spacing (map units). Required > 0 (use the region's h).
        int    maxSweeps = 4000;   ///< SOR sweep cap.
        double tol       = 1e-5;   ///< Stop when the max |Δu| over a sweep drops below this.
        double omega     = 1.7;    ///< SOR relaxation.
        double margin    = 2.0;    ///< Cells of padding around bbox on every side.
    };

    CrossField() = default;

    /*! \brief Solve the field over \p bbox with Dirichlet data along
     *  \p alignedPolylines (>= 2 points each; a closed ring passes its closing
     *  edge explicitly or via last==first). Returns false when pitch <= 0, bbox
     *  is empty, no polyline pins any cell, or the grid would exceed
     *  4e6 cells. */
    bool build(const QRectF &bbox, const QVector<QVector<QPointF>> &alignedPolylines,
               const Options &opts);

    /*! \brief Constant field: every query returns \p thetaDeg (folded). valid() → true. */
    void setConstant(double thetaDeg);

    bool valid() const noexcept { return m_valid; }
    int  cols()  const noexcept { return m_cols; }
    int  rows()  const noexcept { return m_rows; }
    int  sweepsUsed() const noexcept { return m_sweeps; }

    /*! \brief Field angle in radians, folded to [0, π/2). Clamps outside the grid. */
    double thetaAt(double x, double y) const;

    /*! \brief The four unit directions θ, θ+90°, θ+180°, θ+270° at (x, y). */
    void directionsAt(double x, double y, QPointF d[4]) const;

    /*! \brief Raw representation vector (cos 4θ, sin 4θ) at a cell (tests). */
    QPointF cellU(int col, int row) const;
    bool    cellPinned(int col, int row) const;

private:
    bool    m_valid = false;
    bool    m_constant = false;
    double  m_constTheta = 0.0;
    int     m_cols = 0, m_rows = 0, m_sweeps = 0;
    double  m_x0 = 0.0, m_y0 = 0.0, m_pitch = 0.0;   ///< Centre of cell (0,0) and spacing.
    QVector<double> m_ux, m_uy;                       ///< Row-major, cols*rows.
    QVector<bool>   m_pinned;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCROSSFIELD_H
