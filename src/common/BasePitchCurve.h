#pragma once

#include <vector>

namespace multiplyandreplenish
{
/** @brief Turns a row of notes into the smooth line their pitches trace.

    A note names one pitch, but a singer does not step from one to the next: they arrive over some
    tens of milliseconds. So the notes are first laid out as a staircase, one value per millisecond,
    stepping at the midpoint between the end of one note and the start of the next, and that
    staircase is then convolved with a raised cosine. What comes out is the line the melody would
    follow if the singer had no vibrato, no drift and no unsteadiness — the @e base.

    Everything the singer actually did is then kept as the difference from this line, so that moving
    a note moves only the line, and the singing rides along on top of it unchanged.
*/
struct BasePitchCurve
{
    /** @brief One note as this curve sees it: a span of frames holding one pitch. */
    struct NoteSegment
    {
        int firstFrame { 0 };
        int lastFrame { 0 };
        float semitones { 60.0f };
    };

    /** @brief How many taps the smoothing kernel has, one per millisecond. */
    static constexpr int kernelSize = 81;

    /** @brief The width of the raised cosine, in seconds. */
    static constexpr double smoothWindowSeconds = 0.08;

    /** @brief The raised cosine the staircase is convolved with, normalised to sum to one. */
    [[nodiscard]] static const std::vector<double>& getKernel();

    /** @brief Draws the base line for a whole take.

        @param notes       The notes, in any order; they are sorted here.
        @param numFrames   How many frames the result should cover.
        @param frameRate   Frames per second of the melody the result lines up with.
        @return One value per frame, in semitones.
    */
    [[nodiscard]] static std::vector<float> generate (const std::vector<NoteSegment>& notes,
                                                      int numFrames,
                                                      double frameRate);
};
}
