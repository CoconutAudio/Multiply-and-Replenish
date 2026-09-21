#pragma once

#include "common/Note.h"
#include "nn/PitchDetector.h"

#include <vector>

namespace multiplyandreplenish
{
/** @brief The melody to render, and the curves the editor draws over the take. */
struct ComposedMelody
{
    /** @brief The melody to render, in hertz.

        This is dense: it carries a pitch through the frames where nothing was sung, because that
        is what the engine was trained on. Silence is decided when the audio is put together, not
        here, so that the engine is never handed a discontinuity it has to invent a way across.
    */
    std::vector<float> fundamentalFrequencyHz;

    /** @brief Where the take was voiced, carried through so the renderer can leave breaths alone. */
    std::vector<bool> isVoiced;

    /** @brief The line the notes trace, in semitones, before anything the singer did is added. */
    std::vector<float> baseSemitones;

    /** @brief What the singer did around that line, in semitones. */
    std::vector<float> deviationSemitones;

    /** @brief The melody as sung, in semitones, dense. */
    std::vector<float> sungSemitones;

    /** @brief The melody to render, in semitones, for drawing over the sung one. */
    std::vector<float> composedSemitones;

    /** @brief The level each frame is rendered at, as a factor. */
    std::vector<float> gain;
};

/** @brief Carries a pitch through the frames where nothing was sung.

    The interpolation is done in log frequency, so a gap between two notes an octave apart is
    crossed the way a voice crosses it rather than the way a number line does.
*/
[[nodiscard]] std::vector<float> interpolateThroughUnvoiced (const PitchTrack& melody);

/** @brief Measures what the singer did around each note and stores it on the note.

    The notes are laid out as a base line, and what the take does either side of that line becomes
    each note's @ref Note::deviation. Because the deviation is defined as the difference, adding it
    back reproduces the take exactly — so a note that has not been edited is not changed at all.

    Call this once when a take is analysed, and again only if the notes themselves are re-cut.
*/
void measureDeviation (const PitchTrack& melody, std::vector<Note>& notes);

/** @brief Works out what should be heard, from the notes and what was measured around them.

    The notes give a base line; each note's deviation is shaped by its own tools and laid back on
    top. Nothing here touches what was measured, so any edit can be taken back exactly.

    @param melody  The melody as estimated, for its voicing and its frame rate.
    @param notes   The notes, in any order.
*/
[[nodiscard]] ComposedMelody composeMelody (const PitchTrack& melody, const std::vector<Note>& notes);

/** @brief Resamples a curve to a new length by reading straight lines between its points. */
[[nodiscard]] std::vector<float> resampleCurve (const std::vector<float>& curve, int numPoints);
}
