/*!
 * \file   test_meshreorder.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * mesh::reorderMeshHilbert — permutation validity (same coordinate-keyed
 * multisets, attributes preserved, indices correctly remapped), locality
 * improvement on a scrambled structured mesh, and idempotence.
 */
#include <QtTest>
#include <QHash>
#include <QRandomGenerator>

#include "mesh/meshreorder.h"

#include <numeric>

using mesh::MeshResult;
using mesh::MeshTriangle;
using mesh::MeshVertex;

namespace {

/*! Structured N×N grid of squares split into two triangles each, with the
 *  triangle order SHUFFLED (seeded) to imitate Triangle's spatially random
 *  refinement order.  Vertex (i,j) index = j*(N+1)+i; z = x + 100*y so every
 *  vertex is uniquely identifiable by coordinates. */
MeshResult scrambledGrid(int N)
{
    MeshResult m;
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i)
        {
            MeshVertex v;
            v.xy  = QPointF(i, j);
            v.z   = i + 100.0 * j;
            v.tag = QStringLiteral("v%1_%2").arg(i).arg(j);
            m.vertices.append(v);
        }
    auto vid = [N](int i, int j) { return j * (N + 1) + i; };
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i)
        {
            MeshTriangle a;
            a.v0 = vid(i, j); a.v1 = vid(i + 1, j); a.v2 = vid(i, j + 1);
            a.tag = QStringLiteral("t%1_%2a").arg(i).arg(j);
            m.triangles.append(a);
            MeshTriangle b;
            b.v0 = vid(i + 1, j); b.v1 = vid(i + 1, j + 1); b.v2 = vid(i, j + 1);
            b.tag = QStringLiteral("t%1_%2b").arg(i).arg(j);
            m.triangles.append(b);
        }
    // Deterministic shuffle of the triangle order AND the vertex numbering —
    // Triangle's refinement output is spatially near-random in both.
    QRandomGenerator rng(424242u);
    for (int i = m.triangles.size() - 1; i > 0; --i)
        m.triangles.swapItemsAt(i, int(rng.bounded(quint32(i + 1))));

    QVector<int> vperm(m.vertices.size());
    std::iota(vperm.begin(), vperm.end(), 0);
    for (int i = vperm.size() - 1; i > 0; --i)
        std::swap(vperm[i], vperm[int(rng.bounded(quint32(i + 1)))]);
    QVector<MeshVertex> shuffled(m.vertices.size());
    for (int v = 0; v < m.vertices.size(); ++v)
        shuffled[vperm[v]] = m.vertices[v];
    m.vertices = shuffled;
    for (MeshTriangle &t : m.triangles)
    {
        t.v0 = vperm[t.v0];
        t.v1 = vperm[t.v1];
        t.v2 = vperm[t.v2];
    }

    // A few boundary edges + couplings to verify remapping (through the
    // vertex shuffle above, so they reference the shuffled numbering).
    mesh::MeshEdge e;
    e.v0 = vperm[vid(0, 0)]; e.v1 = vperm[vid(1, 0)]; e.marker = 7; e.tag = "edge0";
    m.boundaryEdges.append(e);
    mesh::CellCoupling c;
    c.tri = 3; c.nodeId = "J1"; c.cd = 0.5; c.area = 1.5;
    m.cellCouplings.append(c);
    m.ok = true;
    return m;
}

// Canonical signature of a triangle: sorted coordinate triple + tag.
QString triSignature(const MeshResult &m, const MeshTriangle &t)
{
    QStringList corners;
    for (int v : {t.v0, t.v1, t.v2})
        corners << QStringLiteral("%1,%2,%3")
                       .arg(m.vertices[v].xy.x())
                       .arg(m.vertices[v].xy.y())
                       .arg(m.vertices[v].z);
    corners.sort();
    return corners.join(QLatin1Char('|')) + QLatin1Char('#') + t.tag;
}

double signedArea(const MeshResult &m, const MeshTriangle &t)
{
    const QPointF &a = m.vertices[t.v0].xy;
    const QPointF &b = m.vertices[t.v1].xy;
    const QPointF &c = m.vertices[t.v2].xy;
    return 0.5 * ((b.x() - a.x()) * (c.y() - a.y())
                  - (c.x() - a.x()) * (b.y() - a.y()));
}

// Adjacency-based locality: mean |triA - triB| over shared (interior) edges.
double meanAdjacentTriDistance(const MeshResult &m)
{
    QHash<QPair<int, int>, int> firstTri;
    double sum = 0.0;
    qint64 n = 0;
    for (int t = 0; t < m.triangles.size(); ++t)
    {
        const MeshTriangle &tr = m.triangles[t];
        const int vs[3] = {tr.v0, tr.v1, tr.v2};
        for (int e = 0; e < 3; ++e)
        {
            const auto key = qMakePair(std::min(vs[e], vs[(e + 1) % 3]),
                                       std::max(vs[e], vs[(e + 1) % 3]));
            auto it = firstTri.constFind(key);
            if (it == firstTri.constEnd())
            {
                firstTri.insert(key, t);
            }
            else
            {
                sum += std::abs(t - it.value());
                ++n;
            }
        }
    }
    return n ? sum / double(n) : 0.0;
}

} // namespace

class TestMeshReorder : public QObject
{
    Q_OBJECT

private slots:

    void emptyMesh_noop()
    {
        MeshResult m;
        mesh::reorderMeshHilbert(&m);     // no crash
        mesh::reorderMeshHilbert(nullptr);
        QCOMPARE(m.triangles.size(), 0);
    }

    void permutationValidity()
    {
        const MeshResult before = scrambledGrid(20);
        MeshResult after = before;
        mesh::reorderMeshHilbert(&after);

        QCOMPARE(after.vertices.size(), before.vertices.size());
        QCOMPARE(after.triangles.size(), before.triangles.size());

        // Vertex multiset (coordinates + z + tag) unchanged.
        QStringList vb, va;
        for (const MeshVertex &v : before.vertices)
            vb << QStringLiteral("%1,%2,%3,%4")
                      .arg(v.xy.x()).arg(v.xy.y()).arg(v.z).arg(v.tag);
        for (const MeshVertex &v : after.vertices)
            va << QStringLiteral("%1,%2,%3,%4")
                      .arg(v.xy.x()).arg(v.xy.y()).arg(v.z).arg(v.tag);
        vb.sort(); va.sort();
        QCOMPARE(va, vb);

        // Triangle multiset (by corner coordinates + tag) unchanged.
        QStringList tb, ta;
        for (const MeshTriangle &t : before.triangles)
            tb << triSignature(before, t);
        for (const MeshTriangle &t : after.triangles)
            ta << triSignature(after, t);
        tb.sort(); ta.sort();
        QCOMPARE(ta, tb);

        // Orientation preserved for every triangle.
        for (const MeshTriangle &t : after.triangles)
            QVERIFY(signedArea(after, t) > 0.0);

        // Boundary edge remapped to the same coordinates.
        QCOMPARE(after.boundaryEdges.size(), 1);
        QCOMPARE(after.vertices[after.boundaryEdges[0].v0].xy, QPointF(0, 0));
        QCOMPARE(after.vertices[after.boundaryEdges[0].v1].xy, QPointF(1, 0));
        QCOMPARE(after.boundaryEdges[0].marker, 7);

        // Cell coupling follows its triangle.
        QCOMPARE(after.cellCouplings.size(), 1);
        const QString sigBefore =
            triSignature(before, before.triangles[before.cellCouplings[0].tri]);
        const QString sigAfter =
            triSignature(after, after.triangles[after.cellCouplings[0].tri]);
        QCOMPARE(sigAfter, sigBefore);
    }

    void localityImproves()
    {
        const MeshResult before = scrambledGrid(40);
        MeshResult after = before;
        mesh::reorderMeshHilbert(&after);

        const double adjBefore = meanAdjacentTriDistance(before);
        const double adjAfter  = meanAdjacentTriDistance(after);
        QVERIFY2(adjAfter < adjBefore * 0.25,
                 qPrintable(QStringLiteral("adjacency distance %1 -> %2")
                                .arg(adjBefore).arg(adjAfter)));

        const double spreadBefore = mesh::meanVertexIndexSpread(before);
        const double spreadAfter  = mesh::meanVertexIndexSpread(after);
        QVERIFY2(spreadAfter < spreadBefore,
                 qPrintable(QStringLiteral("vertex spread %1 -> %2")
                                .arg(spreadBefore).arg(spreadAfter)));
    }

    void idempotent()
    {
        MeshResult once = scrambledGrid(15);
        mesh::reorderMeshHilbert(&once);
        MeshResult twice = once;
        mesh::reorderMeshHilbert(&twice);

        QCOMPARE(twice.triangles.size(), once.triangles.size());
        for (int t = 0; t < once.triangles.size(); ++t)
        {
            QCOMPARE(twice.triangles[t].v0, once.triangles[t].v0);
            QCOMPARE(twice.triangles[t].v1, once.triangles[t].v1);
            QCOMPARE(twice.triangles[t].v2, once.triangles[t].v2);
        }
        for (int v = 0; v < once.vertices.size(); ++v)
            QCOMPARE(twice.vertices[v].xy, once.vertices[v].xy);
    }
};

QTEST_MAIN(TestMeshReorder)
#include "test_meshreorder.moc"
