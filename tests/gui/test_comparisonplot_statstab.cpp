/*!
 * \file   test_comparisonplot_statstab.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AT.3 — pins `computeStatistics()`:
 *   - count, mean, median, stddev, min, max, percentiles, sum
 *   - NaN / Inf values are skipped
 *   - empty input returns count=0 with NaN fields
 *   - percentile interpolation between order statistics
 *
 * Self-contained: links only `seriesstatistics.cpp`.
 */
#include "plot/seriesstatistics.h"

#include <QObject>
#include <QTest>

#include <cmath>
#include <limits>

using openswmmvis::plot::computeStatistics;

class TestComparisonPlotStatsTab : public QObject
{
    Q_OBJECT
private slots:
    void emptyInputReturnsZeroCountAllNan();
    void simpleRangeMeanMedianSum();
    void nanInfSamplesAreSkipped();
    void percentilesAreLinearlyInterpolated();
    void stddevMatchesSampleFormula();
};

void TestComparisonPlotStatsTab::emptyInputReturnsZeroCountAllNan()
{
    const auto s = computeStatistics({});
    QCOMPARE(s.count, 0);
    QVERIFY(std::isnan(s.mean));
    QVERIFY(std::isnan(s.median));
    QVERIFY(std::isnan(s.stddev));
    QVERIFY(std::isnan(s.sum));
}

void TestComparisonPlotStatsTab::simpleRangeMeanMedianSum()
{
    // 1, 2, 3, 4, 5
    const std::vector<double> v = {1, 2, 3, 4, 5};
    const auto s = computeStatistics(v);
    QCOMPARE(s.count, 5);
    QCOMPARE(s.mean, 3.0);
    QCOMPARE(s.median, 3.0);
    QCOMPARE(s.min, 1.0);
    QCOMPARE(s.max, 5.0);
    QCOMPARE(s.sum, 15.0);
}

void TestComparisonPlotStatsTab::nanInfSamplesAreSkipped()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::vector<double> v = {1.0, nan, 2.0, inf, 3.0, -inf, 4.0};
    const auto s = computeStatistics(v);
    QCOMPARE(s.count, 4);
    QCOMPARE(s.mean, 2.5);
    QCOMPARE(s.min, 1.0);
    QCOMPARE(s.max, 4.0);
}

void TestComparisonPlotStatsTab::percentilesAreLinearlyInterpolated()
{
    // For 0..10 (11 values), p50 should be 5; p25 = 2.5; p75 = 7.5.
    const std::vector<double> v = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const auto s = computeStatistics(v);
    QCOMPARE(s.p50, 5.0);
    QCOMPARE(s.p25, 2.5);
    QCOMPARE(s.p75, 7.5);
    // p05 / p95 are interior values for 11 samples.
    QVERIFY(s.p05 > 0.0 && s.p05 < 1.0);    // ≈ 0.5
    QVERIFY(s.p95 > 9.0 && s.p95 < 10.0);   // ≈ 9.5
}

void TestComparisonPlotStatsTab::stddevMatchesSampleFormula()
{
    // 2, 4, 4, 4, 5, 5, 7, 9 → sample stddev = 2.138...
    const std::vector<double> v = {2, 4, 4, 4, 5, 5, 7, 9};
    const auto s = computeStatistics(v);
    QCOMPARE(s.count, 8);
    QCOMPARE(s.mean, 5.0);
    // Sample stddev (N-1 denominator) = sqrt(32/7) ≈ 2.13809
    QVERIFY(std::abs(s.stddev - std::sqrt(32.0 / 7.0)) < 1e-9);
}

QTEST_MAIN(TestComparisonPlotStatsTab)
#include "test_comparisonplot_statstab.moc"
