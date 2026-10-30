/**
 * @file      test_gageblend.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 *
 * @brief     Leaf QtTest for the volume-conserving rain-gage blend.
 *
 *        The load-bearing case is two gages with MISMATCHED recording
 *        intervals: that is precisely where a naive timestamp-union or a
 *        resample silently changes total rainfall depth, and where the
 *        exact-integral rebin has to hold.
 *
 *        The boxcar semantics asserted here were read off
 *        openswmm.engine/src/engine/hydrology/Gage.cpp::updateAllGages.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include "core/gageblend.h"

#include <cmath>

using namespace GageBlend;

namespace
{
constexpr qint64 kT0 = 1600000000; // arbitrary epoch anchor

// n entries of constant value, spaced exactly `interval` apart.
/**
 *
 * @brief Build evenly timed source records with one unchanged value.
 * @param value Intensity in depth/hour, interval volume in depth units, or repeated
 * cumulative counter value according to type; not incremented for Cumulative.
 * @param interval Recording spacing and declared interval in seconds, normally positive.
 * @param n Number of records; nonpositive produces an empty point vector.
 * @param type Rain encoding, default Intensity.
 * @param start Epoch-second timestamp of the first record, default kT0.
 * @param scale Dimensionless scale factor stored verbatim, default one.
 * @return Owned SourceGage; no validation or sorting. Requires start+i*interval to
 * fit qint64 and manageable n; allocation exceptions propagate.
 *
 */
SourceGage steady(double value, qint64 interval, int n, RainType type = RainType::Intensity,
                  qint64 start = kT0, double scale = 1.0)
{
    SourceGage g;
    g.intervalSec = interval;
    g.rainType = type;
    g.scaleFactor = scale;
    for (int i = 0; i < n; ++i)
        g.points.append({start + i * interval, value});
    return g;
}

/**
 *
 * @brief Compare a scalar using a reference-scaled absolute tolerance.
 * @param a Computed value.
 * @param b Reference value, in the same units.
 * @param relTol Nonnegative tolerance multiplier, default 1e-9, not validated.
 * @return abs(a-b)<=relTol*max(1,abs(b)); this is asymmetric and has a unit-sized
 * absolute floor. NaNs compare false; no data mutation.
 *
 */
bool near(double a, double b, double relTol = 1e-9)
{
    return std::abs(a - b) <= relTol * std::max(1.0, std::abs(b));
}
} // namespace

class TestGageBlend : public QObject
{
    Q_OBJECT

private slots:
    // ── toBoxes: engine semantics ───────────────────────────────────────
    /**
     *
     * @brief Require sparse intensity records to apply only over each declared 900-s box,
     * preserving two units of depth across the dry gap.
     *
     */
    void boxes_intensityIsABoxcarNotAHold()
    {
        // Two entries 3600 s apart but a 900 s interval: rainfall applies for
        // 900 s after each entry and is ZERO for the remaining 2700 s.
        SourceGage g;
        g.intervalSec = 900;
        g.points = {{kT0, 4.0}, {kT0 + 3600, 4.0}};

        const QVector<Box> b = toBoxes(g);
        QCOMPARE(b.size(), 2);
        QCOMPARE(b[0].t0, kT0);
        QCOMPARE(b[0].t1, kT0 + 900); // NOT kT0 + 3600
        QCOMPARE(b[1].t1, kT0 + 3600 + 900);

        // Depth = 4 in/hr * 0.25 h * 2 boxes = 2.0
        QVERIFY(near(boxDepth(b), 2.0));
    }

    // Entries closer together than the declared interval: the later entry
    // preempts the earlier box. Without the min() the depth is inflated.
    /**
     *
     * @brief Require a later record to truncate the prior recording box rather than double-count
     * overlap, yielding depth 12.5.
     *
     */
    void boxes_earlyEntryPreemptsPreviousBox()
    {
        SourceGage g;
        g.intervalSec = 3600;
        g.points = {{kT0, 10.0}, {kT0 + 900, 10.0}};

        const QVector<Box> b = toBoxes(g);
        QCOMPARE(b.size(), 2);
        QCOMPARE(b[0].t1, kT0 + 900); // cut short, not kT0 + 3600
        QCOMPARE(b[1].t1, kT0 + 900 + 3600);

        // 10 * 0.25 + 10 * 1.0 = 12.5, not 10 * 1.0 + 10 * 1.0 = 20.
        QVERIFY(near(boxDepth(b), 12.5));
    }

    /**
     *
     * @brief Require quarter-unit 15-minute volume records to become unit/hour intensity and
     * conserve unit total depth.
     *
     */
    void boxes_volumeConvertsToIntensity()
    {
        // 0.25 in per 15-min interval == 1.0 in/hr.
        const QVector<Box> b = toBoxes(steady(0.25, 900, 4, RainType::Volume));
        QCOMPARE(b.size(), 4);
        QVERIFY(near(b[0].intensity, 1.0));
        QVERIFY(near(boxDepth(b), 1.0)); // 4 * 0.25 in
    }

    /**
     *
     * @brief Require cumulative records 1,3,6 to become hourly increments 1,2,3 with total depth
     * six.
     *
     */
    void boxes_cumulativeTakesDeltas()
    {
        SourceGage g;
        g.intervalSec = 3600;
        g.rainType = RainType::Cumulative;
        g.points = {{kT0, 1.0}, {kT0 + 3600, 3.0}, {kT0 + 7200, 6.0}};

        const QVector<Box> b = toBoxes(g);
        QCOMPARE(b.size(), 3);
        QVERIFY(near(b[0].intensity, 1.0)); // first value is its own depth
        QVERIFY(near(b[1].intensity, 2.0));
        QVERIFY(near(b[2].intensity, 3.0));
        QVERIFY(near(boxDepth(b), 6.0));
    }

    /**
     *
     * @brief Require a counter drop from five to two to restart at two rather than produce
     * negative rainfall.
     *
     */
    void boxes_cumulativeResetOnDecrease()
    {
        SourceGage g;
        g.intervalSec = 3600;
        g.rainType = RainType::Cumulative;
        // 5 -> 2 is a counter reset: 2 is the whole depth, not -3.
        g.points = {{kT0, 5.0}, {kT0 + 3600, 2.0}};

        const QVector<Box> b = toBoxes(g);
        QVERIFY(near(b[1].intensity, 2.0));
        QVERIFY(near(boxDepth(b), 7.0));
    }

    /**
     *
     * @brief Require a 2.5 scale multiplier to affect intensity and integrated depth.
     *
     */
    void boxes_scaleFactorIsFolded()
    {
        const QVector<Box> b = toBoxes(steady(2.0, 3600, 3, RainType::Intensity, kT0, 2.5));
        QVERIFY(near(b[0].intensity, 5.0));
        QVERIFY(near(boxDepth(b), 15.0));
    }

    // ── blend: the single-source identity ───────────────────────────────
    /**
     *
     * @brief Require one unit-weight gage to retain its 900-s pitch and integrated six-unit depth
     * with no error.
     *
     */
    void blend_singleGageReproducesItsDepth()
    {
        const SourceGage g = steady(3.0, 900, 8);
        const BlendResult r = blend({g}, {1.0});

        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.intervalSec, 900LL);
        QVERIFY(r.volumeOk());
        QVERIFY(near(r.blendedDepth, boxDepth(toBoxes(g))));
        QVERIFY(near(r.blendedDepth, 3.0 * 8.0 * 0.25));
    }

    /**
     *
     * @brief Require weight 0.5 to halve integrated depth rather than renormalize it to unity.
     *
     */
    void blend_halfWeightHalvesTheDepth()
    {
        const SourceGage g = steady(3.0, 900, 8);
        const BlendResult r = blend({g}, {0.5});
        QVERIFY(r.volumeOk());
        QVERIFY(near(r.blendedDepth, 0.5 * 3.0 * 8.0 * 0.25));
    }

    // ── blend: THE case — mismatched intervals ──────────────────────────
    // 15-minute and 7-minute gages have a gcd of 60 s. A timestamp union or a
    // resample changes the total here; the exact-integral rebin must not.
    /**
     *
     * @brief Require offset 900-s/420-s records to blend at 60-s gcd pitch with their weighted
     * reference depth conserved.
     *
     */
    void blend_mismatchedIntervalsConserveVolume()
    {
        const SourceGage a = steady(4.0, 900, 8); // 15 min x 8 = 2 h
        const SourceGage b = steady(6.0, 420, 12, RainType::Intensity,
                                    kT0 + 137); // 7 min, offset
        const QVector<double> w{0.6, 0.4};

        const BlendResult r = blend({a, b}, w);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.intervalSec, 60LL); // gcd(900, 420) = 60

        const double want = 0.6 * boxDepth(toBoxes(a)) + 0.4 * boxDepth(toBoxes(b));
        QVERIFY(near(r.referenceDepth, want));
        QVERIFY2(r.volumeOk(),
                 qPrintable(QStringLiteral("relative error %1").arg(r.relativeError, 0, 'g', 6)));
        QVERIFY(near(r.blendedDepth, want));
    }

    /**
     *
     * @brief Require equivalent hourly intensity, volume and cumulative encodings each to retain
     * five units of depth.
     *
     */
    void blend_mixedRainTypesConserveVolume()
    {
        // Same physical rainfall expressed three ways must blend to one total.
        const SourceGage asIntensity = steady(1.0, 3600, 5);
        const SourceGage asVolume = steady(1.0, 3600, 5, RainType::Volume);
        SourceGage asCumulative;
        asCumulative.intervalSec = 3600;
        asCumulative.rainType = RainType::Cumulative;
        for (int i = 0; i < 5; ++i)
            asCumulative.points.append({kT0 + i * 3600, static_cast<double>(i + 1)});

        for (const SourceGage &g : {asIntensity, asVolume, asCumulative}) {
            const BlendResult r = blend({g}, {1.0});
            QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
            QVERIFY(r.volumeOk());
            QVERIFY2(near(r.blendedDepth, 5.0),
                     qPrintable(QStringLiteral("depth %1").arg(r.blendedDepth)));
        }
    }

    /**
     *
     * @brief Require misaligned equal-interval gages to conserve weighted depth through
     * partial-overlap rebinning.
     *
     */
    void blend_offsetSameIntervalConservesVolume()
    {
        // Identical intervals, misaligned starts — pitch stays 900 s and the
        // partial-overlap arithmetic has to carry the depth.
        const SourceGage a = steady(2.0, 900, 6);
        const SourceGage b = steady(8.0, 900, 6, RainType::Intensity, kT0 + 450);

        const BlendResult r = blend({a, b}, {0.5, 0.5});
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.volumeOk());
        QVERIFY(near(r.blendedDepth, 0.5 * boxDepth(toBoxes(a)) + 0.5 * boxDepth(toBoxes(b))));
    }

    // ── Emission rules ──────────────────────────────────────────────────
    /**
     *
     * @brief Require sparse emitted points across a four-hour dry gap while retaining first/last
     * wet-edge timestamps and volume.
     *
     */
    void blend_dropsInteriorZerosButKeepsEdges()
    {
        SourceGage g;
        g.intervalSec = 900;
        // A dry gap in the middle: entries at 0 and at +4h.
        g.points = {{kT0, 5.0}, {kT0 + 4 * 3600, 5.0}};

        const BlendResult r = blend({g}, {1.0});
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.volumeOk());

        // Far fewer than the 17 grid cells the span spans.
        QVERIFY(r.points.size() < 8);
        QCOMPARE(r.points.first().t, kT0);            // first cell retained
        QVERIFY(r.points.last().t >= kT0 + 4 * 3600); // last cell retained
    }

    /**
     *
     * @brief Require a one-cell/one-record blend to fail with an empty series, avoiding ambiguous
     * engine single-point rain semantics.
     *
     */
    void blend_neverEmitsASinglePointSeries()
    {
        // One entry over one cell would emit a 1-point table, which the engine
        // reads as raining at ALL times before the first entry.
        SourceGage g;
        g.intervalSec = 900;
        g.points = {{kT0, 5.0}};

        const BlendResult r = blend({g}, {1.0});
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.points.isEmpty());
    }

    /**
     *
     * @brief Require a 300-s gcd output pitch and emitted intensity records to reintegrate to the
     * reported blended depth.
     *
     */
    void blend_outputIsIntensityAtTheGcdPitch()
    {
        const BlendResult r = blend({steady(4.0, 900, 4), steady(4.0, 600, 6)}, {0.5, 0.5});
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.intervalSec, 300LL); // gcd(900, 600)

        // Round-tripping the emitted series through the engine rule as
        // INTENSITY at that pitch must return the same depth.
        QVERIFY(near(engineDepth(r.points, r.intervalSec), r.blendedDepth));
    }

    // ── Refusals ────────────────────────────────────────────────────────
    /**
     *
     * @brief Require no source/weight inputs to return a nonempty blend error.
     *
     */
    void refuse_emptyInput() { QVERIFY(!blend({}, {}).error.isEmpty()); }

    /**
     *
     * @brief Require unequal source and weight counts to return a blend error.
     *
     */
    void refuse_weightCountMismatch()
    {
        QVERIFY(!blend({steady(1.0, 900, 4)}, {0.5, 0.5}).error.isEmpty());
    }

    /**
     *
     * @brief Require a 30-s interval to fail with a whole-minutes diagnostic.
     *
     */
    void refuse_subMinuteInterval()
    {
        // 30 s cannot be written as h:mm.
        const BlendResult r = blend({steady(1.0, 30, 10)}, {1.0});
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.error.contains(QStringLiteral("whole minutes")));
    }

    /**
     *
     * @brief Require a 90-s interval to fail the whole-minute output contract.
     *
     */
    void refuse_nonWholeMinuteInterval()
    {
        QVERIFY(!blend({steady(1.0, 90, 10)}, {1.0}).error.isEmpty());
    }

    /**
     *
     * @brief Require a 25-hour recording interval to be rejected.
     *
     */
    void refuse_intervalOverADay()
    {
        QVERIFY(!blend({steady(1.0, 25 * 3600, 4)}, {1.0}).error.isEmpty());
    }

    /**
     *
     * @brief Require a declared interval without rainfall points to be rejected.
     *
     */
    void refuse_emptySeries()
    {
        SourceGage g;
        g.intervalSec = 900;
        QVERIFY(!blend({g}, {1.0}).error.isEmpty());
    }

    /**
     *
     * @brief Require gages separated by roughly ten years to fail with a time-bases diagnostic.
     *
     */
    void refuse_disjointTimeBases()
    {
        // One series near the epoch anchor, one ~10 years away — the signature
        // of relative-hours data mixed with absolute dates.
        const SourceGage a = steady(1.0, 3600, 4);
        const SourceGage b =
            steady(1.0, 3600, 4, RainType::Intensity, kT0 + 10LL * 365 * 24 * 3600);
        const BlendResult r = blend({a, b}, {0.5, 0.5});
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.error.contains(QStringLiteral("time bases")));
    }

    /**
     *
     * @brief Require a long-span 60-s gcd grid to exceed the cap and fail without emitted points.
     *
     */
    void refuse_gridTooLarge()
    {
        // gcd(900, 420) = 60 s; span a year -> ~525k cells, over the cap.
        const SourceGage a = steady(1.0, 900, 2);
        SourceGage b = steady(1.0, 420, 2);
        b.points = {{kT0, 1.0}, {kT0 + 300LL * 24 * 3600, 1.0}};
        const BlendResult r = blend({a, b}, {0.5, 0.5});
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.points.isEmpty());
    }

    // ── Reported figures ────────────────────────────────────────────────
    /**
     *
     * @brief Require a blended spike to conserve volume and report positive peak retention at
     * most one.
     * The case imposes no minimum retention-quality threshold.
     *
     */
    void peakRetentionIsReportedNotEnforced()
    {
        // A short spike on a fine gage blended with a coarse one attenuates.
        SourceGage spike;
        spike.intervalSec = 3600;
        spike.points = {{kT0, 0.0}, {kT0 + 3600, 100.0}, {kT0 + 7200, 0.0}};
        const SourceGage flat = steady(1.0, 3600, 3);

        const BlendResult r = blend({spike, flat}, {0.5, 0.5});
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.volumeOk()); // volume still conserved
        QVERIFY(r.peakRetention() <= 1.0 + 1e-12);
        QVERIFY(r.peakRetention() > 0.0);
    }
};

QTEST_MAIN(TestGageBlend)
#include "test_gageblend.moc"
