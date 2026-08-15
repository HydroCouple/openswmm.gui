/*!
 * \file   meshgenerator.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU — thin Qt-friendly wrapper around Shewchuk's Triangle
 * (vendor/triangle/). Builds a constrained-Delaunay triangulation
 * from a domain polygon, optional constraint segments, and required
 * Steiner points. Tags propagate from input → output via Triangle's
 * marker (point/segment) and region-attribute mechanisms.
 *
 * The MeshGenerator deals only in geometry. Mapping SWMM 1D objects
 * (junctions, conduits, subcatchments) onto inputs is the caller's
 * job — that's where the coupling identity comes from.
 */
#ifndef OPENSWMMVIS_MESH_MESHGENERATOR_H
#define OPENSWMMVIS_MESH_MESHGENERATOR_H

#include "meshresult.h"
#include "trirefinehook.h"

#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

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

/*! \brief Quality knobs surfaced to the user dialog. */
struct GenerationOptions
{
    double maxArea     = 0.0;     ///< 0 = no global cap; else upper bound on triangle area.
                                  ///< Ignored when a refinement size function is
                                  ///< installed (see MeshGenerator::setRefineHook).
    double minAngle    = 26.0;    ///< 0..33 reliable; above that Triangle may not
                                  ///< terminate. Cost rises steeply near the top of
                                  ///< the range — 33° commonly yields 2-4x the
                                  ///< vertices of 26° for no practical benefit.
    bool   allowSteiner = true;   ///< false = -YY (no Steiner points on boundary).
    bool   conformingDelaunay = false; ///< -D switch.
    int    maxSteinerPoints   = -1; ///< -SN cap; -1 = unlimited.
    bool   quiet       = true;    ///< -Q (suppress Triangle's stderr).
    QString customSwitchString;   ///< If non-empty, overrides everything above. Advanced.
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
    void setOptions(const GenerationOptions &opts);

    /*! \brief Install cancellation / progress / graded-sizing callbacks.
     *
     * Passing a hook with any member set makes generate() add Triangle's `-u`
     * switch, which routes refinement decisions through the hook.  A hook whose
     * members are all empty changes nothing.  See trirefinehook.h.
     *
     * When \c hook.targetAreaAt is set it supersedes GenerationOptions::maxArea
     * and the global `-a<area>` switch is omitted.
     *
     * If the hook reports cancellation, generate() returns ok=false with
     * errorMsg set rather than a partially refined mesh. */
    void setRefineHook(const RefineHook &hook);

    /*! \brief Run Triangle. Returns a result with ok=false + errorMsg on failure. */
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
