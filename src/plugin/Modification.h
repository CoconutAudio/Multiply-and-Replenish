#pragma once

#include "edit/EditDocument.h"
#include "edit/RenderScheduler.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace tuner
{
/** @brief One region's edit: the melody read out of it, the notes, and the correction rendered.

    ARA gives a modification per region per take, and the host decides when its audio can be read.
    Everything the user changes lives here, so it survives the editor being closed and is written
    into the session archive.
*/
class Modification final : public juce::ARAAudioModification
{
public:
    using juce::ARAAudioModification::ARAAudioModification;

    /** @brief How far this region has got. */
    enum class State
    {
        idle,
        analysing,
        ready,
        failed
    };

    [[nodiscard]] EditDocument& getDocument() noexcept { return document; }
    [[nodiscard]] RenderScheduler& getScheduler() noexcept { return scheduler; }

    [[nodiscard]] State getState() const noexcept { return state.load (std::memory_order_acquire); }
    void setState (State newState) noexcept { state.store (newState, std::memory_order_release); }

    [[nodiscard]] float getProgress() const noexcept { return progress.load (std::memory_order_relaxed); }
    void setProgress (float newProgress) noexcept { progress.store (newProgress, std::memory_order_relaxed); }

    [[nodiscard]] juce::String getError() const;
    void setError (const juce::String& message);

    /** @brief The rate the region's audio was read at, which is what the renderer serves. */
    [[nodiscard]] double getSampleRate() const noexcept { return document.getSampleRate(); }

    /** @brief Writes the notes and the settings, but never the audio, into the session. */
    void writeToArchive (juce::OutputStream& stream) const;

    bool readFromArchive (juce::InputStream& stream);

    static void readAndDiscard (juce::InputStream& stream);

private:
    static constexpr int archiveVersion = 1;

    EditDocument document;
    RenderScheduler scheduler;

    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };

    mutable juce::CriticalSection errorLock;
    juce::String errorMessage;
};
}
