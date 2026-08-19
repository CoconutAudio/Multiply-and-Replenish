#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tuner
{
/** @brief The plug-in itself.

    It processes nothing on its own: under ARA the host hands each region to the playback renderer,
    which serves the correction back. Loaded anywhere else it passes its input through and says so
    in the editor, because a pitch editor cannot work a block at a time.
*/
class Processor final : public juce::AudioProcessor,
                        public juce::AudioProcessorARAExtension
{
public:
    Processor();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tuner"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Tuner"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** @brief The ARA document controller, when the host has bound one. */
    [[nodiscard]] class DocumentController* getTunerDocumentController() const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
}
