#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace multiplyandreplenish
{
/** @brief Where the networks are looked for, in the order they are looked for in. */
class ModelLibrary
{
public:
    ModelLibrary();

    /** @brief The first place a named file exists, or nothing. */
    [[nodiscard]] juce::File find (const juce::String& relativePath) const;

    [[nodiscard]] const std::vector<juce::File>& getSearchPaths() const noexcept { return searchPaths; }

    /** @brief Where a user is expected to install models of their own. */
    [[nodiscard]] static juce::File getUserModelDirectory();

private:
    std::vector<juce::File> searchPaths;
};
}
