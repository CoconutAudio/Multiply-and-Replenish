#include "audio/TransportPlayer.h"

#include <algorithm>
#include <cmath>

namespace rvctuner
{
namespace
{
    /** @brief Room for the resampler to read ahead, and for a device that hands out more than it
               promised in a callback.
    */
    constexpr int scratchHeadroom = 64;
    constexpr double scratchOversize = 4.0;
}

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
    seekRequest.store (0, std::memory_order_release);
}

void TransportPlayer::start()
{
    const juce::ScopedLock lock { sourceLock };

    if (source == nullptr || source->getNumSamples() == 0)
        return;

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
    const auto clamped = std::max (0.0, seconds);

    position.store (clamped, std::memory_order_release);
    seekRequest.store (static_cast<int> (std::llround (clamped * sourceSampleRate)),
                       std::memory_order_release);
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

    const auto blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 1024;
    const auto ratio = sourceSampleRate / std::max (deviceSampleRate, 1.0);

    scratch.assign (static_cast<std::size_t> (static_cast<double> (blockSize) * ratio * scratchOversize)
                        + scratchHeadroom,
                    0.0f);

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

    if (renderer != nullptr
        && monitoring.load (std::memory_order_acquire) == Monitor::corrected
        && renderer->isReady (firstSample, numAvailable)
        && renderer->read (destination, firstSample, numAvailable) == numAvailable)
        return;

    const auto numChannels = source->getNumChannels();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* channelData = source->getReadPointer (channel, firstSample);

        for (int sampleIndex = 0; sampleIndex < numAvailable; ++sampleIndex)
            destination[sampleIndex] += channelData[sampleIndex] / static_cast<float> (numChannels);
    }
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

    if (! lock.isLocked() || source == nullptr)
        return;

    if (const auto seek = seekRequest.exchange (-1, std::memory_order_acq_rel); seek >= 0)
    {
        readPosition = seek;
        interpolator.reset();
    }

    if (! playing.load (std::memory_order_acquire))
        return;

    auto* left = numOutputChannels > 0 ? outputChannelData[0] : nullptr;

    if (left == nullptr)
        return;

    const auto ratio = sourceSampleRate / std::max (deviceSampleRate, 1.0);
    const auto numWanted = static_cast<int> (std::ceil (static_cast<double> (numSamples) * ratio))
                         + scratchHeadroom;

    if (static_cast<int> (scratch.size()) < numWanted)
        return;

    if (looping.load (std::memory_order_acquire))
    {
        const auto first = static_cast<int> (std::llround (loopFirstSecond.load (std::memory_order_acquire)
                                                           * sourceSampleRate));
        const auto last = static_cast<int> (std::llround (loopLastSecond.load (std::memory_order_acquire)
                                                          * sourceSampleRate));

        if (last > first && (readPosition < first || readPosition >= last))
        {
            readPosition = first;
            interpolator.reset();
        }
    }

    if (readPosition >= source->getNumSamples())
    {
        playing.store (false, std::memory_order_release);
        return;
    }

    readSource (scratch.data(), readPosition, numWanted);

    const auto numUsed = interpolator.process (ratio, scratch.data(), left, numSamples);

    for (int channel = 1; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            std::copy (left, left + numSamples, outputChannelData[channel]);

    readPosition += numUsed;

    position.store (static_cast<double> (readPosition) / sourceSampleRate, std::memory_order_release);
}
}
