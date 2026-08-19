#include "ui/NoteGrid.h"

#include "ui/PanelLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace rvctuner
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;

    bool isBlackKey (int midiNote)
    {
        static const bool black[] = { false, true, false, true, false, false, true, false, true, false, true, false };
        return black[static_cast<std::size_t> (((midiNote % 12) + 12) % 12)];
    }

    /** @brief The spacing of the time grid, in seconds, at a given zoom. */
    double chooseGridSpacing (float pixelsPerSecond)
    {
        static const double spacings[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0 };

        for (const auto spacing : spacings)
            if (spacing * static_cast<double> (pixelsPerSecond) >= 70.0)
                return spacing;

        return 60.0;
    }
}

NoteGrid::NoteGrid (EditDocument& documentToEdit)
    : document (documentToEdit)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    document.addListener (this);
}

NoteGrid::~NoteGrid()
{
    document.removeListener (this);
}

void NoteGrid::setTool (Tool newTool)
{
    if (tool == newTool)
        return;

    tool = newTool;

    setMouseCursor (tool == Tool::draw ? juce::MouseCursor::CrosshairCursor
                                       : juce::MouseCursor::NormalCursor);
    repaint();
}

void NoteGrid::setZoom (float newPixelsPerSecond, float newRowHeight)
{
    pixelsPerSecond = juce::jlimit (minimumPixelsPerSecond, maximumPixelsPerSecond, newPixelsPerSecond);
    rowHeight = juce::jlimit (minimumRowHeight, maximumRowHeight, newRowHeight);
    repaint();
}

juce::Point<int> NoteGrid::getPreferredSize (int minimumWidth, int minimumHeight) const
{
    const auto width = juce::roundToInt (document.getSeconds() * static_cast<double> (pixelsPerSecond));

    return { std::max (width, minimumWidth),
             std::max (juce::roundToInt (rowHeight * static_cast<float> (numRows)), minimumHeight) };
}

float NoteGrid::getYForPitch (double midiPitch) const
{
    return static_cast<float> (getHeight())
         - static_cast<float> (midiPitch - static_cast<double> (lowestNote) + 0.5) * rowHeight;
}

double NoteGrid::getPitchForY (float y) const
{
    return static_cast<double> (lowestNote)
         + static_cast<double> ((static_cast<float> (getHeight()) - y) / rowHeight) - 0.5;
}

float NoteGrid::getXForTime (double seconds) const
{
    return static_cast<float> (seconds * static_cast<double> (pixelsPerSecond));
}

double NoteGrid::getTimeForX (float x) const
{
    return static_cast<double> (x) / static_cast<double> (pixelsPerSecond);
}

int NoteGrid::getFrameForX (float x) const
{
    return juce::jlimit (0, std::max (0, document.getNumFrames() - 1),
                         document.getFrameForTime (getTimeForX (x)));
}

int NoteGrid::getCentreNote() const
{
    const auto& melody = document.getSungMelody();

    auto lowest = 72;
    auto highest = 48;

    for (int frameIndex = 0; frameIndex < melody.getNumFrames(); ++frameIndex)
    {
        if (! melody.isVoiced (frameIndex))
            continue;

        const auto midiNote = juce::jlimit (lowestNote, highestNote,
            juce::roundToInt (69.0 + 12.0 * std::log2 (melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] / 440.0)));

        lowest = std::min (lowest, midiNote);
        highest = std::max (highest, midiNote);
    }

    return highest >= lowest ? (lowest + highest) / 2 : 60;
}

void NoteGrid::setPlayheadPosition (double seconds)
{
    if (std::abs (seconds - playheadSeconds) < 0.005)
        return;

    playheadSeconds = seconds;
    repaint();
}

void NoteGrid::setLoopRange (double firstSecond, double lastSecond, bool shouldShow)
{
    loopFirstSecond = firstSecond;
    loopLastSecond = lastSecond;
    showLoop = shouldShow;
    repaint();
}

void NoteGrid::recordingChanged() { repaint(); }
void NoteGrid::melodyChanged() { repaint(); }
void NoteGrid::selectionChanged() { repaint(); }

juce::Rectangle<float> NoteGrid::getNoteBounds (const Note& note) const
{
    const auto first = getXForTime (document.getTimeForFrame (note.firstFrame));
    const auto last = getXForTime (document.getTimeForFrame (note.lastFrame));
    const auto centre = getYForPitch (static_cast<double> (note.targetNote));

    return { first, centre - rowHeight * 0.5f, std::max (last - first, 2.0f), rowHeight };
}

int NoteGrid::getNoteIndexAt (juce::Point<float> position) const
{
    const auto& notes = document.getNotes();

    for (int index = 0; index < static_cast<int> (notes.size()); ++index)
        if (getNoteBounds (notes[static_cast<std::size_t> (index)]).expanded (0.0f, 2.0f).contains (position))
            return index;

    return -1;
}

std::vector<int> NoteGrid::getSelectedIndices() const
{
    std::vector<int> indices;

    const auto& selection = document.getSelection();

    for (int range = 0; range < selection.getNumRanges(); ++range)
        for (auto index = selection.getRange (range).getStart(); index < selection.getRange (range).getEnd(); ++index)
            indices.push_back (index);

    return indices;
}

void NoteGrid::paintRows (juce::Graphics& graphics) const
{
    for (int midiNote = lowestNote; midiNote <= highestNote; ++midiNote)
    {
        const auto centre = getYForPitch (static_cast<double> (midiNote));
        const auto row = juce::Rectangle<float> (0.0f, centre - rowHeight * 0.5f,
                                                 static_cast<float> (getWidth()), rowHeight);

        if (isBlackKey (midiNote))
        {
            graphics.setColour (Palette::blackKeyRow);
            graphics.fillRect (row);
        }

        if (midiNote % 12 == 0)
        {
            graphics.setColour (Palette::edge);
            graphics.fillRect (0.0f, row.getBottom() - 1.0f, static_cast<float> (getWidth()), 1.0f);
        }
        else
        {
            graphics.setColour (Palette::rule);
            graphics.fillRect (0.0f, row.getBottom() - 1.0f, static_cast<float> (getWidth()), 0.5f);
        }

        if (! document.getScale().contains (midiNote))
            continue;

        graphics.setColour (Palette::accent.withAlpha (0.05f));
        graphics.fillRect (row.withWidth (3.0f));
    }
}

void NoteGrid::paintTimeGrid (juce::Graphics& graphics) const
{
    const auto spacing = chooseGridSpacing (pixelsPerSecond);
    const auto seconds = document.getSeconds();

    graphics.setColour (Palette::rule);

    for (auto time = 0.0; time <= seconds; time += spacing)
        graphics.fillRect (getXForTime (time), 0.0f, 1.0f, static_cast<float> (getHeight()));
}

void NoteGrid::paintCurve (juce::Graphics& graphics,
                           const std::vector<float>& semitones,
                           juce::Colour colour,
                           float thickness) const
{
    if (semitones.empty())
        return;

    const auto& melody = document.getSungMelody();

    juce::Path path;
    auto isDrawing = false;

    for (int frameIndex = 0; frameIndex < static_cast<int> (semitones.size()); ++frameIndex)
    {
        const auto pitch = semitones[static_cast<std::size_t> (frameIndex)];

        if (! melody.isVoiced (frameIndex) || pitch <= 0.0f)
        {
            isDrawing = false;
            continue;
        }

        const auto x = getXForTime (document.getTimeForFrame (frameIndex));
        const auto y = getYForPitch (static_cast<double> (pitch));

        if (! isDrawing)
        {
            path.startNewSubPath (x, y);
            isDrawing = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }

    graphics.setColour (colour);
    graphics.strokePath (path, juce::PathStrokeType (thickness));
}

void NoteGrid::paintNotes (juce::Graphics& graphics) const
{
    const auto& notes = document.getNotes();
    const auto& selection = document.getSelection();

    for (int index = 0; index < static_cast<int> (notes.size()); ++index)
    {
        const auto& note = notes[static_cast<std::size_t> (index)];

        auto bounds = getNoteBounds (note);

        if (drag == Drag::moveNotes && dragSemitones != 0 && selection.contains (index))
            bounds = bounds.withY (bounds.getY() - static_cast<float> (dragSemitones) * rowHeight);

        const auto isSelected = selection.contains (index);
        const auto isMoved = std::abs (note.getError()) > 0.02;

        // Where the note was sung, so that every correction shows what it moved.
        if (isMoved && note.isEnabled)
        {
            const auto sung = bounds.withY (getYForPitch (note.sungPitch) - rowHeight * 0.5f);

            graphics.setColour (Palette::silhouette.withAlpha (0.5f));
            graphics.drawRoundedRectangle (sung.reduced (0.5f), 2.0f, 0.7f);
        }

        graphics.setColour (note.isEnabled ? Palette::noteBlock.withAlpha (0.85f)
                                           : Palette::silhouette.withAlpha (0.6f));
        graphics.fillRoundedRectangle (bounds, 2.0f);

        graphics.setColour (isSelected ? Palette::accent
                                       : (note.isEnabled ? Palette::accent.withAlpha (0.45f) : Palette::edge));
        graphics.drawRoundedRectangle (bounds.reduced (0.5f), 2.0f, isSelected ? 1.8f : 1.0f);

        if (bounds.getWidth() < 30.0f || rowHeight < 11.0f)
            continue;

        graphics.setColour (isSelected ? Palette::text : Palette::dimText);
        graphics.setFont (juce::FontOptions { std::min (rowHeight - 4.0f, 11.0f) });
        graphics.drawText (Scale::getNoteName (note.targetNote),
                           bounds.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
    }
}

void NoteGrid::paintLoop (juce::Graphics& graphics) const
{
    if (! showLoop || loopLastSecond <= loopFirstSecond)
        return;

    const auto first = getXForTime (loopFirstSecond);
    const auto last = getXForTime (loopLastSecond);

    graphics.setColour (Palette::accent.withAlpha (0.08f));
    graphics.fillRect (first, 0.0f, last - first, static_cast<float> (getHeight()));
}

void NoteGrid::paintPlayhead (juce::Graphics& graphics) const
{
    graphics.setColour (Palette::accent);
    graphics.fillRect (getXForTime (playheadSeconds), 0.0f, 1.5f, static_cast<float> (getHeight()));
}

void NoteGrid::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::well);

    paintRows (graphics);
    paintTimeGrid (graphics);
    paintLoop (graphics);

    paintCurve (graphics, document.getCorrectedMelody().sungSemitones, Palette::silhouette.brighter (0.4f), 1.4f);
    paintNotes (graphics);
    paintCurve (graphics, document.getCorrectedMelody().correctedSemitones, Palette::accent, 1.8f);

    if (drag == Drag::rubberBand)
    {
        const auto area = juce::Rectangle<float> (dragOrigin, getMouseXYRelative().toFloat());

        graphics.setColour (Palette::accent.withAlpha (0.12f));
        graphics.fillRect (area);
        graphics.setColour (Palette::accent.withAlpha (0.5f));
        graphics.drawRect (area, 1.0f);
    }

    paintPlayhead (graphics);
}

void NoteGrid::mouseMove (const juce::MouseEvent& event)
{
    if (tool != Tool::select)
        return;

    setMouseCursor (getNoteIndexAt (event.position) >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                                         : juce::MouseCursor::NormalCursor);
}

void NoteGrid::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    dragOrigin = event.position;
    dragSemitones = 0;

    const auto noteIndex = getNoteIndexAt (event.position);
    const auto frameIndex = getFrameForX (event.position.x);

    if (event.mods.isPopupMenu())
    {
        if (noteIndex >= 0 && ! document.getSelection().contains (noteIndex))
            document.selectNote (noteIndex, false);

        showMenuFor (noteIndex);
        return;
    }

    if (event.mods.isAltDown())
    {
        drag = Drag::loopRange;
        return;
    }

    switch (tool)
    {
        case Tool::select:
            if (noteIndex >= 0)
            {
                if (! document.getSelection().contains (noteIndex))
                    document.selectNote (noteIndex, event.mods.isShiftDown());

                drag = Drag::moveNotes;
                dragNoteIndex = noteIndex;
            }
            else
            {
                if (! event.mods.isShiftDown())
                    document.deselectAll();

                drag = Drag::rubberBand;

                if (onPositionClicked != nullptr)
                    onPositionClicked (getTimeForX (event.position.x));
            }
            break;

        case Tool::draw:
            drag = Drag::drawPitch;
            drawFirstFrame = frameIndex;
            drawnSpan.assign (1, static_cast<float> (getPitchForY (event.position.y)));
            break;

        case Tool::split:
            if (noteIndex >= 0)
                document.splitNote (noteIndex, frameIndex);
            break;

        case Tool::join:
            if (noteIndex >= 0 && noteIndex + 1 < static_cast<int> (document.getNotes().size()))
                document.mergeNotes ({ noteIndex, noteIndex + 1 });
            break;
    }
}

void NoteGrid::mouseDrag (const juce::MouseEvent& event)
{
    switch (drag)
    {
        case Drag::moveNotes:
        {
            const auto moved = juce::roundToInt ((dragOrigin.y - event.position.y) / rowHeight);

            if (moved != dragSemitones)
            {
                dragSemitones = moved;
                repaint();
            }

            break;
        }

        case Drag::rubberBand:
            repaint();
            break;

        case Drag::drawPitch:
        {
            const auto frameIndex = getFrameForX (event.position.x);
            const auto pitch = static_cast<float> (getPitchForY (event.position.y));

            if (frameIndex < drawFirstFrame)
            {
                drawnSpan.insert (drawnSpan.begin(),
                                  static_cast<std::size_t> (drawFirstFrame - frameIndex), pitch);
                drawFirstFrame = frameIndex;
            }
            else
            {
                while (static_cast<int> (drawnSpan.size()) <= frameIndex - drawFirstFrame)
                    drawnSpan.push_back (pitch);
            }

            drawnSpan[static_cast<std::size_t> (frameIndex - drawFirstFrame)] = pitch;
            break;
        }

        case Drag::loopRange:
            if (onLoopDragged != nullptr)
                onLoopDragged (getTimeForX (std::min (dragOrigin.x, event.position.x)),
                               getTimeForX (std::max (dragOrigin.x, event.position.x)));
            break;

        case Drag::none:
            break;
    }
}

void NoteGrid::mouseUp (const juce::MouseEvent& event)
{
    switch (drag)
    {
        case Drag::moveNotes:
            if (dragSemitones != 0)
            {
                auto indices = getSelectedIndices();

                if (indices.empty() && dragNoteIndex >= 0)
                    indices.push_back (dragNoteIndex);

                document.nudgeNotes (indices, dragSemitones);
            }

            break;

        case Drag::rubberBand:
        {
            const auto area = juce::Rectangle<float> (dragOrigin, event.position);

            if (area.getWidth() > 3.0f || area.getHeight() > 3.0f)
            {
                juce::SparseSet<int> chosen;
                const auto& notes = document.getNotes();

                for (int index = 0; index < static_cast<int> (notes.size()); ++index)
                    if (area.intersects (getNoteBounds (notes[static_cast<std::size_t> (index)])))
                        chosen.addRange ({ index, index + 1 });

                document.setSelection (std::move (chosen));
            }

            break;
        }

        case Drag::drawPitch:
            document.drawPitch (drawFirstFrame, drawnSpan);
            drawnSpan.clear();
            break;

        case Drag::loopRange:
        case Drag::none:
            break;
    }

    drag = Drag::none;
    dragSemitones = 0;
    dragNoteIndex = -1;
    repaint();
}

void NoteGrid::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto noteIndex = getNoteIndexAt (event.position);

    if (noteIndex < 0)
        return;

    const auto& scale = document.getScale();

    document.modifyNotes ({ noteIndex },
                          [&scale] (Note& note)
                          {
                              note.targetNote = scale.snap (note.sungPitch);
                              note.isEnabled = true;
                              note.correction = 1.0f;
                          },
                          "retune the note");
}

void NoteGrid::showMenuFor (int noteIndex)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    const auto indices = getSelectedIndices();
    const auto hasNotes = ! indices.empty();

    menu.addItem (1, "Retune to the scale", hasNotes);
    menu.addItem (2, "Leave as sung", hasNotes);
    menu.addItem (3, "Correct fully", hasNotes);
    menu.addSeparator();
    menu.addItem (4, "Join", indices.size() > 1);
    menu.addItem (5, "Forget these notes", hasNotes);
    menu.addSeparator();
    menu.addItem (6, "Clear what was drawn");
    menu.addItem (7, "Find the notes again");

    juce::ignoreUnused (noteIndex);

    menu.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (this),
                        [this, indices] (int result)
                        {
                            const auto& scale = document.getScale();

                            switch (result)
                            {
                                case 1:
                                    document.modifyNotes (indices,
                                                          [&scale] (Note& note) { note.targetNote = scale.snap (note.sungPitch); },
                                                          "retune to the scale");
                                    break;
                                case 2:
                                    document.modifyNotes (indices, [] (Note& note) { note.isEnabled = false; },
                                                          "leave as sung");
                                    break;
                                case 3:
                                    document.modifyNotes (indices,
                                                          [] (Note& note) { note.isEnabled = true; note.correction = 1.0f; },
                                                          "correct fully");
                                    break;
                                case 4: document.mergeNotes (indices); break;
                                case 5: document.removeNotes (indices); break;
                                case 6: document.clearDrawnPitch(); break;
                                case 7: document.resetNotes(); break;
                                default: break;
                            }
                        });
}

bool NoteGrid::keyPressed (const juce::KeyPress& key)
{
    const auto indices = getSelectedIndices();

    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        document.nudgeNotes (indices, key == juce::KeyPress::upKey ? 1 : -1);
        return true;
    }

    if (key.getTextCharacter() == 'a' && key.getModifiers().isCommandDown())
    {
        document.selectAll();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        document.removeNotes (indices);
        return true;
    }

    return false;
}
}
