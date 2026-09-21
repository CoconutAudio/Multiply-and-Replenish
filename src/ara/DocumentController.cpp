#include "ara/DocumentController.h"

#include <algorithm>
#include <cmath>

namespace multiplyandreplenish
{
juce::String regionKey (const juce::ARAPlaybackRegion &region)
{
    const auto *modification = region.getAudioModification();
    const auto persistentID =
        modification != nullptr ? juce::String(modification->getPersistentID()) : juce::String();

    return persistentID + "@" +
           juce::String(region.getStartInAudioModificationSamples()) + ":" +
           juce::String(region.getDurationInAudioModificationSamples());
}

namespace
{

/** Prepares `state` to resume, or restarts it when the block is not contiguous. */
void primeResampling(ResamplingState &state, double ratio, int numChannels,
                     juce::int64 sourceStart, juce::int64 absoluteOutputStart)
{
    if (state.initialised && juce::approximatelyEqual(state.ratio, ratio) &&
        state.lastRenderedOutputEnd == absoluteOutputStart)
        return;

    state.interpolators.clear();
    state.interpolators.resize(static_cast<size_t>(std::max(1, numChannels)));
    state.nextSourceSample = sourceStart;
    state.lastRenderedOutputEnd = absoluteOutputStart;
    state.ratio = ratio;
    state.initialised = true;
}

/** The part of `region` that falls inside this block, in playback seconds. */
juce::Range<double> blockIntersection(const juce::ARAPlaybackRegion &region,
                                      juce::int64 blockStartSample, int numSamples,
                                      double outputSampleRate)
{
    const juce::Range<double> blockRange(
        static_cast<double>(blockStartSample) / outputSampleRate,
        static_cast<double>(blockStartSample + numSamples) / outputSampleRate);

    return juce::Range<double>(region.getStartInPlaybackTime(),
                               region.getEndInPlaybackTime())
        .getIntersectionWith(blockRange);
}

/** Reads a region's own source audio into the block, resampling if the rates differ. */
bool addSourceAudio(juce::ARAPlaybackRegion &region, juce::ARAAudioSourceReader &reader,
                    double outputSampleRate, juce::int64 blockStartSample,
                    juce::AudioBuffer<float> &buffer, ResamplingState &state)
{
    auto *source = region.getAudioModification()->getAudioSource();
    if (source == nullptr || source->getSampleRate() <= 0.0)
        return false;

    const auto intersection =
        blockIntersection(region, blockStartSample, buffer.getNumSamples(), outputSampleRate);
    if (intersection.isEmpty())
        return false;

    const double blockStartSeconds = static_cast<double>(blockStartSample) / outputSampleRate;
    const double sourceRate = source->getSampleRate();
    const auto sourceStart =
        region.getStartInAudioModificationSamples() +
        static_cast<juce::int64>(std::llround(
            (intersection.getStart() - region.getStartInPlaybackTime()) * sourceRate));
    const int outputStart = static_cast<int>(
        std::llround((intersection.getStart() - blockStartSeconds) * outputSampleRate));
    const int outputLength =
        std::min(buffer.getNumSamples() - outputStart,
                 static_cast<int>(std::llround(intersection.getLength() * outputSampleRate)));
    if (outputStart < 0 || outputLength <= 0)
        return false;

    const bool useRightChannel = source->getChannelCount() > 1;

    if (juce::approximatelyEqual(sourceRate, outputSampleRate))
        return reader.read(&buffer, outputStart, outputLength, sourceStart, true, useRightChannel);

    const double ratio = sourceRate / outputSampleRate;
    primeResampling(state, ratio, buffer.getNumChannels(), sourceStart,
                    blockStartSample + outputStart);

    const int sourceLength = std::max(1, static_cast<int>(std::ceil(outputLength * ratio)) + 16);
    juce::AudioBuffer<float> sourceBuffer(buffer.getNumChannels(), sourceLength);
    sourceBuffer.clear();
    if (!reader.read(&sourceBuffer, 0, sourceLength, state.nextSourceSample, true, useRightChannel))
        return false;

    int inputSamplesUsed = 0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const int used = state.interpolators[static_cast<size_t>(channel)].process(
            ratio,
            sourceBuffer.getReadPointer(std::min(channel, sourceBuffer.getNumChannels() - 1)),
            buffer.getWritePointer(channel, outputStart), outputLength);
        inputSamplesUsed = std::max(inputSamplesUsed, used);
    }

    state.nextSourceSample += inputSamplesUsed;
    state.lastRenderedOutputEnd = blockStartSample + outputStart + outputLength;
    return true;
}

/** Mixes a region's rendered audio into the block, resampling if the rates differ. */
bool addRenderedAudio(juce::AudioBuffer<float> &buffer, const juce::ARAPlaybackRegion &region,
                      const AudioModification::ProcessedRegionData &data,
                      double modificationSampleRate, double outputSampleRate,
                      juce::int64 blockStartSample, ResamplingState &state)
{
    if (data.sampleRate <= 0.0 || outputSampleRate <= 0.0 || data.audio.getNumSamples() <= 0)
        return false;

    const auto intersection =
        blockIntersection(region, blockStartSample, buffer.getNumSamples(), outputSampleRate);
    if (intersection.isEmpty())
        return false;

    // The rendered audio may start part-way into the modification, so the
    // region's own offset is measured against where the render begins.
    const double renderOffsetSeconds =
        modificationSampleRate > 0.0
            ? static_cast<double>(region.getStartInAudioModificationSamples() -
                                  data.startSampleInModification) /
                  modificationSampleRate
            : 0.0;

    const double blockStartSeconds = static_cast<double>(blockStartSample) / outputSampleRate;
    const auto sourceStart = static_cast<juce::int64>(std::llround(
        ((intersection.getStart() - region.getStartInPlaybackTime()) + renderOffsetSeconds) *
        data.sampleRate));
    const int outputStart = static_cast<int>(
        std::llround((intersection.getStart() - blockStartSeconds) * outputSampleRate));
    const int outputLength =
        std::min(buffer.getNumSamples() - outputStart,
                 static_cast<int>(std::llround(intersection.getLength() * outputSampleRate)));
    if (outputStart < 0 || outputLength <= 0 || sourceStart < 0 ||
        sourceStart >= data.audio.getNumSamples())
        return false;

    if (juce::approximatelyEqual(data.sampleRate, outputSampleRate))
    {
        const int samplesToCopy =
            std::min(outputLength, data.audio.getNumSamples() - static_cast<int>(sourceStart));
        if (samplesToCopy <= 0)
            return false;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addFrom(channel, outputStart, data.audio,
                           std::min(channel, data.audio.getNumChannels() - 1),
                           static_cast<int>(sourceStart), samplesToCopy);
        return true;
    }

    const double ratio = data.sampleRate / outputSampleRate;
    primeResampling(state, ratio, buffer.getNumChannels(), sourceStart,
                    blockStartSample + outputStart);
    if (state.nextSourceSample < 0 || state.nextSourceSample >= data.audio.getNumSamples())
        return false;

    const int sourceLength = std::max(1, static_cast<int>(std::ceil(outputLength * ratio)) + 16);
    const int available =
        std::min(sourceLength, data.audio.getNumSamples() - static_cast<int>(state.nextSourceSample));
    if (available <= 0)
        return false;

    juce::AudioBuffer<float> sourceBuffer(std::max(1, data.audio.getNumChannels()), sourceLength);
    sourceBuffer.clear();
    for (int channel = 0; channel < sourceBuffer.getNumChannels(); ++channel)
        sourceBuffer.copyFrom(channel, 0, data.audio,
                              std::min(channel, data.audio.getNumChannels() - 1),
                              static_cast<int>(state.nextSourceSample), available);

    juce::AudioBuffer<float> resampled(buffer.getNumChannels(), outputLength);
    resampled.clear();
    int inputSamplesUsed = 0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const int used = state.interpolators[static_cast<size_t>(channel)].process(
            ratio,
            sourceBuffer.getReadPointer(std::min(channel, sourceBuffer.getNumChannels() - 1)),
            resampled.getWritePointer(channel), outputLength);
        inputSamplesUsed = std::max(inputSamplesUsed, used);
    }

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.addFrom(channel, outputStart, resampled, channel, 0, outputLength);

    state.nextSourceSample += inputSamplesUsed;
    state.lastRenderedOutputEnd = blockStartSample + outputStart + outputLength;
    return true;
}

} // namespace

void PlaybackRenderer::prepareToPlay(double sampleRateIn, int maximumBlockSize,
                                     int numChannelsIn,
                                     juce::AudioProcessor::ProcessingPrecision,
                                     AlwaysNonRealtime)
{
    sampleRate = sampleRateIn;
    numChannels = numChannelsIn;
    scratchBuffer.setSize(numChannels, maximumBlockSize);
}

void PlaybackRenderer::releaseResources()
{
    readers.clear();
    sourceResampling.clear();
    renderedResampling.clear();
    scratchBuffer.setSize(0, 0);
}

bool PlaybackRenderer::processBlock(
    juce::AudioBuffer<float> &buffer, juce::AudioProcessor::Realtime,
    const juce::AudioPlayHead::PositionInfo &positionInfo) noexcept
{
    buffer.clear();

    const auto timeInSamples = positionInfo.getTimeInSamples();
    if (!positionInfo.getIsPlaying() || !timeInSamples.hasValue())
        return true;

    const int numSamples = buffer.getNumSamples();

    for (auto *region : getPlaybackRegions<juce::ARAPlaybackRegion>())
    {
        if (region == nullptr || region->getAudioModification() == nullptr)
            continue;

        bool rendered = false;

        if (auto *modification = region->getAudioModification<AudioModification>())
        {
            const auto lock = modification->tryLockProcessedAudio();
            if (lock.isLocked())
            {
                if (const auto *data = modification->getProcessedRegionData(regionKey(*region));
                    data != nullptr && data->hasAudio())
                {
                    auto *source = modification->getAudioSource();
                    rendered = addRenderedAudio(buffer, *region, *data,
                                                source != nullptr ? source->getSampleRate() : 0.0,
                                                sampleRate, *timeInSamples,
                                                renderedResampling[region]);
                }
            }
        }

        if (rendered)
            continue;

        auto *source = region->getAudioModification()->getAudioSource();
        if (source == nullptr)
            continue;

        auto reader = readers.find(source);
        if (reader == readers.end())
            reader = readers.emplace(source, std::make_unique<juce::ARAAudioSourceReader>(source))
                         .first;

        if (scratchBuffer.getNumSamples() < numSamples ||
            scratchBuffer.getNumChannels() < buffer.getNumChannels())
            scratchBuffer.setSize(buffer.getNumChannels(), numSamples, false, true, true);

        scratchBuffer.clear();
        if (!addSourceAudio(*region, *reader->second, sampleRate, *timeInSamples, scratchBuffer,
                            sourceResampling[region]))
            continue;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addFrom(channel, 0, scratchBuffer,
                           std::min(channel, scratchBuffer.getNumChannels() - 1), 0, numSamples);
    }

    return true;
}

bool DocumentController::readAudioSource(juce::ARAAudioSource &source,
                                         juce::AudioBuffer<float> &destination)
{
    const auto numSamples = source.getSampleCount();
    if (numSamples <= 0 || source.getChannelCount() <= 0)
        return false;

    juce::ARAAudioSourceReader reader(&source);
    destination.setSize(source.getChannelCount(), static_cast<int>(numSamples));
    destination.clear();
    return reader.read(&destination, 0, static_cast<int>(numSamples), 0, true,
                       source.getChannelCount() > 1);
}

juce::ARAAudioModification *DocumentController::doCreateAudioModification(
    juce::ARAAudioSource *audioSource, ARA::ARAAudioModificationHostRef hostRef,
    const juce::ARAAudioModification *optionalModificationToClone) noexcept
{
    return new AudioModification(audioSource, hostRef, optionalModificationToClone);
}

juce::ARAPlaybackRenderer *DocumentController::doCreatePlaybackRenderer() noexcept
{
    return new PlaybackRenderer(getDocumentController());
}
}
