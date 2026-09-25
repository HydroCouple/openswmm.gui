/*!
 * \file   test_meshquadregion_e2e.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quad redesign gate 8 (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md
 * §3.3, §4, §7.8) — end-to-end through MeshGenerator (links Triangle): a
 * Free rectangle region inside a triangular catchment (≥ 90 % quads, SJ ≥
 * 0.866, convex, triangles-first, no cell straddles the ring), Auto
 * resolving a 4-corner ring to Mapped (exactly 20×10 quads, SJ 1) and an
 * L-ring to Submapped, a region outside the domain skipped with a report
 * line while the mesh still generates, marker-0 terrain Steiners dropped
 * inside a Free ring, and — the "unchanged when absent" gate — two
 * generators with identical inputs and no quad regions (or only a skipped
 * one) produce bitwise-identical meshes.
 *
 * A text report (region reports + computeQuadStats per case) is written to
 * tests/output/quad_redesign_2026-09-06/e2e_report.txt (CLAUDE.md §4.1 —
 * reviewable location; override the directory with SWMMVIS_QUAD_E2E_OUT).
 */
#include <QtTest>
#include <QPolygonF>
#include <QSet>
#include <QString>
#include <QTransform>
#include <QVector>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "mesh/meshcellgeom.h"
#include "mesh/meshcellstats.h"
#include "mesh/meshgenerator.h"
#include "mesh/meshquadquality.h"
#include "mesh/meshquadregion.h"
#include "mesh/meshresult.h"

using namespace mesh;

namespace {

/*! 300×150 outline with a vertex every 10 units (Mapped / Submapped regions
 *  make the generator emit 'Y', which forbids Steiner points on the outline —
 *  the same densification the dialog performs). */
QPolygonF domain300x150()
{
    QPolygonF d;
    for (int i = 0; i < 30; ++i) d << QPointF(10 * i, 0);
    for (int i = 0; i < 15; ++i) d << QPointF(300, 10 * i);
    for (int i = 0; i < 30; ++i) d << QPointF(300 - 10 * i, 150);
    for (int i = 0; i < 15; ++i) d << QPointF(0, 150 - 10 * i);
    return d;
}

QPolygonF rect(double x0, double y0, double w, double h)
{
    return QPolygonF{QPointF(x0, y0), QPointF(x0 + w, y0), QPointF(x0 + w, y0 + h), QPointF(x0, y0 + h)};
}

GenerationOptions baseOptions()
{
    GenerationOptions o;
    o.maxArea  = 60.0;
    o.minAngle = 26.0;
    return o;
}

/*! On some ring edge within \p tol. */
bool onRingBoundary(const QPolygonF &ring, const QPointF &p, double tol)
{
    const int n = ring.size();
    for (int i = 0; i < n; ++i)
    {
        const QPointF a = ring[i], b = ring[(i + 1) % n], ab = b - a, ap = p - a;
        const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
        if (l2 <= 0.0) continue;
        const double t = std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / l2, 0.0, 1.0);
        const QPointF q = a + ab * t;
        if (std::hypot(p.x() - q.x(), p.y() - q.y()) <= tol) return true;
    }
    return false;
}

/*! Inside or on the ring within \p tol. */
bool insideOrOn(const QPolygonF &ring, const QPointF &p, double tol)
{
    return pointInRing(ring, p) || onRingBoundary(ring, p, tol);
}

/*! Strictly inside: inside and not within \p tol of an edge. */
bool strictlyInside(const QPolygonF &ring, const QPointF &p, double tol)
{
    return pointInRing(ring, p) && !onRingBoundary(ring, p, tol);
}

const char *modeName(QuadRegionMode m)
{
    switch (m)
    {
    case QuadRegionMode::Auto:          return "Auto";
    case QuadRegionMode::Mapped:        return "Mapped";
    case QuadRegionMode::Submapped:     return "Submapped";
    case QuadRegionMode::Free:          return "Free";
    case QuadRegionMode::TrianglesOnly: return "TrianglesOnly";
    }
    return "?";
}

std::string narrow(const QString &s) { return s.toStdString(); }

/*! Reviewable output directory (CLAUDE.md §4.1). */
std::filesystem::path outDir()
{
    const char *env = std::getenv("SWMMVIS_QUAD_E2E_OUT");
    std::filesystem::path d = (env && *env) ? std::filesystem::path(env)
                                            : std::filesystem::path("tests/output/quad_redesign_2026-09-06");
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
}

void appendReport(std::ofstream &out, const QString &title, const MeshGenerator &g, const MeshResult &r)
{
    out << "== " << narrow(title) << " ==\n";
    out << "ok=" << (r.ok ? 1 : 0) << " vertices=" << r.vertices.size() << " cells=" << r.triangles.size()
        << " quads=" << r.quadCount() << "\n";
    for (const QuadRegionReport &rep : g.quadRegionReports())
    {
        out << "  [Mesh][quad] region " << rep.index << ": requested=" << modeName(rep.requested)
            << " resolved=" << modeName(rep.resolved) << " h=" << rep.spacing
            << " quads=" << rep.quads << " triangles=" << rep.triangles
            << " template=" << rep.templateQuads << " gap=" << rep.gapQuads
            << " points=" << rep.generatedPoints << " droppedSteiners=" << rep.droppedSteiners
            << " doublets=" << rep.doubletsRemoved << " swaps=" << rep.diagonalSwaps
            << " moved=" << rep.verticesMoved << " minSJ=" << rep.minScaledJacobian
            << " medianRect=" << rep.medianRectangularity
            << (rep.message.isEmpty() ? std::string() : "  msg=\"" + narrow(rep.message) + "\"") << "\n";
    }
    const QuadStats qs = computeQuadStats(r);
    out << "  QuadStats: count=" << qs.count << " minAngle=" << qs.minAngleDeg << " maxAngle=" << qs.maxAngleDeg
        << " minSJ=" << qs.minScaledJacobian << " medianRect=" << qs.medianRectangularity
        << " maxAspect=" << qs.maxAspect << " nonConvex=" << qs.nonConvex
        << " irregularVertices=" << qs.irregularVertices << " sjHist=[";
    for (int b = 0; b < 10; ++b) out << qs.sjHistogram[b] << (b < 9 ? "," : "");
    out << "]\n";
}

/*! Engine order + convexity + valid indices over the whole result. */
#define CHECK_CELL_INVARIANTS(r)                                                        \
    do {                                                                                \
        bool seenQuad = false;                                                          \
        for (const MeshTriangle &c : (r).triangles)                                     \
        {                                                                               \
            if (c.isQuad()) seenQuad = true; else QVERIFY(!seenQuad);                   \
            for (int k = 0; k < c.vertexCount(); ++k)                                   \
                QVERIFY(c.vertex(k) >= 0 && c.vertex(k) < (r).vertices.size());         \
            if (c.isQuad()) QVERIFY(cellIsConvex((r).vertices, c));                     \
            QVERIFY(cellSignedArea((r).vertices, c) > 0.0);                             \
        }                                                                               \
    } while (0)

} // namespace

class TestMeshQuadRegionE2E : public QObject
{
    Q_OBJECT

    std::ofstream m_report;

private slots:
    void deferredMergeLocks_excludeHolesAndSkippedRegions()
    {
        MeshGenerator g;
        g.setDomain(domain300x150());
        g.setOptions(baseOptions());
        QuadRegion bg;
        bg.ring = domain300x150();
        bg.isBackground = true;
        bg.mode = QuadRegionMode::TrianglesOnly;
        bg.spacing = 5;
        bg.holes.append(rect(100, 50, 100, 50));
        g.addQuadRegion(bg);
        QuadRegion invalid;
        invalid.ring = rect(110, 60, 40, 20);
        invalid.spacing = 5;
        invalid.aspectMax = 0.5;
        g.addQuadRegion(invalid);
        QVERIFY(g.generate().ok);
        QVERIFY(g.quadRegionReports()[0].accepted);
        QVERIFY(!g.quadRegionReports()[1].accepted);
        // Probe current geometry independently of generated cell indices:
        // accepted interior, background hole / skipped region, outside.
        MeshResult probe;
        for (const QPointF p : {QPointF(20, 20), QPointF(120, 70), QPointF(400, 200)})
        {
            const int v = probe.vertices.size();
            for (const QPointF d : {QPointF(0, 0), QPointF(1, 0), QPointF(0, 1)})
            {
                MeshVertex vertex; vertex.xy = p + d;
                probe.vertices.append(vertex);
            }
            MeshTriangle t; t.v0 = v; t.v1 = v + 1; t.v2 = v + 2;
            probe.triangles.append(t);
        }
        const auto locks = g.quadRegionMergeLocks(probe);
        QCOMPARE(locks.size(), 3);
        QVERIFY(locks.contains(edgeKey(0, 1)));
        QVERIFY(locks.contains(edgeKey(1, 2)));
        QVERIFY(locks.contains(edgeKey(2, 0)));
    }

    void regionAspectLimits_data()
    {
        QTest::addColumn<int>("mergePath");
        QTest::newRow("region-pairing") << 0;
        QTest::newRow("generator-final-merge") << 1;
        QTest::newRow("deferred-gui-merge") << 2;
    }

    void regionAspectLimits()
    {
        QFETCH(int, mergePath);
        MeshGenerator g;
        g.setDomain(domain300x150());
        auto o = baseOptions();
        o.mergeTrianglePairs = mergePath == 1;
        o.quadRegionBounds.maxAspect = 1.01;
        g.setOptions(o);
        QVector<QuadRegion> regions;
        // Distinct neighboring regions exercise strict inheritance, explicit
        // caps and unlimited pairing in the same run.
        const double caps[] = {-1.0, 2.0, 4.0, 0.0};
        for (int i = 0; i < 4; ++i)
        {
            QuadRegion q;
            q.ring = rect(10 + 70 * i, 30, 60, 90);
            q.mode = QuadRegionMode::Free;
            q.spacing = 7;
            q.hasAlignAngle = true;
            q.alignAngleDeg = 17;
            q.aspectMax = caps[i];
            regions.append(q);
            g.addQuadRegion(q);
        }
        QuadRegion triangles;
        triangles.ring = rect(100, 5, 60, 20);
        triangles.mode = QuadRegionMode::TrianglesOnly;
        triangles.spacing = 5;
        g.addQuadRegion(triangles);
        auto r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        if (mergePath == 2)
        {
            auto locked = g.quadRegionMergeLocks(r);
            for (const auto &edge : r.boundaryEdges) locked.insert(edgeKey(edge.v0, edge.v1));
            mergeTrianglePairs(r, o.quadMerge, locked, nullptr);
        }
        CHECK_CELL_INVARIANTS(r);
        int counts[4] = {}, aboveGlobal[4] = {}, outsideQuads = 0;
        for (const auto &c : r.triangles)
        {
            const QPointF center = cellGeom(r.vertices, c).centroid;
            if (pointInRing(triangles.ring, center)) QVERIFY(!c.isQuad());
            if (!c.isQuad()) continue;
            bool inRegion = false;
            for (int i = 0; i < regions.size(); ++i)
                if (pointInRing(regions[i].ring, center))
                {
                    inRegion = true;
                    ++counts[i];
                    const double aspect = quadQuality(r.vertices, c).aspect;
                    const double cap = caps[i] < 0 ? o.quadRegionBounds.maxAspect : caps[i];
                    QVERIFY2(cap == 0 || aspect <= cap + 1e-10,
                             qPrintable(QString("region %1: aspect %2 exceeds %3").arg(i).arg(aspect).arg(cap)));
                    if (aspect > o.quadRegionBounds.maxAspect + 1e-10) ++aboveGlobal[i];
                }
            if (!inRegion) ++outsideQuads;
        }
        for (int i = 0; i < 4; ++i) QVERIFY(counts[i] > 0);
        for (int i = 1; i < 4; ++i) QVERIFY(aboveGlobal[i] > 0);
        if (mergePath) QVERIFY(outsideQuads > 0);
        appendReport(m_report, QStringLiteral("aspect limits, merge=%1").arg(mergePath), g, r);
    }


    void constraintCrossing_forcesFree_data()
    {
        QTest::addColumn<QPointF>("a");
        QTest::addColumn<QPointF>("b");
        QTest::newRow("horizontal") << QPointF(70, 73) << QPointF(230, 73);
        QTest::newRow("vertical") << QPointF(147, 20) << QPointF(147, 130);
        QTest::newRow("thirty-degrees") << QPointF(80, 35) << QPointF(220, 115.8290376865);
        QTest::newRow("horizontal-interior") << QPointF(120, 73) << QPointF(180, 73);
        QTest::newRow("vertical-interior") << QPointF(147, 60) << QPointF(147, 90);
    }

    /*! W0b/M10: both outside endpoints and wholly interior paths must
     *  prevent a mapped patch from swallowing a road/river constraint. */
    void constraintCrossing_forcesFree()
    {
        QFETCH(QPointF, a);
        QFETCH(QPointF, b);
        MeshGenerator g;
        g.setDomain(domain300x150());
        g.setOptions(baseOptions());
        g.addConstraintSegment({{a, b}, 731, QStringLiteral("corridor")});
        QuadRegion qr;
        qr.ring = rect(100, 50, 100, 50);
        qr.spacing = 5.0;
        g.addQuadRegion(qr);
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        appendReport(m_report, QString::fromLatin1(QTest::currentDataTag()), g, r);
        QCOMPARE(g.quadRegionReports().size(), 1);
        QCOMPARE(g.quadRegionReports().first().resolved, QuadRegionMode::Free);
        QVERIFY(g.quadRegionReports().first().message.contains(QStringLiteral("constraint segment crosses")));
        CHECK_CELL_INVARIANTS(r);

        // The full constrained path survives as marked mesh edges, including
        // any splits at the region boundary and at generated vertices.
        QSet<QPair<int, int>> cellEdges;
        for (const MeshTriangle &cell : r.triangles)
            for (int k = 0; k < cell.vertexCount(); ++k)
            {
                const int v0 = cell.vertex(k), v1 = cell.vertex((k + 1) % cell.vertexCount());
                cellEdges.insert(qMakePair(std::min(v0, v1), std::max(v0, v1)));
            }
        double length = 0.0;
        const QPointF direction = b - a;
        const double expectedLength = std::hypot(direction.x(), direction.y());
        for (const MeshEdge &e : r.boundaryEdges)
        {
            if (e.marker != 731) continue;
            QVERIFY(cellEdges.contains(qMakePair(std::min(e.v0, e.v1), std::max(e.v0, e.v1))));
            const QPointF p = r.vertices[e.v0].xy, q = r.vertices[e.v1].xy;
            for (const QPointF &v : {p, q})
            {
                const QPointF d = v - a;
                const double distance = std::abs(direction.x() * d.y() - direction.y() * d.x()) / expectedLength;
                QVERIFY2(distance < 1e-6, "cleanup moved a constrained vertex away from the corridor");
            }
            length += std::hypot(q.x() - p.x(), q.y() - p.y());
        }
        QVERIFY2(std::abs(length - expectedLength) < 1e-6,
                 qPrintable(QStringLiteral("marked length %1, expected %2")
                                .arg(length, 0, 'g', 16).arg(expectedLength, 0, 'g', 16)));
    }

    void constraintAlignment_matchesExplicitGuide_data()
    {
        QTest::addColumn<QPointF>("a");
        QTest::addColumn<QPointF>("b");
        QTest::newRow("horizontal") << QPointF(130, 75) << QPointF(170, 75);
        QTest::newRow("vertical") << QPointF(150, 60) << QPointF(150, 90);
    }

    void exteriorConstraint_keepsMapped_data()
    {
        QTest::addColumn<QPointF>("a");
        QTest::addColumn<QPointF>("b");
        QTest::newRow("horizontal-disjoint") << QPointF(70, 40) << QPointF(230, 40);
        QTest::newRow("vertical-disjoint") << QPointF(90, 20) << QPointF(90, 130);
        QTest::newRow("endpoint-touch") << QPointF(70, 50) << QPointF(100, 50);
        QTest::newRow("bbox-overlap-only") << QPointF(70, 60) << QPointF(110, 20);
    }

    void exteriorConstraint_keepsMapped()
    {
        QFETCH(QPointF, a);
        QFETCH(QPointF, b);
        MeshGenerator g;
        g.setDomain(domain300x150());
        g.setOptions(baseOptions());
        g.addConstraintSegment({{a, b}, 731, QStringLiteral("exterior")});
        QuadRegion qr;
        qr.ring = rect(100, 50, 100, 50);
        qr.spacing = 5.0;
        g.addQuadRegion(qr);
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QCOMPARE(g.quadRegionReports().size(), 1);
        QCOMPARE(g.quadRegionReports().first().resolved, QuadRegionMode::Mapped);
        QCOMPARE(g.quadRegionReports().first().quads, 200);
        CHECK_CELL_INVARIANTS(r);
    }

    /*! W0b/M10: an interior axis-aligned constraint supplies the same
     *  orientation as an explicit guide. The ring is rotated so its own
     *  directions cannot hide a missing horizontal/vertical guide. */
    void constraintAlignment_matchesExplicitGuide()
    {
        QFETCH(QPointF, a);
        QFETCH(QPointF, b);
        auto generate = [&](bool explicitGuide) {
            MeshGenerator g;
            g.setDomain(domain300x150());
            g.setOptions(baseOptions());
            g.addConstraintSegment({{a, b}, 731, QStringLiteral("corridor")});
            QuadRegion qr;
            QTransform t;
            t.translate(150, 75);
            t.rotate(20);
            qr.ring = t.map(rect(-60, -40, 120, 80));
            qr.mode = QuadRegionMode::Free;
            qr.spacing = 5.0;
            if (explicitGuide) qr.alignGuide = {a, b};
            g.addQuadRegion(qr);
            return g.generate();
        };
        const MeshResult automatic = generate(false), explicitGuide = generate(true);
        QVERIFY2(automatic.ok, qPrintable(automatic.errorMsg));
        QVERIFY2(explicitGuide.ok, qPrintable(explicitGuide.errorMsg));
        CHECK_CELL_INVARIANTS(automatic);
        CHECK_CELL_INVARIANTS(explicitGuide);
        QCOMPARE(automatic.vertices.size(), explicitGuide.vertices.size());
        QCOMPARE(automatic.triangles.size(), explicitGuide.triangles.size());
        for (int i = 0; i < automatic.vertices.size(); ++i)
        {
            const QPointF d = automatic.vertices[i].xy - explicitGuide.vertices[i].xy;
            QVERIFY(std::hypot(d.x(), d.y()) < 1e-7);
        }
        for (int i = 0; i < automatic.triangles.size(); ++i)
            for (int k = 0; k < 4; ++k)
                QCOMPARE(automatic.triangles[i].vertex(k), explicitGuide.triangles[i].vertex(k));
    }

    void initTestCase()
    {
        m_report.open(outDir() / "e2e_report.txt", std::ios::out | std::ios::trunc);
        QVERIFY(m_report.is_open());
        m_report << "Quad redesign gate 8 — MeshGenerator quad regions end-to-end\n"
                    "(workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §7.8)\n\n";
    }

    void cleanupTestCase() { m_report.close(); }

    /*! (g) QUAD_EVERYWHERE_PLAN_2026-09-07.md Q2 — quads over the WHOLE domain
     *  from a background region: no ring picked by the user, the domain outline
     *  IS the region. The ring must not be re-emitted as constraint segments
     *  (that would double every boundary edge), and the mesh must come out
     *  quad-dominant with the engine cell contract intact. */
    void backgroundRegion_wholeDomain()
    {
        MeshGenerator g;
        g.setDomain(domain300x150());
        QuadRegion bg;
        bg.ring         = domain300x150();
        bg.isBackground = true;
        bg.mode         = QuadRegionMode::Free;
        bg.spacing      = 6.0;
        bg.tag          = QStringLiteral("bg");
        g.addQuadRegion(bg);
        g.setOptions(baseOptions());
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        appendReport(m_report, QStringLiteral("(g) background region, whole domain h=6"), g, r);
        CHECK_CELL_INVARIANTS(r);

        const QuadRegionReport &rep = g.quadRegionReports().first();
        QCOMPARE(rep.resolved, QuadRegionMode::Free);
        QVERIFY2(rep.quads > 900, qPrintable(QStringLiteral("quads %1").arg(rep.quads)));

        int quads = 0;
        for (const MeshTriangle &t : r.triangles) if (t.isQuad()) ++quads;
        const double frac = double(quads) / double(std::max<qsizetype>(1, r.triangles.size()));
        QVERIFY2(frac >= 0.90, qPrintable(QStringLiteral("quad fraction %1").arg(frac)));

        const QuadStats qs = computeQuadStats(r);
        QCOMPARE(qs.nonConvex, 0);
        QVERIFY(qs.minScaledJacobian >= 0.866);
        QVERIFY(qs.medianRectangularity >= 0.85);

        // The domain boundary must appear exactly once: a background ring is
        // already in the PSLG, so re-emitting it would duplicate these edges.
        QSet<QPair<int, int>> seen;
        for (const MeshEdge &e : r.boundaryEdges)
        {
            const QPair<int, int> k = e.v0 < e.v1 ? qMakePair(e.v0, e.v1) : qMakePair(e.v1, e.v0);
            QVERIFY2(!seen.contains(k), "duplicate boundary edge from the background ring");
            seen.insert(k);
        }
    }

    /*! (h) Q2 — a hole inside the background region stays unmeshed: no cell
     *  centroid may fall in it, and the lattice keeps its clearance from the
     *  hole ring just as it does from the outer ring. */
    void backgroundRegion_respectsHole()
    {
        const QPolygonF hole = rect(120, 60, 60, 40);
        MeshGenerator g;
        g.setDomain(domain300x150());
        ConstraintSegment cs;
        for (const QPointF &p : hole) cs.path << p;
        cs.path << hole.first();
        cs.marker = 900;
        cs.tag    = QStringLiteral("hole");
        g.addConstraintSegment(cs);
        g.addHole(QPointF(150, 80));

        QuadRegion bg;
        bg.ring         = domain300x150();
        bg.holes        = {hole};
        bg.isBackground = true;
        bg.mode         = QuadRegionMode::Free;
        bg.spacing      = 6.0;
        g.addQuadRegion(bg);
        g.setOptions(baseOptions());
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        appendReport(m_report, QStringLiteral("(h) background region with a hole"), g, r);
        CHECK_CELL_INVARIANTS(r);

        for (const MeshTriangle &t : r.triangles)
            QVERIFY2(!pointInRing(hole, cellGeom(r.vertices, t).centroid),
                     "a cell was generated inside the hole");

        int quads = 0;
        for (const MeshTriangle &t : r.triangles) if (t.isQuad()) ++quads;
        QVERIFY(double(quads) / double(std::max<qsizetype>(1, r.triangles.size())) >= 0.85);
        QCOMPARE(computeQuadStats(r).nonConvex, 0);
    }

    /*! (i) Q1+Q2 — with no explicit spacing the background lattice follows the
     *  size function point by point instead of holding one centroid sample.
     *  h ramps 3 → ~16 across the domain, so the quads must coarsen with it. */
    void backgroundRegion_gradedBySizeFunction()
    {
        MeshGenerator g;
        g.setDomain(domain300x150());
        QuadRegion bg;
        bg.ring         = domain300x150();
        bg.isBackground = true;
        bg.mode         = QuadRegionMode::Free;
        g.addQuadRegion(bg);                       // no spacing → grade from the field
        g.setOptions(baseOptions());
        RefineHook hook;
        hook.targetAreaAt = [](double x, double) {
            const double h = 3.0 + 0.05 * std::abs(x - 40.0);
            return 0.5 * h * h;                     // h = sqrt(2A)
        };
        g.setRefineHook(hook);
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        appendReport(m_report, QStringLiteral("(i) background region graded by the size function"), g, r);
        CHECK_CELL_INVARIANTS(r);

        auto meanEdge = [&](double x0, double x1) {
            double s = 0.0; int n = 0;
            for (const MeshTriangle &t : r.triangles)
            {
                if (!t.isQuad()) continue;
                const CellGeom cg = cellGeom(r.vertices, t);
                if (cg.centroid.x() < x0 || cg.centroid.x() >= x1) continue;
                s += std::sqrt(cg.area); ++n;
            }
            return n ? s / n : 0.0;
        };
        const double near = meanEdge(20, 60), far = meanEdge(240, 300);
        QVERIFY2(near > 0.0 && far > 0.0,
                 qPrintable(QStringLiteral("near=%1 far=%2 — expected quads at both ends").arg(near).arg(far)));
        QVERIFY2(far > 2.0 * near,
                 qPrintable(QStringLiteral("lattice did not follow the field: near=%1 far=%2")
                                .arg(near).arg(far)));
        QCOMPARE(computeQuadStats(r).nonConvex, 0);
        QVERIFY(computeQuadStats(r).minScaledJacobian >= 0.866);
    }

    /*! (a) Free rectangle 100–200 × 50–100, h = 5, in the 300×150 domain. */
    void freeRegion_rectangle()
    {
        const QPolygonF ring = rect(100, 50, 100, 50);
        MeshGenerator g;
        g.setDomain(domain300x150());
        QuadRegion qr;
        qr.ring = ring;
        qr.mode = QuadRegionMode::Free;
        qr.spacing = 5.0;
        qr.tag = QStringLiteral("free");
        g.addQuadRegion(qr);
        g.setOptions(baseOptions());
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        appendReport(m_report, QStringLiteral("(a) Free rectangle h=5"), g, r);

        QCOMPARE(g.quadRegionReports().size(), 1);
        const QuadRegionReport &rep = g.quadRegionReports().first();
        QCOMPARE(rep.index, 0);
        QCOMPARE(rep.requested, QuadRegionMode::Free);
        QCOMPARE(rep.resolved, QuadRegionMode::Free);
        QVERIFY2(rep.message.isEmpty(), qPrintable(rep.message));
        QCOMPARE(rep.spacing, 5.0);
        QVERIFY(rep.quads > 0);
        QVERIFY2(rep.quads >= 0.9 * (rep.quads + rep.triangles),
                 qPrintable(QStringLiteral("quads %1 triangles %2").arg(rep.quads).arg(rep.triangles)));
        QVERIFY2(rep.minScaledJacobian >= 0.866, qPrintable(QStringLiteral("min SJ %1").arg(rep.minScaledJacobian)));
        QVERIFY(rep.medianRectangularity >= 0.85);
        QVERIFY2(std::abs(rep.generatedPoints - 171) <= 0.15 * 171,
                 qPrintable(QStringLiteral("generated points %1").arg(rep.generatedPoints)));
        QVERIFY(rep.templateQuads + rep.gapQuads >= rep.quads - rep.doubletsRemoved);
        QCOMPARE(rep.droppedSteiners, 0);

        CHECK_CELL_INVARIANTS(r);
        QCOMPARE(r.quadCount(), rep.quads);   // no quads outside the region (merge is off)

        // Every quad's vertices are inside or on the ring; no cell straddles
        // the ring (centroid inside ⇒ every vertex inside-or-on); cells
        // inside carry the region tag.
        int insideCells = 0, insideQuads = 0;
        for (const MeshTriangle &c : r.triangles)
        {
            const bool inside = pointInRing(ring, cellGeom(r.vertices, c).centroid);
            if (c.isQuad())
            {
                for (int k = 0; k < 4; ++k) QVERIFY(insideOrOn(ring, r.vertices[c.vertex(k)].xy, 1e-6));
                QVERIFY(inside);
                QVERIFY(quadQuality(r.vertices, c).scaledJacobian >= 0.866);
            }
            if (inside)
            {
                ++insideCells;
                if (c.isQuad()) ++insideQuads;
                for (int k = 0; k < c.vertexCount(); ++k)
                    QVERIFY2(insideOrOn(ring, r.vertices[c.vertex(k)].xy, 1e-6),
                             qPrintable(QStringLiteral("cell straddles the ring at (%1,%2)")
                                            .arg(r.vertices[c.vertex(k)].xy.x()).arg(r.vertices[c.vertex(k)].xy.y())));
                QCOMPARE(c.tag, QStringLiteral("free"));
            }
            else
            {
                QVERIFY(!c.isQuad());
                // Triangles outside must not reach into the region.
                for (int k = 0; k < 3; ++k)
                    QVERIFY(!strictlyInside(ring, r.vertices[c.vertex(k)].xy, 1e-6));
            }
        }
        QCOMPARE(insideCells, rep.quads + rep.triangles);
        QCOMPARE(insideQuads, rep.quads);
        QVERIFY(r.triangles.size() > insideCells);   // the catchment outside is meshed too

        // Ring vertices (resampled at h) are mesh vertices, shared by a
        // triangle outside and a cell inside.
        const QPolygonF ringR = resampleRing(ring, 5.0);
        QCOMPARE(ringR.size(), 60);
        for (const QPointF &p : ringR)
        {
            int vi = -1;
            for (int i = 0; i < r.vertices.size() && vi < 0; ++i)
                if (std::hypot(r.vertices[i].xy.x() - p.x(), r.vertices[i].xy.y() - p.y()) < 1e-6) vi = i;
            QVERIFY2(vi >= 0, qPrintable(QStringLiteral("ring vertex (%1,%2) missing").arg(p.x()).arg(p.y())));
        }

        // Stats agree with the report for a single region.
        const QuadStats qs = computeQuadStats(r);
        QCOMPARE(qs.count, rep.quads);
        QCOMPARE(qs.nonConvex, 0);
        QVERIFY(std::abs(qs.minScaledJacobian - rep.minScaledJacobian) < 1e-12);
    }

    /*! (b) Auto on the same 4-corner ring → Mapped, exactly 20×10 = 200
     *  quads, all SJ 1, no triangles inside. */
    void autoRegion_rectangle_mapped()
    {
        const QPolygonF ring = rect(100, 50, 100, 50);
        MeshGenerator g;
        g.setDomain(domain300x150());
        QuadRegion qr;
        qr.ring = ring;
        qr.mode = QuadRegionMode::Auto;
        qr.spacing = 5.0;
        qr.tag = QStringLiteral("map");
        g.addQuadRegion(qr);
        g.setOptions(baseOptions());
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        appendReport(m_report, QStringLiteral("(b) Auto rectangle -> Mapped"), g, r);

        QCOMPARE(g.quadRegionReports().size(), 1);
        const QuadRegionReport &rep = g.quadRegionReports().first();
        QCOMPARE(rep.requested, QuadRegionMode::Auto);
        QCOMPARE(rep.resolved, QuadRegionMode::Mapped);
        QVERIFY2(rep.message.isEmpty(), qPrintable(rep.message));
        QCOMPARE(rep.quads, 200);
        QCOMPARE(rep.triangles, 0);
        QVERIFY(rep.minScaledJacobian > 1.0 - 1e-9);
        QCOMPARE(r.quadCount(), 200);
        CHECK_CELL_INVARIANTS(r);
        for (const MeshTriangle &c : r.triangles)
        {
            if (!c.isQuad())
            {
                QVERIFY(!pointInRing(ring, cellGeom(r.vertices, c).centroid));
                continue;
            }
            QVERIFY(quadQuality(r.vertices, c).scaledJacobian > 1.0 - 1e-9);
            QVERIFY(std::abs(cellGeom(r.vertices, c).area - 25.0) < 1e-6);
            QCOMPARE(c.tag, QStringLiteral("map"));
        }
        // The 60 ring vertices exist exactly once (patch stitched, no duplicates).
        for (const QPointF &p : resampleRing(ring, 5.0))
        {
            int n = 0;
            for (const MeshVertex &v : r.vertices)
                if (std::hypot(v.xy.x() - p.x(), v.xy.y() - p.y()) < 1e-6) ++n;
            QCOMPARE(n, 1);
        }
    }

    /*! (c) Auto on an L-shaped ring → Submapped, all rectangles, no
     *  triangles inside. */
    void autoRegion_lShape_submapped()
    {
        const QPolygonF ring{QPointF(100, 50), QPointF(200, 50), QPointF(200, 80), QPointF(150, 80),
                             QPointF(150, 110), QPointF(100, 110)};
        MeshGenerator g;
        g.setDomain(domain300x150());
        QuadRegion qr;
        qr.ring = ring;
        qr.mode = QuadRegionMode::Auto;
        qr.spacing = 5.0;
        g.addQuadRegion(qr);
        g.setOptions(baseOptions());
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        appendReport(m_report, QStringLiteral("(c) Auto L-shape -> Submapped"), g, r);

        const QuadRegionReport &rep = g.quadRegionReports().first();
        QCOMPARE(rep.resolved, QuadRegionMode::Submapped);
        QVERIFY2(rep.message.isEmpty(), qPrintable(rep.message));
        // 100×30 + 50×30 at h = 5 → 20·6 + 10·6 = 180 cells of 5×5.
        QCOMPARE(rep.quads, 180);
        QCOMPARE(rep.triangles, 0);
        QVERIFY(rep.minScaledJacobian > 1.0 - 1e-9);
        QCOMPARE(r.quadCount(), 180);
        CHECK_CELL_INVARIANTS(r);
    }

    /*! (d) A region outside the domain is skipped ("skipped: …" in the
     *  report) and the mesh still generates — identical to a run without
     *  any region. */
    void regionOutsideDomain_skipped()
    {
        MeshGenerator g;
        g.setDomain(domain300x150());
        QuadRegion qr;
        qr.ring = rect(400, 400, 50, 50);
        qr.mode = QuadRegionMode::Free;
        qr.spacing = 5.0;
        g.addQuadRegion(qr);
        g.setOptions(baseOptions());
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        appendReport(m_report, QStringLiteral("(d) region outside the domain"), g, r);

        QCOMPARE(g.quadRegionReports().size(), 1);
        const QuadRegionReport &rep = g.quadRegionReports().first();
        QVERIFY2(rep.message.startsWith(QStringLiteral("skipped:")), qPrintable(rep.message));
        QCOMPARE(rep.quads, 0);
        QCOMPARE(rep.triangles, 0);
        QCOMPARE(r.quadCount(), 0);
        QVERIFY(r.triangles.size() > 100);
        CHECK_CELL_INVARIANTS(r);

        MeshGenerator plain;
        plain.setDomain(domain300x150());
        plain.setOptions(baseOptions());
        const MeshResult p = plain.generate();
        QVERIFY2(p.ok, qPrintable(p.errorMsg));
        QCOMPARE(r.vertices.size(), p.vertices.size());
        QCOMPARE(r.triangles.size(), p.triangles.size());
        for (int i = 0; i < r.vertices.size(); ++i) QVERIFY(r.vertices[i].xy == p.vertices[i].xy);
        for (int i = 0; i < r.triangles.size(); ++i)
        {
            QCOMPARE(r.triangles[i].v0, p.triangles[i].v0);
            QCOMPARE(r.triangles[i].v1, p.triangles[i].v1);
            QCOMPARE(r.triangles[i].v2, p.triangles[i].v2);
            QCOMPARE(r.triangles[i].v3, p.triangles[i].v3);
        }

        // A region straddling the domain edge and a too-small one are skipped too.
        MeshGenerator g2;
        g2.setDomain(domain300x150());
        QuadRegion edge; edge.ring = rect(280, 50, 50, 50); edge.spacing = 5.0;
        QuadRegion tiny; tiny.ring = rect(10, 10, 5, 5); tiny.spacing = 5.0;   // area 25 < 4·25
        QuadRegion noH;  noH.ring = rect(20, 100, 40, 30);                   // spacing 0, no default
        g2.addQuadRegion(edge);
        g2.addQuadRegion(tiny);
        g2.addQuadRegion(noH);
        g2.setOptions(baseOptions());
        const MeshResult r2 = g2.generate();
        QVERIFY2(r2.ok, qPrintable(r2.errorMsg));
        appendReport(m_report, QStringLiteral("(d') three invalid regions"), g2, r2);
        QCOMPARE(g2.quadRegionReports().size(), 3);
        for (const QuadRegionReport &rr : g2.quadRegionReports())
            QVERIFY2(rr.message.startsWith(QStringLiteral("skipped:")), qPrintable(rr.message));
        QCOMPARE(r2.quadCount(), 0);
    }

    /*! (e) A marker-0 terrain cloud (4 m grid) inside the Free ring is
     *  dropped (plan D5: the lattice replaces it); marker-0 points outside
     *  the ring survive; the region meshes exactly like (a). */
    void terrainSteiners_droppedInsideFreeRing()
    {
        const QPolygonF ring = rect(100, 50, 100, 50);
        MeshGenerator g;
        g.setDomain(domain300x150());
        int insideCloud = 0;
        for (double x = 104; x <= 196; x += 4.0)
            for (double y = 54; y <= 96; y += 4.0)
            {
                SteinerPoint sp;
                sp.xy = QPointF(x, y);
                sp.marker = 0;
                sp.z = 0.01 * x;
                sp.hasZ = true;
                QVERIFY(pointInRing(ring, sp.xy));
                g.addSteinerPoint(sp);
                ++insideCloud;
            }
        QCOMPARE(insideCloud, 24 * 11);
        const QVector<QPointF> outsideAux{QPointF(33.3, 133.3), QPointF(250.7, 20.2), QPointF(150.0, 130.0)};
        for (const QPointF &p : outsideAux)
        {
            SteinerPoint sp;
            sp.xy = p;
            sp.marker = 0;
            g.addSteinerPoint(sp);
        }

        QuadRegion qr;
        qr.ring = ring;
        qr.mode = QuadRegionMode::Free;
        qr.spacing = 5.0;
        g.addQuadRegion(qr);
        g.setOptions(baseOptions());
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        appendReport(m_report, QStringLiteral("(e) terrain cloud inside the Free ring"), g, r);

        const QuadRegionReport &rep = g.quadRegionReports().first();
        QCOMPARE(rep.resolved, QuadRegionMode::Free);
        QVERIFY2(rep.droppedSteiners > 0, qPrintable(QStringLiteral("dropped %1").arg(rep.droppedSteiners)));
        QCOMPARE(rep.droppedSteiners, insideCloud);
        QVERIFY2(rep.quads >= 0.9 * (rep.quads + rep.triangles),
                 qPrintable(QStringLiteral("quads %1 triangles %2").arg(rep.quads).arg(rep.triangles)));
        QVERIFY(rep.minScaledJacobian >= 0.866);
        QCOMPARE(rep.quads, 200);          // the lattice is exactly (a)'s
        QCOMPARE(rep.generatedPoints, 171);
        CHECK_CELL_INVARIANTS(r);

        auto findVertex = [&](const QPointF &p) {
            for (int i = 0; i < r.vertices.size(); ++i)
                if (std::hypot(r.vertices[i].xy.x() - p.x(), r.vertices[i].xy.y() - p.y()) < 1e-6) return i;
            return -1;
        };
        for (const QPointF &p : outsideAux) QVERIFY(findVertex(p) >= 0);   // marker-0 outside: kept
        int cloudSurvivors = 0;
        for (double x = 104; x <= 196; x += 4.0)
            for (double y = 54; y <= 96; y += 4.0)
                if (findVertex(QPointF(x, y)) >= 0) ++cloudSurvivors;
        // Only coincidences with the h = 5 lattice can "survive" (x and y
        // both multiples of 20: 5 × 2 candidates), never the cloud itself.
        QVERIFY2(cloudSurvivors <= 10, qPrintable(QStringLiteral("cloud survivors %1").arg(cloudSurvivors)));
    }

    /*! (e') KNOWN GAPS, recorded as expected failures so the fix flips them
     *  to XPASS (which QtTest reports as a failure — remove the QEXPECT_FAIL
     *  then). Found while writing this suite, in the sandbox harness:
     *
     *   1. A terrain cloud OUTSIDE the ring at a spacing comparable to h
     *      (4 m vs h = 5) encroaches on the ring segments; Triangle splits
     *      them and cascades Steiner points into the lattice → ~55–65 %
     *      quads instead of ≥ 90 %. The -u hook only suppresses AREA
     *      refinement inside the ring; encroachment and -q splits are not
     *      covered. Candidate fix: drop marker-0 Steiners within ~h of the
     *      ring on the OUTSIDE as well (plan D5 buffer), or emit the ring
     *      with 'Y'-like protection.
     *   2. Smoothing lowers the region's min SJ below the 0.866 acceptance
     *      floor (0.874 → 0.64 here): the guard is "min SJ over incident
     *      cells does not decrease", and when a skinny triangle is the
     *      minimum a quad may degrade freely. Candidate fix: guard every
     *      incident quad individually (or against the bounds' floor).
     *   3. A single junction Steiner (marker != 0) inside the region pins
     *      the lattice and leaves ~26 triangles → 87.5 % quads (< 90 %).
     */
    /*! Gaps 1 and 2 were closed on 2026-09-06 (generator drops marker-0
     *  Steiners within h of a Free ring; smoothing guards every quad against
     *  the SJ floor), so they are plain assertions now. Gap 3 — a pinned
     *  junction inside a Free ring leaves a seam of triangles around it —
     *  remains a documented limitation: >= 85 % quads is asserted, the plan's
     *  90 % target stays an XFAIL. */
    void knownGaps_exteriorCloud_smoothing_junction()
    {
        const QPolygonF ring = rect(100, 50, 100, 50);

        // 1 + 2: off-grid 4 m cloud over the whole domain.
        {
            MeshGenerator g;
            g.setDomain(domain300x150());
            for (double x = 21.3; x < 300; x += 4.0)
                for (double y = 21.3; y < 150; y += 4.0)
                {
                    SteinerPoint sp;
                    sp.xy = QPointF(x, y);
                    g.addSteinerPoint(sp);
                }
            QuadRegion qr;
            qr.ring = ring;
            qr.mode = QuadRegionMode::Free;
            qr.spacing = 5.0;
            g.addQuadRegion(qr);
            g.setOptions(baseOptions());
            const MeshResult r = g.generate();
            QVERIFY2(r.ok, qPrintable(r.errorMsg));
            appendReport(m_report, QStringLiteral("(e') 4 m cloud over the whole domain (ex-gaps 1+2)"), g, r);
            const QuadRegionReport &rep = g.quadRegionReports().first();
            QVERIFY(rep.droppedSteiners > 0);
            CHECK_CELL_INVARIANTS(r);      // convexity / order still hold
            QVERIFY2(rep.quads >= 0.9 * (rep.quads + rep.triangles),
                     qPrintable(QStringLiteral("quads %1 triangles %2").arg(rep.quads).arg(rep.triangles)));
            QVERIFY2(rep.minScaledJacobian >= 0.866,
                     qPrintable(QStringLiteral("min SJ %1").arg(rep.minScaledJacobian)));

            // Same inputs, smoothing off: pairing alone respects the floor.
            MeshGenerator g0;
            g0.setDomain(domain300x150());
            for (double x = 21.3; x < 300; x += 4.0)
                for (double y = 21.3; y < 150; y += 4.0)
                {
                    SteinerPoint sp;
                    sp.xy = QPointF(x, y);
                    g0.addSteinerPoint(sp);
                }
            g0.addQuadRegion(qr);
            GenerationOptions o0 = baseOptions();
            o0.quadCleanup.smoothingIterations = 0;
            g0.setOptions(o0);
            const MeshResult r0 = g0.generate();
            QVERIFY2(r0.ok, qPrintable(r0.errorMsg));
            appendReport(m_report, QStringLiteral("(e') same, smoothing off"), g0, r0);
            QVERIFY(g0.quadRegionReports().first().minScaledJacobian >= 0.866);
        }

        // 3: one junction inside the region.
        {
            MeshGenerator g;
            g.setDomain(domain300x150());
            SteinerPoint junction;
            junction.xy = QPointF(137.3, 71.9);
            junction.marker = 42;
            junction.tag = QStringLiteral("J1");
            g.addSteinerPoint(junction);
            QuadRegion qr;
            qr.ring = ring;
            qr.mode = QuadRegionMode::Free;
            qr.spacing = 5.0;
            g.addQuadRegion(qr);
            g.setOptions(baseOptions());
            const MeshResult r = g.generate();
            QVERIFY2(r.ok, qPrintable(r.errorMsg));
            appendReport(m_report, QStringLiteral("(e') KNOWN GAP 3: junction inside the Free ring"), g, r);
            const QuadRegionReport &rep = g.quadRegionReports().first();
            QCOMPARE(rep.droppedSteiners, 0);
            int vi = -1;
            for (int i = 0; i < r.vertices.size() && vi < 0; ++i)
                if (std::hypot(r.vertices[i].xy.x() - 137.3, r.vertices[i].xy.y() - 71.9) < 1e-6) vi = i;
            QVERIFY(vi >= 0);                                   // the junction pins the lattice
            QCOMPARE(r.vertices[vi].marker, 42);
            CHECK_CELL_INVARIANTS(r);
            QVERIFY(rep.quads >= 0.85 * (rep.quads + rep.triangles));
            QEXPECT_FAIL("", "known gap 3: a pinned junction leaves > 10 % triangles", Continue);
            QVERIFY2(rep.quads >= 0.9 * (rep.quads + rep.triangles),
                     qPrintable(QStringLiteral("quads %1 triangles %2").arg(rep.quads).arg(rep.triangles)));
            QVERIFY2(rep.minScaledJacobian >= 0.866,
                     qPrintable(QStringLiteral("min SJ %1").arg(rep.minScaledJacobian)));
        }
    }


    /*! (f) Unchanged when absent: two fresh generators with identical inputs
     *  and no quad regions produce bitwise-identical vertices and cells. */
    void noRegions_identicalRuns()
    {
        auto build = [](MeshGenerator &g) {
            g.setDomain(domain300x150());
            ConstraintSegment brk;
            brk.path << QPointF(50, 75) << QPointF(250, 75);
            brk.marker = 7;
            brk.tag = QStringLiteral("crest");
            g.addConstraintSegment(brk);
            SteinerPoint sp;
            sp.xy = QPointF(77.7, 33.3);
            sp.marker = 3;
            sp.tag = QStringLiteral("J9");
            g.addSteinerPoint(sp);
            RegionMarker rm;
            rm.xy = QPointF(20, 20);
            rm.attribute = 1.0;
            rm.tag = QStringLiteral("S1");
            g.addRegion(rm);
            g.setOptions(baseOptions());
        };
        MeshGenerator a, b;
        build(a);
        build(b);
        const MeshResult ra = a.generate(), rb = b.generate();
        QVERIFY2(ra.ok, qPrintable(ra.errorMsg));
        QVERIFY2(rb.ok, qPrintable(rb.errorMsg));
        appendReport(m_report, QStringLiteral("(f) no regions, run A"), a, ra);
        QVERIFY(a.quadRegionReports().isEmpty());
        QCOMPARE(ra.quadCount(), 0);
        QCOMPARE(ra.vertices.size(), rb.vertices.size());
        QCOMPARE(ra.triangles.size(), rb.triangles.size());
        QCOMPARE(ra.boundaryEdges.size(), rb.boundaryEdges.size());
        for (int i = 0; i < ra.vertices.size(); ++i)
        {
            QVERIFY(ra.vertices[i].xy == rb.vertices[i].xy);
            QCOMPARE(ra.vertices[i].marker, rb.vertices[i].marker);
        }
        for (int i = 0; i < ra.triangles.size(); ++i)
        {
            QCOMPARE(ra.triangles[i].v0, rb.triangles[i].v0);
            QCOMPARE(ra.triangles[i].v1, rb.triangles[i].v1);
            QCOMPARE(ra.triangles[i].v2, rb.triangles[i].v2);
            QCOMPARE(ra.triangles[i].v3, -1);
            QCOMPARE(ra.triangles[i].tag, rb.triangles[i].tag);
        }
        for (int i = 0; i < ra.boundaryEdges.size(); ++i)
        {
            QCOMPARE(ra.boundaryEdges[i].v0, rb.boundaryEdges[i].v0);
            QCOMPARE(ra.boundaryEdges[i].v1, rb.boundaryEdges[i].v1);
            QCOMPARE(ra.boundaryEdges[i].marker, rb.boundaryEdges[i].marker);
        }
        CHECK_CELL_INVARIANTS(ra);
    }
};

QTEST_MAIN(TestMeshQuadRegionE2E)
#include "test_meshquadregion_e2e.moc"
