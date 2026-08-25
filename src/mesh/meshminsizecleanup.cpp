/*!
 * \file   meshminsizecleanup.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See meshminsizecleanup.h.  Standard half-edge-free edge collapse: candidate
 * edges are the short ones, each collapse is gated by the LINK CONDITION (the
 * textbook test for whether a collapse preserves manifoldness) plus an
 * orientation check on every affected triangle, and the whole pass is
 * transactional.
 */
#include "mesh/meshminsizecleanup.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

QString CleanupReport::summary() const
{
    if (abandoned)
        return QStringLiteral("cleanup ABANDONED after %1 pass(es) — mesh restored")
            .arg(passesUsed);
    return QStringLiteral("collapsed %1 edge(s), removed %2 cell(s) in %3 pass(es); "
                          "%4 protected sub-scale cell(s) left; min area %5 -> %6")
        .arg(edgesCollapsed).arg(cellsRemoved).arg(passesUsed)
        .arg(skippedProtected)
        .arg(minAreaBefore, 0, 'g', 4).arg(minAreaAfter, 0, 'g', 4)
        // Appended only when aggressive mode actually did something, so the
        // default line stays byte-identical.
        + ((identityAbsorptions || interiorConstraintsLost || ringEdgesShortened
            || identityConflictsSkipped || ringGuardSkipped || candidatesDropped
            || exhaustedPasses)
               ? QStringLiteral("; aggressive: %1 absorbed into identities, "
                                "%2 interior constraint(s) lost, %3 ring edge(s) "
                                "shortened, %4 identity conflict(s) skipped, "
                                "%5 ring-guard skip(s), %6 candidate(s) dropped%7")
                     .arg(identityAbsorptions).arg(interiorConstraintsLost)
                     .arg(ringEdgesShortened).arg(identityConflictsSkipped)
                     .arg(ringGuardSkipped).arg(candidatesDropped)
                     .arg(exhaustedPasses ? QStringLiteral(", PASS BUDGET EXHAUSTED")
                                          : QString())
               : QString());
}

namespace {

using VKey = QPair<int, int>;

VKey ekey(int a, int b) { return a < b ? qMakePair(a, b) : qMakePair(b, a); }

double signedArea(const QPointF &a, const QPointF &b, const QPointF &c)
{
    return 0.5 * ((b.x() - a.x()) * (c.y() - a.y())
                - (b.y() - a.y()) * (c.x() - a.x()));
}

double minTriangleArea(const MeshResult &m)
{
    double best = std::numeric_limits<double>::max();
    bool any = false;
    for (const MeshTriangle &t : m.triangles)
    {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
        if (t.v0 >= m.vertices.size() || t.v1 >= m.vertices.size()
            || t.v2 >= m.vertices.size()) continue;
        const double a = std::abs(signedArea(m.vertices[t.v0].xy,
                                             m.vertices[t.v1].xy,
                                             m.vertices[t.v2].xy));
        best = std::min(best, a);
        any = true;
    }
    return any ? best : 0.0;
}

/*! Structural view of the mesh, rebuilt once per pass. */
struct Topo
{
    QVector<QSet<int>>          vertTris;   ///< vertex -> incident triangles
    QVector<QSet<int>>          vertNbrs;   ///< vertex -> adjacent vertices
    QHash<VKey, QVector<int>>   edgeTris;   ///< edge -> incident triangles
    QSet<int>                   boundaryVerts;
    QSet<VKey>                  constrained; ///< edges that must survive
    QVector<bool>               protectedVert;
    QVector<bool>               isIdentityVert; ///< tag/coupledNode only — see build()
    QVector<int>                ringId;         ///< hull-cycle id, -1 = interior
    QHash<int, int>             ringVertexCount;

    void build(const MeshResult &m)
    {
        const int nv = m.vertices.size();
        vertTris.clear();  vertTris.resize(nv);
        vertNbrs.clear();  vertNbrs.resize(nv);
        protectedVert.clear();
        protectedVert.resize(nv);
        protectedVert.fill(false);
        edgeTris.clear();
        boundaryVerts.clear();
        constrained.clear();

        for (int ti = 0; ti < m.triangles.size(); ++ti)
        {
            const MeshTriangle &t = m.triangles[ti];
            const int v[3] = {t.v0, t.v1, t.v2};
            for (int k = 0; k < 3; ++k)
            {
                if (v[k] < 0 || v[k] >= nv) return;   // caller validates first
                vertTris[v[k]].insert(ti);
            }
            for (int k = 0; k < 3; ++k)
            {
                const int a = v[k], b = v[(k + 1) % 3];
                vertNbrs[a].insert(b);
                vertNbrs[b].insert(a);
                edgeTris[ekey(a, b)].append(ti);
            }
        }

        // A constrained edge is one Triangle handed back as a segment: the
        // domain outline, hole rings, and every conduit / breakline alignment.
        for (const MeshEdge &e : m.boundaryEdges)
        {
            if (e.v0 < 0 || e.v1 < 0 || e.v0 >= nv || e.v1 >= nv) continue;
            constrained.insert(ekey(e.v0, e.v1));
        }

        // Mesh-boundary vertices: any endpoint of an edge with one incident
        // triangle.  Collapsing across the boundary pinches the surface.
        for (auto it = edgeTris.constBegin(); it != edgeTris.constEnd(); ++it)
        {
            if (it.value().size() == 1)
            {
                boundaryVerts.insert(it.key().first);
                boundaryVerts.insert(it.key().second);
            }
        }

        // Identity: anything the engine or the coupling map keys on.
        for (int i = 0; i < nv; ++i)
        {
            const MeshVertex &v = m.vertices[i];
            if (v.marker != 0 || !v.tag.isEmpty() || !v.coupledNode.isEmpty())
                protectedVert[i] = true;
        }

        // A GENUINE coupling identity, which is NOT the same test as
        // protectedVert.  MeshGenerator gives every domain/hole boundary
        // vertex the same generic kBoundaryMarker (meshgenerator.cpp:273)
        // whether or not it couples anything, and only attaches a tag when a
        // Steiner point really carries one (:304, :585).  Treating a bare
        // marker as an identity would make every pair of adjacent ring
        // vertices look like "two distinct identities" and the aggressive pass
        // would refuse to shrink any ring at all.
        isIdentityVert.clear();
        isIdentityVert.resize(nv);
        isIdentityVert.fill(false);
        for (int i = 0; i < nv; ++i)
        {
            const MeshVertex &v = m.vertices[i];
            isIdentityVert[i] = !v.tag.isEmpty() || !v.coupledNode.isEmpty();
        }

        // Ring components: union-find over HULL edges only (exactly one
        // incident triangle), which is the same edge set boundaryVerts uses.
        // Each domain/hole outline is one disjoint cycle, so this gives every
        // ring vertex a ring id and a starting vertex count.
        ringId.clear();
        ringId.resize(nv);
        ringId.fill(-1);
        {
            QVector<int> rp(nv);
            for (int i = 0; i < nv; ++i) rp[i] = i;
            auto rf = [&rp](int i) {
                while (rp[i] != i) { rp[i] = rp[rp[i]]; i = rp[i]; }
                return i;
            };
            for (auto it = edgeTris.constBegin(); it != edgeTris.constEnd(); ++it)
                if (it.value().size() == 1)
                    rp[rf(it.key().first)] = rf(it.key().second);
            ringVertexCount.clear();
            for (const int bv : std::as_const(boundaryVerts))
            {
                const int r = rf(bv);
                ringId[bv] = r;
                ++ringVertexCount[r];
            }
        }

        // Endpoints of EVERY constrained edge, not just the marked ones.
        // Deliberately more conservative than the plan, which protected only
        // marker-bearing endpoints: protecting the edge alone still lets a
        // vertex move via some OTHER incident edge, which would drag the
        // domain outline or a conduit alignment with it.  Freezing the
        // endpoints makes "boundaryEdges is preserved exactly" a real
        // invariant the pass can assert on, and the slivers Triangle inserts
        // are overwhelmingly interior anyway.
        for (const MeshEdge &e : m.boundaryEdges)
        {
            if (e.v0 >= 0 && e.v0 < nv) protectedVert[e.v0] = true;
            if (e.v1 >= 0 && e.v1 < nv) protectedVert[e.v1] = true;
        }
    }
};

/*! Every triangle index is in range and references three distinct vertices. */
bool indicesValid(const MeshResult &m)
{
    const int nv = m.vertices.size();
    for (const MeshTriangle &t : m.triangles)
    {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) return false;
        if (t.v0 >= nv || t.v1 >= nv || t.v2 >= nv) return false;
        if (t.v0 == t.v1 || t.v1 == t.v2 || t.v0 == t.v2) return false;
    }
    for (const CellCoupling &c : m.cellCouplings)
        if (c.tri < 0 || c.tri >= m.triangles.size()) return false;
    return true;
}

} // namespace

bool collapseSubScaleCells(MeshResult *mesh, const CleanupPolicy &policy,
                           CleanupReport *report)
{
    Q_ASSERT(mesh && report);
    *report = CleanupReport{};

    if (policy.minCellSize <= 0.0 || policy.beta <= 0.0) return true;
    if (mesh->triangles.isEmpty() || mesh->vertices.isEmpty()) return true;
    if (!indicesValid(*mesh)) return true;   // not ours to repair

    const double thresh  = policy.beta * policy.minCellSize;
    const double thresh2 = thresh * thresh;
    report->minAreaBefore = minTriangleArea(*mesh);
    report->minAreaAfter  = report->minAreaBefore;

    for (int pass = 0; pass < policy.maxPasses; ++pass)
    {
        const MeshResult snapshot = *mesh;   // transactional: restore on failure
        report->passesUsed = pass + 1;

        Topo topo;
        topo.build(*mesh);

        // ── Candidate short edges, shortest first ───────────────────────
        QVector<QPair<double, VKey>> cands;
        for (auto it = topo.edgeTris.constBegin(); it != topo.edgeTris.constEnd(); ++it)
        {
            const int a = it.key().first, b = it.key().second;
            const QPointF &pa = mesh->vertices[a].xy;
            const QPointF &pb = mesh->vertices[b].xy;
            const double dx = pb.x() - pa.x(), dy = pb.y() - pa.y();
            const double d2 = dx * dx + dy * dy;
            if (d2 < thresh2) cands.append(qMakePair(d2, it.key()));
        }
        if (cands.isEmpty())
        {
            report->passesUsed = pass;   // this pass did nothing
            break;
        }
        // Length alone is NOT a total order: a Triangle mesh carries many
        // exactly-congruent short edges, cands was gathered in QHash order,
        // and std::sort is not stable — so ties broke on the hash seed, and
        // since the first collapse of a pair `touched`-blocks its neighbours,
        // the whole collapse set differed run to run.  Tie-break on the edge
        // key, which is (min,max) vertex index and therefore canonical.
        // Relaxing the default protections multiplies how many edges qualify,
        // so aggressive mode gets a budget.  nth_element keeps the shortest N
        // without paying to sort the discarded tail.
        if (policy.allowIdentityCollapse
            && policy.maxCandidatesPerPass > 0
            && cands.size() > policy.maxCandidatesPerPass)
        {
            const auto keep = cands.begin() + policy.maxCandidatesPerPass;
            std::nth_element(cands.begin(), keep, cands.end(),
                             [](const auto &x, const auto &y) {
                                 if (x.first != y.first) return x.first < y.first;
                                 return x.second < y.second;
                             });
            report->candidatesDropped += int(cands.size() - policy.maxCandidatesPerPass);
            cands.resize(policy.maxCandidatesPerPass);
        }

        std::sort(cands.begin(), cands.end(),
                  [](const auto &x, const auto &y) {
                      if (x.first != y.first) return x.first < y.first;
                      return x.second < y.second;
                  });

        // Live per-pass ring sizes, decremented as ring collapses commit, so
        // two collapses in one pass cannot jointly breach minRingVertices.
        QHash<int, int> liveRingCount = topo.ringVertexCount;

        // Union-find over vertices: b merges into a.
        QVector<int> parent(mesh->vertices.size());
        for (int i = 0; i < parent.size(); ++i) parent[i] = i;
        auto find = [&](int i) {
            while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
            return i;
        };

        QVector<QPointF> newPos;
        newPos.reserve(mesh->vertices.size());
        for (const MeshVertex &v : mesh->vertices) newPos.append(v.xy);
        QVector<double> newZ;
        newZ.reserve(mesh->vertices.size());
        for (const MeshVertex &v : mesh->vertices) newZ.append(v.z);

        QSet<int> touched;   // vertices already involved in a collapse this pass
        int collapsed = 0;

        for (const auto &c : std::as_const(cands))
        {
            const int a = c.second.first, b = c.second.second;

            auto reject = [&] {
                ++report->skippedProtected;
                if (report->unfixable.size()
                        < static_cast<qsizetype>(policy.maxUnfixable))
                {
                    const QPointF &pa = mesh->vertices[a].xy;
                    const QPointF &pb = mesh->vertices[b].xy;
                    report->unfixable.append(
                        QPointF((pa.x() + pb.x()) / 2.0, (pa.y() + pb.y()) / 2.0));
                }
            };

            const bool aggressive = policy.allowIdentityCollapse;
            const bool aId = aggressive && topo.isIdentityVert[a];
            const bool bId = aggressive && topo.isIdentityVert[b];

            if (!aggressive)
            {
                if (topo.constrained.contains(c.second))            { reject(); continue; }
                if (topo.protectedVert[a] || topo.protectedVert[b]) { reject(); continue; }
            }
            else
            {
                // Never fuse two distinct coupling identities — the same rule
                // pslgminsize's invariant (1) enforces on the input side.
                if (aId && bId)
                {
                    ++report->identityConflictsSkipped;
                    reject();
                    continue;
                }
            }
            if (touched.contains(a) || touched.contains(b))     continue;

            const QVector<int> &inc = topo.edgeTris[c.second];
            if (inc.size() != 1 && inc.size() != 2)            { reject(); continue; }

            // Ring guard (aggressive only): shortening a ring below
            // minRingVertices leaves a boundary that bounds no area — the
            // mesh-side twin of the PSLG fold this feature already guards.
            const bool ringEdge = aggressive && inc.size() == 1;
            int ringKey = -1;
            if (ringEdge)
            {
                ringKey = topo.ringId[a] >= 0 ? topo.ringId[a] : topo.ringId[b];
                if (ringKey >= 0
                    && liveRingCount.value(ringKey, 0) - 1 < policy.minRingVertices)
                {
                    ++report->ringGuardSkipped;
                    reject();
                    continue;
                }
            }

            // Pinch guard: both endpoints on the mesh boundary but the edge
            // itself interior would fuse two boundary curves.
            const bool aB = topo.boundaryVerts.contains(a);
            const bool bB = topo.boundaryVerts.contains(b);
            // Unchanged in BOTH modes: fusing two boundary curves through an
            // interior edge pinches the surface, which no opt-in should allow.
            if (aB && bB && inc.size() == 2)                   { reject(); continue; }

            // ── Link condition ────────────────────────────────────────
            // nbr(a) ∩ nbr(b) must be exactly the vertices opposite the edge,
            // otherwise the collapse creates a non-manifold vertex or folds
            // the surface onto itself.
            QSet<int> opposite;
            for (const int ti : inc)
            {
                const MeshTriangle &t = mesh->triangles[ti];
                const int v[3] = {t.v0, t.v1, t.v2};
                for (int k = 0; k < 3; ++k)
                    if (v[k] != a && v[k] != b) opposite.insert(v[k]);
            }
            QSet<int> shared = topo.vertNbrs[a];
            shared.intersect(topo.vertNbrs[b]);
            if (shared != opposite)                            { reject(); continue; }

            // ── Target position and orientation check ─────────────────
            const QPointF &pa = mesh->vertices[a].xy;
            const QPointF &pb = mesh->vertices[b].xy;
            // With exactly one identity endpoint the identity does not move at
            // all — its position is authoritative (it is a coupling location,
            // and a rim elevation goes with it).  Averaging would drag the
            // coupling point off the node it represents.
            const QPointF target =
                  aId ? pa
                : bId ? pb
                : QPointF((pa.x() + pb.x()) / 2.0, (pa.y() + pb.y()) / 2.0);

            bool flips = false;
            for (const int end : {a, b})
            {
                for (const int ti : std::as_const(topo.vertTris[end]))
                {
                    if (inc.contains(ti)) continue;   // this one disappears
                    const MeshTriangle &t = mesh->triangles[ti];
                    const int v[3] = {t.v0, t.v1, t.v2};
                    QPointF p[3];
                    for (int k = 0; k < 3; ++k)
                        p[k] = (v[k] == a || v[k] == b) ? target
                                                        : mesh->vertices[v[k]].xy;
                    const double before = signedArea(mesh->vertices[v[0]].xy,
                                                     mesh->vertices[v[1]].xy,
                                                     mesh->vertices[v[2]].xy);
                    const double after  = signedArea(p[0], p[1], p[2]);
                    if (after == 0.0 || (before > 0.0) != (after > 0.0))
                    { flips = true; break; }
                }
                if (flips) break;
            }
            if (flips)                                         { reject(); continue; }

            // ── Commit this collapse ─────────────────────────────────
            // The identity must be the SURVIVOR, so the rebuild carries its
            // marker/tag/coupledNode through with no separate migration step.
            const int keep = bId ? b : a;
            const int gone = (keep == a) ? b : a;

            parent[find(gone)] = find(keep);
            newPos[keep] = target;
            // An identity's elevation is authoritative too — do not average it.
            if (!(aId || bId)) newZ[keep] = 0.5 * (newZ[a] + newZ[b]);

            if (aggressive)
            {
                if (aId || bId) ++report->identityAbsorptions;
                if (topo.constrained.contains(c.second) && inc.size() == 2)
                    ++report->interiorConstraintsLost;
                if (ringEdge)
                {
                    ++report->ringEdgesShortened;
                    if (ringKey >= 0) --liveRingCount[ringKey];
                }
            }

            touched.insert(a);
            touched.insert(b);
            for (const int n : std::as_const(topo.vertNbrs[a])) touched.insert(n);
            for (const int n : std::as_const(topo.vertNbrs[b])) touched.insert(n);
            ++collapsed;
        }

        if (collapsed == 0)
        {
            report->passesUsed = pass;
            break;
        }

        // ── Rebuild: remap, drop degenerates, compact ──────────────────
        MeshResult out;
        out.ok       = mesh->ok;
        out.errorMsg = mesh->errorMsg;

        QVector<int> remap(mesh->vertices.size(), -1);
        for (int i = 0; i < mesh->vertices.size(); ++i)
        {
            const int r = find(i);
            if (r != i) continue;               // merged away
            remap[i] = out.vertices.size();
            MeshVertex v = mesh->vertices[i];
            v.xy = newPos[i];
            v.z  = newZ[i];
            out.vertices.append(std::move(v));
        }
        auto mapV = [&](int i) { return remap[find(i)]; };

        QVector<int> triRemap(mesh->triangles.size(), -1);
        for (int ti = 0; ti < mesh->triangles.size(); ++ti)
        {
            const MeshTriangle &t = mesh->triangles[ti];
            const int a = mapV(t.v0), b = mapV(t.v1), c = mapV(t.v2);
            if (a < 0 || b < 0 || c < 0) continue;
            if (a == b || b == c || a == c) continue;    // collapsed away
            MeshTriangle nt = t;
            nt.v0 = a; nt.v1 = b; nt.v2 = c;
            triRemap[ti] = out.triangles.size();
            out.triangles.append(std::move(nt));
        }

        for (const MeshEdge &e : std::as_const(mesh->boundaryEdges))
        {
            const int a = mapV(e.v0), b = mapV(e.v1);
            if (a < 0 || b < 0 || a == b) continue;
            MeshEdge ne = e;
            ne.v0 = a; ne.v1 = b;
            out.boundaryEdges.append(std::move(ne));
        }

        for (const CellCoupling &cc : std::as_const(mesh->cellCouplings))
        {
            if (cc.tri < 0 || cc.tri >= triRemap.size()) continue;
            if (triRemap[cc.tri] < 0) continue;   // its cell is gone
            CellCoupling nc = cc;
            nc.tri = triRemap[cc.tri];
            out.cellCouplings.append(std::move(nc));
        }

        // ── Validate, or roll the whole pass back ─────────────────────
        // In aggressive mode losing interior constrained edges and ring edges
        // is the POINT — each one was counted and guarded per candidate above,
        // so this whole-pass check would just undo the requested work.
        const bool lostConstrainedEdge =
            !policy.allowIdentityCollapse
            && out.boundaryEdges.size() != mesh->boundaryEdges.size();
        // NEVER relaxed, in either mode: a silently dropped 1D-2D coupling is
        // worse than any sliver.
        const bool lostCoupling =
            out.cellCouplings.size() != mesh->cellCouplings.size();
        if (!indicesValid(out) || out.triangles.isEmpty()
            || lostConstrainedEdge || lostCoupling)
        {
            *mesh = snapshot;
            report->abandoned = true;
            report->minAreaAfter = minTriangleArea(*mesh);
            return false;
        }

        report->cellsRemoved   += static_cast<int>(mesh->triangles.size()
                                                 - out.triangles.size());
        report->edgesCollapsed += collapsed;
        *mesh = std::move(out);
    }

    // passesUsed is set at the top of every pass and only ever DECREMENTED by
    // an early break ("no candidates" / "nothing collapsed").  So reaching
    // maxPasses means the loop never ran out of work — it ran out of budget.
    report->exhaustedPasses =
        policy.allowIdentityCollapse && report->passesUsed >= policy.maxPasses;

    report->minAreaAfter = minTriangleArea(*mesh);
    return true;
}

} // namespace mesh
