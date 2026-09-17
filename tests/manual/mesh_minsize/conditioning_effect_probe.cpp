/*!
 * \file   conditioning_effect_probe.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MIN_CELL_SIZE_TESTING_HANDOFF_2026-08-17.md — the end-to-end half of Phase 4
 * plus the "does it actually work" question the unit tests cannot answer.
 *
 * For one real SWMM model it runs the whole minimum-cell-size path exactly as
 * the mesh-generation worker does — condition the PSLG, mesh with the
 * refinement floor installed, collapse leftover slivers — at several values of
 * h, and reports:
 *
 *   - the SHA-256 of the mesh at h = 0, which must equal the unconditioned
 *     mesh's hash (the regression guarantee that justifies shipping);
 *   - the smallest cell, the count below A_min, and the vertex count at each h,
 *     so the trade the user is making is a number rather than a claim;
 *   - whether Triangle accepted the conditioned PSLG at all.
 *
 * Manual build: see tests/manual/mesh_minsize/README.md
 */
#include "inp_pslg.h"

#include "mesh/meshcellstats.h"
#include "mesh/meshminsizecleanup.h"
#include "mesh/pslgminsize.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

double triArea(const QPointF &a, const QPointF &b, const QPointF &c)
{
    return 0.5 * std::abs((b.x() - a.x()) * (c.y() - a.y())
                        - (b.y() - a.y()) * (c.x() - a.x()));
}

/*! Content hash of the conditioned PSLG, in emission order.
 *
 *  Separates the two ways conditioning can be nondeterministic: if this is
 *  stable while the mesh hash is not, conditioning produced the same geometry
 *  and the variation is downstream (emission order into Triangle); if this
 *  varies, conditioning itself is order-dependent. */
QByteArray pslgHash(const probe::Pslg &g)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    auto feed = [&h](const void *p, int n) {
        h.addData(QByteArrayView(static_cast<const char *>(p), n));
    };
    auto pt = [&](const QPointF &q) {
        const double xy[2] = {q.x(), q.y()};
        feed(xy, sizeof(xy));
    };
    for (const QPolygonF &d : g.domains)
    { const int n = int(d.size()); feed(&n, sizeof(n)); for (const QPointF &q : d) pt(q); }
    for (const mesh::ConstraintSegment &c : g.segs)
    {
        const int n = int(c.path.size());
        feed(&n, sizeof(n)); feed(&c.marker, sizeof(c.marker));
        for (const QPointF &q : c.path) pt(q);
    }
    for (const mesh::SteinerPoint &s : g.pts) { pt(s.xy); feed(&s.marker, sizeof(s.marker)); }
    return h.result().toHex();
}

/*! Content hash of the mesh: every coordinate and every index, bit-exact. */
QByteArray meshHash(const mesh::MeshResult &m)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    auto feed = [&h](const void *p, int n) {
        h.addData(QByteArrayView(static_cast<const char *>(p), n));
    };
    for (const mesh::MeshVertex &v : m.vertices)
    {
        const double xy[2] = {v.xy.x(), v.xy.y()};
        feed(xy, sizeof(xy));
        feed(&v.marker, sizeof(v.marker));
    }
    for (const mesh::MeshTriangle &t : m.triangles)
    {
        const int idx[3] = {t.v0, t.v1, t.v2};
        feed(idx, sizeof(idx));
    }
    for (const mesh::MeshEdge &e : m.boundaryEdges)
    {
        const int idx[3] = {e.v0, e.v1, e.marker};
        feed(idx, sizeof(idx));
    }
    return h.result().toHex();
}

struct Stats
{
    bool      ok       = false;
    QString   err;
    qsizetype vertices = 0;
    qsizetype cells    = 0;
    double    minArea  = 0.0;
    int       belowAmin = 0;
    qint64    genMs    = 0;
    QByteArray hash;

    // The quantity that actually sets the marcher timestep. minArea cannot
    // stand in for it: a thin cell can carry area and still have a tiny
    // L_char, and a refinement explosion LOWERS the p50/min ratio while making
    // the mesh strictly worse. See meshcellstats.h.
    double minLchar = 0.0;
    double p50Lchar = 0.0;
    double ltsWork  = 0.0;
};

Stats measure(const mesh::MeshResult &r, double aMin, qint64 genMs)
{
    Stats s;
    s.ok = r.ok;
    s.err = r.errorMsg;
    s.genMs = genMs;
    if (!r.ok) return s;
    s.vertices = r.vertices.size();
    s.cells    = r.triangles.size();
    s.minArea  = std::numeric_limits<double>::max();
    for (const mesh::MeshTriangle &t : r.triangles)
    {
        const double a = triArea(r.vertices[t.v0].xy, r.vertices[t.v1].xy,
                                 r.vertices[t.v2].xy);
        s.minArea = std::min(s.minArea, a);
        if (aMin > 0.0 && a < aMin) ++s.belowAmin;
    }
    if (r.triangles.isEmpty()) s.minArea = 0.0;

    const mesh::CflStats c = mesh::computeCflStats(r);
    s.minLchar = c.min;
    s.p50Lchar = c.p50;
    s.ltsWork  = c.ltsWork;

    s.hash = meshHash(r);
    return s;
}

/*!
 * \brief The PSLG defects Triangle chokes on that invariant (5) does NOT cover.
 *
 * conditionMinSize only verifies that PROPER crossings did not increase.  A
 * proper crossing is an X; everything degenerate is a T or an overlap, and
 * properlyCrosses() deliberately returns false for all of those.  Triangle
 * aborts on them just the same.
 */
struct PslgAudit
{
    int duplicateSegments = 0;   ///< the same undirected pair twice
    int zeroLengthSegments = 0;
    int collinearOverlaps  = 0;  ///< two segments sharing more than a point
    int vertexOnSegment    = 0;  ///< a vertex exactly interior to another segment
    QVector<QPointF> examples;
};

PslgAudit auditPslg(const probe::Pslg &g)
{
    PslgAudit a;
    QVector<QPair<QPointF, QPointF>> e;
    for (const QPolygonF &d : g.domains)
    {
        const int n = d.size();
        if (n < 3) continue;
        const int en = (d.first() == d.last()) ? n - 1 : n;
        for (int i = 0; i < en; ++i) e.append({d[i], d[(i + 1) % en]});
    }
    for (const auto &sg : g.segs)
        for (int i = 1; i < sg.path.size(); ++i)
            e.append({sg.path[i - 1], sg.path[i]});

    auto key = [](const QPointF &p, const QPointF &q) {
        const bool sw = (p.x() != q.x()) ? (q.x() < p.x()) : (q.y() < p.y());
        const QPointF &a = sw ? q : p; const QPointF &b = sw ? p : q;
        return QStringLiteral("%1|%2|%3|%4")
            .arg(a.x(), 0, 'g', 17).arg(a.y(), 0, 'g', 17)
            .arg(b.x(), 0, 'g', 17).arg(b.y(), 0, 'g', 17);
    };
    QHash<QString, int> seen;
    for (const auto &s : std::as_const(e))
    {
        if (s.first == s.second) { ++a.zeroLengthSegments; continue; }
        const QString k = key(s.first, s.second);
        if (++seen[k] == 2)
        {
            ++a.duplicateSegments;
            if (a.examples.size() < 8) a.examples.append(s.first);
        }
    }

    // Grid the segments so the pairwise scan stays tractable on a real model.
    double span = 0.0;
    for (const auto &s : e)
        span = std::max(span, std::hypot(s.second.x() - s.first.x(),
                                         s.second.y() - s.first.y()));
    const double cell = std::max(span, 1.0);
    QHash<QPair<int, int>, QVector<int>> grid;
    auto cellOf = [cell](const QPointF &p) {
        return qMakePair(int(std::floor(p.x() / cell)),
                         int(std::floor(p.y() / cell)));
    };
    for (int i = 0; i < e.size(); ++i)
    {
        const auto c0 = cellOf(e[i].first), c1 = cellOf(e[i].second);
        for (int gy = std::min(c0.second, c1.second); gy <= std::max(c0.second, c1.second); ++gy)
            for (int gx = std::min(c0.first, c1.first); gx <= std::max(c0.first, c1.first); ++gx)
                grid[qMakePair(gx, gy)].append(i);
    }
    auto cross = [](const QPointF &o, const QPointF &p, const QPointF &q) {
        return (p.x() - o.x()) * (q.y() - o.y()) - (p.y() - o.y()) * (q.x() - o.x());
    };
    auto onSeg = [](const QPointF &p, const QPointF &s0, const QPointF &s1) {
        return p.x() >= std::min(s0.x(), s1.x()) && p.x() <= std::max(s0.x(), s1.x())
            && p.y() >= std::min(s0.y(), s1.y()) && p.y() <= std::max(s0.y(), s1.y());
    };
    QSet<QPair<int, int>> tested;
    for (auto it = grid.constBegin(); it != grid.constEnd(); ++it)
    {
        const QVector<int> &v = it.value();
        for (int i = 0; i < v.size(); ++i)
            for (int j = i + 1; j < v.size(); ++j)
            {
                const auto pk = qMakePair(std::min(v[i], v[j]), std::max(v[i], v[j]));
                if (tested.contains(pk)) continue;
                tested.insert(pk);
                const auto &A = e[v[i]]; const auto &B = e[v[j]];
                const bool shares = (A.first == B.first) || (A.first == B.second)
                                 || (A.second == B.first) || (A.second == B.second);
                // Collinear overlap: all four endpoints on one line and the
                // spans genuinely overlap.
                if (cross(A.first, A.second, B.first) == 0.0
                    && cross(A.first, A.second, B.second) == 0.0)
                {
                    const bool bInA = onSeg(B.first, A.first, A.second)
                                   || onSeg(B.second, A.first, A.second);
                    const bool aInB = onSeg(A.first, B.first, B.second)
                                   || onSeg(A.second, B.first, B.second);
                    if ((bInA || aInB) && !(shares && !bInA && !aInB))
                    {
                        // A shared endpoint alone is legal; an actual overlap is not.
                        const bool realOverlap =
                            !shares
                            || onSeg(B.first, A.first, A.second) != (B.first == A.first || B.first == A.second)
                            || onSeg(B.second, A.first, A.second) != (B.second == A.first || B.second == A.second);
                        if (realOverlap)
                        {
                            ++a.collinearOverlaps;
                            if (a.examples.size() < 16) a.examples.append(B.first);
                            continue;
                        }
                    }
                }
                if (shares) continue;
                // Vertex of one lying exactly ON the interior of the other.
                for (const QPointF &p : {B.first, B.second})
                    if (cross(A.first, A.second, p) == 0.0
                        && onSeg(p, A.first, A.second))
                    { ++a.vertexOnSegment;
                      if (a.examples.size() < 24) a.examples.append(p); }
                for (const QPointF &p : {A.first, A.second})
                    if (cross(B.first, B.second, p) == 0.0
                        && onSeg(p, B.first, B.second))
                    { ++a.vertexOnSegment;
                      if (a.examples.size() < 24) a.examples.append(p); }
            }
    }
    return a;
}

/*! One generation, wired exactly as runMeshPipelineImpl wires it. */
mesh::MeshResult generate(const probe::Pslg &g, double maxArea,
                          const mesh::pslg::MinSizePolicy &pol, qint64 *msOut)
{
    mesh::MeshGenerator mg;
    mg.setDomains(g.domains);
    for (const auto &sp : g.pts)  mg.addSteinerPoint(sp);
    for (const auto &cs : g.segs) mg.addConstraintSegment(cs);

    mesh::GenerationOptions o;
    o.maxArea  = maxArea;
    o.minAngle = 26.0;
    o.quiet    = true;
    mg.setOptions(o);

    // The refinement floor, verbatim from the worker.
    if (pol.enabled() && pol.minTriangleArea() > 0.0)
    {
        mesh::RefineHook hook;
        const double capped = pol.refinementAreaCap(maxArea);
        hook.targetAreaAt = [capped](double, double) { return capped; };
        mg.setRefineHook(hook);
    }

    QElapsedTimer clock; clock.start();
    mesh::MeshResult r = mg.generate();
    *msOut = clock.elapsed();
    return r;
}

void printRow(const char *label, const Stats &s, double aMin)
{
    if (!s.ok)
    {
        std::printf("  %-22s FAILED: %s\n", label, qPrintable(s.err));
        return;
    }
    std::printf("  %-22s %8lld verts %9lld cells  minArea %-12.6g "
                "belowAmin %-7d %5lld ms  %s\n",
                label, (long long)s.vertices, (long long)s.cells,
                s.minArea, s.belowAmin, (long long)s.genMs,
                s.hash.left(12).constData());
    std::printf("  %-22s minL %-12.6g p50L %-12.6g W %-12.6g\n",
                "", s.minLchar, s.p50Lchar, s.ltsWork);
    (void)aMin;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2)
    {
        std::fprintf(stderr,
            "usage: conditioning_effect_probe <model.inp> [maxArea] [h ...]\n");
        return 2;
    }
    const QString inpPath = QString::fromLocal8Bit(argv[1]);
    const double  cliArea = (argc > 2) ? QString::fromLocal8Bit(argv[2]).toDouble() : 0.0;

    probe::Pslg base;
    QString err;
    if (!probe::loadPslg(inpPath, &base, &err))
    { std::fprintf(stderr, "read failed: %s\n", qPrintable(err)); return 2; }

    // EXPERIMENT B (min-cell plan Phase 0.6) — the ceiling. Drop the conduit
    // constraints and mesh domain + node Steiner points only. Whatever min
    // L_char that reaches is the best ANY conditioning of the conduit set
    // could reach, because conditioning may soften those constraints but can
    // never remove them.
    const bool noConduits = !qEnvironmentVariableIsEmpty("MINSIZE_PROBE_NO_CONDUITS");
    if (noConduits)
    {
        std::printf("!! MINSIZE_PROBE_NO_CONDUITS — dropping %lld conduit "
                    "constraints (ceiling measurement)\n",
                    (long long)base.segs.size());
        base.segs.clear();
    }

    const QRectF bb = base.domains.first().boundingRect();
    const double maxArea = cliArea > 0.0 ? cliArea
                                         : (bb.width() * bb.height()) / 200000.0;

    QVector<double> hs;
    for (int i = 3; i < argc; ++i) hs.append(QString::fromLocal8Bit(argv[i]).toDouble());
    if (hs.isEmpty())
    {
        // Fractions of the uniform cell scale implied by maxArea.
        const double side = std::sqrt(4.0 * maxArea / std::sqrt(3.0));
        hs = {side / 6.0, side / 3.0, side / 1.5};
    }

    std::printf("model     : %s\n", qPrintable(QFileInfo(inpPath).fileName()));
    std::printf("PSLG      : %lld nodes, %lld conduits\n",
                (long long)base.pts.size(), (long long)base.segs.size());
    std::printf("maxArea   : %.6g\n\n", maxArea);

    // ── Reference: no conditioning at all ───────────────────────────────
    qint64 ms = 0;
    const mesh::MeshResult ref = generate(base, maxArea, {}, &ms);
    const Stats refS = measure(ref, 0.0, ms);
    std::printf("%-24s %8s %9s  %-20s %-17s %8s  %s\n",
                "", "verts", "cells", "minArea", "belowAmin", "time", "sha256");
    printRow("unconditioned", refS, 0.0);
    if (!refS.ok) return 1;

    // ── Regression guarantee: h = 0 must reproduce it EXACTLY ───────────
    // (handoff Phase 4: "with h = 0 the mesh must be identical to the current
    // build's output ... hash the written mesh file".)
    {
        probe::Pslg g = base;
        QVector<QVector<QPointF>> holes;
        mesh::pslg::MinSizePolicy pol;                 // minCellSize 0 => off
        mesh::pslg::ConditionReport crep;
        const bool ok = mesh::pslg::conditionMinSize(
            &g.domains, &holes, &g.segs, &g.pts, pol, &crep);
        qint64 t = 0;
        const mesh::MeshResult r = generate(g, maxArea, pol, &t);
        const Stats s0 = measure(r, 0.0, t);
        printRow("h = 0 (feature off)", s0, 0.0);
        std::printf("  -> conditionMinSize returned %s, abandoned=%s\n",
                    ok ? "true" : "false",
                    crep.conditioningAbandoned ? "true" : "false");
        std::printf("  -> h=0 mesh %s the unconditioned mesh\n",
                    s0.hash == refS.hash ? "MATCHES" : "DIFFERS FROM ***");
    }

    // ── The real thing, at several h ────────────────────────────────────
    struct Row { double h; Stats s; mesh::pslg::ConditionReport crep;
                 mesh::CleanupReport clean; bool condOk = false;
                 bool cleanOk = false; };
    QVector<Row> rows;

    std::printf("\n");
    for (const double h : std::as_const(hs))
    {
        Row row; row.h = h;
        probe::Pslg g = base;
        QVector<QVector<QPointF>> holes;

        mesh::pslg::MinSizePolicy pol;
        pol.minCellSize = h;
        // The two opt-ins from the 2026-08-21 crash-fix/enforcement handoff.
        pol.allowIdentityMerge =
            !qEnvironmentVariableIsEmpty("MINSIZE_PROBE_IDENTITY_MERGE");
        pol.resolveDefaults();
        const double aMin = pol.minTriangleArea();

        QElapsedTimer condClock; condClock.start();
        row.condOk = mesh::pslg::conditionMinSize(
            &g.domains, &holes, &g.segs, &g.pts, pol, &row.crep);
        const qint64 condMs = condClock.elapsed();

        const PslgAudit au = auditPslg(g);

        qint64 t = 0;
        mesh::MeshResult r = generate(g, maxArea, pol, &t);

        mesh::CleanupPolicy cpol;
        cpol.minCellSize = h;
        cpol.allowIdentityCollapse =
            !qEnvironmentVariableIsEmpty("MINSIZE_PROBE_AGGRESSIVE_CLEANUP");
        row.cleanOk = mesh::collapseSubScaleCells(&r, cpol, &row.clean);

        row.s = measure(r, aMin, t);
        rows.append(row);

        std::printf("h = %-8.4g  A_min = %-12.6g  (conditioning %lld ms, %s)\n",
                    h, aMin, (long long)condMs,
                    row.condOk ? "applied" : "ABANDONED");
        printRow("  conditioned", row.s, aMin);
        std::printf("    %s\n", qPrintable(row.crep.summary()));
        std::printf("    pslgSha %s\n", pslgHash(g).left(12).constData());
        std::printf("    cleanup: %s\n", qPrintable(row.clean.summary()));
        // Spike census. A weld that merges two vertices two apart in a path
        // rewrites [a, b, c] to [a, b, a]: stage 3b only collapses CONSECUTIVE
        // duplicates, so the revisit survives as a zero-angle spike. That is an
        // lfs of exactly 0, which Ruppert answers with an unbounded shell
        // cascade. Count them in the input and in the conditioned PSLG.
        {
            auto spikes = [](const QVector<mesh::ConstraintSegment> &segs,
                             int *revisitsOut) {
                int spike = 0, revisit = 0;
                for (const auto &sg : segs)
                {
                    for (int i = 0; i + 2 < sg.path.size(); ++i)
                        if (sg.path[i] == sg.path[i + 2]) ++spike;
                    QSet<QPair<qreal, qreal>> seen;
                    for (const QPointF &q : sg.path)
                    {
                        const auto k = qMakePair(q.x(), q.y());
                        if (seen.contains(k)) { ++revisit; }
                        else seen.insert(k);
                    }
                }
                if (revisitsOut) *revisitsOut = revisit;
                return spike;
            };
            int rIn = 0, rOut = 0;
            const int sIn  = spikes(base.segs, &rIn);
            const int sOut = spikes(g.segs, &rOut);
            std::printf("    spikes(a-b-a): input %d, conditioned %d   |   "
                        "path revisits: input %d, conditioned %d\n",
                        sIn, sOut, rIn, rOut);
        }

        // Local feature size of the INPUT vs the CONDITIONED PSLG at this h.
        // `min lfs 0` in the summary says a zero-scale feature SURVIVES
        // conditioning; this says whether conditioning inherited it or
        // manufactured it, and of which kind.
        {
            auto hist = [](const QVector<mesh::pslg::Violation> &v,
                           const char *label, double hh) {
                int c[4] = {0, 0, 0, 0};
                int zero = 0;
                double worst = std::numeric_limits<double>::max();
                for (const auto &x : v)
                {
                    ++c[int(x.cause)];
                    if (!(x.lfs > 0.0)) ++zero;
                    worst = std::min(worst, x.lfs);
                }
                std::printf("    lfs(%-11s) n=%-7lld min=%-12.6g zero=%-6d "
                            "short %d | close %d | angle %d | ring %d\n",
                            label, (long long)v.size(),
                            v.isEmpty() ? hh : worst, zero,
                            c[0], c[1], c[2], c[3]);
                for (int k = 0; k < std::min<int>(3, v.size()); ++k)
                    std::printf("        %-14s lfs=%-12.6g at (%.3f, %.3f) %s %s\n",
                                qPrintable(mesh::pslg::violationCauseName(v[k].cause)),
                                v[k].lfs, v[k].xy.x(), v[k].xy.y(),
                                qPrintable(v[k].tagA), qPrintable(v[k].tagB));
            };
            QVector<QVector<QPointF>> noHoles;
            hist(mesh::pslg::analyseLocalFeatureSize(
                     base.domains, noHoles, base.segs, base.pts, h, -1),
                 "input", h);
            hist(mesh::pslg::analyseLocalFeatureSize(
                     g.domains, holes, g.segs, g.pts, h, -1),
                 "conditioned", h);
        }

        // Residual cause histogram on the CONDITIONED PSLG. `min lfs 0` in the
        // summary says a zero-scale feature SURVIVES conditioning; this says
        // which kind, which is what decides whether welding manufactured it.
        {
            int rc[4] = {0, 0, 0, 0};
            int zero = 0;
            for (const mesh::pslg::Violation &vi : std::as_const(row.crep.residuals))
            {
                ++rc[int(vi.cause)];
                if (!(vi.lfs > 0.0)) ++zero;
            }
            std::printf("    residual causes: short %d | close %d | angle %d "
                        "| ring %d   (lfs==0: %d of %lld)\n",
                        rc[0], rc[1], rc[2], rc[3], zero,
                        (long long)row.crep.residuals.size());
            for (int k = 0; k < std::min<int>(4, row.crep.residuals.size()); ++k)
            {
                const auto &vi = row.crep.residuals[k];
                std::printf("      worst[%d] %s lfs=%.6g at (%.3f, %.3f) %s %s\n",
                            k, qPrintable(mesh::pslg::violationCauseName(vi.cause)),
                            vi.lfs, vi.xy.x(), vi.xy.y(),
                            qPrintable(vi.tagA), qPrintable(vi.tagB));
            }
        }
        std::printf("    PSLG audit: duplicate seg %d, zero-length %d, "
                    "collinear overlap %d, vertex-on-segment %d\n",
                    au.duplicateSegments, au.zeroLengthSegments,
                    au.collinearOverlaps, au.vertexOnSegment);
        for (int k = 0; k < std::min<int>(3, au.examples.size()); ++k)
            std::printf("      e.g. (%.6f, %.6f)\n",
                        au.examples[k].x(), au.examples[k].y());
    }

    // ── Report file ─────────────────────────────────────────────────────
    const QString outDir = QStringLiteral(SWMMVIS_MINSIZE_OUT);
    QDir().mkpath(outDir);
    const QString stem = QFileInfo(inpPath).completeBaseName();
    QFile rep(outDir + QStringLiteral("/effect_") + stem + QStringLiteral(".txt"));
    if (rep.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream o(&rep);
        o << "Minimum cell size — end-to-end effect\n"
          << "model          " << inpPath << "\n"
          << "PSLG           " << base.pts.size() << " nodes, "
                               << base.segs.size() << " conduits\n"
          << "maxArea        " << maxArea << "\n\n"
          << "unconditioned  " << refS.vertices << " verts, " << refS.cells
          << " cells, minArea " << refS.minArea << ", " << refS.genMs << " ms\n"
          << "               sha256 " << refS.hash << "\n\n";
        for (const Row &r : std::as_const(rows))
        {
            mesh::pslg::MinSizePolicy pol; pol.minCellSize = r.h; pol.resolveDefaults();
            o << "h = " << r.h << "   A_min = " << pol.minTriangleArea() << "\n"
              << "  conditioning " << (r.condOk ? "applied" : "ABANDONED") << "\n"
              << "  " << r.crep.summary() << "\n"
              << "  cleanup " << (r.cleanOk ? "ok" : "ABANDONED a pass") << ": "
              << r.clean.summary() << "\n";
            if (!r.s.ok) { o << "  MESH FAILED: " << r.s.err << "\n\n"; continue; }
            o << "  mesh " << r.s.vertices << " verts, " << r.s.cells
              << " cells, minArea " << r.s.minArea
              << ", cells below A_min " << r.s.belowAmin
              << ", " << r.s.genMs << " ms\n"
              << "  sha256 " << r.s.hash << "\n\n";
        }
        std::printf("\nreport    : %s\n", qPrintable(rep.fileName()));
    }
    return 0;
}
