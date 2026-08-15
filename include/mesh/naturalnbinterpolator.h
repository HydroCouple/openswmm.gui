/*!
 * \file   naturalnbinterpolator.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Natural-neighbour interpolation over a sparse set of scattered seed
 * points (x, y, z).  Used as an alternative to inverse-distance weighting
 * for the no-DTM elevation fallback in the mesh generator: when no terrain
 * raster is supplied, mesh-vertex z is interpolated from SWMM junction rim
 * elevations and 3D feature-Z seeds.
 *
 * The seeds are triangulated once with Shewchuk's Triangle (the same
 * vendored library the MeshGenerator uses) to obtain a Delaunay
 * triangulation + neighbour adjacency.  Each query then performs a local
 * insertion (Watson's method) to recover the natural neighbours of the
 * query point and their weights:
 *
 *   Sibson  — area-stealing coordinates: weight_i = (Voronoi area the query
 *             cell steals from neighbour i) / (total stolen area).  C1-smooth.
 *   Laplace — non-Sibsonian "edge" coordinates: weight_i = (length of the
 *             Voronoi facet between query and neighbour i) / ||query - p_i||.
 *
 * Natural-neighbour interpolation is only defined inside the convex hull of
 * the seeds.  interpolate() returns NaN outside the hull and on any
 * degeneracy so the caller can fall back to IDW.
 *
 * Thread-safety: interpolate() mutates a walk-start cache (m_lastTri), so a
 * single instance is NOT safe for concurrent queries.  It is, however, safe
 * to build and query off the GUI thread (no Qt-widget access) — which is how
 * the QtConcurrent mesh worker uses it, sequentially.
 */
#ifndef OPENSWMMVIS_MESH_NATURALNBINTERPOLATOR_H
#define OPENSWMMVIS_MESH_NATURALNBINTERPOLATOR_H

#include <QPointF>
#include <QString>
#include <QVector>

#include <vector>

namespace mesh {

class NaturalNeighbourInterpolator
{
public:
    enum class Variant { Sibson, Laplace };

    NaturalNeighbourInterpolator() = default;

    void setVariant(Variant v) { m_variant = v; }
    Variant variant() const { return m_variant; }

    /*! \brief Triangulate the seed set.  Snap-dedupes coincident points,
     *         requires >= 3 unique non-collinear points.  Returns false (and
     *         sets *err when non-null) when interpolation is unavailable — the
     *         caller should then fall back to IDW entirely. */
    bool build(const QVector<QPointF> &pts, const QVector<double> &z, QString *err);

    bool isValid() const { return m_valid; }

    /*! \brief Interpolate z at (x, y).  Returns NaN when the point is outside
     *         the seed convex hull or the local insertion is degenerate — the
     *         caller falls back to IDW for that vertex. */
    double interpolate(double x, double y) const;

private:
    // Coordinates are normalised to a local [0,~1] space: translated by
    // (-m_ox, -m_oy) (the seed bbox minimum) then divided by m_scale (the
    // larger bbox side).  This keeps orientation / in-circle / circumcenter
    // determinants well-conditioned at projected-CRS magnitudes.  z, and the
    // Sibson (area-ratio) / Laplace (length-ratio) weights, are invariant
    // under this affine map.
    int locate(double qx, double qy) const;   // returns triangle index or -1 (outside hull)

    Variant m_variant = Variant::Sibson;
    bool    m_valid   = false;

    double  m_ox = 0.0, m_oy = 0.0;            // local-origin translation
    double  m_scale = 1.0;                     // normalisation scale (max bbox side)

    std::vector<double> m_px, m_py, m_pz;      // per Delaunay vertex (translated x,y; raw z)
    std::vector<int>    m_tris;                // 3 * numTri vertex indices (CCW)
    std::vector<int>    m_nbrs;                // 3 * numTri neighbour tri indices (-1 = hull)
    std::vector<double> m_ccx, m_ccy;          // circumcenter per triangle (NaN if degenerate)
    int                 m_numTri = 0;

    mutable int         m_lastTri = 0;         // jump-and-walk start hint
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_NATURALNBINTERPOLATOR_H
