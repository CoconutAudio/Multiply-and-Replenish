#pragma once

#include "model/ModelLibrary.h"
#include "plugin/Modification.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <map>

namespace tuner
{
/** @brief Owns the ARA model: which regions exist, what has been heard in them, and the thread
           everything slow runs on.

    A region is analysed once, when the host says its samples may be read: the melody is estimated,
    the notes are cut, and the vocoder reads the audio. After that every edit is a re-render of a
    couple of seconds, which the region's own scheduler does.
*/
class DocumentController final : public juce::ARADocumentControllerSpecialisation
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

    ~DocumentController() override;

    /** @brief Notified on the message thread when a region's state changes. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void regionStateChanged() = 0;
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

    /** @brief Every modification the document holds, in no particular order. */
    [[nodiscard]] std::vector<Modification*> getModifications();

    /** @brief The one the editor should show: the first that has been analysed, else the first. */
    [[nodiscard]] Modification* getModificationToEdit();

    /** @brief Reads a region if it has not been read yet. */
    void requestAnalysis (Modification& modification);

    [[nodiscard]] const ModelLibrary& getModelLibrary() const noexcept { return models; }

protected:
    juce::ARAAudioModification* doCreateAudioModification (
        juce::ARAAudioSource* audioSource,
        ARA::ARAAudioModificationHostRef hostRef,
        const juce::ARAAudioModification* optionalModificationToClone) noexcept override;

    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;

    bool doRestoreObjectsFromStream (juce::ARAInputStream& input,
                                     const juce::ARARestoreObjectsFilter* filter) noexcept override;

    bool doStoreObjectsToStream (juce::ARAOutputStream& output,
                                 const juce::ARAStoreObjectsFilter* filter) noexcept override;

    void didEnableAudioSourceSamplesAccess (juce::ARAAudioSource* audioSource, bool enable) noexcept override;

private:
    class AnalysisJob;

    void notifyStateChanged();

    /** @brief Forgets a finished job, so a region that failed can be read again. */
    void jobFinished (Modification& modification);

    ModelLibrary models;

    juce::ThreadPool pool { juce::ThreadPoolOptions {}.withNumberOfThreads (1)
                                                      .withThreadName ("Tuner analysis") };

    mutable juce::CriticalSection jobLock;
    std::map<Modification*, AnalysisJob*> activeJobs;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DocumentController)
};
}
