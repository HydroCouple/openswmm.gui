/*!
 * \file   test_colorramp.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BB-α — RasterColorRamp data type, HSV interpolation, built-in
 * catalogue, JSON round-trip. Self-contained: pulls in render/colorramp.cpp
 * only.
 */

#include "render/colorramp.h"

#include <QJsonDocument>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

namespace
{

// Helper — compare two QColors in HSV-F space (because RGB→HSV is what
// the test cases reason about), with a small tolerance for floating-
// point error.
bool fuzzyEqualF(double a, double b, double eps = 1e-3)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

class TestColorRamp : public QObject
{
    Q_OBJECT
private slots:

    // ---- Stops at 0, 0.5, 1 --------------------------------------------------

    void colorAt_endpoints()
    {
        RasterColorRamp r;
        r.stops = { {0.0, QColor(255, 0, 0)},
                    {1.0, QColor(0, 0, 255)} };
        r.interp = RampInterp::Rgb;
        QCOMPARE(r.colorAt(0.0), QColor(255, 0, 0));
        QCOMPARE(r.colorAt(1.0), QColor(0, 0, 255));
    }

    void colorAt_midpoint_rgb()
    {
        RasterColorRamp r;
        r.stops = { {0.0, QColor(0, 0, 0)},
                    {1.0, QColor(100, 200, 50)} };
        r.interp = RampInterp::Rgb;
        const QColor mid = r.colorAt(0.5);
        // RGB-linear midpoint:
        QVERIFY(qAbs(mid.red()   - 50)  <= 1);
        QVERIFY(qAbs(mid.green() - 100) <= 1);
        QVERIFY(qAbs(mid.blue()  - 25)  <= 1);
    }

    // ---- HSV-short vs HSV-long interpolation --------------------------------

    void colorAt_hsvShort_pure_red_to_blue()
    {
        // Red (hue 0°) → Blue (hue 240°). Short arc backwards (red → blue
        // crossing magenta) is 120° span; long arc forward is 240° span.
        RasterColorRamp r;
        r.stops = { {0.0, QColor(255, 0, 0)},
                    {1.0, QColor(0, 0, 255)} };
        r.interp = RampInterp::HsvShort;
        const QColor mid = r.colorAt(0.5);
        float h = 0.0f, s = 0.0f, v = 0.0f, a = 0.0f;
        mid.getHsvF(&h, &s, &v, &a);
        // Short arc midway between 0° (=0.0) and 240° (=0.667) is 300° (=0.833),
        // crossing via magenta.
        QVERIFY(fuzzyEqualF(h, 0.833, 0.02));
    }

    void colorAt_hsvLong_pure_red_to_blue()
    {
        RasterColorRamp r;
        r.stops = { {0.0, QColor(255, 0, 0)},
                    {1.0, QColor(0, 0, 255)} };
        r.interp = RampInterp::HsvLong;
        const QColor mid = r.colorAt(0.5);
        float h = 0.0f, s = 0.0f, v = 0.0f, a = 0.0f;
        mid.getHsvF(&h, &s, &v, &a);
        // Long arc midway is 120° (=0.333), crossing via green/yellow.
        QVERIFY(fuzzyEqualF(h, 0.333, 0.02));
    }

    void colorAt_hsv_falls_back_to_rgb_on_achromatic_endpoint()
    {
        // Black has hue == -1 (achromatic). HSV interpolation is undefined;
        // the impl must fall back to RGB to remain sensible.
        RasterColorRamp r;
        r.stops = { {0.0, QColor::fromRgb(0, 0, 0)},
                    {1.0, QColor::fromRgb(255, 0, 0)} };
        r.interp = RampInterp::HsvShort;
        const QColor mid = r.colorAt(0.5);
        // RGB-linear midpoint of black → red is (127, 0, 0).
        QVERIFY(qAbs(mid.red() - 127) <= 1);
        QVERIFY(qAbs(mid.green()) <= 1);
        QVERIFY(qAbs(mid.blue())  <= 1);
    }

    // ---- Clamping and empty stops --------------------------------------------

    void colorAt_clamps_to_endpoints_below_zero_above_one()
    {
        RasterColorRamp r;
        r.stops = { {0.0, QColor(10, 20, 30)},
                    {1.0, QColor(200, 210, 220)} };
        QCOMPARE(r.colorAt(-1.0), QColor(10, 20, 30));
        QCOMPARE(r.colorAt( 2.0), QColor(200, 210, 220));
    }

    void colorAt_empty_stops_returns_transparent()
    {
        RasterColorRamp r;
        QCOMPARE(r.colorAt(0.5).alpha(), 0);
    }

    // ---- Built-in catalogue ---------------------------------------------------

    void builtin_names_lists_all_entries()
    {
        // Slice BB-α shipped 13 entries; Slice BB-β added 10 Plotly
        // continuous palettes — total 23.
        const QStringList names = RasterColorRamp::builtinNames();
        QCOMPARE(names.size(), 23);
        QVERIFY(names.contains(QStringLiteral("Viridis")));
        QVERIFY(names.contains(QStringLiteral("Plasma")));
        QVERIFY(names.contains(QStringLiteral("RdBu")));
        QVERIFY(names.contains(QStringLiteral("Legacy SWMM (5-interval)")));
        // Slice BB-β Plotly entries
        QVERIFY(names.contains(QStringLiteral("Plotly3")));
        QVERIFY(names.contains(QStringLiteral("Jet")));
        QVERIFY(names.contains(QStringLiteral("Bluered")));
    }

    void builtin_plotly_lookup_returns_distinct_ramps()
    {
        // Slice BB-β — every Plotly factory returns a non-grayscale ramp
        // (more than 2 stops or distinct endpoints).
        for (const QString &name :
             { QStringLiteral("Plotly3"),  QStringLiteral("IceFire"),
               QStringLiteral("Blackbody"),QStringLiteral("Electric"),
               QStringLiteral("Hot"),      QStringLiteral("Jet"),
               QStringLiteral("Picnic"),   QStringLiteral("Portland"),
               QStringLiteral("Rainbow"),  QStringLiteral("Bluered") })
        {
            const RasterColorRamp r = RasterColorRamp::builtin(name);
            QVERIFY2(r.stops.size() >= 2,
                     qPrintable(QStringLiteral("Plotly ramp \"%1\" has < 2 stops").arg(name)));
            // First and last stops differ (no degenerate flat ramp).
            QVERIFY2(r.stops.first().second.rgba() != r.stops.last().second.rgba(),
                     qPrintable(QStringLiteral("Plotly ramp \"%1\" endpoints identical").arg(name)));
            // Endpoints are at exactly 0.0 and 1.0.
            QCOMPARE(r.stops.first().first, 0.0);
            QCOMPARE(r.stops.last().first,  1.0);
        }
    }

    void builtin_lookup_is_case_insensitive()
    {
        const RasterColorRamp r1 = RasterColorRamp::builtin(QStringLiteral("VIRIDIS"));
        const RasterColorRamp r2 = RasterColorRamp::builtin(QStringLiteral("viridis"));
        QCOMPARE(r1.stops.size(), r2.stops.size());
        QCOMPARE(r1.stops.first().second, r2.stops.first().second);
    }

    void builtin_unknown_falls_back_to_grayscale()
    {
        const RasterColorRamp r = RasterColorRamp::builtin(QStringLiteral("does-not-exist"));
        // Grayscale has two stops, black → white.
        QCOMPARE(r.stops.size(), 2);
        QCOMPARE(r.stops.first().second.name(), QStringLiteral("#000000"));
        QCOMPARE(r.stops.last().second.name(),  QStringLiteral("#ffffff"));
    }

    // ---- JSON round-trip ------------------------------------------------------

    void json_round_trip_preserves_stops_and_interp()
    {
        RasterColorRamp r;
        r.minValue = -3.0;
        r.maxValue = 12.5;
        r.clampMin = true;
        r.clampMax = false;
        r.interp   = RampInterp::HsvLong;
        r.stops    = { {0.0, QColor(10, 20, 30, 200)},
                       {0.4, QColor(120, 130, 140)},
                       {1.0, QColor(250, 240, 230)} };

        const QJsonObject obj = r.toJson();
        const RasterColorRamp r2 = RasterColorRamp::fromJson(obj);

        QCOMPARE(r2.minValue, r.minValue);
        QCOMPARE(r2.maxValue, r.maxValue);
        QCOMPARE(r2.clampMin, r.clampMin);
        QCOMPARE(r2.clampMax, r.clampMax);
        QCOMPARE(r2.interp,   r.interp);
        QCOMPARE(r2.stops.size(), r.stops.size());
        for (int i = 0; i < r.stops.size(); ++i)
        {
            QCOMPARE(r2.stops[i].first,  r.stops[i].first);
            QCOMPARE(r2.stops[i].second.rgba(), r.stops[i].second.rgba());
        }
    }
};

QTEST_APPLESS_MAIN(TestColorRamp)
#include "test_colorramp.moc"
