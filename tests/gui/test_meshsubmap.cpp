/*!
 * \file   test_meshsubmap.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quad redesign gate 7 (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md
 * §4.3, §7.7) — QtTest coverage for mesh/meshsubmap.h: rectilinearity test
 * (L in any frame, blob rejected), the L (75 unit squares, 40 boundary
 * segments) and U (125) decompositions, a slightly noisy L still meshed, the
 * blob rejected with "not rectilinear", and h <= 0 rejected.
 */
#include <QtTest>
#include <QHash>
#include <QPair>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

#include <cmath>
#include <random>

#include "mesh/meshcellgeom.h"
#include "mesh/meshpatch.h"
#include "mesh/meshquadmerge.h"
#include "mesh/meshquadquality.h"
#include "mesh/meshquadregion.h"
#include "mesh/meshresult.h"
#include "mesh/meshsubmap.h"

using namespace mesh;

namespace {

QVector<MeshVertex> asVertices(const QVector<QPointF> &xy)
{
    QVector<MeshVertex> v;
    for (const QPointF &p : xy) { MeshVertex mv; mv.xy = p; v.append(mv); }
    return v;
}

/*! 10×10 square with a 5×5 notch at the top-right, CCW. */
QPolygonF lShape()
{
    return QPolygonF{QPointF(0, 0), QPointF(10, 0), QPointF(10, 5), QPointF(5, 5), QPointF(5, 10), QPointF(0, 10)};
}

/*! 15×10 with a 5×5 notch in the top middle, CCW. */
QPolygonF uShape()
{
    return QPolygonF{QPointF(0, 0), QPointF(15, 0), QPointF(15, 10), QPointF(10, 10),
                     QPointF(10, 5), QPointF(5, 5), QPointF(5, 10), QPointF(0, 10)};
}

QPolygonF blob()
{
    return QPolygonF{QPointF(0, 0), QPointF(10, 1), QPointF(12, 6), QPointF(6, 11), QPointF(-1, 5)};
}

QPolygonF rotated(const QPolygonF &p, double deg, const QPointF &shift = QPointF())
{
    const double a = deg * M_PI / 180.0, c = std::cos(a), s = std::sin(a);
    QPolygonF out;
    for (const QPointF &q : p) out << QPointF(q.x() * c - q.y() * s, q.x() * s + q.y() * c) + shift;
    return out;
}

double minSJ(const PatchMesh &pm)
{
    const QVector<MeshVertex> V = asVertices(pm.xy);
    double mn = 2.0;
    for (const MeshTriangle &q : pm.quads) mn = std::min(mn, quadQuality(V, q).scaledJacobian);
    return mn;
}

/*! Every boundary segment is a side of exactly one quad and the segments
 *  form one closed loop covering the ring's perimeter. */
void checkBoundary(const PatchMesh &pm, double perimeter)
{
    QHash<QPair<int, int>, int> edgeUse;
    for (const MeshTriangle &q : pm.quads)
        for (int k = 0; k < 4; ++k)
        {
            int a = 0, b = 0;
            edgeEndpoints(q, k, a, b);
            ++edgeUse[edgeKey(a, b)];
        }
    double len = 0.0;
    QHash<int, int> degree;
    for (const auto &s : pm.boundarySegments)
    {
        QCOMPARE(edgeUse.value(edgeKey(s.first, s.second), 0), 1);
        len += std::hypot(pm.xy[s.first].x() - pm.xy[s.second].x(), pm.xy[s.first].y() - pm.xy[s.second].y());
        ++degree[s.first];
        ++degree[s.second];
    }
    QVERIFY(std::abs(len - perimeter) < 1e-6);
    for (auto it = degree.cbegin(); it != degree.cend(); ++it) QCOMPARE(it.value(), 2);
    // Every quad edge used once is a boundary segment (no dangling boundary).
    int once = 0;
    for (auto it = edgeUse.cbegin(); it != edgeUse.cend(); ++it) if (it.value() == 1) ++once;
    QCOMPARE(once, pm.boundarySegments.size());
}

} // namespace

class TestMeshSubmap : public QObject
{
    Q_OBJECT

private slots:

    /*! ringIsRectilinear: L → true, frame 0; L rotated 20° → true, frame ≈ 20;
     *  blob → false; a 1° skew inside tolerance → true, outside → false. */
    void ringIsRectilinear_cases()
    {
        double frame = -1.0;
        QVERIFY(ringIsRectilinear(lShape(), 2.0, &frame));
        QVERIFY(std::abs(frame) < 1e-9 || std::abs(frame - 90.0) < 1e-9);
        QVERIFY(ringIsRectilinear(lShape(), 2.0, nullptr));

        QVERIFY(ringIsRectilinear(rotated(lShape(), 20.0, QPointF(1000, 500)), 2.0, &frame));
        QVERIFY2(std::abs(frame - 20.0) < 1e-6, qPrintable(QStringLiteral("frame %1").arg(frame)));

        QVERIFY(!ringIsRectilinear(blob(), 2.0, &frame));
        QVERIFY(!ringIsRectilinear(uShape() << QPointF(-3, 5), 2.0, nullptr));

        // Skew one edge of a rectangle by ~1.1°: within 2° tolerance, not within 0.5°.
        QPolygonF skew{QPointF(0, 0), QPointF(10, 0), QPointF(10, 5), QPointF(0, 5.2)};
        QVERIFY(ringIsRectilinear(skew, 2.0, nullptr));
        QVERIFY(!ringIsRectilinear(skew, 0.5, nullptr));
        // Degenerate input.
        QVERIFY(!ringIsRectilinear(QPolygonF{QPointF(0, 0), QPointF(1, 0)}, 2.0, nullptr));
    }

    /*! L at h = 1: 75 unit squares, 96 vertices, 40 boundary segments, all
     *  SJ 1, CCW, validate() clean, total area 75, every quad centre inside. */
    void submapped_lShape()
    {
        QString err;
        const PatchMesh pm = makeSubmappedPatch(lShape(), 1.0, QStringLiteral("L"), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.quads.size(), 75);
        QCOMPARE(pm.xy.size(), 96);
        QCOMPARE(pm.boundarySegments.size(), 40);
        QCOMPARE(pm.tag, QStringLiteral("L"));
        QVERIFY(validate(pm).isEmpty());
        QVERIFY(minSJ(pm) > 1.0 - 1e-9);

        const QVector<MeshVertex> V = asVertices(pm.xy);
        double area = 0.0;
        for (const MeshTriangle &q : pm.quads)
        {
            QVERIFY(q.isQuad());
            QCOMPARE(q.tag, QStringLiteral("L"));
            QVERIFY(cellSignedArea(V, q) > 0.0);
            QVERIFY(cellIsConvex(V, q));
            const CellGeom cg = cellGeom(V, q);
            area += cg.area;
            QVERIFY(std::abs(cg.area - 1.0) < 1e-9);
            QVERIFY(pointInRing(lShape(), cg.centroid));
        }
        QVERIFY(std::abs(area - 75.0) < 1e-9);
        checkBoundary(pm, 40.0);
        // Every ring corner is a patch vertex (exact user geometry).
        for (const QPointF &c : lShape()) QVERIFY(pm.xy.contains(c));
    }

    /*! U at h = 1 → 125 squares; boundary perimeter 60. */
    void submapped_uShape()
    {
        QString err;
        const PatchMesh pm = makeSubmappedPatch(uShape(), 1.0, QStringLiteral("U"), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.quads.size(), 125);
        QCOMPARE(pm.boundarySegments.size(), 60);
        QVERIFY(validate(pm).isEmpty());
        QVERIFY(minSJ(pm) > 1.0 - 1e-9);
        checkBoundary(pm, 60.0);
        const QVector<MeshVertex> V = asVertices(pm.xy);
        for (const MeshTriangle &q : pm.quads)
            QVERIFY(pointInRing(uShape(), cellGeom(V, q).centroid));
    }

    /*! Rotated L (20°, shifted): same 75 squares, SJ 1, vertices in the
     *  original frame (corners preserved exactly). h = 2: every 5-unit band
     *  splits into round(5/2) = 3 intervals → 6×6 grid minus the 3×3 notch
     *  = 27 cells. */
    void submapped_rotatedAndCoarse()
    {
        QString err;
        const QPolygonF rot = rotated(lShape(), 20.0, QPointF(1000, 500));
        const PatchMesh pm = makeSubmappedPatch(rot, 1.0, QString(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.quads.size(), 75);
        QVERIFY(minSJ(pm) > 1.0 - 1e-9);
        for (const QPointF &c : rot)
        {
            bool found = false;
            for (const QPointF &p : pm.xy)
                if (std::hypot(p.x() - c.x(), p.y() - c.y()) < 1e-6) { found = true; break; }
            QVERIFY(found);
        }
        const PatchMesh coarse = makeSubmappedPatch(lShape(), 2.0, QString(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(coarse.quads.size(), 36 - 9);
        QVERIFY(validate(coarse).isEmpty());
        // Non-multiple spacing still fills the shape (10×5.4 at h=1 → 10×5).
        const PatchMesh odd = makeSubmappedPatch(QPolygonF{QPointF(0, 0), QPointF(10, 0), QPointF(10, 5.4), QPointF(0, 5.4)},
                                                 1.0, QString(), &err);
        QCOMPARE(odd.quads.size(), 50);
    }

    /*! Vertices jittered by 0.02 (≈ 0.2°–0.5° edge skew) are snapped and the
     *  L is still meshed with 75 rectangles, SJ ≈ 1, centroids inside. */
    void submapped_noisyL()
    {
        std::mt19937 rng(3u);
        std::uniform_real_distribution<double> u(-0.02, 0.02);
        QPolygonF noisy = lShape();
        for (QPointF &p : noisy) p += QPointF(u(rng), u(rng));
        double frame = 0.0;
        QVERIFY(ringIsRectilinear(noisy, 2.0, &frame));
        QString err;
        const PatchMesh pm = makeSubmappedPatch(noisy, 1.0, QStringLiteral("N"), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.quads.size(), 75);
        QVERIFY(validate(pm).isEmpty());
        QVERIFY2(minSJ(pm) > 0.999, qPrintable(QStringLiteral("min SJ %1").arg(minSJ(pm))));
        const QVector<MeshVertex> V = asVertices(pm.xy);
        for (const MeshTriangle &q : pm.quads)
        {
            const QPointF c = cellGeom(V, q).centroid;
            QVERIFY(pointInRing(noisy, c) || pointInRing(lShape(), c));
        }
    }

    /*! Blob → empty mesh, err contains "rectilinear"; h <= 0 → error;
     *  degenerate ring → error. */
    void submapped_rejections()
    {
        QString err;
        PatchMesh pm = makeSubmappedPatch(blob(), 1.0, QString(), &err);
        QVERIFY(pm.quads.isEmpty());
        QVERIFY(pm.xy.isEmpty());
        QVERIFY2(err.contains(QStringLiteral("rectilinear")), qPrintable(err));

        err.clear();
        pm = makeSubmappedPatch(lShape(), 0.0, QString(), &err);
        QVERIFY(pm.quads.isEmpty());
        QVERIFY(!err.isEmpty());
        err.clear();
        pm = makeSubmappedPatch(lShape(), -1.0, QString(), &err);
        QVERIFY(pm.quads.isEmpty());
        QVERIFY(!err.isEmpty());

        err.clear();
        pm = makeSubmappedPatch(QPolygonF{QPointF(0, 0), QPointF(1, 0)}, 1.0, QString(), &err);
        QVERIFY(pm.quads.isEmpty());
        QVERIFY(!err.isEmpty());
        // Null err pointer is accepted.
        QVERIFY(makeSubmappedPatch(blob(), 1.0, QString(), nullptr).quads.isEmpty());
    }
};

QTEST_MAIN(TestMeshSubmap)
#include "test_meshsubmap.moc"
