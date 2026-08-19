#include "plugin/Processor.h"

#include "plugin/DocumentController.h"
#include "plugin/Editor.h"

namespace tuner
{
Processor::Processor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void Processor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    prepareToPlayForARA (sampleRate,
                         maximumExpectedSamplesPerBlock,
                         getMainBusNumOutputChannels(),
                         getProcessingPrecision());
}

void Processor::releaseResources()
{
    releaseResourcesForARA();
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    if (processBlockForARA (buffer, isRealtime(), getPlayHead()))
        return;

    // Not bound to ARA: the host is routing audio through an editor that has nothing to edit, so
    // the least surprising thing is to leave the audio alone.
}

DocumentController* Processor::getTunerDocumentController() const
{
    auto* araDocumentController = getDocumentController<ARA::PlugIn::DocumentController>();

    if (araDocumentController == nullptr)
        return nullptr;

    return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<DocumentController> (
        araDocumentController);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

void Processor::getStateInformation (juce::MemoryBlock& destination)
{
    // Everything an edit changes belongs to a region, and ARA stores it with the document.
    destination.reset();
}

void Processor::setStateInformation (const void*, int)
{
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tuner::Processor();
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<tuner::DocumentController>();
}
