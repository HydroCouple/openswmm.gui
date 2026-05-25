/*!
 * \file   test_intervalbinner.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BB-α — IntervalBinner. Self-contained: pulls in
 * render/intervalbinner.cpp only.
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

    void equalInterval_binFor_maps_value_to_correct_bin()
    {
        IntervalBinner b;
        b.setBinCount(5);
        const QVector<double> br = {20.0, 40.0, 60.0, 80.0};
        QCOMPARE(b.binFor( -5.0, br), 0);
        QCOMPARE(b.binFor( 10.0, br), 0);
        QCOMPARE(b.binFor( 30.0, br), 1);
        QCOMPARE(b.binFor( 50.0, br), 2);
        QCOMPARE(b.binFor( 70.0, br), 3);
        QCOMPARE(b.binFor( 90.0, br), 4);
        QCOMPARE(b.binFor(200.0, br), 4);
    }

    // ---- Quantile -------------------------------------------------------------

    void quantile_partitions_finite_samples_evenly()
    {
        IntervalBinner b;
        b.setMethod(BinMethod::Quantile);
        b.setBinCount(4);
        QVector<double> samples;
        for (int i = 1; i <= 100; ++i) samples.append(static_cast<double>(i));
        const auto br = b.computeBreaks(samples);
        QCOMPARE(br.size(), 3);
        // Linear-interp quantiles at i*(n-1)/nBins: 24.75, 49.5, 74.25
        QVERIFY(br[0] > 24.0 && br[0] < 26.0);
        QVERIFY(br[1] > 48.0 && br[1] < 51.0);
        QVERIFY(br[2] > 73.0 && br[2] < 75.5);
    }

    // ---- Manual ---------------------------------------------------------------

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

    void constant_samples_collapse_to_sentinel_breaks()
    {
        IntervalBinner b;
        b.setBinCount(3);
        const auto br = b.computeBreaks({7.0, 7.0, 7.0, 7.0});
        QCOMPARE(br.size(), 2);
        QCOMPARE(br[0], 0.5);
        QCOMPARE(br[1], 1.5);
    }

    void nan_and_inf_are_skipped()
    {
        IntervalBinner b;
        b.setBinCount(2);
        const auto br = b.computeBreaks({std::nan(""),
                                         std::numeric_limits<double>::infinity(),
                                         0.0,
                                         10.0});
        QCOMPARE(br.size(), 1);
        QCOMPARE(br[0], 5.0);
    }

    // ---- JSON round-trip ------------------------------------------------------

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
        QCOMPARE(b2.manualBreaks().last(),  3.0);
    }
};

QTEST_APPLESS_MAIN(TestIntervalBinner)
#include "test_intervalbinner.moc"
