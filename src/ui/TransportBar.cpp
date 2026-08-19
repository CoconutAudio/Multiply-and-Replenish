#include "ui/TransportBar.h"

#include "ui/PanelLookAndFeel.h"

#include <cmath>

namespace tuner
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;

    juce::String formatTime (double seconds)
    {
        const auto minutes = static_cast<int> (seconds) / 60;
        const auto remainder = seconds - 60.0 * minutes;

        return juce::String (minutes) + ":" + juce::String (remainder, 2).paddedLeft ('0', 5);
    }
}

TransportBar::TransportBar (TransportPlayer& playerToDrive)
    : player (playerToDrive)
{
    setOpaque (true);

    for (auto* button : { &playButton, &stopButton, &loopButton, &monitorButton, &keepUneditedButton })
        addAndMakeVisible (button);

    keepUneditedButton.setClickingTogglesState (true);
    keepUneditedButton.setToggleState (true, juce::dontSendNotification);
    keepUneditedButton.onClick = [this]
    {
        if (onKeepUneditedChanged != nullptr)
            onKeepUneditedChanged (keepUneditedButton.getToggleState());
    };

    playButton.onClick = [this]
    {
        if (player.isPlaying())
            player.stop();
        else
            player.start();
    };

    stopButton.onClick = [this]
    {
        player.stop();
        player.setPosition (player.isLooping() ? player.getLoopStart() : 0.0);

        if (onPositionChanged != nullptr)
            onPositionChanged (player.getPosition());
    };

    loopButton.setClickingTogglesState (true);
    loopButton.onClick = [this]
    {
        player.setLoop (player.getLoopStart(), player.getLoopEnd(), loopButton.getToggleState());
    };

    monitorButton.setClickingTogglesState (true);
    monitorButton.onClick = [this]
    {
        const auto isOriginal = monitorButton.getToggleState();

        player.setMonitor (isOriginal ? TransportPlayer::Monitor::original
                                      : TransportPlayer::Monitor::corrected);
        monitorButton.setButtonText (isOriginal ? "ORIGINAL" : "CORRECTED");
    };

    startTimerHz (30);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::setRenderState (double newFractionReady, const juce::String& newStatus, bool isAlert)
{
    fractionReady = newFractionReady;
    status = newStatus;
    isStatusAlert = isAlert;
    repaint();
}

void TransportBar::setLength (double seconds)
{
    lengthSeconds = seconds;
    repaint();
}

void TransportBar::timerCallback()
{
    playButton.setButtonText (player.isPlaying() ? "PAUSE" : "PLAY");

    if (onPositionChanged != nullptr)
        onPositionChanged (player.getPosition());

    repaint();
}

void TransportBar::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::bar);

    graphics.setColour (Palette::edge);
    graphics.fillRect (0, 0, getWidth(), 1);

    auto bounds = getLocalBounds().reduced (Metrics::margin, 0);
    bounds.removeFromLeft (420);

    const auto position = formatTime (player.getPosition()) + " / " + formatTime (lengthSeconds);

    PanelLookAndFeel::drawTrackedText (graphics, position, bounds.removeFromLeft (150).toFloat(),
                                       juce::Justification::left, TypeScale::value,
                                       Metrics::tracking, Palette::text);

    auto meterArea = bounds.removeFromLeft (160).reduced (0, 13).toFloat();

    graphics.setColour (Palette::well);
    graphics.fillRoundedRectangle (meterArea, 3.0f);

    graphics.setColour (Palette::accent.withAlpha (fractionReady >= 1.0 ? 0.9f : 0.6f));
    graphics.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth()
                                                        * static_cast<float> (fractionReady)), 3.0f);

    bounds.removeFromLeft (Metrics::gap);

    PanelLookAndFeel::drawTrackedText (graphics, status.toUpperCase(), bounds.toFloat(),
                                       juce::Justification::left, TypeScale::label,
                                       Metrics::tracking,
                                       isStatusAlert ? Palette::alert : Palette::dimText);
}

void TransportBar::resized()
{
    auto bounds = getLocalBounds().reduced (Metrics::margin, 5);

    const auto place = [&bounds] (juce::Component& component, int width)
    {
        component.setBounds (bounds.removeFromLeft (width));
        bounds.removeFromLeft (4);
    };

    place (playButton, 66);
    place (stopButton, 62);
    place (loopButton, 62);
    place (monitorButton, 96);
    place (keepUneditedButton, 118);
}
}
