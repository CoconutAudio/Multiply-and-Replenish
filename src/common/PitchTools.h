#pragma once

#include <vector>

namespace multiplyandreplenish
{
/** @brief What the notes on either side are doing where they meet this one.

    Smoothing a boundary needs somewhere to smooth towards. Left to itself a note would be faded
    towards its own centre, which pulls a step into the join rather than taking one out; fading
    towards what the neighbour is doing at the same instant is what makes the two run together.
*/
struct AdjacentNotes
{
    bool hasPrevious { false };
    bool hasNext { false };

    /** @brief The previous note's last deviation, in semitones. */
    float previousDeviation { 0.0f };

    /** @brief The next note's first deviation, in semitones. */
    float nextDeviation { 0.0f };
};

/** @brief The per-note shaping, all of it applied to the deviation rather than to the pitch.

    None of this is destructive: the deviation measured at analysis is kept untouched, and these
    are re-applied to it every time the melody is composed. Setting them all back to their defaults
    gives back the note exactly as it was sung.
*/
struct PitchTools
{
    /** @brief Tilts the deviation about a pivot, in semitones at the furthest edge.

        @param deviation  The note's deviation from its base, in semitones.
        @param pivot      Where the tilt turns, from 0 at the start of the note to 1 at its end.
        @param amount     How far the furthest edge moves, in semitones.
    */
    [[nodiscard]] static std::vector<float> tilt (const std::vector<float>& deviation,
                                                  float pivot,
                                                  float amount);

    /** @brief Scales the deviation about the note's centre; 0 flattens it, 1 leaves it as sung. */
    [[nodiscard]] static std::vector<float> scale (const std::vector<float>& deviation, float factor);

    /** @brief Fades one end of the note towards what its neighbour is doing.

        The fade is Gaussian with a deviation of half the transition, so it is full at the boundary
        and has all but let go by the far end of the transition.

        @param deviation         The note's deviation from its base, in semitones.
        @param isRightBoundary   False smooths the start of the note, true its end.
        @param transitionFrames  How many frames the fade covers.
        @param target            The deviation to fade towards, in semitones.
    */
    [[nodiscard]] static std::vector<float> smoothBoundary (const std::vector<float>& deviation,
                                                            bool isRightBoundary,
                                                            int transitionFrames,
                                                            float target);
};
}
