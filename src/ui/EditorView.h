#pragma once

#include "ui/NoteGrid.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tuner
{
/** @brief The editor proper: a keyboard and a ruler pinned around the scrolling note grid, with the
           waveform beneath it and a zoom for each axis.

    The keyboard, ruler and waveform are painted rather than made components of their own, offset by
    the viewport's scroll position, so each stays pinned to the axis it labels.
*/
class EditorView final : public juce::Component,
                         private EditDocument::Listener
{
public:
    explicit EditorView (EditDocument& document);
    ~EditorView() override;

    [[nodiscard]] NoteGrid& getGrid() noexcept { return grid; }

    /** @brief Moves the playhead, scrolling to keep it in view while playing. */
    void setPlayheadPosition (double seconds, bool shouldFollow);

    void setLoopRange (double firstSecond, double lastSecond, bool shouldShow);

    /** @brief Shows a line or two over the editor, as when nothing is open yet. */
    void setCaption (const juce::String& caption, bool isAlert);

    /** @brief Scrolls so that the singing is in the middle of the view. */
    void scrollToSinging();

    void paint (juce::Graphics& graphics) override;
    void paintOverChildren (juce::Graphics& graphics) override;
    void resized() override;

private:
    /** @brief A viewport that says when it scrolled, so the pinned lanes can follow it. */
    struct ScrollReportingViewport final : public juce::Viewport
    {
        std::function<void()> onScroll;

        void visibleAreaChanged (const juce::Rectangle<int>&) override
        {
            if (onScroll != nullptr)
                onScroll();
        }
    };

    static constexpr int keyboardWidth = 46;
    static constexpr int rulerHeight = 20;
    static constexpr int waveformHeight = 54;
    static constexpr int scalerHeight = 16;
    static constexpr int scrollBarThickness = 10;
    static constexpr int waveformResolution = 8192;

    void recordingChanged() override;

    void applyZoom();
    void rebuildWaveform();

    void paintKeyboard (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintRuler (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintWaveform (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintCaption (juce::Graphics& graphics) const;

    EditDocument& document;

    ScrollReportingViewport viewport;
    NoteGrid grid;

    juce::Slider horizontalScaler { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider verticalScaler { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    std::vector<float> waveform;

    double playheadSeconds { 0.0 };

    juce::String caption;
    bool isCaptionAlert { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorView)
};
}
