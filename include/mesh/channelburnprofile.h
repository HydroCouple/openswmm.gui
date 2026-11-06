/**
 * @file      channelburnprofile.h
 * @brief     Open-channel bathymetry reconstruction — phase P0 of
 *            workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§4.1-§4.3).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Open-channel bathymetry reconstruction — phase P0 of
 * workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§4.1-§4.3).
 *
 * WHAT THIS IS.  One conduit plus its cross-section is turned into a 3D bed
 * surface: a densified centreline, a bed elevation interpolated between the
 * two end inverts, and a lateral shape sampled at a fixed ladder of signed
 * offsets.  That is everything the raster burn (P1) and the corridor lattice
 * (P2/P3) need, and nothing else — no GDAL, no engine handle, no Qt GUI.
 * The whole header is Qt Core only so it unit-tests headless.
 *
 * WHY RELATIVE ELEVATIONS.  A transect's absolute elevations are a survey
 * datum that may agree with neither the node inverts nor the DEM.  The one
 * quantity that is reliably the modeller's intent is the SHAPE, so the shape
 * is normalised to `e_rel(s) = elev(s) − min(elev)` and re-anchored on the
 * interpolated invert.  The burn is then consistent with 1D hydraulics by
 * construction (plan §4.1, D-I).
 *
 * WHERE THE SECTION COMES FROM.  Two producers, both pure:
 *   - \ref mesh::sectionFromWidths for the analytic open shapes, fed by the engine's
 *     own width ladder (`XsectSampler::widthsAtDepths`).  Symmetric, which is
 *     what those shapes are.
 *   - \ref mesh::sectionFromTransect for IRREGULAR, fed by the raw [TRANSECTS] GR
 *     data plus the GUI modifier pair.  Asymmetric, which is the point —
 *     `XsectSampler::outline()` mirrors half-widths and therefore CANNOT
 *     supply a natural channel's real geometry (plan §16.1/12.3).
 * Resolving which producer a link needs touches the engine and therefore
 * belongs on the GUI thread, in the selector (P1), not here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_CHANNELBURNPROFILE_H
#define OPENSWMMVIS_MESH_CHANNELBURNPROFILE_H

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtNumeric>

#include <cmath>

namespace mesh
{

/*! \brief Where `s = 0` sits on the authored section — i.e. what the digitised
 *         link polyline is taken to follow (plan D-D). */
enum class SectionAnchor {
    Thalweg,      ///< The deepest point. Default: a digitised polyline follows the visible channel.
    BankMidpoint, ///< Midway between the bank stations. For surveyed-section workflows.
    StationZero   ///< Raw station 0, no shift.
};

/*!
 * \brief One cross-section as authored, in section coordinates and MODEL
 *        vertical units.
 *
 * \p station must be strictly ascending (the [TRANSECTS] storage invariant).
 * \p elevation is absolute or relative — \ref normalizeSection only ever uses
 * differences.  Bank stations and the roughness triple are optional (NaN).
 */
struct SectionGeometry {
    QVector<double> station;
    QVector<double> elevation;
    double leftBank = qQNaN();
    double rightBank = qQNaN();
    double nLeft = qQNaN();
    double nChannel = qQNaN();
    double nRight = qQNaN();
};

/*!
 * \brief Every knob the burn takes (plan §3).
 *
 * Persisted lengths use model units, except geometryTolerance (metres).
 * The dialog converts horizontal lengths to mesh-CRS units and elevations to
 * output vertical units before preparing worker inputs.
 */
struct BurnOptions {
    // ── corridor geometry ────────────────────────────────────────────────
    double forceHalfWidth = 2.0; ///< R. |s| <= R: the section REPLACES the DEM (D2).
    double maxHalfWidth = 0.0;   ///< 0 = unbounded; else clip the corridor to +-this.
    bool clipToBanks = true;     ///< Stop at the bank stations — no floodplain extension (D-C).
    double bankPad = 0.0;        ///< Extra distance beyond the banks before stopping.

    // ── resolution ───────────────────────────────────────────────────────
    double chainageStep =
        0.0; ///< 0 = retain geometric stations; never infer distance from raster degrees.
    double lateralStep =
        0.0; ///< 0 = off; else no two corridor offsets are further apart than this.
    int stringCount =
        2; ///< Intermediate offset strings per side, beyond +-R, the banks and the extent.

    // ── longitudinal ─────────────────────────────────────────────────────
    SectionAnchor anchor = SectionAnchor::Thalweg;
    double sectionBlend = 0.0; ///< 0 = prismatic per conduit; > 0 = morph e_rel over this distance
                               ///<     either side of a shared node (§4.3).
    bool enforceMonotone = false; ///< Clamp the bed to a non-adverse slope. Default off (D-G): an
                                  ///<     adverse reach is usually real data — warn, do not fix.
    double maxIncision = 0.0;     ///< 0 = unbounded; else never cut more than this below the DEM.
                                  ///<     Applied by the rasterizer (P1), not here.

    // ── selection ────────────────────────────────────────────────────────
    bool burnStreets = false; ///< SWMM_XSECT_STREET opt-in (D-F). The engine classifies a street
                              ///<     as CLOSED, so the open-section gate already excludes it.

    // ── meshing ──────────────────────────────────────────────────────────
    bool emitStrings = true;       ///< Corridor lattice rows into the PSLG.
    bool quadCorridor = true;      ///< Corridor -> mesh::PatchMesh when quads are enabled (§16.2).
    double channelCellSize = 0.0;  ///< 0 = derive from the size field.
    double channelAspectMax = 4.0; ///< Streamwise-elongated cells are wanted here.
    double geometryTolerance =
        0.05; ///< Channel elevation tolerance, in metres (independent of DEM tolerance).
    qint64 maxCorridorVertices =
        2000000; ///< Preparation allocation guard; further limited by mesh cell budget.
    bool roughnessFromTransect = true; ///< n_left / n_channel / n_right -> per-cell Manning's n.

    // ── network surgery (gated by D-A / D-B) ─────────────────────────────
    bool removeBurnedFrom1D = true;
    bool convertInterfaceNodes = true;
    bool truncateAtBoundary = true;
    double connectorLength = 0.0; ///< 1D stretch kept before a connector outfall; 0 = automatic
                                  ///<     (routing step × √(g·D), at least two channel cells).
    double routingStepSeconds =
        0.0; ///< Model routing step for the automatic length; not persisted.
};

/*!
 * \brief Which conduits participate (plan §3, §4.8).
 *
 * No new "burn me" flag is proposed: `[TAGS]` already round-trips, is editable
 * in every attribute table and is filterable, so a parallel boolean would be a
 * second source of truth for the same thing.
 */
struct BurnSelector {
    enum class Mode {
        AllOpen,     ///< Every conduit with an open section.
        ByQuery,     ///< A core/queryparser expression, e.g. Tag LIKE '%creek%'.
        ExplicitList ///< The map selection, or a saved list.
    };

    Mode mode = Mode::AllOpen;
    QString query;
    QStringList conduitIds;
    /*! The `fullProps().open` gate. On by default and meant to stay on: a
     *  culvert is a structure, not terrain, and burning it cuts a trench where
     *  there is a road (D-E). */
    bool requireOpenSection = true;
};

/*!
 * \brief Everything the Channel Burn-in tab holds, as one persisted unit.
 *
 * Kept here rather than beside \ref resolveBurnSet so the project serializer
 * can round-trip it without pulling in the engine headers.
 */
struct ChannelBurnSettings {
    bool enabled = false;
    bool exportRaster = false;
    QString exportDemPath;
    QString exportDirectory;
    double exportRasterZToSI = 1.0;
    int maxMeshCells = 2000000;
    BurnSelector selector;
    BurnOptions options;
};

/*!
 * \brief A section after normalisation: elevations relative to the thalweg,
 *        stations shifted so `s = 0` is the anchor, extent clipped.
 *
 * \p sMin is negative and \p sMax positive whenever the clip left the anchor
 * inside the section, which \ref normalizeSection enforces.
 */
struct NormalizedSection {
    QVector<double> station;   ///< Signed lateral offset, strictly ascending. 0 = the anchor.
    QVector<double> relZ;      ///< Elevation above the thalweg; min is exactly 0.
    double sMin = qQNaN();     ///< Clipped extent, left.
    double sMax = qQNaN();     ///< Clipped extent, right.
    double leftBank = qQNaN(); ///< Bank stations in the same shifted frame (NaN when unset).
    double rightBank = qQNaN();
    double nLeft = qQNaN();
    double nChannel = qQNaN();
    double nRight = qQNaN();

    /**
     * @brief Check minimal storage and clipped-extent prerequisites for section queries.
     * @return True for >=2 matching station/relZ entries and finite strictly ordered sMin/sMax.
     * Does not check station ordering/finite values, relative elevations, bank metadata or
     * anchor containment. Read-only, constant time, no allocation; stronger invariants come from
     * normalization/caller review.
     */
    [[nodiscard]] bool isValid() const noexcept
    {
        return station.size() >= 2 && station.size() == relZ.size() && std::isfinite(sMin) &&
               std::isfinite(sMax) && sMin < sMax;
    }
};

/*! \brief One conduit's inputs to the burn. */
struct ChannelInput {
    QString conduitId;
    QVector<QPointF> centerline; ///< Map CRS, >= 2 points, as digitised.
    double zUp = 0.0;            ///< node1.invertElev + link.offset1 (profilebuilder.cpp:65).
    double zDn = 0.0;            ///< node2.invertElev + link.offset2 (profilebuilder.cpp:72).
    SectionGeometry section;
};

/*!
 * \brief One conduit's reconstructed bed (plan §3).
 *
 * \note Deviation from the plan's struct: the flat `sLeftBank` / `sRightBank` /
 *       `nLeft` / `nChannel` / `nRight` fields live on \ref section, which the
 *       blending pass needs to carry anyway.
 */
struct BurnProfile {
    QString conduitId;
    QVector<QPointF> centerline;   ///< Densified.
    QVector<double> chainage;      ///< Parallel to \ref centerline, cumulative, ascending.
    QVector<double> bedZ;          ///< Invert at each chainage station.
    QVector<double> offsets;       ///< Lateral offsets s_k, ascending, signed, sMin..sMax.
    QVector<QVector<double>> relZ; ///< relZ[i][k] = elevation above bedZ[i] at offsets[k].
    NormalizedSection section;     ///< The section the profile was built from.

    /**
     * @brief Check the profile's outer array counts and minimum lateral ladder size.
     * @return True for >=2 equal chainage/bedZ/relZ counts and >=2 offsets. Does NOT verify
     * centerline count, row widths, ascending/finite chainage or offsets, section validity or
     * geometry. Queries/builders require those stronger caller invariants. Read-only constant
     * time, no allocation.
     */
    [[nodiscard]] bool isValid() const noexcept
    {
        return chainage.size() >= 2 && chainage.size() == bedZ.size() &&
               chainage.size() == relZ.size() && offsets.size() >= 2;
    }
    /**
     * @brief Return the last stored cumulative chainage, or zero for no stations.
     * @return Stored horizontal-frame length; not recomputed or validated, so malformed profiles
     * can return nonfinite/negative values. Read-only constant time, no allocation or reference
     * retention.
     */
    [[nodiscard]] double length() const noexcept
    {
        return chainage.isEmpty() ? 0.0 : chainage.last();
    }
};

// ── Section producers ──────────────────────────────────────────────────────

/**
 * @brief Generate an inclusive depth ladder clustered near invert and full depth.
 * @param[in] yFull Positive finite full depth in model vertical units; invalid/nonpositive
 * returns empty.
 * @param[in] samples Number of intervals, clamped to [8,512], not number of output points.
 * @return Owned n+1 values spanning zero through yFull via cosine spacing. No engine query,
 * coordinate conversion or state mutation; allocation bounded to 513 doubles.
 *
 * @details Depth ladder for \ref mesh::sectionFromWidths, cosine-spaced over `[0, yFull]`.
 *
 * Uniform spacing under-integrates every shape whose width changes fastest at the invert
 * (parabola, power): the first linear segment cuts a fixed sliver off the bed that no amount of
 * depth refinement elsewhere recovers.  Cosine spacing clusters samples at the invert and the
 * crown, which is the same reasoning `XsectSampler::outline()` gives for its own ladder.  \p
 * samples is clamped to [8, 512].
 */
[[nodiscard]] QVector<double> cosineDepthLadder(double yFull, int samples = 64);

/**
 * @brief Reconstruct a symmetric height-field section from paired depth and full-width samples.
 * @param[in] depths Borrowed intended ascending model-unit depths, normally beginning zero,
 * unchanged.
 * @param[in] widths Borrowed corresponding full widths in model horizontal length units,
 * unchanged; only min(array counts) pairs used, though full-width scale scans every width value.
 * @return Owned section, empty for no usable width. Skips nonfinite/negative-width/nonascending
 * samples and shrinking widths. Repeated exact widths retain toe/crest using a small outward
 * batter (up to 0.1% full width per side, further capped by next widening), rather than
 * discarding wall height. Mirrors half-widths with one zero-width invert station. Bank/roughness
 * metadata remains unset. Does not validate complete output or infer units; caller uses
 * validateSection(). No external state.
 *
 * @details Symmetric section from a width ladder: `station = ±w(y)/2`, `elevation = y`.
 *
 * \p depths must be ascending and start at 0; \p widths is the engine's width at each depth
 * (`XsectSampler::widthsAtDepths`).  Use \ref cosineDepthLadder to build \p depths.
 *
 * Repeated exact widths retain toe and crest with a small outward batter, so a height field can
 * represent wall height approximately. Shrinking widths are skipped; callers validate the
 * result.
 */
[[nodiscard]] SectionGeometry sectionFromWidths(const QVector<double> &depths,
                                                const QVector<double> &widths);

/**
 * @brief Copy raw asymmetric transect data with station scaling and elevation datum shift.
 * @param[in] stations Borrowed station coordinates, unchanged; intended strictly ascending.
 * @param[in] elevations Borrowed elevations, unchanged; min(counts) pairs used, <2 returns
 * empty.
 * @param[in] leftBank Optional finite left bank station, otherwise retained as NaN.
 * @param[in] rightBank Optional finite right bank station, otherwise NaN.
 * @param[in] nLeft Left overbank Manning value, copied without validation.
 * @param[in] nChannel Channel Manning value, copied without validation.
 * @param[in] nRight Right overbank Manning value, copied without validation.
 * @param[in] stationMultiplier Station/bank multiplier; nonfinite or zero becomes 1. Negative
 * mirrors geometry then reverses pair order and swaps bank/overbank roughness sides.
 * @param[in] elevationOffset Finite additive elevation offset; nonfinite becomes zero.
 * @return Owned section in caller's model units; no engine UCF conversion, source sorting or
 * physical value audit. Negative multiplier handling assumes original stations ascend. No
 * retained references.
 *
 * @details Section from raw [TRANSECTS] GR data plus the GUI modifier pair.
 *
 * Modifiers are applied to a COPY, exactly as the GUI spells them
 * (`transectchartview.cpp:249-255`): `station × stationMultiplier`, `elevation +
 * elevationOffset`.  Bank stations take the multiplier (`Transect.cpp:455-456`).  The engine's
 * internal UCF bookkeeping is NOT reproduced — these values are already in model units.
 */
[[nodiscard]] SectionGeometry
sectionFromTransect(const QVector<double> &stations, const QVector<double> &elevations,
                    double leftBank, double rightBank, double nLeft, double nChannel, double nRight,
                    double stationMultiplier = 1.0, double elevationOffset = 0.0);

/**
 * @brief Validate paired finite geometry and strictly ascending station coordinates.
 * @param[in] s Borrowed section, unchanged; no bank/roughness metadata or physical units
 * checked.
 * @return Empty for >=2 matched finite station/elevation pairs with strictly increasing stations
 * and positive span; otherwise owned first diagnostic. Does not require nonnegative elevations,
 * a zero station, finite arithmetic span, valid banks or hydraulic suitability. Allocates
 * diagnostic only.
 *
 * @details Empty string when \p s can be burned; the reason otherwise.
 */
[[nodiscard]] QString validateSection(const SectionGeometry &s);

// ── Normalisation and lateral queries ──────────────────────────────────────

/**
 * @brief Validate, datum-normalize, anchor and clip a cross-section without changing source
 * arrays.
 * @param[in] s Borrowed model-frame geometry, unchanged; validateSection() applies first.
 * @param[in] opt Borrowed options, unchanged; lengths must already share section horizontal
 * units. Anchor chooses thalweg low-station midpoint, bank midpoint or raw zero; missing bank
 * midpoint warns and falls back. Clips extent to section, optional banks plus nonnegative pad
 * and positive half-width cap.
 * @param[in,out] warnings Optional borrowed list, appended for fallback/forced-width overflow,
 * not cleared.
 * @param[out] err Optional borrowed hard-failure diagnostic, not cleared on success and not
 * retained.
 * @return Owned matching stations/relative elevations, copied roughness/banks and valid clipped
 * extents, or invalid section on failure. Clip must retain zero within inclusive limits and
 * nontrivial width; failure after copying can retain arrays with NaN extents. Thalweg minimum
 * uses relief-relative tolerance and midpoint of first/last near-minimal stations, even across
 * separated minima. No densification, profile building, engine/file changes, coordinate
 * transform or retained caller references.
 *
 * @details Relative elevations, anchored, clipped (plan §4.1 steps 3-5).
 *
 * The thalweg anchor is the MIDPOINT of the minimum-elevation run, not its first station: a
 * trapezoid's flat bed is entirely minimal, and taking the first station would shift the whole
 * section by half the bed width.
 */
[[nodiscard]] NormalizedSection normalizeSection(const SectionGeometry &s, const BurnOptions &opt,
                                                 QStringList *warnings = nullptr,
                                                 QString *err = nullptr);

/**
 * @brief Interpolate elevation above the thalweg at one clipped lateral offset.
 * @param[in] ns Borrowed normalized section, unchanged; ascending finite station geometry
 * required.
 * @param[in] s Finite lateral offset in section horizontal units, inclusive clipped bounds.
 * @return Piecewise-linear relative elevation in section vertical units; NaN for invalid
 * section, nonfinite/out-of-extent query or failed interpolation. No extrapolation, allocation
 * or mutation.
 *
 * @details Elevation above the thalweg at signed offset \p s; NaN outside the clipped extent.
 */
[[nodiscard]] double relZAt(const NormalizedSection &ns, double s);

/**
 * @brief Construct the deduplicated ascending lateral station ladder for a valid section.
 * @param[in] ns Borrowed normalized section, unchanged; valid aligned finite ascending geometry
 * and anchor within extent required beyond minimal isValid() count checks.
 * @param[in] opt Borrowed horizontal-unit widths/steps, unchanged. Includes zero, clipped ends,
 * real slope breaks, clipped +/-R, interior banks and stringCount fractions on each side.
 * Positive lateralStep subdivides wider gaps. Counts/step quotients must fit int; caller bounds
 * stringCount and memory budget.
 * @return Owned offsets, empty for invalid section or nonfinite/overbudget subdivision.
 * Coalesces offsets within max(1e-9,width*1e-9); does not sort/mutate source or perform CRS
 * conversion. Initial feature collection is not independently capped by maxCorridorVertices;
 * allocation may occur before refusal.
 *
 * @details The corridor's lateral offset ladder (plan §4.6), ascending.
 *
 * `{ sMin, -R, intermediates, 0, intermediates, +R, sMax }` plus the bank stations,
 * deduplicated, each clipped to the section extent; gaps wider than `BurnOptions::lateralStep`
 * are subdivided when that is set.
 */
[[nodiscard]] QVector<double> corridorOffsets(const NormalizedSection &ns, const BurnOptions &opt);

// ── Profile assembly ───────────────────────────────────────────────────────

/**
 * @brief Insert evenly spaced Cartesian points into each segment exceeding the requested maximum
 * gap.
 * @param[in] path Borrowed ordered finite coordinates in one horizontal frame, unchanged;
 * duplicates retained.
 * @param[in] step Positive maximum gap in that frame; nonpositive/NaN returns original value
 * copy.
 * @return Owned or implicitly shared path preserving all original vertices. Positive finite
 * ratios and output counts must fit int/available memory; no count budget, cancellation, finite
 * audit or CRS-aware distance calculation here. Infinite step inserts nothing. No external state
 * or retained views.
 *
 * @details Insert vertices so no segment of \p path is longer than \p step. Original vertices
 * are always kept. \p step <= 0 returns \p path.
 */
[[nodiscard]] QVector<QPointF> densifyPolyline(const QVector<QPointF> &path, double step);

/**
 * @brief Accumulate planar Euclidean segment lengths from zero along ordered points.
 * @param[in] path Borrowed coordinates in one horizontal frame, unchanged; finite nonoverflowing
 * geometry required for meaningful distances. Repeated vertices yield repeated cumulative
 * values.
 * @return Owned parallel distance vector, empty for empty path. No geodesic/CRS conversion,
 * monotonic strictness enforcement, finite validation or caller-state mutation.
 *
 * @details Cumulative Euclidean distance along \p path, starting at 0.
 */
[[nodiscard]] QVector<double> polylineChainage(const QVector<QPointF> &path);

/**
 * @brief Assemble a densified linear-invert bed and prismatic lateral elevation grid.
 * @param[in] in Borrowed conduit ID, >=2 finite centerline points, finite endpoint elevations
 * and section, unchanged. XY/options share horizontal units, section/inverts share vertical
 * units.
 * @param[in] opt Borrowed normalization/spacing/budget options, unchanged; caller bounds finite
 * scales, string counts and int-sized output extents. maxCorridorVertices caps
 * stationCount*offsetCount before grid allocation; no engine, DEM or raster-unit lookup occurs.
 * @param[in,out] warnings Optional borrowed list, appended for normalization/adverse slope, not
 * cleared.
 * @param[out] err Optional borrowed diagnostic, set on hard failure, not cleared on success/not
 * retained.
 * @return Owned profile or invalid/partially populated profile on failure (budget failure
 * returns empty). Rejects invalid centerline/inverts/section, zero total length, unusable
 * lateral ladder or budget. Repeated centerline points may retain repeated chainages. Bed
 * linearly joins endpoints; adverse rise warns and stays unless enforceMonotone flattens
 * downstream to upstream. No blending/maxIncision, geometry transform, callbacks, file/engine
 * changes or retained references.
 *
 * @details Build one conduit's bed surface (plan §4.1-§4.2).
 *
 * The bed is linear in chainage between the two end inverts.  An adverse reach warns; it is only
 * clamped when `BurnOptions::enforceMonotone` is set.
 */
[[nodiscard]] BurnProfile buildBurnProfile(const ChannelInput &in, const BurnOptions &opt,
                                           QStringList *warnings = nullptr, QString *err = nullptr);

/**
 * @brief Interpolate the longitudinal bed with endpoint clamping.
 * @param[in] p Borrowed profile chainage/bed arrays, unchanged; matching counts and ascending
 * chainage required for interior interpolation, no full-profile validation.
 * @param[in] chainage Query in profile horizontal units; nonfinite query returns first stored
 * bed value.
 * @return Bed elevation in profile vertical units; endpoint value outside range, NaN for
 * empty/mismatched arrays. One station returns its value. No allocation, unit conversion or
 * state mutation.
 *
 * @details Bed elevation at \p chainage (clamped to the profile's range).
 */
[[nodiscard]] double bedZAt(const BurnProfile &p, double chainage);

/**
 * @brief Bilinearly interpolate bed-plus-relative section elevation on the stored profile grid.
 * @param[in] p Borrowed valid profile, unchanged; requires finite ascending chainage/offset
 * arrays, matching row widths and aligned bedZ/relZ storage beyond minimal isValid() checks.
 * @param[in] chainage Finite longitudinal query in profile horizontal units, REQUIRED; clamped
 * to endpoints. NaN is not rejected before bracket indexing and must not be supplied.
 * @param[in] offset Finite signed lateral query in the same horizontal units; outside ladder
 * yields NaN.
 * @param[out] inExtent Optional borrowed flag, reset false at entry then true after finite
 * lateral interpolants obtained; does not independently certify final finite bed elevation. Not
 * retained.
 * @return Elevation in profile vertical units, or NaN for checked
 * invalid/out-of-extent/interpolation failure. No extrapolation across lateral bounds,
 * allocation, source mutation or engine/file effects.
 *
 * @details Section elevation at (\p chainage, \p offset) — the value the burn writes.  NaN
 * outside the corridor extent.
 */
[[nodiscard]] double sectionZAt(const BurnProfile &p, double chainage, double offset,
                                bool *inExtent = nullptr);

// ── Inter-section blending (plan §4.3) ─────────────────────────────────────

/**
 * @brief Blend section shapes in normalized left/right station coordinates, preserving slope
 * breaks.
 * @param[in] a Borrowed section in common horizontal/vertical frame, unchanged; valid sorted
 * geometry required.
 * @param[in] b Borrowed compatible section, unchanged; zero must be interior with nonzero side
 * extents for normalized divisions. Minimal isValid() alone does not ensure this.
 * @param[in] w Finite blend weight, <=0 selects a, >=1 selects b; intermediate linearly mixes
 * geometry, extents, banks and roughness (NaN metadata propagates).
 * @return Owned blended section with near-duplicate stations removed and minimum relative
 * elevation renormalized to zero. Invalid a returns b, invalid b returns a; no full validity
 * guarantee on result. Allocates union station ladder/output; no unit conversion, source
 * mutation or retained references.
 *
 * @details Station-normalised blend of two sections, \p w = 0 gives \p a.
 *
 * Both sections are resampled onto `u ∈ [−1, 1]` (0 = anchor, ±1 = the respective clipped
 * extent) before mixing, which is what keeps a 3 m ditch and a 30 m creek from producing a
 * nonsense intermediate.
 */
[[nodiscard]] NormalizedSection blendSections(const NormalizedSection &a,
                                              const NormalizedSection &b, double w);

/**
 * @brief Morph only relative elevation rows at an assumed connected upstream/downstream
 * interface.
 * @param[in,out] upstream Borrowed profile, tail rows within blend distance modified in place.
 * @param[in,out] downstream Borrowed distinct profile, head rows modified in place; no aliasing
 * with upstream. Both require compatible frames, valid section geometry, finite sorted ladders,
 * matching row widths and intended shared endpoint; actual connectivity/elevation continuity is
 * not checked.
 * @param[in] blend Positive requested horizontal distance, capped by both stored profile
 * lengths; nonpositive/NaN or minimally invalid profiles cause no-op. Weights reach the same
 * 50/50 shape at the interface, but each profile retains offsets, extent, bed, section metadata
 * and geometry; offsets clamp to the intermediate extent when querying. Does not force absolute
 * bed continuity or add new stations, so sampling may omit parts of the transition. Allocates
 * temporary blended sections; no rollback, cancellation, external IO or retained pointers.
 *
 * @details Morph both profiles' lateral shape across their shared node.
 *
 * Over `[L − B, L]` of \p upstream and `[0, B]` of \p downstream the blend weight runs 0 → 0.5 →
 * 0, so the two agree exactly at the node.  Each profile keeps its OWN offset ladder and extent
 * — only `e_rel` morphs, which is what keeps each conduit's corridor lattice conformal along its
 * length.
 *
 * No-op when \p blend <= 0 or either profile is invalid.
 */
void blendAtSharedNode(BurnProfile &upstream, BurnProfile &downstream, double blend);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNPROFILE_H
