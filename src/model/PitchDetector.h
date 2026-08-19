#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace tuner
{
/** @brief A melody as estimated: one fundamental and one confidence per frame.

    Unvoiced frames hold zero rather than an interpolated value, so that a silence, a consonant and
    a breath stay distinguishable from a sung note.
*/
struct PitchTrack
{
    std::vector<float> fundamentalFrequencyHz;
    std::vector<float> confidence;

    int frameRate { 100 };

    [[nodiscard]] int getNumFrames() const noexcept
    {
        return static_cast<int> (fundamentalFrequencyHz.size());
    }

    [[nodiscard]] double getSeconds() const noexcept
    {
        return static_cast<double> (getNumFrames()) / static_cast<double> (frameRate);
    }

    [[nodiscard]] bool isVoiced (int frameIndex) const noexcept
    {
        return frameIndex >= 0 && frameIndex < getNumFrames()
            && fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] > 0.0f;
    }
};

/** @brief One way of hearing the melody in a recording.

    Every detector reads mono audio at getSampleRate() and reports one frame every 10 ms, so the
    editor can offer them as a choice without the timeline changing underneath it.
*/
class PitchDetector
{
public:
    virtual ~PitchDetector() = default;

    /** @brief The name this detector is offered under. */
    [[nodiscard]] virtual juce::String getName() const = 0;

    /** @brief The rate @ref estimate expects its input at. */
    [[nodiscard]] virtual int getSampleRate() const noexcept = 0;

    /** @brief Estimates the melody.
        @param samples     Mono audio at getSampleRate().
        @param numSamples  Length of @p samples.
        @param error       Set when estimation fails.
        @return The melody, or an empty track on failure.
    */
    [[nodiscard]] virtual PitchTrack estimate (const float* samples,
                                               int numSamples,
                                               juce::String& error) const = 0;
};
}
