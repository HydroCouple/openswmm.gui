/**
 * @file      xsectsampler.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     RAII wrapper over the engine's standalone cross-section API.
 *
 * Slice SP.1 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * The engine ships a complete geometry API in openswmm_xsect.h that the GUI
 * had never consumed: an opaque SWMM_XSect handle plus width / area /
 * hydraulic-radius queries. Sampling `swmm_xsect_width_of_depth_array()` over
 * a depth ladder yields the exact outline the solver itself uses, for every
 * SWMM_XSectShape — which is what the section-preview panels draw instead of
 * the hand-drawn SVG thumbnails.
 *
 * Shape numbering: openswmm_xsect.h takes and returns SWMM_XSectShape codes
 * from openswmm_links.h — the SAME numbering space xsectshapegeom.h uses.
 * There is no second numbering space and no mapping table. Still spell shape
 * ids as SWMM_XSECT_* constants, never bare integers (see the drift warning
 * on XsectShapeRow in ui/properties/xsectshapegeom.h).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_XSECTSAMPLER_H
#define OPENSWMMVIS_SECTIONVIEW_XSECTSAMPLER_H

#include <QPolygonF>
#include <QVector>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>

namespace openswmmvis::sectionview
{

/*! Bank-full properties, as reported by swmm_xsect_full_properties(). */
struct XsectFullProps {
    double yFull = 0.0; //!< Full depth.
    double aFull = 0.0; //!< Area when full.
    double rFull = 0.0; //!< Hydraulic radius when full.
    double wMax = 0.0;  //!< Width at the widest point.
    double sFull = 0.0; //!< Section factor when full.
    double aMax = 0.0;  //!< Area of maximum flow.
    bool open = false;  //!< True for an open channel.
};

/*!
 * \class XsectSampler
 * \brief Owning handle on one standalone SWMM cross-section.
 *
 * Move-only (the engine handle is not refcounted). A default-constructed or
 * failed sampler is `!isValid()` and every query returns a zeroed result, so
 * callers can build one unconditionally and let the empty state fall through
 * to the widget's "no geometry" message.
 */
class XsectSampler
{
public:
    /**
     * @brief Construct invalid empty owning wrapper, no engine allocation. Queries return
     *   documented empty/zero sentinels.
     */
    XsectSampler() = default;
    /**
     * @brief Free owned standalone cross-section handle once, no effect if invalid. Does not touch
     *   source model; caller must not destroy concurrently with queries.
     */
    ~XsectSampler();

    /**
     * @param[in,out] other Ownership source, invalidated without freeing transferred handle.
     * @brief Move handle into new wrapper; no deep copy or engine mutation.
     */
    XsectSampler(XsectSampler &&other) noexcept;
    /**
     * @param[in,out] other Ownership source, becomes invalid on nonself move.
     * @return This wrapper after freeing prior owned handle and transferring source; self-move is
     *   no-op. External queries must be serialized with move/destruction.
     */
    XsectSampler &operator=(XsectSampler &&other) noexcept;
    /**
     * @brief Copy construction is disabled to prevent double-free of unique engine handle; use move
     *   or rebuild independent sampler. Unnamed source argument unavailable by design.
     */
    XsectSampler(const XsectSampler &) = delete;
    /**
     * @brief Copy assignment disabled for unique handle ownership. Unnamed source is never
     *   evaluated by an available operation; move assignment transfers ownership.
     */
    XsectSampler &operator=(const XsectSampler &) = delete;

    // ---- Construction ------------------------------------------------------

    /*! Self-contained shapes (everything except IRREGULAR / CUSTOM / STREET).
     *  \param shape  A SWMM_XSectShape code.
     *  \param si     true → metres/CMS, false → feet/CFS.
     * @param[in] geom1 Full depth/diameter in selected display length, shape-specific semantics per
     *   SWMM_XSectShape.
     * @param[in] geom2 Finite second [XSECTIONS] geometry field; may be width, sediment depth,
     *   shape index or roughness depending on shape, unused 0.
     * @param[in] geom3 Finite third shape-specific geometry field, lengths in selected units and
     *   dimensionless slopes/exponents as defined by engine, unused 0.
     * @param[in] geom4 Finite fourth shape-specific geometry field, unused 0.
     * @return Independent move-only owning sampler, invalid if engine rejects
     *   shape/finite/geometry/unit requirements. No detailed error exposed and input model not
     *   required.
     */
    [[nodiscard]] static XsectSampler fromShape(int shape, double geom1, double geom2, double geom3,
                                                double geom4, bool si);

    /*! Deep-copies the geometry the engine actually built for a link,
     *  including transect / shape-curve / street tables — the only way to get
     *  IRREGULAR / CUSTOM / STREET geometry without re-supplying the tables.
     *
     *  \note The engine returns SWMM_ERR_LIFECYCLE while the model is still in
     *        the BUILDING state (geometry not resolved yet), so this yields an
     *        invalid sampler during programmatic model construction. Callers
     *        that have the tables to hand should prefer fromTransect() /
     *        fromStreet(), which work in every lifecycle state.
     * @param[in] engine Optional borrowed live engine with resolved geometry; null rejects.
     * @param[in] linkIdx Zero-based link index, negative rejects; engine validates upper
     *   bound/type/lifecycle.
     * @return Deep independent copy in ENGINE display/flow units, valid after model
     *   close/destruction. Invalid for unresolved BUILDING geometry, absent cross-section (e.g.
     *   pump), invalid handle/index or other API failure; no logging/status detail. Does not alter
     *   engine.
     */
    [[nodiscard]] static XsectSampler fromLink(SWMM_Engine engine, int linkIdx);

    /*! Irregular (natural channel) section from raw transect data.
     *  Roughness values are load-bearing: the hydraulic-radius table is
     *  conveyance-weighted across the overbank / channel subsections.
     * @param[in] stations Borrowed ascending finite station coordinates in selected display length,
     *   copied by engine.
     * @param[in] elevations Borrowed matching finite elevation values in selected length; only
     *   min(array sizes) pairs consumed.
     * @param[in] xLeftBank Left-bank transition station in same units, finite.
     * @param[in] xRightBank Right-bank transition station; equal banks denote no overbanks.
     * @param[in] nLeft Finite nonnegative overbank Manning n; 0 uses channel.
     * @param[in] nChannel Finite positive channel Manning n.
     * @param[in] nRight Finite nonnegative right overbank n; 0 uses channel.
     * @param[in] lengthFactor Finite nonnegative main-channel/floodplain length ratio; 0 means 1.
     * @param[in] si True metres/CMS, false feet/CFS.
     * @return Independent copied-table sampler, invalid if fewer than 2 paired points, nonpositive
     *   channel roughness or engine rejects transect. Extra unmatched input entries silently
     *   ignored, sizes must fit int. Roughness affects hydraulic radius/conveyance, no input
     *   mutation or model dependency.
     */
    [[nodiscard]] static XsectSampler fromTransect(const QVector<double> &stations,
                                                   const QVector<double> &elevations,
                                                   double xLeftBank, double xRightBank,
                                                   double nLeft, double nChannel, double nRight,
                                                   double lengthFactor, bool si);

    /*! Street section from [STREETS] parameters (slopes in percent).
     * @param[in] width Finite positive curb-to-crown road width in selected length units.
     * @param[in] curbHeight Finite positive curb height in selected length.
     * @param[in] slope Road cross slope in PERCENT, not fractional rise/run.
     * @param[in] roughness Positive road Manning n.
     * @param[in] gutterDepression Gutter depression in selected length.
     * @param[in] gutterWidth Gutter width in selected length.
     * @param[in] sides 1 for half street, 2 for full street.
     * @param[in] backWidth Backing width in selected length, 0 none.
     * @param[in] backSlope Backing slope in PERCENT.
     * @param[in] backRoughness Backing Manning n.
     * @param[in] si True metres/CMS, false feet/CFS.
     * @return Independent owning sampler, invalid on engine rejection/degenerate resulting table.
     *   Caller supplies finite valid street parameters; wrapper adds no validation/reporting. No
     *   model handle retained.
     */
    [[nodiscard]] static XsectSampler fromStreet(double width, double curbHeight, double slope,
                                                 double roughness, double gutterDepression,
                                                 double gutterWidth, int sides, double backWidth,
                                                 double backSlope, double backRoughness, bool si);

    /*! Custom section from a normalized SHAPE curve (depths and widths are
     *  y/yFull and w/wMax in [0,1]).
     * @param[in] yFull Finite positive full depth in selected display length.
     * @param[in] normDepths Borrowed finite ascending normalized y/yFull profile, normally [0,1].
     * @param[in] normWidths Borrowed normalized width/wMax values corresponding to depths; only
     *   min(array sizes) pairs consumed.
     * @param[in] si True metres/CMS, false feet/CFS.
     * @return Independent copied custom-shape sampler or invalid for fewer than 2 paired
     *   points/nonpositive depth/engine rejection. Unmatched tail ignored, array count must fit
     *   int. No input/model mutation; ordered valid normalized profile caller responsibility.
     */
    [[nodiscard]] static XsectSampler fromCurve(double yFull, const QVector<double> &normDepths,
                                                const QVector<double> &normWidths, bool si);

    // ---- Queries -----------------------------------------------------------

    /**
     * @return Whether owned opaque handle is nonnull; no full geometry/engine lifecycle query,
     *   DUMMY can be valid. No ownership transfer.
     */
    [[nodiscard]] bool isValid() const noexcept { return m_handle != nullptr; }

    /*! SWMM_XSectShape code, or -1 when invalid.
     * @return Engine shape code, or -1 for null handle/API failure; no exception/status
     *   differentiation.
     */
    [[nodiscard]] int shape() const;

    /*! Bank-full properties; all zeros when invalid.
     * @return Copied bank-full geometry: y/r/w display length, areas length², section factor engine
     *   display length^(8/3), and hydraulic open flag. All zero/false if absent/full-property API
     *   fails; open-query failure alone retains geometry with false. STREET hydraulic open flag
     *   differs from drawing predicate used by linkFullDepth. No source mutation.
     */
    [[nodiscard]] XsectFullProps fullProps() const;

    /*! Width at each depth (one output per input). Empty on failure.
     * @param[in] depths Finite nonnegative depths in HANDLE display length; count must fit int.
     *   Values above crown follow engine clamping.
     * @return One width in same length units per depth, or empty for invalid handle, empty input,
     *   or ANY invalid input/API failure. Whole vector result discarded on failure, no partial
     *   result/error index. Input unchanged; handle must stay live and not move/free during call.
     */
    [[nodiscard]] QVector<double> widthsAtDepths(const QVector<double> &depths) const;

    /*!
     * \brief Closed outline polygon of the section, in section coordinates.
     *
     * The polygon is centred on x = 0 with y measured UP from the invert
     * (y = 0 at the invert, y = yFull at the crown / top of bank), i.e. the
     * natural coordinate system for a section drawing — the painter flips Y.
     *
     * Built by sampling half-width over `samples + 1` depths and mirroring, so
     * it reproduces the engine's own geometry for every shape, including the
     * tabulated ones. The ladder is cosine-spaced, not uniform: width changes
     * fastest at the invert and the crown, so that is where the points need to
     * be (a uniform ladder makes circular pipes look faceted top and bottom).
     *
     * \param samples  Depth intervals; more samples smooth curved crowns.
     *                 Clamped to [8, 512]. The default is generous because a
     *                 section is drawn once per selection, not per frame.
     * \returns An empty polygon when the sampler is invalid or the section has
     *          no geometry (SWMM_XSECT_DUMMY, or a degenerate depth).
     * @note Result has 2*(clamped intervals+1) points, right half bottom→top then mirrored left
     *   top→bottom; polygon fill implicitly closes ring and endpoints may repeat at zero width. It
     *   is a symmetric hydraulic-width representation, not original transect bank/station positions
     *   or asymmetric street layout. All coordinates use handle display length. Failure/invalid
     *   full depth/width sampling yields empty, source not mutated.
     */
    [[nodiscard]] QPolygonF outline(int samples = 240) const;

private:
    /**
     * @param[in] handle Opaque standalone SWMM_XSect whose sole ownership transfers to wrapper;
     *   null allowed. Must not already be owned/freed elsewhere.
     * @note No validation, reference count increment or clone. Private factories ensure success
     *   before adopting.
     */
    explicit XsectSampler(void *handle) noexcept : m_handle(handle) {}
    /**
     * @brief Free current handle and set null, invalid no-op; queries/moves/free must be serialized
     *   by owner. Does not affect any original model/table or notify UI.
     */
    void reset() noexcept;

    void *m_handle = nullptr; //!< SWMM_XSect (opaque void*).
};

// ---- Link geometry resolution (shared by the Section View and the profile
//      plot, so both surfaces read one implementation) ----------------------

/*!
 * \brief Sampler for the street at \p streetIdx, built from [STREETS].
 *
 * Preferred over the link-derived handle because swmm_street_get_params reads
 * stored input and therefore works in EVERY lifecycle state, whereas
 * swmm_link_create_xsect needs resolved geometry and returns
 * SWMM_ERR_LIFECYCLE while the model is still being edited.
 * @param[in] engine Optional borrowed engine; getters inspect stored input without requiring
 *   resolved link geometry.
 * @param[in] streetIdx Zero-based street row, bounded against swmm_street_count.
 * @param[in] si Units to use when interpreting retrieved dimensions; MUST agree with engine display
 *   units, no automatic verification.
 * @return Independent street sampler, invalid for null/out-of-range/parameter getter or street
 *   construction failure. Reads all road/gutter/backing parameters, no engine edits.
 */
[[nodiscard]] XsectSampler samplerFromStreetIndex(SWMM_Engine engine, int streetIdx, bool si);

/*!
 * \brief Sampler for a link's cross-section, given the values
 *        `swmm_link_get_xsect` reported for it.
 *
 * \warning For STREET **and** IRREGULAR, `geom1` is a TABLE INDEX, not a
 *          depth — the engine resolves the retained name back to an index for
 *          both (openswmm_links_impl.cpp, `swmm_link_get_xsect`). Callers must
 *          never use geom1 as a dimension for those two shapes; take the depth
 *          from fullProps().yFull instead, which is what linkFullDepth() does.
 *          Every other shape reports a real depth in geom1.
 * @param[in] engine Borrowed engine for tabulated-link/street lookup, can be null for
 *   self-contained shapes.
 * @param[in] linkIdx Zero-based link index used by model-derived fallback, upper bound validated by
 *   engine.
 * @param[in] shape SWMM_XSectShape code returned by link getter.
 * @param[in] g1 First reported field: table index for STREET/IRREGULAR, full depth for ordinary
 *   self-contained shapes.
 * @param[in] g2 Second reported geometry field, interpreted per shape.
 * @param[in] g3 Third reported geometry field, interpreted per shape.
 * @param[in] g4 Fourth reported geometry field, interpreted per shape.
 * @param[in] si Expected dimensional interpretation, must agree with source engine; link-derived
 *   sampler inherits actual engine units.
 * @return Independent owning sampler: IRREGULAR/CUSTOM use fromLink; STREET first rounds g1 to
 *   street index and builds from input, then fromLink fallback; others fromShape. Failure invalid.
 *   g1 must be finite/representable for lround/narrowing on STREET, no local guard. No input/model
 *   mutation.
 */
[[nodiscard]] XsectSampler samplerForLink(SWMM_Engine engine, int linkIdx, int shape, double g1,
                                          double g2, double g3, double g4, bool si);

/*!
 * \brief A link's true full depth (crown height above the invert).
 *
 * Returns \p g1 directly for the shapes that report a depth there, and falls
 * back to the sampler only for STREET / IRREGULAR / CUSTOM — so the common
 * closed-pipe path costs nothing. Returns 0 when the geometry cannot be
 * resolved (the caller then has no crown to draw).
 *
 * \param[out] outOpenTop  When non-null, receives whether the section should
 *                         be DRAWN with an open top. This is a presentation
 *                         predicate, not the engine's hydraulic one: it is
 *                         `swmm_xsect_is_open() || shape == STREET`. The
 *                         engine deliberately classifies a street as closed
 *                         (its isOpen whitelist omits STREET_XSECT), but a
 *                         street drawn with a soffit line reads as a box
 *                         culvert, which it is not.
 * @param[in] engine Borrowed engine for tabulated-link/street lookup, can be null for
 *   self-contained shapes.
 * @param[in] linkIdx Zero-based link index used by model-derived fallback, upper bound validated by
 *   engine.
 * @param[in] shape SWMM_XSectShape code returned by link getter.
 * @param[in] g1 First reported field: table index for STREET/IRREGULAR, full depth for ordinary
 *   self-contained shapes.
 * @param[in] g2 Second reported geometry field, interpreted per shape.
 * @param[in] g3 Third reported geometry field, interpreted per shape.
 * @param[in] g4 Fourth reported geometry field, interpreted per shape.
 * @param[in] si Expected dimensional interpretation, must agree with source engine; link-derived
 *   sampler inherits actual engine units.
 * @return Ordinary-shape g1 returned VERBATIM without geometry validation, including
 *   invalid/nonfinite/negative values; table/custom shapes return resolved full depth or 0 on
 *   failure. No logging/error distinction.
 * @note Optional outOpenTop receives sampled hydraulic open flag OR STREET. Ordinary shape only
 *   constructs sampler when output pointer requested; failed sampler reports false while g1 still
 *   returned. STREET remains visually open even unresolved. Does not mutate engine.
 */
[[nodiscard]] double linkFullDepth(SWMM_Engine engine, int linkIdx, int shape, double g1, double g2,
                                   double g3, double g4, bool si, bool *outOpenTop = nullptr);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_XSECTSAMPLER_H
