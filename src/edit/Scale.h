#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace tuner
{
/** @brief The set of pitches a correction is allowed to land on. */
class Scale
{
public:
    /** @brief The scales the editor offers, in the order they appear in the menu. */
    enum class Type
    {
        chromatic,
        major,
        naturalMinor,
        harmonicMinor,
        majorPentatonic,
        minorPentatonic,
        blues,
        dorian,
        mixolydian,
        custom
    };

    Scale() = default;

    Scale (Type scaleType, int tonicPitchClass) noexcept;

    /** @brief Returns the names of every scale, indexed by Type. */
    [[nodiscard]] static juce::StringArray getTypeNames();

    /** @brief Returns the note names, indexed by pitch class, with sharps. */
    [[nodiscard]] static juce::StringArray getPitchClassNames();

    /** @brief Names a MIDI note the way the keyboard labels it, as in "C4". */
    [[nodiscard]] static juce::String getNoteName (int midiNote);

    [[nodiscard]] Type getType() const noexcept { return type; }
    [[nodiscard]] int getTonic() const noexcept { return tonic; }

    /** @brief Whether a pitch class is in the scale. */
    [[nodiscard]] bool contains (int midiNote) const noexcept;

    /** @brief Adds or removes a pitch class, which makes the scale custom. */
    void setPitchClassAllowed (int pitchClass, bool shouldBeAllowed) noexcept;

    /** @brief Returns the nearest allowed note to a pitch in semitones. */
    [[nodiscard]] int snap (double midiPitch) const noexcept;

    [[nodiscard]] bool operator== (const Scale& other) const noexcept
    {
        return type == other.type && tonic == other.tonic && allowed == other.allowed;
    }

private:
    void rebuild() noexcept;

    Type type { Type::chromatic };
    int tonic { 0 };
    std::array<bool, 12> allowed { { true, true, true, true, true, true, true, true, true, true, true, true } };
};
}
