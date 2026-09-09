/*!
 * \file   test_meshquadpoints.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quad redesign gate 4 (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md
 * §4.4d, §7.4) — QtTest coverage for mesh::placeQuadPoints: a 10h × 10h
 * square yields the 9×9 interior lattice (81 ± 2 points, 32 ± 4 in the
 * boundary layer), every point clears the ring by boundaryClearance·h, the
 * nearest-neighbour spacing is within [0.7h, 1.3h] for ≥ 95 % of the points,
 * the 100 lattice squares come back as CCW templates with valid distinct
 * indices, the result is deterministic, a rotated square with a solved
 * field gives the same counts, and h <= 0 yields an empty set.
 */
#include <QtTest>
#include <QPointF>
#include <QPolygonF>
#include <QSet>
#include <QVector>

#include <cmath>
#include <limits>

#include "mesh/meshcrossfield.h"
#include "mesh/meshquadpoints.h"
#include "mesh/meshquadregion.h"

using namespace mesh;

namespace {

QPolygonF rect(double w, double h)
{
    return QPolygonF{QPointF(0, 0), QPointF(w, 0), QPointF(w, h), QPointF(0, h)};
}

QPolygonF rotated(const QPolygonF &p, double deg)
{
    const double a = deg * M_PI / 180.0, c = std::cos(a), s = std::sin(a);
    QPolygonF out;
    for (const QPointF &q : p) out << QPointF(q.x() * c - q.y() * s, q.x() * s + q.y() * c);
    return out;
}

double dist(const QPointF &a, const QPointF &b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

/*! Fraction of generated points whose nearest neighbour among ALL points
 *  (seeds + generated) lies within [lo, hi]. */
double nearestNeighbourFraction(const QVector<QPointF> &seeds, const QVector<QPointF> &generated,
                                double lo, double hi)
{
    QVector<QPointF> all = seeds;
    all += generated;
    int good = 0;
    for (const QPointF &p : generated)
    {
        double best = std::numeric_limits<double>::infinity();
        for (const QPointF &q : all)
        {
            if (&q == &p || q == p) continue;
            best = std::min(best, dist(p, q));
        }
        if (best >= lo && best <= hi) ++good;
    }
    return generated.isEmpty() ? 0.0 : double(good) / generated.size();
}

/*! Combined index → point (i < seeds.size() → seed). */
QPointF pointAt(const QVector<QPointF> &seeds, const QVector<QPointF> &generated, int i)
{
    return i < seeds.size() ? seeds[i] : generated[i - seeds.size()];
}

double signedArea(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d)
{
    const QPointF p[4] = {a, b, c, d};
    double s = 0.0;
    for (int i = 0; i < 4; ++i) s += p[i].x() * p[(i + 1) % 4].y() - p[(i + 1) % 4].x() * p[i].y();
    return 0.5 * s;
}

} // namespace

class TestMeshQuadPoints : public QObject
{
    Q_OBJECT

private slots:

    /*! 10h × 10h square, h = 1, constant field 0, seeds = ring vertices. */
    void square_latticeCounts()
    {
        const double h = 1.0;
        const QPolygonF ring = resampleRing(rect(10 * h, 10 * h), h);
        QCOMPARE(ring.size(), 40);
        const QVector<QPointF> seeds = ring;
        CrossField field;
        field.setConstant(0.0);
        QuadPointOptions o;
        o.h = h;
        const QuadPointSet ps = placeQuadPoints(ring, seeds, ring.size(), field, o);

        QVERIFY2(std::abs(ps.generated.size() - 81) <= 2,
                 qPrintable(QStringLiteral("generated %1").arg(ps.generated.size())));
        QVERIFY2(std::abs(ps.boundaryLayerPoints - 32) <= 4,
                 qPrintable(QStringLiteral("boundary layer %1").arg(ps.boundaryLayerPoints)));
        QVERIFY(ps.boundaryLayerPoints <= ps.generated.size());

        // Every generated point is strictly inside and clears the ring.
        for (const QPointF &p : ps.generated)
        {
            QVERIFY(pointInRing(ring, p));
            QVERIFY2(distanceToRing(ring, p) >= o.boundaryClearance * h - 1e-9,
                     qPrintable(QStringLiteral("(%1,%2) d=%3").arg(p.x()).arg(p.y()).arg(distanceToRing(ring, p))));
        }
        // No duplicates.
        for (int i = 0; i < ps.generated.size(); ++i)
            for (int j = i + 1; j < ps.generated.size(); ++j)
                QVERIFY(dist(ps.generated[i], ps.generated[j]) > 1e-9);

        // Spacing statistics.
        QVERIFY(nearestNeighbourFraction(seeds, ps.generated, 0.7 * h, 1.3 * h) >= 0.95);

        // Templates: 100 expected (>= 95 required), CCW, valid distinct indices,
        // deduplicated by vertex set, side length ≈ h.
        QVERIFY2(ps.templates.size() >= 95, qPrintable(QStringLiteral("templates %1").arg(ps.templates.size())));
        QVERIFY(ps.templates.size() <= 100);
        const int total = seeds.size() + ps.generated.size();
        QSet<QVector<int>> seen;
        for (const QuadTemplate &t : ps.templates)
        {
            QVector<int> sorted;
            for (int k = 0; k < 4; ++k)
            {
                QVERIFY(t.v[k] >= 0 && t.v[k] < total);
                for (int j = 0; j < k; ++j) QVERIFY(t.v[j] != t.v[k]);
                sorted << t.v[k];
            }
            std::sort(sorted.begin(), sorted.end());
            QVERIFY(!seen.contains(sorted));
            seen.insert(sorted);
            const QPointF a = pointAt(seeds, ps.generated, t.v[0]), b = pointAt(seeds, ps.generated, t.v[1]),
                          c = pointAt(seeds, ps.generated, t.v[2]), d = pointAt(seeds, ps.generated, t.v[3]);
            QVERIFY(signedArea(a, b, c, d) > 0.0);
            QVERIFY(std::abs(signedArea(a, b, c, d) - h * h) < 0.35 * h * h);
        }
    }

    /*! Two runs on identical input are bitwise identical (FIFO front, no
     *  hashing order dependence). */
    void square_deterministic()
    {
        const QPolygonF ring = resampleRing(rect(10, 10), 1.0);
        CrossField field;
        field.setConstant(0.0);
        QuadPointOptions o;
        o.h = 1.0;
        const QuadPointSet a = placeQuadPoints(ring, ring, ring.size(), field, o);
        const QuadPointSet b = placeQuadPoints(ring, ring, ring.size(), field, o);
        QCOMPARE(a.generated.size(), b.generated.size());
        QCOMPARE(a.boundaryLayerPoints, b.boundaryLayerPoints);
        QCOMPARE(a.templates.size(), b.templates.size());
        for (int i = 0; i < a.generated.size(); ++i) QVERIFY(a.generated[i] == b.generated[i]);
        for (int i = 0; i < a.templates.size(); ++i)
            for (int k = 0; k < 4; ++k) QCOMPARE(a.templates[i].v[k], b.templates[i].v[k]);
    }

    /*! The square rotated 45° with a SOLVED cross field (aligned to the ring)
     *  yields the same counts within 5 %. */
    void rotatedSquare_solvedField()
    {
        const double h = 1.0;
        const QPolygonF ring = resampleRing(rotated(rect(10 * h, 10 * h), 45.0), h);
        QCOMPARE(ring.size(), 40);
        CrossField field;
        CrossField::Options fo;
        fo.pitch = h;
        QVector<QPointF> closed = ring;
        closed.append(ring.first());
        QVERIFY(field.build(ring.boundingRect(), {closed}, fo));
        QuadPointOptions o;
        o.h = h;
        const QuadPointSet ps = placeQuadPoints(ring, ring, ring.size(), field, o);
        QVERIFY2(std::abs(ps.generated.size() - 81) <= 5,
                 qPrintable(QStringLiteral("generated %1").arg(ps.generated.size())));
        QVERIFY2(ps.templates.size() >= 95, qPrintable(QStringLiteral("templates %1").arg(ps.templates.size())));
        for (const QPointF &p : ps.generated)
        {
            QVERIFY(pointInRing(ring, p));
            QVERIFY(distanceToRing(ring, p) >= o.boundaryClearance * h - 1e-9);
        }
        QVERIFY(nearestNeighbourFraction(ring, ps.generated, 0.7 * h, 1.3 * h) >= 0.95);
    }

    /*! QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.2 / Q1 — a constant hAt must
     *  reproduce the uniform lattice EXACTLY. This is the regression guard
     *  that keeps the graded refactor invisible to every committed caller. */
    void gradedField_constantMatchesUniform()
    {
        const double h = 1.0;
        const QPolygonF ring = resampleRing(rect(10 * h, 10 * h), h);
        CrossField field;
        field.setConstant(0.0);

        QuadPointOptions uniform;
        uniform.h = h;
        const QuadPointSet a = placeQuadPoints(ring, ring, ring.size(), field, uniform);

        QuadPointOptions graded;
        graded.h    = h;
        graded.hAt  = [](double, double) { return 1.0; };
        graded.hMin = graded.hMax = h;
        const QuadPointSet b = placeQuadPoints(ring, ring, ring.size(), field, graded);

        QCOMPARE(b.generated.size(), a.generated.size());
        QCOMPARE(b.boundaryLayerPoints, a.boundaryLayerPoints);
        QCOMPARE(b.templates.size(), a.templates.size());
        for (int i = 0; i < a.generated.size(); ++i)
        {
            QCOMPARE(b.generated[i].x(), a.generated[i].x());
            QCOMPARE(b.generated[i].y(), a.generated[i].y());
        }
        for (int i = 0; i < a.templates.size(); ++i)
            for (int k = 0; k < 4; ++k)
                QCOMPARE(b.templates[i].v[k], a.templates[i].v[k]);
    }

    /*! Q1 gate — a linear spacing ramp (h = 2 at x=0 → 8 at x=120) must produce
     *  a lattice whose LOCAL spacing tracks h, not a uniform one. The analytic
     *  point count is ∫∫ dA/h(x)² = 40·∫₀¹²⁰ dx/(2+0.05x)² = 300; a uniform
     *  lattice would give 1200 (at h=2) or 75 (at h=8), so the count alone
     *  separates graded from either uniform fallback. */
    void gradedField_rampTracksLocalSpacing()
    {
        const auto hOf = [](double x) { return 2.0 + 6.0 * x / 120.0; };
        const QPolygonF ring = resampleRing(rect(120.0, 40.0), 2.0);
        CrossField field;
        field.setConstant(0.0);

        QuadPointOptions o;
        o.h    = 2.0;                       // fallback only
        o.hAt  = [&hOf](double x, double) { return hOf(x); };
        o.hMin = 2.0;
        o.hMax = 8.0;
        const QuadPointSet ps = placeQuadPoints(ring, ring, ring.size(), field, o);

        QVERIFY2(ps.generated.size() > 180 && ps.generated.size() < 420,
                 qPrintable(QStringLiteral("graded count %1, expected ≈300 (uniform would be 1200 or 75)")
                                .arg(ps.generated.size())));

        // Local spacing tracks h(x): nearest neighbour within [0.6, 1.5]·h(p).
        QVector<QPointF> all = ring;
        all += ps.generated;
        int good = 0;
        for (const QPointF &p : ps.generated)
        {
            double best = std::numeric_limits<double>::infinity();
            for (const QPointF &q : all)
            {
                const double d = dist(p, q);
                if (d > 1e-9) best = std::min(best, d);
            }
            const double r = best / hOf(p.x());
            if (r >= 0.6 && r <= 1.5) ++good;
        }
        const double frac = double(good) / double(std::max<qsizetype>(1, ps.generated.size()));
        QVERIFY2(frac >= 0.90,
                 qPrintable(QStringLiteral("only %1 of points track h(x)").arg(frac)));

        // The coarse end really is coarser: mean NN spacing in the last third
        // must exceed the first third by at least 2x (h ramps 2 -> 8).
        auto meanSpacing = [&](double x0, double x1) {
            double s = 0.0; int n = 0;
            for (const QPointF &p : ps.generated)
            {
                if (p.x() < x0 || p.x() >= x1) continue;
                double best = std::numeric_limits<double>::infinity();
                for (const QPointF &q : all)
                { const double d = dist(p, q); if (d > 1e-9) best = std::min(best, d); }
                s += best; ++n;
            }
            return n ? s / n : 0.0;
        };
        const double near = meanSpacing(0.0, 40.0), far = meanSpacing(80.0, 120.0);
        QVERIFY2(far > 2.0 * near,
                 qPrintable(QStringLiteral("near=%1 far=%2").arg(near).arg(far)));

        // Determinism.
        const QuadPointSet again = placeQuadPoints(ring, ring, ring.size(), field, o);
        QCOMPARE(again.generated.size(), ps.generated.size());
        for (int i = 0; i < ps.generated.size(); ++i)
            QCOMPARE(again.generated[i], ps.generated[i]);
    }

    /*! h <= 0 or a degenerate ring → empty result. */
    void invalidInput_empty()
    {
        const QPolygonF ring = resampleRing(rect(10, 10), 1.0);
        CrossField field;
        field.setConstant(0.0);
        QuadPointOptions o;
        o.h = 0.0;
        QuadPointSet ps = placeQuadPoints(ring, ring, ring.size(), field, o);
        QVERIFY(ps.generated.isEmpty());
        QVERIFY(ps.templates.isEmpty());
        QCOMPARE(ps.boundaryLayerPoints, 0);
        o.h = -2.0;
        ps = placeQuadPoints(ring, ring, ring.size(), field, o);
        QVERIFY(ps.generated.isEmpty());
        o.h = 1.0;
        const QPolygonF line{QPointF(0, 0), QPointF(5, 0)};
        ps = placeQuadPoints(line, line, line.size(), field, o);
        QVERIFY(ps.generated.isEmpty());
        QVERIFY(ps.templates.isEmpty());
    }

    /*! distanceToRing: interior point, boundary point, exterior point. */
    void distanceToRing_values()
    {
        const QPolygonF sq = rect(10, 10);
        QVERIFY(std::abs(distanceToRing(sq, QPointF(5, 5)) - 5.0) < 1e-12);
        QVERIFY(std::abs(distanceToRing(sq, QPointF(1, 5)) - 1.0) < 1e-12);
        QVERIFY(std::abs(distanceToRing(sq, QPointF(0, 5))) < 1e-12);
        QVERIFY(std::abs(distanceToRing(sq, QPointF(-3, 5)) - 3.0) < 1e-12);
        QVERIFY(std::abs(distanceToRing(sq, QPointF(13, 14)) - 5.0) < 1e-12);
    }
};

QTEST_MAIN(TestMeshQuadPoints)
#include "test_meshquadpoints.moc"
