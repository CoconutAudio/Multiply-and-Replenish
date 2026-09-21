#include "dsp/Smoothing.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace multiplyandreplenish
{
std::vector<float> medianFilter (const std::vector<float>& values, int halfLength)
{
    if (halfLength <= 0 || values.empty())
        return values;

    const auto numValues = static_cast<int> (values.size());
    std::vector<float> filtered (values.size());
    std::vector<float> window;
    window.reserve (static_cast<std::size_t> (2 * halfLength + 1));

    for (int index = 0; index < numValues; ++index)
    {
        const auto first = std::max (index - halfLength, 0);
        const auto last = std::min (index + halfLength + 1, numValues);

        window.assign (values.begin() + first, values.begin() + last);

        const auto middle = window.begin() + static_cast<std::ptrdiff_t> (window.size() / 2);
        std::nth_element (window.begin(), middle, window.end());

        filtered[static_cast<std::size_t> (index)] = *middle;
    }

    return filtered;
}

std::vector<float> gaussianFilter (const std::vector<float>& values, double deviation)
{
    if (deviation <= 0.0 || values.empty())
        return values;

    const auto radius = std::max (1, static_cast<int> (std::ceil (3.0 * deviation)));

    std::vector<double> kernel (static_cast<std::size_t> (2 * radius + 1));
    auto sum = 0.0;

    for (int offset = -radius; offset <= radius; ++offset)
    {
        const auto position = static_cast<double> (offset) / deviation;
        const auto weight = std::exp (-0.5 * position * position);

        kernel[static_cast<std::size_t> (offset + radius)] = weight;
        sum += weight;
    }

    for (auto& weight : kernel)
        weight /= sum;

    const auto numValues = static_cast<int> (values.size());
    std::vector<float> smoothed (values.size());

    for (int index = 0; index < numValues; ++index)
    {
        auto accumulated = 0.0;

        for (int offset = -radius; offset <= radius; ++offset)
        {
            const auto source = std::clamp (index + offset, 0, numValues - 1);
            accumulated += kernel[static_cast<std::size_t> (offset + radius)]
                         * static_cast<double> (values[static_cast<std::size_t> (source)]);
        }

        smoothed[static_cast<std::size_t> (index)] = static_cast<float> (accumulated);
    }

    return smoothed;
}

float weightedMedian (const std::vector<float>& values, const std::vector<float>& weights)
{
    std::vector<std::size_t> order;
    order.reserve (values.size());

    auto total = 0.0;

    for (std::size_t index = 0; index < values.size() && index < weights.size(); ++index)
    {
        if (weights[index] <= 0.0f)
            continue;

        order.push_back (index);
        total += static_cast<double> (weights[index]);
    }

    if (order.empty())
        return 0.0f;

    std::sort (order.begin(), order.end(), [&values] (std::size_t first, std::size_t second)
    {
        return values[first] < values[second];
    });

    auto accumulated = 0.0;

    for (const auto index : order)
    {
        accumulated += static_cast<double> (weights[index]);

        if (accumulated >= 0.5 * total)
            return values[index];
    }

    return values[order.back()];
}
}
