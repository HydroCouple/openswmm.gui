/*!
 * \file   channelburnlattice.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The channel corridor as mesh input — phases P2 and P3 of
 * workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§4.6, §4.7, §16.2).
 *
 * WHAT THIS IS.  One conduit's bed, sampled on a conformal lattice: the
 * thalweg plus N offset strings, every string sampled at the SAME chainage
 * stations, so string k vertex i and string k+1 vertex i are corners of one
 * quad.  From that single lattice come every product the mesher needs:
 *
 *   - \ref corridorPatch — a `mesh::PatchMesh` of pure quads. This is the
 *     corridor (plan D5, as corrected by §16.2).
 *   - \ref corridorStrings / \ref corridorPoints — breaklines and exact-z
 *     vertices, for the triangles-only corridor.
 *   - \ref corridorRing — the outline, for previewing and for subtracting the
 *     corridor from a "quads everywhere" background region.
 *
 * WHY A PATCH AND NOT A QuadRegion.  `QuadRegion::alignGuide` is read only in
 * the Free branch of MeshGenerator, and a constraint segment crossing a
 * region's ring demotes Mapped/Submapped to Free — which the corridor's own
 * strings would do to the very region they are meant to build.  `addPatch`
 * takes a caller-built lattice directly: the boundary becomes a PSLG
 * constraint, the interior a hole, the quads are stitched in afterwards, and
 * the CDT's fixed constraints protect the outline from being split.  100 % quads,
 * no pairing heuristic, no new region mode.
 *
 * PER-CELL ROUGHNESS.  The stitch copies each quad whole, filling only an
 * EMPTY tag from the patch, so `MeshTriangle::mannings` and per-cell tags both
 * survive. The transect's n_left / n_channel / n_right therefore reach the
 * mesh as real per-cell values rather than as a tag the writer has to resolve.
 *
 * Unlike channelburn.h this header pulls in the mesh data model (QtGui), so it
 * is kept separate: the profile and rule layers stay Qt Core only and unit-test
 * without a GUI dependency.
 */
#ifndef OPENSWMMVIS_MESH_CHANNELBURNLATTICE_H
#define OPENSWMMVIS_MESH_CHANNELBURNLATTICE_H

#include "mesh/channelburnprofile.h"
#include "mesh/meshgenerator.h"
#include "mesh/meshpatch.h"

#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mesh {

/*!
 * \brief One conduit's corridor, sampled on a conformal (along x across)
 *        lattice in the MESH CRS and model vertical units.
 *
 * Row-major: vertex (i, k) is at \ref at(i, k), i along the channel, k across
 * it with k ascending to the RIGHT of the downstream direction.
 */
struct BurnLattice
{
    QString          conduitId;
    int              nAlong  = 0;
    int              nAcross = 0;
    QVector<double>  chainage;   ///< nAlong, ascending, 0 .. length.
    QVector<double>  offsets;    ///< nAcross, ascending, signed.
    QVector<QPointF> xy;         ///< nAlong · nAcross.
    QVector<double>  z;          ///< nAlong · nAcross, exact burned elevation.
    double minAlongSpacing  = 0.0;
    double minAcrossSpacing = 0.0;

    [[nodiscard]] int at(int i, int k) const noexcept { return i * nAcross + k; }
    [[nodiscard]] bool isValid() const noexcept
    {
        return nAlong >= 2 && nAcross >= 2
               && xy.size() == qsizetype(nAlong) * nAcross && z.size() == xy.size();
    }
};

/*! Piecewise-linear authored bathymetry, indexed independently of DEM pixels.
 * Raster export, vertex elevations and final triangle checks share this surface. */
class BurnSurface
{
public:
    struct Hit { double z=qQNaN(); int profile=-1; double offset=0; };
    struct Error { double maximum=0; QPointF point; bool touched=false; };
    void build(const QVector<BurnLattice> &lattices);
    Hit sample(const QPointF &p) const;
    /*! sample(), or when \p p misses every face but lies within \p radius
     *  of one, the lowest such face's value at its closest point to \p p
     *  (a vertex constraint joining pulled a hair outside the corridor). */
    Hit sampleNear(const QPointF &p, double radius) const;
    /*! Every face covering \p p (sample() keeps only the lowest). */
    QVector<Hit> hitsAt(const QPointF &p) const;
    bool intersects(const QRectF &bounds) const;
    /*! Largest |triangle - face| over every face overlapping the triangle.
     *  \p minWidth > 0 skips overlaps thinner than that (2 x area /
     *  perimeter): slivers where a cell only touches a corridor edge that
     *  constraint joining moved by less than its tolerance. */
    Error error(const QPointF *xy, const double *z,
                const std::function<bool(const QPointF &)> &inside, double minWidth = 0.0) const;
    /*! Bounded refinement estimator against a permitted target (for example,
     *  burnPixel's incision cap and lower envelope). Samples clipped face
     *  corners, edge midpoints and interiors. Unlike error(), this is not an
     *  exact authored-surface diagnostic; retain that independent final check. */
    Error sampledTargetError(const QPointF *xy, const double *z,
                const std::function<double(const QPointF &)> &target, double minWidth = 0.0) const;
private:
    Error errorImpl(const QPointF *xy, const double *z,
                const std::function<bool(const QPointF &)> &inside, double minWidth,
                const std::function<double(const QPointF &)> &target) const;
    struct Face { QPointF p[3]; double z[3],offset[3]; int profile; QRectF bounds; };
    QVector<Face> faces;
    QHash<QPair<int,int>,QVector<int>> grid;
    double pitch=1;
    QPointF origin;
    QRectF surfaceBounds;
    QVector<int> candidates(const QRectF &bounds) const;
};

/*!
 * \brief Sample \p p onto a lattice at \p alongStep spacing.
 *
 * The local tangent is taken from a central difference half a step either side
 * of each station rather than from the containing segment, which smooths the
 * normal through a bend and is what keeps the offset rows from folding.
 *
 * \param alongStep   Target streamwise spacing; <= 0 uses the profile's own
 *                    chainage stations.
 * \param minCellSize Mesh minimum cell size, for the densification guard
 *                    (§4.6): a lattice finer than the mesh floor is reported,
 *                    not silently emitted. 0 = no check.
 * \param acrossTolerance > 0 drops section offsets whose removal changes no
 *                    station's cross-section by more than this (vertical,
 *                    profile units): dense surveyed or lidar sections keep
 *                    their shape without hundreds of offsets. The first and
 *                    last offsets and the one nearest the centreline stay,
 *                    and no lateral gap grows beyond the along spacing.
 *                    Applies only to sections denser than \p minCellSize
 *                    (more offsets than width / minCellSize + 1).
 *                    0 = keep every offset.
 */
[[nodiscard]] BurnLattice buildCorridorLattice(const BurnProfile &p,
                                               double alongStep, double minCellSize,
                                               QStringList *warnings = nullptr,
                                               QString *err = nullptr,
                                               double acrossTolerance = 0.0,
                                               double minSpacing = 0.0);

/*!
 * \brief The corridor as quads — the P3 deliverable.
 *
 * Every cell is emitted CCW and convexity-checked by `mesh::validate`, so a
 * corridor that folds on a hairpin bend is REPORTED rather than handed to
 * the mesher as a bad patch. Cells carry `channel:<id>` (or `:left` / `:chan` /
 * `:right` with `roughnessFromTransect`) and, where the transect supplied them,
 * per-cell Manning's n.
 */
[[nodiscard]] PatchMesh corridorPatch(const BurnLattice &lat, const BurnProfile &p,
                                      const BurnOptions &opt, QString *err = nullptr);

/*!
 * \brief The lattice's longitudinal strings as breaklines — the triangles-only
 *        corridor (use this OR \ref corridorPatch, never both: a patch interior
 *        is a hole, and a constraint segment inside it is invalid).
 *
 * Markers run from \p markerBase; \p markerToTag receives marker → tag.
 */
[[nodiscard]] QVector<ConstraintSegment> corridorStrings(const BurnLattice &lat,
                                                         int markerBase,
                                                         QHash<int, QString> *markerToTag = nullptr);

/*! \brief The lattice's vertices as exact-z Steiner points (`hasZ = true`), so
 *         the post-mesh elevation fill never re-samples them. */
[[nodiscard]] QVector<SteinerPoint> corridorPoints(const BurnLattice &lat, int marker);

/*!
 * \brief The corridor outline, CCW, first vertex not repeated.
 *
 * Two consumers: the preview overlay, and the `holes` of a "quads everywhere"
 * background region — without which the background lattice would place points
 * inside the patch's hole.
 */
[[nodiscard]] QPolygonF corridorRing(const BurnLattice &lat);

/*! \brief Lattice rows \p i0 .. \p i1 (inclusive) of \p lat as a lattice of
 *  their own, so a run of rows can become a quad patch while the rest of the
 *  corridor stays constraint lines. Invalid when fewer than two rows. */
[[nodiscard]] BurnLattice latticeRows(const BurnLattice &lat, int i0, int i1);

/*!
 * \brief Lattice vertices as (xy, z) pairs for the pipeline's `featureZSeed*`
 *        arrays, which seed `elevCache` by coordinate.
 *
 * This is how a patch vertex keeps its exact burned elevation: the elevation
 * fill is coordinate-keyed, so a seeded vertex is never re-sampled, whether it
 * came from a Steiner point or from a stitched patch.
 */
void corridorZSeeds(const BurnLattice &lat, QVector<QPointF> *xy, QVector<double> *z);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNLATTICE_H
