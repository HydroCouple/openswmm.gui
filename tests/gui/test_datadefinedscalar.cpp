/*!
 * \file   test_datadefinedscalar.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BI Phase 8.13.43-α — DataDefinedScalar maps one numeric attribute
 * onto a scalar output range (typically symbol size in pixels). Test sweep:
 * curve correctness (Linear / Sqrt / Log), clamping behaviour, JSON
 * round-trip, NaN / infinity handling, degenerate range fallback.
 */

#include "render/datadefined.h"

#include <QJsonObject>
#include <QObject>
#include <QTest>

#include <cmath>
#include <limits>

using namespace OpenSWMM::Render;

class TestDataDefinedScalar : public QObject
{
    Q_OBJECT
private slots:

    // ── Linear curve ──────────────────────────────────────────────────────

    void linear_midpoint_returns_midpoint()
    {
        DataDefinedScalar d;
        d.attribute = QStringLiteral("maxDepth");
        d.valueMin = 0.0; d.valueMax = 10.0;
        d.outMin   = 2.0; d.outMax   = 12.0;
        d.curve    = DDCurve::Linear;
        QCOMPARE(d.evaluate(5.0), 7.0);   // midpoint of [2,12]
    }

    void linear_endpoints()
    {
        DataDefinedScalar d;
        d.attribute = QStringLiteral("x");
        d.valueMin = -5.0; d.valueMax = 5.0;
        d.outMin   =  4.0; d.outMax  = 20.0;
        QCOMPARE(d.evaluate(-5.0),  4.0);
        QCOMPARE(d.evaluate( 5.0), 20.0);
    }

    // ── Sqrt curve ────────────────────────────────────────────────────────

    void sqrt_curve_compresses_high_end()
    {
        DataDefinedScalar d;
        d.attribute = QStringLiteral("area");
        d.valueMin = 0.0; d.valueMax = 100.0;
        d.outMin   = 0.0; d.outMax   = 100.0;
        d.curve    = DDCurve::Sqrt;
        // sqrt(0.25) = 0.5  →  out = 50
        QVERIFY(qAbs(d.evaluate(25.0) - 50.0) < 1e-6);
        // sqrt(1.0)  = 1.0  →  out = 100
        QVERIFY(qAbs(d.evaluate(100.0) - 100.0) < 1e-6);
    }

    // ── Log curve ─────────────────────────────────────────────────────────

    void log_curve_endpoints_and_monotonic()
    {
        DataDefinedScalar d;
        d.attribute = QStringLiteral("flow");
        d.valueMin = 0.0; d.valueMax = 10.0;
        d.outMin   = 0.0; d.outMax   = 1.0;
        d.curve    = DDCurve::Log;
        // log10(1)  = 0  →  out = 0    (at t=0)
        // log10(10) = 1  →  out = 1    (at t=1)
        QVERIFY(qAbs(d.evaluate(0.0)  - 0.0) < 1e-6);
        QVERIFY(qAbs(d.evaluate(10.0) - 1.0) < 1e-6);
        // Monotonic + concave (compresses high end).
        const double midA = d.evaluate(2.0);
        const double midB = d.evaluate(5.0);
        QVERIFY(midA < midB);
        QVERIFY(midA > 0.0);
    }

    // ── Clamping ──────────────────────────────────────────────────────────

    void clamps_below_min_to_outMin()
    {
        DataDefinedScalar d;
        d.attribute = QStringLiteral("x");
        d.valueMin = 10.0; d.valueMax = 20.0;
        d.outMin   = 5.0;  d.outMax  = 15.0;
        QCOMPARE(d.evaluate(0.0),  5.0);
        QCOMPARE(d.evaluate(-5.0), 5.0);
    }

    void clamps_above_max_to_outMax()
    {
        DataDefinedScalar d;
        d.attribute = QStringLiteral("x");
        d.valueMin = 10.0; d.valueMax = 20.0;
        d.outMin   = 5.0;  d.outMax  = 15.0;
        QCOMPARE(d.evaluate(50.0), 15.0);
    }

    // ── Defensive handling ───────────────────────────────────────────────

    void nan_returns_outMin()
    {
        DataDefinedScalar d;
        d.attribute = QStringLiteral("x");
        d.outMin = 3.0; d.outMax = 9.0;
        QCOMPARE(d.evaluate(std::nan("")), 3.0);
    }

    void degenerate_input_range_returns_output_centre()
    {
        DataDefinedScalar d;
        d.attribute = QStringLiteral("x");
        d.valueMin = 5.0; d.valueMax = 5.0;  // zero-width input
        d.outMin   = 2.0; d.outMax   = 10.0;
        QCOMPARE(d.evaluate(5.0), 6.0);   // midpoint of [2,10]
    }

    // ── isValid ───────────────────────────────────────────────────────────

    void isValid_rejects_empty_attribute()
    {
        DataDefinedScalar d;
        QVERIFY(!d.isValid());
        d.attribute = QStringLiteral("x");
        QVERIFY(d.isValid());
    }

    // ── JSON round-trip ──────────────────────────────────────────────────

    void json_round_trip()
    {
        DataDefinedScalar in;
        in.attribute = QStringLiteral("geom1");
        in.valueMin = 0.5;  in.valueMax = 4.5;
        in.outMin   = 1.0;  in.outMax   = 8.0;
        in.curve    = DDCurve::Sqrt;

        DataDefinedScalar out = DataDefinedScalar::fromJson(in.toJson());
        QCOMPARE(out.attribute, in.attribute);
        QCOMPARE(out.valueMin,  in.valueMin);
        QCOMPARE(out.valueMax,  in.valueMax);
        QCOMPARE(out.outMin,    in.outMin);
        QCOMPARE(out.outMax,    in.outMax);
        QCOMPARE(out.curve,     in.curve);
    }
};

QTEST_APPLESS_MAIN(TestDataDefinedScalar)
#include "test_datadefinedscalar.moc"
