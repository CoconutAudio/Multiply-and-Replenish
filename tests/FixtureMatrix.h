#pragma once

#include <juce_core/juce_core.h>

#include <cstring>
#include <vector>

namespace tuner::test
{
/** @brief A row-major float32 matrix, read from the fixtures the reference implementation wrote.

    The header is eight magic bytes, then version, element type, rows and columns as little-endian
    32-bit integers, padded to 32 bytes before the data.
*/
struct FixtureMatrix
{
    int numRows { 0 };
    int numColumns { 0 };
    std::vector<float> values;
    juce::String error;

    [[nodiscard]] bool isValid() const noexcept { return ! values.empty(); }

    [[nodiscard]] int getNumRows() const noexcept { return numRows; }
    [[nodiscard]] int getNumColumns() const noexcept { return numColumns; }

    [[nodiscard]] const float* getData() const noexcept { return values.data(); }

    /** @brief Says what went wrong, for a failing assertion to print. */
    [[nodiscard]] juce::String getError() const { return isValid() ? juce::String {} : error; }

    [[nodiscard]] const float* getRow (int rowIndex) const noexcept
    {
        return values.data() + static_cast<std::size_t> (rowIndex) * static_cast<std::size_t> (numColumns);
    }

    static FixtureMatrix load (const juce::File& file)
    {
        FixtureMatrix matrix;

        juce::MemoryBlock block;

        if (! file.loadFileAsData (block) || block.getSize() < 32)
        {
            matrix.error = "cannot read " + file.getFullPathName();
            return matrix;
        }

        const auto* bytes = static_cast<const std::uint8_t*> (block.getData());

        const auto readInt = [bytes] (std::size_t offset)
        {
            return static_cast<int> (static_cast<std::uint32_t> (bytes[offset])
                                     | (static_cast<std::uint32_t> (bytes[offset + 1]) << 8)
                                     | (static_cast<std::uint32_t> (bytes[offset + 2]) << 16)
                                     | (static_cast<std::uint32_t> (bytes[offset + 3]) << 24));
        };

        matrix.numRows = readInt (16);
        matrix.numColumns = readInt (20);

        const auto numValues = static_cast<std::size_t> (matrix.numRows)
                             * static_cast<std::size_t> (matrix.numColumns);

        if (block.getSize() < 32 + numValues * sizeof (float))
        {
            FixtureMatrix truncated;
            truncated.error = file.getFileName() + " is truncated";
            return truncated;
        }

        matrix.values.resize (numValues);
        std::memcpy (matrix.values.data(), bytes + 32, numValues * sizeof (float));

        return matrix;
    }
};
}
