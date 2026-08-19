#pragma once

#include "plugin/DocumentController.h"
#include "ui/EditorView.h"
#include "ui/Inspector.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/ToolBar.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace tuner
{
class Processor;

/** @brief The plug-in's editor: the same note grid as the app, bound to whichever region the host
           has given us, or an explanation of why there is nothing to edit.
*/
class Editor final : public juce::AudioProcessorEditor,
                     private DocumentController::Listener,
                     private juce::Timer
{
public:
    explicit Editor (Processor& processor);
    ~Editor() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void regionStateChanged() override;
    void timerCallback() override;

    void bindToRegion();

    [[nodiscard]] DocumentController* getBoundDocumentController() const;

    Processor& processor;

    PanelLookAndFeel lookAndFeel;

    Modification* boundModification { nullptr };

    std::unique_ptr<ToolBar> toolBar;
    std::unique_ptr<EditorView> editorView;
    std::unique_ptr<Inspector> inspector;

    juce::String caption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};
}
