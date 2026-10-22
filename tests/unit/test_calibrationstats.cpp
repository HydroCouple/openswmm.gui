/**
 * @file      test_calibrationstats.cpp
 * @brief     Tests of local calibration-statistic stand-ins; CalibrationManager calls are disabled.
 *        All tests use hand-calculable values so failures pinpoint exact formula bugs.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>
#include <cmath>
// #include "calibration/calibrationmanager.h"   // uncomment when implemented

// Stand-in free functions until CalibrationManager is implemented.
// Replace these with CalibrationManager::nse(), ::rmse(), etc.
/**
 *
 * @brief Evaluate the test-local Nash-Sutcliffe stand-in, without calling CalibrationManager.
 * @param obs Nonempty finite observed values with strictly positive centered variance.
 * @param sim At least obs.size() finite simulated values in the same units/order;
 * extra entries ignored, aliasing obs permitted. Inputs remain unchanged.
 * @return 1-SSE/observed-centered-SSE, dimensionless. No size/variance/finite checks;
 * invalid fixtures can divide by zero or read out of bounds.
 *
 */
static double nse(const std::vector<double> &obs, const std::vector<double> &sim)
{
    double mean = 0;
    for (double v : obs)
        mean += v;
    mean /= obs.size();

    double num = 0, den = 0;
    for (size_t i = 0; i < obs.size(); ++i) {
        num += (obs[i] - sim[i]) * (obs[i] - sim[i]);
        den += (obs[i] - mean) * (obs[i] - mean);
    }
    return 1.0 - num / den;
}

/**
 *
 * @brief Evaluate the test-local root mean squared error stand-in.
 * @param obs Nonempty finite observed samples.
 * @param sim At least obs.size() finite corresponding samples in the same units;
 * extra entries ignored, aliasing obs permitted.
 * @return sqrt(sum squared errors/count), in sample units. No bounds, empty-input,
 * overflow or finite checks; neither input is changed.
 *
 */
static double rmse(const std::vector<double> &obs, const std::vector<double> &sim)
{
    double sum = 0;
    for (size_t i = 0; i < obs.size(); ++i)
        sum += (obs[i] - sim[i]) * (obs[i] - sim[i]);
    return std::sqrt(sum / obs.size());
}

/**
 *
 * @brief Evaluate the test-local percent-bias stand-in with observed-minus-simulated sign.
 * @param obs Finite nonempty observed samples with nonzero sum.
 * @param sim At least obs.size() finite corresponding values in identical units;
 * extra ignored, aliasing permitted.
 * @return 100*sum(obs-sim)/sum(obs), percent; overprediction is negative. Does not
 * validate dimensions/denominator/overflow or call production CalibrationManager.
 *
 */
static double pbias(const std::vector<double> &obs, const std::vector<double> &sim)
{
    double sumObs = 0, sumErr = 0;
    for (size_t i = 0; i < obs.size(); ++i) {
        sumErr += (obs[i] - sim[i]);
        sumObs += obs[i];
    }
    return 100.0 * sumErr / sumObs;
}

// ---- Nash-Sutcliffe Efficiency -------------------------------------------

/**
 *
 * @brief Require unit Nash-Sutcliffe score for identical nonconstant test vectors.
 *
 */
TEST(CalibrationStats, NSE_PerfectFit)
{
    std::vector<double> obs = {1, 2, 3, 4, 5};
    EXPECT_DOUBLE_EQ(nse(obs, obs), 1.0);
}

/**
 *
 * @brief Require zero Nash-Sutcliffe score when every prediction equals observed mean.
 *
 */
TEST(CalibrationStats, NSE_MeanPrediction)
{
    // Predicting the mean every timestep → NSE = 0
    std::vector<double> obs = {1, 2, 3, 4, 5};
    std::vector<double> sim = {3, 3, 3, 3, 3};
    EXPECT_NEAR(nse(obs, sim), 0.0, 1e-10);
}

// ---- RMSE ----------------------------------------------------------------

/**
 *
 * @brief Require zero local RMSE for identical test vectors.
 *
 */
TEST(CalibrationStats, RMSE_PerfectFit)
{
    std::vector<double> obs = {1, 2, 3};
    EXPECT_DOUBLE_EQ(rmse(obs, obs), 0.0);
}

/**
 *
 * @brief Require local RMSE one for three errors of exactly one sample unit.
 *
 */
TEST(CalibrationStats, RMSE_KnownValue)
{
    // errors = [1, 1, 1] → RMSE = 1
    std::vector<double> obs = {2, 3, 4};
    std::vector<double> sim = {1, 2, 3};
    EXPECT_DOUBLE_EQ(rmse(obs, sim), 1.0);
}

// ---- Percent Bias --------------------------------------------------------

/**
 *
 * @brief Require zero local percent bias for identical test vectors.
 *
 */
TEST(CalibrationStats, PBias_PerfectFit)
{
    std::vector<double> obs = {1, 2, 3};
    EXPECT_DOUBLE_EQ(pbias(obs, obs), 0.0);
}

/**
 *
 * @brief Require negative ten-percent local bias for uniform ten-percent overprediction.
 *
 */
TEST(CalibrationStats, PBias_OverEstimation)
{
    // sim always 10% higher → pbias should be negative (model overestimates)
    std::vector<double> obs = {10, 20, 30};
    std::vector<double> sim = {11, 22, 33};
    EXPECT_NEAR(pbias(obs, sim), -10.0, 1e-9);
}
