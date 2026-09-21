#pragma once

#include "common/EditDocument.h"
#include "common/Pipeline.h"
#include "common/RenderScheduler.h"
#include "common/Scale.h"
#include "common/TransportPlayer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace multiplyandreplenish
{
/** @brief One take, analysed once and edited as any number of tabs.

    Every tab is the same audio with its own edit of the melody, its own undo history and its own
    render. Playing sings all of them at once, so a second tab moved up a third is a harmony.
    The same processor is the VST3 and the standalone; the standalone opens files and saves
    projects, the plug-in takes its audio from the host through ARA.
*/
class PluginProcessor final : public juce::AudioProcessor,
#if JucePlugin_Enable_ARA
                              public juce::AudioProcessorARAExtension,
#endif
                              public juce::ChangeBroadcaster
{
public:
    /** @brief A tab: one edit of the take, and the render of that edit. */
    struct Tab
    {
        EditDocument document;
        RenderScheduler scheduler;
    };

    PluginProcessor();
    ~PluginProcessor() override;

    // --- juce::AudioProcessor ------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    [[nodiscard]] bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    [[nodiscard]] bool hasEditor() const override { return true; }

    [[nodiscard]] const juce::String getName() const override { return JucePlugin_Name; }
    [[nodiscard]] bool acceptsMidi() const override { return false; }
    [[nodiscard]] bool producesMidi() const override { return false; }
    [[nodiscard]] bool isMidiEffect() const override { return false; }
    [[nodiscard]] double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    /** @brief The host's copy of the state: the same project @ref saveProject writes. */
    void getStateInformation (juce::MemoryBlock& destinationData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- The take ------------------------------------------------------------------------------
    [[nodiscard]] juce::AudioFormatManager& getFormats() noexcept { return formats; }

    /** @brief Opens an audio file as the take, analyses it, and leaves one tab. Standalone. */
    void openFile (const juce::File& file);

    /** @brief Writes the take's source and every tab's edits to a project file. Standalone. */
    [[nodiscard]] bool saveProject (const juce::File& file);

    /** @brief Reads a file @ref saveProject wrote. */
    [[nodiscard]] bool loadProject (const juce::File& file);

    /** @brief True while the take is being analysed. */
    [[nodiscard]] bool isBusy() const noexcept { return busy; }

    /** @brief True once a take is open. */
    [[nodiscard]] bool hasTake() const noexcept;

    /** @brief Why the last analysis failed, or empty; a missing model is the usual reason. */
    [[nodiscard]] juce::String getError() const;

    // --- Tabs ----------------------------------------------------------------------------------
    [[nodiscard]] int getNumTabs() const noexcept;
    [[nodiscard]] int getActiveTab() const noexcept { return activeTab; }
    void setActiveTab (int index);
    [[nodiscard]] EditDocument& getDocument (int tabIndex) noexcept;
    [[nodiscard]] EditDocument& getActiveDocument() noexcept { return getDocument (activeTab); }

    /** @brief Adds a tab that starts as the take was analysed, makes it active, and renders it. */
    void addTab();

    /** @brief Closes a tab and its render; the last tab cannot be closed. */
    void removeTab (int index);

    // --- Editing -------------------------------------------------------------------------------
    [[nodiscard]] bool canUndo() noexcept;
    [[nodiscard]] bool canRedo() noexcept;
    void undo();
    void redo();

    /** @brief The key and scale, shared by every tab so a harmony stays in the key it is sung in. */
    [[nodiscard]] Scale getScale() const;
    void setScale (const Scale& scale);

    // --- Playback ------------------------------------------------------------------------------
    [[nodiscard]] TransportPlayer& getPlayer() noexcept { return player; }

    /** @brief Plays or pauses from the current position; from the start if it had run to the end. */
    void togglePlayback();

    /** @brief Moves the playhead, and renders around it first. */
    void setPlayhead (double seconds);

    /** @brief Renders around the playhead first; the editor calls it from its timer while playing. */
    void updateRenderPriority();

    /** @brief Every tab summed over the whole take, as far as each has been rendered.

        The first tab plays as sung where it is not rendered yet, so this is always the full length
        of the take; a tab that is not ready for a stretch contributes nothing to it.
    */
    [[nodiscard]] juce::AudioBuffer<float> getMix();

#if JucePlugin_Enable_ARA
    // --- ARA (implemented in ara/AraSupport.cpp) -----------------------------------------------
    /** @brief True once the host has put this instance under ARA control. */
    [[nodiscard]] bool isAraActive() const;

    /** @brief Takes the audio of the region selected in the host as the take, and analyses it. */
    bool openFromAraSelection (const juce::ARAViewSelection& selection);

    /** @brief Hands the rendered mix of all tabs back to the host so it plays what was edited. */
    void publishToAra();
#endif

private:
    /** @brief The pipeline, the shared synthesiser and the tabs. */
    struct Impl;

    juce::AudioFormatManager formats;
    TransportPlayer player;
    int activeTab { 0 };
    bool busy { false };
    juce::String error;

    // Last, so it is built after the player it feeds and destroyed before it.
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
}
