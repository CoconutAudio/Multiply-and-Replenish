#pragma once

#include "ara/AudioModification.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace multiplyandreplenish
{
/** @brief Names a playback region: which modification it plays, and the stretch of it. */
[[nodiscard]] juce::String regionKey (const juce::ARAPlaybackRegion& region);

/** @brief Where a resampled read left off, so consecutive blocks join without a click. */
struct ResamplingState
{
    std::vector<juce::LagrangeInterpolator> interpolators;
    juce::int64 nextSourceSample { 0 };
    juce::int64 lastRenderedOutputEnd { std::numeric_limits<juce::int64>::lowest() };
    double ratio { 1.0 };
    bool initialised { false };
};

/** @brief Plays what the host's timeline holds, swapping in the edited audio where there is any. */
class PlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    using ARAPlaybackRenderer::ARAPlaybackRenderer;

    void prepareToPlay (double sampleRate,
                        int maximumBlockSize,
                        int numChannels,
                        juce::AudioProcessor::ProcessingPrecision,
                        AlwaysNonRealtime alwaysNonRealtime) override;
    void releaseResources() override;

    using juce::ARARenderer::processBlock;
    bool processBlock (juce::AudioBuffer<float>& buffer,
                       juce::AudioProcessor::Realtime realtime,
                       const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

private:
    std::map<juce::ARAAudioSource*, std::unique_ptr<juce::ARAAudioSourceReader>> readers;
    std::unordered_map<juce::ARAPlaybackRegion*, ResamplingState> sourceResampling;
    std::unordered_map<juce::ARAPlaybackRegion*, ResamplingState> renderedResampling;
    juce::AudioBuffer<float> scratchBuffer;
    double sampleRate { 44100.0 };
    int numChannels { 2 };
};

/** @brief The plug-in's side of an ARA document: it stores nothing of its own, since the
    processor's state already carries the project. */
class DocumentController final : public juce::ARADocumentControllerSpecialisation
{
public:
    using ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    /** @brief Reads all of an audio source's samples; false if the host has not allowed access. */
    static bool readAudioSource (juce::ARAAudioSource& source, juce::AudioBuffer<float>& destination);

protected:
    juce::ARAAudioModification* doCreateAudioModification (
        juce::ARAAudioSource* audioSource,
        ARA::ARAAudioModificationHostRef hostRef,
        const juce::ARAAudioModification* optionalModificationToClone) noexcept override;
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;

    bool doRestoreObjectsFromStream (juce::ARAInputStream&,
                                     const juce::ARARestoreObjectsFilter*) noexcept override { return true; }
    bool doStoreObjectsToStream (juce::ARAOutputStream&,
                                 const juce::ARAStoreObjectsFilter*) noexcept override { return true; }
};
}
