#include "PluginEditor.h"

namespace multiplyandreplenish
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;

    constexpr auto projectExtension = ".mrp";
    constexpr int playheadRefreshHz = 30;
}

PluginEditor::PluginEditor (PluginProcessor& owner)
    : juce::AudioProcessorEditor (owner),
#if JucePlugin_Enable_ARA
      juce::AudioProcessorEditorARAExtension (&owner),
#endif
      processor (owner),
      toolBar (owner.wrapperType == juce::AudioProcessor::wrapperType_Standalone),
      editorView (owner.getActiveDocument())
{
    setOpaque (true);
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (toolBar);
    addAndMakeVisible (tabBar);
    addAndMakeVisible (editorView);

    toolBar.onOpen = [this] { open(); };
    toolBar.onSave = [this] { chooseAndSave(); };
    toolBar.onUndo = [this] { processor.undo(); };
    toolBar.onRedo = [this] { processor.redo(); };
    toolBar.onPlayPause = [this] { processor.togglePlayback(); };
    toolBar.onToolChosen = [this] (EditTool tool) { editorView.getGrid().setTool (tool); };
    toolBar.onTranspose = [this] (int steps) { processor.getActiveDocument().transposeByScaleSteps (steps); };
    toolBar.onScaleChanged = [this] (const Scale& scale) { processor.setScale (scale); };

    tabBar.onTabSelected = [this] (int index) { processor.setActiveTab (index); };
    tabBar.onAddTab = [this] { processor.addTab(); };
    tabBar.onTabClosed = [this] (int index) { processor.removeTab (index); };

    editorView.getGrid().onPositionClicked = [this] (double seconds)
    {
        processor.setPlayhead (seconds);
        editorView.setPlayheadPosition (seconds, false);
    };

    processor.addChangeListener (this);

#if JucePlugin_Enable_ARA
    if (auto* view = getARAEditorView())
    {
        view->addListener (this);
        processor.openFromAraSelection (view->getViewSelection());
    }
#endif

    setSize (1200, 760);
    setResizable (true, false);
    setResizeLimits (920, 480, 10000, 10000);

    refresh();
    startTimerHz (playheadRefreshHz);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    processor.removeChangeListener (this);

#if JucePlugin_Enable_ARA
    if (auto* view = getARAEditorView())
        view->removeListener (this);
#endif

    setLookAndFeel (nullptr);
}

void PluginEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

#if JucePlugin_Enable_ARA
void PluginEditor::onNewSelection (const juce::ARAViewSelection& viewSelection)
{
    processor.openFromAraSelection (viewSelection);
}
#endif

void PluginEditor::refresh()
{
    auto& document = processor.getActiveDocument();

    editorView.setDocument (document);

    tabBar.setTabs (processor.getNumTabs(), processor.getActiveTab());

    editorView.setTabColour (Palette::tab (processor.getActiveTab()));

    tabBar.setAddEnabled (processor.hasTake() && ! processor.isBusy());
    toolBar.setScale (processor.getScale());

    if (! document.hasMelody())
        hasShownMelody = false;
    else if (! hasShownMelody)
    {
        hasShownMelody = true;
        editorView.scrollToSinging();
    }

    editorView.setBusy (processor.isBusy());
    editorView.setCaption (processor.getError(), true);

    refreshTransport();
}

void PluginEditor::refreshTransport()
{
    auto& player = processor.getPlayer();

    toolBar.setPlaying (player.isPlaying());
    toolBar.setUndoRedoEnabled (processor.canUndo(), processor.canRedo());

    if (player.isPlaying())
        processor.updateRenderPriority();

    editorView.setPlayheadPosition (player.getPosition(), player.isPlaying());
}

void PluginEditor::timerCallback()
{
    refreshTransport();
}

void PluginEditor::open()
{
#if JucePlugin_Enable_ARA
    if (processor.isAraActive())
    {
        if (auto* view = getARAEditorView())
            processor.openFromAraSelection (view->getViewSelection());

        return;
    }
#endif

    chooseAndOpen();
}

void PluginEditor::chooseAndOpen()
{
    chooser = std::make_unique<juce::FileChooser> ("Open",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   processor.getFormats().getWildcardForAllFormats()
                                                       + ";*" + projectExtension);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& browser)
                          {
                              const auto file = browser.getResult();

                              if (! file.existsAsFile())
                                  return;

                              if (file.hasFileExtension (projectExtension))
                                  juce::ignoreUnused (processor.loadProject (file));
                              else
                                  processor.openFile (file);
                          });
}

void PluginEditor::chooseAndSave()
{
    if (! processor.hasTake())
        return;

    const auto& source = processor.getActiveDocument().getFile();

    chooser = std::make_unique<juce::FileChooser> ("Save",
                                                   source.getParentDirectory()
                                                         .getChildFile (source.getFileNameWithoutExtension() + projectExtension),
                                                   juce::String ("*") + projectExtension);

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& browser)
                          {
                              const auto file = browser.getResult();

                              if (file != juce::File {})
                                  juce::ignoreUnused (processor.saveProject (file.withFileExtension (projectExtension)));
                          });
}

bool PluginEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        processor.togglePlayback();
        return true;
    }

    if (key.getTextCharacter() == 'z' && key.getModifiers().isCommandDown())
    {
        if (key.getModifiers().isShiftDown())
            processor.redo();
        else
            processor.undo();

        return true;
    }

    return false;
}

void PluginEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);
}

void PluginEditor::resized()
{
    auto bounds = getLocalBounds();

    toolBar.setBounds (bounds.removeFromTop (ToolBar::preferredHeight));
    tabBar.setBounds (bounds.removeFromTop (TabBar::preferredHeight));
    editorView.setBounds (bounds);
}
}
