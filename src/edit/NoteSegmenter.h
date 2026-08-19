#pragma once

#include "edit/Note.h"
#include "edit/Scale.h"
#include "model/PitchDetector.h"

#include <vector>

namespace tuner
{
/** @brief How readily the melody is cut into notes. */
struct SegmenterSettings
{
    /** @brief Notes shorter than this are folded into a neighbour. */
    double minimumNoteMilliseconds { 120.0 };

    /** @brief Unvoiced gaps shorter than this do not end a note. */
    double bridgedGapMilliseconds { 40.0 };

    /** @brief Confidence a frame needs before it counts as sung. */
    float confidenceThreshold { 0.05f };

    /** @brief Frames either end of a note ignored when measuring its centre, as a fraction. */
    double edgeFraction { 0.12 };

    /** @brief The window the sung pitch is smoothed over before it is read for steps. */
    double steadyWindowMilliseconds { 50.0 };

    /** @brief How far the pitch must leave a note, in semitones, to be a different note.

        Vibrato swings past a semitone and comes back; a new note does not come back. That is what
        this threshold and @ref minimumNoteMilliseconds together test for.
    */
    double splitSemitones { 0.7 };
};

/** @brief Cuts an estimated melody into notes.

    Voiced runs are split wherever the sung pitch settles onto a different semitone for long
    enough to be heard as a new note, which is what separates a slide within one note from two
    notes either side of a step.

    @param melody    The melody as estimated.
    @param scale     The scale each note's target is snapped to.
    @param settings  Segmentation thresholds.
    @return The notes, in time order.
*/
[[nodiscard]] std::vector<Note> segmentNotes (const PitchTrack& melody,
                                              const Scale& scale,
                                              const SegmenterSettings& settings = {});

/** @brief Measures a note's centre and target from the melody underneath it.
    @param note      The note to measure; its frame span is read, its pitch written.
    @param melody    The melody as estimated.
    @param scale     The scale the target is snapped to.
    @param settings  Supplies the fraction of the note ignored at each edge.
*/
void measureNote (Note& note,
                  const PitchTrack& melody,
                  const Scale& scale,
                  const SegmenterSettings& settings = {});
}
