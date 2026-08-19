#include "dsp/Smoothing.h"

#include <gtest/gtest.h>

using namespace rvctuner;

TEST (Smoothing, MedianRemovesAnIsolatedSpike)
{
    const std::vector<float> values { 1.0f, 1.0f, 9.0f, 1.0f, 1.0f };

    const auto filtered = medianFilter (values, 1);

    EXPECT_FLOAT_EQ (filtered[2], 1.0f);
}

TEST (Smoothing, MedianOfNothingIsNothing)
{
    EXPECT_TRUE (medianFilter ({}, 3).empty());
}

TEST (Smoothing, GaussianKeepsAConstant)
{
    const std::vector<float> values (32, 0.75f);

    for (const auto value : gaussianFilter (values, 4.0))
        EXPECT_NEAR (value, 0.75f, 1.0e-5f);
}

TEST (Smoothing, GaussianPullsAStepTowardsItsNeighbours)
{
    std::vector<float> values (32, 0.0f);

    for (std::size_t index = 16; index < values.size(); ++index)
        values[index] = 1.0f;

    const auto smoothed = gaussianFilter (values, 3.0);

    EXPECT_GT (smoothed[15], 0.0f);
    EXPECT_LT (smoothed[16], 1.0f);
    EXPECT_NEAR (smoothed[15] + smoothed[16], 1.0f, 1.0e-4f);
}

TEST (Smoothing, WeightedMedianIgnoresZeroWeights)
{
    const std::vector<float> values { 60.0f, 61.0f, 90.0f };
    const std::vector<float> weights { 1.0f, 1.0f, 0.0f };

    EXPECT_LE (weightedMedian (values, weights), 61.0f);
}

TEST (Smoothing, WeightedMedianOfNothingIsZero)
{
    EXPECT_FLOAT_EQ (weightedMedian ({ 1.0f }, { 0.0f }), 0.0f);
}
