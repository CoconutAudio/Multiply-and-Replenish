#include "common/TransportPlayer.h"

#include <algorithm>
#include <cmath>

namespace multiplyandreplenish
{
namespace
{
    /** @brief Room for the resampler to read ahead, and for a host that hands out more than it
               promised in a callback.
    */
    constexpr int scratchHeadroom = 64;
    constexpr double scratchOversize = 4.0;
}

TransportPlayer::TransportPlayer() = default;
TransportPlayer::~TransportPlayer() = default;

void TransportPlayer::setRecording (const juce::AudioBuffer<float>* recording, double newSampleRate)
{
    stop();

    const juce::ScopedLock lock { sourceLock };

    source = recording;
    tabs.clear();
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

void TransportPlayer::setTabs (std::vector<RenderScheduler*> schedulers)
{
    const juce::ScopedLock lock { sourceLock };
    tabs = std::move (schedulers);
}

void TransportPlayer::prepare (double newHostSampleRate, int maximumBlockSize)
{
    hostSampleRate = newHostSampleRate > 0.0 ? newHostSampleRate : 44100.0;

    const auto ratio = sourceSampleRate / std::max (hostSampleRate, 1.0);
    constexpr auto numChannels = 2;

    const auto scratchSamples = static_cast<int> (static_cast<double> (maximumBlockSize) * ratio * scratchOversize)
                              + scratchHeadroom;

    scratch.setSize (numChannels, scratchSamples);
    scratch.clear();

    tabScratch.setSize (numChannels, scratchSamples);
    tabScratch.clear();

    interpolators.clear();

    for (int channel = 0; channel < numChannels; ++channel)
        interpolators.push_back (std::make_unique<juce::LagrangeInterpolator>());
}

void TransportPlayer::releaseResources()
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

    auto* main = tabs.empty() ? nullptr : tabs.front();

    if (! (main != nullptr
         && main->isReady (firstSample, numAvailable)
         && main->read (scratch, 0, firstSample, numAvailable) == numAvailable))
        for (int channel = 0; channel < scratch.getNumChannels(); ++channel)
            scratch.copyFrom (channel, 0, *source,
                              std::min (channel, source->getNumChannels() - 1),
                              firstSample, numAvailable);

    if (tabs.size() < 2 || tabScratch.getNumSamples() < numAvailable)
        return;

    for (std::size_t index = 1; index < tabs.size(); ++index)
    {
        auto* tab = tabs[index];

        if (tab == nullptr || ! tab->isReady (firstSample, numAvailable))
            continue;

        tabScratch.clear (0, numAvailable);

        if (tab->read (tabScratch, 0, firstSample, numAvailable) != numAvailable)
            continue;

        for (int channel = 0; channel < scratch.getNumChannels(); ++channel)
            scratch.addFrom (channel, 0, tabScratch,
                             std::min (channel, tabScratch.getNumChannels() - 1),
                             0, numAvailable);
    }
}

void TransportPlayer::renderBlock (float* const* outputChannelData, int numOutputChannels, int numSamples)
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

    const auto ratio = sourceSampleRate / std::max (hostSampleRate, 1.0);
    const auto numWanted = static_cast<int> (std::ceil (static_cast<double> (numSamples) * ratio))
                         + scratchHeadroom;

    if (scratch.getNumSamples() < numWanted || interpolators.empty())
        return;

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
