#include "PluginProcessor.h"

#if JucePlugin_Enable_ARA

#include "ara/AraSupport.h"
#include "ara/DocumentController.h"

#include <map>

namespace multiplyandreplenish
{
namespace
{
    /** Which region each processor opened, by the key the playback renderer looks audio up by. */
    juce::CriticalSection openedRegionLock;
    std::map<const PluginProcessor*, juce::String> openedRegionKeys;

    juce::File getTempFileFor (const juce::String& key)
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("MultiplyAndReplenish")
            .getChildFile (juce::String::toHexString (key.hashCode64()) + ".wav");
    }

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, int firstSample, int numSamples, double sampleRate)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream() };
        juce::WavAudioFormat wav;

        auto writer = wav.createWriterFor (stream,
                                           juce::AudioFormatWriterOptions {}
                                               .withSampleRate (sampleRate)
                                               .withNumChannels (static_cast<unsigned int> (audio.getNumChannels()))
                                               .withBitsPerSample (24));

        if (writer == nullptr)
            return false;

        const bool written = writer->writeFromAudioSampleBuffer (audio, firstSample, numSamples);
        writer.reset();
        return written;
    }
}

bool PluginProcessor::isAraActive() const
{
    return isBoundToARA();
}

bool PluginProcessor::openFromAraSelection (const juce::ARAViewSelection& selection)
{
    const auto regions = selection.getPlaybackRegions<juce::ARAPlaybackRegion>();

    if (regions.empty() || regions.front() == nullptr)
        return false;

    auto* region = regions.front();

    if (region->getAudioModification() == nullptr)
        return false;

    auto* source = region->getAudioModification()->getAudioSource();

    if (source == nullptr || source->getSampleRate() <= 0.0)
        return false;

    juce::AudioBuffer<float> hostAudio;

    if (! DocumentController::readAudioSource (*source, hostAudio))
        return false;

    const int first = juce::jlimit (0, hostAudio.getNumSamples(),
                                    static_cast<int> (region->getStartInAudioModificationSamples()));
    const int length = juce::jmin (static_cast<int> (region->getDurationInAudioModificationSamples()),
                                   hostAudio.getNumSamples() - first);

    if (length <= 0)
        return false;

    const auto key = regionKey (*region);
    const auto file = getTempFileFor (key);

    if (! writeWav (file, hostAudio, first, length, source->getSampleRate()))
        return false;

    {
        const juce::ScopedLock lock (openedRegionLock);
        openedRegionKeys[this] = key;
    }

    openFile (file);
    return true;
}

void PluginProcessor::publishToAra()
{
    if (hasTake())
        publishMixToAra (*this, getMix(), getDocument (0).getSampleRate());
}

void publishMixToAra (PluginProcessor& processor, const juce::AudioBuffer<float>& mix, double sampleRate)
{
    juce::String key;

    {
        const juce::ScopedLock lock (openedRegionLock);
        const auto found = openedRegionKeys.find (&processor);

        if (found == openedRegionKeys.end())
            return;

        key = found->second;
    }

    auto* renderer = processor.getPlaybackRenderer();

    if (renderer == nullptr)
        return;

    for (auto* region : renderer->getPlaybackRegions<juce::ARAPlaybackRegion>())
    {
        if (region == nullptr || regionKey (*region) != key)
            continue;

        if (auto* modification = region->getAudioModification<AudioModification>())
            modification->setProcessedAudioForRegion (key, mix, sampleRate,
                                                      region->getStartInAudioModificationSamples());
    }
}

}

// JUCE looks the factory up by its global, unnamespaced name.
const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<multiplyandreplenish::DocumentController>();
}

#endif
