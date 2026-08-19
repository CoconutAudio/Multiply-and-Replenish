#include "edit/CorrectionCurve.h"

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

    float toFrequency (float semitones) noexcept
    {
        return 440.0f * std::exp2 ((semitones - 69.0f) / 12.0f);
    }

    /** @brief The short smoothing that keeps a drawn stroke from starting with a step. */
    constexpr double jointMilliseconds = 8.0;

    double toFrames (double milliseconds, int frameRate) noexcept
    {
        return milliseconds * static_cast<double> (frameRate) / 1000.0;
    }

    /** @brief Carries the shift across the frames no note covers, so nothing jumps at a boundary. */
    void fillBetweenNotes (std::vector<float>& shift, const std::vector<bool>& isCovered)
    {
        const auto numFrames = static_cast<int> (shift.size());

        auto previous = -1;

        for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
        {
            if (! isCovered[static_cast<std::size_t> (frameIndex)])
                continue;

            if (previous < 0)
            {
                for (int fill = 0; fill < frameIndex; ++fill)
                    shift[static_cast<std::size_t> (fill)] = shift[static_cast<std::size_t> (frameIndex)];
            }
            else if (frameIndex > previous + 1)
            {
                const auto first = shift[static_cast<std::size_t> (previous)];
                const auto last = shift[static_cast<std::size_t> (frameIndex)];
                const auto span = static_cast<float> (frameIndex - previous);

                for (int fill = previous + 1; fill < frameIndex; ++fill)
                    shift[static_cast<std::size_t> (fill)] =
                        first + (last - first) * static_cast<float> (fill - previous) / span;
            }

            previous = frameIndex;
        }

        for (int fill = previous + 1; fill < numFrames && previous >= 0; ++fill)
            shift[static_cast<std::size_t> (fill)] = shift[static_cast<std::size_t> (previous)];
    }
}

CorrectedMelody correctMelody (const PitchTrack& melody,
                               const std::vector<Note>& notes,
                               const std::vector<float>& drawn,
                               const CorrectionSettings& settings)
{
    CorrectedMelody corrected;

    const auto numFrames = melody.getNumFrames();

    if (numFrames <= 0)
        return corrected;

    corrected.sungSemitones.assign (static_cast<std::size_t> (numFrames), 0.0f);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
        if (melody.isVoiced (frameIndex))
            corrected.sungSemitones[static_cast<std::size_t> (frameIndex)] =
                toSemitones (melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)]);

    std::vector<float> centring (static_cast<std::size_t> (numFrames), 0.0f);
    std::vector<float> shaping (static_cast<std::size_t> (numFrames), 0.0f);
    std::vector<bool> isCovered (static_cast<std::size_t> (numFrames), false);

    const auto driftDeviation = toFrames (settings.driftMilliseconds, melody.frameRate);
    const auto detailDeviation = toFrames (settings.detailMilliseconds, melody.frameRate);

    for (const auto& note : notes)
    {
        const auto first = std::max (note.firstFrame, 0);
        const auto last = std::min (note.lastFrame, numFrames);

        if (last <= first)
            continue;

        for (int frameIndex = first; frameIndex < last; ++frameIndex)
            isCovered[static_cast<std::size_t> (frameIndex)] = true;

        if (! note.isEnabled)
            continue;

        std::vector<float> deviation (static_cast<std::size_t> (last - first), 0.0f);

        for (int frameIndex = first; frameIndex < last; ++frameIndex)
            if (melody.isVoiced (frameIndex))
                deviation[static_cast<std::size_t> (frameIndex - first)] =
                    corrected.sungSemitones[static_cast<std::size_t> (frameIndex)]
                    - static_cast<float> (note.sungPitch);

        const auto wander = gaussianFilter (deviation, driftDeviation);
        const auto steady = gaussianFilter (deviation, detailDeviation);

        const auto centre = static_cast<float> (static_cast<double> (note.targetNote) - note.sungPitch)
                          * note.correction * settings.correction;

        for (int frameIndex = first; frameIndex < last; ++frameIndex)
        {
            const auto index = static_cast<std::size_t> (frameIndex - first);

            const auto slow = wander[index];
            const auto shake = steady[index] - slow;
            const auto detail = deviation[index] - steady[index];

            const auto shaped = slow * note.drift * settings.drift
                              + shake * note.vibrato * settings.vibrato
                              + detail;

            centring[static_cast<std::size_t> (frameIndex)] = centre;
            shaping[static_cast<std::size_t> (frameIndex)] = shaped - deviation[index];
        }
    }

    fillBetweenNotes (centring, isCovered);

    // Only where a note sits is smoothed across boundaries: how it was shaped within the note is
    // the singer's own movement, and smoothing that would put back what the shaping took out.
    auto shift = gaussianFilter (centring, toFrames (settings.transitionMilliseconds, melody.frameRate));

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
        shift[static_cast<std::size_t> (frameIndex)] += shaping[static_cast<std::size_t> (frameIndex)];

    if (static_cast<int> (drawn.size()) == numFrames)
        for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
            if (drawn[static_cast<std::size_t> (frameIndex)] > 0.0f && melody.isVoiced (frameIndex))
                shift[static_cast<std::size_t> (frameIndex)] =
                    drawn[static_cast<std::size_t> (frameIndex)]
                    - corrected.sungSemitones[static_cast<std::size_t> (frameIndex)];

    corrected.shiftSemitones = gaussianFilter (shift, toFrames (jointMilliseconds, melody.frameRate));

    std::vector<float> gain (static_cast<std::size_t> (numFrames), 1.0f);

    for (const auto& note : notes)
    {
        if (note.gainDecibels == 0.0f)
            continue;

        const auto factor = std::pow (10.0f, note.gainDecibels / 20.0f);

        for (int frameIndex = std::max (note.firstFrame, 0);
             frameIndex < std::min (note.lastFrame, numFrames);
             ++frameIndex)
            gain[static_cast<std::size_t> (frameIndex)] = factor;
    }

    corrected.gain = gaussianFilter (gain, toFrames (jointMilliseconds * 2.0, melody.frameRate));

    corrected.fundamentalFrequencyHz.assign (static_cast<std::size_t> (numFrames), 0.0f);
    corrected.correctedSemitones.assign (static_cast<std::size_t> (numFrames), 0.0f);

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        if (! melody.isVoiced (frameIndex))
            continue;

        const auto index = static_cast<std::size_t> (frameIndex);
        const auto semitones = corrected.sungSemitones[index] + corrected.shiftSemitones[index];

        corrected.correctedSemitones[index] = semitones;
        corrected.fundamentalFrequencyHz[index] = toFrequency (semitones);
    }

    return corrected;
}
}
