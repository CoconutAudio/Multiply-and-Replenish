#pragma once

#include "audio/TransportPlayer.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvctuner
{
/** @brief The strip along the bottom: playing, looping, what is being heard, and how much of the
           correction has been rendered.
*/
class TransportBar final : public juce::Component,
                           private juce::Timer
{
public:
    explicit TransportBar (TransportPlayer& player);
    ~TransportBar() override;

    static constexpr int preferredHeight = 34;

    /** @brief Called every timer tick with the playhead, so the editor can follow it. */
    std::function<void (double)> onPositionChanged;

    /** @brief Called when the listener asks for untouched stretches to play as recorded. */
    std::function<void (bool)> onKeepUneditedChanged;

    /** @brief Sets what the fraction bar shows and what the status line says. */
    void setRenderState (double fractionReady, const juce::String& status, bool isAlert);

    /** @brief The length of the recording, for the position readout. */
    void setLength (double seconds);

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;

    TransportPlayer& player;

    juce::TextButton playButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton monitorButton { "CORRECTED" };
    juce::TextButton keepUneditedButton { "KEEP UNEDITED" };

    double fractionReady { 0.0 };
    double lengthSeconds { 0.0 };
    juce::String status;
    bool isStatusAlert { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
}
