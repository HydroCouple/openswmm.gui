/*!
 * \file   meshcdt.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Constrained Delaunay triangulation kernel
 * (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md Stage 4). Replaces Triangle
 * for the fringe between the quadtree core and the constraints, and for the
 * natural-neighbour interpolator's Delaunay.
 *
 *  - Incremental insertion in Morton order with walking point location;
 *    Lawson flips restore the Delaunay property. Orientation and in-circle
 *    tests are Shewchuk's adaptive exact predicates (vendor/predicates), so
 *    collinear and cocircular inputs are handled, not guessed.
 *  - Constraint edges are recovered by flipping the edges that cross them
 *    (Sloan 1993); a vertex lying exactly on a constraint splits it there.
 *  - Regions are the connected components of triangles bounded by
 *    constrained edges; removeExterior() drops the component touching the
 *    bounding super-triangle, removeRegionAt() drops the one containing a
 *    point (a hole seed, a core cell centre).
 *  - refine() inserts circumcentres of triangles that are too large for the
 *    local size or too thin, subject to a caller predicate that keeps the
 *    new point clear of constraints; bounded by a hard insertion cap so it
 *    always terminates.
 *
 * Duplicate input points (bit-identical coordinates) map to one vertex.
 * Vertex ids are stable: input point i is vertex i (after dedup mapping).
 */
#ifndef OPENSWMMVIS_MESH_MESHCDT_H
#define OPENSWMMVIS_MESH_MESHCDT_H

#include <QHash>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

namespace mesh {

class ConstrainedDelaunay
{
public:
    struct Triangle
    {
        int  v[3];            ///< Counter-clockwise.
        int  adj[3];          ///< Neighbour across the edge opposite v[i] (v[i+1], v[i+2]); -1 = none.
        bool constrained[3];  ///< Edge opposite v[i] is a constraint.
        bool alive;
    };

    /*! \brief Triangulate \p points. \p vertexOfPoint receives, per input
     *  point, its vertex id (duplicates share one). Returns false on fewer
     *  than 3 distinct non-collinear points. */
    bool build(const QVector<QPointF> &points, QVector<int> *vertexOfPoint = nullptr);

    /*! \brief Force the edge between two vertex ids into the triangulation
     *  and mark it constrained. Returns false when the edge cannot be
     *  recovered (it would cross another constraint). */
    bool insertConstraint(int a, int b);

    /*! \brief Insert a new vertex at \p p (inside the current hull). Returns
     *  its id, or -1 when the point is outside every live triangle. */
    int insertPoint(const QPointF &p);

    /*! \brief Drop every triangle reachable from the super-triangle without
     *  crossing a constrained edge (the exterior of a constrained boundary). */
    void removeExterior();
    /*! \brief Drop only the triangles touching a super-triangle vertex,
     *  leaving the convex hull of the input (unconstrained use). */
    void removeSuperTriangles();
    /*! \brief Drop the region (constraint-bounded component) containing \p p. */
    void removeRegionAt(const QPointF &p);

    /*! \brief Size-and-quality refinement. A live triangle is split at its
     *  circumcentre when its circumradius exceeds hAt(centre) or its
     *  smallest angle is under \p minAngleDeg, provided \p allowed(centre)
     *  holds (the caller keeps points clear of constraints) and the centre
     *  falls in a live triangle. Stops after \p maxInsertions.
     *  Returns the number of vertices inserted. */
    int refine(const std::function<double(double, double)> &hAt,
               double minAngleDeg,
               const std::function<bool(const QPointF &)> &allowed,
               int maxInsertions,
               const QVector<double> *sizeHint = nullptr);
    /*! \p sizeHint (optional, indexed by vertex id, 0 = none): a triangle is
     *  also split when its longest edge exceeds 1.9× the smallest hint among
     *  its vertices — how the fringe stays within the grading bound of the
     *  core cells it touches. */

    // ── Read-out ───────────────────────────────────────────────────────
    [[nodiscard]] const QVector<QPointF> &vertices() const { return m_pts; }
    [[nodiscard]] const QVector<Triangle> &triangles() const { return m_tris; }
    [[nodiscard]] int liveTriangleCount() const;
    /*! \brief Index of the live triangle containing \p p (on an edge counts), -1 = none. */
    [[nodiscard]] int locate(const QPointF &p) const;
    /*! \brief True when the (min,max) vertex pair is a constrained edge. */
    [[nodiscard]] bool isConstrained(int a, int b) const;
    /*! \brief The vertices from a to b along the constrained edges that
     *  realise the constraint (a, b) — {a, b} when it is one edge, more when
     *  vertices lying on the segment split it. Empty when no such chain. */
    [[nodiscard]] QVector<int> constrainedChain(int a, int b) const;
    [[nodiscard]] bool isSuperVertex(int v) const { return v >= m_superBase && v < m_superBase + 3; }
    [[nodiscard]] QString errorMsg() const { return m_errorMsg; }

private:
    double orient(int a, int b, int c) const;
    double orient(int a, int b, const QPointF &p) const;
    bool   inCircle(int a, int b, int c, int d) const;
    int    findTriangle(const QPointF &p, int start) const;
    int    triangleWithEdge(int a, int b, int *edgeIndex) const;
    void   legalize(int t, int e);
    void   flip(int t, int e);
    void   setAdj(int t, int e, int n);
    void   splitTriangle(int t, int v);
    void   splitEdge(int t, int e, int v);
    int    edgeIndex(int t, int v) const;
    void   trianglesAround(int v, QVector<int> *out) const;
    void   markConstrained(int a, int b);

    QVector<QPointF>  m_pts;
    QVector<Triangle> m_tris;
    QVector<int>      m_vertexTri;   ///< Some live triangle incident to each vertex.
    int               m_superBase = 0;
    int               m_lastLocate = 0;
    QString           m_errorMsg;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCDT_H
