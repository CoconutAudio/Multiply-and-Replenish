#pragma once

#include "ui/NoteGrid.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace multiplyandreplenish
{
/** @brief The editor proper: a keyboard and a ruler pinned around the scrolling note grid, with the
           waveform overview beneath it. There are no scroll bars or zoom sliders: the wheel scrolls, with Ctrl it zooms, and the overview's box is dragged.

    The keyboard, ruler and waveform are painted rather than made components of their own, offset by
    the viewport's scroll position, so each stays pinned to the axis it labels.
*/
class EditorView final : public juce::Component,
                         private EditDocument::Listener,
                         private juce::Timer
{
public:
    explicit EditorView (EditDocument& document);
    ~EditorView() override;

    [[nodiscard]] NoteGrid& getGrid() noexcept { return grid; }

    /** @brief Moves the playhead, scrolling to keep it in view while playing. */
    void setPlayheadPosition (double seconds, bool shouldFollow);

    /** @brief Shows another document (another tab), keeping the zoom and scroll as they are. */
    void setDocument (EditDocument& newDocument);

    /** @brief The active tab's hue, for its notes, curve and the waveform overview. */
    void setTabColour (juce::Colour colour);

    /** @brief Shows why the take could not be analysed, or clears the message when empty. */
    void setCaption (const juce::String& caption, bool isAlert);

    /** @brief Shows a spinning circle over the editor while the take is being analysed. */
    void setBusy (bool isBusy);

    /** @brief Scrolls so that the singing is in the middle of the view. */
    void scrollToSinging();

    void paint (juce::Graphics& graphics) override;
    void paintOverChildren (juce::Graphics& graphics) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    /** @brief A viewport that says when it scrolled, so the pinned lanes can follow it. */
    struct ScrollReportingViewport final : public juce::Viewport
    {
        std::function<void()> onScroll;
        std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

        void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
        {
            if (onWheel != nullptr)
                onWheel (event, wheel);
        }

        void visibleAreaChanged (const juce::Rectangle<int>&) override
        {
            if (onScroll != nullptr)
                onScroll();
        }
    };

    static constexpr int keyboardWidth = 46;
    static constexpr int rulerHeight = 20;
    static constexpr int waveformHeight = 64;
    static constexpr int waveformResolution = 8192;

    void recordingChanged() override;

    void resizeGrid();
    void zoomHorizontally (float factor, double anchorSeconds, int anchorOffset);
    void zoomVertically (float factor, int anchorOffset);
    void scrollBy (float deltaX, float deltaY);
    void scrollToSeconds (double firstSecond);

    /** @brief The wheel over the grid, the keyboard, the ruler or the overview. */
    void handleWheel (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel);

    [[nodiscard]] juce::Rectangle<int> getOverviewBounds() const;
    [[nodiscard]] juce::Rectangle<float> getOverviewViewBox() const;
    [[nodiscard]] double getOverviewTime (float x) const;
    void rebuildWaveform();

    void paintKeyboard (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintRuler (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintWaveform (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintCaption (juce::Graphics& graphics) const;
    void paintSpinner (juce::Graphics& graphics) const;
    void timerCallback() override;

    EditDocument* document;

    ScrollReportingViewport viewport;
    NoteGrid grid;

    std::vector<float> waveform;
    juce::Colour tabColour { 0xffb08cff };

    double playheadSeconds { 0.0 };

    /** @brief Where inside the overview's box the pointer took hold, in seconds. */
    double dragOffsetSeconds { 0.0 };
    bool isDraggingOverview { false };

    juce::String caption;
    bool isCaptionAlert { false };

    bool busy { false };
    float spinnerAngle { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorView)
};
}
