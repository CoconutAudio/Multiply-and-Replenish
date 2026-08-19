#include "dsp/MelFilterBank.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace rvctuner;

namespace
{
constexpr int fftSize = 1024;
constexpr int numMelBins = 128;
constexpr int numBins = fftSize / 2 + 1;
}

TEST (MelFilterBank, HasOneRowPerBand)
{
    const auto bank = makeMelFilterBank (MelScale::slaney, 16000.0, fftSize, numMelBins, 0.0, 8000.0);

    EXPECT_EQ (bank.size(), static_cast<std::size_t> (numMelBins * numBins));
}

TEST (MelFilterBank, WeightsAreNonNegativeAndBounded)
{
    const auto bank = makeMelFilterBank (MelScale::htk, 16000.0, fftSize, numMelBins, 30.0, 8000.0);

    for (const auto weight : bank)
    {
        EXPECT_GE (weight, 0.0f);
        EXPECT_LT (weight, 1.0f);
    }
}

TEST (MelFilterBank, BandsClimbInFrequency)
{
    const auto bank = makeMelFilterBank (MelScale::slaney, 44100.0, 2048, numMelBins, 40.0, 16000.0);
    const auto rowLength = static_cast<std::size_t> (2048 / 2 + 1);

    auto previousPeak = -1;

    for (int melIndex = 0; melIndex < numMelBins; ++melIndex)
    {
        const auto* row = bank.data() + static_cast<std::size_t> (melIndex) * rowLength;

        auto peak = 0;

        for (std::size_t binIndex = 1; binIndex < rowLength; ++binIndex)
            if (row[binIndex] > row[static_cast<std::size_t> (peak)])
                peak = static_cast<int> (binIndex);

        EXPECT_GT (peak, previousPeak);
        previousPeak = peak;
    }
}

TEST (MelFilterBank, NormalisationScalesByBandWidth)
{
    const auto normalised = makeMelFilterBank (MelScale::slaney, 16000.0, fftSize, numMelBins, 0.0, 8000.0, true);
    const auto raw = makeMelFilterBank (MelScale::slaney, 16000.0, fftSize, numMelBins, 0.0, 8000.0, false);

    const auto peakOfBand = [] (const std::vector<float>& bank, int melIndex)
    {
        const auto* row = bank.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numBins);
        return *std::max_element (row, row + numBins);
    };

    EXPECT_LE (peakOfBand (raw, 0), 1.0f);
    EXPECT_GT (peakOfBand (raw, 0), 0.0f);

    EXPECT_LT (peakOfBand (normalised, 0), peakOfBand (raw, 0));
    EXPECT_LT (peakOfBand (normalised, numMelBins - 1), peakOfBand (raw, numMelBins - 1));

    // Normalising divides each band by its width, and the bands widen with frequency.
    EXPECT_GT (peakOfBand (normalised, 0), peakOfBand (normalised, numMelBins - 1));
}
