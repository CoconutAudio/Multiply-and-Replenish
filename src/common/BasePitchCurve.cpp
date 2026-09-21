#include "common/BasePitchCurve.h"

#include <algorithm>
#include <cmath>

namespace multiplyandreplenish
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
}

const std::vector<double>& BasePitchCurve::getKernel()
{
    static const auto kernel = []
    {
        std::vector<double> taps (static_cast<std::size_t> (kernelSize));
        auto sum = 0.0;

        for (int tap = 0; tap < kernelSize; ++tap)
        {
            const auto seconds = 0.001 * static_cast<double> (tap - kernelSize / 2);
            taps[static_cast<std::size_t> (tap)] = std::cos (pi * seconds / smoothWindowSeconds);
            sum += taps[static_cast<std::size_t> (tap)];
        }

        for (auto& tap : taps)
            tap /= sum;

        return taps;
    }();

    return kernel;
}

std::vector<float> BasePitchCurve::generate (const std::vector<NoteSegment>& notes,
                                             int numFrames,
                                             double frameRate)
{
    if (notes.empty() || numFrames <= 0 || frameRate <= 0.0)
        return {};

    auto sorted = notes;
    std::sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b)
    {
        if (a.firstFrame != b.firstFrame)
            return a.firstFrame < b.firstFrame;

        return a.lastFrame < b.lastFrame;
    });

    const auto millisecondsPerFrame = 1000.0 / frameRate;

    auto lastFrame = 0;

    for (const auto& note : sorted)
        lastFrame = std::max (lastFrame, note.lastFrame);

    const auto endSeconds = static_cast<double> (lastFrame) * millisecondsPerFrame / 1000.0;
    const auto numMilliseconds = static_cast<int> (std::round (1000.0 * (endSeconds + smoothWindowSeconds))) + 1;

    if (numMilliseconds <= 0)
        return {};

    std::vector<double> staircase (static_cast<std::size_t> (numMilliseconds), 0.0);

    auto noteIndex = 0;

    for (int millisecond = 0; millisecond < numMilliseconds; ++millisecond)
    {
        staircase[static_cast<std::size_t> (millisecond)] =
            static_cast<double> (sorted[static_cast<std::size_t> (noteIndex)].semitones);

        if (noteIndex < static_cast<int> (sorted.size()) - 1)
        {
            const auto& current = sorted[static_cast<std::size_t> (noteIndex)];
            const auto& next = sorted[static_cast<std::size_t> (noteIndex + 1)];

            const auto midpoint = 0.5 * (static_cast<double> (current.lastFrame)
                                         + static_cast<double> (next.firstFrame))
                                * millisecondsPerFrame / 1000.0;

            if (0.001 * static_cast<double> (millisecond) > midpoint)
                ++noteIndex;
        }
    }

    const auto& kernel = getKernel();

    std::vector<double> smoothed (static_cast<std::size_t> (numMilliseconds), 0.0);

    for (int millisecond = 0; millisecond < numMilliseconds; ++millisecond)
    {
        auto sum = 0.0;

        for (int tap = 0; tap < kernelSize; ++tap)
        {
            const auto source = std::clamp (millisecond - kernelSize / 2 + tap, 0, numMilliseconds - 1);
            sum += staircase[static_cast<std::size_t> (source)] * kernel[static_cast<std::size_t> (tap)];
        }

        smoothed[static_cast<std::size_t> (millisecond)] = sum;
    }

    std::vector<float> result (static_cast<std::size_t> (numFrames), 0.0f);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto millisecond = static_cast<double> (frameIndex) * millisecondsPerFrame;
        const auto whole = static_cast<int> (millisecond);
        const auto fraction = millisecond - static_cast<double> (whole);

        if (whole + 1 < numMilliseconds)
            result[static_cast<std::size_t> (frameIndex)] =
                static_cast<float> (smoothed[static_cast<std::size_t> (whole)] * (1.0 - fraction)
                                    + smoothed[static_cast<std::size_t> (whole + 1)] * fraction);
        else if (whole < numMilliseconds)
            result[static_cast<std::size_t> (frameIndex)] = static_cast<float> (smoothed[static_cast<std::size_t> (whole)]);
        else
            result[static_cast<std::size_t> (frameIndex)] = static_cast<float> (smoothed.back());
    }

    return result;
}
}
