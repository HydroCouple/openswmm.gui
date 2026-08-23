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
// cellHasSurface defaults to false (no-data), which keeps the legacy
// bridge-every-gap behaviour for the pre-existing cases below.
Sample mk(double c, double g, double d, bool hasSurface = false)
{
    Sample s;
    s.chainage = c;
    s.ground   = g;
    s.depthNow = d;
    s.maxDepth = d;
    s.cellHasSurface = hasSurface;
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

    // WSE-extrapolation plan, Phase 3 — two pools separated by a dry crest
    // whose samples ALL carry a valid free surface (cellHasSurface = true):
    // the gap is genuinely dry ground, not missing data, so it must NOT be
    // bridged. Each pool stays flat at its own level; the crest stays dry.
    // Before the restriction this ramped 2.0 → 1.0 across the gap, painting
    // water up the crest flanks (t[2] would read 1.75, t[4] 1.25).
    void pools_stay_split_when_gap_carries_surface_data()
    {
        const QVector<Sample> s = {
            mk(0, 0.0, 2.0, true),   // left pool, WSE 2
            mk(1, 0.0, 2.0, true),   // left pool run end
            mk(2, 0.5, 0.0, true),   // gap: low bench — a legacy bridge would wet it
            mk(3, 6.0, 0.0, true),   // gap: crest
            mk(4, 0.5, 0.0, true),   // gap: low bench on the far side
            mk(5, 0.0, 1.0, true),   // right pool, WSE 1
            mk(6, 0.0, 1.0, true),
        };
        const QVector<double> t = tops(s);
        for (int i = 2; i <= 4; ++i)
            QVERIFY2(std::isnan(t[i]),
                     qPrintable(QString("gap idx %1 must stay dry").arg(i)));
        QVERIFY(std::abs(t[0] - 2.0) < 1e-12 && std::abs(t[1] - 2.0) < 1e-12);
        QVERIFY(std::abs(t[5] - 1.0) < 1e-12 && std::abs(t[6] - 1.0) < 1e-12);
    }

    // Discriminator twin: the SAME geometry with no-data samples in the gap
    // (cellHasSurface = false) is a true data hole and still bridges exactly
    // as before — linear in chainage, dry over the crest.
    void pools_bridge_when_gap_is_no_data()
    {
        const QVector<Sample> s = {
            mk(0, 0.0, 2.0, true),
            mk(1, 0.0, 2.0, true),
            mk(2, 0.5, 0.0, false),
            mk(3, 6.0, 0.0, false),
            mk(4, 0.5, 0.0, false),
            mk(5, 0.0, 1.0, true),
            mk(6, 0.0, 1.0, true),
        };
        const QVector<double> t = tops(s);
        QVERIFY(!std::isnan(t[2]) && std::abs(t[2] - 1.75) < 1e-12);
        QVERIFY2(std::isnan(t[3]), "crest above the bridged line stays dry");
        QVERIFY(!std::isnan(t[4]) && std::abs(t[4] - 1.25) < 1e-12);
    }

    // ── Shoreline intercepts (premature-truncation fix) ────────────────────
    // The painted band must taper to the exact WSE/ground crossing between the
    // last wet sample and its dry neighbour, not stop with a vertical cliff.

    // Flat pool (WSE 1.0) over a rising bed: ground passes 1.0 at chainage
    // 1.5, halfway between the last wet sample (c=1, g=0.5) and the dry one
    // (c=2, g=1.5). The trailing intercept must land there, on the ground.
    void trailing_intercept_lands_on_ground()
    {
        const QVector<Sample> s = {
            mk(0, 0.0, 1.0, true),   // WSE 1.0
            mk(1, 0.5, 0.5, true),   // WSE 1.0 (run end)
            mk(2, 1.5, 0.0, true),   // dry, ground above the pool
        };
        const QVector<double> t = tops(s);
        QVERIFY(!std::isnan(t[0]) && !std::isnan(t[1]) && std::isnan(t[2]));
        double c = 0.0, e = 0.0;
        QVERIFY(MeshProfileInterp::shorelineIntercept(s, t, 0, 1, true, &c, &e));
        QVERIFY(std::abs(c - 1.5) < 1e-12);
        QVERIFY(std::abs(e - 1.0) < 1e-12);
    }

    // Mirror geometry: the leading intercept of a run is found symmetrically.
    void leading_intercept_lands_on_ground()
    {
        const QVector<Sample> s = {
            mk(0, 1.5, 0.0, true),   // dry, ground above the pool
            mk(1, 0.5, 0.5, true),   // WSE 1.0 (run start)
            mk(2, 0.0, 1.0, true),   // WSE 1.0
        };
        const QVector<double> t = tops(s);
        double c = 0.0, e = 0.0;
        QVERIFY(MeshProfileInterp::shorelineIntercept(s, t, 1, 2, false, &c, &e));
        QVERIFY(std::abs(c - 0.5) < 1e-12);
        QVERIFY(std::abs(e - 1.0) < 1e-12);
    }

    // A sloping surface is extrapolated from the wet side (last two wet
    // samples), not interpolated toward the dry sample (which carries no
    // surface). Surface 2.0 → 1.8 over one unit (slope −0.2) meets ground
    // rising 0 → 3 at t = 1.8/3.2 past the run end.
    void sloped_surface_intercept_extrapolates_wet_side()
    {
        const QVector<Sample> s = {
            mk(0, 0.0, 2.0, true),
            mk(1, 0.0, 1.8, true),   // run end, WSE 1.8
            mk(2, 3.0, 0.0, true),   // dry wall face
        };
        const QVector<double> t = tops(s);
        double c = 0.0, e = 0.0;
        QVERIFY(MeshProfileInterp::shorelineIntercept(s, t, 0, 1, true, &c, &e));
        const double tExp = 1.8 / 3.2;
        QVERIFY(std::abs(c - (1.0 + tExp)) < 1e-12);
        QVERIFY(std::abs(e - 3.0 * tExp) < 1e-12);
        // The intercept sits on the extrapolated surface too: 1.8 − 0.2·t.
        QVERIFY(std::abs(e - (1.8 - 0.2 * tExp)) < 1e-12);
    }

    // No intercept when the dry neighbour's ground stays BELOW the surface —
    // a no-data / unbridged termination (e.g. the split flank of a pool over
    // a low bench) keeps its hard edge rather than inventing a crossing.
    void no_intercept_when_ground_stays_below_surface()
    {
        const QVector<Sample> s = {
            mk(0, 0.0, 1.0, true),
            mk(1, 0.0, 1.0, true),   // run end, WSE 1.0
            mk(2, 0.2, 0.0, true),   // dry, but ground (0.2) below WSE (1.0)
        };
        const QVector<double> t = tops(s);
        QVERIFY(!MeshProfileInterp::shorelineIntercept(s, t, 0, 1, true,
                                                       nullptr, nullptr));
    }

    // No intercept at an off-mesh (NaN ground) neighbour or at the data edge.
    void no_intercept_at_offmesh_or_data_edge()
    {
        const QVector<Sample> s = {
            mk(0, 0.0, 1.0, true),
            mk(1, 0.5, 0.5, true),
            mk(2, std::numeric_limits<double>::quiet_NaN(), 0.0, true),
        };
        const QVector<double> t = tops(s);
        // Off-mesh neighbour on the trailing side.
        QVERIFY(!MeshProfileInterp::shorelineIntercept(s, t, 0, 1, true,
                                                       nullptr, nullptr));
        // Leading side of a run starting at sample 0: data edge.
        QVERIFY(!MeshProfileInterp::shorelineIntercept(s, t, 0, 1, false,
                                                       nullptr, nullptr));
    }

    // A single-sample run extends flat: the intercept is where the ground
    // rises through that sample's WSE.
    void single_sample_run_extends_flat()
    {
        const QVector<Sample> s = {
            mk(0, 2.0, 0.0, true),   // dry
            mk(1, 0.0, 1.0, true),   // lone wet sample, WSE 1.0
            mk(2, 2.0, 0.0, true),   // dry
        };
        const QVector<double> t = tops(s);
        double c = 0.0, e = 0.0;
        QVERIFY(MeshProfileInterp::shorelineIntercept(s, t, 1, 1, true, &c, &e));
        QVERIFY(std::abs(c - 1.5) < 1e-12 && std::abs(e - 1.0) < 1e-12);
        QVERIFY(MeshProfileInterp::shorelineIntercept(s, t, 1, 1, false, &c, &e));
        QVERIFY(std::abs(c - 0.5) < 1e-12 && std::abs(e - 1.0) < 1e-12);
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
