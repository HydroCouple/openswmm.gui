/*!
 * \file   meshquadregion.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * PSLG quad regions (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §3,
 * §4.1): closed polygons inside the meshing domain where quadrilateral cells
 * are prioritised. Everything outside a region stays Triangle's triangles;
 * the interface conforms because the region ring is a constraint loop in the
 * same CDT.
 *
 * This header holds the data model plus the pure-geometry helpers
 * (validation, auto-classification, ring resampling). How a region is turned
 * into cells lives in MeshGenerator::generate() (Free regions: meshcrossfield
 * → meshquadpoints → Triangle → meshquadmatch → meshquadcleanup; Mapped /
 * Submapped regions: meshpatch::makeMappedPatch / meshsubmap).
 */
#ifndef OPENSWMMVIS_MESH_MESHQUADREGION_H
#define OPENSWMMVIS_MESH_MESHQUADREGION_H

#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

namespace mesh {

enum class QuadRegionMode
{
    Auto,          ///< classifyQuadRegion() decides: Mapped → Submapped → Free.
    Mapped,        ///< Four logical sides (polylines) → transfinite quads. Needs 4 corners.
    Submapped,     ///< Exactly rectilinear ring → grid-based structured quads.
    Free,          ///< Cross-field aligned lattice + template pairing (quad-dominant).
    TrianglesOnly  ///< Region ring is still a constraint loop, interior stays triangles.
};

struct QuadRegion
{
    QPolygonF      ring;                 ///< Closed (last == first optional), simple, any orientation.
    /*! Areas subtracted from \ref ring (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.1):
     *  domain hole rings, and the rings of any explicit region nested inside this
     *  one. No cell is generated inside a hole and the lattice keeps its boundary
     *  clearance from every hole ring, exactly as it does from \ref ring. */
    QVector<QPolygonF> holes;
    /*! True when \ref ring IS a domain outline rather than a ring drawn inside
     *  one — the "toggle quads on for the whole model" case. Two consequences:
     *  the ring is NOT re-emitted as constraint segments (the domain boundary is
     *  already in the PSLG, and duplicating it would double every boundary edge),
     *  and the "region lies inside exactly one domain ring" validation is skipped
     *  because the region *is* that ring. */
    bool           isBackground = false;
    QuadRegionMode mode = QuadRegionMode::Auto;
    double spacing   = 0.0;              ///< Target quad edge length h; 0 = derive from the size field / max area.
    double aspectMax = 2.0;              ///< Longest/shortest side accepted (Free); <= 0 = unbounded.
    bool   hasAlignAngle = false;        ///< Free: constant cross field at alignAngleDeg (from +x, CCW).
    double alignAngleDeg = 0.0;
    QVector<QPointF> alignGuide;         ///< Free: optional polyline the field aligns to (streets, channels).
    QVector<int>     corners;            ///< Mapped: 4 ring vertex indices in ring order; empty = auto-pick.
    QString tag;                         ///< MeshTriangle::tag for every cell inside; empty = inherit region marker's.
};

/*! \brief Drop a closing duplicate, drop consecutive duplicates, make CCW. */
QPolygonF normalizeRingCCW(const QPolygonF &ring);

/*! \brief Signed shoelace area (CCW positive). */
double ringSignedArea(const QPolygonF &ring);

/*! \brief Odd-even point-in-polygon (closed ring, last == first optional). */
bool pointInRing(const QPolygonF &ring, const QPointF &p);

/*! \brief Inside \p ring and outside every ring of \p holes — the region's
 *  meshable interior. With no holes this is exactly pointInRing(). */
bool pointInRegion(const QPolygonF &ring, const QVector<QPolygonF> &holes, const QPointF &p);

/*! \brief True when \p ring is simple (no proper self-intersection). O(n²). */
bool ringIsSimple(const QPolygonF &ring);

/*! \brief Validate one region: >= 3 distinct vertices, simple, area >= 4·h²
 *  (h = \p spacing when > 0, else skipped), every vertex inside exactly one
 *  of \p domains and outside every \p holes ring. Empty string = valid. */
QString validateQuadRegion(const QuadRegion &r,
                           const QVector<QPolygonF> &domains,
                           const QVector<QPolygonF> &holes);

/*! \brief Pairwise check: rings may share edges/vertices but no ring vertex of
 *  one may lie strictly inside another and edges may not properly cross.
 *  Returns the first offending pair as "region i overlaps region j", or "". */
QString validateQuadRegionsDisjoint(const QVector<QuadRegion> &regions);

/*! \brief Auto-classification (plan §4.1) on the normalised ring.
 *
 *  Turning angle at each vertex (exterior angle, CCW positive):
 *   - exactly four vertices with turn in [90 − cornerTolDeg, 90 + cornerTolDeg]
 *     and every other |turn| < cornerTolDeg → Mapped (corners = those four, in
 *     ring order);
 *   - every turn within rectilinearTolDeg of ±90° → Submapped;
 *   - otherwise Free.
 *  \p corners receives the Mapped corners when non-null (cleared otherwise). */
QuadRegionMode classifyQuadRegion(const QPolygonF &ring, QVector<int> *corners,
                                  double cornerTolDeg = 25.0,
                                  double rectilinearTolDeg = 2.0);

/*! \brief Resample a closed ring at spacing \p h: every edge is split into
 *  max(1, round(len/h)) equal parts; ring vertices whose |turn| >= keepTurnDeg
 *  are always kept, others are kept too (geometry is never simplified here —
 *  run pslg::simplifyPolyline first). Output is CCW, open (no closing duplicate). */
QPolygonF resampleRing(const QPolygonF &ring, double h, double keepTurnDeg = 0.0);

/*! \brief Exterior turning angle (degrees, CCW positive) at ring vertex \p i. */
double ringTurnDeg(const QPolygonF &ring, int i);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHQUADREGION_H
