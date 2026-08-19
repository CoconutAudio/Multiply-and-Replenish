#pragma once

#include <algorithm>

namespace tuner
{
/** @brief One sung note: the frames it covers, the pitch it was sung at, and where it should land.

    Everything here is per note and editable. The pitch is kept in semitones on the MIDI scale, so
    that 69.0 is A4 and a difference of one is a semitone whatever the octave.
*/
struct Note
{
    int firstFrame { 0 };
    int lastFrame { 0 };

    /** @brief The centre of the note as sung, in semitones. */
    double sungPitch { 60.0 };

    /** @brief The note the correction pulls towards. */
    int targetNote { 60 };

    /** @brief How far towards @ref targetNote the note is pulled: 0 as sung, 1 fully in tune. */
    float correction { 1.0f };

    /** @brief Scales the 4 to 9 Hz modulation within the note; 1 keeps the vibrato as sung. */
    float vibrato { 1.0f };

    /** @brief Scales the slow wander within the note; 1 keeps the drift as sung. */
    float drift { 1.0f };

    /** @brief Gain applied to the note when it is rendered, in decibels. */
    float gainDecibels { 0.0f };

    /** @brief False leaves the note exactly as sung, whatever the other settings say. */
    bool isEnabled { true };

    [[nodiscard]] int getNumFrames() const noexcept { return std::max (0, lastFrame - firstFrame); }

    [[nodiscard]] bool contains (int frameIndex) const noexcept
    {
        return frameIndex >= firstFrame && frameIndex < lastFrame;
    }

    /** @brief How far the note is from its target, in semitones, as sung. */
    [[nodiscard]] double getError() const noexcept
    {
        return sungPitch - static_cast<double> (targetNote);
    }
};
}
