#pragma once

#include "common/RenderScheduler.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <vector>

namespace multiplyandreplenish
{
/** @brief Plays the tabs mixed together, and reports where it has got to.

    The first tab plays as it was sung wherever it is not rendered yet, so playback is audible from
    the moment a file is open and sharpens into the edit as the renderer catches up. Every other
    tab is silent until its own render is ready for the block being read.

    This has no audio device of its own: @ref renderBlock is called from AudioProcessor::processBlock,
    which is what lets the same code drive both the VST3 and the standalone build, the standalone's
    own device being owned and driven by JUCE's generated wrapper rather than by this class.

    The audio thread owns the read position and advances it by exactly what the resampler consumed;
    the position seen from outside is a copy for the playhead to follow, and a seek is a request the
    audio thread picks up. Nothing here allocates or waits once playback has started.
*/
class TransportPlayer final
{
public:
    TransportPlayer();
    ~TransportPlayer();

    /** @brief Points the player at a recording, and clears the tabs it was playing. */
    void setRecording (const juce::AudioBuffer<float>* recording, double sampleRate);

    /** @brief The tab renders to mix, replacing whatever was set before; the first is the main one. */
    void setTabs (std::vector<RenderScheduler*> schedulers);

    void start();
    void stop();

    [[nodiscard]] bool isPlaying() const noexcept { return playing.load (std::memory_order_acquire); }

    /** @brief Where playback has got to, in seconds. */
    [[nodiscard]] double getPosition() const;

    void setPosition (double seconds);

    /** @brief Sizes the scratch buffers for a host rate and block size, as AudioProcessor::prepareToPlay
               would report them. Called again whenever either changes.
    */
    void prepare (double newHostSampleRate, int maximumBlockSize);

    /** @brief Drops what @ref prepare allocated; safe to call whether or not it was ever called. */
    void releaseResources();

    /** @brief Fills one block of host audio, called from AudioProcessor::processBlock.

        Every channel is cleared first, so nothing here is added on top of whatever the buffer held
        going in — a plug-in with an input bus should read it before calling this, not after.

        @param outputChannelData  Where to write; silence where nothing is playing.
        @param numOutputChannels  How many of @p outputChannelData are valid.
        @param numSamples         How many samples to fill, at the host rate @ref prepare was given.
    */
    void renderBlock (float* const* outputChannelData, int numOutputChannels, int numSamples);

private:
    /** @brief Fills the scratch buffer from the tabs, taking a tab only where its whole stretch is
               rendered, so that a span finishing mid-buffer cannot splice one into another.
    */
    void readSource (int firstSample, int numSamples);

    juce::CriticalSection sourceLock;
    const juce::AudioBuffer<float>* source { nullptr };
    double sourceSampleRate { 44100.0 };

    std::vector<std::unique_ptr<juce::LagrangeInterpolator>> interpolators;
    juce::AudioBuffer<float> scratch;

    std::vector<RenderScheduler*> tabs;
    juce::AudioBuffer<float> tabScratch;

    /** @brief The next sample to read, owned by the audio thread. */
    int readPosition { 0 };

    std::atomic<bool> playing { false };
    std::atomic<int> seekRequest { -1 };
    std::atomic<double> position { 0.0 };

    double hostSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportPlayer)
};
}
