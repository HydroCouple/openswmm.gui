/*!
 * \file   meshcdt.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Constrained Delaunay triangulation kernel
 * (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md Stage 4). Replaces Triangle
 * for the mesh generator and for the natural-neighbour interpolator's
 * Delaunay.
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
 *  - refineQuality() is Delaunay refinement with Ruppert's and Shewchuk's
 *    rules (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §4): encroached
 *    subsegments are split (concentric shells at acute input corners), bad
 *    triangles receive an off-centre or circumcentre, and a point that would
 *    encroach a subsegment splits the subsegment instead. Every triangle ends
 *    with a minimum angle >= the bound except in the wedge of two segments
 *    meeting at a small input angle and next to fixed (unsplittable) edges.
 *
 * Duplicate input points (bit-identical coordinates) map to one vertex.
 * Vertex ids are stable: input point i is vertex i (after dedup mapping).
 */
#ifndef OPENSWMMVIS_MESH_MESHCDT_H
#define OPENSWMMVIS_MESH_MESHCDT_H

#include <QHash>
#include <QPair>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <limits>

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

    /*! \brief Exact orientation of \p c against the line \p a->\p b:
     *  positive left, negative right, exactly zero when collinear. */
    [[nodiscard]] static double orientExact(const QPointF &a, const QPointF &b, const QPointF &c);

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
    int removeRegionLeftOf(int a, int b);
    [[nodiscard]] int inputVertexCount() const { return m_superBase; }

    int removeRegionAt(const QPointF &p);   ///< Returns how many triangles it removed.

    /*! \brief Options for refineQuality(). */
    struct QualityOptions
    {
        /*! Target edge length h at (x, y): a triangle is split when its area
         *  exceeds the equilateral triangle of side h(centroid). Null, or
         *  <= 0 at a point, = no size bound there. */
        std::function<double(double, double)> hAt;
        /*! Actual surface error at the reference DEM samples covered by a
         * triangle. Return a failing residual and its source point; NaN means
         * unverified coverage. Elevations are cached per vertex. */
        std::function<double(const QPointF *, const double *, QPointF *)> terrainError;
        std::function<double(double, double)> terrainElevationAt;
        double terrainTolerance = 0.0;
        double terrainMinSpacing = 0.0;
        /*! Refine size and angle first, then insert terrain points worst
         *  error first until the tolerance or maxTriangles is reached, so a
         *  capped run spends its budget where the surface error is largest.
         *  false = terrain is checked inline with size and angle (default). */
        bool terrainWorstFirst = false;
        /*! After refinement, measure every live triangle's terrain error for
         *  Report::terrainUnresolved / terrainUnknown / maxTerrainError. A
         *  full exact read of the DEM; false leaves those counts at zero for a
         *  caller that verifies the finished mesh itself. */
        bool terrainFinalCheck = true;
        /*! Passes of non-degrading smoothing after size and angle refinement:
         *  each free vertex (inserted after build(), on no constraint) moves
         *  to the area-weighted centroid of its star when that raises the
         *  star's worst angle, then the star is made Delaunay again. Runs
         *  before terrain points are placed (with terrainWorstFirst, or
         *  without terrain). 0 = off. */
        int smoothingPasses = 0;
        bool prioritizeQuality = true;
        int maxTriangles = std::numeric_limits<int>::max();
        /*! Smallest angle every triangle must reach (degrees); 0 = size only.
         *  Refinement terminates up to about 34 degrees. */
        double minAngleDeg = 30.0;
        /*! Safety cap on inserted vertices (segment splits included). */
        int    maxInsertions = std::numeric_limits<int>::max();
        /*! Floor against refinement cascades (an angle bound near the limit
         *  on an awkward input can otherwise shrink edges towards rounding
         *  error): a triangle whose shortest edge is below this is not split
         *  for its angle, and a subsegment shorter than twice this is not
         *  split. 0 = none. */
        double minEdge = 0.0;
        /*! Polled every 4096 insertions; returning true stops refinement. */
        std::function<bool()> cancelled;
    };
    struct QualityReport
    {
        int  inserted = 0;        ///< Vertices added, segment splits included.
        int  segmentSplits = 0;
        int  blockedByFixed = 0;  ///< Bad triangles left because their point would encroach a fixed edge.
        int  sizeInsertions = 0, qualityInsertions = 0, terrainInsertions = 0;
        int  terrainUnresolved = 0, terrainUnknown = 0;
        double maxTerrainError = 0.0;
        bool capped = false;      ///< Stopped at maxInsertions.
        bool cancelled = false;
    };
    /*! \brief Delaunay refinement to the size and angle bounds (see the file
     *  comment). Call after the constraints are inserted and the exterior,
     *  holes and any other removed regions are dead; only live triangles are
     *  refined and new vertices never enter a dead region. */
    QualityReport refineQuality(const QualityOptions &opt);
    /*! \brief Mark the input segment (a, b), already inserted with
     *  insertConstraint(), as unsplittable: refineQuality() never puts a
     *  vertex on it (a structured patch edge that must keep its stations). */
    void setFixedConstraint(int a, int b);
    /*! \brief True when live triangle \p t is below the angle bound only
     *  because of a small input angle (Shewchuk's exemption: its shortest
     *  edge joins two segment vertices equidistant from the input vertex
     *  their segments share). For reporting and tests. */
    [[nodiscard]] bool smallAngleExempt(int t) const;
    /*! \brief True when a vertex of live triangle \p t lies on a fixed
     *  segment (see setFixedConstraint). For reporting and tests. */
    [[nodiscard]] bool touchesFixed(int t) const;

    // ── Read-out ───────────────────────────────────────────────────────
    [[nodiscard]] const QVector<QPointF> &vertices() const { return m_pts; }
    [[nodiscard]] const QVector<Triangle> &triangles() const { return m_tris; }
    [[nodiscard]] const QVector<double> &terrainElevations() const { return m_terrainElevations; }
    [[nodiscard]] int liveTriangleCount() const;
    /*! \brief Index of the live triangle containing \p p (on an edge counts), -1 = none. */
    [[nodiscard]] int locate(const QPointF &p) const;
    /*! \brief True when the (min,max) vertex pair is a constrained edge. */
    [[nodiscard]] bool isConstrained(int a, int b) const;
    /*! \brief The vertices from a to b along the constrained edges that
     *  realise the input constraint (a, b) — {a, b} when it is one edge, more
     *  when vertices lying on the segment or refinement split it. Empty when
     *  no such chain. */
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
    bool   insertConstraintImpl(int a, int b, quint64 origin);
    int    splitSubsegment(int a, int b);
    bool   subsegmentEncroached(int a, int b) const;
    bool   isFixedSub(int a, int b) const;
    bool   exemptShortestEdge(int u, int v) const;

    QVector<QPointF>  m_pts;
    QVector<double>   m_terrainElevations;
    QVector<Triangle> m_tris;
    QVector<int>      m_vertexTri;   ///< Some live triangle incident to each vertex.
    int               m_superBase = 0;
    int               m_lastLocate = 0;
    QString           m_errorMsg;
    QHash<quint64, quint64> m_segOrigin;   ///< constrained subsegment → the input segment it came from (edge keys)
    QMultiHash<quint64, quint64> m_segOriginExtra; ///< further input segments sharing that subsegment (overlaps)
    QHash<quint64, quint64> m_segPiece;    ///< constrained subsegment → the edge it was inserted as (input piece)
    QHash<int, quint64>     m_vertexSeg;   ///< vertex added on a segment by refinement → its input piece
    QSet<quint64>           m_fixedSub;    ///< subsegments refinement may not split
    // Installed only during refinement. Every geometric edit invalidates the
    // affected work, including flips outside the inserted vertex's final star.
    std::function<void(int)> m_triangleChanged;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCDT_H
