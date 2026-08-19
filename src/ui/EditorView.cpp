#include "ui/EditorView.h"

#include "ui/PanelLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace tuner
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;

    bool isBlackKey (int midiNote)
    {
        static const bool black[] = { false, true, false, true, false, false, true, false, true, false, true, false };
        return black[static_cast<std::size_t> (((midiNote % 12) + 12) % 12)];
    }

    double chooseLabelSpacing (float pixelsPerSecond)
    {
        static const double spacings[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0 };

        for (const auto spacing : spacings)
            if (spacing * static_cast<double> (pixelsPerSecond) >= 70.0)
                return spacing;

        return 60.0;
    }

    juce::String formatTime (double seconds)
    {
        const auto minutes = static_cast<int> (seconds) / 60;
        const auto remainder = seconds - 60.0 * minutes;

        return juce::String (minutes) + ":" + juce::String (remainder, remainder < 10.0 ? 2 : 1).paddedLeft ('0', 5);
    }
}

EditorView::EditorView (EditDocument& documentToEdit)
    : document (documentToEdit),
      grid (documentToEdit)
{
    setOpaque (true);
    document.addListener (this);

    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (true, true);
    viewport.setScrollBarThickness (scrollBarThickness);
    viewport.onScroll = [this] { repaint(); };
    addAndMakeVisible (viewport);

    const auto configureScaler = [this] (juce::Slider& slider, double minimum, double maximum, double value)
    {
        slider.setRange (minimum, maximum);
        slider.setValue (value, juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, value);
        slider.onValueChange = [this] { applyZoom(); };
        addAndMakeVisible (slider);
    };

    configureScaler (horizontalScaler, NoteGrid::minimumPixelsPerSecond, NoteGrid::maximumPixelsPerSecond,
                     grid.getPixelsPerSecond());
    configureScaler (verticalScaler, NoteGrid::minimumRowHeight, NoteGrid::maximumRowHeight,
                     grid.getRowHeight());
}

EditorView::~EditorView()
{
    document.removeListener (this);
}

void EditorView::recordingChanged()
{
    rebuildWaveform();
    applyZoom();
    scrollToSinging();
    repaint();
}

void EditorView::setCaption (const juce::String& newCaption, bool isAlert)
{
    caption = newCaption;
    isCaptionAlert = isAlert;
    repaint();
}

void EditorView::setLoopRange (double firstSecond, double lastSecond, bool shouldShow)
{
    grid.setLoopRange (firstSecond, lastSecond, shouldShow);
    repaint();
}

void EditorView::setPlayheadPosition (double seconds, bool shouldFollow)
{
    playheadSeconds = seconds;
    grid.setPlayheadPosition (seconds);

    if (shouldFollow)
    {
        const auto x = juce::roundToInt (grid.getXForTime (seconds));
        const auto area = viewport.getViewArea();

        if (x < area.getX() + 40 || x > area.getRight() - 80)
            viewport.setViewPosition (std::max (0, x - area.getWidth() / 3), area.getY());
    }

    repaint();
}

void EditorView::scrollToSinging()
{
    const auto centre = juce::roundToInt (grid.getYForPitch (static_cast<double> (grid.getCentreNote())));

    viewport.setViewPosition (0, std::max (0, centre - viewport.getViewHeight() / 2));
}

void EditorView::applyZoom()
{
    const auto anchorSeconds = grid.getTimeForX (static_cast<float> (viewport.getViewPositionX()));

    grid.setZoom (static_cast<float> (horizontalScaler.getValue()),
                  static_cast<float> (verticalScaler.getValue()));

    const auto preferred = grid.getPreferredSize (viewport.getMaximumVisibleWidth(),
                                                  viewport.getMaximumVisibleHeight());
    grid.setSize (preferred.x, preferred.y);

    viewport.setViewPosition (juce::roundToInt (grid.getXForTime (anchorSeconds)),
                              viewport.getViewPositionY());

    repaint();
}

void EditorView::rebuildWaveform()
{
    waveform.assign (waveformResolution, 0.0f);

    const auto& recording = document.getRecording();
    const auto numSamples = recording.getNumSamples();

    if (numSamples <= 0)
        return;

    const auto samplesPerPoint = std::max (1, numSamples / waveformResolution);

    auto loudest = 0.0f;

    for (int pointIndex = 0; pointIndex < waveformResolution; ++pointIndex)
    {
        const auto first = pointIndex * samplesPerPoint;
        const auto last = std::min (first + samplesPerPoint, numSamples);

        auto peak = 0.0f;

        for (int channel = 0; channel < recording.getNumChannels(); ++channel)
            for (int sampleIndex = first; sampleIndex < last; ++sampleIndex)
                peak = std::max (peak, std::abs (recording.getSample (channel, sampleIndex)));

        waveform[static_cast<std::size_t> (pointIndex)] = peak;
        loudest = std::max (loudest, peak);
    }

    if (loudest > 0.0f)
        for (auto& point : waveform)
            point /= loudest;
}

void EditorView::resized()
{
    auto bounds = getLocalBounds();

    bounds.removeFromBottom (scalerHeight);
    const auto waveformArea = bounds.removeFromBottom (waveformHeight);
    juce::ignoreUnused (waveformArea);

    auto rulerArea = bounds.removeFromTop (rulerHeight);
    rulerArea.removeFromLeft (keyboardWidth);

    auto gridArea = bounds;
    gridArea.removeFromLeft (keyboardWidth);

    viewport.setBounds (gridArea);

    const auto preferred = grid.getPreferredSize (viewport.getMaximumVisibleWidth(),
                                                  viewport.getMaximumVisibleHeight());
    grid.setSize (preferred.x, preferred.y);

    auto scalerArea = getLocalBounds().removeFromBottom (scalerHeight).reduced (Metrics::margin, 2);
    horizontalScaler.setBounds (scalerArea.removeFromLeft (140));
    scalerArea.removeFromLeft (Metrics::gap);
    verticalScaler.setBounds (scalerArea.removeFromLeft (110));
}

void EditorView::paintKeyboard (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    const auto scrollY = static_cast<float> (viewport.getViewPositionY());
    const auto rowHeight = grid.getRowHeight();

    for (int midiNote = NoteGrid::lowestNote; midiNote <= NoteGrid::highestNote; ++midiNote)
    {
        const auto centre = grid.getYForPitch (static_cast<double> (midiNote)) - scrollY
                          + static_cast<float> (bounds.getY());

        if (centre < static_cast<float> (bounds.getY()) - rowHeight
            || centre > static_cast<float> (bounds.getBottom()) + rowHeight)
            continue;

        const auto key = juce::Rectangle<float> (static_cast<float> (bounds.getX()),
                                                 centre - rowHeight * 0.5f,
                                                 static_cast<float> (bounds.getWidth()) - 1.0f,
                                                 rowHeight - 1.0f);

        graphics.setColour (isBlackKey (midiNote) ? Palette::blackKey : Palette::whiteKey);
        graphics.fillRect (key);

        if (midiNote % 12 != 0 || rowHeight < 9.0f)
            continue;

        graphics.setColour (Palette::ground);
        graphics.setFont (juce::FontOptions { std::min (rowHeight - 3.0f, 10.0f) });
        graphics.drawText (Scale::getNoteName (midiNote), key.reduced (3.0f, 0.0f),
                           juce::Justification::centredRight, false);
    }
}

void EditorView::paintRuler (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    const auto scrollX = static_cast<float> (viewport.getViewPositionX());
    const auto spacing = chooseLabelSpacing (grid.getPixelsPerSecond());
    const auto seconds = document.getSeconds();

    graphics.setFont (juce::FontOptions { TypeScale::label });

    for (auto time = 0.0; time <= seconds; time += spacing)
    {
        const auto x = grid.getXForTime (time) - scrollX + static_cast<float> (bounds.getX());

        if (x < static_cast<float> (bounds.getX()) || x > static_cast<float> (bounds.getRight()))
            continue;

        graphics.setColour (Palette::edge);
        graphics.fillRect (x, static_cast<float> (bounds.getBottom() - 5), 1.0f, 5.0f);

        graphics.setColour (Palette::dimText);
        graphics.drawText (formatTime (time),
                           juce::Rectangle<float> (x + 3.0f, static_cast<float> (bounds.getY()),
                                                   70.0f, static_cast<float> (bounds.getHeight())),
                           juce::Justification::centredLeft, false);
    }
}

void EditorView::paintWaveform (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::well);
    graphics.fillRect (bounds);

    if (waveform.empty() || document.getSeconds() <= 0.0)
        return;

    const auto centre = static_cast<float> (bounds.getCentreY());
    const auto scale = static_cast<float> (bounds.getHeight()) * 0.45f;

    graphics.setColour (Palette::silhouette);

    for (int x = bounds.getX(); x < bounds.getRight(); ++x)
    {
        const auto time = static_cast<double> (x - bounds.getX()) / static_cast<double> (bounds.getWidth())
                        * document.getSeconds();
        const auto pointIndex = juce::jlimit (0, static_cast<int> (waveform.size()) - 1,
                                              static_cast<int> (time / document.getSeconds()
                                                                * static_cast<double> (waveform.size())));

        const auto height = waveform[static_cast<std::size_t> (pointIndex)] * scale;

        graphics.fillRect (static_cast<float> (x), centre - height, 1.0f, height * 2.0f);
    }

    const auto viewFirst = grid.getTimeForX (static_cast<float> (viewport.getViewPositionX()));
    const auto viewLast = grid.getTimeForX (static_cast<float> (viewport.getViewPositionX()
                                                                + viewport.getViewWidth()));

    const auto toX = [&bounds, this] (double time)
    {
        return static_cast<float> (bounds.getX())
             + static_cast<float> (time / document.getSeconds() * static_cast<double> (bounds.getWidth()));
    };

    graphics.setColour (Palette::accent.withAlpha (0.14f));
    graphics.fillRect (toX (viewFirst), static_cast<float> (bounds.getY()),
                       toX (viewLast) - toX (viewFirst), static_cast<float> (bounds.getHeight()));

    graphics.setColour (Palette::accent);
    graphics.fillRect (toX (playheadSeconds), static_cast<float> (bounds.getY()),
                       1.0f, static_cast<float> (bounds.getHeight()));
}

void EditorView::paintCaption (juce::Graphics& graphics) const
{
    if (caption.isEmpty())
        return;

    auto area = viewport.getBounds().toFloat();

    graphics.setColour (Palette::ground.withAlpha (0.88f));
    graphics.fillRect (area);

    graphics.setColour (isCaptionAlert ? Palette::alert : Palette::dimText);
    graphics.setFont (juce::FontOptions { TypeScale::caption });
    graphics.drawFittedText (caption, area.reduced (40.0f).toNearestInt(),
                             juce::Justification::centred, 4);
}

void EditorView::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);

    auto bounds = getLocalBounds();

    bounds.removeFromBottom (scalerHeight);
    paintWaveform (graphics, bounds.removeFromBottom (waveformHeight));

    auto rulerArea = bounds.removeFromTop (rulerHeight);

    graphics.setColour (Palette::bar);
    graphics.fillRect (rulerArea.removeFromLeft (keyboardWidth));

    paintRuler (graphics, rulerArea);
    paintKeyboard (graphics, bounds.removeFromLeft (keyboardWidth));
}

void EditorView::paintOverChildren (juce::Graphics& graphics)
{
    paintCaption (graphics);
}
}
