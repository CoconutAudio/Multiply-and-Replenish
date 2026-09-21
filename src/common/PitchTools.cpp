#include "common/PitchTools.h"

#include <algorithm>
#include <cmath>

namespace multiplyandreplenish
{
std::vector<float> PitchTools::tilt (const std::vector<float>& deviation, float pivot, float amount)
{
    if (deviation.size() < 2)
        return deviation;

    const auto clampedPivot = std::clamp (pivot, 0.0f, 1.0f);
    const auto furthest = std::max (clampedPivot, 1.0f - clampedPivot);

    if (furthest <= 0.0f)
        return deviation;

    auto result = deviation;

    const auto lastIndex = static_cast<float> (deviation.size() - 1);

    for (std::size_t index = 0; index < deviation.size(); ++index)
    {
        const auto position = static_cast<float> (index) / lastIndex;

        result[index] = deviation[index] + (position - clampedPivot) / furthest * amount;
    }

    return result;
}

std::vector<float> PitchTools::scale (const std::vector<float>& deviation, float factor)
{
    auto result = deviation;

    for (auto& value : result)
        value *= factor;

    return result;
}

std::vector<float> PitchTools::smoothBoundary (const std::vector<float>& deviation,
                                               bool isRightBoundary,
                                               int transitionFrames,
                                               float target)
{
    auto result = deviation;

    if (deviation.empty() || transitionFrames <= 0)
        return result;

    const auto numFrames = std::max (1, std::min (transitionFrames, static_cast<int> (deviation.size())));

    const auto deviationFrames = static_cast<float> (numFrames) / 2.0f;
    const auto scaling = 1.0f / (2.0f * deviationFrames * deviationFrames);

    const auto firstIndex = isRightBoundary ? deviation.size() - static_cast<std::size_t> (numFrames)
                                            : std::size_t { 0 };

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto distance = static_cast<float> (isRightBoundary ? numFrames - 1 - frameIndex : frameIndex);
        const auto weight = std::exp (-distance * distance * scaling);
        const auto index = firstIndex + static_cast<std::size_t> (frameIndex);

        result[index] = target * weight + deviation[index] * (1.0f - weight);
    }

    return result;
}
}
