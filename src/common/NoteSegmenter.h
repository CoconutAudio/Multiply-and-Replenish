#pragma once

#include "common/Note.h"
#include "nn/PitchDetector.h"

#include <vector>

namespace multiplyandreplenish
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

    /** @brief The stretch a note's centre is measured over when deciding where the next one starts.

        It has to be shorter than the notes being played, or the centre is a median across several
        of them and every step is measured against the wrong pitch. Fast playing needs it short.
    */
    double centreWindowMilliseconds { 250.0 };

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
    @param settings  Segmentation thresholds.
    @return The notes, in time order.
*/
[[nodiscard]] std::vector<Note> segmentNotes (const PitchTrack& melody,
                                              const SegmenterSettings& settings = {});

/** @brief Turns the segments a model found into notes, measured against the melody.

    The model says where the notes are and roughly what they are; the pitch each note holds still
    comes from the melody, because that is what everything downstream is measured against.

    @param firstFrames  The first frame of each segment.
    @param lastFrames   One past the last frame of each segment.
    @param melody       The melody as estimated.
    @param settings     Supplies the fraction of the note ignored at each edge.
    @return The notes, in time order.
*/
[[nodiscard]] std::vector<Note> notesFromSegments (const std::vector<int>& firstFrames,
                                                   const std::vector<int>& lastFrames,
                                                   const PitchTrack& melody,
                                                   const SegmenterSettings& settings = {});

/** @brief Measures the pitch a note holds from the melody underneath it.
    @param note      The note to measure; its frame span is read, its pitch written.
    @param melody    The melody as estimated.
    @param settings  Supplies the fraction of the note ignored at each edge.
*/
void measureNote (Note& note,
                  const PitchTrack& melody,
                  const SegmenterSettings& settings = {});
}
