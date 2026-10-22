/**
 * @file      channelburn.h
 * @brief     Channel burn-in: corridor projection and the per-pixel rule
 *            (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.4, phase P1).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * (\ref mesh::toRasterFrame) instead of a rule the rest of the code has to remember.
 * The coordinate transform itself belongs to the caller — a GDAL/OGR handle
 * must not cross the worker-thread boundary.
 *
 * PROJECTION, NOT SWEEPING.  A pixel is located by NEAREST-POINT projection
 * onto the densified centreline.  Swept normals cross on the inside of a tight
 * bend and make the (chainage, offset) map multi-valued there; nearest-point
 * projection is single-valued everywhere by construction.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_CHANNELBURN_H
#define OPENSWMMVIS_MESH_CHANNELBURN_H

#include "mesh/channelburnprofile.h"

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace mesh
{

/*! \brief Where a point landed on one conduit's corridor. */
struct BurnProjection {
    int profile = -1;      ///< Index into the caller's profile vector; < 0 = no hit.
    double chainage = 0.0; ///< Distance along that profile's centreline.
    double offset = 0.0;   ///< Signed lateral distance. **Positive = right of the
                           ///<     downstream direction**, matching [TRANSECTS], whose
                           ///<     stations ascend from the left bank to the right.
};

/*! \brief What the rule did to one pixel. */
enum class BurnOutcome {
    Outside,   ///< No corridor covers it — the DEM stands.
    Replaced,  ///< |s| <= R: the section overwrote the DEM (D2).
    Lowered,   ///< |s| >  R: min(DEM, section) cut the DEM down (D3).
    Unchanged, ///< Inside the corridor, but min() kept the DEM.
    NoDataKept ///< NoData outside R — min() with NaN is undefined, so it stays NoData.
};

/*!
 * \brief The rule's parameters, in the RASTER's own units.
 *
 * Deliberately not `BurnOptions`: those lengths are in model units, and a
 * function that took both would be one rename away from the 3.28x bug.
 */
struct BurnRule {
    double forceHalfWidth = 0.0; ///< R. |s| <= R replaces; beyond it, min() only.
    double maxIncision = 0.0;    ///< 0 = unbounded; else never cut more than this
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
    /**
     * @brief Replace this index with owned segments and corridor extents from valid profiles.
     * @param[in] profiles Borrowed immutable raster-frame profiles, unchanged; valid profiles
     * require aligned centerline/chainage arrays and finite coordinates/extents whose grid
     * quotients/loop extents fit int. Copies nonzero-length segments and per-profile extents;
     * original profiles may be moved/destroyed after return. Invalid profiles skip,
     * empty/degenerate input leaves an empty index. Grid pitch is max(widest extent, mean
     * segment length, 1e-9); long segments occupy every intersected bounding-box grid cell, so
     * memory/work is not strictly bounded by profile count. No cancellation, geometry/finite
     * audit, CRS transform, file access or engine mutation. Caller excludes concurrent
     * build/query operations.
     */
    void build(const QVector<BurnProfile> &profiles);

    /**
     * @brief Test whether any nondegenerate segment was retained by the last build.
     * @return True before build or when all source profiles/segments were skipped; does not
     * inspect the original profiles or certify the validity of subsequent profile-array pairing.
     * Read-only, no allocation.
     */
    [[nodiscard]] bool isEmpty() const noexcept { return m_seg.isEmpty(); }
    /**
     * @brief Return the retained segment bounding box expanded by the widest corridor extent.
     * @return Owned raster-horizontal-frame rectangle, default empty rectangle when no segment
     * retained. This is a conservative global envelope, not the exact union of clipped profile
     * corridors. Read-only.
     */
    [[nodiscard]] QRectF bounds() const noexcept
    {
        return m_bounds;
    } ///< Corridor bbox, extent included.

    /**
     * @brief Project a raster-frame point onto the nearest eligible segment of each nearby
     * profile.
     * @param[in] p Borrowed query point in this index's horizontal CRS/units, unchanged; finite
     * coordinates and cell quotients/neighbour arithmetic must fit int, unchecked.
     * @param[out] out Optional borrowed vector; null does nothing, otherwise cleared first then
     * receives at most one projection per profile in unspecified hash order. No pointer
     * retained. Selects nearest segment by squared Euclidean distance; equal-distance ties keep
     * the first encountered candidate. Rejects positions beyond original first/last segment
     * caps, then limits radius by that profile's section extent. Chainage interpolates copied
     * chainageA plus segment distance; signed offset is positive downstream-right, not radial
     * distance at internal endpoints. Allocates temporary hashes and output storage. Const
     * queries share read-only index state safely with separate outputs; exclude build.
     *
     * @details Nearest point on EVERY profile whose corridor can cover \p p, one projection per
     * profile, unordered.
     */
    void projectAll(const QPointF &p, QVector<BurnProjection> *out) const;

private:
    struct Seg {
        QPointF a, b;
        double chainageA = 0.0; ///< Chainage at \ref a.
        int profile = -1;
        bool first = false, last = false;
    };

    /**
     * @brief Pack signed grid coordinates into a lossless two-word hash key.
     * @param[in] i Horizontal signed grid index, converted through its 32-bit unsigned
     * representation.
     * @param[in] j Vertical signed grid index, converted through its 32-bit unsigned
     * representation.
     * @return Combined 64-bit bit pattern; no spatial arithmetic, bounds check, allocation or
     * state mutation.
     */
    [[nodiscard]] qint64 cellKey(int i, int j) const noexcept
    {
        return qint64((quint64(quint32(i)) << 32) | quint32(j));
    }

    QVector<Seg> m_seg;
    QHash<qint64, QVector<int>> m_grid;
    QVector<double> m_extent; ///< Per profile: max(|sMin|, sMax).
    QRectF m_bounds;
    double m_cell = 0.0;
    double m_radius = 0.0; ///< Widest corridor half-extent.
};

// ── The rule ───────────────────────────────────────────────────────────────

/**
 * @brief Apply forced replacement or lowering and optional DEM-relative incision limit to one
 * pixel.
 * @param[in] zDem Existing elevation in raster vertical units; ignored when demNoData
 * true/nonfinite.
 * @param[in] demNoData True marks missing terrain; nonfinite zDem is treated equivalently.
 * @param[in] zSec Candidate section elevation in raster vertical units; nonfinite returns
 * Outside.
 * @param[in] s Signed lateral offset in raster horizontal units, intended finite.
 * @param[in] rule Borrowed raster-unit thresholds, unchanged; |s|<=forceHalfWidth forces
 * replacement; positive maxIncision limits lowering below finite DEM, zero/nonpositive disables
 * the limit.
 * @param[out] zOut Optional borrowed destination; assigned for Replaced/Lowered/Unchanged,
 * untouched for Outside/NoDataKept. No pointer retained.
 * @return Outside for invalid section; NoDataKept for missing DEM outside forced strip;
 * Unchanged for exact equal final DEM value, otherwise Replaced inside forced strip or Lowered
 * outside. Missing DEM inside strip is bridged without incision clamp. Does not test corridor
 * membership, validate rule ranges, allocate, convert units or modify any raster directly.
 *
 * @details Plan §4.4 step 3 — what one pixel becomes.
 *
 *
 * \note `maxIncision` clamps against the **DEM**, not the bed: the plan's `max(z, bedZ −
 * maxIncision)` can never fire, because the section is by definition at or above its own bed
 * (§16.5).
 */
BurnOutcome burnPixel(double zDem, bool demNoData, double zSec, double s, const BurnRule &rule,
                      double *zOut);

/**
 * @brief Select the lowest finite in-extent section elevation among indexed corridor
 * projections.
 * @param[in] index Borrowed built immutable index, unchanged; must correspond to profiles
 * ordering.
 * @param[in] profiles Borrowed immutable raster-frame profile vector used to build index,
 * unchanged; geometry/chainage/section pairing must still match even though the index owns its
 * own segments.
 * @param[in] p Borrowed finite raster-horizontal-frame query point, unchanged.
 * @param[out] projection Optional borrowed winning projection, assigned only on true.
 * @param[out] zSec Optional borrowed winning elevation in raster vertical units, assigned only
 * on true.
 * @return True when a valid finite section candidate exists, false with outputs unchanged
 * otherwise. Exact equal elevations choose lexicographically smaller conduitId; duplicate equal
 * IDs do not receive an additional tie-break. Allocates local hit vector/hashes, retains no
 * inputs, performs no IO or engine mutation. Concurrent read-only queries require no
 * index/profile mutation and distinct outputs.
 *
 * @details The confluence rule (§4.4 step 4) — among every corridor covering
 * \p p, the LOWEST section elevation wins, ties broken on ascending conduit id.
 *
 * Deterministic and independent of the order the conduits were burned in, which is gate V2(c).
 */
bool bestBurnAt(const BurnCorridorIndex &index, const QVector<BurnProfile> &profiles,
                const QPointF &p, BurnProjection *projection, double *zSec);

// ── Frames ─────────────────────────────────────────────────────────────────

/**
 * @brief Copy a model-frame profile into caller-provided raster horizontal and scaled vertical
 * frames.
 * @param[in] p Borrowed valid source profile in model horizontal/vertical units, unchanged.
 * @param[in] rasterCenterline Borrowed already-transformed points, same count/order as
 * p.centerline; unchanged. Caller owns CRS transform and must provide finite usable raster
 * coordinates.
 * @param[in] hScale Positive finite raster horizontal units per model length unit; scales
 * lateral offsets, section stations/extents/banks, never the already-transformed centerline.
 * @param[in] vScale Finite raster vertical units per model vertical unit; scales bed/relative
 * elevations. Zero/negative finite values are accepted by this helper, though caller normally
 * requires positive.
 * @return Owned profile with recomputed raster chainage, or empty for invalid
 * source/count/hScale or nonfinite vScale. Does not validate transformed point quality or
 * perform the coordinate transform. Copies arrays; no engine/GDAL handle, IO, retained
 * references or caller-state mutation.
 *
 * @details A copy of \p p in the raster's frame.
 *
 *
 * Chainage is recomputed from \p rasterCenterline, so the bed stays pinned to the right vertex
 * even when the two CRSs disagree about distance.
 */
[[nodiscard]] BurnProfile toRasterFrame(const BurnProfile &p,
                                        const QVector<QPointF> &rasterCenterline, double hScale,
                                        double vScale);

/**
 * @brief Scale model force width and incision depth into raster-unit rule thresholds.
 * @param[in] opt Borrowed model-unit options, unchanged; only forceHalfWidth and maxIncision
 * consulted.
 * @param[in] hScale Raster horizontal units per model length unit, intended positive finite,
 * unchecked.
 * @param[in] vScale Raster vertical units per model vertical unit, intended positive finite,
 * unchecked.
 * @return Value containing the two products. No validation, coordinate transform, allocation or
 * state mutation.
 *
 * @details \ref BurnOptions to \ref BurnRule, converting both lengths.
 */
[[nodiscard]] BurnRule toRasterRule(const BurnOptions &opt, double hScale, double vScale);

// ── Identity ───────────────────────────────────────────────────────────────

/**
 * @brief Derive an eight-hex-character cache name from selected burn inputs and native numeric
 * bytes.
 * @param[in] demIdentity Borrowed source DEM identity string, unchanged; caller establishes
 * freshness.
 * @param[in] opt Borrowed options, unchanged; current geometric/burn/export options feed the
 * digest.
 * @param[in] profiles Borrowed ordered profiles, unchanged; IDs, centerline coordinates, bed,
 * lateral offsets and relative-elevation grids feed SHA-256. Reordering changes identity;
 * section metadata not represented by those arrays is not independently hashed.
 * @return First eight lowercase hex characters of the digest (32 bits). This is a cache hint,
 * NOT a collision-free identity or proof of file existence/freshness. Numeric bytes use native
 * representation, so cross-architecture canonical identity is not promised. Allocates
 * digest/encoding storage, no IO or state mutation, and retains no source references.
 *
 * @details Eight hex characters over {source DEM identity, options, resolved conduits and their
 * geometry} — the burned raster's filename stem.
 *
 * Identical inputs give an identical name, so a re-run is a no-op; the short fingerprint can
 * collide and does not replace a full source identity check.
 */
[[nodiscard]] QString burnFingerprint(const QString &demIdentity, const BurnOptions &opt,
                                      const QVector<BurnProfile> &profiles);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURN_H
