#include "edit/EditDocument.h"

#include <algorithm>
#include <cmath>

namespace tuner
{
namespace
{
    /** @brief One edit, remembered as the whole of what it changed. The document is small enough
               that keeping both sides of every edit costs less than describing them one by one.
    */
    class StateChange final : public juce::UndoableAction
    {
    public:
        StateChange (EditDocument& documentToChange, EditDocument::State before, EditDocument::State after)
            : document (documentToChange),
              previousState (std::move (before)),
              nextState (std::move (after))
        {
        }

        bool perform() override
        {
            document.setState (nextState);
            return true;
        }

        bool undo() override
        {
            document.setState (previousState);
            return true;
        }

        [[nodiscard]] int getSizeInUnits() override
        {
            return static_cast<int> (sizeof (Note) * (previousState.notes.size() + nextState.notes.size())
                                     + sizeof (float) * (previousState.drawn.size() + nextState.drawn.size()));
        }

    private:
        EditDocument& document;
        EditDocument::State previousState;
        EditDocument::State nextState;
    };
}

EditDocument::EditDocument() = default;
EditDocument::~EditDocument() = default;

void EditDocument::addListener (Listener* listener) { listeners.add (listener); }
void EditDocument::removeListener (Listener* listener) { listeners.remove (listener); }

double EditDocument::getSeconds() const noexcept
{
    if (hasMelody())
        return sung.getSeconds();

    return sampleRate > 0.0 ? static_cast<double> (recording.getNumSamples()) / sampleRate : 0.0;
}

int EditDocument::getFrameForTime (double seconds) const noexcept
{
    return juce::jlimit (0, std::max (0, getNumFrames() - 1),
                         static_cast<int> (std::floor (seconds * static_cast<double> (getFrameRate()))));
}

double EditDocument::getTimeForFrame (int frameIndex) const noexcept
{
    return static_cast<double> (frameIndex) / static_cast<double> (getFrameRate());
}

int EditDocument::getSampleForFrame (int frameIndex) const noexcept
{
    return static_cast<int> (std::llround (getTimeForFrame (frameIndex) * sampleRate));
}

void EditDocument::setRecording (juce::AudioBuffer<float> audio, double rate, const juce::File& file)
{
    recording = std::move (audio);
    sampleRate = rate;
    sourceFile = file;

    sung = {};
    corrected = {};
    notes.clear();
    drawn.clear();
    selection.clear();
    undoManager.clearUndoHistory();

    listeners.call ([] (Listener& listener) { listener.recordingChanged(); });
    sendChangeMessage();
}

void EditDocument::setMelody (PitchTrack melody)
{
    sung = std::move (melody);

    drawn.assign (static_cast<std::size_t> (sung.getNumFrames()), 0.0f);
    notes = segmentNotes (sung, scale, segmenter);
    selection.clear();
    undoManager.clearUndoHistory();

    rebuildMelody();

    listeners.call ([] (Listener& listener) { listener.recordingChanged(); });
    notifyMelodyChanged();
}

void EditDocument::rebuildMelody()
{
    corrected = correctMelody (sung, notes, drawn, correction);
}

void EditDocument::notifyMelodyChanged()
{
    listeners.call ([] (Listener& listener) { listener.melodyChanged(); });
    sendChangeMessage();
}

EditDocument::State EditDocument::getState() const
{
    return { notes, drawn, scale, correction };
}

void EditDocument::setState (State state)
{
    notes = std::move (state.notes);
    drawn = std::move (state.drawn);
    scale = state.scale;
    correction = state.correction;

    if (selection.getTotalRange().getEnd() > static_cast<int> (notes.size()))
        selection.clear();

    rebuildMelody();
    notifyMelodyChanged();
}

void EditDocument::applyState (State state, const juce::String& actionName)
{
    undoManager.beginNewTransaction (actionName);
    undoManager.perform (new StateChange (*this, getState(), std::move (state)), actionName);
}

int EditDocument::getNoteAt (int frameIndex) const noexcept
{
    for (int index = 0; index < static_cast<int> (notes.size()); ++index)
        if (notes[static_cast<std::size_t> (index)].contains (frameIndex))
            return index;

    return -1;
}

void EditDocument::setSelection (juce::SparseSet<int> indices)
{
    if (selection == indices)
        return;

    selection = std::move (indices);
    listeners.call ([] (Listener& listener) { listener.selectionChanged(); });
}

void EditDocument::selectNote (int index, bool shouldExtend)
{
    auto updated = shouldExtend ? selection : juce::SparseSet<int> {};

    if (index >= 0)
    {
        if (shouldExtend && updated.contains (index))
            updated.removeRange ({ index, index + 1 });
        else
            updated.addRange ({ index, index + 1 });
    }

    setSelection (std::move (updated));
}

void EditDocument::selectAll()
{
    juce::SparseSet<int> everything;

    if (! notes.empty())
        everything.addRange ({ 0, static_cast<int> (notes.size()) });

    setSelection (std::move (everything));
}

void EditDocument::deselectAll()
{
    setSelection ({});
}

void EditDocument::setScale (const Scale& newScale, bool shouldRetarget)
{
    auto state = getState();
    state.scale = newScale;

    if (shouldRetarget)
        for (auto& note : state.notes)
            note.targetNote = newScale.snap (note.sungPitch);

    applyState (std::move (state), "change the scale");
}

void EditDocument::setCorrectionSettings (const CorrectionSettings& settings)
{
    auto state = getState();
    state.correction = settings;
    applyState (std::move (state), "change the correction");
}

void EditDocument::setSegmenterSettings (const SegmenterSettings& settings)
{
    segmenter = settings;

    auto state = getState();
    state.notes = segmentNotes (sung, scale, segmenter);
    applyState (std::move (state), "find the notes again");
}

void EditDocument::modifyNotes (const std::vector<int>& indices,
                                std::function<void (Note&)> change,
                                const juce::String& actionName)
{
    if (indices.empty() || change == nullptr)
        return;

    auto state = getState();

    for (const auto index : indices)
        if (index >= 0 && index < static_cast<int> (state.notes.size()))
            change (state.notes[static_cast<std::size_t> (index)]);

    applyState (std::move (state), actionName);
}

void EditDocument::nudgeNotes (const std::vector<int>& indices, int semitones)
{
    modifyNotes (indices,
                 [semitones] (Note& note) { note.targetNote += semitones; },
                 semitones > 0 ? "move up" : "move down");
}

void EditDocument::splitNote (int index, int frameIndex)
{
    if (index < 0 || index >= static_cast<int> (notes.size()))
        return;

    const auto& note = notes[static_cast<std::size_t> (index)];

    if (frameIndex <= note.firstFrame || frameIndex >= note.lastFrame)
        return;

    auto state = getState();

    auto first = state.notes[static_cast<std::size_t> (index)];
    auto second = first;

    first.lastFrame = frameIndex;
    second.firstFrame = frameIndex;

    measureNote (first, sung, scale, segmenter);
    measureNote (second, sung, scale, segmenter);

    state.notes[static_cast<std::size_t> (index)] = first;
    state.notes.insert (state.notes.begin() + index + 1, second);

    applyState (std::move (state), "split the note");
}

void EditDocument::mergeNotes (const std::vector<int>& indices)
{
    if (indices.size() < 2)
        return;

    auto sorted = indices;
    std::sort (sorted.begin(), sorted.end());

    if (sorted.front() < 0 || sorted.back() >= static_cast<int> (notes.size()))
        return;

    auto state = getState();

    auto merged = state.notes[static_cast<std::size_t> (sorted.front())];
    merged.lastFrame = state.notes[static_cast<std::size_t> (sorted.back())].lastFrame;

    measureNote (merged, sung, scale, segmenter);

    state.notes.erase (state.notes.begin() + sorted.front(), state.notes.begin() + sorted.back() + 1);
    state.notes.insert (state.notes.begin() + sorted.front(), merged);

    deselectAll();
    applyState (std::move (state), "join the notes");
}

void EditDocument::removeNotes (const std::vector<int>& indices)
{
    if (indices.empty())
        return;

    auto sorted = indices;
    std::sort (sorted.rbegin(), sorted.rend());

    auto state = getState();

    for (const auto index : sorted)
        if (index >= 0 && index < static_cast<int> (state.notes.size()))
            state.notes.erase (state.notes.begin() + index);

    deselectAll();
    applyState (std::move (state), "leave the notes as sung");
}

void EditDocument::resetNotes()
{
    auto state = getState();
    state.notes = segmentNotes (sung, scale, segmenter);
    state.drawn.assign (static_cast<std::size_t> (sung.getNumFrames()), 0.0f);

    deselectAll();
    applyState (std::move (state), "start again");
}

void EditDocument::drawPitch (int firstFrame, const std::vector<float>& semitones)
{
    if (semitones.empty() || drawn.empty())
        return;

    auto state = getState();

    for (std::size_t offset = 0; offset < semitones.size(); ++offset)
    {
        const auto frameIndex = firstFrame + static_cast<int> (offset);

        if (frameIndex < 0 || frameIndex >= static_cast<int> (state.drawn.size()))
            continue;

        state.drawn[static_cast<std::size_t> (frameIndex)] = semitones[offset];
    }

    applyState (std::move (state), "draw the pitch");
}

void EditDocument::clearDrawnPitch()
{
    auto state = getState();
    state.drawn.assign (static_cast<std::size_t> (sung.getNumFrames()), 0.0f);
    applyState (std::move (state), "clear what was drawn");
}
}
