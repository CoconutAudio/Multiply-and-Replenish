#pragma once

#include "common/NoteSegmenter.h"
#include "common/PitchCurve.h"
#include "common/Scale.h"
#include "nn/PitchDetector.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace multiplyandreplenish
{
/** @brief One recording open for editing: what was sung, what it should become, and how to undo.

    The document owns nothing that takes time to compute except the melody, which is handed to it
    once the detector has run. Every edit goes through it, so that undo, the editor and the renderer
    all see the same thing.
*/
class EditDocument : public juce::ChangeBroadcaster
{
public:
    EditDocument();
    ~EditDocument() override;

    /** @brief Notified on the message thread; the renderer and the editor listen to different parts. */
    struct Listener
    {
        virtual ~Listener() = default;

        /** @brief A different recording, or a fresh analysis of this one. */
        virtual void recordingChanged() {}

        /** @brief The melody to render has changed. */
        virtual void melodyChanged() {}

        /** @brief The notes under selection have changed. */
        virtual void selectionChanged() {}

        /** @brief The key or the scale has changed. */
        virtual void scaleChanged() {}
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    /** @brief Opens a recording, clearing everything that described the last one. */
    void setRecording (juce::AudioBuffer<float> audio, double sampleRate, const juce::File& file);

    /** @brief Takes the melody a detector estimated, and the notes found in it.
        @param melody  The melody as estimated.
        @param notes   The notes a model found, or empty to cut them up here instead.
    */
    void setMelody (PitchTrack melody, std::vector<Note> notes = {});

    [[nodiscard]] bool hasRecording() const noexcept { return recording.getNumSamples() > 0; }
    [[nodiscard]] bool hasMelody() const noexcept { return sung.getNumFrames() > 0; }

    [[nodiscard]] const juce::AudioBuffer<float>& getRecording() const noexcept { return recording; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] const juce::File& getFile() const noexcept { return sourceFile; }

    [[nodiscard]] const PitchTrack& getSungMelody() const noexcept { return sung; }
    [[nodiscard]] const ComposedMelody& getComposedMelody() const noexcept { return composed; }
    [[nodiscard]] const std::vector<Note>& getNotes() const noexcept { return notes; }

    [[nodiscard]] int getNumFrames() const noexcept { return sung.getNumFrames(); }
    [[nodiscard]] int getFrameRate() const noexcept { return sung.frameRate; }
    [[nodiscard]] double getSeconds() const noexcept;

    [[nodiscard]] int getFrameForTime (double seconds) const noexcept;
    [[nodiscard]] double getTimeForFrame (int frameIndex) const noexcept;

    /** @brief The note covering a frame, or -1. */
    [[nodiscard]] int getNoteAt (int frameIndex) const noexcept;

    [[nodiscard]] const juce::SparseSet<int>& getSelection() const noexcept { return selection; }
    void setSelection (juce::SparseSet<int> indices);
    void selectNote (int index, bool shouldExtend);
    void selectAll();
    void deselectAll();

    [[nodiscard]] const Scale& getScale() const noexcept { return scale; }

    /** @brief Sets the key and scale. It is a way of looking at the notes, not an edit to them, so
               it is not something undo takes back.
    */
    void setScale (const Scale& newScale);

    /** @brief Cuts a note in two at a frame, keeping the pitch and the measured movement of both halves. */
    void splitNote (int noteIndex, int frameIndex);

    /** @brief Moves every note by a number of steps along the scale, as one undo step. */
    void transposeByScaleSteps (int steps);

    /** @brief Applies a change to every named note through a function of the note. */
    void modifyNotes (const std::vector<int>& indices, std::function<void (Note&)> change,
                      const juce::String& actionName);

    [[nodiscard]] juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    /** @brief A snapshot of everything an edit can change, which is what undo restores. */
    struct State
    {
        std::vector<Note> notes;
    };

    /** @brief Puts a state back, as undo and redo do; also how a saved project is restored. */
    void setState (State state);

    [[nodiscard]] State getState() const;

private:
    void applyState (State state, const juce::String& actionName);
    void rebuildMelody();
    void notifyMelodyChanged();

    juce::AudioBuffer<float> recording;
    double sampleRate { 44100.0 };
    juce::File sourceFile;

    PitchTrack sung;
    ComposedMelody composed;

    std::vector<Note> notes;

    Scale scale { Scale::Type::chromatic, 0 };

    juce::SparseSet<int> selection;

    juce::UndoManager undoManager;
    juce::ListenerList<Listener> listeners;
};
}
