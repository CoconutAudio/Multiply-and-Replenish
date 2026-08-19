#include "plugin/PlaybackRenderer.h"

#include <algorithm>
#include <cmath>

namespace tuner
{
void PlaybackRenderer::prepareToPlay (double sampleRate,
                                      int maximumSamplesPerBlock,
                                      int numChannels,
                                      juce::AudioProcessor::ProcessingPrecision,
                                      AlwaysNonRealtime)
{
    hostSampleRate = sampleRate;
    maximumBlockSize = maximumSamplesPerBlock;

    scratch.setSize (std::max (1, numChannels), std::max (1, maximumSamplesPerBlock));
    scratch.clear();
}

void PlaybackRenderer::releaseResources()
{
    scratch.setSize (0, 0);
}

bool PlaybackRenderer::processBlock (juce::AudioBuffer<float>& buffer,
                                     juce::AudioProcessor::Realtime,
                                     const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    buffer.clear();

    const auto numSamples = buffer.getNumSamples();
    const auto timeInSamples = positionInfo.getTimeInSamples();

    if (! timeInSamples.hasValue() || ! positionInfo.getIsPlaying())
        return true;

    const auto blockStart = static_cast<juce::int64> (*timeInSamples);
    const auto blockEnd = blockStart + static_cast<juce::int64> (numSamples);

    for (const auto& playbackRegion : getPlaybackRegions())
    {
        auto* modification = dynamic_cast<Modification*> (playbackRegion->getAudioModification());

        if (modification == nullptr || modification->getState() != Modification::State::ready)
            continue;

        const auto regionStart = static_cast<juce::int64> (
            std::llround (playbackRegion->getStartInPlaybackTime() * hostSampleRate));
        const auto regionEnd = static_cast<juce::int64> (
            std::llround (playbackRegion->getEndInPlaybackTime() * hostSampleRate));

        const auto from = std::max (blockStart, regionStart);
        const auto to = std::min (blockEnd, regionEnd);

        if (to <= from)
            continue;

        const auto numToRead = static_cast<int> (to - from);
        const auto startInBuffer = static_cast<int> (from - blockStart);

        // Where the region sits in its own audio, which is what both the renderer and the
        // recording are indexed by.
        const auto startInModification = static_cast<int> (
            std::llround ((playbackRegion->getStartInAudioModificationTime()
                           + static_cast<double> (from - regionStart) / hostSampleRate)
                          * modification->getSampleRate()));

        auto& scheduler = modification->getScheduler();
        auto& document = modification->getDocument();

        scheduler.setPriorityFrame (document.getFrameForTime (
            static_cast<double> (startInModification) / std::max (modification->getSampleRate(), 1.0)));

        if (scratch.getNumSamples() < numToRead || scratch.getNumChannels() < buffer.getNumChannels())
            scratch.setSize (buffer.getNumChannels(), std::max (numToRead, maximumBlockSize), false, false, true);

        scratch.clear (0, numToRead);

        const auto& recording = document.getRecording();

        if (! scheduler.isReady (startInModification, numToRead)
            || scheduler.read (scratch, 0, startInModification, numToRead) != numToRead)
        {
            for (int channel = 0; channel < scratch.getNumChannels(); ++channel)
            {
                const auto sourceChannel = std::min (channel, recording.getNumChannels() - 1);

                if (sourceChannel < 0 || startInModification >= recording.getNumSamples())
                    continue;

                const auto numAvailable = std::min (numToRead, recording.getNumSamples() - startInModification);

                if (numAvailable > 0)
                    scratch.copyFrom (channel, 0, recording, sourceChannel, startInModification, numAvailable);
            }
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addFrom (channel, startInBuffer, scratch,
                            std::min (channel, scratch.getNumChannels() - 1), 0, numToRead);
    }

    return true;
}
}
