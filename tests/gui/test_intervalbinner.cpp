/**
 * @file      test_intervalbinner.cpp
 * @brief     Tests for intervalbinner.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice BB-α — IntervalBinner. Self-contained: pulls in
 * render/intervalbinner.cpp only.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/intervalbinner.h"

#include <QJsonObject>
#include <QObject>
#include <QTest>

#include <cmath>

using namespace OpenSWMM::Render;

class TestIntervalBinner : public QObject
{
    Q_OBJECT
private slots:

    // ---- Equal-interval -------------------------------------------------------

    /**
     *
     * @brief Require five equal intervals over [0,100] to produce breaks 20,40,60,80.
     *
     */
    void equalInterval_evenly_spaces_breaks_between_min_and_max()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::EqualInterval);
        b.setBinCount(5);
        // Samples spanning 0..100. Breaks = {20, 40, 60, 80}.
        const QVector<double> samples = {0.0, 25.0, 50.0, 75.0, 100.0};
        const auto br = b.computeBreaks(samples);
        QCOMPARE(br.size(), 4);
        QCOMPARE(br[0], 20.0);
        QCOMPARE(br[1], 40.0);
        QCOMPARE(br[2], 60.0);
        QCOMPARE(br[3], 80.0);
    }

    /**
     *
     * @brief Check below/above-range saturation and representative assignments across five
     * explicit bins.
     *
     */
    void equalInterval_binFor_maps_value_to_correct_bin()
    {
        IntervalBinner b;
        b.setBinCount(5);
        const QVector<double> br = {20.0, 40.0, 60.0, 80.0};
        QCOMPARE(b.binFor(-5.0, br), 0);
        QCOMPARE(b.binFor(10.0, br), 0);
        QCOMPARE(b.binFor(30.0, br), 1);
        QCOMPARE(b.binFor(50.0, br), 2);
        QCOMPARE(b.binFor(70.0, br), 3);
        QCOMPARE(b.binFor(90.0, br), 4);
        QCOMPARE(b.binFor(200.0, br), 4);
    }

    // ---- Quantile -------------------------------------------------------------

    /**
     *
     * @brief Require three four-class quantile breaks near the expected quartiles of 1..100.
     *
     */
    void quantile_partitions_finite_samples_evenly()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::Quantile);
        b.setBinCount(4);
        QVector<double> samples;
        for (int i = 1; i <= 100; ++i)
            samples.append(static_cast<double>(i));
        const auto br = b.computeBreaks(samples);
        QCOMPARE(br.size(), 3);
        // Linear-interpolated quartile positions give values 25.75, 50.5, 75.25;
        // the checks below deliberately use bounded neighborhoods.
        QVERIFY(br[0] > 24.0 && br[0] < 26.0);
        QVERIFY(br[1] > 48.0 && br[1] < 51.0);
        QVERIFY(br[2] > 73.0 && br[2] < 75.5);
    }

    // ---- Manual ---------------------------------------------------------------

    /**
     *
     * @brief Require manual break values 3,9,27 to survive sample processing unchanged.
     *
     */
    void manual_returns_user_supplied_breaks_verbatim()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::Manual);
        b.setBinCount(4);
        b.setManualBreaks({3.0, 9.0, 27.0});
        const QVector<double> samples = {1.0, 5.0, 10.0, 20.0, 30.0};
        const auto br = b.computeBreaks(samples);
        QCOMPARE(br.size(), 3);
        QCOMPARE(br[0], 3.0);
        QCOMPARE(br[1], 9.0);
        QCOMPARE(br[2], 27.0);
    }

    // ---- Edge cases -----------------------------------------------------------

    /**
     *
     * @brief Require empty data and four classes to produce sentinel breaks .5,1.5,2.5.
     *
     */
    void empty_samples_emit_sentinel_breaks()
    {
        IntervalBinner b;
        b.setBinCount(4);
        const auto br = b.computeBreaks({});
        QCOMPARE(br.size(), 3);
        // Sentinel form: {0.5, 1.5, 2.5}.
        QCOMPARE(br[0], 0.5);
        QCOMPARE(br[1], 1.5);
        QCOMPARE(br[2], 2.5);
    }

    /**
     *
     * @brief Require constant data and three classes to produce sentinel breaks .5,1.5.
     *
     */
    void constant_samples_collapse_to_sentinel_breaks()
    {
        IntervalBinner b;
        b.setBinCount(3);
        const auto br = b.computeBreaks({7.0, 7.0, 7.0, 7.0});
        QCOMPARE(br.size(), 2);
        QCOMPARE(br[0], 0.5);
        QCOMPARE(br[1], 1.5);
    }

    /**
     *
     * @brief Require nonfinite samples to be ignored while finite endpoints yield midpoint break
     * 5.
     *
     */
    void nan_and_inf_are_skipped()
    {
        IntervalBinner b;
        b.setBinCount(2);
        const auto br =
            b.computeBreaks({std::nan(""), std::numeric_limits<double>::infinity(), 0.0, 10.0});
        QCOMPARE(br.size(), 1);
        QCOMPARE(br[0], 5.0);
    }

    // ---- JSON round-trip ------------------------------------------------------

    /**
     *
     * @brief Round-trip quantile method, seven bins and the stored manual-break list metadata.
     *
     */
    void json_round_trip()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::Quantile);
        b.setBinCount(7);
        b.setManualBreaks({1.0, 2.0, 3.0});
        const QJsonObject obj = b.toJson();
        const IntervalBinner b2 = IntervalBinner::fromJson(obj);
        QCOMPARE(b2.method(), BinMethod::Quantile);
        QCOMPARE(b2.binCount(), 7);
        QCOMPARE(b2.manualBreaks().size(), 3);
        QCOMPARE(b2.manualBreaks().first(), 1.0);
        QCOMPARE(b2.manualBreaks().last(), 3.0);
    }

    // ---- VS.3 — Natural breaks (Jenks) ----------------------------------------

    /**
     *
     * @brief Require two-cluster natural breaks to select the low-cluster maximum 3.
     *
     */
    void jenks_splits_two_obvious_clusters()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::NaturalBreaks);
        b.setBinCount(2);
        // Two tight clusters; the natural boundary is the top of the low one.
        const auto br = b.computeBreaks({1.0, 2.0, 3.0, 50.0, 51.0, 52.0});
        QCOMPARE(br.size(), 1);
        QCOMPARE(br[0], 3.0);
    }

    /**
     *
     * @brief Require two-class Jenks on values 1..10 to break at 5.
     *
     */
    void jenks_even_ramp_breaks_at_midpoint()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::NaturalBreaks);
        b.setBinCount(2);
        QVector<double> samples;
        for (int i = 1; i <= 10; ++i)
            samples.append(static_cast<double>(i));
        const auto br = b.computeBreaks(samples);
        QCOMPARE(br.size(), 1);
        QCOMPARE(br[0], 5.0);
    }

    /**
     *
     * @brief Require three-class Jenks to return two strictly ascending breaks inside the sample
     * range.
     *
     */
    void jenks_three_classes_are_ascending_and_in_range()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::NaturalBreaks);
        b.setBinCount(3);
        const QVector<double> samples = {1, 2, 3, 4, 20, 21, 22, 80, 81, 82};
        const auto br = b.computeBreaks(samples);
        QCOMPARE(br.size(), 2);
        QVERIFY(br[0] < br[1]);
        QVERIFY(br[0] >= 1.0 && br[1] <= 82.0);
    }

    // ---- VS.3 — Standard deviation --------------------------------------------

    /**
     *
     * @brief Require mean 5/population sigma 2 to yield four-class breaks 3,5,7.
     *
     */
    void stddev_breaks_symmetric_about_mean()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::StdDev);
        b.setBinCount(4);
        // Classic fixture: mean = 5, population sigma = 2.
        const auto br = b.computeBreaks({2, 4, 4, 4, 5, 5, 7, 9});
        QCOMPARE(br.size(), 3);
        QCOMPARE(br[0], 3.0); // mean - sigma
        QCOMPARE(br[1], 5.0); // mean
        QCOMPARE(br[2], 7.0); // mean + sigma
    }

    // ---- VS.3 — Logarithmic ---------------------------------------------------

    /**
     *
     * @brief Require three logarithmic classes over 1..1000 to break at 10 and 100 within 1e-6.
     *
     */
    void log_breaks_evenly_spaced_in_log10()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::Logarithmic);
        b.setBinCount(3);
        const auto br = b.computeBreaks({1.0, 10.0, 100.0, 1000.0});
        QCOMPARE(br.size(), 2);
        QVERIFY(std::abs(br[0] - 10.0) < 1e-6);
        QVERIFY(std::abs(br[1] - 100.0) < 1e-6);
    }

    /**
     *
     * @brief Require nonpositive logarithmic input to return three ascending fallback breaks.
     * Does not assert their exact expected equal-interval values.
     *
     */
    void log_falls_back_to_equal_interval_for_nonpositive_min()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::Logarithmic);
        b.setBinCount(4);
        const auto br = b.computeBreaks({0.0, 30.0, 60.0, 90.0});
        QCOMPARE(br.size(), 3);
        // Equal-interval fallback over [0, 90]: {22.5, 45, 67.5}, ascending.
        QVERIFY(br[0] < br[1] && br[1] < br[2]);
    }

    // ---- VS.3 — Exponential ---------------------------------------------------

    /**
     *
     * @brief Check base-two cumulative break values and increasing first three interval widths
     * over [0,100].
     *
     */
    void exponential_band_widths_double()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::Exponential);
        b.setBinCount(4);
        const auto br = b.computeBreaks({0.0, 100.0});
        QCOMPARE(br.size(), 3);
        // Geometric base-2 over [0,100]: 100*{1,3,7}/15.
        QVERIFY(std::abs(br[0] - (100.0 * 1.0 / 15.0)) < 1e-6);
        QVERIFY(std::abs(br[1] - 20.0) < 1e-6);
        QVERIFY(std::abs(br[2] - (100.0 * 7.0 / 15.0)) < 1e-6);
        // Each band wider than the last.
        const double w0 = br[0];
        const double w1 = br[1] - br[0];
        const double w2 = br[2] - br[1];
        QVERIFY(w1 > w0 && w2 > w1);
    }

    // ---- VS.3 — JSON round-trips for the new methods --------------------------

    /**
     *
     * @brief Round-trip method identity and five-bin count for Jenks, standard deviation, log and
     * exponential methods.
     *
     */
    void json_round_trip_new_methods()
    {
        for (BinMethod m : {BinMethod::NaturalBreaks, BinMethod::StdDev, BinMethod::Logarithmic,
                            BinMethod::Exponential}) {
            IntervalBinner b;
            b.setMethod(m);
            b.setBinCount(5);
            const IntervalBinner b2 = IntervalBinner::fromJson(b.toJson());
            QCOMPARE(b2.method(), m);
            QCOMPARE(b2.binCount(), 5);
        }
    }
};

QTEST_APPLESS_MAIN(TestIntervalBinner)
#include "test_intervalbinner.moc"
