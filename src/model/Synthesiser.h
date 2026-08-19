#pragma once

#include "model/PitchDetector.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <vector>

namespace rvctuner
{
/** @brief One way of turning an edited melody back into singing.

    A synthesiser reads the recording once, in prepare(), and keeps whatever survives editing: the
    timbre, the words, the delivery. render() is then called again and again as the melody changes,
    for whichever span of it the editor needs to hear, at the sample rate the recording arrived at.
*/
class Synthesiser
{
public:
    virtual ~Synthesiser() = default;

    using ProgressCallback = std::function<void (float)>;

    /** @brief The name this engine is offered under. */
    [[nodiscard]] virtual juce::String getName() const = 0;

    /** @brief Reads the recording, once.
        @param samples      Mono source audio.
        @param numSamples   Length of @p samples.
        @param sampleRate   Rate @p samples arrived at, and the rate render() returns.
        @param melody       The melody as sung, which fixes the frame grid everything else uses.
        @param onProgress   Called with the fraction prepared, on the calling thread.
        @param shouldAbort  Polled between chunks.
        @param error        Set when preparation fails.
    */
    virtual bool prepare (const float* samples,
                          int numSamples,
                          double sampleRate,
                          const PitchTrack& melody,
                          const ProgressCallback& onProgress,
                          const std::atomic<bool>& shouldAbort,
                          juce::String& error) = 0;

    [[nodiscard]] virtual bool isPrepared() const noexcept = 0;

    /** @brief Renders one span of the edited melody.
        @param melodyHz    The edited melody, one entry per frame of the whole recording.
        @param firstFrame  First frame of the span.
        @param numFrames   Length of the span, in frames.
        @param error       Set when the render fails.
        @return The span at the source sample rate, or empty on failure.
    */
    [[nodiscard]] virtual std::vector<float> render (const float* melodyHz,
                                                     int firstFrame,
                                                     int numFrames,
                                                     juce::String& error) const = 0;

    /** @brief Frames either side of a span that its render depends on, which is how far an edit
               reaches into neighbouring spans.
    */
    [[nodiscard]] virtual int getDependencyFrames() const noexcept = 0;

    /** @brief The frame grid the melody and every span are counted in. */
    [[nodiscard]] virtual int getFrameRate() const noexcept = 0;

    /** @brief Frames the prepared recording covers. */
    [[nodiscard]] virtual int getNumFrames() const noexcept = 0;
};
}
