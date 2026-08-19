#pragma once

#include "edit/Note.h"
#include "model/PitchDetector.h"

#include <vector>

namespace tuner
{
/** @brief The settings that shape every note at once. */
struct CorrectionSettings
{
    /** @brief Scales each note's own correction; 0 leaves the whole take as sung. */
    float correction { 1.0f };

    /** @brief Scales each note's vibrato depth. */
    float vibrato { 1.0f };

    /** @brief Scales each note's slow wander. */
    float drift { 1.0f };

    /** @brief How long the correction takes to come and go across a note boundary. */
    float transitionMilliseconds { 45.0f };

    /** @brief Modulation slower than this is drift rather than vibrato. */
    float driftMilliseconds { 130.0f };

    /** @brief Modulation faster than this is neither drift nor vibrato, and is always kept.

        It is the jitter of a real voice, a few hundredths of a semitone of unsteadiness, and
        keeping it is most of why a corrected note still sounds sung.
    */
    float detailMilliseconds { 8.0f };
};

/** @brief What the correction did, frame by frame, alongside the melody it produced. */
struct CorrectedMelody
{
    /** @brief The melody to render, in hertz, with unvoiced frames left at zero. */
    std::vector<float> fundamentalFrequencyHz;

    /** @brief How far each frame moved, in semitones. */
    std::vector<float> shiftSemitones;

    /** @brief The melody as sung, in semitones, with unvoiced frames left at zero. */
    std::vector<float> sungSemitones;

    /** @brief The corrected melody in semitones, for drawing over the sung one. */
    std::vector<float> correctedSemitones;

    /** @brief The level each frame is rendered at, as a factor, smoothed across note boundaries. */
    std::vector<float> gain;
};

/** @brief Works out what the singer should be heard singing.

    Each note is split into where it sat, how it wandered, how it shook and the detail below that.
    Correction moves only where it sat; the wander and the shake are scaled; the detail is always
    kept, because it is most of what makes a corrected note still sound sung. The shift is then
    smoothed across note boundaries, so that the tuning arrives and leaves the way a slide does.

    @param melody    The melody as estimated.
    @param notes     The notes, in time order.
    @param drawn     Pitches drawn by hand, in semitones, with anything not drawn left at zero.
    @param settings  The global shaping.
    @return The melody to render and the curves the editor draws.
*/
[[nodiscard]] CorrectedMelody correctMelody (const PitchTrack& melody,
                                             const std::vector<Note>& notes,
                                             const std::vector<float>& drawn,
                                             const CorrectionSettings& settings);
}
