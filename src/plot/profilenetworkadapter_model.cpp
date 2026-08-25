/*!
 * \file   profilenetworkadapter_model.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Live-model glue for ProfileNetworkAdapter — pulls per-node and
 *         per-link static data from a SWMMModelLayer's engine handle and
 *         forwards to the pure-logic builders in profilenetworkadapter.cpp.
 *
 *         This translation unit links the SWMM engine; the pure-logic
 *         translation unit does not.  Unit tests that need only the
 *         pure-logic surface link `profilenetworkadapter.cpp` alone.
 */

#include "plot/profilenetworkadapter.h"

#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <cmath>

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>

namespace ProfileNetworkAdapter
{

namespace
{

ProfileBuilder::LinkKind toLinkKind(int swmmLinkType)
{
    using K = ProfileBuilder::LinkKind;
    switch (swmmLinkType) {
    case 0: return K::Conduit;
    case 1: return K::Pump;
    case 2: return K::Orifice;
    case 3: return K::Weir;
    case 4: return K::Outlet;
    default: return K::Conduit;
    }
}

// toNodeKind() / renderRimDepth() live in the pure half
// (profilenetworkadapter.cpp) so they can be unit-tested without the engine.

} // namespace

ProfileRouter::Graph buildGraphFromModel(SWMMModelLayer *model, double nonConduitWeight)
{
    ProfileRouter::Graph empty;
    if (!model) return empty;
    SWMM_Engine eng = model->engine();
    if (!eng) return empty;

    int linkCount = swmm_link_count(eng);
    int nodeCount = swmm_node_count(eng);
    QVector<LinkInfo> links;
    links.reserve(linkCount);
    for (int i = 0; i < linkCount; ++i) {
        int from = -1, to = -1, type = 0;
        // We previously skipped on a non-zero return from these getters,
        // but that silently dropped pumps / weirs / orifices / outlets
        // on some engine builds (return-value semantics vary across link
        // types).  The endpoints are always populated when valid, so
        // gate only on the index sentinels.
        swmm_link_get_from_node(eng, i, &from);
        swmm_link_get_to_node  (eng, i, &to);
        swmm_link_get_type     (eng, i, &type);
        if (from < 0 || from >= nodeCount) continue;
        if (to   < 0 || to   >= nodeCount) continue;
        double len = 0.0;
        swmm_link_get_length(eng, i, &len);   // 0 for non-conduits is fine
        LinkInfo li;
        li.engineLinkIdx = i;
        li.fromNode      = from;
        li.toNode        = to;
        li.length        = len;
        li.kind          = toLinkKind(type);
        links.push_back(li);
    }
    return buildGraph(nodeCount, links, nonConduitWeight);
}

namespace
{

/*!
 * \brief Plan bearing of link \p engLinkIdx as it leaves the end selected by
 *        \p nodeIsFrom.
 * \details cachedLinkPolyline() carries the node endpoints, so the tangent is
 *          taken from the node's own end toward the next point along — a
 *          curved link then reads by the direction it actually leaves the
 *          manhole rather than by the straight line to its far end. The
 *          convention itself lives in bearingFromPoints() (pure half, unit
 *          tested); this only picks the two points.
 */
double linkBearingAtNode(SWMMModelLayer *model, int engLinkIdx, bool nodeIsFrom)
{
    const QVector<QPointF> poly = model->cachedLinkPolyline(engLinkIdx);
    if (poly.size() < 2) return ProfileBuilder::kNoBearing;
    const QPointF at   = nodeIsFrom ? poly.first() : poly.last();
    const QPointF away = nodeIsFrom ? poly.at(1)   : poly.at(poly.size() - 2);
    return bearingFromPoints(at, away);
}

} // namespace

void collectNodeBranches(SWMMModelLayer *model,
                         const ProfileRouter::Path &routerPath,
                         ProfileBuilder::PathStatic &path)
{
    if (!model || path.nodes.isEmpty()) return;
    SWMM_Engine eng = model->engine();
    if (!eng) return;

    // ONE pass over the links, not one per path node: a long profile through
    // an all-pipes model would otherwise make O(path nodes x links) engine
    // calls just to discover incidence.
    QHash<int, int> pathIndexOfNode;               // engine node idx -> path idx
    pathIndexOfNode.reserve(routerPath.nodes.size());
    for (int i = 0; i < path.nodes.size() && i < routerPath.nodes.size(); ++i)
        pathIndexOfNode.insert(routerPath.nodes[i], i);

    QSet<int> pathLinks;                           // links the profile traverses
    pathLinks.reserve(routerPath.linkIds.size());
    for (int id : routerPath.linkIds) pathLinks.insert(id);

    const int nLinks = swmm_link_count(eng);
    for (int l = 0; l < nLinks; ++l) {
        int from = -1, to = -1;
        swmm_link_get_from_node(eng, l, &from);
        swmm_link_get_to_node  (eng, l, &to);

        // A link can touch the path at both ends (the profile's own links,
        // and any branch that loops back), so both ends are considered.
        for (int end = 0; end < 2; ++end) {
            const bool isFrom     = (end == 0);
            const int  engNodeIdx = isFrom ? from : to;
            const auto it = pathIndexOfNode.constFind(engNodeIdx);
            if (it == pathIndexOfNode.constEnd()) continue;
            const int pathIdx = it.value();

            ProfileBuilder::BranchLink b;
            b.name     = QString::fromUtf8(swmm_link_id(eng, l));
            b.intoNode = !isFrom;                  // this node is the To-node
            b.onPath   = pathLinks.contains(l);

            int linkType = 0;
            swmm_link_get_type(eng, l, &linkType);
            b.kind = toLinkKind(linkType);

            // The offset at THIS end. The engine reports offsets against its
            // own from/to ends, independent of how the profile traverses.
            double offUp = 0.0, offDn = 0.0;
            swmm_link_get_offset_up(eng, l, &offUp);
            swmm_link_get_offset_dn(eng, l, &offDn);
            b.invertElev = path.nodes[pathIdx].invertElev
                           + (isFrom ? offUp : offDn);

            int xsShape = 0;
            double g1 = 0.0, g2 = 0.0, g3 = 0.0, g4 = 0.0;
            if (swmm_link_get_xsect(eng, l, &xsShape, &g1, &g2, &g3, &g4) == 0)
                b.maxDepth = g1;

            // Unusable geometry keeps the stub (which needs only the invert)
            // but loses the rose spoke — better a missing spoke than one
            // pointing north by accident.
            b.bearingRad = linkBearingAtNode(model, l, isFrom);

            path.nodes[pathIdx].branches.push_back(b);
        }
    }
}

ProfileBuilder::PathStatic buildPathStaticFromModel(
    SWMMModelLayer *model,
    const ProfileRouter::Path &routerPath)
{
    ProfileBuilder::PathStatic empty;
    if (!model) return empty;
    SWMM_Engine eng = model->engine();
    if (!eng) return empty;
    if (routerPath.nodes.isEmpty()) return empty;

    QVector<NodeInfo> nodes;
    nodes.reserve(routerPath.nodes.size());
    for (int engNodeIdx : routerPath.nodes) {
        NodeInfo n;
        n.engineNodeIdx = engNodeIdx;
        n.name          = QString::fromUtf8(swmm_node_id(eng, engNodeIdx));
        double invert   = 0.0, maxDepth = 0.0, surchargeDepth = 0.0, rimDepth = 0.0;
        int    nodeType = 0, isVirtual = 0;
        swmm_node_get_invert_elev    (eng, engNodeIdx, &invert);
        swmm_node_get_max_depth      (eng, engNodeIdx, &maxDepth);
        swmm_node_get_surcharge_depth(eng, engNodeIdx, &surchargeDepth);
        swmm_node_get_type           (eng, engNodeIdx, &nodeType);
        swmm_node_is_virtual         (eng, engNodeIdx, &isVirtual);
        swmm_node_get_rim_depth      (eng, engNodeIdx, &rimDepth);
        n.invertElev     = invert;
        n.maxDepth       = renderRimDepth(isVirtual != 0, maxDepth, rimDepth);
        n.surchargeDepth = std::max(0.0, surchargeDepth);
        n.kind           = toNodeKind(nodeType, isVirtual != 0);
        nodes.push_back(n);
    }

    QVector<PathLinkInfo> links;
    links.reserve(routerPath.linkIds.size());
    for (int i = 0; i < routerPath.linkIds.size(); ++i) {
        const int engLinkIdx = routerPath.linkIds[i];
        PathLinkInfo l;
        l.engineLinkIdx = engLinkIdx;
        l.name          = QString::fromUtf8(swmm_link_id(eng, engLinkIdx));
        double len = 0.0, rawOffUp = 0.0, rawOffDn = 0.0;
        int    linkType = 0;
        swmm_link_get_length(eng, engLinkIdx, &len);
        // Engine contract: swmm_link_get_offset_up/dn always returns the
        // offset as **depth above the connecting node invert**, regardless
        // of the source .inp's LINK_OFFSETS setting. PostParseResolver
        // normalises ELEVATION-mode input to DEPTH at parse time for both
        // conduits and orifices (see openswmm.engine
        // src/engine/input/PostParseResolver.cpp). Do not re-convert here.
        swmm_link_get_offset_up(eng, engLinkIdx, &rawOffUp);
        swmm_link_get_offset_dn(eng, engLinkIdx, &rawOffDn);
        swmm_link_get_type(eng, engLinkIdx, &linkType);
        l.length = len;
        l.kind   = toLinkKind(linkType);
        int xsShape = 0;
        double g1 = 0.0, g2 = 0.0, g3 = 0.0, g4 = 0.0;
        if (swmm_link_get_xsect(eng, engLinkIdx, &xsShape, &g1, &g2, &g3, &g4) == 0)
            l.maxDepth = g1;

        // Identify path-traversal orientation against the model link's
        // intrinsic from→to direction.
        int modelFrom = -1, modelTo = -1;
        swmm_link_get_from_node(eng, engLinkIdx, &modelFrom);
        swmm_link_get_to_node  (eng, engLinkIdx, &modelTo);
        const bool reversed = (modelFrom == routerPath.nodes[i + 1]
                                  && modelTo == routerPath.nodes[i]);
        l.reversed = reversed;

        // Assign in path-traversal order: offset1 sits next to nodes[i],
        // offset2 next to nodes[i+1].  When reversed, model-from is on the
        // downstream end of our path, so swap.
        l.offset1 = reversed ? rawOffDn : rawOffUp;
        l.offset2 = reversed ? rawOffUp : rawOffDn;

        // Weirs / outlets carry their sill in a separate `crest_height`
        // field rather than offset1/2 (those are zeroed for weirs by
        // PostParseResolver).  Pull it here so the profile renderer can
        // place the weir block at the correct elevation above the inlet
        // node's invert.  Note: crest_height is anchored to the *engine
        // inlet* (the model-from node).  When the path traverses the link
        // in reverse, the engine's inlet is on our *downstream* end — so
        // the path-upstream invert is no longer the right anchor.  We
        // store the value as-is and rely on the path-traversal-aware
        // renderer to honour the `reversed` flag.
        if (l.kind == ProfileBuilder::LinkKind::Weir
            || l.kind == ProfileBuilder::LinkKind::Outlet) {
            double crest = 0.0;
            swmm_link_get_crest_height(eng, engLinkIdx, &crest);
            l.crestHeight = crest;
        }

        links.push_back(l);
    }

    ProfileBuilder::PathStatic path = buildPathStatic(routerPath, nodes, links);
    collectNodeBranches(model, routerPath, path);
    return path;
}

int findNodeIndex(SWMMModelLayer *model, const QString &name)
{
    if (!model) return -1;
    SWMM_Engine eng = model->engine();
    if (!eng) return -1;
    const QByteArray u = name.toUtf8();
    return swmm_node_index(eng, u.constData());
}

int findLinkIndex(SWMMModelLayer *model, const QString &name)
{
    if (!model) return -1;
    SWMM_Engine eng = model->engine();
    if (!eng) return -1;
    const QByteArray u = name.toUtf8();
    return swmm_link_index(eng, u.constData());
}

} // namespace ProfileNetworkAdapter
