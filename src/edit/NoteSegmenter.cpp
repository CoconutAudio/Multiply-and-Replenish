#include "edit/NoteSegmenter.h"

#include "dsp/PitchConversions.h"
#include "dsp/Smoothing.h"

#include <algorithm>
#include <cmath>

namespace tuner
{
namespace
{
    float toSemitones (float frequencyHz) noexcept
    {
        return 69.0f + 12.0f * std::log2 (frequencyHz / 440.0f);
    }

    /** @brief The frames of one continuous phrase, gaps too short to hear included. */
    struct Run
    {
        int firstFrame { 0 };
        int lastFrame { 0 };
    };

    std::vector<Run> findRuns (const PitchTrack& melody, const SegmenterSettings& settings)
    {
        const auto numFrames = melody.getNumFrames();
        const auto bridged = static_cast<int> (settings.bridgedGapMilliseconds
                                               * static_cast<double> (melody.frameRate) / 1000.0);

        std::vector<Run> runs;

        auto frameIndex = 0;

        while (frameIndex < numFrames)
        {
            const auto isSung = [&] (int index)
            {
                return melody.isVoiced (index)
                    && melody.confidence[static_cast<std::size_t> (index)] >= settings.confidenceThreshold;
            };

            if (! isSung (frameIndex))
            {
                ++frameIndex;
                continue;
            }

            auto lastSung = frameIndex;
            auto scan = frameIndex;

            while (scan < numFrames)
            {
                if (isSung (scan))
                    lastSung = scan;
                else if (scan - lastSung > bridged)
                    break;

                ++scan;
            }

            runs.push_back ({ frameIndex, lastSung + 1 });
            frameIndex = scan;
        }

        return runs;
    }
}

void measureNote (Note& note,
                  const PitchTrack& melody,
                  const Scale& scale,
                  const SegmenterSettings& settings)
{
    const auto numFrames = note.getNumFrames();

    if (numFrames <= 0)
        return;

    const auto edge = std::min (static_cast<int> (settings.edgeFraction * numFrames), numFrames / 3);

    std::vector<float> pitches;
    std::vector<float> weights;

    for (int frameIndex = note.firstFrame + edge; frameIndex < note.lastFrame - edge; ++frameIndex)
    {
        if (! melody.isVoiced (frameIndex))
            continue;

        pitches.push_back (toSemitones (melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)]));
        weights.push_back (melody.confidence[static_cast<std::size_t> (frameIndex)]);
    }

    if (pitches.empty())
        return;

    note.sungPitch = static_cast<double> (weightedMedian (pitches, weights));
    note.targetNote = scale.snap (note.sungPitch);
}

std::vector<Note> segmentNotes (const PitchTrack& melody,
                                const Scale& scale,
                                const SegmenterSettings& settings)
{
    std::vector<Note> notes;

    const auto numFrames = melody.getNumFrames();

    if (numFrames <= 0)
        return notes;

    const auto minimumFrames = std::max (1, static_cast<int> (settings.minimumNoteMilliseconds
                                                              * static_cast<double> (melody.frameRate) / 1000.0));

    std::vector<float> semitones (static_cast<std::size_t> (numFrames), 0.0f);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
        if (melody.isVoiced (frameIndex))
            semitones[static_cast<std::size_t> (frameIndex)] =
                toSemitones (melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)]);

    const auto steadyHalfLength = std::max (1, static_cast<int> (0.5 * settings.steadyWindowMilliseconds
                                                                 * static_cast<double> (melody.frameRate)
                                                                 / 1000.0));

    const auto steady = medianFilter (semitones, steadyHalfLength);

    /** @brief The pitch a stretch of frames sits at, ignoring how it moves within them. */
    const auto centreOf = [&steady, &melody] (int first, int last)
    {
        std::vector<float> pitches;
        std::vector<float> weights;

        for (int frameIndex = first; frameIndex < last; ++frameIndex)
        {
            if (! melody.isVoiced (frameIndex))
                continue;

            pitches.push_back (steady[static_cast<std::size_t> (frameIndex)]);
            weights.push_back (1.0f);
        }

        return pitches.empty() ? 0.0f : weightedMedian (pitches, weights);
    };

    const auto centreFrames = std::max (minimumFrames,
                                        static_cast<int> (0.25 * static_cast<double> (melody.frameRate)));

    for (const auto& run : findRuns (melody, settings))
    {
        std::vector<int> boundaries { run.firstFrame };

        auto centre = centreOf (run.firstFrame, std::min (run.firstFrame + centreFrames, run.lastFrame));
        auto excursionStart = -1;
        auto excursionSign = 0;

        for (int frameIndex = run.firstFrame + 1; frameIndex < run.lastFrame; ++frameIndex)
        {
            if (! melody.isVoiced (frameIndex))
                continue;

            const auto distance = steady[static_cast<std::size_t> (frameIndex)] - centre;
            const auto sign = distance > 0.0f ? 1 : -1;

            if (std::abs (distance) <= settings.splitSemitones)
            {
                excursionStart = -1;
                continue;
            }

            if (excursionStart < 0 || sign != excursionSign)
            {
                excursionStart = frameIndex;
                excursionSign = sign;
                continue;
            }

            if (frameIndex - excursionStart + 1 < minimumFrames)
                continue;

            boundaries.push_back (excursionStart);
            centre = centreOf (excursionStart, std::min (excursionStart + centreFrames, run.lastFrame));
            excursionStart = -1;
        }

        boundaries.push_back (run.lastFrame);

        for (std::size_t index = 0; index + 1 < boundaries.size(); ++index)
        {
            Note note;
            note.firstFrame = boundaries[index];
            note.lastFrame = boundaries[index + 1];

            if (note.getNumFrames() <= 0)
                continue;

            measureNote (note, melody, scale, settings);
            notes.push_back (note);
        }
    }

    // A note too short to hear belongs to whichever neighbour it is closest in pitch to.
    for (auto index = 0; index < static_cast<int> (notes.size());)
    {
        if (notes.size() < 2 || notes[static_cast<std::size_t> (index)].getNumFrames() >= minimumFrames)
        {
            ++index;
            continue;
        }

        const auto& note = notes[static_cast<std::size_t> (index)];

        const auto hasPrevious = index > 0
                              && notes[static_cast<std::size_t> (index - 1)].lastFrame == note.firstFrame;
        const auto hasNext = index + 1 < static_cast<int> (notes.size())
                          && notes[static_cast<std::size_t> (index + 1)].firstFrame == note.lastFrame;

        if (! hasPrevious && ! hasNext)
        {
            ++index;
            continue;
        }

        const auto previousDistance = hasPrevious
            ? std::abs (notes[static_cast<std::size_t> (index - 1)].sungPitch - note.sungPitch)
            : std::numeric_limits<double>::max();
        const auto nextDistance = hasNext
            ? std::abs (notes[static_cast<std::size_t> (index + 1)].sungPitch - note.sungPitch)
            : std::numeric_limits<double>::max();

        const auto mergeInto = previousDistance <= nextDistance ? index - 1 : index + 1;

        auto& neighbour = notes[static_cast<std::size_t> (mergeInto)];
        neighbour.firstFrame = std::min (neighbour.firstFrame, note.firstFrame);
        neighbour.lastFrame = std::max (neighbour.lastFrame, note.lastFrame);

        notes.erase (notes.begin() + index);

        measureNote (neighbour, melody, scale, settings);

        index = std::max (0, mergeInto - 1);
    }

    return notes;
}
}
