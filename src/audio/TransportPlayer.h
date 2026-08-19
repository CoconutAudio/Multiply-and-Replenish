#pragma once

#include "edit/RenderScheduler.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>

namespace rvctuner
{
/** @brief Plays the recording, or the correction of it, and reports where it has got to.

    Anything not rendered yet plays as it was sung, so the editor is audible from the moment a file
    is open and simply sharpens into the corrected version as the renderer catches up.

    The audio thread owns the read position and advances it by exactly what the resampler consumed;
    the position seen from outside is a copy for the playhead to follow, and a seek is a request the
    audio thread picks up. Nothing here allocates or waits once playback has started.
*/
class TransportPlayer final : private juce::AudioIODeviceCallback
{
public:
    explicit TransportPlayer (juce::AudioDeviceManager& deviceManager);
    ~TransportPlayer() override;

    /** @brief Which of the two the listener hears. */
    enum class Monitor
    {
        corrected,
        original
    };

    /** @brief Points the player at a recording and the renderer following it. */
    void setRecording (const juce::AudioBuffer<float>* recording,
                       double sampleRate,
                       RenderScheduler* scheduler);

    void start();
    void stop();

    [[nodiscard]] bool isPlaying() const noexcept { return playing.load (std::memory_order_acquire); }

    /** @brief Where playback has got to, in seconds. */
    [[nodiscard]] double getPosition() const;

    void setPosition (double seconds);

    void setMonitor (Monitor monitor) noexcept { monitoring.store (monitor, std::memory_order_release); }

    [[nodiscard]] Monitor getMonitor() const noexcept { return monitoring.load (std::memory_order_acquire); }

    /** @brief Sets the stretch that playback returns to, or turns looping off. */
    void setLoop (double firstSecond, double lastSecond, bool shouldLoop);

    [[nodiscard]] bool isLooping() const noexcept { return looping.load (std::memory_order_acquire); }

    [[nodiscard]] double getLoopStart() const noexcept { return loopFirstSecond; }
    [[nodiscard]] double getLoopEnd() const noexcept { return loopLastSecond; }

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    /** @brief Fills @p destination from the recording, corrected wherever the whole stretch is
               rendered, so that a span finishing mid-buffer cannot splice one into the other.
    */
    void readSource (float* destination, int firstSample, int numSamples);

    juce::CriticalSection sourceLock;
    const juce::AudioBuffer<float>* source { nullptr };
    RenderScheduler* renderer { nullptr };
    double sourceSampleRate { 44100.0 };

    juce::AudioDeviceManager& devices;

    juce::LagrangeInterpolator interpolator;
    std::vector<float> scratch;

    /** @brief The next sample to read, owned by the audio thread. */
    int readPosition { 0 };

    std::atomic<bool> playing { false };
    std::atomic<int> seekRequest { -1 };
    std::atomic<double> position { 0.0 };
    std::atomic<Monitor> monitoring { Monitor::corrected };
    std::atomic<bool> looping { false };

    std::atomic<double> loopFirstSecond { 0.0 };
    std::atomic<double> loopLastSecond { 0.0 };

    double deviceSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportPlayer)
};
}
