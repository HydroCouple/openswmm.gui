/*!
 * \file   channelburn.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Channel burn-in: corridor projection and the per-pixel rule
 * (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.4, phase P1).
 *
 * WHAT THIS IS.  Given the reconstructed beds from channelburnprofile.h, this
 * answers one question per DEM pixel: *which channel is this pixel in, how far
 * along, how far across, and what elevation should it have?*  The answer feeds
 * burnedrasterwriter.h, which is the only part that touches GDAL.
 *
 * FRAMES.  Everything here is in the RASTER's own frame — its horizontal CRS
 * and its vertical unit.  That is deliberate: mixing a model elevation in feet
 * with a DEM in metres burns the channel 3.28x too deep, and the cheapest
 * defence is to make the conversion a single, explicit, testable step
 * (\ref toRasterFrame) instead of a rule the rest of the code has to remember.
 * The coordinate transform itself belongs to the caller — a GDAL/OGR handle
 * must not cross the worker-thread boundary.
 *
 * PROJECTION, NOT SWEEPING.  A pixel is located by NEAREST-POINT projection
 * onto the densified centreline.  Swept normals cross on the inside of a tight
 * bend and make the (chainage, offset) map multi-valued there; nearest-point
 * projection is single-valued everywhere by construction.
 */
#ifndef OPENSWMMVIS_MESH_CHANNELBURN_H
#define OPENSWMMVIS_MESH_CHANNELBURN_H

#include "mesh/channelburnprofile.h"

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace mesh {

/*! \brief Where a point landed on one conduit's corridor. */
struct BurnProjection
{
    int    profile  = -1;   ///< Index into the caller's profile vector; < 0 = no hit.
    double chainage = 0.0;  ///< Distance along that profile's centreline.
    double offset   = 0.0;  ///< Signed lateral distance. **Positive = right of the
                            ///<     downstream direction**, matching [TRANSECTS], whose
                            ///<     stations ascend from the left bank to the right.
};

/*! \brief What the rule did to one pixel. */
enum class BurnOutcome
{
    Outside,     ///< No corridor covers it — the DEM stands.
    Replaced,    ///< |s| <= R: the section overwrote the DEM (D2).
    Lowered,     ///< |s| >  R: min(DEM, section) cut the DEM down (D3).
    Unchanged,   ///< Inside the corridor, but min() kept the DEM.
    NoDataKept   ///< NoData outside R — min() with NaN is undefined, so it stays NoData.
};

/*!
 * \brief The rule's parameters, in the RASTER's own units.
 *
 * Deliberately not `BurnOptions`: those lengths are in model units, and a
 * function that took both would be one rename away from the 3.28x bug.
 */
struct BurnRule
{
    double forceHalfWidth = 0.0;   ///< R. |s| <= R replaces; beyond it, min() only.
    double maxIncision    = 0.0;   ///< 0 = unbounded; else never cut more than this
                                   ///<     below the DEM. Inert where the DEM is NoData.
};

/*!
 * \brief Uniform-hash index over the burn set's centreline segments.
 *
 * Self-contained: \ref build copies what it needs, so the profiles may be
 * moved or destroyed afterwards. Queries are O(segments in a 3x3 cell
 * neighbourhood), with the cell sized so that neighbourhood always covers the
 * widest corridor.
 */
class BurnCorridorIndex
{
public:
    void build(const QVector<BurnProfile> &profiles);

    [[nodiscard]] bool   isEmpty() const noexcept { return m_seg.isEmpty(); }
    [[nodiscard]] QRectF bounds()  const noexcept { return m_bounds; }   ///< Corridor bbox, extent included.

    /*! \brief Nearest point on EVERY profile whose corridor can cover \p p,
     *         one projection per profile, unordered. */
    void projectAll(const QPointF &p, QVector<BurnProjection> *out) const;

private:
    struct Seg
    {
        QPointF a, b;
        double  chainageA = 0.0;   ///< Chainage at \ref a.
        int     profile   = -1;
    };

    [[nodiscard]] qint64 cellKey(int i, int j) const noexcept
    {
        return (qint64(i) << 32) ^ quint32(j);
    }

    QVector<Seg>              m_seg;
    QHash<qint64, QVector<int>> m_grid;
    QVector<double>           m_extent;   ///< Per profile: max(|sMin|, sMax).
    QRectF                    m_bounds;
    double                    m_cell   = 0.0;
    double                    m_radius = 0.0;  ///< Widest corridor half-extent.
};

// ── The rule ───────────────────────────────────────────────────────────────

/*!
 * \brief Plan §4.4 step 3 — what one pixel becomes.
 *
 * \param zDem      The DEM value, raster vertical units.
 * \param demNoData True when the pixel has no data; \p zDem is then ignored.
 * \param zSec      Section elevation at the projection, raster vertical units.
 * \param s         Signed lateral offset, raster horizontal units.
 * \param[out] zOut The new value. Untouched for \ref BurnOutcome::NoDataKept.
 *
 * \note `maxIncision` clamps against the **DEM**, not the bed: the plan's
 *       `max(z, bedZ − maxIncision)` can never fire, because the section is by
 *       definition at or above its own bed (§16.5).
 */
BurnOutcome burnPixel(double zDem, bool demNoData, double zSec, double s,
                      const BurnRule &rule, double *zOut);

/*!
 * \brief The confluence rule (§4.4 step 4) — among every corridor covering
 *        \p p, the LOWEST section elevation wins, ties broken on ascending
 *        conduit id.
 *
 * Deterministic and independent of the order the conduits were burned in,
 * which is gate V2(c).
 *
 * \returns false when no corridor covers \p p.
 */
bool bestBurnAt(const BurnCorridorIndex &index, const QVector<BurnProfile> &profiles,
                const QPointF &p, BurnProjection *projection, double *zSec);

// ── Frames ─────────────────────────────────────────────────────────────────

/*!
 * \brief A copy of \p p in the raster's frame.
 *
 * \param rasterCenterline \p p's centreline transformed into the raster CRS by
 *        the caller — same length, vertex for vertex.
 * \param hScale Raster horizontal units per model length unit (lateral offsets
 *        and the section's stations scale by this; the centreline does not,
 *        because it arrives already transformed).
 * \param vScale Raster vertical units per model vertical unit — the RECIPROCAL
 *        of the pipeline's `zConversionFactor`, which multiplies raster samples
 *        to get model units.
 *
 * Chainage is recomputed from \p rasterCenterline, so the bed stays pinned to
 * the right vertex even when the two CRSs disagree about distance.
 */
[[nodiscard]] BurnProfile toRasterFrame(const BurnProfile &p,
                                        const QVector<QPointF> &rasterCenterline,
                                        double hScale, double vScale);

/*! \brief \ref BurnOptions to \ref BurnRule, converting both lengths. */
[[nodiscard]] BurnRule toRasterRule(const BurnOptions &opt, double hScale, double vScale);

// ── Identity ───────────────────────────────────────────────────────────────

/*!
 * \brief Eight hex characters over {source DEM identity, options, resolved
 *        conduits and their geometry} — the burned raster's filename stem.
 *
 * Identical inputs give an identical name, so a re-run is a no-op and
 * `MeshStageCache`'s DEM file-identity key does the right thing for free;
 * different inputs cannot collide onto one file.
 */
[[nodiscard]] QString burnFingerprint(const QString &demIdentity, const BurnOptions &opt,
                                      const QVector<BurnProfile> &profiles);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURN_H
