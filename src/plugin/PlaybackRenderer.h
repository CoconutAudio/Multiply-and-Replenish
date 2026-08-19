#pragma once

#include "plugin/Modification.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace tuner
{
/** @brief Serves the corrected audio to the host, region by region.

    Anything not rendered yet is served as it was recorded, so a region is audible the moment the
    host asks for it and sharpens into the correction as the renderer catches up.
*/
class PlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    using juce::ARAPlaybackRenderer::ARAPlaybackRenderer;

    void prepareToPlay (double sampleRate,
                        int maximumSamplesPerBlock,
                        int numChannels,
                        juce::AudioProcessor::ProcessingPrecision precision,
                        AlwaysNonRealtime alwaysNonRealtime) override;

    void releaseResources() override;

    bool processBlock (juce::AudioBuffer<float>& buffer,
                       juce::AudioProcessor::Realtime realtime,
                       const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

    using juce::ARAPlaybackRenderer::processBlock;

private:
    juce::AudioBuffer<float> scratch;

    double hostSampleRate { 48000.0 };
    int maximumBlockSize { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaybackRenderer)
};
}
