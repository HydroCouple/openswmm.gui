/*!
 * \file   test_meshstrips.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Aligned quad strips in the triangle engine
 * (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D12): facing break-line
 * pairs (streets, ditches), conduit strips with the conduit along the middle
 * row, strips that do not fit being dropped, and the triangles around them.
 */
#include "mesh/breaklinestrips.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshcellstats.h"
#include "mesh/meshgenerator.h"

#include <QLineF>
#include <QSet>
#include <QTest>

#include <cmath>

using mesh::ConstraintSegment;
using mesh::FacingPairOptions;
using mesh::GenerationOptions;
using mesh::MeshGenerator;
using mesh::MeshResult;

namespace {

QPolygonF rect(double x0, double y0, double w, double h)
{
    return QPolygonF({QPointF(x0, y0), QPointF(x0 + w, y0), QPointF(x0 + w, y0 + h), QPointF(x0, y0 + h), QPointF(x0, y0)});
}

QVector<QPointF> line(QPointF a, QPointF b, double step)
{
    QVector<QPointF> out;
    const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
    const int n = std::max(1, int(std::ceil(len / step)));
    for (int k = 0; k <= n; ++k) out.append(a + (b - a) * (double(k) / n));
    return out;
}

double totalArea(const MeshResult &m)
{
    double a = 0.0;
    for (const mesh::MeshTriangle &t : m.triangles) a += mesh::cellGeom(m.vertices, t).area;
    return a;
}

int missingConstraintEdges(const MeshResult &m)
{
    QSet<QPair<int, int>> cellEdges;
    for (const mesh::MeshTriangle &t : m.triangles)
        for (int e = 0; e < t.vertexCount(); ++e)
        {
            const int a = t.vertex(e), b = t.vertex((e + 1) % t.vertexCount());
            cellEdges.insert(qMakePair(std::min(a, b), std::max(a, b)));
        }
    int missing = 0;
    for (const mesh::MeshEdge &e : m.boundaryEdges)
        if (!cellEdges.contains(qMakePair(std::min(e.v0, e.v1), std::max(e.v0, e.v1)))) ++missing;
    return missing;
}

/*! Length of the edges carrying \p marker, and whether they chain from
 *  \p from to \p to without a gap. */
double markedLength(const MeshResult &m, int marker, QPointF from, QPointF to, bool *chained)
{
    QHash<int, QVector<int>> adj;
    double len = 0.0;
    for (const mesh::MeshEdge &e : m.boundaryEdges)
    {
        if (e.marker != marker) continue;
        adj[e.v0].append(e.v1);
        adj[e.v1].append(e.v0);
        const QPointF d = m.vertices[e.v1].xy - m.vertices[e.v0].xy;
        len += std::hypot(d.x(), d.y());
    }
    int start = -1, goal = -1;
    for (int v = 0; v < m.vertices.size(); ++v)
    {
        if (m.vertices[v].xy == from) start = v;
        if (m.vertices[v].xy == to) goal = v;
    }
    QSet<int> seen{start};
    QVector<int> stack{start};
    while (!stack.isEmpty())
    {
        const int v = stack.takeLast();
        for (int w : adj.value(v)) if (!seen.contains(w)) { seen.insert(w); stack.append(w); }
    }
    *chained = start >= 0 && goal >= 0 && seen.contains(goal);
    return len;
}

/*! Smallest angle over triangles that touch no quad vertex. */
double freeTriangleMinAngle(const MeshResult &m)
{
    QSet<int> quadVertices;
    for (const mesh::MeshTriangle &t : m.triangles)
        if (t.isQuad()) for (int k = 0; k < 4; ++k) quadVertices.insert(t.vertex(k));
    double worst = 180.0;
    for (const mesh::MeshTriangle &t : m.triangles)
    {
        if (t.isQuad()) continue;
        bool touches = false;
        for (int k = 0; k < 3; ++k) touches |= quadVertices.contains(t.vertex(k));
        if (touches) continue;
        for (int k = 0; k < 3; ++k)
        {
            const QPointF p = m.vertices[t.vertex(k)].xy;
            const QPointF u = m.vertices[t.vertex((k + 1) % 3)].xy - p, v = m.vertices[t.vertex((k + 2) % 3)].xy - p;
            const double c = QPointF::dotProduct(u, v) / (std::hypot(u.x(), u.y()) * std::hypot(v.x(), v.y()));
            worst = std::min(worst, std::acos(std::clamp(c, -1.0, 1.0)) * 180.0 / M_PI);
        }
    }
    return worst;
}

} // namespace

class TestMeshStrips : public QObject
{
    Q_OBJECT
private slots:
    void facingLinesWithLowGroundBetweenPair()
    {
        const QVector<QVector<QPointF>> lines = {line({10, 20}, {90, 20}, 0.5), line({90, 32}, {10, 32}, 0.5)};
        FacingPairOptions o;
        o.minWidth = 2.0; o.maxWidth = 40.0; o.station = 1.0;
        o.elevationAt = [](double, double y) { return (y > 20 && y < 32) ? -0.15 : 0.0; };
        const auto pairs = mesh::findFacingPairs(lines, {}, o);
        QCOMPARE(pairs.size(), 1);
        QVERIFY(std::abs(pairs[0].width - 12.0) < 1e-6);
        QVERIFY(pairs[0].length > 70.0);   // 80 m less the trimmed ends
        // A ridge between them is not a street.
        o.elevationAt = [](double, double y) { return (y > 20 && y < 32) ? 0.15 : 0.0; };
        QCOMPARE(mesh::findFacingPairs(lines, {}, o).size(), 0);
        // Without the trough test any facing pair qualifies.
        o.elevationAt = nullptr;
        QCOMPARE(mesh::findFacingPairs(lines, {}, o).size(), 1);
        // Something between them blocks the pair.
        QCOMPARE(mesh::findFacingPairs(lines, {qMakePair(QPointF(0, 26), QPointF(100, 26))}, o).size(), 0);
        // Too narrow, too wide, or not parallel: no pair.
        o.minWidth = 13.0;
        QCOMPARE(mesh::findFacingPairs(lines, {}, o).size(), 0);
        o.minWidth = 2.0; o.maxWidth = 10.0;
        QCOMPARE(mesh::findFacingPairs(lines, {}, o).size(), 0);
        o.maxWidth = 40.0;
        const QVector<QVector<QPointF>> skew = {line({10, 20}, {90, 20}, 0.5), line({10, 32}, {90, 80}, 0.5)};
        QCOMPARE(mesh::findFacingPairs(skew, {}, o).size(), 0);
    }

    void streetBetweenCurbsGetsQuadsAlongIt()
    {
        auto build = [](bool trough, MeshGenerator *g) {
            g->setDomain(rect(0, 0, 100, 60));
            g->setTerrainBreaklines({line({5, 20}, {95, 20}, 0.5), line({95, 32}, {5, 32}, 0.5)});
            mesh::RefineHook hook;
            hook.elevationAt = [trough](double, double y) { return (y > 20 && y < 32) ? (trough ? -0.15 : 0.15) : 0.0; };
            g->setRefineHook(hook);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 2.0 * 2.0;
            o.minCellSize = 0.5;
            o.quadsBetweenBreaklines = true;
            g->setOptions(o);
            return g->generate();
        };
        MeshGenerator g;
        const MeshResult m = build(true, &g);
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(g.stats().breaklineStrips, 1);
        QVERIFY(std::abs(totalArea(m) - 6000.0) < 1e-6);
        QCOMPARE(missingConstraintEdges(m), 0);
        int quads = 0;
        for (const mesh::MeshTriangle &c : m.triangles)
        {
            if (!c.isQuad()) continue;
            ++quads;
            const QPointF ctr = mesh::cellGeom(m.vertices, c).centroid;
            QVERIFY(ctr.y() > 20 && ctr.y() < 32);
            // Edges run along the street or across it.
            for (int k = 0; k < 4; ++k)
            {
                const QPointF d = m.vertices[c.vertex((k + 1) % 4)].xy - m.vertices[c.vertex(k)].xy;
                const double ang = std::fmod(std::atan2(d.y(), d.x()) * 180.0 / M_PI + 360.0, 90.0);
                QVERIFY2(ang < 2.0 || ang > 88.0, qPrintable(QString::number(ang)));
            }
        }
        QVERIFY2(quads >= 150, qPrintable(QString::number(quads)));   // ~85 m × 12 m at h = 2
        const mesh::QuadStats qs = mesh::computeQuadStats(m);
        QCOMPARE(qs.nonConvex, 0);
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        qInfo("street: %d quads, %d triangles, free-triangle min angle %.2f, ratio p50 %.2f max %.2f, below 10°: %d",
              gs.quads, gs.triangles, freeTriangleMinAngle(m), gs.ratioP50, gs.ratioMax, gs.cellsBelow10Deg);
        QVERIFY(freeTriangleMinAngle(m) >= 30.0 - 1e-6);
        QCOMPARE(gs.cellsBelow10Deg, 0);
        // A ridge between the lines: no strip, both lines stay break lines.
        MeshGenerator r;
        const MeshResult mr = build(false, &r);
        QVERIFY2(mr.ok, qPrintable(mr.errorMsg));
        QCOMPARE(r.stats().breaklineStrips, 0);
        QCOMPARE(r.acceptedTerrainBreaklines().size(), 2);
        for (const mesh::MeshTriangle &c : mr.triangles) QVERIFY(!c.isQuad());
    }

    void conduitRunsAlongTheMiddleOfItsStrip()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 200, 100));
        ConstraintSegment c;
        c.path = {QPointF(20, 50), QPointF(100, 55), QPointF(180, 50)};
        c.marker = 9; c.tag = QStringLiteral("C9"); c.stripWidth = 8.0;
        g.addConstraintSegment(c);
        for (const QPointF &p : {QPointF(20, 50), QPointF(180, 50)})
        {
            mesh::SteinerPoint j; j.xy = p; j.marker = p.x() < 100 ? 11 : 12; j.tag = p.x() < 100 ? QStringLiteral("J1") : QStringLiteral("J2");
            g.addSteinerPoint(j);
        }
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 4.0 * 4.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(g.stats().conduitStrips, 1);
        QCOMPARE(missingConstraintEdges(m), 0);
        QVERIFY(std::abs(totalArea(m) - 20000.0) < 1e-6);
        bool chained = false;
        const double len = markedLength(m, 9, QPointF(20, 50), QPointF(180, 50), &chained);
        QVERIFY(chained);
        const double expected = std::hypot(80.0, 5.0) * 2.0;
        QVERIFY2(std::abs(len - expected) < 1e-6, qPrintable(QStringLiteral("%1 vs %2").arg(len).arg(expected)));
        for (const mesh::MeshEdge &e : m.boundaryEdges) if (e.marker == 9) QCOMPARE(e.tag, QStringLiteral("C9"));
        int quads = 0;
        for (const mesh::MeshTriangle &t : m.triangles) if (t.isQuad()) ++quads;
        QVERIFY2(quads >= 70, qPrintable(QString::number(quads)));
        int junctions = 0;
        for (const mesh::MeshVertex &v : m.vertices) if (v.marker == 11 || v.marker == 12) ++junctions;
        QCOMPARE(junctions, 2);
        const mesh::QuadStats qs = mesh::computeQuadStats(m);
        QCOMPARE(qs.nonConvex, 0);
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        qInfo("conduit strip: %d quads, free-triangle min angle %.2f, ratio max %.2f, below 10°: %d",
              quads, freeTriangleMinAngle(m), gs.ratioMax, gs.cellsBelow10Deg);
        QVERIFY(freeTriangleMinAngle(m) >= 30.0 - 1e-6);
        QCOMPARE(gs.cellsBelow10Deg, 0);
    }

    void networkStripsStopShortOfJunctionsAndCrowdedOnesAreDropped()
    {
        // Four conduits leave one junction, two of them only 20° apart: every
        // strip fits once those two stop further from the junction.
        {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 200, 200));
            const QPointF hub(100, 100);
            int marker = 20;
            for (double deg : {0.0, 120.0, 240.0, 20.0})
            {
                ConstraintSegment c;
                const double a = deg * M_PI / 180.0;
                c.path = {hub, hub + QPointF(80 * std::cos(a), 80 * std::sin(a))};
                c.marker = marker++;
                c.stripWidth = 6.0;
                g.addConstraintSegment(c);
            }
            mesh::SteinerPoint j; j.xy = hub; j.marker = 5; j.tag = QStringLiteral("HUB");
            g.addSteinerPoint(j);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 4.0 * 4.0;
            g.setOptions(o);
            const MeshResult m = g.generate();
            QVERIFY2(m.ok, qPrintable(m.errorMsg));
            QCOMPARE(g.stats().conduitStrips, 4);
            QCOMPARE(g.stats().stripsDropped, 0);
            QCOMPARE(missingConstraintEdges(m), 0);
            QVERIFY(std::abs(totalArea(m) - 40000.0) < 1e-6);
            const double degs[4] = {0.0, 120.0, 240.0, 20.0};
            for (int k = 0; k < 4; ++k)
            {
                const double a = degs[k] * M_PI / 180.0;
                bool chained = false;
                markedLength(m, 20 + k, hub, hub + QPointF(80 * std::cos(a), 80 * std::sin(a)), &chained);
                QVERIFY2(chained, qPrintable(QString::number(20 + k)));
            }
            QCOMPARE(mesh::computeQuadStats(m).nonConvex, 0);
        }
        // Two conduits 5 apart with 6-wide strips: the second cannot fit
        // anywhere along its length, so it stays a plain line.
        {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 200, 100));
            for (int k = 0; k < 2; ++k)
            {
                ConstraintSegment c;
                c.path = {QPointF(20, 50 + 5 * k), QPointF(180, 50 + 5 * k)};
                c.marker = 30 + k;
                c.stripWidth = 6.0;
                g.addConstraintSegment(c);
            }
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 4.0 * 4.0;
            g.setOptions(o);
            const MeshResult m = g.generate();
            QVERIFY2(m.ok, qPrintable(m.errorMsg));
            QCOMPARE(g.stats().conduitStrips, 1);
            QCOMPARE(g.stats().stripsDropped, 1);
            for (int k = 0; k < 2; ++k)
            {
                bool chained = false;
                markedLength(m, 30 + k, QPointF(20, 50 + 5 * k), QPointF(180, 50 + 5 * k), &chained);
                QVERIFY(chained);
            }
        }
    }

    void stripsNeverSwallowAConstraint()
    {
        // A hole, a closed ring and an open line, each inside a four-sided
        // quad region: the region keeps triangles (it would otherwise cover
        // the hole or drop the line's edges) and says why.
        for (int kind = 0; kind < 3; ++kind)
        {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 100, 100));
            ConstraintSegment c;
            if (kind < 2) c.path = {QPointF(45, 45), QPointF(55, 45), QPointF(55, 55), QPointF(45, 55), QPointF(45, 45)};
            else c.path = {QPointF(35, 50), QPointF(65, 50)};
            c.marker = 9;
            g.addConstraintSegment(c);
            if (kind == 0) g.addHole(QPointF(50, 50));
            mesh::QuadRegion qr;
            qr.ring = rect(20, 20, 60, 60);
            qr.spacing = 4.0;
            g.addQuadRegion(qr);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 25.0;
            g.setOptions(o);
            const MeshResult m = g.generate();
            QVERIFY2(m.ok, qPrintable(m.errorMsg));
            QCOMPARE(g.stats().regionPatches, 0);
            QVERIFY(g.quadRegionReports()[0].message.contains(QStringLiteral("inside")));
            QVERIFY(std::abs(totalArea(m) - (kind == 0 ? 9900.0 : 10000.0)) < 1e-6);
            QCOMPARE(missingConstraintEdges(m), 0);
        }
    }

    void conduitThatBendsBackKeepsItsEnds()
    {
        // The path folds back near its start: no strip may cross the plain
        // end (generation used to fail on the crossing).
        MeshGenerator g;
        g.setDomain(rect(0, 0, 200, 200));
        const double w = 6.0;
        ConstraintSegment c;
        c.path = {QPointF(60, 100), QPointF(60 + 3 * w, 100), QPointF(60, 100 + 0.6 * w), QPointF(60, 190)};
        c.marker = 9; c.stripWidth = w;
        g.addConstraintSegment(c);
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 16.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QVERIFY(std::abs(totalArea(m) - 40000.0) < 1e-6);
        QCOMPARE(missingConstraintEdges(m), 0);
        bool chained = false;
        markedLength(m, 9, QPointF(60, 100), QPointF(60, 190), &chained);
        QVERIFY(chained);
    }

    void pipesCrossingInPlanKeepTheirStrips()
    {
        // Two pipes cross without a junction (one passes over the other):
        // both get a vertex at the crossing, and each is cut into two runs
        // there, so four strips stop short of it rather than two dropped.
        for (const double deg : {90.0, 35.0})
        {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 200, 200));
            const QPointF hub(100, 100);
            const double a = deg * M_PI / 180.0;
            const QPointF ends[2][2] = {{QPointF(20, 100), QPointF(180, 100)},
                                        {hub - QPointF(80 * std::cos(a), 80 * std::sin(a)), hub + QPointF(80 * std::cos(a), 80 * std::sin(a))}};
            for (int k = 0; k < 2; ++k)
            {
                ConstraintSegment c;
                c.path = {ends[k][0], ends[k][1]};
                c.marker = 40 + k;
                c.stripWidth = 6.0;
                g.addConstraintSegment(c);
            }
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 4.0 * 4.0;
            g.setOptions(o);
            const MeshResult m = g.generate();
            QVERIFY2(m.ok, qPrintable(m.errorMsg));
            QCOMPARE(g.stats().conduitStrips, 4);
            QCOMPARE(g.stats().stripsDropped, 0);
            QCOMPARE(missingConstraintEdges(m), 0);
            QVERIFY(std::abs(totalArea(m) - 40000.0) < 1e-6);
            for (int k = 0; k < 2; ++k)
            {
                bool chained = false;
                const double len = markedLength(m, 40 + k, ends[k][0], ends[k][1], &chained);
                QVERIFY2(chained, qPrintable(QString::number(40 + k)));
                QVERIFY(std::abs(len - 160.0) < 1e-6);
            }
            int atHub = 0;
            for (const mesh::MeshVertex &v : m.vertices) if (QLineF(v.xy, hub).length() < 1e-9) ++atHub;
            QCOMPARE(atHub, 1);
            QCOMPARE(mesh::computeQuadStats(m).nonConvex, 0);
            QCOMPARE(mesh::computeGradingStats(m).cellsBelow10Deg, 0);
        }
    }

    void stripsStopShortOfSharpBendsAndKeepEvenRows()
    {
        // A conduit turning 90° (Bellinge G72F...): one strip each side of
        // the corner, plain line round it. A vertex 1.5 cm past the cut
        // (G70F...) must not leave a sliver row of quads.
        MeshGenerator g;
        g.setDomain(rect(0, 0, 200, 200));
        ConstraintSegment c;
        c.path = {QPointF(20, 50), QPointF(26.015, 50), QPointF(120, 50), QPointF(120, 170)};
        c.marker = 9; c.stripWidth = 6.0;
        g.addConstraintSegment(c);
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 4.0 * 4.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(g.stats().conduitStrips, 2);
        QCOMPARE(missingConstraintEdges(m), 0);
        QVERIFY(std::abs(totalArea(m) - 40000.0) < 1e-6);
        bool chained = false;
        const double len = markedLength(m, 9, QPointF(20, 50), QPointF(120, 170), &chained);
        QVERIFY(chained);
        QVERIFY(std::abs(len - 220.0) < 1e-6);
        double shortest = 1e9;
        for (const mesh::MeshTriangle &t : m.triangles)
            if (t.isQuad())
                for (int k = 0; k < 4; ++k)
                    shortest = std::min(shortest, QLineF(m.vertices[t.vertex(k)].xy, m.vertices[t.vertex((k + 1) % 4)].xy).length());
        QVERIFY2(shortest > 1.0, qPrintable(QString::number(shortest)));
        const mesh::QuadStats qs = mesh::computeQuadStats(m);
        QCOMPARE(qs.nonConvex, 0);
        QVERIFY(qs.minScaledJacobian >= 0.9);
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        QVERIFY(freeTriangleMinAngle(m) >= 30.0 - 1e-6);
        QVERIFY2(gs.ratioMax <= 2.1, qPrintable(QString::number(gs.ratioMax)));
    }

    void aManholeSplitsTheStreetInsteadOfDroppingIt()
    {
        auto build = [](bool manhole, MeshGenerator *g) {
            g->setDomain(rect(0, 0, 100, 60));
            g->setTerrainBreaklines({line({5, 20}, {95, 20}, 0.5), line({95, 32}, {5, 32}, 0.5)});
            if (manhole) { mesh::SteinerPoint p; p.xy = QPointF(50, 26); p.marker = 3; g->addSteinerPoint(p); }
            mesh::RefineHook hook;
            hook.elevationAt = [](double, double y) { return (y > 20 && y < 32) ? -0.15 : 0.0; };
            g->setRefineHook(hook);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 4.0;
            o.minCellSize = 0.5;
            o.quadsBetweenBreaklines = true;
            g->setOptions(o);
            return g->generate();
        };
        MeshGenerator plain, split;
        QVERIFY(build(false, &plain).ok);
        const MeshResult m = build(true, &split);
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(plain.stats().breaklineStrips, 1);
        QCOMPARE(split.stats().breaklineStrips, 2);
        bool vertex = false;
        for (const mesh::MeshVertex &v : m.vertices) if (v.xy == QPointF(50, 26)) vertex = true;
        QVERIFY(vertex);
    }
};

QTEST_MAIN(TestMeshStrips)
#include "test_meshstrips.moc"
