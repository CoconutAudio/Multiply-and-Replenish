#include "edit/Scale.h"

#include <algorithm>
#include <cmath>

namespace tuner
{
namespace
{
    constexpr std::array<const char*, 10> scaleNames {
        "Chromatic", "Major", "Minor", "Harmonic minor", "Major pentatonic",
        "Minor pentatonic", "Blues", "Dorian", "Mixolydian", "Custom"
    };

    /** @brief Intervals above the tonic, one entry per scale in Type's order. */
    const std::vector<int>& getIntervals (Scale::Type type)
    {
        static const std::vector<std::vector<int>> intervals {
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
            { 0, 2, 4, 5, 7, 9, 11 },
            { 0, 2, 3, 5, 7, 8, 10 },
            { 0, 2, 3, 5, 7, 8, 11 },
            { 0, 2, 4, 7, 9 },
            { 0, 3, 5, 7, 10 },
            { 0, 3, 5, 6, 7, 10 },
            { 0, 2, 3, 5, 7, 9, 10 },
            { 0, 2, 4, 5, 7, 9, 10 },
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }
        };

        const auto index = static_cast<std::size_t> (type);
        return intervals[index < intervals.size() ? index : 0];
    }
}

Scale::Scale (Type scaleType, int tonicPitchClass) noexcept
    : type (scaleType),
      tonic (((tonicPitchClass % 12) + 12) % 12)
{
    rebuild();
}

void Scale::rebuild() noexcept
{
    allowed.fill (false);

    for (const auto interval : getIntervals (type))
        allowed[static_cast<std::size_t> ((tonic + interval) % 12)] = true;
}

juce::StringArray Scale::getTypeNames()
{
    juce::StringArray names;

    for (const auto* name : scaleNames)
        names.add (name);

    return names;
}

juce::StringArray Scale::getPitchClassNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::String Scale::getNoteName (int midiNote)
{
    const auto pitchClass = ((midiNote % 12) + 12) % 12;
    const auto octave = midiNote / 12 - 1;

    return getPitchClassNames()[pitchClass] + juce::String (octave);
}

bool Scale::contains (int midiNote) const noexcept
{
    return allowed[static_cast<std::size_t> (((midiNote % 12) + 12) % 12)];
}

void Scale::setPitchClassAllowed (int pitchClass, bool shouldBeAllowed) noexcept
{
    const auto index = static_cast<std::size_t> (((pitchClass % 12) + 12) % 12);

    if (allowed[index] == shouldBeAllowed)
        return;

    allowed[index] = shouldBeAllowed;
    type = Type::custom;

    if (std::none_of (allowed.begin(), allowed.end(), [] (bool isAllowed) { return isAllowed; }))
        allowed[index] = true;
}

int Scale::snap (double midiPitch) const noexcept
{
    const auto nearest = static_cast<int> (std::llround (midiPitch));

    if (contains (nearest))
        return nearest;

    for (int distance = 1; distance <= 6; ++distance)
    {
        const auto below = nearest - distance;
        const auto above = nearest + distance;

        const auto preferBelow = midiPitch - static_cast<double> (below) <= static_cast<double> (above) - midiPitch;

        if (preferBelow && contains (below))
            return below;

        if (contains (above))
            return above;

        if (contains (below))
            return below;
    }

    return nearest;
}
}
