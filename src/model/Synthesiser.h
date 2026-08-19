#pragma once

#include "model/PitchDetector.h"

#include <juce_audio_basics/juce_audio_basics.h>
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

        Channels are read and rendered one by one, so a stereo take stays stereo. Channels that
        arrive identical are read once and rendered once, which keeps a mono take recorded to two
        channels exactly centred and costs nothing extra.

        @param recording    The source audio, however many channels it has.
        @param sampleRate   Rate the recording arrived at, and the rate render() returns.
        @param melody       The melody as sung, which fixes the frame grid everything else uses.
        @param onProgress   Called with the fraction prepared, on the calling thread.
        @param shouldAbort  Polled between chunks.
        @param error        Set when preparation fails.
    */
    virtual bool prepare (const juce::AudioBuffer<float>& recording,
                          double sampleRate,
                          const PitchTrack& melody,
                          const ProgressCallback& onProgress,
                          const std::atomic<bool>& shouldAbort,
                          juce::String& error) = 0;

    [[nodiscard]] virtual bool isPrepared() const noexcept = 0;

    /** @brief Renders one span of the edited melody.
        @param melodyHz     The edited melody, one entry per frame of the whole recording.
        @param firstFrame   First frame of the span.
        @param numFrames    Length of the span, in frames.
        @param destination  Written with getNumChannels() channels at the source sample rate; it is
                            resized to hold the span.
        @param error        Set when the render fails.
        @return True when the span was rendered.
    */
    virtual bool render (const float* melodyHz,
                         int firstFrame,
                         int numFrames,
                         juce::AudioBuffer<float>& destination,
                         juce::String& error) const = 0;

    /** @brief Channels the render comes back with, which is what the recording had. */
    [[nodiscard]] virtual int getNumChannels() const noexcept = 0;

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
