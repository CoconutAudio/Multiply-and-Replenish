#include "audio/TransportPlayer.h"

#include <algorithm>
#include <cmath>

namespace rvctuner
{
TransportPlayer::TransportPlayer (juce::AudioDeviceManager& deviceManager)
    : devices (deviceManager)
{
    devices.addAudioCallback (this);
}

TransportPlayer::~TransportPlayer()
{
    devices.removeAudioCallback (this);
}

void TransportPlayer::setRecording (const juce::AudioBuffer<float>* recording,
                                    double newSampleRate,
                                    RenderScheduler* scheduler)
{
    stop();

    const juce::ScopedLock lock { sourceLock };

    source = recording;
    renderer = scheduler;
    sourceSampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    position.store (0.0, std::memory_order_release);
}

void TransportPlayer::start()
{
    const juce::ScopedLock lock { sourceLock };

    if (source == nullptr || source->getNumSamples() == 0)
        return;

    interpolator.reset();
    playing.store (true, std::memory_order_release);
}

void TransportPlayer::stop()
{
    playing.store (false, std::memory_order_release);
}

double TransportPlayer::getPosition() const
{
    return position.load (std::memory_order_acquire);
}

void TransportPlayer::setPosition (double seconds)
{
    position.store (std::max (0.0, seconds), std::memory_order_release);
}

void TransportPlayer::setLoop (double firstSecond, double lastSecond, bool shouldLoop)
{
    loopFirstSecond.store (std::min (firstSecond, lastSecond), std::memory_order_release);
    loopLastSecond.store (std::max (firstSecond, lastSecond), std::memory_order_release);
    looping.store (shouldLoop, std::memory_order_release);
}

void TransportPlayer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceSampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    interpolator.reset();
}

void TransportPlayer::audioDeviceStopped()
{
    playing.store (false, std::memory_order_release);
}

void TransportPlayer::readSource (float* destination, int firstSample, int numSamples)
{
    std::fill (destination, destination + numSamples, 0.0f);

    if (source == nullptr)
        return;

    const auto numAvailable = std::min (numSamples, source->getNumSamples() - firstSample);

    if (numAvailable <= 0)
        return;

    const auto numChannels = source->getNumChannels();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* channelData = source->getReadPointer (channel, firstSample);

        for (int sampleIndex = 0; sampleIndex < numAvailable; ++sampleIndex)
            destination[sampleIndex] += channelData[sampleIndex] / static_cast<float> (numChannels);
    }

    if (renderer == nullptr || monitoring.load (std::memory_order_acquire) == Monitor::original)
        return;

    if (renderer->isReady (firstSample, numAvailable))
        renderer->read (destination, firstSample, numAvailable);
}

void TransportPlayer::audioDeviceIOCallbackWithContext (const float* const*,
                                                        int,
                                                        float* const* outputChannelData,
                                                        int numOutputChannels,
                                                        int numSamples,
                                                        const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            std::fill (outputChannelData[channel], outputChannelData[channel] + numSamples, 0.0f);

    const juce::ScopedTryLock lock { sourceLock };

    if (! lock.isLocked() || ! playing.load (std::memory_order_acquire) || source == nullptr)
        return;

    const auto ratio = sourceSampleRate / deviceSampleRate;
    const auto numSourceSamples = static_cast<int> (std::ceil (static_cast<double> (numSamples) * ratio)) + 4;

    if (static_cast<int> (scratch.size()) < numSourceSamples)
        scratch.resize (static_cast<std::size_t> (numSourceSamples));

    auto positionSeconds = position.load (std::memory_order_acquire);

    if (looping.load (std::memory_order_acquire))
    {
        const auto first = loopFirstSecond.load (std::memory_order_acquire);
        const auto last = loopLastSecond.load (std::memory_order_acquire);

        if (last > first && (positionSeconds < first || positionSeconds >= last))
        {
            positionSeconds = first;
            interpolator.reset();
        }
    }

    const auto firstSample = static_cast<int> (std::llround (positionSeconds * sourceSampleRate));

    if (firstSample >= source->getNumSamples())
    {
        playing.store (false, std::memory_order_release);
        return;
    }

    readSource (scratch.data(), firstSample, numSourceSamples);

    auto* left = numOutputChannels > 0 ? outputChannelData[0] : nullptr;

    if (left == nullptr)
        return;

    const auto numUsed = interpolator.process (ratio, scratch.data(), left, numSamples);

    for (int channel = 1; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            std::copy (left, left + numSamples, outputChannelData[channel]);

    position.store (positionSeconds + static_cast<double> (numUsed) / sourceSampleRate,
                    std::memory_order_release);
}
}
