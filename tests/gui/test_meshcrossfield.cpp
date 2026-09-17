/*!
 * \file   test_meshcrossfield.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quad redesign gate 3 (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md
 * §4.4b, §7.3) — QtTest coverage for mesh::CrossField: an axis-aligned ring
 * gives the constant field θ ≡ 0 (mod 90°), a rotated ring the rotated
 * constant, setConstant folds into [0, 90°), an annulus gives a
 * radial/tangential field (θ follows the polar angle mod 90°), a guide
 * polyline is honoured, invalid inputs are rejected and directionsAt yields
 * four unit vectors 90° apart.
 */
#include <QtTest>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

#include <cmath>

#include "mesh/meshcrossfield.h"

using namespace mesh;

namespace {

QPolygonF rect(double x0, double y0, double w, double h)
{
    return QPolygonF{QPointF(x0, y0), QPointF(x0 + w, y0), QPointF(x0 + w, y0 + h), QPointF(x0, y0 + h)};
}

QPolygonF regularPolygon(int n, double r)
{
    QPolygonF p;
    for (int i = 0; i < n; ++i)
    {
        const double a = 2.0 * M_PI * i / n;
        p << QPointF(r * std::cos(a), r * std::sin(a));
    }
    return p;
}

QPolygonF rotated(const QPolygonF &p, double deg)
{
    const double a = deg * M_PI / 180.0, c = std::cos(a), s = std::sin(a);
    QPolygonF out;
    for (const QPointF &q : p) out << QPointF(q.x() * c - q.y() * s, q.x() * s + q.y() * c);
    return out;
}

/*! Ring as a closed polyline (last == first) for CrossField::build. */
QVector<QPointF> closed(const QPolygonF &ring)
{
    QVector<QPointF> pl = ring;
    pl.append(ring.first());
    return pl;
}

/*! |a − b| on the circle of period 90°, in degrees. */
double angleDiffMod90(double aDeg, double bDeg)
{
    double d = std::fmod(std::abs(aDeg - bDeg), 90.0);
    return std::min(d, 90.0 - d);
}

double deg(double rad) { return rad * 180.0 / M_PI; }

} // namespace

class TestMeshCrossField : public QObject
{
    Q_OBJECT

private slots:

    /*! Axis-aligned square ring: every query inside is 0 mod 90° within
     *  1e-3 rad, the field is valid and every ring cell is pinned. */
    void square_constantZero()
    {
        const QPolygonF ring = rect(0, 0, 10, 10);
        CrossField f;
        CrossField::Options o;
        o.pitch = 1.0;
        QVERIFY(f.build(ring.boundingRect(), {closed(ring)}, o));
        QVERIFY(f.valid());
        QVERIFY(f.cols() > 10 && f.rows() > 10);
        for (double x = 0.5; x < 10.0; x += 1.5)
            for (double y = 0.5; y < 10.0; y += 1.5)
            {
                const double th = f.thetaAt(x, y);
                QVERIFY(th >= 0.0 && th < M_PI / 2.0 + 1e-12);
                const double dev = std::min(th, M_PI / 2.0 - th);
                QVERIFY2(dev < 1e-3, qPrintable(QStringLiteral("theta(%1,%2) = %3").arg(x).arg(y).arg(th)));
            }
        // Outside the grid the query clamps rather than failing.
        const double far = f.thetaAt(1000.0, -1000.0);
        QVERIFY(std::isfinite(far));
        QVERIFY(std::min(far, M_PI / 2.0 - far) < 1e-3);
    }

    /*! Square rotated by 30°: θ ≈ 30° (folded to [0, 90°)) inside. */
    void rotatedSquare_thirtyDegrees()
    {
        const QPolygonF ring = rotated(rect(0, 0, 10, 10), 30.0);
        CrossField f;
        CrossField::Options o;
        o.pitch = 1.0;
        QVERIFY(f.build(ring.boundingRect(), {closed(ring)}, o));
        const QPointF c = (ring[0] + ring[2]) / 2.0;
        QVERIFY(angleDiffMod90(deg(f.thetaAt(c.x(), c.y())), 30.0) < 1.0);
        // A second interior sample, off-centre.
        const QPointF p = ring[0] + (ring[1] - ring[0]) * 0.3 + (ring[3] - ring[0]) * 0.6;
        QVERIFY(angleDiffMod90(deg(f.thetaAt(p.x(), p.y())), 30.0) < 1.0);
    }

    /*! setConstant folds: 120° → 30°, −45° → 45°, 90° → 0. */
    void setConstant_folded()
    {
        CrossField f;
        QVERIFY(!f.valid());
        f.setConstant(120.0);
        QVERIFY(f.valid());
        QVERIFY(std::abs(deg(f.thetaAt(0, 0)) - 30.0) < 1e-9);
        QVERIFY(std::abs(deg(f.thetaAt(123.0, -77.0)) - 30.0) < 1e-9);
        f.setConstant(-45.0);
        QVERIFY(std::abs(deg(f.thetaAt(0, 0)) - 45.0) < 1e-9);
        f.setConstant(90.0);
        QVERIFY(std::min(deg(f.thetaAt(0, 0)), 90.0 - deg(f.thetaAt(0, 0))) < 1e-9);
    }

    /*! Annulus (outer 32-gon r = 10, inner 32-gon r = 4): the harmonic
     *  interpolant of the two tangential boundaries is tangential/radial
     *  throughout, i.e. θ ≡ polar angle (mod 90°): ≈ 0 at (7,0) and (0,7),
     *  ≈ 45° at (7/√2, 7/√2), within 5°. */
    void annulus_radialTangential()
    {
        const QPolygonF outer = regularPolygon(32, 10.0), inner = regularPolygon(32, 4.0);
        CrossField f;
        CrossField::Options o;
        o.pitch = 0.5;
        QVERIFY(f.build(outer.boundingRect(), {closed(outer), closed(inner)}, o));
        QVERIFY(angleDiffMod90(deg(f.thetaAt(7.0, 0.0)), 0.0) < 5.0);
        QVERIFY(angleDiffMod90(deg(f.thetaAt(0.0, 7.0)), 0.0) < 5.0);
        QVERIFY(angleDiffMod90(deg(f.thetaAt(-7.0, 0.0)), 0.0) < 5.0);
        const double d = 7.0 / std::sqrt(2.0);
        QVERIFY(angleDiffMod90(deg(f.thetaAt(d, d)), 45.0) < 5.0);
        QVERIFY(angleDiffMod90(deg(f.thetaAt(-d, d)), 45.0) < 5.0);
        // Generic polar angle, radius 7.
        for (int k = 0; k < 16; ++k)
        {
            const double phi = 2.0 * M_PI * k / 16.0 + 0.1;
            const double x = 7.0 * std::cos(phi), y = 7.0 * std::sin(phi);
            QVERIFY2(angleDiffMod90(deg(f.thetaAt(x, y)), deg(phi)) < 5.0,
                     qPrintable(QStringLiteral("phi=%1 theta=%2").arg(deg(phi)).arg(deg(f.thetaAt(x, y)))));
        }
    }

    /*! A guide polyline pins the field to its direction (plan §7.3: within 2°). */
    void guidePolyline_honoured()
    {
        CrossField f;
        CrossField::Options o;
        o.pitch = 1.0;
        const QVector<QPointF> guide{QPointF(0, 0), QPointF(10, 3)};
        QVERIFY(f.build(QRectF(0, 0, 10, 10), {guide}, o));
        const double want = deg(std::atan2(3.0, 10.0));
        QVERIFY(angleDiffMod90(deg(f.thetaAt(5.0, 1.5)), want) < 2.0);
        QVERIFY(angleDiffMod90(deg(f.thetaAt(2.0, 6.0)), want) < 2.0);   // harmonic → same constant everywhere
    }

    /*! build() rejects pitch <= 0, an empty bbox and no polylines; a failed
     *  build leaves the field invalid. */
    void build_rejectsBadInput()
    {
        const QPolygonF ring = rect(0, 0, 10, 10);
        CrossField f;
        CrossField::Options o;
        o.pitch = 0.0;
        QVERIFY(!f.build(ring.boundingRect(), {closed(ring)}, o));
        QVERIFY(!f.valid());
        o.pitch = -1.0;
        QVERIFY(!f.build(ring.boundingRect(), {closed(ring)}, o));
        o.pitch = 1.0;
        QVERIFY(!f.build(ring.boundingRect(), {}, o));
        QVERIFY(!f.valid());
        QVERIFY(!f.build(QRectF(), {closed(ring)}, o));
        // A single-point "polyline" pins nothing.
        QVERIFY(!f.build(ring.boundingRect(), {QVector<QPointF>{QPointF(5, 5)}}, o));
    }

    /*! directionsAt: four unit vectors, successive ones 90° apart, d[0] at θ. */
    void directionsAt_fourUnitVectors()
    {
        CrossField f;
        f.setConstant(30.0);
        QPointF d[4];
        f.directionsAt(3.0, 4.0, d);
        for (int k = 0; k < 4; ++k)
        {
            QVERIFY(std::abs(std::hypot(d[k].x(), d[k].y()) - 1.0) < 1e-12);
            const QPointF &n = d[(k + 1) % 4];
            QVERIFY(std::abs(d[k].x() * n.x() + d[k].y() * n.y()) < 1e-12);            // perpendicular
            QVERIFY(d[k].x() * n.y() - d[k].y() * n.x() > 0.0);                         // CCW order
        }
        QVERIFY(std::abs(d[0].x() - std::cos(30.0 * M_PI / 180.0)) < 1e-12);
        QVERIFY(std::abs(d[0].y() - std::sin(30.0 * M_PI / 180.0)) < 1e-12);
        QVERIFY(std::abs(d[2].x() + d[0].x()) < 1e-12 && std::abs(d[2].y() + d[0].y()) < 1e-12);

        // Same on a solved field.
        const QPolygonF ring = rect(0, 0, 10, 10);
        CrossField g;
        CrossField::Options o;
        o.pitch = 1.0;
        QVERIFY(g.build(ring.boundingRect(), {closed(ring)}, o));
        g.directionsAt(5.0, 5.0, d);
        for (int k = 0; k < 4; ++k)
            QVERIFY(std::abs(std::hypot(d[k].x(), d[k].y()) - 1.0) < 1e-12);
        QVERIFY(std::abs(d[0].x() * d[1].x() + d[0].y() * d[1].y()) < 1e-12);
    }
};

QTEST_MAIN(TestMeshCrossField)
#include "test_meshcrossfield.moc"
