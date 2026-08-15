/*!
 * \file   selectionops.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "selection/selectionops.h"

#include "layers/swmmmodellayer.h"
#include "plot/profilenetworkadapter.h"
#include "plot/profilerouter.h"

#include <QHash>
#include <QList>
#include <QVector>

#include <utility>   // std::as_const

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

namespace {

using Category = SWMMModelLayer::Category;

//! Coarse selection kind for a fine category. The four node categories all
//! collapse to `Node` and the five link categories to `Link` — the ObjectType
//! is only the *identity* namespace, the Category is what invert scopes on.
SWMMObjectRef::ObjectType objectTypeFor(Category c)
{
    switch (c) {
    case SWMMModelLayer::CatJunctions:
    case SWMMModelLayer::CatOutfalls:
    case SWMMModelLayer::CatStorage:
    case SWMMModelLayer::CatDividers:
        return SWMMObjectRef::Node;
    case SWMMModelLayer::CatConduits:
    case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:
    case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return SWMMObjectRef::Link;
    case SWMMModelLayer::CatSubcatchments:
        return SWMMObjectRef::Subcatchment;
    case SWMMModelLayer::CatRainGages:
        return SWMMObjectRef::RainGage;
    default:
        return SWMMObjectRef::Unknown;
    }
}

//! Subcatchment drainage topology, read once per trace.
struct SubcatchTopology
{
    int                      count = 0;
    QVector<int>             outNode;              ///< subcatch idx → outlet node idx (-1 if none)
    QVector<int>             outSub;               ///< subcatch idx → outlet subcatch idx (-1 if none)
    QHash<int, QVector<int>> subsDrainingToNode;   ///< node idx     → subcatchments draining to it
    QHash<int, QVector<int>> subsDrainingToSub;    ///< subcatch idx → subcatchments draining to it
};

SubcatchTopology buildSubcatchTopology(SWMM_Engine e)
{
    SubcatchTopology t;
    t.count = swmm_subcatch_count(e);
    if (t.count <= 0) { t.count = 0; return t; }

    t.outNode.fill(-1, t.count);
    t.outSub.fill(-1, t.count);

    for (int i = 0; i < t.count; ++i) {
        int nodeIdx = -1;
        if (swmm_subcatch_get_outlet(e, i, &nodeIdx) == 0 && nodeIdx >= 0) {
            t.outNode[i] = nodeIdx;
            t.subsDrainingToNode[nodeIdx].push_back(i);
            continue;   // a subcatchment drains to a node OR a subcatchment, never both
        }
        int scIdx = -1;
        if (swmm_subcatch_get_outlet_subcatch(e, i, &scIdx) == 0
                && scIdx >= 0 && scIdx < t.count && scIdx != i) {
            t.outSub[i] = scIdx;
            t.subsDrainingToSub[scIdx].push_back(i);
        }
    }
    return t;
}

} // namespace

namespace SelectionOps {

QSet<SWMMObjectRef> invert(SWMMModelLayer *model,
                           const QSet<SWMMObjectRef> &current)
{
    QSet<SWMMObjectRef> next;
    if (!model || !model->engine()) return next;

    // 1. Universe per category + reverse map. Keyed on the *ref* (type+name),
    //    so the legal gage/subcatchment and node/link name collisions SWMM
    //    permits can't cross-contaminate the scope test.
    QVector<QSet<SWMMObjectRef>>     byCategory(SWMMModelLayer::NumCategories);
    QHash<SWMMObjectRef, Category>   catOf;

    for (int ci = 0; ci < SWMMModelLayer::NumCategories; ++ci) {
        const auto c = static_cast<Category>(ci);
        const SWMMObjectRef::ObjectType t = objectTypeFor(c);
        if (t == SWMMObjectRef::Unknown) continue;
        const int n = model->categoryCount(c);
        for (int row = 0; row < n; ++row) {
            const QString name = model->objectNameAt(c, row);
            if (name.isEmpty()) continue;
            const SWMMObjectRef ref(t, name);
            byCategory[ci].insert(ref);
            catOf.insert(ref, c);
        }
    }

    // 2. Split the current selection: spatial network refs participate in the
    //    invert; 2D mesh elements and non-spatial data objects ride through.
    QSet<SWMMObjectRef> spatial;
    QSet<SWMMObjectRef> passthrough;
    for (const SWMMObjectRef &r : current) {
        if (catOf.contains(r)) spatial.insert(r);
        else                   passthrough.insert(r);
    }

    // 3. Scope: one category → invert within it; zero or many → invert across
    //    every category (excludes 2D by construction — mesh kinds have no
    //    Category).
    QSet<int> cats;
    for (const SWMMObjectRef &r : spatial)
        cats.insert(static_cast<int>(catOf.value(r)));

    QSet<SWMMObjectRef> universe;
    if (cats.size() == 1) {
        universe = byCategory[*cats.cbegin()];
    } else {
        for (const QSet<SWMMObjectRef> &s : std::as_const(byCategory))
            universe.unite(s);
    }

    // 4. next = (universe − selected) ∪ passthrough
    next.reserve(universe.size());
    for (const SWMMObjectRef &r : universe)
        if (!spatial.contains(r)) next.insert(r);
    next.unite(passthrough);

    return next;
}

TraceResult trace(SWMMModelLayer *model,
                  const QSet<SWMMObjectRef> &seeds,
                  bool upstream)
{
    TraceResult res;
    if (!model || !model->engine()) return res;
    SWMM_Engine e = model->engine();

    const SubcatchTopology topo = buildSubcatchTopology(e);

    // ── Seeding ────────────────────────────────────────────────────────────
    QSet<int> nodeSeeds;
    QSet<int> subcatchResult;   // subcatchments already known to be in the trace

    for (const SWMMObjectRef &r : seeds) {
        switch (r.objectType) {
        case SWMMObjectRef::Node: {
            const int idx = swmm_node_index(e, r.name.toUtf8().constData());
            if (idx >= 0) nodeSeeds.insert(idx);
            break;
        }
        case SWMMObjectRef::Link: {
            const int li = swmm_link_index(e, r.name.toUtf8().constData());
            if (li >= 0) {
                const int a = model->linkFromNodeIdx(li);
                const int b = model->linkToNodeIdx(li);
                if (a >= 0) nodeSeeds.insert(a);
                if (b >= 0) nodeSeeds.insert(b);
            }
            break;
        }
        case SWMMObjectRef::Subcatchment: {
            const int si = swmm_subcatch_index(e, r.name.toUtf8().constData());
            if (si < 0 || si >= topo.count) break;
            subcatchResult.insert(si);
            if (upstream) break;   // nothing but other subcatchments lies upstream of one

            // Downstream: walk the outlet chain. Each subcatchment hop joins
            // the result; the terminal outlet node seeds the link-graph BFS.
            int cur = si;
            QSet<int> walked{si};
            while (cur >= 0) {
                if (topo.outNode[cur] >= 0) { nodeSeeds.insert(topo.outNode[cur]); break; }
                const int nextSub = topo.outSub[cur];
                if (nextSub < 0 || walked.contains(nextSub)) break;   // dead end or cycle
                walked.insert(nextSub);
                subcatchResult.insert(nextSub);
                cur = nextSub;
            }
            break;
        }
        default:
            break;   // mesh + data refs are not traceable
        }
    }

    if (nodeSeeds.isEmpty() && subcatchResult.isEmpty()) return res;
    res.noSeeds = false;

    // ── Node BFS over the directed link graph ──────────────────────────────
    const ProfileRouter::Graph g = ProfileNetworkAdapter::buildGraphFromModel(model);

    QHash<int, QVector<int>> adj;   // traverse-from node → outgoing edge indices
    for (int i = 0; i < g.edges.size(); ++i)
        adj[upstream ? g.edges[i].toNode : g.edges[i].fromNode].push_back(i);

    QSet<int> visited = nodeSeeds;
    QList<int> queue(nodeSeeds.cbegin(), nodeSeeds.cend());
    while (!queue.isEmpty()) {
        const int n = queue.takeFirst();
        for (int ei : adj.value(n)) {
            const int nx = upstream ? g.edges[ei].fromNode : g.edges[ei].toNode;
            if (nx >= 0 && !visited.contains(nx)) { visited.insert(nx); queue.push_back(nx); }
        }
    }

    QVector<int> interiorLinks;
    for (const auto &edge : g.edges)
        if (visited.contains(edge.fromNode) && visited.contains(edge.toNode))
            interiorLinks.push_back(edge.linkId);

    // ── Subcatchment propagation ───────────────────────────────────────────
    if (upstream) {
        // Every subcatchment draining to a visited node is upstream of it …
        for (int n : visited)
            for (int si : topo.subsDrainingToNode.value(n))
                subcatchResult.insert(si);

        // … and so, transitively, is anything draining into those.
        QList<int> sq(subcatchResult.cbegin(), subcatchResult.cend());
        while (!sq.isEmpty()) {
            const int s = sq.takeFirst();
            for (int up : topo.subsDrainingToSub.value(s))
                if (!subcatchResult.contains(up)) { subcatchResult.insert(up); sq.push_back(up); }
        }
    }
    // Downstream: nothing to add — drainage never runs node → subcatchment, so
    // the only subcatchments in a downstream trace are the seed's outlet chain,
    // already collected during seeding.

    // ── Materialise refs ───────────────────────────────────────────────────
    for (int n : visited) {
        const char *id = swmm_node_id(e, n);
        if (id && *id) res.refs.insert(SWMMObjectRef(SWMMObjectRef::Node, QString::fromUtf8(id)));
    }
    for (int li : interiorLinks) {
        const char *id = swmm_link_id(e, li);
        if (id && *id) res.refs.insert(SWMMObjectRef(SWMMObjectRef::Link, QString::fromUtf8(id)));
    }
    for (int si : subcatchResult) {
        const char *id = swmm_subcatch_id(e, si);
        if (id && *id)
            res.refs.insert(SWMMObjectRef(SWMMObjectRef::Subcatchment, QString::fromUtf8(id)));
    }

    res.nodeCount     = static_cast<int>(visited.size());
    res.linkCount     = static_cast<int>(interiorLinks.size());
    res.subcatchCount = static_cast<int>(subcatchResult.size());
    return res;
}

} // namespace SelectionOps
