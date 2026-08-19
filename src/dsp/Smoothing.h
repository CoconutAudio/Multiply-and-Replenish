#pragma once

#include <vector>

namespace rvctuner
{
/** @brief Replaces each entry with the median of the window centred on it.
    @param values      Sequence to filter.
    @param halfLength  Entries either side of the centre; zero returns @p values unchanged.
    @return The filtered sequence, the same length as @p values.
*/
[[nodiscard]] std::vector<float> medianFilter (const std::vector<float>& values, int halfLength);

/** @brief Convolves with a Gaussian, extending the sequence by its end values.
    @param values     Sequence to smooth.
    @param deviation  Standard deviation, in entries; zero or less returns @p values unchanged.
    @return The smoothed sequence, the same length as @p values.
*/
[[nodiscard]] std::vector<float> gaussianFilter (const std::vector<float>& values, double deviation);

/** @brief The weighted median of @p values, which ignores entries with a weight of zero.
    @param values   Sequence to summarise.
    @param weights  One weight per entry.
    @return The weighted median, or zero when every weight is zero.
*/
[[nodiscard]] float weightedMedian (const std::vector<float>& values, const std::vector<float>& weights);
}
