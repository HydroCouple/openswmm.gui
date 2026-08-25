/*!
 * \file   test_meshminsizecleanup.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Post-generation sliver collapse
 * (MIN_CELL_SIZE_TESTING_HANDOFF_2026-08-17.md Phase 5).
 *
 * The protection rules are the point of this pass, not the collapsing: a mesh
 * that lost a constrained edge, a coupled cell, or a triangle's orientation is
 * worse than a mesh with slivers in it.  Every test below therefore checks
 * what SURVIVED as hard as it checks what went away.
 */
#include <QtTest>
#include <QCryptographicHash>
#include <QPointF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QVector>

#include "mesh/meshminsizecleanup.h"

#include <cmath>
#include <cstdio>

using mesh::CellCoupling;
using mesh::CleanupPolicy;
using mesh::CleanupReport;
using mesh::MeshEdge;
using mesh::MeshResult;
using mesh::MeshTriangle;
using mesh::MeshVertex;

namespace {

MeshVertex vtx(double x, double y, int marker = 0,
               const QString &tag = {}, const QString &coupled = {})
{
    MeshVertex v;
    v.xy = QPointF(x, y);
    v.marker = marker;
    v.tag = tag;
    v.coupledNode = coupled;
    return v;
}

MeshTriangle tri(int a, int b, int c)
{
    MeshTriangle t; t.v0 = a; t.v1 = b; t.v2 = c; return t;
}

double signedArea(const MeshResult &m, const MeshTriangle &t)
{
    const QPointF &a = m.vertices[t.v0].xy;
    const QPointF &b = m.vertices[t.v1].xy;
    const QPointF &c = m.vertices[t.v2].xy;
    return 0.5 * ((b.x() - a.x()) * (c.y() - a.y())
                - (b.y() - a.y()) * (c.x() - a.x()));
}

bool allCounterClockwise(const MeshResult &m)
{
    for (const MeshTriangle &t : m.triangles)
        if (signedArea(m, t) <= 0.0) return false;
    return true;
}

bool indicesConsistent(const MeshResult &m)
{
    const int nv = m.vertices.size();
    for (const MeshTriangle &t : m.triangles)
    {
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) return false;
        if (t.v0 >= nv || t.v1 >= nv || t.v2 >= nv) return false;
        if (t.v0 == t.v1 || t.v1 == t.v2 || t.v0 == t.v2) return false;
    }
    for (const MeshEdge &e : m.boundaryEdges)
        if (e.v0 < 0 || e.v1 < 0 || e.v0 >= nv || e.v1 >= nv) return false;
    for (const CellCoupling &c : m.cellCouplings)
        if (c.tri < 0 || c.tri >= m.triangles.size()) return false;
    return true;
}

/*! boundaryEdges as coordinate pairs, so "preserved exactly" survives the
 *  renumbering the pass performs. */
QSet<QString> edgeCoordSet(const MeshResult &m)
{
    QSet<QString> s;
    for (const MeshEdge &e : m.boundaryEdges)
    {
        const QPointF &a = m.vertices[e.v0].xy;
        const QPointF &b = m.vertices[e.v1].xy;
        QString k = QStringLiteral("%1,%2|%3,%4|%5|%6")
                        .arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y())
                        .arg(e.marker).arg(e.tag);
        QString r = QStringLiteral("%1,%2|%3,%4|%5|%6")
                        .arg(b.x()).arg(b.y()).arg(a.x()).arg(a.y())
                        .arg(e.marker).arg(e.tag);
        s.insert(k < r ? k : r);
    }
    return s;
}

bool meshesIdentical(const MeshResult &a, const MeshResult &b)
{
    if (a.vertices.size() != b.vertices.size()) return false;
    if (a.triangles.size() != b.triangles.size()) return false;
    if (a.boundaryEdges.size() != b.boundaryEdges.size()) return false;
    if (a.cellCouplings.size() != b.cellCouplings.size()) return false;
    for (int i = 0; i < a.vertices.size(); ++i)
    {
        if (a.vertices[i].xy != b.vertices[i].xy) return false;
        if (a.vertices[i].z != b.vertices[i].z) return false;
        if (a.vertices[i].marker != b.vertices[i].marker) return false;
        if (a.vertices[i].tag != b.vertices[i].tag) return false;
        if (a.vertices[i].coupledNode != b.vertices[i].coupledNode) return false;
    }
    for (int i = 0; i < a.triangles.size(); ++i)
    {
        if (a.triangles[i].v0 != b.triangles[i].v0) return false;
        if (a.triangles[i].v1 != b.triangles[i].v1) return false;
        if (a.triangles[i].v2 != b.triangles[i].v2) return false;
        if (a.triangles[i].tag != b.triangles[i].tag) return false;
    }
    for (int i = 0; i < a.boundaryEdges.size(); ++i)
        if (a.boundaryEdges[i].v0 != b.boundaryEdges[i].v0
            || a.boundaryEdges[i].v1 != b.boundaryEdges[i].v1
            || a.boundaryEdges[i].marker != b.boundaryEdges[i].marker) return false;
    for (int i = 0; i < a.cellCouplings.size(); ++i)
        if (a.cellCouplings[i].tri != b.cellCouplings[i].tri
            || a.cellCouplings[i].nodeId != b.cellCouplings[i].nodeId) return false;
    return true;
}

/*!
 * \brief A square patch of \p n x \p n cells of side \p s, split into
 *        counter-clockwise triangles, with the outer ring recorded as
 *        boundaryEdges (as Triangle would hand it back).
 *
 * Interior vertices carry no marker, tag, or coupled node, so the interior is
 * the only place cleanup is allowed to act — which is exactly the real
 * situation the pass is designed for.
 */
MeshResult grid(int n, double s);

/*! A grid whose every interior edge is a collapse candidate and whose axis
 *  edges are all EXACTLY the same length.  Ties are the whole point: they are
 *  what a length-only sort cannot order, so this is the shape that exposes a
 *  hash-order dependence.  Used by the cross-process determinism test. */
MeshResult tiedGrid()
{
    return grid(10, 1.0);
}

MeshResult grid(int n, double s)
{
    MeshResult m;
    m.ok = true;
    const int side = n + 1;
    for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i)
        {
            const bool edge = (i == 0 || j == 0 || i == n || j == n);
            m.vertices.append(vtx(i * s, j * s, edge ? 1 : 0));
        }
    auto id = [side](int i, int j) { return j * side + i; };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
        {
            m.triangles.append(tri(id(i, j), id(i + 1, j), id(i + 1, j + 1)));
            m.triangles.append(tri(id(i, j), id(i + 1, j + 1), id(i, j + 1)));
        }
    for (int i = 0; i < n; ++i)
    {
        MeshEdge e;
        e.marker = 1; e.tag = QStringLiteral("domain");
        e.v0 = id(i, 0);     e.v1 = id(i + 1, 0);     m.boundaryEdges.append(e);
        e.v0 = id(i, n);     e.v1 = id(i + 1, n);     m.boundaryEdges.append(e);
        e.v0 = id(0, i);     e.v1 = id(0, i + 1);     m.boundaryEdges.append(e);
        e.v0 = id(n, i);     e.v1 = id(n, i + 1);     m.boundaryEdges.append(e);
    }
    return m;
}

} // namespace

class TestMeshMinSizeCleanup : public QObject
{
    Q_OBJECT

private slots:

    void disabledPolicyIsExactNoOp()
    {
        const MeshResult base = grid(4, 10.0);
        MeshResult m = base;
        CleanupPolicy p;                       // minCellSize stays 0
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));
        QVERIFY(!r.abandoned);
        QCOMPARE(r.edgesCollapsed, 0);
        QVERIFY(meshesIdentical(m, base));
    }

    void emptyMeshIsHandled()
    {
        MeshResult m;
        CleanupPolicy p; p.minCellSize = 5.0;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));
        QVERIFY(m.triangles.isEmpty());
    }

    void nothingBelowThresholdIsANoOp()
    {
        const MeshResult base = grid(4, 10.0);
        MeshResult m = base;
        CleanupPolicy p; p.minCellSize = 1.0;  // threshold 0.35, cells are 10
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));
        QCOMPARE(r.edgesCollapsed, 0);
        QVERIFY(meshesIdentical(m, base));
    }

    /*! An interior sliver: one free vertex pulled hard toward its neighbour. */
    void interiorSliverIsCollapsed()
    {
        MeshResult m = grid(4, 10.0);
        const int side = 5;
        const int a = 2 * side + 2;            // (20,20), interior
        const int b = 2 * side + 3;            // (30,20), interior
        QCOMPARE(m.vertices[a].marker, 0);
        QCOMPARE(m.vertices[b].marker, 0);
        m.vertices[b].xy = QPointF(20.2, 20.0);   // 0.2 apart

        const int trisBefore  = m.triangles.size();
        const int edgesBefore = m.boundaryEdges.size();
        const QSet<QString> bEdges = edgeCoordSet(m);
        QVERIFY(allCounterClockwise(m));

        CleanupPolicy p; p.minCellSize = 2.0;   // threshold 0.7 > 0.2
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));

        QVERIFY(!r.abandoned);
        QVERIFY2(r.edgesCollapsed >= 1, "the sliver edge was not collapsed");
        QVERIFY(r.cellsRemoved >= 1);
        QVERIFY(m.triangles.size() < trisBefore);
        QVERIFY(indicesConsistent(m));
        QVERIFY2(allCounterClockwise(m), "a triangle flipped orientation");
        QCOMPARE(m.boundaryEdges.size(), edgesBefore);
        QCOMPARE(edgeCoordSet(m), bEdges);      // preserved EXACTLY (§2.3)
        QVERIFY(r.minAreaAfter > r.minAreaBefore);
    }

    /*! A short edge listed in boundaryEdges is never collapsed. */
    void constrainedEdgeIsProtected()
    {
        MeshResult m = grid(4, 10.0);
        const int side = 5;
        const int a = 2 * side + 2, b = 2 * side + 3;
        m.vertices[b].xy = QPointF(20.2, 20.0);

        MeshEdge ce;                            // a conduit alignment
        ce.v0 = a; ce.v1 = b; ce.marker = 77; ce.tag = QStringLiteral("C1");
        m.boundaryEdges.append(ce);

        const MeshResult base = m;
        CleanupPolicy p; p.minCellSize = 2.0;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));

        QCOMPARE(r.edgesCollapsed, 0);
        QVERIFY2(r.skippedProtected >= 1, "protected edge was not reported");
        QVERIFY(!r.unfixable.isEmpty());
        // Centroid of the protected edge is reported so it is actionable.
        const QPointF mid(20.1, 20.0);
        bool found = false;
        for (const QPointF &q : std::as_const(r.unfixable))
            if (std::hypot(q.x() - mid.x(), q.y() - mid.y()) < 1e-9) found = true;
        QVERIFY(found);
        QVERIFY(meshesIdentical(m, base));
    }

    /*! Marker, tag, and coupledNode each independently pin a vertex. */
    void identityVerticesSurvive_data()
    {
        QTest::addColumn<int>("marker");
        QTest::addColumn<QString>("tag");
        QTest::addColumn<QString>("coupled");
        QTest::newRow("marker")  << 5  << QString()               << QString();
        QTest::newRow("tag")     << 0  << QStringLiteral("J1")    << QString();
        QTest::newRow("coupled") << 0  << QString() << QStringLiteral("N42");
    }

    void identityVerticesSurvive()
    {
        QFETCH(int, marker);
        QFETCH(QString, tag);
        QFETCH(QString, coupled);

        MeshResult m = grid(4, 10.0);
        const int side = 5;
        const int a = 2 * side + 2, b = 2 * side + 3;
        m.vertices[b].xy = QPointF(20.2, 20.0);
        m.vertices[b].marker      = marker;
        m.vertices[b].tag         = tag;
        m.vertices[b].coupledNode = coupled;

        const MeshResult base = m;
        CleanupPolicy p; p.minCellSize = 2.0;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));

        QCOMPARE(r.edgesCollapsed, 0);
        QVERIFY(r.skippedProtected >= 1);
        QVERIFY(meshesIdentical(m, base));
    }

    /*! cellCouplings survive, and their triangle indices are remapped. */
    void cellCouplingsArePreservedAndRemapped()
    {
        MeshResult m = grid(4, 10.0);
        const int side = 5;
        const int b = 2 * side + 3;
        m.vertices[b].xy = QPointF(20.2, 20.0);

        // Couple a cell far from the collapse so it is not itself removed.
        CellCoupling cc;
        cc.tri = 0; cc.nodeId = QStringLiteral("J9"); cc.cd = 0.7; cc.area = 3.0;
        m.cellCouplings.append(cc);
        const QPointF coupledCentroid(
            (m.vertices[m.triangles[0].v0].xy.x()
             + m.vertices[m.triangles[0].v1].xy.x()
             + m.vertices[m.triangles[0].v2].xy.x()) / 3.0,
            (m.vertices[m.triangles[0].v0].xy.y()
             + m.vertices[m.triangles[0].v1].xy.y()
             + m.vertices[m.triangles[0].v2].xy.y()) / 3.0);

        CleanupPolicy p; p.minCellSize = 2.0;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));
        QVERIFY(!r.abandoned);
        QVERIFY(r.edgesCollapsed >= 1);

        QCOMPARE(m.cellCouplings.size(), 1);
        QCOMPARE(m.cellCouplings[0].nodeId, QStringLiteral("J9"));
        QCOMPARE(m.cellCouplings[0].cd, 0.7);
        QVERIFY(m.cellCouplings[0].tri >= 0);
        QVERIFY(m.cellCouplings[0].tri < m.triangles.size());

        // The remap must point at the SAME cell, not merely a valid one.
        const MeshTriangle &t = m.triangles[m.cellCouplings[0].tri];
        const QPointF c((m.vertices[t.v0].xy.x() + m.vertices[t.v1].xy.x()
                         + m.vertices[t.v2].xy.x()) / 3.0,
                        (m.vertices[t.v0].xy.y() + m.vertices[t.v1].xy.y()
                         + m.vertices[t.v2].xy.y()) / 3.0);
        QVERIFY(std::hypot(c.x() - coupledCentroid.x(),
                           c.y() - coupledCentroid.y()) < 1e-9);
    }

    /*! Link condition: collapsing would create a non-manifold vertex.
     *
     * Two triangles sharing ONLY the short edge's endpoints — no shared cell —
     * so nbr(a) INTERSECT nbr(b) contains a vertex that is not opposite the
     * edge.  Merging a and b would pinch the surface at that vertex. */
    void linkConditionFailureIsDeclined()
    {
        MeshResult m;
        m.ok = true;
        //  a=(0,0)  b=(0.2,0)  are the short edge.
        //  c above and d below are each shared by both a and b, but the two
        //  triangles a-b-c and a-b-d are the edge's own cells; e is a third
        //  common neighbour reached WITHOUT sharing a cell with the edge.
        m.vertices = {vtx(0, 0), vtx(0.2, 0), vtx(0.1, 1.0), vtx(0.1, -1.0),
                      vtx(-2.0, 0.0), vtx(-1.0, 1.5), vtx(-1.0, -1.5)};
        const int a = 0, b = 1, c = 2, d = 3, e = 4, f = 5, gg = 6;
        m.triangles = {tri(a, b, c), tri(b, a, d),
                       tri(a, c, f), tri(a, f, e),        // a -- e
                       tri(a, e, gg), tri(a, gg, d),
                       tri(b, c, f)};                     // b -- f, no cell with a-b
        // Make every triangle counter-clockwise so the orientation guard is
        // not what declines the collapse.
        for (MeshTriangle &t : m.triangles)
            if (signedArea(m, t) < 0.0) std::swap(t.v1, t.v2);
        QVERIFY(allCounterClockwise(m));

        const MeshResult base = m;
        CleanupPolicy p; p.minCellSize = 2.0;   // threshold 0.7 > 0.2
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));

        QCOMPARE(r.edgesCollapsed, 0);
        QVERIFY2(r.skippedProtected >= 1, "link-condition failure not reported");
        QVERIFY(meshesIdentical(m, base));
    }

    /*! An orientation flip declines the collapse rather than committing it. */
    void orientationFlipIsDeclined()
    {
        MeshResult m;
        m.ok = true;
        // A short edge whose midpoint lies OUTSIDE a neighbouring triangle:
        // moving both endpoints there inverts that triangle.
        m.vertices = {vtx(0.0, 0.0), vtx(0.30, 0.0),
                      vtx(0.15, 2.0), vtx(0.15, -2.0),
                      vtx(0.155, 0.0005)};
        const int a = 0, b = 1, c = 2, d = 3, e = 4;
        m.triangles = {tri(a, b, c), tri(b, a, d),
                       tri(a, e, c), tri(e, b, c),
                       tri(a, d, e), tri(e, d, b)};
        for (MeshTriangle &t : m.triangles)
            if (signedArea(m, t) < 0.0) std::swap(t.v1, t.v2);
        QVERIFY(allCounterClockwise(m));

        const MeshResult base = m;
        CleanupPolicy p; p.minCellSize = 10.0;
        CleanupReport r;
        const bool ok = mesh::collapseSubScaleCells(&m, p, &r);

        // Whatever it decides, it must never leave an inverted triangle.
        QVERIFY2(allCounterClockwise(m), "cleanup produced an inverted triangle");
        QVERIFY(indicesConsistent(m));
        if (!ok) QVERIFY(meshesIdentical(m, base));   // rollback is byte-exact
    }

    /*! The transactional rollback, forced through the validity gate rather
     *  than through a per-edge guard.
     *
     * A coupling whose cell is one of the two the collapse removes cannot be
     * remapped, so the pass loses it.  Losing a 1D->2D coupling silently would
     * be far worse than leaving a sliver, so the whole pass is rolled back. */
    void lostCouplingRollsBackTheWholePass()
    {
        MeshResult m = grid(4, 10.0);
        const int side = 5;
        const int a = 2 * side + 2, b = 2 * side + 3;
        m.vertices[b].xy = QPointF(20.2, 20.0);

        // Find a cell that the collapse of edge (a,b) will delete: one that
        // has BOTH endpoints.
        int doomed = -1;
        for (int i = 0; i < m.triangles.size(); ++i)
        {
            const MeshTriangle &t = m.triangles[i];
            const bool hasA = (t.v0 == a || t.v1 == a || t.v2 == a);
            const bool hasB = (t.v0 == b || t.v1 == b || t.v2 == b);
            if (hasA && hasB) { doomed = i; break; }
        }
        QVERIFY(doomed >= 0);

        CellCoupling cc;
        cc.tri = doomed; cc.nodeId = QStringLiteral("J7");
        m.cellCouplings.append(cc);

        const MeshResult base = m;
        CleanupPolicy p; p.minCellSize = 2.0;
        CleanupReport r;
        QVERIFY2(!mesh::collapseSubScaleCells(&m, p, &r),
                 "a lost coupling must abandon the pass");
        QVERIFY(r.abandoned);
        QVERIFY2(meshesIdentical(m, base), "rollback was not byte-identical");
        QCOMPARE(m.cellCouplings.size(), 1);
        QVERIFY(r.summary().contains(QStringLiteral("ABANDONED")));
    }

    /*! A hand-built mesh whose indices are already invalid is not "repaired" —
     *  the pass declares it not ours and leaves it alone. */
    void invalidInputIsLeftAlone()
    {
        MeshResult m;
        m.ok = true;
        m.vertices = {vtx(0, 0), vtx(1, 0), vtx(0, 1)};
        m.triangles = {tri(0, 1, 5)};             // out of range
        const MeshResult base = m;

        CleanupPolicy p; p.minCellSize = 5.0;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));
        QVERIFY(!r.abandoned);
        QVERIFY(meshesIdentical(m, base));
    }

    /*!
     * The child half of \ref collapseIsDeterministicAcrossProcesses.  Runs a
     * cleanup whose candidate edges are massively tied in length and prints a
     * hash of the survivors.  Harmless when the whole suite runs.
     */
    void determinismChild()
    {
        MeshResult m = tiedGrid();
        CleanupPolicy p; p.minCellSize = 4.0;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));

        QCryptographicHash h(QCryptographicHash::Sha256);
        for (const MeshVertex &v : m.vertices)
        {
            const double xy[2] = {v.xy.x(), v.xy.y()};
            h.addData(QByteArrayView(reinterpret_cast<const char *>(xy),
                                     sizeof(xy)));
        }
        for (const MeshTriangle &t : m.triangles)
        {
            const int idx[3] = {t.v0, t.v1, t.v2};
            h.addData(QByteArrayView(reinterpret_cast<const char *>(idx),
                                     sizeof(idx)));
        }
        std::printf("DETERMINISM_HASH %s %d\n",
                    h.result().toHex().constData(), r.edgesCollapsed);
        std::fflush(stdout);
    }

    /*!
     * Cleanup must produce the same mesh on every run of the same input.
     *
     * This MUST be a cross-process test.  Qt 6 randomises the QHash seed once
     * per process, so a same-process comparison sees one hash order twice and
     * passes no matter what — the trap the sibling
     * test_pslgminsize::condition_isDeterministic still sits in.  Nor can the
     * seed be pinned to compare against: QT_HASH_SEED accepts only 0, and any
     * other value is ignored AND still disables randomisation, so every
     * "different seed" run is the same run.
     *
     * The defect this guards was real and shipped: candidate edges were
     * gathered from a QHash and sorted on squared length alone.  A mesh
     * carries many exactly-congruent short edges, std::sort is not stable, and
     * the first collapse of a tied pair blocks its neighbours via `touched` —
     * so the collapse set, and the mesh, differed run to run.  Measured on a
     * real model: five runs, five different meshes.
     */
    void collapseIsDeterministicAcrossProcesses()
    {
        const QString exe = QCoreApplication::applicationFilePath();
        QStringList seen;
        for (int run = 0; run < 4; ++run)
        {
            QProcess child;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.remove(QStringLiteral("QT_HASH_SEED"));   // randomised, deliberately
            child.setProcessEnvironment(env);
            child.start(exe, {QStringLiteral("determinismChild")});
            QVERIFY2(child.waitForFinished(60000), "child probe timed out");
            const QString out = QString::fromLocal8Bit(child.readAllStandardOutput());

            QString line;
            const QStringList lines = out.split(QLatin1Char('\n'));
            for (const QString &l : lines)
                if (l.startsWith(QStringLiteral("DETERMINISM_HASH"))) line = l.trimmed();
            QVERIFY2(!line.isEmpty(),
                     qPrintable(QStringLiteral("no hash from child; output was: %1")
                                    .arg(out.left(500))));
            seen.append(line);
        }
        // The collapse must actually have done something, or the hashes agree
        // trivially and this test proves nothing.
        QVERIFY2(!seen.first().endsWith(QStringLiteral(" 0")),
                 qPrintable(QStringLiteral("cleanup collapsed nothing: %1")
                                .arg(seen.first())));
        for (const QString &s : seen)
            QCOMPARE(s, seen.first());
    }

    // ── Opt-in: aggressive identity collapse ────────────────────────────

    /*! Regression pin: with the opt-in off, the pass is exactly what it was
     *  and every new counter reads zero. */
    void aggressiveModeOffReproducesJuniorPassExactly()
    {
        MeshResult base = grid(4, 10.0);
        base.vertices[2 * 5 + 3].xy = QPointF(20.2, 20.0);   // one sliver
        MeshResult withOff = base, plain = base;

        CleanupPolicy pOff; pOff.minCellSize = 2.0;
        pOff.allowIdentityCollapse = false;
        CleanupReport rOff;
        QVERIFY(mesh::collapseSubScaleCells(&withOff, pOff, &rOff));

        CleanupPolicy pDefault; pDefault.minCellSize = 2.0;
        CleanupReport rDefault;
        QVERIFY(mesh::collapseSubScaleCells(&plain, pDefault, &rDefault));
        QVERIFY2(rDefault.edgesCollapsed > 0,
                 "the fixture must actually collapse something, or this pins nothing");

        QVERIFY(meshesIdentical(withOff, plain));
        QCOMPARE(rOff.identityAbsorptions,      0);
        QCOMPARE(rOff.interiorConstraintsLost,  0);
        QCOMPARE(rOff.ringEdgesShortened,       0);
        QCOMPARE(rOff.identityConflictsSkipped, 0);
        QCOMPARE(rOff.ringGuardSkipped,         0);
        QCOMPARE(rOff.candidatesDropped,        0);
        QVERIFY(!rOff.exhaustedPasses);
        QVERIFY(!rOff.summary().contains(QStringLiteral("aggressive")));
    }

    /*!
     * The subtlety that makes the whole opt-in work.  MeshGenerator stamps
     * EVERY domain/hole boundary vertex with the same generic boundary marker
     * whether or not it couples anything.  If a bare marker counted as an
     * identity, every pair of adjacent ring vertices would look like "two
     * distinct identities" and aggressive mode would refuse to shrink anything.
     */
    void aggressiveMode_bareMarkerWithoutTagIsNotTreatedAsIdentity()
    {
        // Two triangles, one very short INTERIOR edge, both its endpoints
        // carrying a bare marker and no tag.
        MeshResult m;
        m.ok = true;
        m.vertices = { vtx(0, 0, 1), vtx(10, 0, 1),
                       vtx(5, 5.0, 1), vtx(5, 5.05, 1) };
        m.triangles = { tri(0, 1, 2), tri(0, 3, 1), tri(0, 2, 3), tri(1, 3, 2) };

        CleanupPolicy p; p.minCellSize = 1.0;
        p.allowIdentityCollapse = true;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));
        // Without the collapse actually happening this asserts nothing.
        QVERIFY2(r.edgesCollapsed > 0,
                 "bare-marker vertices must be free to collapse");
        QCOMPARE(r.identityConflictsSkipped, 0);   // the actual assertion
    }

    /*! One identity endpoint absorbs a plain neighbour and does NOT move.  Its
     *  position must be BIT-identical, not merely close: it is a coupling
     *  location, so averaging would drag it off the node it represents. */
    void aggressiveMode_identityVertexAbsorbsPlainNeighbourWithoutMoving()
    {
        MeshResult m;
        m.ok = true;
        const QPointF anchor(5.0, 5.0);
        m.vertices = { vtx(0, 0, 1), vtx(10, 0, 1),
                       vtx(anchor.x(), anchor.y(), 3, QStringLiteral("J1")),
                       vtx(5.02, 5.03, 0) };
        m.triangles = { tri(0, 1, 2), tri(0, 3, 1), tri(0, 2, 3), tri(1, 3, 2) };

        CleanupPolicy p; p.minCellSize = 1.0;
        p.allowIdentityCollapse = true;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));
        // Guard against a vacuous pass: if nothing collapsed, "the identity
        // did not move" is true for the boring reason.
        QVERIFY2(r.identityAbsorptions > 0,
                 "the plain neighbour must actually be absorbed");

        int found = -1;
        for (int i = 0; i < m.vertices.size(); ++i)
            if (m.vertices[i].tag == QStringLiteral("J1")) found = i;
        QVERIFY2(found >= 0, "the identity must survive the collapse");
        QCOMPARE(m.vertices[found].xy, anchor);    // bit-identical
    }

    /*! Two distinct identities are never fused, opt-in or not — the same rule
     *  pslgminsize enforces on the input side. */
    void aggressiveMode_twoDistinctIdentitiesNeverMerge()
    {
        MeshResult m;
        m.ok = true;
        m.vertices = { vtx(0, 0, 1), vtx(10, 0, 1),
                       vtx(5.0,  5.0,  3, QStringLiteral("J1")),
                       vtx(5.02, 5.03, 4, QStringLiteral("J2")) };
        m.triangles = { tri(0, 1, 2), tri(0, 3, 1), tri(0, 2, 3), tri(1, 3, 2) };
        const MeshResult before = m;

        CleanupPolicy p; p.minCellSize = 1.0;
        p.allowIdentityCollapse = true;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));

        QVERIFY2(r.identityConflictsSkipped >= 1,
                 "the J1/J2 pair must be declined, and counted");
        QCOMPARE(r.identityAbsorptions, 0);
        QVERIFY(meshesIdentical(m, before));
    }

    /*! A ring may shorten, but never below minRingVertices — a boundary with
     *  two vertices bounds no area, the mesh-side twin of the PSLG fold. */
    void aggressiveMode_ringNeverShortensBelowMinRingVertices()
    {
        // A 4-vertex square, two triangles, with two SHORT opposite ring edges.
        MeshResult m;
        m.ok = true;
        m.vertices = { vtx(0, 0, 1), vtx(0.1, 0, 1),
                       vtx(0.1, 10, 1), vtx(0, 10, 1) };
        m.triangles = { tri(0, 1, 2), tri(0, 2, 3) };
        CleanupPolicy p;
        p.minCellSize = 1.0;
        p.allowIdentityCollapse = true;
        p.minRingVertices = 3;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));

        // Whatever it did, it must never have left fewer than 3 ring vertices.
        QVERIFY2(m.vertices.size() >= 3,
                 qPrintable(QStringLiteral("ring fell to %1 vertices")
                                .arg(m.vertices.size())));
        QVERIFY2(r.ringGuardSkipped >= 1,
                 "the second short ring edge must be declined by the guard");
    }

    void reportSummaryIsInformative()
    {
        MeshResult m = grid(4, 10.0);
        m.vertices[2 * 5 + 3].xy = QPointF(20.2, 20.0);
        CleanupPolicy p; p.minCellSize = 2.0;
        CleanupReport r;
        QVERIFY(mesh::collapseSubScaleCells(&m, p, &r));
        const QString s = r.summary();
        QVERIFY(!s.isEmpty());
        QVERIFY(s.contains(QStringLiteral("collapsed")));
        QVERIFY(s.contains(QStringLiteral("min area")));
    }
};

QTEST_MAIN(TestMeshMinSizeCleanup)
#include "test_meshminsizecleanup.moc"
