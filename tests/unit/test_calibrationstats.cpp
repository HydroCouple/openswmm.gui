/**
 * @file test_calibrationstats.cpp
 * @brief Unit tests for CalibrationManager statistics — NSE, RMSE, percent bias, correlation.
 *        All tests use hand-calculable values so failures pinpoint exact formula bugs.
 */

#include <gtest/gtest.h>
#include <cmath>
// #include "calibration/calibrationmanager.h"   // uncomment when implemented

// Stand-in free functions until CalibrationManager is implemented.
// Replace these with CalibrationManager::nse(), ::rmse(), etc.
static double nse(const std::vector<double>& obs, const std::vector<double>& sim)
{
    double mean = 0;
    for (double v : obs) mean += v;
    mean /= obs.size();

    double num = 0, den = 0;
    for (size_t i = 0; i < obs.size(); ++i) {
        num += (obs[i] - sim[i]) * (obs[i] - sim[i]);
        den += (obs[i] - mean)   * (obs[i] - mean);
    }
    return 1.0 - num / den;
}

static double rmse(const std::vector<double>& obs, const std::vector<double>& sim)
{
    double sum = 0;
    for (size_t i = 0; i < obs.size(); ++i)
        sum += (obs[i] - sim[i]) * (obs[i] - sim[i]);
    return std::sqrt(sum / obs.size());
}

static double pbias(const std::vector<double>& obs, const std::vector<double>& sim)
{
    double sumObs = 0, sumErr = 0;
    for (size_t i = 0; i < obs.size(); ++i) {
        sumErr += (obs[i] - sim[i]);
        sumObs += obs[i];
    }
    return 100.0 * sumErr / sumObs;
}

// ---- Nash-Sutcliffe Efficiency -------------------------------------------

TEST(CalibrationStats, NSE_PerfectFit)
{
    std::vector<double> obs = {1, 2, 3, 4, 5};
    EXPECT_DOUBLE_EQ(nse(obs, obs), 1.0);
}

TEST(CalibrationStats, NSE_MeanPrediction)
{
    // Predicting the mean every timestep → NSE = 0
    std::vector<double> obs = {1, 2, 3, 4, 5};
    std::vector<double> sim = {3, 3, 3, 3, 3};
    EXPECT_NEAR(nse(obs, sim), 0.0, 1e-10);
}

// ---- RMSE ----------------------------------------------------------------

TEST(CalibrationStats, RMSE_PerfectFit)
{
    std::vector<double> obs = {1, 2, 3};
    EXPECT_DOUBLE_EQ(rmse(obs, obs), 0.0);
}

TEST(CalibrationStats, RMSE_KnownValue)
{
    // errors = [1, 1, 1] → RMSE = 1
    std::vector<double> obs = {2, 3, 4};
    std::vector<double> sim = {1, 2, 3};
    EXPECT_DOUBLE_EQ(rmse(obs, sim), 1.0);
}

// ---- Percent Bias --------------------------------------------------------

TEST(CalibrationStats, PBias_PerfectFit)
{
    std::vector<double> obs = {1, 2, 3};
    EXPECT_DOUBLE_EQ(pbias(obs, obs), 0.0);
}

TEST(CalibrationStats, PBias_OverEstimation)
{
    // sim always 10% higher → pbias should be negative (model overestimates)
    std::vector<double> obs = {10, 20, 30};
    std::vector<double> sim = {11, 22, 33};
    EXPECT_NEAR(pbias(obs, sim), -10.0, 1e-9);
}
