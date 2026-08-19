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
    const auto numChannels = device != nullptr
                           ? std::max (1, device->getActiveOutputChannels().countNumberOfSetBits())
                           : 2;

    scratch.setSize (numChannels,
                     static_cast<int> (static_cast<double> (blockSize) * ratio * scratchOversize)
                         + scratchHeadroom);
    scratch.clear();

    interpolators.clear();

    for (int channel = 0; channel < numChannels; ++channel)
        interpolators.push_back (std::make_unique<juce::LagrangeInterpolator>());
}

void TransportPlayer::audioDeviceStopped()
{
    playing.store (false, std::memory_order_release);
}

void TransportPlayer::readSource (int firstSample, int numSamples)
{
    scratch.clear (0, numSamples);

    if (source == nullptr)
        return;

    const auto numAvailable = std::min (numSamples, source->getNumSamples() - firstSample);

    if (numAvailable <= 0)
        return;

    if (renderer != nullptr
        && monitoring.load (std::memory_order_acquire) == Monitor::corrected
        && renderer->isReady (firstSample, numAvailable)
        && renderer->read (scratch, 0, firstSample, numAvailable) == numAvailable)
        return;

    for (int channel = 0; channel < scratch.getNumChannels(); ++channel)
        scratch.copyFrom (channel, 0, *source,
                          std::min (channel, source->getNumChannels() - 1),
                          firstSample, numAvailable);
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

        for (auto& interpolator : interpolators)
            interpolator->reset();
    }

    if (! playing.load (std::memory_order_acquire))
        return;

    if (numOutputChannels <= 0 || outputChannelData[0] == nullptr)
        return;

    const auto ratio = sourceSampleRate / std::max (deviceSampleRate, 1.0);
    const auto numWanted = static_cast<int> (std::ceil (static_cast<double> (numSamples) * ratio))
                         + scratchHeadroom;

    if (scratch.getNumSamples() < numWanted || interpolators.empty())
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

            for (auto& interpolator : interpolators)
                interpolator->reset();
        }
    }

    if (readPosition >= source->getNumSamples())
    {
        playing.store (false, std::memory_order_release);
        return;
    }

    readSource (readPosition, numWanted);

    auto numUsed = 0;

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] == nullptr)
            continue;

        const auto sourceChannel = std::min (channel, scratch.getNumChannels() - 1);

        numUsed = interpolators[static_cast<std::size_t> (sourceChannel)]
                      ->process (ratio, scratch.getReadPointer (sourceChannel),
                                 outputChannelData[channel], numSamples);
    }

    readPosition += numUsed;

    position.store (static_cast<double> (readPosition) / sourceSampleRate, std::memory_order_release);
}
}
