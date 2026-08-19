#pragma once

#include "edit/CorrectionCurve.h"
#include "edit/NoteSegmenter.h"
#include "model/PitchDetector.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace rvctuner
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
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    /** @brief Opens a recording, clearing everything that described the last one. */
    void setRecording (juce::AudioBuffer<float> audio, double sampleRate, const juce::File& file);

    /** @brief Takes the melody a detector estimated, and cuts it into notes. */
    void setMelody (PitchTrack melody);

    [[nodiscard]] bool hasRecording() const noexcept { return recording.getNumSamples() > 0; }
    [[nodiscard]] bool hasMelody() const noexcept { return sung.getNumFrames() > 0; }

    [[nodiscard]] const juce::AudioBuffer<float>& getRecording() const noexcept { return recording; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] const juce::File& getFile() const noexcept { return sourceFile; }

    [[nodiscard]] const PitchTrack& getSungMelody() const noexcept { return sung; }
    [[nodiscard]] const CorrectedMelody& getCorrectedMelody() const noexcept { return corrected; }
    [[nodiscard]] const std::vector<Note>& getNotes() const noexcept { return notes; }

    [[nodiscard]] int getNumFrames() const noexcept { return sung.getNumFrames(); }
    [[nodiscard]] int getFrameRate() const noexcept { return sung.frameRate; }
    [[nodiscard]] double getSeconds() const noexcept;

    [[nodiscard]] int getFrameForTime (double seconds) const noexcept;
    [[nodiscard]] double getTimeForFrame (int frameIndex) const noexcept;
    [[nodiscard]] int getSampleForFrame (int frameIndex) const noexcept;

    /** @brief The note covering a frame, or -1. */
    [[nodiscard]] int getNoteAt (int frameIndex) const noexcept;

    [[nodiscard]] const juce::SparseSet<int>& getSelection() const noexcept { return selection; }
    void setSelection (juce::SparseSet<int> indices);
    void selectNote (int index, bool shouldExtend);
    void selectAll();
    void deselectAll();

    [[nodiscard]] const Scale& getScale() const noexcept { return scale; }
    void setScale (const Scale& newScale, bool shouldRetarget);

    [[nodiscard]] const CorrectionSettings& getCorrectionSettings() const noexcept { return correction; }
    void setCorrectionSettings (const CorrectionSettings& settings);

    [[nodiscard]] const SegmenterSettings& getSegmenterSettings() const noexcept { return segmenter; }
    void setSegmenterSettings (const SegmenterSettings& settings);

    /** @brief Moves the selected notes, or one note, by whole semitones. */
    void nudgeNotes (const std::vector<int>& indices, int semitones);

    /** @brief Applies a change to every named note through a function of the note. */
    void modifyNotes (const std::vector<int>& indices, std::function<void (Note&)> change,
                      const juce::String& actionName);

    /** @brief Cuts a note in two at a frame. */
    void splitNote (int index, int frameIndex);

    /** @brief Joins a run of neighbouring notes into one. */
    void mergeNotes (const std::vector<int>& indices);

    /** @brief Leaves a stretch of the recording as sung, by dropping the notes over it. */
    void removeNotes (const std::vector<int>& indices);

    /** @brief Returns the notes to what the segmenter made of the melody. */
    void resetNotes();

    /** @brief Writes a drawn pitch over a span, in semitones; zero clears a frame. */
    void drawPitch (int firstFrame, const std::vector<float>& semitones);

    /** @brief Clears everything drawn by hand. */
    void clearDrawnPitch();

    [[nodiscard]] const std::vector<float>& getDrawnPitch() const noexcept { return drawn; }

    [[nodiscard]] juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    /** @brief A snapshot of everything an edit can change, which is what undo restores. */
    struct State
    {
        std::vector<Note> notes;
        std::vector<float> drawn;
        Scale scale;
        CorrectionSettings correction;
    };

    /** @brief Puts a state back, as undo and redo do. */
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
    CorrectedMelody corrected;

    std::vector<Note> notes;
    std::vector<float> drawn;

    Scale scale { Scale::Type::chromatic, 0 };
    CorrectionSettings correction;
    SegmenterSettings segmenter;

    juce::SparseSet<int> selection;

    juce::UndoManager undoManager;
    juce::ListenerList<Listener> listeners;
};
}
