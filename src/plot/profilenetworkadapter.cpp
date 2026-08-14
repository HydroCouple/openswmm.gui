/*!
 * \file   profilenetworkadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Pure-logic half of ProfileNetworkAdapter — graph build and path
 *         materialization from explicit POD inputs.  Live-model glue lives
 *         in profilenetworkadapter_model.cpp so unit tests can link this
 *         file without pulling in the SWMM engine library.
 */

#include "plot/profilenetworkadapter.h"

#include <algorithm>

namespace ProfileNetworkAdapter
{

// Minimum weight assigned to a conduit even when its length is reported as
// <= 0 — avoids zero-weight edges that break Dijkstra's positive-weight
// invariant.  Exposed at file scope so the live-model glue can share it.
constexpr double kMinConduitWeight = 0.01;

ProfileRouter::Graph buildGraph(int nodeCount,
                                const QVector<LinkInfo> &links,
                                double nonConduitWeight)
{
    ProfileRouter::Graph g;
    g.nodeCount = std::max(0, nodeCount);
    g.edges.reserve(links.size());
    for (const LinkInfo &li : links) {
        if (li.fromNode < 0 || li.fromNode >= g.nodeCount) continue;
        if (li.toNode   < 0 || li.toNode   >= g.nodeCount) continue;
        ProfileRouter::Edge e;
        e.fromNode = li.fromNode;
        e.toNode   = li.toNode;
        e.linkId   = li.engineLinkIdx;
        e.weight   = (li.kind == ProfileBuilder::LinkKind::Conduit)
                         ? std::max(li.length, kMinConduitWeight)
                         : nonConduitWeight;
        g.edges.push_back(e);
    }
    return g;
}

void swapForReversed(PathLinkInfo &link)
{
    if (!link.reversed) return;
    std::swap(link.offset1, link.offset2);
}

ProfileBuilder::NodeKind toNodeKind(int swmmNodeType, bool isVirtual)
{
    using K = ProfileBuilder::NodeKind;
    if (swmmNodeType == 0 && isVirtual) return K::VirtualJunction;
    switch (swmmNodeType) {
    case 0: return K::Junction;
    case 1: return K::Outfall;
    case 2: return K::Storage;
    case 3: return K::Divider;
    default: return K::Junction;
    }
}

double renderRimDepth(bool isVirtual, double maxDepth, double rimDepth)
{
    return (isVirtual && rimDepth > 0.0) ? rimDepth : maxDepth;
}

ProfileBuilder::PathStatic buildPathStatic(
    const ProfileRouter::Path &routerPath,
    const QVector<NodeInfo> &nodes,
    const QVector<PathLinkInfo> &links)
{
    ProfileBuilder::PathStatic out;
    if (routerPath.nodes.size() != nodes.size())          return out;
    if (routerPath.linkIds.size() != links.size())        return out;
    if (links.size() + 1     != nodes.size())             return out;

    out.nodes.reserve(nodes.size());
    out.links.reserve(links.size());

    for (const NodeInfo &n : nodes) {
        ProfileBuilder::NodeStatic ns;
        ns.name           = n.name;
        ns.invertElev     = n.invertElev;
        ns.maxDepth       = n.maxDepth;
        ns.surchargeDepth = n.surchargeDepth;
        ns.kind           = n.kind;
        out.nodes.push_back(ns);
    }
    for (const PathLinkInfo &l : links) {
        ProfileBuilder::LinkStatic ls;
        ls.name        = l.name;
        ls.length      = l.length;
        ls.offset1     = l.offset1;
        ls.offset2     = l.offset2;
        ls.maxDepth    = l.maxDepth;
        ls.crestHeight = l.crestHeight;
        ls.kind        = l.kind;
        ls.reversed    = l.reversed;
        out.links.push_back(ls);
    }
    out.chainage = ProfileBuilder::computeChainage(out.links);
    return out;
}

} // namespace ProfileNetworkAdapter
