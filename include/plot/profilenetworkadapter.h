/**
 * @file      profilenetworkadapter.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Bridge between SWMMModelLayer (live engine state) and the
 *         pure-logic ProfileRouter / ProfileBuilder modules used by
 *         Slice BC's profile plot.
 *
 *         The adapter is split into two layers:
 *
 *           1.  Pure-logic builders — `buildGraph()` / `buildPathStatic()`
 *               operate on plain POD inputs (LinkInfo / NodeInfo / etc.)
 *               and are fully unit-testable headless.
 *
 *           2.  Live-model glue — `*FromModel()` overloads pull data from
 *               the SWMMModelLayer + raw engine handle and forward to the
 *               pure-logic builders.  They are exercised by integration
 *               tests, not by the GoogleTest unit fixture.
 *
 *         This split lets the routing math be verified in isolation while
 *         the engine-binding layer evolves with the rest of BC's stack.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_NETWORK_ADAPTER_H
#define PROFILE_NETWORK_ADAPTER_H

#include "plot/profilebuilder.h"
#include "plot/profilerouter.h"

#include <QPointF>
#include <QString>
#include <QVector>

class SWMMModelLayer;

namespace ProfileNetworkAdapter
{

// ---------------------------------------------------------------------------
// Pure-logic input records — fed directly to the builders below.
// ---------------------------------------------------------------------------

/*!
 * \struct LinkInfo
 * \brief Just enough about each link to weight it in the routing graph.
 *        `engineLinkIdx` is round-tripped through the Edge::linkId field
 *        so callers can later resolve which model link a path traverses.
 */
struct LinkInfo {
    int engineLinkIdx = -1;
    int fromNode = -1;
    int toNode = -1;
    double length = 0.0;
    ProfileBuilder::LinkKind kind = ProfileBuilder::LinkKind::Conduit;
};

/*!
 * \struct NodeInfo
 * \brief Per-node static data needed to materialize a PathStatic.
 */
struct NodeInfo {
    int engineNodeIdx = -1;
    QString name;
    double invertElev = 0.0;
    double maxDepth = 0.0;
    double surchargeDepth = 0.0;
    ProfileBuilder::NodeKind kind = ProfileBuilder::NodeKind::Junction;
    /*! Inlet junction — see ProfileBuilder::NodeStatic::isInlet. */
    bool isInlet = false;
    QVector<ProfileBuilder::NodeLidLayer> lidLayers;
};

/*!
 * \struct PathLinkInfo
 * \brief Per-link static data needed to materialize a PathStatic.
 *        `maxDepth` is the conduit cross-section primary dimension
 *        (e.g. circular pipe diameter) — used for the conduit-crown line
 *        in the renderer; 0 for non-conduits.
 */
struct PathLinkInfo {
    int engineLinkIdx = -1;
    QString name;
    double length = 0.0;
    double offset1 = 0.0;
    double offset2 = 0.0;
    double maxDepth = 0.0;
    double crestHeight = 0.0; /*!< Weir / outlet sill
                                                          (relative to the
                                                          inlet invert).  0
                                                          for other links. */
    bool openTop = false;     /*!< Draw with no soffit. */
    bool isStreet = false;    /*!< STREET cross-section. */
    ProfileBuilder::LinkKind kind = ProfileBuilder::LinkKind::Conduit;
    bool reversed = false;
};

// ---------------------------------------------------------------------------
// Pure-logic builders (unit-tested in tests/unit/test_profilenetworkadapter.cpp).
// ---------------------------------------------------------------------------

/*!
 * \brief Builds a directed routing graph from the given link list.
 *        Conduits receive `weight = max(length, kMinConduitWeight)`; all
 *        other link kinds receive `nonConduitWeight` (a small fixed value
 *        so pumps/weirs/orifices are traversable without dominating
 *        conduit-length routing).  See Slice BC's Phase 8.7.1 notes.
 *
 * \param nodeCount         Explicit total node count; the builder does not infer it.
 * \param links             Each contributes one Edge.  Invalid endpoint
 *                          indices are silently skipped.
 * \param nonConduitWeight  Fixed weight assigned to non-conduit links.
 * @return Owned directed graph with nodeCount clamped to zero minimum. Conduit weights are at least 0.01; other weights are used verbatim and must be finite/nonnegative for routing. Node count is never inferred from links.
 */
[[nodiscard]] ProfileRouter::Graph buildGraph(int nodeCount, const QVector<LinkInfo> &links,
                                              double nonConduitWeight = 1.0);

/*!
 * \brief Materializes a `ProfileBuilder::PathStatic` from a router-emitted
 *        path plus pre-resolved per-node / per-link static data.
 *        \p nodes must have length `routerPath.nodes.size()`;
 *        \p links must have length `routerPath.linkIds.size()`.
 *
 *        Each entry in \p links should already carry the correct `offset1`
 *        / `offset2` / `maxDepth` / `kind` / `reversed` orientation for
 *        the *path-traversal direction* (which may differ from the model
 *        link's intrinsic from→to direction when the router used
 *        `Options::undirected`).  The caller is responsible for that
 *        orientation; see `swapForReversed()` for a helper.
 * @param[in] routerPath Traversal identity/counts; this builder checks lengths but does not match individual indices to the supplied metadata.
 * @param[in] nodes Already ordered static node data, copied verbatim.
 * @param[in] links Already ordered/oriented static link data, copied without swapping here.
 * @return Owned static path with recomputed cumulative chainage, or empty for count mismatch. A single-node/no-link path is accepted here though ProfileBuilder::validate() rejects it. No live engine access occurs.
 */
[[nodiscard]] ProfileBuilder::PathStatic buildPathStatic(const ProfileRouter::Path &routerPath,
                                                         const QVector<NodeInfo> &nodes,
                                                         const QVector<PathLinkInfo> &links);

/*!
 * \brief Swaps the `offset1` / `offset2` fields of \p link in-place when
 *        the path traverses the underlying model link backward.  Reduces
 *        boilerplate in callers building PathLinkInfo lists.
 * @param[in,out] link Swaps offset1/offset2 only if reversed is true; other fields, including reversed, are retained. Calling twice reverses the swap, so apply once when orienting a record.
 */
void swapForReversed(PathLinkInfo &link);

/*!
 * \brief Maps an engine node type code (0=junction, 1=outfall, 2=storage,
 *        3=divider) plus the virtual-junction flag to a `NodeKind`.
 *
 *        Virtual junctions are JUNCTION-typed on the engine — the flag is
 *        the only thing separating them — and the profile draws them as
 *        pipe rather than as a structure.
 * @param[in] swmmNodeType Engine code 0..3; unknown codes fall back to Junction.
 * @param[in] isVirtual Selects VirtualJunction only when the type code is Junction.
 * @return Profile glyph/topology kind, without engine access.
 */
[[nodiscard]] ProfileBuilder::NodeKind toNodeKind(int swmmNodeType, bool isVirtual);

/*!
 * \brief Picks the depth the profile should draw a node's ground/rim line
 *        at, given its hydraulic max depth and its rendering rim depth.
 *
 *        A virtual junction's max depth is derived — always the shared pipe
 *        crown — so drawing the ground at `invert + maxDepth` sinks the
 *        terrain into the pipe at every break point.  When the model supplies
 *        a rim depth (the optional `[VIRTUAL_JUNCTIONS]` MaxDepth, 0 = unset)
 *        that value wins.  Every other node type keeps its max depth.
 * @param[in] isVirtual Whether the node is a computational virtual junction.
 * @param[in] maxDepth Fallback hydraulic depth in node vertical units.
 * @param[in] rimDepth Authored rendering depth in the same units; used only when strictly positive and virtual.
 * @return Selected depth, without further finite/range normalization.
 */
[[nodiscard]] double renderRimDepth(bool isVirtual, double maxDepth, double rimDepth);

// ---------------------------------------------------------------------------
// Live-model glue (not unit-tested; manual / integration only).
// ---------------------------------------------------------------------------

/*!
 * \brief Convenience overload — pulls links from a live SWMMModelLayer
 *        and forwards to `buildGraph()`.
 * @param[in] model Optional borrowed layer/engine; caller keeps it stable during all getters.
 * @param[in] nonConduitWeight Finite nonnegative routing cost for pumps/weirs/orifices/outlets.
 * @return Owned graph in engine node/link indices, empty for null model/engine. Invalid endpoint sentinels are skipped; getter failures otherwise retain default type/length values. No engine mutation occurs.
 */
[[nodiscard]] ProfileRouter::Graph buildGraphFromModel(SWMMModelLayer *model,
                                                       double nonConduitWeight = 1.0);

/*!
 * \brief Convenience overload — materializes a PathStatic by querying the
 *        engine for each node's invert/maxDepth and each link's offsets/
 *        cross-section.  Path orientation is derived from
 *        `routerPath.nodes`.
 * @param[in] model Optional borrowed model/engine with stable metadata and geometry. Application UnitSystem must match the engine for cross-section sampling.
 * @param[in] routerPath Valid current engine indices with exactly nodes.size()-1 links and endpoints in matching traversal order; not fully validated before indexed reads.
 * @return Owned path with traversal-oriented offsets, model-inlet-anchored weir/outlet crests, virtual rims, physical storage-LID layers and incident branches. Null model/engine or empty node sequence returns empty; many getter failures retain default fields without diagnostics. Elevations/offsets follow project m/ft; LID mm/in thickness is converted accordingly.
 */
[[nodiscard]] ProfileBuilder::PathStatic
buildPathStaticFromModel(SWMMModelLayer *model, const ProfileRouter::Path &routerPath);

/*!
 * \brief Map bearing of the direction \p at -> \p away, as the profile's
 *        plan rose measures it: 0 = +y (north), increasing CLOCKWISE, in
 *        radians on (-pi, pi].
 *
 *        Note the argument order into atan2 is (dx, dy), not the usual
 *        (dy, dx) — that is what rotates the zero from +x to +y and flips
 *        the sense to clockwise. Returns ProfileBuilder::kNoBearing when the
 *        two points coincide, so a caller never publishes a north spoke it
 *        did not measure.
 * @param[in] at Finite map-coordinate node endpoint.
 * @param[in] away Finite next polyline point in the same CRS.
 * @return atan2(dx,dy) in radians, or kNoBearing when points exactly coincide. No reprojection or scene-Y inversion occurs.
 */
[[nodiscard]] double bearingFromPoints(const QPointF &at, const QPointF &away);

/*!
 * \brief Fills `PathStatic::nodes[i].branches` with every link incident to
 *        each path node, path links included (marked `onPath`).
 *
 *        Separated from buildPathStaticFromModel() so a caller that only
 *        wants the hydraulic profile can skip the geometry work, and so the
 *        incidence pass can be exercised on its own. One pass over the
 *        model's links; nothing is written when \p model has no engine.
 * @param[in] model Optional borrowed stable model/engine and cached map polylines.
 * @param[in] routerPath Engine node/link indices corresponding to the existing path.
 * @param[in,out] path Existing ordered nodes; branches are appended, not cleared. Repeated calls duplicate entries. Only the common prefix of path/router nodes participates.
 * @note One engine-link pass adds each incident end, including both ends on the path; marks onPath and intrinsic intoNode, resolves local tangent bearings and optional LID anchors. Unavailable getters retain defaults; degenerate polylines use kNoBearing.
 */
void collectNodeBranches(SWMMModelLayer *model, const ProfileRouter::Path &routerPath,
                         ProfileBuilder::PathStatic &path);

/*!
 * \brief Resolves an engine node index by name; returns -1 if not found.
 *        Wrapper around `swmm_node_index` for the rare callers that don't
 *        want to pull `<openswmm_nodes.h>` themselves.
 * @param[in] model Optional borrowed model/engine.
 * @param[in] name Identifier encoded as UTF-8 and passed to the engine's node lookup without normalization.
 * @return Current engine node index, or -1 when missing/unbound. No lifetime-stable index guarantee is implied across model edits.
 */
[[nodiscard]] int findNodeIndex(SWMMModelLayer *model, const QString &name);

/*!
 * \brief Resolves an engine link index by name; returns -1 if not found.
 * @param[in] model Optional borrowed model/engine.
 * @param[in] name Identifier encoded as UTF-8 and passed to the engine's link lookup without normalization.
 * @return Current engine link index, or -1 when missing/unbound. No lifetime-stable index guarantee is implied across model edits.
 */
[[nodiscard]] int findLinkIndex(SWMMModelLayer *model, const QString &name);

} // namespace ProfileNetworkAdapter

#endif // PROFILE_NETWORK_ADAPTER_H
