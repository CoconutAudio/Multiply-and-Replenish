#include "ui/EditorView.h"

#include "ui/Flat.h"
#include "ui/PanelLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace multiplyandreplenish
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;

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
    : document (&documentToEdit),
      grid (documentToEdit)
{
    setOpaque (true);
    document->addListener (this);

    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (false, false);
    viewport.onScroll = [this] { repaint(); };
    viewport.onWheel = [this] (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
    {
        handleWheel (event.getEventRelativeTo (this), wheel);
    };
    addAndMakeVisible (viewport);
}

EditorView::~EditorView()
{
    document->removeListener (this);
}

void EditorView::setTabColour (juce::Colour colour)
{
    tabColour = colour;
    grid.setTabColour (colour);
    repaint();
}

void EditorView::recordingChanged()
{
    rebuildWaveform();
    resizeGrid();
    scrollToSinging();
    repaint();
}

void EditorView::setCaption (const juce::String& newCaption, bool isAlert)
{
    caption = newCaption;
    isCaptionAlert = isAlert;
    repaint();
}

void EditorView::setDocument (EditDocument& newDocument)
{
    if (document == &newDocument)
        return;

    document->removeListener (this);
    document = &newDocument;
    document->addListener (this);

    grid.setDocument (newDocument);
    rebuildWaveform();
    resized();
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

void EditorView::resizeGrid()
{
    const auto preferred = grid.getPreferredSize (viewport.getMaximumVisibleWidth(),
                                                  viewport.getMaximumVisibleHeight());
    grid.setSize (preferred.x, preferred.y);
}

void EditorView::zoomHorizontally (float factor, double anchorSeconds, int anchorOffset)
{
    grid.setZoom (grid.getPixelsPerSecond() * factor, grid.getRowHeight());
    resizeGrid();

    viewport.setViewPosition (juce::roundToInt (grid.getXForTime (anchorSeconds)) - anchorOffset,
                              viewport.getViewPositionY());
    repaint();
}

void EditorView::zoomVertically (float factor, int anchorOffset)
{
    const auto oldHeight = static_cast<float> (std::max (1, grid.getHeight()));
    const auto ratio = static_cast<float> (viewport.getViewPositionY() + anchorOffset) / oldHeight;

    grid.setZoom (grid.getPixelsPerSecond(), grid.getRowHeight() * factor);
    resizeGrid();

    viewport.setViewPosition (viewport.getViewPositionX(),
                              juce::roundToInt (ratio * static_cast<float> (grid.getHeight())) - anchorOffset);
    repaint();
}

void EditorView::scrollBy (float deltaX, float deltaY)
{
    viewport.setViewPosition (viewport.getViewPositionX() + juce::roundToInt (deltaX),
                              viewport.getViewPositionY() + juce::roundToInt (deltaY));
}

void EditorView::scrollToSeconds (double firstSecond)
{
    viewport.setViewPosition (juce::roundToInt (grid.getXForTime (std::max (0.0, firstSecond))),
                              viewport.getViewPositionY());
}

juce::Rectangle<int> EditorView::getOverviewBounds() const
{
    return getLocalBounds().removeFromBottom (waveformHeight);
}

double EditorView::getOverviewTime (float x) const
{
    const auto bounds = getOverviewBounds();

    return static_cast<double> ((x - static_cast<float> (bounds.getX())) / static_cast<float> (bounds.getWidth()))
         * document->getSeconds();
}

juce::Rectangle<float> EditorView::getOverviewViewBox() const
{
    const auto bounds = getOverviewBounds().toFloat();
    const auto seconds = document->getSeconds();

    if (seconds <= 0.0)
        return {};

    const auto toX = [&] (double time)
    {
        return bounds.getX() + static_cast<float> (time / seconds) * bounds.getWidth();
    };

    const auto first = grid.getTimeForX (static_cast<float> (viewport.getViewPositionX()));
    const auto last = grid.getTimeForX (static_cast<float> (viewport.getViewPositionX() + viewport.getViewWidth()));

    return juce::Rectangle<float> (toX (first), bounds.getY(), toX (last) - toX (first), bounds.getHeight())
        .reduced (0.0f, 2.0f);
}

void EditorView::handleWheel (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    constexpr auto scrollPixels = 300.0f;
    constexpr auto zoomSpeed = 1.5f;

    const auto zooms = event.mods.isCtrlDown() || event.mods.isCommandDown();
    const auto factor = std::exp (wheel.deltaY * zoomSpeed);
    const auto position = event.getPosition();

    const auto overview = getOverviewBounds();
    const auto gridArea = viewport.getBounds();
    const auto overRuler = position.y < gridArea.getY() && position.x >= gridArea.getX();

    if (overview.contains (position) || overRuler)
    {
        if (document->getSeconds() <= 0.0)
            return;

        if (zooms)
        {
            const auto centre = viewport.getViewWidth() / 2;

            zoomHorizontally (factor, grid.getTimeForX (static_cast<float> (viewport.getViewPositionX() + centre)),
                              centre);
            return;
        }

        const auto amount = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        scrollBy (-amount * scrollPixels, 0.0f);
        return;
    }

    if (zooms)
    {
        zoomVertically (factor, position.y - gridArea.getY());
        return;
    }

    if (event.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const auto amount = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        scrollBy (-amount * scrollPixels, 0.0f);
        return;
    }

    scrollBy (0.0f, -wheel.deltaY * scrollPixels);
}

void EditorView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    handleWheel (event, wheel);
}

void EditorView::mouseDown (const juce::MouseEvent& event)
{
    isDraggingOverview = false;

    if (! getOverviewBounds().contains (event.getPosition()) || document->getSeconds() <= 0.0)
        return;

    const auto box = getOverviewViewBox();
    const auto pointerTime = getOverviewTime (static_cast<float> (event.x));
    const auto firstSecond = grid.getTimeForX (static_cast<float> (viewport.getViewPositionX()));
    const auto viewSeconds = grid.getTimeForX (static_cast<float> (viewport.getViewWidth()));

    dragOffsetSeconds = box.contains (event.position) ? pointerTime - firstSecond : viewSeconds * 0.5;
    isDraggingOverview = true;

    scrollToSeconds (pointerTime - dragOffsetSeconds);
}

void EditorView::mouseDrag (const juce::MouseEvent& event)
{
    if (isDraggingOverview)
        scrollToSeconds (getOverviewTime (static_cast<float> (event.x)) - dragOffsetSeconds);
}

void EditorView::rebuildWaveform()
{
    waveform.assign (waveformResolution, 0.0f);

    const auto& recording = document->getRecording();
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

    bounds.removeFromBottom (waveformHeight);
    bounds.removeFromTop (rulerHeight);
    bounds.removeFromLeft (keyboardWidth);

    viewport.setBounds (bounds);
    resizeGrid();
}

void EditorView::paintKeyboard (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    // Keys scroll past both edges; keep them off the ruler and the waveform overview.
    const juce::Graphics::ScopedSaveState saveState { graphics };
    graphics.reduceClipRegion (bounds);

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

        graphics.setColour (Palette::dimText);
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
    const auto seconds = document->getSeconds();

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

    if (waveform.empty() || document->getSeconds() <= 0.0)
        return;

    const auto centre = static_cast<float> (bounds.getCentreY());
    const auto scale = static_cast<float> (bounds.getHeight()) * 0.45f;

    graphics.setColour (tabColour.withAlpha (0.35f));

    for (int x = bounds.getX(); x < bounds.getRight(); ++x)
    {
        const auto time = static_cast<double> (x - bounds.getX()) / static_cast<double> (bounds.getWidth())
                        * document->getSeconds();
        const auto pointIndex = juce::jlimit (0, static_cast<int> (waveform.size()) - 1,
                                              static_cast<int> (time / document->getSeconds()
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
             + static_cast<float> (time / document->getSeconds() * static_cast<double> (bounds.getWidth()));
    };

    const auto view = juce::Rectangle<float> (toX (viewFirst), static_cast<float> (bounds.getY()),
                                              toX (viewLast) - toX (viewFirst), static_cast<float> (bounds.getHeight()))
                          .reduced (0.0f, 2.0f);

    graphics.setColour (tabColour.withAlpha (0.14f));
    graphics.fillRect (view);
    graphics.setColour (tabColour.withAlpha (0.6f));
    graphics.drawRect (view, 1.0f);

    Flat::verticalLine (graphics, toX (playheadSeconds), static_cast<float> (bounds.getY()),
                        static_cast<float> (bounds.getBottom()), tabColour.brighter (0.6f), 1.0f);
}

void EditorView::setBusy (bool isBusy)
{
    if (busy == isBusy)
        return;

    busy = isBusy;

    if (busy)
        startTimerHz (60);
    else
        stopTimer();

    repaint();
}

void EditorView::timerCallback()
{
    spinnerAngle = std::fmod (spinnerAngle + 0.11f, juce::MathConstants<float>::twoPi);
    repaint (viewport.getBounds());
}

void EditorView::paintSpinner (juce::Graphics& graphics) const
{
    const auto area = viewport.getBounds().toFloat();

    graphics.setColour (Palette::ground.withAlpha (0.6f));
    graphics.fillRect (area);

    const auto popup = juce::Rectangle<float> { 84.0f, 84.0f }.withCentre (area.getCentre());
    Flat::panel (graphics, popup, Palette::bar, Palette::edge);

    const auto circle = popup.reduced (24.0f);

    juce::Path track;
    track.addEllipse (circle);
    graphics.setColour (Palette::edge);
    graphics.strokePath (track, juce::PathStrokeType (3.0f));

    juce::Path arc;
    arc.addCentredArc (circle.getCentreX(), circle.getCentreY(), circle.getWidth() * 0.5f, circle.getHeight() * 0.5f,
                       0.0f, spinnerAngle, spinnerAngle + juce::MathConstants<float>::pi * 0.6f, true);
    graphics.setColour (Palette::accent);
    graphics.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

    if (busy)
        paintSpinner (graphics);
}
}
