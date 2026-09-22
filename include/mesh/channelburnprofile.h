/*!
 * \file   channelburnprofile.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 *   - \ref sectionFromWidths for the analytic open shapes, fed by the engine's
 *     own width ladder (`XsectSampler::widthsAtDepths`).  Symmetric, which is
 *     what those shapes are.
 *   - \ref sectionFromTransect for IRREGULAR, fed by the raw [TRANSECTS] GR
 *     data plus the GUI modifier pair.  Asymmetric, which is the point —
 *     `XsectSampler::outline()` mirrors half-widths and therefore CANNOT
 *     supply a natural channel's real geometry (plan §16.1/12.3).
 * Resolving which producer a link needs touches the engine and therefore
 * belongs on the GUI thread, in the selector (P1), not here.
 */
#ifndef OPENSWMMVIS_MESH_CHANNELBURNPROFILE_H
#define OPENSWMMVIS_MESH_CHANNELBURNPROFILE_H

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtNumeric>

#include <cmath>

namespace mesh {

/*! \brief Where `s = 0` sits on the authored section — i.e. what the digitised
 *         link polyline is taken to follow (plan D-D). */
enum class SectionAnchor
{
    Thalweg,       ///< The deepest point. Default: a digitised polyline follows the visible channel.
    BankMidpoint,  ///< Midway between the bank stations. For surveyed-section workflows.
    StationZero    ///< Raw station 0, no shift.
};

/*!
 * \brief One cross-section as authored, in section coordinates and MODEL
 *        vertical units.
 *
 * \p station must be strictly ascending (the [TRANSECTS] storage invariant).
 * \p elevation is absolute or relative — \ref normalizeSection only ever uses
 * differences.  Bank stations and the roughness triple are optional (NaN).
 */
struct SectionGeometry
{
    QVector<double> station;
    QVector<double> elevation;
    double leftBank  = qQNaN();
    double rightBank = qQNaN();
    double nLeft     = qQNaN();
    double nChannel  = qQNaN();
    double nRight    = qQNaN();
};

/*!
 * \brief Every knob the burn takes (plan §3).
 *
 * Lengths are MODEL length units throughout; the DEM-unit conversion happens
 * once, in the rasterizer (plan §4.5 trap).
 */
struct BurnOptions
{
    // ── corridor geometry ────────────────────────────────────────────────
    double forceHalfWidth = 2.0;   ///< R. |s| <= R: the section REPLACES the DEM (D2).
    double maxHalfWidth   = 0.0;   ///< 0 = unbounded; else clip the corridor to +-this.
    bool   clipToBanks    = true;  ///< Stop at the bank stations — no floodplain extension (D-C).
    double bankPad        = 0.0;   ///< Extra distance beyond the banks before stopping.

    // ── resolution ───────────────────────────────────────────────────────
    double chainageStep   = 0.0;   ///< 0 = auto (P1 resolves min(demPixel/2, R/4); P0 falls back to R/4).
    double lateralStep    = 0.0;   ///< 0 = off; else no two corridor offsets are further apart than this.
    int    stringCount    = 2;     ///< Intermediate offset strings per side, beyond +-R, the banks and the extent.

    // ── longitudinal ─────────────────────────────────────────────────────
    SectionAnchor anchor  = SectionAnchor::Thalweg;
    double sectionBlend   = 0.0;   ///< 0 = prismatic per conduit; > 0 = morph e_rel over this distance
                                   ///<     either side of a shared node (§4.3).
    bool   enforceMonotone = false;///< Clamp the bed to a non-adverse slope. Default off (D-G): an
                                   ///<     adverse reach is usually real data — warn, do not fix.
    double maxIncision    = 0.0;   ///< 0 = unbounded; else never cut more than this below the DEM.
                                   ///<     Applied by the rasterizer (P1), not here.

    // ── selection ────────────────────────────────────────────────────────
    bool   burnStreets    = false; ///< SWMM_XSECT_STREET opt-in (D-F). The engine classifies a street
                                   ///<     as CLOSED, so the open-section gate already excludes it.

    // ── meshing ──────────────────────────────────────────────────────────
    bool   emitStrings    = true;  ///< Corridor lattice rows into the PSLG.
    bool   quadCorridor   = true;  ///< Corridor -> mesh::PatchMesh when quads are enabled (§16.2).
    double channelCellSize = 0.0;  ///< 0 = derive from the size field.
    double channelAspectMax = 4.0; ///< Streamwise-elongated cells are wanted here.
    bool   roughnessFromTransect = true; ///< n_left / n_channel / n_right -> per-cell Manning's n.

    // ── network surgery (gated by D-A / D-B) ─────────────────────────────
    bool   removeBurnedFrom1D    = true;
    bool   convertInterfaceNodes = true;
    bool   truncateAtBoundary    = true;
};

/*!
 * \brief A section after normalisation: elevations relative to the thalweg,
 *        stations shifted so `s = 0` is the anchor, extent clipped.
 *
 * \p sMin is negative and \p sMax positive whenever the clip left the anchor
 * inside the section, which \ref normalizeSection enforces.
 */
struct NormalizedSection
{
    QVector<double> station;   ///< Signed lateral offset, strictly ascending. 0 = the anchor.
    QVector<double> relZ;      ///< Elevation above the thalweg; min is exactly 0.
    double sMin = qQNaN();     ///< Clipped extent, left.
    double sMax = qQNaN();     ///< Clipped extent, right.
    double leftBank  = qQNaN();///< Bank stations in the same shifted frame (NaN when unset).
    double rightBank = qQNaN();
    double nLeft     = qQNaN();
    double nChannel  = qQNaN();
    double nRight    = qQNaN();

    [[nodiscard]] bool isValid() const noexcept
    {
        return station.size() >= 2 && station.size() == relZ.size()
               && std::isfinite(sMin) && std::isfinite(sMax) && sMin < sMax;
    }
};

/*! \brief One conduit's inputs to the burn. */
struct ChannelInput
{
    QString          conduitId;
    QVector<QPointF> centerline;  ///< Map CRS, >= 2 points, as digitised.
    double           zUp = 0.0;   ///< node1.invertElev + link.offset1 (profilebuilder.cpp:65).
    double           zDn = 0.0;   ///< node2.invertElev + link.offset2 (profilebuilder.cpp:72).
    SectionGeometry  section;
};

/*!
 * \brief One conduit's reconstructed bed (plan §3).
 *
 * \note Deviation from the plan's struct: the flat `sLeftBank` / `sRightBank` /
 *       `nLeft` / `nChannel` / `nRight` fields live on \ref section, which the
 *       blending pass needs to carry anyway.
 */
struct BurnProfile
{
    QString                  conduitId;
    QVector<QPointF>         centerline;  ///< Densified.
    QVector<double>          chainage;    ///< Parallel to \ref centerline, cumulative, ascending.
    QVector<double>          bedZ;        ///< Invert at each chainage station.
    QVector<double>          offsets;     ///< Lateral offsets s_k, ascending, signed, sMin..sMax.
    QVector<QVector<double>> relZ;        ///< relZ[i][k] = elevation above bedZ[i] at offsets[k].
    NormalizedSection        section;     ///< The section the profile was built from.

    [[nodiscard]] bool isValid() const noexcept
    {
        return chainage.size() >= 2 && chainage.size() == bedZ.size()
               && chainage.size() == relZ.size() && offsets.size() >= 2;
    }
    [[nodiscard]] double length() const noexcept
    {
        return chainage.isEmpty() ? 0.0 : chainage.last();
    }
};

// ── Section producers ──────────────────────────────────────────────────────

/*!
 * \brief Depth ladder for \ref sectionFromWidths, cosine-spaced over `[0, yFull]`.
 *
 * Uniform spacing under-integrates every shape whose width changes fastest at
 * the invert (parabola, power): the first linear segment cuts a fixed sliver off
 * the bed that no amount of depth refinement elsewhere recovers.  Cosine spacing
 * clusters samples at the invert and the crown, which is the same reasoning
 * `XsectSampler::outline()` gives for its own ladder.  \p samples is clamped to
 * [8, 512].
 */
[[nodiscard]] QVector<double> cosineDepthLadder(double yFull, int samples = 64);

/*!
 * \brief Symmetric section from a width ladder: `station = ±w(y)/2`, `elevation = y`.
 *
 * \p depths must be ascending and start at 0; \p widths is the engine's width
 * at each depth (`XsectSampler::widthsAtDepths`).  Use \ref cosineDepthLadder to
 * build \p depths.
 *
 * Only the first depth at which a given width occurs is kept, because the
 * station ladder must stay strictly ascending.  Two consequences, both correct
 * for a burn: a vertical wall (a repeated width, e.g. RECT_OPEN) reduces to the
 * corridor extent, since a raster cannot hold a wall anyway; and a width that
 * shrinks with depth — a closed shape, which the open-section gate excludes —
 * contributes nothing.
 */
[[nodiscard]] SectionGeometry sectionFromWidths(const QVector<double> &depths,
                                                const QVector<double> &widths);

/*!
 * \brief Section from raw [TRANSECTS] GR data plus the GUI modifier pair.
 *
 * Modifiers are applied to a COPY, exactly as the GUI spells them
 * (`transectchartview.cpp:249-255`): `station × stationMultiplier`,
 * `elevation + elevationOffset`.  Bank stations take the multiplier
 * (`Transect.cpp:455-456`).  The engine's internal UCF bookkeeping is NOT
 * reproduced — these values are already in model units.
 */
[[nodiscard]] SectionGeometry sectionFromTransect(const QVector<double> &stations,
                                                  const QVector<double> &elevations,
                                                  double leftBank, double rightBank,
                                                  double nLeft, double nChannel, double nRight,
                                                  double stationMultiplier = 1.0,
                                                  double elevationOffset   = 0.0);

/*! \brief Empty string when \p s can be burned; the reason otherwise. */
[[nodiscard]] QString validateSection(const SectionGeometry &s);

// ── Normalisation and lateral queries ──────────────────────────────────────

/*!
 * \brief Relative elevations, anchored, clipped (plan §4.1 steps 3-5).
 *
 * The thalweg anchor is the MIDPOINT of the minimum-elevation run, not its
 * first station: a trapezoid's flat bed is entirely minimal, and taking the
 * first station would shift the whole section by half the bed width.
 *
 * \param warnings Soft problems (bank fallback, R wider than the section).
 * \param err      Set on hard failure; the result is then `!isValid()`.
 */
[[nodiscard]] NormalizedSection normalizeSection(const SectionGeometry &s,
                                                 const BurnOptions &opt,
                                                 QStringList *warnings = nullptr,
                                                 QString *err = nullptr);

/*! \brief Elevation above the thalweg at signed offset \p s; NaN outside the
 *         clipped extent. */
[[nodiscard]] double relZAt(const NormalizedSection &ns, double s);

/*!
 * \brief The corridor's lateral offset ladder (plan §4.6), ascending.
 *
 * `{ sMin, -R, intermediates, 0, intermediates, +R, sMax }` plus the bank
 * stations, deduplicated, each clipped to the section extent; gaps wider than
 * `BurnOptions::lateralStep` are subdivided when that is set.
 */
[[nodiscard]] QVector<double> corridorOffsets(const NormalizedSection &ns,
                                              const BurnOptions &opt);

// ── Profile assembly ───────────────────────────────────────────────────────

/*! \brief Insert vertices so no segment of \p path is longer than \p step.
 *         Original vertices are always kept. \p step <= 0 returns \p path. */
[[nodiscard]] QVector<QPointF> densifyPolyline(const QVector<QPointF> &path, double step);

/*! \brief Cumulative Euclidean distance along \p path, starting at 0. */
[[nodiscard]] QVector<double> polylineChainage(const QVector<QPointF> &path);

/*!
 * \brief Build one conduit's bed surface (plan §4.1-§4.2).
 *
 * The bed is linear in chainage between the two end inverts.  An adverse reach
 * warns; it is only clamped when `BurnOptions::enforceMonotone` is set.
 */
[[nodiscard]] BurnProfile buildBurnProfile(const ChannelInput &in,
                                           const BurnOptions &opt,
                                           QStringList *warnings = nullptr,
                                           QString *err = nullptr);

/*! \brief Bed elevation at \p chainage (clamped to the profile's range). */
[[nodiscard]] double bedZAt(const BurnProfile &p, double chainage);

/*!
 * \brief Section elevation at (\p chainage, \p offset) — the value the burn
 *        writes.  NaN outside the corridor extent.
 * \param inExtent When non-null, receives whether \p offset was inside.
 */
[[nodiscard]] double sectionZAt(const BurnProfile &p, double chainage, double offset,
                                bool *inExtent = nullptr);

// ── Inter-section blending (plan §4.3) ─────────────────────────────────────

/*!
 * \brief Station-normalised blend of two sections, \p w = 0 gives \p a.
 *
 * Both sections are resampled onto `u ∈ [−1, 1]` (0 = anchor, ±1 = the
 * respective clipped extent) before mixing, which is what keeps a 3 m ditch
 * and a 30 m creek from producing a nonsense intermediate.
 */
[[nodiscard]] NormalizedSection blendSections(const NormalizedSection &a,
                                              const NormalizedSection &b,
                                              double w);

/*!
 * \brief Morph both profiles' lateral shape across their shared node.
 *
 * Over `[L − B, L]` of \p upstream and `[0, B]` of \p downstream the blend
 * weight runs 0 → 0.5 → 0, so the two agree exactly at the node.  Each profile
 * keeps its OWN offset ladder and extent — only `e_rel` morphs, which is what
 * keeps each conduit's corridor lattice conformal along its length.
 *
 * No-op when \p blend <= 0 or either profile is invalid.
 */
void blendAtSharedNode(BurnProfile &upstream, BurnProfile &downstream, double blend);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNPROFILE_H
