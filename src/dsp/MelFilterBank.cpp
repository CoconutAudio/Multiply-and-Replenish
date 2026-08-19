#include "dsp/MelFilterBank.h"

#include <algorithm>
#include <cmath>

namespace rvctuner
{
namespace
{
    constexpr double slaneyHzPerMel = 200.0 / 3.0;
    constexpr double slaneyBreakHz = 1000.0;
    constexpr double slaneyBreakMel = slaneyBreakHz / slaneyHzPerMel;

    double toMel (MelScale scale, double frequencyHz) noexcept
    {
        if (scale == MelScale::htk)
            return 1127.0 * std::log (1.0 + frequencyHz / 700.0);

        if (frequencyHz < slaneyBreakHz)
            return frequencyHz / slaneyHzPerMel;

        return slaneyBreakMel + std::log (frequencyHz / slaneyBreakHz) / (std::log (6.4) / 27.0);
    }

    double toHz (MelScale scale, double mel) noexcept
    {
        if (scale == MelScale::htk)
            return 700.0 * (std::exp (mel / 1127.0) - 1.0);

        if (mel < slaneyBreakMel)
            return mel * slaneyHzPerMel;

        return slaneyBreakHz * std::exp ((std::log (6.4) / 27.0) * (mel - slaneyBreakMel));
    }
}

std::vector<float> makeMelFilterBank (MelScale scale,
                                      double sampleRate,
                                      int fftSize,
                                      int numMelBins,
                                      double minimumHz,
                                      double maximumHz,
                                      bool isNormalised)
{
    const auto numBins = fftSize / 2 + 1;

    std::vector<float> bank (static_cast<std::size_t> (numMelBins) * static_cast<std::size_t> (numBins), 0.0f);

    if (numMelBins <= 0 || numBins <= 0 || maximumHz <= minimumHz)
        return bank;

    std::vector<double> edgeHz (static_cast<std::size_t> (numMelBins + 2));
    {
        const auto lowestMel = toMel (scale, minimumHz);
        const auto highestMel = toMel (scale, maximumHz);

        for (int edgeIndex = 0; edgeIndex < numMelBins + 2; ++edgeIndex)
        {
            const auto position = static_cast<double> (edgeIndex) / static_cast<double> (numMelBins + 1);
            edgeHz[static_cast<std::size_t> (edgeIndex)] =
                toHz (scale, lowestMel + position * (highestMel - lowestMel));
        }
    }

    for (int melIndex = 0; melIndex < numMelBins; ++melIndex)
    {
        const auto lowerHz = edgeHz[static_cast<std::size_t> (melIndex)];
        const auto centreHz = edgeHz[static_cast<std::size_t> (melIndex + 1)];
        const auto upperHz = edgeHz[static_cast<std::size_t> (melIndex + 2)];

        const auto scaling = isNormalised && upperHz > lowerHz ? 2.0 / (upperHz - lowerHz) : 1.0;

        auto* row = bank.data() + static_cast<std::size_t> (melIndex) * static_cast<std::size_t> (numBins);

        for (int binIndex = 0; binIndex < numBins; ++binIndex)
        {
            const auto binHz = static_cast<double> (binIndex) * sampleRate / static_cast<double> (fftSize);

            const auto rising = centreHz > lowerHz ? (binHz - lowerHz) / (centreHz - lowerHz) : 0.0;
            const auto falling = upperHz > centreHz ? (upperHz - binHz) / (upperHz - centreHz) : 0.0;

            const auto weight = std::max (0.0, std::min (rising, falling));

            row[binIndex] = static_cast<float> (weight * scaling);
        }
    }

    return bank;
}
}
