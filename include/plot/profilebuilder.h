/**
 * @file      profilebuilder.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Pure-logic data assembly for Slice BC's longitudinal profile
 *         plot: chainage, HGL/EGL time-series, and min/max envelopes.
 *
 *         ProfileBuilder is intentionally decoupled from SWMMModelLayer and
 *         the engine output reader so it can be unit-tested without a live
 *         model: the caller pre-fetches static topology (NodeStatic /
 *         LinkStatic) and full per-source time-series (SourceSeries) and
 *         hands them in; the builder derives HGL, EGL, and envelopes.
 *
 *         Rendering conventions assumed by callers (see ProfilePlotWidget):
 *           - Ground / rim at each path node = `invertElev + maxDepth`
 *             (NodeStatic carries both; no DEM is consulted in v1).
 *           - Per-conduit geometry is derived from link `length`, `offset1`
 *             (upstream invert offset), `offset2` (downstream invert
 *             offset), and `maxDepth` (conduit max cross-section depth).
 *           - EGL is derived as `HGL + v² / (2·g)` because the engine
 *             exposes no first-class energy-grade output.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_BUILDER_H
#define PROFILE_BUILDER_H

#include <QDateTime>
#include <QPointF>
#include <QString>
#include <QVector>

#include <limits>

namespace ProfileBuilder
{

/*!
 * \brief Standard gravitational acceleration in the two unit systems SWMM
 *        supports.  Caller passes whichever applies to its `.out` file.
 */
inline constexpr double kGravityFps2 = 32.174;  /*!< ft / s²  — US flow units. */
inline constexpr double kGravityMps2 = 9.80665; /*!< m  / s²  — SI flow units. */

/*!
 * \enum NodeKind
 * \brief Topology category for the manhole-glyph picker in ProfilePlotWidget.
 *        Maps 1:1 to the relevant `SWMMModelLayer::Category` values; kept as
 *        a self-contained enum so this header has no model-layer dependency.
 */
enum class NodeKind {
    Junction = 0,
    Outfall,
    Storage,
    Divider,
    /*! Virtual junction — a computational break point inside a conduit, not a
     *  structure. It contributes a rim elevation to the ground line but gets
     *  no manhole shaft and no glyph: the pipe (and the soil above it) runs
     *  through it unbroken. */
    VirtualJunction,
};

/*!
 * \enum LinkKind
 * \brief Connector category for the conduit-vs-glyph render branch in
 *        ProfilePlotWidget.
 */
enum class LinkKind {
    Conduit = 0,
    Pump,
    Orifice,
    Weir,
    Outlet,
};

/*!
 * \enum OutputKind
 * \brief Which elevation-axis quantity a profile series represents.
 *        Every option resolves to an elevation (ft or m) so all series can
 *        share the y-axis.  Used by the multi-output/multi-model overlay:
 *        each series in the profile is (resultsLayer × OutputKind).
 *
 *        Mapping to ProfileBuilder::SourceDerived arrays:
 *          - HGL          → hglByPeriod              (animated, per period)
 *          - EGL          → eglByPeriod              (animated, per period)
 *          - WaterSurface → waterSurfaceByPeriod     (invert + nodeDepth)
 *          - MaxHGL       → maxHgl  (envelope, time-invariant)
 *          - MaxEGL       → maxEgl
 *          - MinHGL       → minHgl
 *          - MinEGL       → minEgl
 *          - MaxWaterSurface / MinWaterSurface → maxWaterSurface / minWaterSurface
 */
enum class OutputKind {
    HGL = 0,
    EGL,
    WaterSurface,
    MaxHGL,
    MaxEGL,
    MinHGL,
    MinEGL,
    MaxWaterSurface,
    MinWaterSurface,
};

/*! Sentinel for BranchLink::bearingRad when a link's plan geometry is
 *  degenerate and no honest bearing exists. Outside [-pi, pi], so a renderer
 *  that forgets to check draws nothing sensible rather than a plausible-but-
 *  wrong north spoke. */
inline constexpr double kNoBearing = 1.0e9;

/*!
 * \struct BranchLink
 * \brief One link incident to a path node, described from that node's point
 *        of view.
 *
 *        Every link touching the node is recorded, including the one or two
 *        the profile itself traverses (`onPath`). The renderer uses the set
 *        two ways: off-path entries become truncated stubs butting against
 *        the manhole tube, so a profile shows what else arrives at each
 *        structure; and the whole set — path links included — becomes the
 *        per-node plan rose, where the path spokes are what orient the
 *        reader.
 *
 *        `bearingRad` is a MAP bearing measured at this node: 0 = +y (north),
 *        increasing clockwise, in the layer CRS. It is the direction the link
 *        leaves this node, taken from the first polyline point away from the
 *        node, so a curved link reads by its local tangent rather than by the
 *        straight line to its far end.
 *
 *        `intoNode` is the MODEL's flow direction (this node is the link's
 *        To-node), not the profile traversal direction — a rose arrow should
 *        show how the network is built, which is independent of which way the
 *        user happened to draw the profile.
 */
struct BranchLink {
    QString name;
    LinkKind kind = LinkKind::Conduit;
    double invertElev = 0.0;   /*!< Absolute invert at THIS node — the node
                                       invert plus the link's offset at this
                                       end. Sets where the stub attaches. */
    double maxDepth = 0.0;     /*!< Cross-section max depth (crown offset)
                                       for the stub's thickness; 0 when the
                                       link has no conduit geometry. */
    bool openTop = false;      /*!< Draw the stub with no soffit line. */
    double bearingRad = 0.0;   /*!< Plan bearing, 0 = north, clockwise. */
    bool intoNode = false;     /*!< Model flow arrives at this node. */
    bool onPath = false;       /*!< The profile traverses this link, so it
                                       gets no stub (it is already drawn full
                                       length) but does get a rose spoke. */
    int lidLayer = 0;          /*!< Optional one-based authored LID outlet anchor. */
    bool lidAnchorTop = false; /*!< TOP versus BOTTOM of the authored layer. */
};

/*! One authored physical layer in a storage-node LID, ordered top to bottom.
 *  Elevations use the same m/ft units as the node. BOTTOM is a boundary
 *  condition and is not represented as a physical layer. */
struct NodeLidLayer {
    enum Kind { Surface, Media, Aggregate };
    Kind kind = Media;
    double topElev = 0.0;
    double bottomElev = 0.0;
};

/*!
 * \struct NodeStatic
 * \brief Time-invariant per-node properties needed by the profile plot.
 *        The "rim" elevation is always `invertElev + maxDepth` — terrain
 *        DEMs only override the *ground line* drawn behind the manhole
 *        glyph (see PathStatic::terrainSamples), never the rim itself.
 *
 *        For a virtual junction `maxDepth` carries the node's rendering rim
 *        depth (the optional `[VIRTUAL_JUNCTIONS]` MaxDepth) when it has one,
 *        falling back to the derived pipe crown when it does not — the
 *        adapter resolves that, so this struct stays a plain rim carrier.
 */
struct NodeStatic {
    QString name;
    double invertElev = 0.0;
    double maxDepth = 0.0;
    double surchargeDepth = 0.0; /*!< extra depth allowed above rim
                                            before flooding occurs */
    NodeKind kind = NodeKind::Junction;
    /*! Inlet junction — a VirtualJunction that also captures street flow to
     *  an off-profile underdrain node. Carried as a flag rather than a sixth
     *  NodeKind so no existing kind switch changes meaning: it is drawn with
     *  the virtual-junction break rectangle plus a small inlet glyph. */
    bool isInlet = false;
    /*! Every link incident to this node, path links included (see
     *  BranchLink). Empty when the caller built the path from pure-logic
     *  inputs without geometry — the renderer simply draws no stubs and no
     *  rose, exactly as before this field existed. */
    QVector<BranchLink> branches;
    QVector<NodeLidLayer> lidLayers;
};

/*!
 * \struct LinkStatic
 * \brief Time-invariant per-link properties.  Only links present *on the
 *        chosen path* are populated.  Indices in PathStatic::links are 0..N-1
 *        where N == PathStatic::nodes.size() - 1, and link[i] joins
 *        node[i] → node[i+1] in path-traversal order (which may differ from
 *        the underlying model link's intrinsic from/to direction when the
 *        router operated in undirected mode).
 */
struct LinkStatic {
    QString name;
    double length = 0.0;
    double offset1 = 0.0;     /*!< Invert offset at *path-upstream* end.
                                        Per SWMM convention, kept at 0 for
                                        weirs / outlets — use crestHeight
                                        for their sill elevation. */
    double offset2 = 0.0;     /*!< Invert offset at *path-downstream* end. */
    double maxDepth = 0.0;    /*!< Cross-section max depth (crown offset). */
    double crestHeight = 0.0; /*!< Weir / outlet crest height above the
                                        inlet (path-upstream) node invert.
                                        Read via swmm_link_get_crest_height.
                                        Zero for conduits / pumps / orifices. */
    bool openTop = false;     /*!< Draw with no soffit line (open channel,
                                        or a street). Presentation only — see
                                        linkFullDepth() in xsectsampler.h. */
    bool isStreet = false;    /*!< STREET cross-section: the invert is a
                                        road gutter line and is drawn with the
                                        pavement brush. Carried as a plain
                                        flag so the renderer stays free of
                                        engine shape codes. */
    LinkKind kind = LinkKind::Conduit;
    bool reversed = false; /*!< True when traversal direction is the
                                        reverse of the underlying model link;
                                        lets the velocity-sign convention map
                                        correctly in EGL computation. */
};

/*!
 * \struct PathStatic
 * \brief A fully-resolved profile path: ordered nodes, joining links, and
 *        cumulative chainage from `nodes[0]` to each subsequent node.
 *        `chainage.size() == nodes.size()` and `chainage[0] == 0.0`.
 *
 *        `terrainSamples` is an optional ground-line override sampled
 *        from a digital terrain model — empty when the user hasn't
 *        opted in (or when no terrain raster is active).  Each entry is
 *        `(chainage, elevation)` in path-local coordinates.  Renderers
 *        fall back to the per-node `invert + maxDepth` rim line when
 *        this is empty.  Note that the rim line still drives the
 *        manhole-shaft glyph regardless — terrain only changes how the
 *        ground / soil-fill bound is drawn.
 */
struct PathStatic {
    QVector<NodeStatic> nodes;
    QVector<LinkStatic> links;
    QVector<double> chainage;
    QVector<QPointF> terrainSamples;
};

/*!
 * \struct SourceSeries
 * \brief Pre-fetched per-source time-series.  One instance per
 *        `SWMMResultsLayer` that the user wants to overlay on the plot.
 *
 *        `nodeHead`, `nodeDepth`, and `linkVelocity` are addressed by path
 *        index (i.e. position along the path), not by engine index, so
 *        ProfileBuilder is agnostic to how the caller resolves names.
 *        Series with fewer periods than the longest source are accepted —
 *        envelope computation operates on each source's own length.
 */
struct SourceSeries {
    /*! Opaque stable identifier (e.g. SWMMResultsLayer pointer or path).
     *  Used only for diagnostics; carries no semantic meaning. */
    QString sourceId;
    QDateTime startTime;
    int reportStepSec = 0;
    int periodCount = 0;

    /*! [pathNodeIdx][period] hydraulic head (ft or m). */
    QVector<QVector<float>> nodeHead;
    /*! [pathNodeIdx][period] optional depth used for invert-plus-depth water surface. */
    QVector<QVector<float>> nodeDepth;
    /*! [pathLinkIdx][period] velocity magnitude (already abs-valued by
     *  caller or by `compute()` internally — sign is direction, EGL uses
     *  kinetic energy which is direction-agnostic). */
    QVector<QVector<float>> linkVelocity;
};

/*!
 * \struct SourceDerived
 * \brief Output of `compute()`: per-source HGL/EGL series and per-node
 *        envelope min/max.  Sized to match the input `SourceSeries`.
 *
 *        Layout: `hglByPeriod[period][pathNodeIdx]`, etc. — period-major so
 *        the renderer's per-frame access pattern (one period × all nodes)
 *        reads from a single contiguous inner vector instead of pointer-
 *        chasing across one inner vector per node.  Envelope arrays are
 *        indexed only by `pathNodeIdx`.
 */
struct SourceDerived {
    /*! [period][pathNodeIdx] — period-major; see struct docstring. */
    QVector<QVector<double>> hglByPeriod;
    /*! [period][pathNodeIdx] — period-major; see struct docstring. */
    QVector<QVector<double>> eglByPeriod;
    /*! [period][pathNodeIdx] free-surface elevation = `invertElev + nodeDepth`.
     *  Differs from HGL when the conduit is pressurized (HGL > rim). */
    QVector<QVector<double>> waterSurfaceByPeriod;
    QVector<double> minHgl;
    QVector<double> maxHgl;
    QVector<double> minEgl;
    QVector<double> maxEgl;
    QVector<double> minWaterSurface;
    QVector<double> maxWaterSurface;
};

/*!
 * \struct Diagnostic
 * \brief Returned from `validate()` to flag malformed inputs without
 *        triggering exceptions.  Empty `error` means "OK".
 */
struct Diagnostic {
    QString error;
};

// --------------------------------------------------------------------------
// Static-topology utilities
// --------------------------------------------------------------------------

/*!
 * \brief Computes cumulative chainage from the first node.
 *        `chainage[0] = 0.0`, `chainage[i] = chainage[i-1] + links[i-1].length`.
 *        Returns the populated array — does not mutate `path`.
 * @param[in] links Ordered traversal links with lengths in one common horizontal unit.
 * @return Owned vector of size links.size()+1 starting at zero, including a single zero for no links. Lengths are summed verbatim; negative/nonfinite values are not rejected.
 */
[[nodiscard]] QVector<double> computeChainage(const QVector<LinkStatic> &links);

/*!
 * \brief Returns the ground elevation at node `i` along the path.
 *        Equivalent to `nodes[i].invertElev + nodes[i].maxDepth`.
 * @param[in] n Static node with invert and rendering depth in a common vertical unit.
 * @return invertElev+maxDepth, without DEM lookup or finite/range validation.
 */
[[nodiscard]] double groundElev(const NodeStatic &n);

/*!
 * \brief Returns the conduit-invert elevation at the upstream end of link
 *        `i` along the path: `nodes[i].invertElev + links[i].offset1`.
 * @param[in] path Structurally valid path with links.size()+1 nodes and traversal-oriented offsets.
 * @param[in] linkIdx Path-link index.
 * @return Upstream node invert plus that link end's offset, in path vertical units; invalid link indices return zero. A valid path shape is a caller precondition.
 */
[[nodiscard]] double conduitInvertUpstream(const PathStatic &path, int linkIdx);

/*!
 * \brief Symmetric to \ref conduitInvertUpstream for the downstream end:
 *        `nodes[i+1].invertElev + links[i].offset2`.
 * @param[in] path Structurally valid path with links.size()+1 nodes and traversal-oriented offsets.
 * @param[in] linkIdx Path-link index.
 * @return Downstream node invert plus that link end's offset, in path vertical units; invalid link indices return zero. A valid path shape is a caller precondition.
 */
[[nodiscard]] double conduitInvertDownstream(const PathStatic &path, int linkIdx);

// --------------------------------------------------------------------------
// Per-period EGL helper (exposed for callers that want a point query rather
// than a full envelope compute, e.g. tooltip / hover read-outs).
// --------------------------------------------------------------------------

/*!
 * \brief Velocity-head at node `nodeIdx` for the given source at period `p`,
 *        using the path-direction convention: head node uses downstream
 *        velocity only, tail node uses upstream only, interior nodes
 *        average the two incident links.  Returns 0.0 if velocity data is
 *        missing or the period is out of range.  Uses `|v|`.
 * @param[in] path Ordered path whose node count establishes end/interior incidence.
 * @param[in] src Path-indexed link velocities in length/second, matching gravity's length unit.
 * @param[in] nodeIdx Path node index; an out-of-range node or path shorter than two gives zero.
 * @param[in] period Zero-based source period; missing/out-of-range link samples are treated as zero velocity.
 * @param[in] gravity Finite positive gravitational acceleration in length/second squared; nonpositive returns zero.
 * @return Square of the mean incident absolute velocity divided by 2*gravity (not the mean of squared heads), in length units. NaN/infinite velocity samples are not filtered and may propagate.
 */
[[nodiscard]] double velocityHead(const PathStatic &path, const SourceSeries &src, int nodeIdx,
                                  int period, double gravity);

/*!
 * \brief Convenience: EGL at node `nodeIdx`, period `p` =
 *        `HGL(nodeIdx, p) + velocityHead(nodeIdx, p, g)`.
 * @param[in] path Traversal topology for velocity incidence.
 * @param[in] src Path-indexed head and optional velocity rows, in a consistent length unit.
 * @param[in] nodeIdx Node row index; a missing head row/sample returns zero rather than NaN.
 * @param[in] period Zero-based period.
 * @param[in] gravity Acceleration in the same length/second-squared units as source velocities.
 * @return Raw nodeHead plus velocityHead(); nonfinite input head/velocities may propagate.
 */
[[nodiscard]] double eglAtPeriod(const PathStatic &path, const SourceSeries &src, int nodeIdx,
                                 int period, double gravity);

// --------------------------------------------------------------------------
// Validation + full compute
// --------------------------------------------------------------------------

/*!
 * \brief Sanity-checks a PathStatic + SourceSeries pair before compute().
 *        Catches: empty path (< 2 nodes), links-count mismatch (must be
 *        nodes-1), mismatched series array sizes vs path size, zero report
 *        step on a non-empty series, etc.
 * @param[in] path Requires at least two nodes, exactly nodes-1 links and either empty chainage or one entry per node.
 * @param[in] src Nonnegative periodCount, positive reportStepSec when nonempty, one head row per node, and either no velocity array or one row per link.
 * @return First shape/count diagnostic, empty error on success. Does not validate inner row lengths, nodeDepth shape, startTime, finite physical values, monotonic chainage, units or gravity.
 */
[[nodiscard]] Diagnostic validate(const PathStatic &path, const SourceSeries &src);

/*!
 * \brief Builds per-node HGL/EGL time-series and per-node envelope min/max
 *        for a single source.  Assumes the input has passed `validate()`.
 *        Returns an empty `SourceDerived` on validation failure.
 * @param[in] path Static topology and elevations in the source's length units.
 * @param[in] src Full path-indexed history; missing/short inner rows are allowed and leave per-period NaN slots. Optional nodeDepth is used independently for water surface.
 * @param[in] gravity Finite positive acceleration in matching length/second-squared units; nonpositive suppresses velocity head.
 * @return Owned period-major arrays and node envelopes, or a fully empty result if validate() fails. Unobserved extrema retain +infinity/-infinity sentinels; zero is never substituted. Samples are not finite-filtered before assignment, so infinities can enter extrema and NaN comparisons are ignored. Water surface is simply invert+depth without a rim clamp.
 */
[[nodiscard]] SourceDerived compute(const PathStatic &path, const SourceSeries &src,
                                    double gravity);

/*!
 * \brief Live results: extend a `SourceDerived` built over the first
 *        `derived.hglByPeriod.size()` periods of `src` with the periods
 *        `src` has gained since. `src` must be the full (grown) series.
 *        Appends one period-major row per new period and updates the
 *        running envelopes; the result is identical to `compute()` over the
 *        whole series (both use `accumulatePeriod`). Returns false and
 *        leaves `derived` untouched when `src` does not validate, has fewer
 *        periods than `derived`, or its checked dimensions do not match. Callers must ensure it belongs to this
 *        path and the unchanged historical prefix. A no-op success when nothing was appended.
 * @param[in] path Same valid static path used for the existing derived prefix.
 * @param[in] src Full grown source with unchanged historical samples, row ordering and unit system.
 * @param[in] gravity Same matching acceleration used to compute the prefix.
 * @param[in,out] derived Previous compute/append result with every period row and envelope consistently sized. Appends new period-major rows and updates extrema; does not recalculate old data.
 * @return False for validation failure, shrinking period count, mismatched top-level period arrays or a mismatched minHgl node count when extending a nonempty prefix. Those checks leave derived unchanged; equal period counts succeed without verifying all row/envelope dimensions. Path/history identity and the other envelope sizes are caller preconditions, not fully verified.
 */
bool appendPeriods(const PathStatic &path, const SourceSeries &src, double gravity,
                   SourceDerived &derived);

/*!
 * \brief The per-period kernel shared by `compute()` and `appendPeriods()`:
 *        fills period `p`'s rows of `out` (which must already be sized and
 *        NaN-filled for `p`) and folds the values into the envelopes.
 * @param[in] path Valid path matching source and output node counts.
 * @param[in] src Source with one nodeHead row per path node; individual histories may be short.
 * @param[in] p Nonnegative period with allocated output rows; not range-validated here.
 * @param[in] gravity Matching acceleration used by velocityHead().
 * @param[in,out] out Period rows must already have node-sized NaN storage and all node-sized envelopes initialized to extrema sentinels or prior extrema. Writes available head/EGL/depth-derived surface values and folds comparisons into envelopes, leaving absent samples unchanged.
 */
void accumulatePeriod(const PathStatic &path, const SourceSeries &src, int p, double gravity,
                      SourceDerived &out);

} // namespace ProfileBuilder

#endif // PROFILE_BUILDER_H
