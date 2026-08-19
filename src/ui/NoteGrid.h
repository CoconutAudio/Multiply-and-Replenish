#pragma once

#include "edit/EditDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace tuner
{
/** @brief The note grid: a row per semitone, the melody drawn across it, and the notes on top.

    This is where the editing happens. The pitch as sung is drawn behind the pitch as corrected, so
    that every edit shows what it did to the take, and each note is a block on the row it will be
    sung on rather than the row it was sung on.
*/
class NoteGrid final : public juce::Component,
                       private EditDocument::Listener
{
public:
    explicit NoteGrid (EditDocument& document);
    ~NoteGrid() override;

    /** @brief What the mouse does on the grid. */
    enum class Tool
    {
        select,
        draw,
        split,
        join
    };

    /** @brief The rows the grid holds, which is the piano's range rather than a singer's: an
               instrument is as likely to be a cello as a soprano.
    */
    static constexpr int lowestNote = 21;
    static constexpr int highestNote = 108;
    static constexpr int numRows = highestNote - lowestNote + 1;

    static constexpr float minimumPixelsPerSecond = 20.0f;
    static constexpr float maximumPixelsPerSecond = 600.0f;
    static constexpr float minimumRowHeight = 4.0f;
    static constexpr float maximumRowHeight = 40.0f;

    void setTool (Tool tool);
    [[nodiscard]] Tool getTool() const noexcept { return tool; }

    void setZoom (float pixelsPerSecond, float rowHeight);

    [[nodiscard]] float getPixelsPerSecond() const noexcept { return pixelsPerSecond; }
    [[nodiscard]] float getRowHeight() const noexcept { return rowHeight; }

    /** @brief The size the grid wants at this zoom, given the space the viewport can give it. */
    [[nodiscard]] juce::Point<int> getPreferredSize (int minimumWidth, int minimumHeight) const;

    /** @brief The vertical centre of a note's row. */
    [[nodiscard]] float getYForPitch (double midiPitch) const;

    /** @brief The pitch a vertical position falls on. */
    [[nodiscard]] double getPitchForY (float y) const;

    [[nodiscard]] float getXForTime (double seconds) const;
    [[nodiscard]] double getTimeForX (float x) const;

    /** @brief The note the sung range sits in the middle of, for scrolling to the singing. */
    [[nodiscard]] int getCentreNote() const;

    /** @brief Where the playhead is drawn, in seconds. */
    void setPlayheadPosition (double seconds);

    /** @brief Shows the stretch that plays on repeat, or nothing when @p shouldShow is false. */
    void setLoopRange (double firstSecond, double lastSecond, bool shouldShow);

    /** @brief Called when the user clicks somewhere that should move the playhead. */
    std::function<void (double)> onPositionClicked;

    /** @brief Called when the user drags out a stretch to play on repeat. */
    std::function<void (double, double)> onLoopDragged;

    void paint (juce::Graphics& graphics) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;

    bool keyPressed (const juce::KeyPress& key) override;

private:
    void recordingChanged() override;
    void melodyChanged() override;
    void selectionChanged() override;

    /** @brief What a drag is in the middle of doing. */
    enum class Drag
    {
        none,
        moveNotes,
        rubberBand,
        drawPitch,
        loopRange
    };

    void paintRows (juce::Graphics& graphics) const;
    void paintTimeGrid (juce::Graphics& graphics) const;
    void paintCurve (juce::Graphics& graphics, const std::vector<float>& semitones,
                     juce::Colour colour, float thickness) const;
    void paintNotes (juce::Graphics& graphics) const;
    void paintPlayhead (juce::Graphics& graphics) const;
    void paintLoop (juce::Graphics& graphics) const;

    [[nodiscard]] int getNoteIndexAt (juce::Point<float> position) const;
    [[nodiscard]] int getFrameForX (float x) const;
    [[nodiscard]] std::vector<int> getSelectedIndices() const;
    [[nodiscard]] juce::Rectangle<float> getNoteBounds (const Note& note) const;

    void showMenuFor (int noteIndex);

    EditDocument& document;

    Tool tool { Tool::select };

    float pixelsPerSecond { 110.0f };
    float rowHeight { 14.0f };

    Drag drag { Drag::none };
    int dragNoteIndex { -1 };
    int dragSemitones { 0 };
    juce::Point<float> dragOrigin;

    int drawFirstFrame { 0 };
    std::vector<float> drawnSpan;

    double playheadSeconds { 0.0 };
    double loopFirstSecond { 0.0 };
    double loopLastSecond { 0.0 };
    bool showLoop { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteGrid)
};
}
