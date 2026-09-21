#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>
#include <memory>

namespace multiplyandreplenish
{
/** @brief An ARA audio modification that can hold the edited audio the host should play.

    While a region has no edited audio the playback renderer plays the host's own source audio, so
    the plug-in is transparent until the first render has been published.
*/
class AudioModification final : public juce::ARAAudioModification
{
public:
    struct ProcessedRegionData
    {
        juce::AudioBuffer<float> audio;
        double sampleRate { 0.0 };
        juce::int64 startSampleInModification { 0 };

        [[nodiscard]] bool hasAudio() const { return audio.getNumSamples() > 0 && sampleRate > 0.0; }
    };

    AudioModification (juce::ARAAudioSource* audioSource,
                       ARA::ARAAudioModificationHostRef hostRef,
                       const juce::ARAAudioModification* optionalModificationToClone)
        : juce::ARAAudioModification (audioSource, hostRef, optionalModificationToClone)
    {
        if (const auto* other = dynamic_cast<const AudioModification*> (optionalModificationToClone))
        {
            const juce::SpinLock::ScopedLockType lock (other->processedAudioLock);

            for (const auto& [regionID, data] : other->processedRegions)
            {
                if (data == nullptr || ! data->hasAudio())
                    continue;

                auto copy = std::make_unique<ProcessedRegionData>();
                copy->audio.makeCopyOf (data->audio);
                copy->sampleRate = data->sampleRate;
                copy->startSampleInModification = data->startSampleInModification;
                processedRegions[regionID] = std::move (copy);
            }
        }
    }

    void setProcessedAudioForRegion (const juce::String& regionID,
                                     const juce::AudioBuffer<float>& buffer,
                                     double sampleRate,
                                     juce::int64 startSampleInModification)
    {
        auto data = std::make_unique<ProcessedRegionData>();
        data->audio.makeCopyOf (buffer);
        data->sampleRate = sampleRate;
        data->startSampleInModification = startSampleInModification;

        const juce::SpinLock::ScopedLockType lock (processedAudioLock);
        processedRegions[regionID] = std::move (data);
    }

    void clearProcessedAudio()
    {
        const juce::SpinLock::ScopedLockType lock (processedAudioLock);
        processedRegions.clear();
    }

    /** @brief For the audio thread: held only if the message thread is not swapping audio in. */
    [[nodiscard]] juce::SpinLock::ScopedTryLockType tryLockProcessedAudio() const
    {
        return juce::SpinLock::ScopedTryLockType (processedAudioLock);
    }

    /** @brief Call with the lock from @ref tryLockProcessedAudio held. */
    [[nodiscard]] const ProcessedRegionData* getProcessedRegionData (const juce::String& regionID) const noexcept
    {
        if (const auto it = processedRegions.find (regionID); it != processedRegions.end())
            return it->second.get();

        return nullptr;
    }

private:
    mutable juce::SpinLock processedAudioLock;
    std::map<juce::String, std::unique_ptr<ProcessedRegionData>> processedRegions;
};
}
