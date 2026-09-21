#pragma once

#include "PluginProcessor.h"
#include "ui/EditorView.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/TabBar.h"
#include "ui/ToolBar.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace multiplyandreplenish
{
/** @brief The window: a toolbar, a bar of tabs, and the piano roll of whichever tab is showing.

    Owns no state of its own beyond the view. The take, the tabs and the renders all live in the
    processor, so closing and reopening the window leaves the edit untouched.
*/
class PluginEditor final : public juce::AudioProcessorEditor,
#if JucePlugin_Enable_ARA
                           public juce::AudioProcessorEditorARAExtension,
                           private juce::ARAEditorView::Listener,
#endif
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor& owner);
    ~PluginEditor() override;

    void resized() override;
    void paint (juce::Graphics& graphics) override;

    bool keyPressed (const juce::KeyPress& key) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

#if JucePlugin_Enable_ARA
    void onNewSelection (const juce::ARAViewSelection& viewSelection) override;
#endif

    /** @brief Opens a file in the standalone, or takes the host's selected region under ARA. */
    void open();
    void chooseAndOpen();
    void chooseAndSave();

    void refresh();
    void refreshTransport();

    PluginProcessor& processor;

    PanelLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    ToolBar toolBar;
    TabBar tabBar;
    EditorView editorView;

    std::unique_ptr<juce::FileChooser> chooser;

    bool hasShownMelody { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
}
