/*!
 * \file   test_meshfeaturecapture.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Terrain feature capture end-to-end on the triangle engine
 *         (MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md gate 6b.2,
 *         MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D12.3): a synthetic street
 *         DEM (0.5 m pixels, crowned carriageway, two 0.16 m curbs, a 3 m
 *         building) goes through the break-line extractor, the terrain size
 *         field and the generator. The street between its curbs becomes an
 *         aligned quad strip, every other kept line (the building) is a mesh
 *         edge, the triangles meet the angle bound, the mesh conforms and is
 *         graded, and terrain lines never become holes, region barriers or
 *         boundary edges.
 *
 *         Meshes are written as CSV under
 *         tests/output/mesh_triangle_engine_2026-09-30/feature_capture
 *         (SWMMVIS_MESH_FEATURE_OUTPUT overrides) for review (CLAUDE.md §4.1).
 */
#include "mesh/meshcellgeom.h"
#include "mesh/meshcellstats.h"
#include "mesh/meshgenerator.h"
#include "mesh/sizefield.h"
#include "mesh/terrainbreaklines.h"
#include "mesh/terrainsizefield.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QTest>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

using namespace mesh;

namespace {

constexpr double kPx = 0.5;   // metres per pixel

struct Dem
{
    int cols = 0, rows = 0;
    std::vector<float> z;
    float &at(int c, int r) { return z[size_t(r) * cols + c]; }
};

/*! 100 m × 60 m: road along x between y = 20 m and 40 m (rows 40..79),
 *  crowned at 1 %, 0.16 m curbs, sidewalks/lawn at 0.16 m with a gentle
 *  fall, and a 3 m building (x 30..60 m, y 3..15 m) on the north side. */
Dem streetDem()
{
    Dem d;
    d.cols = 200; d.rows = 120;
    d.z.assign(size_t(d.cols) * d.rows, 0.0f);
    for (int r = 0; r < d.rows; ++r)
        for (int c = 0; c < d.cols; ++c)
        {
            double z;
            if (r >= 40 && r < 80) z = 0.005 * (20 - std::abs(r - 60) + (r >= 60 ? -1 : 0));   // crowned road
            else z = 0.16 + 0.001 * c;                                                           // sidewalk / lawn
            if (r >= 6 && r < 30 && c >= 60 && c < 120) z = 3.16;                                // building
            d.at(c, r) = float(z);
        }
    return d;
}

QVector<QVector<QPointF>> toMesh(const QVector<QVector<QPointF>> &pixelChains)
{
    QVector<QVector<QPointF>> out;
    for (const auto &c : pixelChains)
    {
        QVector<QPointF> m;
        for (const QPointF &p : c) m.append(QPointF(p.x() * kPx, p.y() * kPx));
        out.append(m);
    }
    return out;
}

struct Setup
{
    Dem dem;
    TerrainSizeField terrain;
    SizeField size;
    QVector<QVector<QPointF>> lines;
    QPolygonF domain;
};

bool prepare(Setup &s, double tolerance = 0.1)
{
    s.dem = streetDem();
    TerrainSizeOptions to;
    to.tolerance = tolerance; to.maxLevel = 6; to.outLevel = 1;
    TerrainBreaklineExtractor ex;
    TerrainBreaklineOptions bo;
    bo.tolerance = tolerance;
    to.rowSink = [&](const float *row, int r, int cols, int rows) {
        if (r == 0) ex.begin(cols, rows, bo);
        ex.pushRow(row);
    };
    if (!s.terrain.buildFromGrid(s.dem.z.data(), s.dem.cols, s.dem.rows, to)) return false;
    s.lines = toMesh(ex.finish());
    s.domain = QPolygonF({QPointF(2, 2), QPointF(98, 2), QPointF(98, 58), QPointF(2, 58)});
    SizeFieldOptions so;
    so.nearSize = 4.0;
    so.gradation = 0.5;
    so.maxSize = 16.0;
    so.areaFloor = 0.4330127018922193 * 0.5 * 0.5;
    const TerrainSizeField *tf = &s.terrain;
    so.terrainSizeAt = [tf](double x, double y) { return tf->sizePixelsAt(x / kPx, y / kPx) * kPx; };
    so.steps = s.lines;   // captured as edges: no refinement cone around them
    return s.size.build(QRectF(0, 0, 100, 60), {}, {}, {}, so);
}

/*! Nearest-pixel DEM elevation at a mesh coordinate (NaN off the grid). */
std::function<double(double, double)> demElevation(const std::vector<float> *z, int cols, int rows)
{
    return [z, cols, rows](double x, double y) {
        const int c = int(std::floor(x / kPx)), r = int(std::floor(y / kPx));
        if (c < 0 || r < 0 || c >= cols || r >= rows) return std::numeric_limits<double>::quiet_NaN();
        return double((*z)[size_t(r) * cols + c]);
    };
}

MeshGenerator makeGenerator(const Setup &s, bool streets)
{
    MeshGenerator g;
    g.setDomain(s.domain);
    GenerationOptions o;
    o.maxArea = 0.4330127018922193 * 16.0;
    o.minCellSize = 0.5;
    o.quadsBetweenBreaklines = streets;
    g.setOptions(o);
    RefineHook h;
    const SizeField *sf = &s.size;
    h.targetAreaAt = [sf](double x, double y) { return sf->targetAreaAt(x, y); };
    h.elevationAt = demElevation(&s.dem.z, s.dem.cols, s.dem.rows);
    g.setRefineHook(h);
    g.setTerrainBreaklines(s.lines);
    return g;
}

/*! Smallest angle over triangles that touch no quad vertex. */
double freeTriangleMinAngle(const MeshResult &m)
{
    QSet<int> quadVertices;
    for (const MeshTriangle &t : m.triangles)
        if (t.isQuad()) for (int k = 0; k < 4; ++k) quadVertices.insert(t.vertex(k));
    double worst = 180.0;
    for (const MeshTriangle &t : m.triangles)
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

double distToLines(const QPointF &p, const QVector<QVector<QPointF>> &lines)
{
    double best = std::numeric_limits<double>::infinity();
    for (const auto &l : lines)
        for (int k = 1; k < l.size(); ++k)
        {
            const QPointF a = l[k - 1], b = l[k];
            const QPointF ab = b - a;
            const double len2 = QPointF::dotProduct(ab, ab);
            double t = len2 > 0 ? QPointF::dotProduct(p - a, ab) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const QPointF q = a + ab * t;
            best = std::min(best, std::hypot(p.x() - q.x(), p.y() - q.y()));
        }
    return best;
}

/*! Every point sampled along the kept lines lies on a mesh edge. */
int pointsOffEdges(const MeshResult &r, const QVector<QVector<QPointF>> &lines)
{
    QVector<QPair<QPointF, QPointF>> edges;
    QSet<QPair<int, int>> seen;
    for (const MeshTriangle &t : r.triangles)
    {
        const int n = t.vertexCount();
        for (int e = 0; e < n; ++e)
        {
            int a = t.vertex(e), b = t.vertex((e + 1) % n);
            if (a > b) std::swap(a, b);
            if (seen.contains({a, b})) continue;
            seen.insert({a, b});
            edges.append({r.vertices[a].xy, r.vertices[b].xy});
        }
    }
    int off = 0;
    for (const auto &l : lines)
        for (int k = 1; k < l.size(); ++k)
            for (int s = 0; s <= 4; ++s)
            {
                const QPointF p = l[k - 1] + (l[k] - l[k - 1]) * (s / 4.0);
                double best = std::numeric_limits<double>::infinity();
                for (const auto &e : edges)
                {
                    const QPointF ab = e.second - e.first;
                    const double len2 = QPointF::dotProduct(ab, ab);
                    double t = len2 > 0 ? QPointF::dotProduct(p - e.first, ab) / len2 : 0.0;
                    t = std::clamp(t, 0.0, 1.0);
                    const QPointF q = e.first + ab * t;
                    best = std::min(best, std::hypot(p.x() - q.x(), p.y() - q.y()));
                    if (best < 1e-7) break;
                }
                if (best > 1e-7) ++off;
            }
    return off;
}

double meshArea(const MeshResult &r)
{
    double a = 0;
    for (const MeshTriangle &t : r.triangles) a += cellGeom(r.vertices, t).area;
    return a;
}

/*! Every edge used by exactly two cells, or by one cell on the domain outline. */
int nonConformingEdges(const MeshResult &r, const QRectF &box)
{
    QHash<QPair<int, int>, int> use;
    for (const MeshTriangle &t : r.triangles)
    {
        const int n = t.vertexCount();
        for (int e = 0; e < n; ++e)
        {
            int a = t.vertex(e), b = t.vertex((e + 1) % n);
            if (a > b) std::swap(a, b);
            ++use[{a, b}];
        }
    }
    auto onBox = [&](const QPointF &p) {
        return std::abs(p.x() - box.left()) < 1e-9 || std::abs(p.x() - box.right()) < 1e-9
            || std::abs(p.y() - box.top()) < 1e-9 || std::abs(p.y() - box.bottom()) < 1e-9;
    };
    int bad = 0;
    for (auto it = use.cbegin(); it != use.cend(); ++it)
    {
        if (it.value() == 2) continue;
        if (it.value() == 1 && onBox(r.vertices[it.key().first].xy) && onBox(r.vertices[it.key().second].xy)) continue;
        ++bad;
    }
    return bad;
}

/*! Faces above \p limit (longest-edge ratio): their count and the worst
 *  \p count described, for diagnostics. */
QPair<int, QStringList> worstFaces(const MeshResult &r, double limit, int count)
{
    auto longest = [&](const MeshTriangle &t) {
        double L = 0.0;
        const int n = t.vertexCount();
        for (int k = 0; k < n; ++k)
        {
            const QPointF d = r.vertices[t.vertex(k)].xy - r.vertices[t.vertex((k + 1) % n)].xy;
            L = std::max(L, std::hypot(d.x(), d.y()));
        }
        return L;
    };
    QHash<QPair<int, int>, QVector<int>> faces;
    for (int i = 0; i < r.triangles.size(); ++i)
    {
        const MeshTriangle &t = r.triangles[i];
        const int n = t.vertexCount();
        for (int k = 0; k < n; ++k)
        {
            int a = t.vertex(k), b = t.vertex((k + 1) % n);
            if (a > b) std::swap(a, b);
            faces[{a, b}].append(i);
        }
    }
    QVector<QPair<double, QString>> bad;
    for (auto it = faces.cbegin(); it != faces.cend(); ++it)
    {
        if (it.value().size() != 2) continue;
        const MeshTriangle &A = r.triangles[it.value()[0]], &B = r.triangles[it.value()[1]];
        const double la = longest(A), lb = longest(B), q = std::max(la, lb) / std::min(la, lb);
        if (q <= limit) continue;
        const QPointF m = 0.5 * (r.vertices[it.key().first].xy + r.vertices[it.key().second].xy);
        bad.append({q, QStringLiteral("%1 at (%2,%3): %4 %5 | %6 %7").arg(q, 0, 'f', 2).arg(m.x(), 0, 'f', 2).arg(m.y(), 0, 'f', 2)
                           .arg(A.isQuad() ? "quad" : "tri").arg(la, 0, 'f', 2).arg(B.isQuad() ? "quad" : "tri").arg(lb, 0, 'f', 2)});
    }
    std::sort(bad.begin(), bad.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
    QStringList out;
    out << QStringLiteral("%1 faces above %2 of %3").arg(bad.size()).arg(limit).arg(faces.size());
    for (int i = 0; i < std::min<int>(count, bad.size()); ++i) out << bad[i].second;
    return {int(bad.size()), out};
}

/*! Grading gate for meshes with terrain break lines and strips: the median
 *  face near 1:1, none above 3, and at most 0.1 % above 2.1 (a triangle
 *  resting on a fixed strip edge cannot always shrink to its neighbour). */
bool featureGradingOk(const MeshResult &r, const GradingStats &gs, QString *why)
{
    const auto worst = worstFaces(r, 2.1, 5);
    *why = QStringLiteral("max %1 p50 %2; %3").arg(gs.ratioMax).arg(gs.ratioP50).arg(worst.second.join(QStringLiteral("; ")));
    return gs.ratioMax <= 3.0 && gs.ratioP50 <= 1.3 + 1e-9 && worst.first <= std::max(1, gs.faces / 1000);
}

void writeCsv(const MeshResult &r, const QVector<QVector<QPointF>> &lines, const QString &name)
{
    const QString dir = qEnvironmentVariable("SWMMVIS_MESH_FEATURE_OUTPUT",
                                             QStringLiteral(SWMMVIS_MESH_FEATURE_OUTPUT_DIR));
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(name + QStringLiteral("_cells.csv")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QTextStream o(&f);
        o << "cell,kind,x0,y0,x1,y1,x2,y2,x3,y3\n";
        for (int i = 0; i < r.triangles.size(); ++i)
        {
            const MeshTriangle &t = r.triangles[i];
            o << i << ',' << (t.isQuad() ? "quad" : "tri");
            for (int k = 0; k < 4; ++k)
            {
                if (k < t.vertexCount()) o << ',' << r.vertices[t.vertex(k)].xy.x() << ',' << r.vertices[t.vertex(k)].xy.y();
                else o << ",,";
            }
            o << '\n';
        }
    }
    QFile g(QDir(dir).filePath(name + QStringLiteral("_lines.csv")));
    if (g.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QTextStream o(&g);
        o << "line,point,x,y\n";
        for (int i = 0; i < lines.size(); ++i)
            for (int k = 0; k < lines[i].size(); ++k) o << i << ',' << k << ',' << lines[i][k].x() << ',' << lines[i][k].y() << '\n';
    }
}

} // namespace

class TestMeshFeatureCapture : public QObject
{
    Q_OBJECT

private slots:
    void streetBecomesAlignedQuadsBetweenItsCurbs()
    {
        Setup s;
        QVERIFY(prepare(s));
        QVERIFY2(s.lines.size() >= 3, qPrintable(QStringLiteral("%1 lines").arg(s.lines.size())));
        MeshGenerator g = makeGenerator(s, true);
        QElapsedTimer t;
        t.start();
        const MeshResult r = g.generate();
        const qint64 ms = t.elapsed();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        const auto &kept = g.acceptedTerrainBreaklines();
        writeCsv(r, kept, QStringLiteral("street_strip"));
        QCOMPARE(g.stats().breaklineStrips, 1);

        // Quads only on the carriageway, aligned with it.
        for (const MeshTriangle &c : r.triangles)
        {
            if (!c.isQuad()) continue;
            const QPointF ctr = cellGeom(r.vertices, c).centroid;
            QVERIFY2(ctr.y() > 19.5 && ctr.y() < 40.5, qPrintable(QStringLiteral("quad at %1,%2").arg(ctr.x()).arg(ctr.y())));
            for (int k = 0; k < 4; ++k)
            {
                const QPointF d = r.vertices[c.vertex((k + 1) % 4)].xy - r.vertices[c.vertex(k)].xy;
                const double ang = std::fmod(std::atan2(d.y(), d.x()) * 180.0 / M_PI + 360.0, 90.0);
                QVERIFY2(ang < 5.0 || ang > 85.0, qPrintable(QString::number(ang)));
            }
        }
        QVERIFY(r.quadCount() >= 8);
        // The building wall stays a break line, as mesh edges; the curbs are
        // the strip's sides: every traced curb point is within the
        // simplification tolerance of a mesh edge.
        int nearWall = 0;
        for (const auto &l : kept)
            for (const QPointF &p : l)
                if (std::abs(p.y() - 15.0) < 1.0 && p.x() > 30 && p.x() < 60) ++nearWall;
        QVERIFY(nearWall >= 2);
        QCOMPARE(pointsOffEdges(r, kept), 0);
        QVector<QVector<QPointF>> meshEdges;
        for (const MeshTriangle &c : r.triangles)
            for (int k = 0; k < c.vertexCount(); ++k)
                meshEdges.append({r.vertices[c.vertex(k)].xy, r.vertices[c.vertex((k + 1) % c.vertexCount())].xy});
        int curbPoints = 0;
        for (const auto &l : s.lines)
            for (const QPointF &p : l)
            {
                if (p.x() < 20 || p.x() > 80 || (std::abs(p.y() - 20.0) > 1.0 && std::abs(p.y() - 40.0) > 1.0)) continue;
                ++curbPoints;
                QVERIFY2(distToLines(p, meshEdges) < 0.5, qPrintable(QStringLiteral("curb point %1,%2").arg(p.x()).arg(p.y())));
            }
        QVERIFY(curbPoints > 100);

        // Conforming, covering, graded, the angle bound away from the strip.
        QCOMPARE(nonConformingEdges(r, s.domain.boundingRect()), 0);
        QVERIFY2(std::abs(meshArea(r) - 96.0 * 56.0) < 1e-6, qPrintable(QString::number(meshArea(r), 'g', 17)));
        const GradingStats gs = computeGradingStats(r);
        qInfo().noquote() << QStringLiteral("[feature] street: %1 cells (%2 quads) in %3 ms; ratio max %4 p50 %5 p95 %6; min angle %7 (free triangles %8); lines kept %9")
                                 .arg(r.triangles.size()).arg(r.quadCount()).arg(ms).arg(gs.ratioMax).arg(gs.ratioP50)
                                 .arg(gs.ratioP95).arg(gs.minAngleDeg).arg(freeTriangleMinAngle(r)).arg(kept.size());
        QString why;
        const bool gradingOk = featureGradingOk(r, gs, &why);
        qInfo().noquote() << "[feature]   grading:" << why;
        QVERIFY2(gradingOk, qPrintable(why));
        QCOMPARE(gs.cellsBelow10Deg, 0);
        QVERIFY2(freeTriangleMinAngle(r) >= 30.0 - 1e-6, qPrintable(QString::number(freeTriangleMinAngle(r))));

        // Terrain edges are not boundary edges.
        for (const MeshEdge &e : r.boundaryEdges)
        {
            const QPointF mid = 0.5 * (r.vertices[e.v0].xy + r.vertices[e.v1].xy);
            QVERIFY(distToLines(mid, kept) > 1e-7);
        }
    }

    void withoutStreetsEveryLineIsATriangleEdge()
    {
        Setup s;
        QVERIFY(prepare(s));
        MeshGenerator g = makeGenerator(s, false);
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        const auto &kept = g.acceptedTerrainBreaklines();
        writeCsv(r, kept, QStringLiteral("street_triangles"));
        int nearCurbN = 0, nearCurbS = 0, nearWall = 0;
        for (const auto &l : kept)
            for (const QPointF &p : l)
            {
                if (std::abs(p.y() - 20.0) < 1.0) ++nearCurbN;
                if (std::abs(p.y() - 40.0) < 1.0) ++nearCurbS;
                if (std::abs(p.y() - 15.0) < 1.0 && p.x() > 30 && p.x() < 60) ++nearWall;
            }
        QVERIFY2(nearCurbN >= 2 && nearCurbS >= 2 && nearWall >= 2,
                 qPrintable(QStringLiteral("curbN %1 curbS %2 wall %3").arg(nearCurbN).arg(nearCurbS).arg(nearWall)));
        QCOMPARE(pointsOffEdges(r, kept), 0);
        QCOMPARE(r.quadCount(), 0);
        QCOMPARE(nonConformingEdges(r, s.domain.boundingRect()), 0);
        QVERIFY(std::abs(meshArea(r) - 96.0 * 56.0) < 1e-6);
        const GradingStats gs = computeGradingStats(r);
        qInfo().noquote() << QStringLiteral("[feature] triangles: %1 cells; ratio max %2 p50 %3; min angle %4")
                                 .arg(r.triangles.size()).arg(gs.ratioMax).arg(gs.ratioP50).arg(gs.minAngleDeg);
        QCOMPARE(gs.cellsBelow10Deg, 0);
        QVERIFY(gs.ratioMax <= 2.0 + 1e-9);
        QVERIFY(gs.minAngleDeg >= 30.0 - 1e-6);
    }

    void linesAreCutBackFromOtherConstraints()
    {
        // A conduit crossing both curbs, and a line hugging the domain edge:
        // generation succeeds, no kept line comes near the conduit.
        Setup s;
        QVERIFY(prepare(s));
        MeshGenerator g = makeGenerator(s, true);
        ConstraintSegment conduit;
        conduit.path = {QPointF(70, 5), QPointF(72, 55)};
        conduit.marker = 100; conduit.tag = QStringLiteral("C1");
        g.addConstraintSegment(conduit);
        QVector<QVector<QPointF>> lines = s.lines;
        QVector<QPointF> hug;
        for (double x = 2.2; x < 97.8; x += 0.5) hug.append(QPointF(x, 2.3));   // 0.3 m inside the domain edge
        lines.append(hug);
        g.setTerrainBreaklines(lines);
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        const auto &kept = g.acceptedTerrainBreaklines();
        for (const auto &l : kept)
            for (const QPointF &p : l)
            {
                QVERIFY2(distToLines(p, {conduit.path}) >= 0.5, qPrintable(QStringLiteral("(%1,%2) near conduit").arg(p.x()).arg(p.y())));
                QVERIFY2(p.y() > 2.3 + 0.5 || std::abs(p.y() - 2.3) > 1e-6, "domain-hugging line kept");
            }
        // The conduit is still a boundary edge chain with its marker.
        int conduitEdges = 0;
        for (const MeshEdge &e : r.boundaryEdges) if (e.marker == 100) ++conduitEdges;
        QVERIFY(conduitEdges > 0);
    }

    void terrainLoopIsNeverAHoleOrBarrier()
    {
        // A closed terrain loop (the building) around a user hole ring and a
        // region seed outside it: the annulus between loop and hole is meshed,
        // and the region tag floods across the loop.
        Setup s;
        QVERIFY(prepare(s));
        MeshGenerator g = makeGenerator(s, true);
        ConstraintSegment hole;
        hole.path = {QPointF(43, 7), QPointF(47, 7), QPointF(47, 11), QPointF(43, 11), QPointF(43, 7)};
        g.addConstraintSegment(hole);
        g.addHole(QPointF(45, 9));
        RegionMarker rm;
        rm.xy = QPointF(80, 10);
        rm.tag = QStringLiteral("lawn");
        g.addRegion(rm);
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QVERIFY2(std::abs(meshArea(r) - (96.0 * 56.0 - 16.0)) < 1e-6, qPrintable(QString::number(meshArea(r), 'g', 17)));
        // Cells inside the building footprint (away from the hole) carry the tag.
        int inside = 0, tagged = 0;
        for (const MeshTriangle &c : r.triangles)
        {
            const QPointF ctr = cellGeom(r.vertices, c).centroid;
            if (ctr.x() > 33 && ctr.x() < 40 && ctr.y() > 5 && ctr.y() < 13)
            {
                ++inside;
                if (c.tag == QStringLiteral("lawn")) ++tagged;
            }
        }
        QVERIFY(inside > 0);
        QCOMPARE(tagged, inside);
    }

    void crowdedLinesKeepOnlyOne()
    {
        // Two parallel lines 0.3 m apart with a 0.5 m floor: only one survives.
        MeshGenerator g;
        g.setDomain(QPolygonF({QPointF(0, 0), QPointF(40, 0), QPointF(40, 20), QPointF(0, 20)}));
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 4.0;
        o.minCellSize = 0.5;
        g.setOptions(o);
        QVector<QPointF> a, b;
        for (double x = 5; x <= 35; x += 0.5) { a.append(QPointF(x, 10.0)); b.append(QPointF(x, 10.3)); }
        g.setTerrainBreaklines({a, b});
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QCOMPARE(g.acceptedTerrainBreaklines().size(), 1);
        QCOMPARE(pointsOffEdges(r, g.acceptedTerrainBreaklines()), 0);
    }

    void hairpinKeepsOneLeg()
    {
        // A two-pixel-wide response traced out and back: after simplification
        // it folds at the far end. One leg survives, the constraint set stays
        // valid, no sliver.
        MeshGenerator g;
        g.setDomain(QPolygonF({QPointF(0, 0), QPointF(40, 0), QPointF(40, 20), QPointF(0, 20)}));
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 4.0;
        o.minCellSize = 0.5;
        g.setOptions(o);
        QVector<QPointF> hairpin;
        for (double x = 5; x <= 30; x += 0.5) hairpin.append(QPointF(x, 10.0));
        for (double x = 30; x >= 5; x -= 0.5) hairpin.append(QPointF(x, 10.5));
        g.setTerrainBreaklines({hairpin});
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QCOMPARE(g.acceptedTerrainBreaklines().size(), 1);
        QCOMPARE(pointsOffEdges(r, g.acceptedTerrainBreaklines()), 0);
        QCOMPARE(computeGradingStats(r).cellsBelow10Deg, 0);
    }

    void sparseLineDoesNotWidenOthers()
    {
        // A two-point line 30 m long next to a dense line 4 m away: the dense
        // line keeps its own (small) cut-back distance and survives.
        MeshGenerator g;
        g.setDomain(QPolygonF({QPointF(0, 0), QPointF(60, 0), QPointF(60, 30), QPointF(0, 30)}));
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 4.0;
        o.minCellSize = 0.5;
        g.setOptions(o);
        QVector<QPointF> dense;
        for (double x = 10; x <= 50; x += 0.5) dense.append(QPointF(x, 12.0));
        const QVector<QPointF> sparse = {QPointF(15, 18), QPointF(45, 18)};
        g.setTerrainBreaklines({sparse, dense});
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        bool denseKept = false;
        for (const auto &l : g.acceptedTerrainBreaklines())
            for (const QPointF &p : l) if (std::abs(p.y() - 12.0) < 1e-9) denseKept = true;
        QVERIFY(denseKept);
    }

    void cityBlocksScale()
    {
        // 1 km² at 0.5 m pixels (4 M pixels) of 40 m blocks with curbs and
        // 8 m streets: every street between two blocks becomes a quad strip;
        // records the end-to-end cost of extraction + generation.
        const int cols = 2000, rows = 2000;
        std::vector<float> z(size_t(cols) * rows);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                z[size_t(r) * cols + c] = ((c % 80) < 16 || (r % 80) < 16) ? 0.0f : 0.16f;
        QElapsedTimer t;
        t.start();
        TerrainSizeField tf;
        TerrainSizeOptions to;
        to.tolerance = 0.1; to.maxLevel = 8; to.outLevel = 2;
        TerrainBreaklineExtractor ex;
        TerrainBreaklineOptions bo;
        bo.tolerance = 0.1;
        to.rowSink = [&](const float *row, int r, int cc, int rr) { if (r == 0) ex.begin(cc, rr, bo); ex.pushRow(row); };
        QVERIFY(tf.buildFromGrid(z.data(), cols, rows, to));
        const auto lines = toMesh(ex.finish());
        const qint64 msTerrain = t.restart();
        SizeField sf;
        SizeFieldOptions so;
        so.nearSize = 4.0; so.gradation = 0.5; so.maxSize = 16.0;
        so.areaFloor = 0.4330127018922193 * 1.0;
        so.terrainSizeAt = [&tf](double x, double y) { return tf.sizePixelsAt(x / kPx, y / kPx) * kPx; };
        so.steps = lines;
        QVERIFY(sf.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, so));
        MeshGenerator g;
        g.setDomain(QPolygonF({QPointF(1, 1), QPointF(999, 1), QPointF(999, 999), QPointF(1, 999)}));
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 16.0;
        o.minCellSize = 1.0;
        o.quadsBetweenBreaklines = true;
        g.setOptions(o);
        RefineHook h;
        h.targetAreaAt = [&sf](double x, double y) { return sf.targetAreaAt(x, y); };
        h.elevationAt = demElevation(&z, cols, rows);
        g.setRefineHook(h);
        g.setTerrainBreaklines(lines);
        const MeshResult r = g.generate();
        const qint64 msMesh = t.elapsed();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        const GradingStats gs = computeGradingStats(r);
        qInfo().noquote() << QStringLiteral("[feature] city 1 km²: terrain+lines %1 ms (%2 lines, %3 kept), mesh %4 ms, %5 cells (%6 quads, %7 street strips, %8 dropped), ratio max %9 p50 %10, min angle %11 (free triangles %12)")
                                 .arg(msTerrain).arg(lines.size()).arg(g.acceptedTerrainBreaklines().size()).arg(msMesh)
                                 .arg(r.triangles.size()).arg(r.quadCount()).arg(g.stats().breaklineStrips).arg(g.stats().stripsDropped)
                                 .arg(gs.ratioMax).arg(gs.ratioP50).arg(gs.minAngleDeg).arg(freeTriangleMinAngle(r));
        writeCsv(r, g.acceptedTerrainBreaklines(), QStringLiteral("city_strips"));
        QVERIFY(g.stats().breaklineStrips > 1000);
        QVERIFY(g.stats().stripsDropped < g.stats().breaklineStrips / 20);
        QCOMPARE(gs.cellsBelow10Deg, 0);
        QVERIFY2(gs.minAngleDeg >= 20.0, qPrintable(QStringLiteral("min angle %1").arg(gs.minAngleDeg)));
        QString why;
        const bool gradingOk = featureGradingOk(r, gs, &why);
        qInfo().noquote() << "[feature]   grading:" << why;
        QVERIFY2(gradingOk, qPrintable(why));
    }
};

QTEST_MAIN(TestMeshFeatureCapture)
#include "test_meshfeaturecapture.moc"
