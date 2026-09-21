#include "common/PitchCurve.h"

#include "common/BasePitchCurve.h"
#include "common/PitchTools.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace multiplyandreplenish
{
namespace
{
    float toSemitones (float frequencyHz) noexcept
    {
        if (frequencyHz <= 0.0f)
            return 0.0f;

        return 69.0f + 12.0f * std::log2 (frequencyHz / 440.0f);
    }

    float toFrequency (float semitones) noexcept
    {
        return 440.0f * std::exp2 ((semitones - 69.0f) / 12.0f);
    }

    /** @brief Lays the notes out as a base line.

        A note that has been tilted has its base taken down by the mean of the tilt, so that tilting
        one end up and the other down turns the note about its centre instead of walking it.

        @param asPlayed  True draws the line the take was sung around, which is what the deviation is
                         measured against; false draws the line it is now heard around.
    */
    std::vector<float> makeBase (const std::vector<Note>& notes, const PitchTrack& melody, bool asPlayed)
    {
        std::vector<BasePitchCurve::NoteSegment> segments;
        segments.reserve (notes.size());

        for (const auto& note : notes)
        {
            if (note.isRest || note.getNumFrames() <= 0)
                continue;

            const auto semitones = asPlayed
                                 ? note.originalSemitones
                                 : note.getSoundingSemitones() - (note.tiltLeft + note.tiltRight) / 2.0f;

            segments.push_back ({ note.firstFrame, note.lastFrame, semitones });
        }

        return BasePitchCurve::generate (segments, melody.getNumFrames(), melody.frameRate);
    }

    /** @brief The deviation the notes on either side reach where they meet this one. */
    AdjacentNotes findAdjacent (const std::vector<Note>& notes, std::size_t noteIndex)
    {
        AdjacentNotes adjacent;

        const auto& note = notes[noteIndex];

        auto previousLast = std::numeric_limits<int>::min();
        auto nextFirst = std::numeric_limits<int>::max();

        for (std::size_t other = 0; other < notes.size(); ++other)
        {
            if (other == noteIndex || notes[other].isRest || notes[other].deviation.empty())
                continue;

            const auto& candidate = notes[other];

            if (candidate.lastFrame <= note.firstFrame && candidate.lastFrame > previousLast)
            {
                previousLast = candidate.lastFrame;
                adjacent.hasPrevious = true;
                adjacent.previousDeviation = candidate.deviation.back();
            }

            if (candidate.firstFrame >= note.lastFrame && candidate.firstFrame < nextFirst)
            {
                nextFirst = candidate.firstFrame;
                adjacent.hasNext = true;
                adjacent.nextDeviation = candidate.deviation.front();
            }
        }

        return adjacent;
    }
}

std::vector<float> resampleCurve (const std::vector<float>& curve, int numPoints)
{
    if (numPoints <= 0)
        return {};

    if (curve.empty())
        return std::vector<float> (static_cast<std::size_t> (numPoints), 0.0f);

    if (numPoints == 1)
        return { curve.front() };

    if (curve.size() == 1)
        return std::vector<float> (static_cast<std::size_t> (numPoints), curve.front());

    std::vector<float> result (static_cast<std::size_t> (numPoints), 0.0f);

    const auto lastPoint = static_cast<float> (curve.size() - 1);

    for (int index = 0; index < numPoints; ++index)
    {
        const auto position = lastPoint * static_cast<float> (index) / static_cast<float> (numPoints - 1);
        const auto lower = std::clamp (static_cast<int> (std::floor (position)), 0, static_cast<int> (curve.size()) - 1);
        const auto upper = std::min (lower + 1, static_cast<int> (curve.size()) - 1);
        const auto fraction = position - static_cast<float> (lower);

        const auto below = curve[static_cast<std::size_t> (lower)];
        const auto above = curve[static_cast<std::size_t> (upper)];

        result[static_cast<std::size_t> (index)] = below + (above - below) * fraction;
    }

    return result;
}

std::vector<float> interpolateThroughUnvoiced (const PitchTrack& melody)
{
    const auto numFrames = melody.getNumFrames();

    if (numFrames <= 0)
        return {};

    auto dense = melody.fundamentalFrequencyHz;
    dense.resize (static_cast<std::size_t> (numFrames), 0.0f);

    auto isVoicedFrame = [&] (int frameIndex)
    {
        return frameIndex >= 0
            && frameIndex < numFrames
            && melody.isVoiced (frameIndex)
            && melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] > 0.0f;
    };

    auto findNextVoiced = [&] (int from)
    {
        for (int frameIndex = from; frameIndex < numFrames; ++frameIndex)
            if (isVoicedFrame (frameIndex))
                return frameIndex;

        return -1;
    };

    auto previous = -1;
    auto next = findNextVoiced (0);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto index = static_cast<std::size_t> (frameIndex);

        if (isVoicedFrame (frameIndex))
        {
            previous = frameIndex;

            if (frameIndex == next)
                next = findNextVoiced (frameIndex + 1);

            continue;
        }

        if (next >= 0 && next < frameIndex)
            next = findNextVoiced (frameIndex + 1);

        const auto before = previous >= 0 ? dense[static_cast<std::size_t> (previous)] : 0.0f;
        const auto after = next >= 0 ? dense[static_cast<std::size_t> (next)] : 0.0f;

        if (before <= 0.0f && after <= 0.0f)
            dense[index] = 0.0f;
        else if (before <= 0.0f)
            dense[index] = after;
        else if (after <= 0.0f)
            dense[index] = before;
        else
        {
            const auto position = next > previous
                                ? static_cast<float> (frameIndex - previous) / static_cast<float> (next - previous)
                                : 0.0f;

            dense[index] = std::exp (std::log (before) * (1.0f - position) + std::log (after) * position);
        }
    }

    return dense;
}

void measureDeviation (const PitchTrack& melody, std::vector<Note>& notes)
{
    const auto numFrames = melody.getNumFrames();

    if (numFrames <= 0)
        return;

    const auto dense = interpolateThroughUnvoiced (melody);
    const auto base = makeBase (notes, melody, true);

    if (static_cast<int> (base.size()) != numFrames)
        return;

    for (auto& note : notes)
    {
        const auto first = std::max (note.firstFrame, 0);
        const auto last = std::min (note.lastFrame, numFrames);

        if (last <= first)
        {
            note.deviation.clear();
            continue;
        }

        note.deviation.assign (static_cast<std::size_t> (last - first), 0.0f);

        for (int frameIndex = first; frameIndex < last; ++frameIndex)
            note.deviation[static_cast<std::size_t> (frameIndex - first)] =
                toSemitones (dense[static_cast<std::size_t> (frameIndex)])
                - base[static_cast<std::size_t> (frameIndex)];
    }
}

ComposedMelody composeMelody (const PitchTrack& melody, const std::vector<Note>& notes)
{
    ComposedMelody composed;

    const auto numFrames = melody.getNumFrames();

    if (numFrames <= 0)
        return composed;

    const auto count = static_cast<std::size_t> (numFrames);

    const auto dense = interpolateThroughUnvoiced (melody);

    composed.isVoiced.assign (count, false);
    composed.sungSemitones.assign (count, 0.0f);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        composed.isVoiced[static_cast<std::size_t> (frameIndex)] = melody.isVoiced (frameIndex);
        composed.sungSemitones[static_cast<std::size_t> (frameIndex)] =
            toSemitones (dense[static_cast<std::size_t> (frameIndex)]);
    }

    composed.baseSemitones = makeBase (notes, melody, false);

    if (static_cast<int> (composed.baseSemitones.size()) != numFrames)
        composed.baseSemitones = composed.sungSemitones;

    composed.deviationSemitones.assign (count, 0.0f);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto index = static_cast<std::size_t> (frameIndex);
        composed.deviationSemitones[index] = composed.sungSemitones[index] - composed.baseSemitones[index];
    }

    for (std::size_t noteIndex = 0; noteIndex < notes.size(); ++noteIndex)
    {
        const auto& note = notes[noteIndex];

        if (note.isRest || note.deviation.empty())
            continue;

        const auto numNoteFrames = note.getNumFrames();

        if (numNoteFrames <= 0)
            continue;

        auto deviation = static_cast<int> (note.deviation.size()) == numNoteFrames
                       ? note.deviation
                       : resampleCurve (note.deviation, numNoteFrames);

        const auto vibrato = note.vibrato;

        if (std::abs (vibrato - 1.0f) > 0.001f)
            deviation = PitchTools::scale (deviation, vibrato);

        if (std::abs (note.tiltLeft) > 0.001f)
            deviation = PitchTools::tilt (deviation, 1.0f, -note.tiltLeft);

        if (std::abs (note.tiltRight) > 0.001f)
            deviation = PitchTools::tilt (deviation, 0.0f, note.tiltRight);

        if (note.smoothLeftFrames > 0 || note.smoothRightFrames > 0)
        {
            const auto adjacent = findAdjacent (notes, noteIndex);

            if (note.smoothLeftFrames > 0)
                deviation = PitchTools::smoothBoundary (deviation, false, note.smoothLeftFrames,
                                                        adjacent.hasPrevious ? adjacent.previousDeviation : 0.0f);

            if (note.smoothRightFrames > 0)
                deviation = PitchTools::smoothBoundary (deviation, true, note.smoothRightFrames,
                                                        adjacent.hasNext ? adjacent.nextDeviation : 0.0f);
        }

        if (std::abs (note.deviationScale - 1.0f) > 0.0001f || std::abs (note.deviationOffset) > 0.0001f)
            for (auto& value : deviation)
                value = value * note.deviationScale + note.deviationOffset;

        const auto first = std::max (note.firstFrame, 0);
        const auto last = std::min (note.lastFrame, numFrames);

        for (int frameIndex = first; frameIndex < last; ++frameIndex)
        {
            const auto index = static_cast<std::size_t> (frameIndex - note.firstFrame);

            if (index < deviation.size())
                composed.deviationSemitones[static_cast<std::size_t> (frameIndex)] = deviation[index];
        }
    }

    composed.composedSemitones.assign (count, 0.0f);
    composed.fundamentalFrequencyHz.assign (count, 0.0f);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto index = static_cast<std::size_t> (frameIndex);

        const auto semitones = composed.baseSemitones[index]
                             + composed.deviationSemitones[index];

        composed.composedSemitones[index] = semitones;
        composed.fundamentalFrequencyHz[index] = toFrequency (semitones);
    }

    composed.gain.assign (count, 1.0f);

    for (const auto& note : notes)
    {
        if (note.gainDecibels == 0.0f)
            continue;

        const auto factor = std::pow (10.0f, note.gainDecibels / 20.0f);

        for (int frameIndex = std::max (note.firstFrame, 0);
             frameIndex < std::min (note.lastFrame, numFrames);
             ++frameIndex)
            composed.gain[static_cast<std::size_t> (frameIndex)] = factor;
    }

    return composed;
}
}
