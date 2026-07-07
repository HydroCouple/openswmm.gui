/*!
 * \file   test_meshprofileinterp.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Issue 4 — 2D profile water-surface bridging (MeshProfileInterp::bridgedTops).
 * Self-contained: the helper is header-only and only depends on the (header-
 * only) Sample struct, so this test links no .cpp. Pure assertions — writes no
 * temp files.
 *
 * Covers the success criteria: continuity across a partial-wet gap, monotonic
 * (no-upstream-flow) bridged surface, bed-follow over an interior crest,
 * shallow-film rendering, and a preserved off-mesh true gap.
 */

#include "plot/meshprofileinterp.h"
#include "plot/meshprofilesampler.h"

#include <QObject>
#include <QTest>
#include <QVector>

#include <cmath>
#include <limits>

using Sample = MeshProfileSampler::Sample;

namespace {

// Build a sample with chainage c, bed elevation g, current depth d.
Sample mk(double c, double g, double d)
{
    Sample s;
    s.chainage = c;
    s.ground   = g;
    s.depthNow = d;
    s.maxDepth = d;
    return s;
}

// The wet-band "top" the painter passes for the depth-fill / WSE passes.
double topDepth(const Sample &s) { return s.ground + std::max(0.0, s.depthNow); }

QVector<double> tops(const QVector<Sample> &s)
{
    return MeshProfileInterp::bridgedTops(s, topDepth);
}

} // namespace

class TestMeshProfileInterp : public QObject
{
    Q_OBJECT
private slots:

    // A dry saddle between two wet runs is bridged into one continuous surface,
    // and the bridged surface is monotonic non-increasing toward the (lower)
    // downstream end — water never pools higher than its upstream source.
    void bridges_dry_saddle_monotonically()
    {
        // Flat bed at 10; upstream WSE 12 (idx1) → downstream WSE 10.5 (idx4).
        const QVector<Sample> s = {
            mk(0, 10, 3.0),   // WSE 13
            mk(1, 10, 2.0),   // WSE 12  (upstream run end)
            mk(2, 10, 0.0),   // dry gap
            mk(3, 10, 0.0),   // dry gap
            mk(4, 10, 0.5),   // WSE 10.5 (downstream run start)
            mk(5, 10, 0.3),   // WSE 10.3
        };
        const QVector<double> t = tops(s);

        // Continuity: nothing dropped — every flat-bed sample is wet.
        for (int i = 0; i < t.size(); ++i)
            QVERIFY2(!std::isnan(t[i]), qPrintable(QString("idx %1 NaN").arg(i)));

        // Interior bridged values: linear in chainage between WSE 12 and 10.5.
        QVERIFY(std::abs(t[2] - 11.5) < 1e-9);
        QVERIFY(std::abs(t[3] - 11.0) < 1e-9);

        // Monotonic non-increase from the upstream run end to the far end, and
        // never above the upstream level (12).
        for (int i = 1; i + 1 < t.size(); ++i)
            QVERIFY(t[i + 1] <= t[i] + 1e-12);
        for (int i = 2; i <= 3; ++i)
            QVERIFY(t[i] <= 12.0 + 1e-12);
    }

    // A bed crest inside the gap stays dry (depth → 0): water follows the bed
    // rather than flowing uphill over the hill, splitting the run at the crest.
    void crest_in_gap_follows_bed()
    {
        const QVector<Sample> s = {
            mk(0, 10, 2.0),   // WSE 12
            mk(1, 10, 1.0),   // WSE 11  (upstream run end)
            mk(2, 10.5, 0.0), // gap, bed below the bridged surface → wet
            mk(3, 13.0, 0.0), // CREST: bed above any bridged surface → dry
            mk(4, 10.5, 0.0), // gap, bed below → wet
            mk(5, 10, 1.0),   // WSE 11  (downstream run start)
            mk(6, 10, 2.0),   // WSE 12
        };
        const QVector<double> t = tops(s);

        // The crest is dry; its neighbours in the gap are bridged wet (~11).
        QVERIFY2(std::isnan(t[3]), "crest must be dry");
        QVERIFY(!std::isnan(t[2]) && std::abs(t[2] - 11.0) < 1e-9);
        QVERIFY(!std::isnan(t[4]) && std::abs(t[4] - 11.0) < 1e-9);
        // Bridged water never sits below the bed.
        for (int i = 0; i < t.size(); ++i)
            if (!std::isnan(t[i]))
                QVERIFY(t[i] >= s[i].ground - 1e-12);
    }

    // Genuinely-wet thin films (depth between the paint floor and the hydraulic
    // dry depth ~1e-4) still render instead of being dropped.
    void shallow_films_render()
    {
        const QVector<Sample> s = {
            mk(0, 10, 5e-5),  // film
            mk(1, 10, 6e-5),  // film
        };
        const QVector<double> t = tops(s);
        QVERIFY(!std::isnan(t[0]));
        QVERIFY(!std::isnan(t[1]));

        // Truly zero / negative depth still reads as dry.
        const QVector<Sample> dry = { mk(0, 10, 0.0), mk(1, 10, -1.0) };
        const QVector<double> td = tops(dry);
        QVERIFY(std::isnan(td[0]));
        QVERIFY(std::isnan(td[1]));
    }

    // An off-mesh (NaN ground) sample inside a gap is never bridged across —
    // a genuine mesh hole stays a true gap.
    void offmesh_gap_is_preserved()
    {
        QVector<Sample> s = {
            mk(0, 10, 2.0),                                  // wet
            mk(1, 10, 1.0),                                  // wet (run end)
            mk(2, std::numeric_limits<double>::quiet_NaN(), 0.0), // off-mesh
            mk(3, 10, 1.0),                                  // wet
        };
        const QVector<double> t = tops(s);
        QVERIFY(!std::isnan(t[0]));
        QVERIFY(!std::isnan(t[1]));
        QVERIFY2(std::isnan(t[2]), "off-mesh sample must stay a true gap");
        QVERIFY(!std::isnan(t[3]));
    }
};

QTEST_APPLESS_MAIN(TestMeshProfileInterp)
#include "test_meshprofileinterp.moc"
