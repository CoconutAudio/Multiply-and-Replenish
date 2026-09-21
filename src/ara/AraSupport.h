#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace multiplyandreplenish
{
class PluginProcessor;

/** @brief Gives the host the audio to play for the region this processor took its take from.

    Until this has been called the host plays its own source audio, so the plug-in is transparent.
    Call it from the message thread whenever the rendered mix of the tabs has changed.

    @param mix         The mix of every tab, covering exactly the region that was opened.
    @param sampleRate  The rate @p mix is at.
*/
void publishMixToAra (PluginProcessor& processor, const juce::AudioBuffer<float>& mix, double sampleRate);
}
