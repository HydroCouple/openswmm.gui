/*!
 * \file   profilenetworkadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Bridge between SWMMModelLayer (live engine state) and the
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
 */

#ifndef PROFILE_NETWORK_ADAPTER_H
#define PROFILE_NETWORK_ADAPTER_H

#include "plot/profilebuilder.h"
#include "plot/profilerouter.h"

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
struct LinkInfo
{
    int                       engineLinkIdx = -1;
    int                       fromNode      = -1;
    int                       toNode        = -1;
    double                    length        = 0.0;
    ProfileBuilder::LinkKind  kind          = ProfileBuilder::LinkKind::Conduit;
};

/*!
 * \struct NodeInfo
 * \brief Per-node static data needed to materialize a PathStatic.
 */
struct NodeInfo
{
    int                       engineNodeIdx  = -1;
    QString                   name;
    double                    invertElev     = 0.0;
    double                    maxDepth       = 0.0;
    double                    surchargeDepth = 0.0;
    ProfileBuilder::NodeKind  kind           = ProfileBuilder::NodeKind::Junction;
};

/*!
 * \struct PathLinkInfo
 * \brief Per-link static data needed to materialize a PathStatic.
 *        `maxDepth` is the conduit cross-section primary dimension
 *        (e.g. circular pipe diameter) — used for the conduit-crown line
 *        in the renderer; 0 for non-conduits.
 */
struct PathLinkInfo
{
    int                       engineLinkIdx = -1;
    QString                   name;
    double                    length        = 0.0;
    double                    offset1       = 0.0;
    double                    offset2       = 0.0;
    double                    maxDepth      = 0.0;
    double                    crestHeight   = 0.0;  /*!< Weir / outlet sill
                                                          (relative to the
                                                          inlet invert).  0
                                                          for other links. */
    ProfileBuilder::LinkKind  kind          = ProfileBuilder::LinkKind::Conduit;
    bool                      reversed      = false;
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
 * \param nodeCount         Total node count (= max(fromNode, toNode) + 1
 *                          if you let `buildGraph` infer; pass an explicit
 *                          count to be defensive).
 * \param links             Each contributes one Edge.  Invalid endpoint
 *                          indices are silently skipped.
 * \param nonConduitWeight  Fixed weight assigned to non-conduit links.
 */
[[nodiscard]] ProfileRouter::Graph buildGraph(int nodeCount,
                                              const QVector<LinkInfo> &links,
                                              double nonConduitWeight = 1.0);

/*!
 * \brief Materializes a `ProfileBuilder::PathStatic` from a router-emitted
 *        path plus pre-resolved per-node / per-link static data.
 *        \p nodes must have length `routerPath.nodes.size()`;
 *        \p links must have length `routerPath.edges.size()`.
 *
 *        Each entry in \p links should already carry the correct `offset1`
 *        / `offset2` / `maxDepth` / `kind` / `reversed` orientation for
 *        the *path-traversal direction* (which may differ from the model
 *        link's intrinsic from→to direction when the router used
 *        `Options::undirected`).  The caller is responsible for that
 *        orientation; see `swapForReversed()` for a helper.
 */
[[nodiscard]] ProfileBuilder::PathStatic buildPathStatic(
    const ProfileRouter::Path &routerPath,
    const QVector<NodeInfo> &nodes,
    const QVector<PathLinkInfo> &links);

/*!
 * \brief Swaps the `offset1` / `offset2` fields of \p link in-place when
 *        the path traverses the underlying model link backward.  Reduces
 *        boilerplate in callers building PathLinkInfo lists.
 */
void swapForReversed(PathLinkInfo &link);

// ---------------------------------------------------------------------------
// Live-model glue (not unit-tested; manual / integration only).
// ---------------------------------------------------------------------------

/*!
 * \brief Convenience overload — pulls links from a live SWMMModelLayer
 *        and forwards to `buildGraph()`.
 */
[[nodiscard]] ProfileRouter::Graph buildGraphFromModel(
    SWMMModelLayer *model,
    double nonConduitWeight = 1.0);

/*!
 * \brief Convenience overload — materializes a PathStatic by querying the
 *        engine for each node's invert/maxDepth and each link's offsets/
 *        cross-section.  Path orientation is derived from
 *        `routerPath.nodes`.
 */
[[nodiscard]] ProfileBuilder::PathStatic buildPathStaticFromModel(
    SWMMModelLayer *model,
    const ProfileRouter::Path &routerPath);

/*!
 * \brief Resolves an engine node index by name; returns -1 if not found.
 *        Wrapper around `swmm_node_index` for the rare callers that don't
 *        want to pull `<openswmm_nodes.h>` themselves.
 */
[[nodiscard]] int findNodeIndex(SWMMModelLayer *model, const QString &name);

/*!
 * \brief Resolves an engine link index by name; returns -1 if not found.
 */
[[nodiscard]] int findLinkIndex(SWMMModelLayer *model, const QString &name);

} // namespace ProfileNetworkAdapter

#endif // PROFILE_NETWORK_ADAPTER_H
