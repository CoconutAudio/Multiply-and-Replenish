#include "common/EditDocument.h"

#include <algorithm>
#include <cmath>

namespace multiplyandreplenish
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
            return static_cast<int> (sizeof (Note) * (previousState.notes.size() + nextState.notes.size()));
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

void EditDocument::setRecording (juce::AudioBuffer<float> audio, double rate, const juce::File& file)
{
    recording = std::move (audio);
    sampleRate = rate;
    sourceFile = file;

    sung = {};
    composed = {};
    notes.clear();
    selection.clear();
    undoManager.clearUndoHistory();

    listeners.call ([] (Listener& listener) { listener.recordingChanged(); });
    sendChangeMessage();
}

void EditDocument::setMelody (PitchTrack melody, std::vector<Note> foundNotes)
{
    sung = std::move (melody);

    notes = foundNotes.empty() ? segmentNotes (sung, {}) : std::move (foundNotes);

    measureDeviation (sung, notes);

    selection.clear();
    undoManager.clearUndoHistory();

    rebuildMelody();

    listeners.call ([] (Listener& listener) { listener.recordingChanged(); });
    notifyMelodyChanged();
}

void EditDocument::rebuildMelody()
{
    composed = composeMelody (sung, notes);
}

void EditDocument::notifyMelodyChanged()
{
    listeners.call ([] (Listener& listener) { listener.melodyChanged(); });
    sendChangeMessage();
}

EditDocument::State EditDocument::getState() const
{
    return { notes };
}

void EditDocument::setState (State state)
{
    notes = std::move (state.notes);

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

void EditDocument::setScale (const Scale& newScale)
{
    if (scale == newScale)
        return;

    scale = newScale;

    listeners.call ([] (Listener& listener) { listener.scaleChanged(); });
    sendChangeMessage();
}

void EditDocument::splitNote (int noteIndex, int frameIndex)
{
    if (noteIndex < 0 || noteIndex >= static_cast<int> (notes.size()))
        return;

    const auto& note = notes[static_cast<std::size_t> (noteIndex)];

    if (frameIndex <= note.firstFrame || frameIndex >= note.lastFrame)
        return;

    auto state = getState();

    auto first = state.notes[static_cast<std::size_t> (noteIndex)];
    auto second = first;

    const auto cut = frameIndex - first.firstFrame;

    first.lastFrame = frameIndex;
    second.firstFrame = frameIndex;

    first.sourceLastFrame = std::min (first.sourceLastFrame, first.sourceFirstFrame + cut);
    second.sourceFirstFrame = first.sourceLastFrame;

    if (static_cast<int> (first.deviation.size()) > cut)
    {
        second.deviation.assign (first.deviation.begin() + cut, first.deviation.end());
        first.deviation.resize (static_cast<std::size_t> (cut));
    }

    state.notes[static_cast<std::size_t> (noteIndex)] = std::move (first);
    state.notes.insert (state.notes.begin() + noteIndex + 1, std::move (second));

    applyState (std::move (state), "cut the note");
}

void EditDocument::transposeByScaleSteps (int steps)
{
    if (steps == 0)
        return;

    auto state = getState();

    for (auto& note : state.notes)
    {
        if (note.isRest)
            continue;

        const auto start = scale.snap (static_cast<double> (note.semitones));
        const auto direction = steps > 0 ? 1 : -1;

        auto target = start;

        for (int remaining = std::abs (steps); remaining > 0; --remaining)
        {
            auto candidate = target + direction;

            for (int guard = 0; guard < 12 && ! scale.contains (candidate); ++guard)
                candidate += direction;

            target = candidate;
        }

        note.semitones += static_cast<float> (target - start);
        note.lastEditedSemitones = note.semitones;
    }

    applyState (std::move (state), steps > 0 ? "transpose up" : "transpose down");
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
}
