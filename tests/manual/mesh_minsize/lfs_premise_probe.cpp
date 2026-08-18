/*!
 * \file   lfs_premise_probe.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MIN_CELL_SIZE_TESTING_HANDOFF_2026-08-17.md — Phase 0, the stop/go gate.
 *
 * Tests the premise that pslgminsize.h rests on: that Triangle's output cell
 * size tracks the INPUT's local feature size, so the smallest cells are caused
 * by input geometry rather than by refinement.
 *
 * Method: build the same PSLG shape the mesh-generation worker builds from a
 * real SWMM model (nodes as tagged Steiner points, conduits as tagged
 * constraint polylines, convex hull as the domain), mesh it with minimum cell
 * size OFF, then ask analyseLocalFeatureSize() where the input cannot hold
 * cells of size h and check whether the smallest output triangles are there.
 *
 * Manual build (no CMake target; this is a diagnostic, not a regression test):
 *   see tests/manual/mesh_minsize/README.md
 */
#include "mesh/meshgenerator.h"
#include "mesh/pslgminsize.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QRegularExpression>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

struct Model
{
    QHash<QString, QPointF>            nodeXY;
    QVector<QString>                   nodeOrder;
    QVector<QPair<QString, QString>>   linkEnds;    // link -> (from, to)
    QVector<QString>                   linkOrder;
    QHash<QString, QVector<QPointF>>   linkVerts;
};

/*! Minimal .inp reader — only the sections the PSLG is built from. */
bool readInp(const QString &path, Model *m, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    { *err = QStringLiteral("cannot open %1").arg(path); return false; }

    QTextStream in(&f);
    QString section;
    while (!in.atEnd())
    {
        QString line = in.readLine();
        const int semi = line.indexOf(QLatin1Char(';'));
        if (semi >= 0) line = line.left(semi);
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('[')))
        { section = line.mid(1, line.indexOf(QLatin1Char(']')) - 1).toUpper(); continue; }

        const QStringList tok = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                           Qt::SkipEmptyParts);
        if (section == QLatin1String("COORDINATES"))
        {
            if (tok.size() < 3) continue;
            bool okx = false, oky = false;
            const double x = tok[1].toDouble(&okx), y = tok[2].toDouble(&oky);
            if (!okx || !oky) continue;
            if (!m->nodeXY.contains(tok[0])) m->nodeOrder.append(tok[0]);
            m->nodeXY.insert(tok[0], QPointF(x, y));
        }
        else if (section == QLatin1String("VERTICES"))
        {
            if (tok.size() < 3) continue;
            bool okx = false, oky = false;
            const double x = tok[1].toDouble(&okx), y = tok[2].toDouble(&oky);
            if (!okx || !oky) continue;
            m->linkVerts[tok[0]].append(QPointF(x, y));
        }
        else if (section == QLatin1String("CONDUITS"))
        {
            if (tok.size() < 3) continue;
            m->linkOrder.append(tok[0]);
            m->linkEnds.append(qMakePair(tok[1], tok[2]));
        }
    }
    if (m->nodeXY.isEmpty()) { *err = QStringLiteral("no [COORDINATES]"); return false; }
    return true;
}

/*! Monotone-chain convex hull; the domain stand-in for the boundary layer. */
QPolygonF convexHull(QVector<QPointF> p)
{
    std::sort(p.begin(), p.end(), [](const QPointF &a, const QPointF &b) {
        return a.x() != b.x() ? a.x() < b.x() : a.y() < b.y();
    });
    p.erase(std::unique(p.begin(), p.end()), p.end());
    if (p.size() < 3) return QPolygonF(p);

    auto cross = [](const QPointF &o, const QPointF &a, const QPointF &b) {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    QVector<QPointF> h(2 * p.size());
    int k = 0;
    for (const QPointF &q : std::as_const(p))
    {
        while (k >= 2 && cross(h[k - 2], h[k - 1], q) <= 0) --k;
        h[k++] = q;
    }
    const int lower = k + 1;
    for (int i = p.size() - 2; i >= 0; --i)
    {
        while (k >= lower && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    h.resize(k - 1);
    return QPolygonF(h);
}

/*! Expand a ring outward from its centroid, so no conduit endpoint sits ON the
 *  domain edge (which would be a PSLG violation the worker filters out). */
QPolygonF inflate(const QPolygonF &ring, double f)
{
    QPointF c(0, 0);
    for (const QPointF &q : ring) c += q;
    c /= double(ring.size());
    QPolygonF out;
    out.reserve(ring.size());
    for (const QPointF &q : ring) out.append(c + (q - c) * f);
    return out;
}

double triArea(const QPointF &a, const QPointF &b, const QPointF &c)
{
    return 0.5 * std::abs((b.x() - a.x()) * (c.y() - a.y())
                        - (b.y() - a.y()) * (c.x() - a.x()));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2)
    {
        std::fprintf(stderr,
            "usage: lfs_premise_probe <model.inp> [maxArea] [hOverride]\n");
        return 2;
    }
    const QString inpPath = QString::fromLocal8Bit(argv[1]);
    const double  cliArea = (argc > 2) ? QString::fromLocal8Bit(argv[2]).toDouble() : 0.0;
    const double  cliH    = (argc > 3) ? QString::fromLocal8Bit(argv[3]).toDouble() : 0.0;

    Model  m;
    QString err;
    if (!readInp(inpPath, &m, &err))
    { std::fprintf(stderr, "read failed: %s\n", qPrintable(err)); return 2; }

    // ── Build the PSLG the worker would build ───────────────────────────
    QVector<QPointF> all;
    for (const QString &n : std::as_const(m.nodeOrder)) all.append(m.nodeXY[n]);
    const QPolygonF hull   = inflate(convexHull(all), 1.02);
    const QVector<QPolygonF> domains{hull};

    QVector<mesh::SteinerPoint>      pts;
    QVector<mesh::ConstraintSegment> segs;
    int marker = 1;
    for (const QString &n : std::as_const(m.nodeOrder))
    {
        mesh::SteinerPoint sp;
        sp.xy = m.nodeXY[n]; sp.marker = marker++; sp.tag = n;
        pts.append(sp);
    }
    for (int i = 0; i < m.linkOrder.size(); ++i)
    {
        const QString &id = m.linkOrder[i];
        const auto it0 = m.nodeXY.constFind(m.linkEnds[i].first);
        const auto it1 = m.nodeXY.constFind(m.linkEnds[i].second);
        if (it0 == m.nodeXY.constEnd() || it1 == m.nodeXY.constEnd()) continue;
        QVector<QPointF> path;
        path.append(*it0);
        path += m.linkVerts.value(id);
        path.append(*it1);
        // Dedupe consecutive duplicates, as dedupeSegPath does upstream.
        QVector<QPointF> dedup;
        for (const QPointF &q : std::as_const(path))
            if (dedup.isEmpty() || dedup.last() != q) dedup.append(q);
        if (dedup.size() < 2) continue;
        mesh::ConstraintSegment cs;
        cs.path = dedup; cs.marker = marker++; cs.tag = id;
        segs.append(cs);
    }

    // Domain extent, for a sane default maxArea (~200k cells).
    QRectF bb = hull.boundingRect();
    const double maxArea = cliArea > 0.0 ? cliArea
                                         : (bb.width() * bb.height()) / 200000.0;

    std::printf("model            : %s\n", qPrintable(QFileInfo(inpPath).fileName()));
    std::printf("nodes / conduits : %lld / %lld\n",
                (long long)pts.size(), (long long)segs.size());
    std::printf("domain bbox      : %.1f x %.1f\n", bb.width(), bb.height());
    std::printf("maxArea          : %.6g\n", maxArea);

    // ── Mesh with minimum cell size OFF ────────────────────────────────
    mesh::MeshGenerator g;
    g.setDomains(domains);
    for (const auto &sp : std::as_const(pts))  g.addSteinerPoint(sp);
    for (const auto &cs : std::as_const(segs)) g.addConstraintSegment(cs);
    mesh::GenerationOptions opts;
    opts.maxArea  = maxArea;
    opts.minAngle = 26.0;
    opts.quiet    = true;
    g.setOptions(opts);

    QElapsedTimer clock; clock.start();
    const mesh::MeshResult r = g.generate();
    std::printf("generate         : %s (%lld ms)\n",
                r.ok ? "ok" : qPrintable(QStringLiteral("FAILED: ") + r.errorMsg),
                (long long)clock.elapsed());
    if (!r.ok) return 1;
    std::printf("mesh             : %lld vertices, %lld triangles\n",
                (long long)r.vertices.size(), (long long)r.triangles.size());

    // ── h = median cell EDGE length ────────────────────────────────────
    QVector<double> edgeLens;
    edgeLens.reserve(r.triangles.size() * 3);
    QVector<QPair<double, int>> areas;      // (area, triangle index)
    areas.reserve(r.triangles.size());
    for (int ti = 0; ti < r.triangles.size(); ++ti)
    {
        const mesh::MeshTriangle &t = r.triangles[ti];
        const QPointF &a = r.vertices[t.v0].xy;
        const QPointF &b = r.vertices[t.v1].xy;
        const QPointF &c = r.vertices[t.v2].xy;
        edgeLens.append(std::hypot(b.x() - a.x(), b.y() - a.y()));
        edgeLens.append(std::hypot(c.x() - b.x(), c.y() - b.y()));
        edgeLens.append(std::hypot(a.x() - c.x(), a.y() - c.y()));
        areas.append(qMakePair(triArea(a, b, c), ti));
    }
    std::sort(edgeLens.begin(), edgeLens.end());
    const double medianEdge = edgeLens[edgeLens.size() / 2];
    const double h = cliH > 0.0 ? cliH : medianEdge;
    std::printf("median cell edge : %.6g\nh (probe)        : %.6g\n", medianEdge, h);

    // ── Violations at that h ───────────────────────────────────────────
    clock.restart();
    const QVector<mesh::pslg::Violation> viol =
        mesh::pslg::analyseLocalFeatureSize(domains, {}, segs, pts, h, -1);
    std::printf("violations       : %lld (%lld ms)\n",
                (long long)viol.size(), (long long)clock.elapsed());

    int causeAll[4] = {0, 0, 0, 0};
    for (const auto &v : viol) ++causeAll[int(v.cause)];
    std::printf("cause histogram  : short %d | close %d | angle %d | ring %d\n",
                causeAll[0], causeAll[1], causeAll[2], causeAll[3]);

    // ── The 100 smallest triangles vs. the violations ──────────────────
    const int kSmall = std::min<int>(100, areas.size());
    std::nth_element(areas.begin(), areas.begin() + kSmall, areas.end(),
                     [](const auto &x, const auto &y) { return x.first < y.first; });
    QVector<QPair<double, int>> small = areas.mid(0, kSmall);
    std::sort(small.begin(), small.end(),
              [](const auto &x, const auto &y) { return x.first < y.first; });

    // Uniform grid over the violations at cell size h, so the nearest lookup is
    // O(1) per query instead of O(#violations).
    QHash<QPair<qint32, qint32>, QVector<int>> vgrid;
    const double inv = 1.0 / h;
    auto cellOf = [inv](const QPointF &p) {
        return qMakePair(qint32(std::floor(p.x() * inv)),
                         qint32(std::floor(p.y() * inv)));
    };
    for (int i = 0; i < viol.size(); ++i) vgrid[cellOf(viol[i].xy)].append(i);

    // Search rings outward until a hit or the cutoff (5 h) is passed.
    auto nearestViolation = [&](const QPointF &p, int *causeOut) {
        const auto c = cellOf(p);
        double best = std::numeric_limits<double>::max();
        int    bestCause = -1;
        for (qint32 ring = 0; ring <= 6; ++ring)
        {
            for (qint32 dy = -ring; dy <= ring; ++dy)
                for (qint32 dx = -ring; dx <= ring; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                    auto it = vgrid.constFind(qMakePair(c.first + dx, c.second + dy));
                    if (it == vgrid.constEnd()) continue;
                    for (const int i : *it)
                    {
                        const double d = std::hypot(viol[i].xy.x() - p.x(),
                                                    viol[i].xy.y() - p.y());
                        if (d < best) { best = d; bestCause = int(viol[i].cause); }
                    }
                }
            if (bestCause >= 0 && best <= double(ring) * h) break;
        }
        if (causeOut) *causeOut = bestCause;
        return best;
    };

    int within1 = 0, within3 = 0, within5 = 0;
    int causeMatched[4] = {0, 0, 0, 0};
    QVector<double> ratios;
    for (const auto &pr : std::as_const(small))
    {
        const mesh::MeshTriangle &t = r.triangles[pr.second];
        const QPointF ctr((r.vertices[t.v0].xy.x() + r.vertices[t.v1].xy.x()
                           + r.vertices[t.v2].xy.x()) / 3.0,
                          (r.vertices[t.v0].xy.y() + r.vertices[t.v1].xy.y()
                           + r.vertices[t.v2].xy.y()) / 3.0);
        int cause = -1;
        const double d = nearestViolation(ctr, &cause);
        ratios.append(d / h);
        if (d <= 1.0 * h) ++within1;
        if (d <= 3.0 * h) { ++within3; if (cause >= 0) ++causeMatched[cause]; }
        if (d <= 5.0 * h) ++within5;
    }

    // Control: the SAME question asked of 100 random triangles. Without it a
    // high hit rate proves nothing — the violations may simply blanket the
    // domain, in which case every triangle "coincides" with one.
    int ctrlWithin1 = 0, ctrlWithin3 = 0;
    const int stride = std::max(1, int(areas.size() / 100));
    int ctrlN = 0;
    QVector<double> ctrlRatios;
    for (int ti = 0; ti < r.triangles.size() && ctrlN < 100; ti += stride, ++ctrlN)
    {
        const mesh::MeshTriangle &t = r.triangles[ti];
        const QPointF ctr((r.vertices[t.v0].xy.x() + r.vertices[t.v1].xy.x()
                           + r.vertices[t.v2].xy.x()) / 3.0,
                          (r.vertices[t.v0].xy.y() + r.vertices[t.v1].xy.y()
                           + r.vertices[t.v2].xy.y()) / 3.0);
        const double d = nearestViolation(ctr, nullptr);
        ctrlRatios.append(d / h);
        if (d <= 1.0 * h) ++ctrlWithin1;
        if (d <= 3.0 * h) ++ctrlWithin3;
    }
    auto median = [](QVector<double> v) {
        if (v.isEmpty()) return 0.0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };

    std::printf("\n--- Phase 0 result ---\n");
    std::printf("smallest %d triangles: area %.6g .. %.6g\n",
                kSmall, small.first().first, small.last().first);
    std::printf("  within 1h of a violation : %d / %d\n", within1, kSmall);
    std::printf("  within 3h of a violation : %d / %d   <-- gate\n", within3, kSmall);
    std::printf("  within 5h of a violation : %d / %d\n", within5, kSmall);
    std::printf("  matched cause histogram  : short %d | close %d | angle %d | ring %d\n",
                causeMatched[0], causeMatched[1], causeMatched[2], causeMatched[3]);
    std::printf("CONTROL %d evenly-sampled triangles: within 1h %d, within 3h %d\n",
                ctrlN, ctrlWithin1, ctrlWithin3);
    std::printf("median d/h  smallest %.4g   control %.4g\n",
                median(ratios), median(ctrlRatios));

    // ── Report file ────────────────────────────────────────────────────
    const QString outDir = QStringLiteral(SWMMVIS_MINSIZE_OUT);
    QDir().mkpath(outDir);
    const QString stem = QFileInfo(inpPath).completeBaseName();
    QFile rep(outDir + QStringLiteral("/phase0_") + stem + QStringLiteral(".txt"));
    if (rep.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream o(&rep);
        o << "Phase 0 — local feature size vs. observed small cells\n"
          << "model            " << inpPath << "\n"
          << "nodes/conduits   " << pts.size() << " / " << segs.size() << "\n"
          << "maxArea          " << maxArea << "\n"
          << "mesh             " << r.vertices.size() << " vertices, "
                                 << r.triangles.size() << " triangles\n"
          << "median cell edge " << medianEdge << "\n"
          << "h                " << h << "\n"
          << "violations       " << viol.size() << "\n"
          << "cause histogram  short " << causeAll[0] << " close " << causeAll[1]
          << " angle " << causeAll[2] << " ring " << causeAll[3] << "\n"
          << "within 1h        " << within1 << " / " << kSmall << "\n"
          << "within 3h        " << within3 << " / " << kSmall << "\n"
          << "within 5h        " << within5 << " / " << kSmall << "\n"
          << "matched causes   short " << causeMatched[0] << " close " << causeMatched[1]
          << " angle " << causeMatched[2] << " ring " << causeMatched[3] << "\n"
          << "control within1h " << ctrlWithin1 << " / " << ctrlN << "\n"
          << "control within3h " << ctrlWithin3 << " / " << ctrlN << "\n"
          << "median d/h      smallest " << median(ratios) << "  control " << median(ctrlRatios) << "\n\n"
          << "distance/h for the smallest cells, worst-cell first:\n";
        for (int i = 0; i < ratios.size(); ++i)
            o << "  area " << small[i].first << "   d/h " << ratios[i] << "\n";
        std::printf("report           : %s\n", qPrintable(rep.fileName()));
    }
    return 0;
}
