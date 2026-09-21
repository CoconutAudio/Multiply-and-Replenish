#pragma once

#include <algorithm>
#include <vector>

namespace multiplyandreplenish
{
/** @brief One note: the frames it covers, the pitch it holds, and everything it does around it.

    @section timing Timing

    A note knows where it was played and where it is heard. Those are the same until the note is
    moved in time, and @ref sourceFirstFrame is what the recording is read from while
    @ref firstFrame is where the result is written.

    @section pitch Pitch

    The pitch is kept in two parts. @ref semitones is the note itself — one number, the thing a
    piano roll draws — and @ref deviation is everything the singer did around it, frame by frame:
    the scoop into the note, the vibrato, the drift, the unsteadiness. The two are defined so that
    adding them back together reproduces what was sung @e exactly, which is why an unedited take
    plays as the recording and not as a re-synthesis of it.

    Moving the note changes @ref semitones and leaves @ref deviation alone. So the singing is not
    flattened onto the new pitch, it is carried to it.
*/
struct Note
{
    /** @brief Where the note is heard, in melody frames. */
    int firstFrame { 0 };
    int lastFrame { 0 };

    /** @brief Where the note was played, in melody frames; fixed once the take is analysed. */
    int sourceFirstFrame { 0 };
    int sourceLastFrame { 0 };

    /** @brief The pitch of the note, in semitones on the MIDI scale, where 69 is A4. */
    float semitones { 60.0f };

    /** @brief The pitch as it was played, kept so an edit can always be measured or undone. */
    float originalSemitones { 60.0f };

    /** @brief The pitch as the last edit by hand left it.

        Retuning works from this rather than from @ref semitones, so that running it again at a
        lower strength restores the hand edit instead of correcting its own output a second time.
    */
    float lastEditedSemitones { 60.0f };

    /** @brief Moves the note without counting as an edit to it, in semitones. */
    float pitchOffset { 0.0f };

    /** @brief What the singer did around the note, in semitones, one value per frame.

        This is the curve measured at analysis and it is never written to. Every tool below is
        re-applied to it each time the melody is composed, so the note can always be put back.
    */
    std::vector<float> deviation;

    /** @brief Tilts the start of the note, in semitones. */
    float tiltLeft { 0.0f };

    /** @brief Tilts the end of the note, in semitones. */
    float tiltRight { 0.0f };

    /** @brief Scales the movement around the note; 1 as sung, 0 flat, negative inverts it. */
    float vibrato { 1.0f };

    /** @brief How many frames at the start of the note are faded towards the previous one. */
    int smoothLeftFrames { 0 };

    /** @brief How many frames at the end of the note are faded towards the next one. */
    int smoothRightFrames { 0 };

    /** @brief Scales the whole deviation after the tools above have run. */
    float deviationScale { 1.0f };

    /** @brief Shifts the whole deviation after the tools above have run, in semitones. */
    float deviationOffset { 0.0f };

    /** @brief Gain applied to the note when it is rendered, in decibels. */
    float gainDecibels { 0.0f };

    /** @brief True where nothing was sung, so the note holds a silence rather than a pitch. */
    bool isRest { false };

    [[nodiscard]] int getNumFrames() const noexcept { return std::max (0, lastFrame - firstFrame); }

    [[nodiscard]] int getNumSourceFrames() const noexcept
    {
        return std::max (0, sourceLastFrame - sourceFirstFrame);
    }

    [[nodiscard]] bool contains (int frameIndex) const noexcept
    {
        return frameIndex >= firstFrame && frameIndex < lastFrame;
    }

    /** @brief The pitch the note is heard at, before its deviation is added. */
    [[nodiscard]] float getSoundingSemitones() const noexcept { return semitones + pitchOffset; }

    /** @brief How far the note has been moved from where it was played, in semitones. */
    [[nodiscard]] float getShift() const noexcept { return getSoundingSemitones() - originalSemitones; }

    /** @brief True where the note is exactly as it was played. */
    [[nodiscard]] bool isAsPlayed() const noexcept
    {
        return getShift() == 0.0f
            && tiltLeft == 0.0f
            && tiltRight == 0.0f
            && vibrato == 1.0f
            && smoothLeftFrames == 0
            && smoothRightFrames == 0
            && deviationScale == 1.0f
            && deviationOffset == 0.0f
            && firstFrame == sourceFirstFrame
            && lastFrame == sourceLastFrame;
    }
};
}
