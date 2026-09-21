#include "ui/NoteGrid.h"

#include "ui/Flat.h"
#include "ui/PanelLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace multiplyandreplenish
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
    : document (&documentToEdit)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    document->addListener (this);
}

NoteGrid::~NoteGrid()
{
    document->removeListener (this);
}

void NoteGrid::setDocument (EditDocument& newDocument)
{
    if (document == &newDocument)
        return;

    document->removeListener (this);
    document = &newDocument;
    document->addListener (this);

    drag = Drag::none;
    repaint();
}

void NoteGrid::setTool (EditTool newTool)
{
    tool = newTool;
    setMouseCursor (tool == EditTool::cut ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void NoteGrid::setTabColour (juce::Colour colour)
{
    tabColour = colour;
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
    const auto width = juce::roundToInt (document->getSeconds() * static_cast<double> (pixelsPerSecond));

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
    return juce::jlimit (0, std::max (0, document->getNumFrames() - 1),
                         document->getFrameForTime (getTimeForX (x)));
}

int NoteGrid::getCentreNote() const
{
    const auto& melody = document->getSungMelody();

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

void NoteGrid::recordingChanged() { repaint(); }
void NoteGrid::melodyChanged() { repaint(); }
void NoteGrid::selectionChanged() { repaint(); }

juce::Rectangle<float> NoteGrid::getNoteBounds (const Note& note) const
{
    const auto first = getXForTime (document->getTimeForFrame (note.firstFrame));
    const auto last = getXForTime (document->getTimeForFrame (note.lastFrame));
    const auto centre = getYForPitch (static_cast<double> (note.getSoundingSemitones()));

    return { first, centre - rowHeight * 0.5f, std::max (last - first, 2.0f), rowHeight };
}

int NoteGrid::getNoteIndexAt (juce::Point<float> position) const
{
    const auto& notes = document->getNotes();

    for (int index = 0; index < static_cast<int> (notes.size()); ++index)
        if (getNoteBounds (notes[static_cast<std::size_t> (index)]).expanded (0.0f, 2.0f).contains (position))
            return index;

    return -1;
}

std::vector<int> NoteGrid::getSelectedIndices() const
{
    std::vector<int> indices;

    const auto& selection = document->getSelection();

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

        if (! document->getScale().contains (midiNote))
            continue;

        graphics.setColour (tabColour.withAlpha (0.10f));
        graphics.fillRect (row.withWidth (3.0f));
    }
}

void NoteGrid::paintTimeGrid (juce::Graphics& graphics) const
{
    const auto spacing = chooseGridSpacing (pixelsPerSecond);
    const auto seconds = document->getSeconds();

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

    const auto& melody = document->getSungMelody();

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

        const auto x = getXForTime (document->getTimeForFrame (frameIndex));
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
    graphics.strokePath (path, juce::PathStrokeType { thickness });
}

void NoteGrid::paintNotes (juce::Graphics& graphics) const
{
    const auto& notes = document->getNotes();
    const auto& selection = document->getSelection();

    for (int index = 0; index < static_cast<int> (notes.size()); ++index)
    {
        const auto& note = notes[static_cast<std::size_t> (index)];

        auto bounds = getNoteBounds (note).reduced (0.0f, 1.0f);

        if (drag == Drag::moveNotes && selection.contains (index))
            bounds = bounds.withY (getYForPitch (static_cast<double> (note.getSoundingSemitones() + getDragShift (note)))
                                   - rowHeight * 0.5f + 1.0f);

        const auto isSelected = selection.contains (index);
        const auto isMoved = std::abs (note.getShift()) > 0.02f;

        if (isMoved)
        {
            const auto played = bounds.withY (getYForPitch (note.originalSemitones) - rowHeight * 0.5f + 1.0f);

            graphics.setColour (tabColour.withAlpha (0.3f));
            graphics.drawRect (played, 1.0f);
        }

        graphics.setColour (isMoved || isSelected ? tabColour : tabColour.withAlpha (0.45f));
        graphics.fillRect (bounds);

        if (isSelected)
        {
            graphics.setColour (juce::Colours::white);
            graphics.drawRect (bounds, 1.5f);
        }

        if (bounds.getWidth() < 30.0f || rowHeight < 11.0f)
            continue;

        graphics.setColour (Palette::ground.withAlpha (0.85f));
        graphics.setFont (juce::FontOptions { std::min (rowHeight - 4.0f, 11.0f) });
        graphics.drawText (Scale::getNoteName (juce::roundToInt (note.getSoundingSemitones())),
                           bounds.reduced (5.0f, 0.0f), juce::Justification::centredLeft, false);
    }
}

void NoteGrid::paintPlayhead (juce::Graphics& graphics) const
{
    Flat::verticalLine (graphics, getXForTime (playheadSeconds), 0.0f, static_cast<float> (getHeight()),
                        tabColour.brighter (0.6f));
}

void NoteGrid::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::well);

    paintRows (graphics);
    paintTimeGrid (graphics);

    paintCurve (graphics, document->getComposedMelody().sungSemitones, Palette::sungCurve.withAlpha (0.7f), 1.3f);
    paintNotes (graphics);
    paintCurve (graphics, document->getComposedMelody().composedSemitones, tabColour.brighter (0.5f), 1.8f);

    if (drag == Drag::rubberBand)
    {
        const auto area = juce::Rectangle<float> (dragOrigin, getMouseXYRelative().toFloat());

        graphics.setColour (tabColour.withAlpha (0.14f));
        graphics.fillRect (area);
        graphics.setColour (tabColour.withAlpha (0.7f));
        graphics.drawRect (area, 1.0f);
    }

    if (tool == EditTool::cut && hoverX >= 0.0f)
        Flat::verticalLine (graphics, hoverX, 0.0f, static_cast<float> (getHeight()), Palette::text, 1.0f);

    paintPlayhead (graphics);
}

float NoteGrid::getDragShift (const Note& note) const
{
    if (dragIsFree)
        return dragDelta;

    return static_cast<float> (document->getScale().snap (static_cast<double> (note.getSoundingSemitones() + dragDelta)))
         - note.getSoundingSemitones();
}

void NoteGrid::mouseMove (const juce::MouseEvent& event)
{
    if (tool == EditTool::cut)
    {
        hoverX = event.position.x;
        repaint();
        return;
    }

    setMouseCursor (getNoteIndexAt (event.position) >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                                         : juce::MouseCursor::NormalCursor);
}

void NoteGrid::mouseExit (const juce::MouseEvent&)
{
    if (hoverX < 0.0f)
        return;

    hoverX = -1.0f;
    repaint();
}

void NoteGrid::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (tool == EditTool::cut)
    {
        const auto noteIndex = getNoteIndexAt (event.position);

        if (noteIndex >= 0)
            document->splitNote (noteIndex, getFrameForX (event.position.x));

        drag = Drag::none;
        return;
    }

    dragOrigin = event.position;
    dragDelta = 0.0f;
    dragIsFree = false;

    const auto noteIndex = getNoteIndexAt (event.position);

    if (noteIndex >= 0)
    {
        if (! document->getSelection().contains (noteIndex))
            document->selectNote (noteIndex, event.mods.isShiftDown());

        drag = Drag::moveNotes;
        dragNoteIndex = noteIndex;
        return;
    }

    if (! event.mods.isShiftDown())
        document->deselectAll();

    drag = Drag::rubberBand;

    if (onPositionClicked != nullptr)
        onPositionClicked (getTimeForX (event.position.x));
}

void NoteGrid::mouseDrag (const juce::MouseEvent& event)
{
    if (drag == Drag::moveNotes)
    {
        // Control (Command on a Mac) lets the pitch go anywhere; otherwise it lands on the scale.
        dragIsFree = event.mods.isCtrlDown() || event.mods.isCommandDown();
        dragDelta = (dragOrigin.y - event.position.y) / rowHeight;
        repaint();
    }
    else if (drag == Drag::rubberBand)
    {
        repaint();
    }
}

void NoteGrid::mouseUp (const juce::MouseEvent& event)
{
    if (drag == Drag::moveNotes)
    {
        dragIsFree = event.mods.isCtrlDown() || event.mods.isCommandDown();
        dragDelta = (dragOrigin.y - event.position.y) / rowHeight;

        auto indices = getSelectedIndices();

        if (indices.empty() && dragNoteIndex >= 0)
            indices.push_back (dragNoteIndex);

        if (std::abs (dragDelta) >= 0.05f)
            document->modifyNotes (indices,
                                   [this] (Note& note)
                                   {
                                       note.semitones += getDragShift (note);
                                       note.lastEditedSemitones = note.semitones;
                                   },
                                   "move notes");
    }
    else if (drag == Drag::rubberBand)
    {
        const auto area = juce::Rectangle<float> (dragOrigin, event.position);

        if (area.getWidth() > 3.0f || area.getHeight() > 3.0f)
        {
            juce::SparseSet<int> chosen;
            const auto& notes = document->getNotes();

            for (int index = 0; index < static_cast<int> (notes.size()); ++index)
                if (area.intersects (getNoteBounds (notes[static_cast<std::size_t> (index)])))
                    chosen.addRange ({ index, index + 1 });

            document->setSelection (std::move (chosen));
        }
    }

    drag = Drag::none;
    dragDelta = 0.0f;
    dragNoteIndex = -1;
    repaint();
}

bool NoteGrid::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'a' && key.getModifiers().isCommandDown())
    {
        document->selectAll();
        return true;
    }

    return false;
}
}
