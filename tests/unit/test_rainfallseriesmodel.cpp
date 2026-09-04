/*!
 * \file   test_rainfallseriesmodel.cpp
 * \brief  Rainfall Visualization data model — step-series shape, basis
 *         conversions, stats/gap math, and engine-backed gage assembly.
 */
#include <gtest/gtest.h>

#include "core/swmmdatetime.h"
#include "plot/rainfallseriesmodel.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_tables.h>

using openswmmvis::plot::RainfallSeriesModel;
using openswmmvis::plot::RainGageRainfall;
using openswmmvis::plot::RainGageStats;

namespace {

// A 1-hour-interval gage with a gap: entries at 00:00, 01:00, then 04:00.
RainGageRainfall gappyGage()
{
    RainGageRainfall g;
    g.id = QStringLiteral("G1");
    g.intervalSec = 3600.0;
    const double day0 = openswmmvis::core::qDateTimeToSwmmDateTime(
        QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC));
    g.timesOA   = {day0, day0 + 1.0 / 24.0, day0 + 4.0 / 24.0};
    g.intensity = {2.0, 1.0, 0.5};
    return g;
}

} // namespace

TEST(RainfallSeriesModel, StatsTotalsPeaksAndGaps)
{
    const RainGageRainfall g = gappyGage();
    const RainGageStats s = RainfallSeriesModel::computeStats(g);

    EXPECT_EQ(s.sampleCount, 3);
    // Depth: 2.0*1h + 1.0*1h + 0.5*1h = 3.5 units.
    EXPECT_NEAR(s.totalDepth, 3.5, 1e-9);
    EXPECT_NEAR(s.peakIntensity, 2.0, 1e-12);
    EXPECT_EQ(s.peakTime, QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC));
    EXPECT_EQ(s.first, QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC));
    EXPECT_EQ(s.last,  QDateTime(QDate(2026, 1, 1), QTime(4, 0), Qt::UTC));
    // One gap: 01:00 → 04:00 spacing (3h) > 1.5 × interval; the gap net of
    // the entry's own interval is 2h.
    EXPECT_EQ(s.gapCount, 1);
    EXPECT_EQ(s.longestGapSecs, qint64(2 * 3600));
}

TEST(RainfallSeriesModel, StepSeriesHasExplicitZeroAcrossTheGap)
{
    const RainGageRainfall g = gappyGage();
    const auto pts = RainfallSeriesModel::buildStepSeries(
        g, RainfallSeriesModel::Basis::Intensity);

    // Entry 0: (00:00,2)(01:00,2); entry 1: (01:00,1)(02:00,1) then zeros
    // (02:00,0)(04:00,0); entry 2: (04:00,.5)(05:00,.5); final drop to 0.
    ASSERT_EQ(pts.size(), 9);
    const qint64 t0 =
        QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC).toMSecsSinceEpoch();
    auto ms = [t0](double hours) {
        return double(t0) + hours * 3600.0 * 1000.0;
    };
    EXPECT_DOUBLE_EQ(pts[0].x(), ms(0)); EXPECT_DOUBLE_EQ(pts[0].y(), 2.0);
    EXPECT_DOUBLE_EQ(pts[1].x(), ms(1)); EXPECT_DOUBLE_EQ(pts[1].y(), 2.0);
    EXPECT_DOUBLE_EQ(pts[2].x(), ms(1)); EXPECT_DOUBLE_EQ(pts[2].y(), 1.0);
    EXPECT_DOUBLE_EQ(pts[3].x(), ms(2)); EXPECT_DOUBLE_EQ(pts[3].y(), 1.0);
    EXPECT_DOUBLE_EQ(pts[4].x(), ms(2)); EXPECT_DOUBLE_EQ(pts[4].y(), 0.0);
    EXPECT_DOUBLE_EQ(pts[5].x(), ms(4)); EXPECT_DOUBLE_EQ(pts[5].y(), 0.0);
    EXPECT_DOUBLE_EQ(pts[6].x(), ms(4)); EXPECT_DOUBLE_EQ(pts[6].y(), 0.5);
    EXPECT_DOUBLE_EQ(pts[7].x(), ms(5)); EXPECT_DOUBLE_EQ(pts[7].y(), 0.5);
    EXPECT_DOUBLE_EQ(pts[8].y(), 0.0);
}

TEST(RainfallSeriesModel, BasisConversions)
{
    RainGageRainfall g = gappyGage();
    g.intervalSec = 1800.0;   // 30-min interval, entries 1h/3h apart → no cap

    // Depth per interval: v * dur/3600 with dur = min(interval, spacing).
    const auto depth = RainfallSeriesModel::buildStepSeries(
        g, RainfallSeriesModel::Basis::DepthPerInterval);
    EXPECT_DOUBLE_EQ(depth[0].y(), 2.0 * 0.5);

    // Cumulative: running total of depths, monotone non-decreasing.
    const auto cum = RainfallSeriesModel::buildCumulativeSeries(g);
    double prev = -1.0;
    for (const auto &pt : cum) {
        EXPECT_GE(pt.y(), prev);
        prev = pt.y();
    }
    EXPECT_NEAR(cum.last().y(), (2.0 + 1.0 + 0.5) * 0.5, 1e-9);
}

TEST(RainfallSeriesModel, EntryDurationCapsAtNextEntry)
{
    RainGageRainfall g = gappyGage();
    g.intervalSec = 2.0 * 3600.0;   // interval longer than the 1-h spacing
    // 1e-4 s tolerance: OADate fractions are not exact binary at ~46000-day
    // magnitudes, so a 1-hour spacing reconstructs to 3600 ± sub-millisecond.
    EXPECT_NEAR(RainfallSeriesModel::entryDurationSecs(g, 0), 3600.0, 1e-4);
    // Last entry has no successor → full interval.
    EXPECT_NEAR(RainfallSeriesModel::entryDurationSecs(g, 2), 7200.0, 1e-9);
}

TEST(RainfallSeriesModel, AssemblesTimeseriesGageFromEngine)
{
    SWMM_Engine eng = swmm_engine_new();   // BUILDING state
    ASSERT_NE(eng, nullptr);

    ASSERT_EQ(swmm_timeseries_add(eng, "RAIN_TS"), SWMM_OK);
    const int ts = swmm_table_index(eng, "RAIN_TS");
    ASSERT_GE(ts, 0);
    const double day0 = openswmmvis::core::qDateTimeToSwmmDateTime(
        QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC));
    ASSERT_EQ(swmm_table_add_point(eng, ts, day0, 1.0), SWMM_OK);
    ASSERT_EQ(swmm_table_add_point(eng, ts, day0 + 1.0 / 24.0, 2.0), SWMM_OK);

    ASSERT_EQ(swmm_gage_add(eng, "G1"), SWMM_OK);
    const int rg = swmm_gage_index(eng, "G1");
    ASSERT_GE(rg, 0);
    ASSERT_EQ(swmm_gage_set_data_source(eng, rg, 0 /*TIMESERIES*/), SWMM_OK);
    ASSERT_EQ(swmm_gage_set_timeseries(eng, rg, "RAIN_TS"), SWMM_OK);
    ASSERT_EQ(swmm_gage_set_rain_interval(eng, rg, 3600.0), SWMM_OK);

    RainfallSeriesModel m;
    m.setEngine(eng);
    ASSERT_EQ(m.reload(/*reloadRainFiles=*/false), 1);
    const auto &gages = m.gages();
    ASSERT_EQ(gages.size(), 1);
    EXPECT_EQ(gages[0].id, QStringLiteral("G1"));
    EXPECT_EQ(gages[0].dataSource, 0);
    EXPECT_FALSE(gages[0].fileFailed);
    ASSERT_EQ(gages[0].timesOA.size(), 2);
    EXPECT_NEAR(gages[0].timesOA[0], day0, 1e-9);

    swmm_engine_destroy(eng);
}
