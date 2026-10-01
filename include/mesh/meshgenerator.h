/*!
 * \file   meshgenerator.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * 2D mesh generator (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md): a
 * balanced quadtree core sized by the caller's size function, conformed to
 * every constraint by a constrained-Delaunay fringe (mesh/meshquadtree.h,
 * mesh/meshcdt.h). Tags propagate from input → output through point and
 * segment markers and region seeds, as they did with Triangle.
 *
 * The MeshGenerator deals only in geometry. Mapping SWMM 1D objects
 * (junctions, conduits, subcatchments) onto inputs is the caller's
 * job — that's where the coupling identity comes from.
 */
#ifndef OPENSWMMVIS_MESH_MESHGENERATOR_H
#define OPENSWMMVIS_MESH_MESHGENERATOR_H

#include "meshresult.h"
#include "meshedgekey.h"
#include "meshpatch.h"
#include "meshquadquality.h"
#include "meshquadregion.h"

#include <QHash>
#include <QPair>
#include <QSet>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

#include <functional>

namespace mesh {

/*! \brief A polyline that must appear as constrained edges in the mesh. */
struct ConstraintSegment
{
    QVector<QPointF> path;     ///< >= 2 points; consecutive pairs become Triangle segments.
    int              marker = 0; ///< Triangle marker value; preserved on output edges.
    QString          tag;      ///< Resolved later via the marker→tag lookup the caller maintains.
};

/*! \brief A point that must appear as a vertex in the output mesh. */
struct SteinerPoint
{
    QPointF xy;
    int     marker = 0;        ///< Carries a tag id; resolved via marker→tag map.
    QString tag;               ///< Convenience copy (caller can resolve via marker, but stash here too).
    double  z      = 0.0;      ///< Pre-sampled elevation when hasZ is true (e.g. from DTM thinner).
    bool    hasZ   = false;    ///< If true, z is exact — skip DTM re-sampling in post-mesh step.
};

/*! \brief A region attribute — an interior seed point with a value Triangle propagates
 *         into the triangle-attribute output array. We use it to tag triangles with
 *         a numeric id that maps back to subcatchment names etc.
 */
struct RegionMarker
{
    QPointF xy;                ///< Seed point inside the region (must be inside a sub-polygon).
    double  attribute = 0.0;   ///< Region id; resolved via id→tag map.
    double  maxArea   = -1.0;  ///< -1 = use global max area.
    QString tag;               ///< Convenience.
};

/*! \brief Cancellation, progress and graded sizing callbacks. */
struct RefineHook
{
    /*! Polled between stages and inside the long loops. Returning true stops
     *  generation; generate() then reports failure rather than a partial mesh.
     *  Must be cheap and thread-safe (typically reads an atomic). */
    std::function<bool()> isCancelled;
    /*! Maximum permitted triangle area at map coordinate (x, y); the target
     *  edge length is the side of the equilateral triangle of that area.
     *  Return <= 0 for "no limit here". When set, this SUPERSEDES
     *  GenerationOptions::maxArea. */
    std::function<double(double x, double y)> targetAreaAt;
    /*! Called every so often with a running cell count. Purely advisory. */
    std::function<void(qint64 count)> onProgress;
};

/*! \brief Quality knobs surfaced to the user dialog. */
struct GenerationOptions
{
    double maxArea     = 0.0;     ///< 0 = no global cap; else upper bound on triangle area.
                                  ///< Ignored when a refinement size function is
                                  ///< installed (see MeshGenerator::setRefineHook).
                                  ///< The target edge length is the equilateral side.

    // ── Overhaul (MESH_OVERHAUL_PLAN_2026-09-29.md §3) ───────────────────
    /*! Floor cell size h_min (map units) = the finest quadtree leaf and the
     *  lower clamp of the size function. 0 = a quarter of the smallest size
     *  sampled over the inputs. */
    double minCellSize = 0.0;
    /*! Cell shape: false = quads where possible (core squares + paired
     *  fringe), true = triangles everywhere. */
    bool   trianglesOnly = false;
    /*! Orientation of the background grid, degrees from +x counter-clockwise. */
    double frameAngleDeg = 0.0;

    /*! Snap radius (map units) used to match patch vertices against the
     *  output vertices. 0 = 1e-7. */
    double patchSnapEps = 0.0;
};

/*! \brief Per-region outcome of generate() (MeshGenerator::quadRegionReports()). */
struct QuadRegionReport
{
    int            index = -1;
    QuadRegionMode requested = QuadRegionMode::Auto;
    QuadRegionMode resolved  = QuadRegionMode::Free;   ///< Free = its own or the background grid; TrianglesOnly in triangles mode.
    bool    accepted = false;                         ///< Region was included in generation, not skipped.
    double  spacing = 0.0;                            ///< Size override inside the ring (0 = none).
    QString message;                                  ///< Validation error; empty when clean.
};

/*! \brief Generate a 2D triangular mesh.
 *
 * Usage:
 *
 *     MeshGenerator g;
 *     g.setDomain(boundaryPolygon);
 *     g.addSteinerPoint({junctionXY, 1, "J1"});       // marker = 1
 *     g.addConstraintSegment({conduitPath, 100, "C5"}); // marker = 100
 *     g.setOptions({.maxArea = 50.0, .minAngle = 28.0});
 *     const MeshResult r = g.generate();
 */
class MeshGenerator
{
public:
    MeshGenerator() = default;

    /*! \brief Replace the domain with a single closed boundary polygon. */
    void setDomain(const QPolygonF &outerBoundary);

    /*! \brief Replace the domain with multiple disjoint closed polygons.
     *  Each polygon becomes its own boundary ring; Triangle meshes the
     *  interior of every ring and leaves the gaps unmeshed. Useful when
     *  the meshing region is a multi-catchment area or a layer
     *  containing several non-overlapping polygons. */
    void setDomains(const QVector<QPolygonF> &outerBoundaries);

    /*! \brief Append one more closed boundary polygon to the current
     *  domain set. Equivalent to building up via setDomains. */
    void addDomain(const QPolygonF &outerBoundary);

    void addConstraintSegment(const ConstraintSegment &seg);
    void addSteinerPoint(const SteinerPoint &pt);
    /*! \brief Pre-reserve capacity for \p additional upcoming addSteinerPoint
     *  calls.  Avoids the transient ~2x peak of geometric growth when bulk-
     *  adding terrain points; callers should cap the request. */
    void reserveSteinerPoints(qsizetype additional);
    void addHole(const QPointF &interiorPointInsideHole);
    void addRegion(const RegionMarker &region);
    /*! \brief Stitch a structured quad patch (mesh/meshpatch.h) into the
     *  domain. Its boundary segments become PSLG constraints, its interior
     *  a hole (seed = the first quad's centroid), and after Triangle runs
     *  its quads are appended after every triangle with vertices merged by
     *  coordinate against the Triangle output (GenerationOptions::patchSnapEps).
     *  Patch vertices receive whatever elevation fill the caller applies to
     *  MeshResult::vertices afterwards — same path as Triangle's own. */
    void addPatch(const PatchMesh &patch);
    /*! \brief Register a quad region (mesh/meshquadregion.h): its ring becomes
     *  a constraint loop, its spacing (when > 0) a size override inside the
     *  ring, and — when hasAlignAngle — the region gets its own quadtree
     *  frame at alignAngleDeg. Cells inside carry the region's tag. */
    void addQuadRegion(const QuadRegion &region);
    /*! \brief One report per addQuadRegion() call, filled by generate(). */
    [[nodiscard]] const QVector<QuadRegionReport> &quadRegionReports() const { return m_quadReports; }
    /*! \brief Retained for API compatibility: always empty (core cells are
     *  never triangle pairs). */
    [[nodiscard]] QSet<QPair<int, int>> quadRegionMergeLocks(const MeshResult &mesh) const;
    void setOptions(const GenerationOptions &opts);

    /*! \brief Install cancellation / progress / graded-sizing callbacks.
     *  When \c hook.targetAreaAt is set it supersedes GenerationOptions::maxArea.
     *  If the hook reports cancellation, generate() returns ok=false with
     *  errorMsg set rather than a partial mesh. */
    void setRefineHook(const RefineHook &hook);

    /*! \brief Generate the mesh. Returns a result with ok=false + errorMsg on failure. */
    [[nodiscard]] MeshResult generate() const;

    /*! \brief Translate an output point's marker back to a tag string.
     *         Used by clients that don't want to maintain their own map.
     *         The generator builds this from \c SteinerPoint::marker→tag during generate(). */
    [[nodiscard]] QString tagForVertexMarker(int marker) const;
    /*! \brief Translate an output edge's marker back to a tag string. */
    [[nodiscard]] QString tagForEdgeMarker(int marker) const;

private:
    QVector<QPolygonF>         m_domains;
    QVector<ConstraintSegment> m_segments;
    QVector<SteinerPoint>      m_steiners;
    QVector<QPointF>           m_holes;
    QVector<RegionMarker>      m_regions;
    QVector<PatchMesh>         m_patches;
    QVector<QuadRegion>        m_quadRegions;
    mutable QVector<QuadRegionReport> m_quadReports;
    GenerationOptions          m_opts;
    RefineHook                 m_refineHook;

    // Filled in during generate(); exposed so callers don't have to maintain
    // a parallel marker→tag map. mutable: generate() is logically const for
    // the Inputs but builds these tables.
    mutable QHash<int, QString>     m_vertexTagByMarker;
    mutable QHash<int, QString>     m_edgeTagByMarker;
    mutable QHash<int, QString>     m_triangleTagByRegionId;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHGENERATOR_H
