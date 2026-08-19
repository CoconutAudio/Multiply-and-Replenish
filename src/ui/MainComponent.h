#pragma once

#include "audio/TransportPlayer.h"
#include "edit/EditDocument.h"
#include "edit/Pipeline.h"
#include "edit/RenderScheduler.h"
#include "ui/EditorView.h"
#include "ui/Inspector.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/ToolBar.h"
#include "ui/TransportBar.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <thread>

namespace rvctuner
{
/** @brief The whole application: a recording, the notes read out of it, and everything that acts
           on them.
*/
class MainComponent final : public juce::Component,
                            private EditDocument::Listener,
                            private Pipeline::Listener,
                            private RenderScheduler::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    /** @brief Opens a recording, as the command line or the open button asks. */
    void open (const juce::File& file);

    void resized() override;
    void paint (juce::Graphics& graphics) override;

    bool keyPressed (const juce::KeyPress& key) override;

private:
    void melodyChanged() override;

    void pipelineProgressed (float fraction, const juce::String& stage) override;
    void melodyEstimated (PitchTrack melody) override;
    void pipelineFinished (std::shared_ptr<Synthesiser> synthesiser, const juce::String& error) override;

    void renderChanged() override;

    void chooseAndOpen();
    void chooseAndExport();
    void chooseAndExportAudio();
    void chooseAndExportMidi();
    void exportTo (const juce::File& file);

    /** @brief Renders everything and writes it, on the export thread. */
    [[nodiscard]] bool writeExport (const juce::File& file);
    void analyse();
    void updateStatus();

    juce::AudioFormatManager formats;
    juce::AudioDeviceManager devices;

    EditDocument document;
    Pipeline pipeline;
    RenderScheduler scheduler;
    TransportPlayer player { devices };

    PanelLookAndFeel lookAndFeel;

    ToolBar toolBar { document, pipeline };
    EditorView editor { document };
    Inspector inspector { document };
    TransportBar transport { player };

    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<std::thread> exportThread;
    std::atomic<bool> exportShouldAbort { false };

    juce::String status;
    bool isStatusAlert { false };
    bool isBusy { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
}
