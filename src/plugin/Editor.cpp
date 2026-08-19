#include "plugin/Editor.h"

#include "plugin/Processor.h"

namespace tuner
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
}

Editor::Editor (Processor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      processor (processorToEdit)
{
    setLookAndFeel (&lookAndFeel);
    setResizable (true, true);
    setResizeLimits (900, 560, 4000, 3000);
    setSize (1280, 720);

    if (auto* controller = getBoundDocumentController())
        controller->addListener (this);

    caption = processor.isBoundToARA()
                ? "Waiting for the host to hand over a region."
                : "This host is not using ARA.\n\n"
                  "A pitch editor needs the whole take before it can hear a melody in it, which is "
                  "what ARA provides. Load Tuner as an ARA plug-in, or use the standalone app.";

    bindToRegion();
    startTimerHz (4);
}

Editor::~Editor()
{
    stopTimer();

    if (auto* controller = getBoundDocumentController())
        controller->removeListener (this);

    setLookAndFeel (nullptr);
}

DocumentController* Editor::getBoundDocumentController() const
{
    auto* controller = processor.getDocumentController<ARA::PlugIn::DocumentController>();

    if (controller == nullptr)
        return nullptr;

    return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<DocumentController> (
        controller);
}

void Editor::regionStateChanged()
{
    bindToRegion();
    repaint();
}

void Editor::timerCallback()
{
    if (boundModification == nullptr)
        bindToRegion();
    else
        repaint();
}

void Editor::bindToRegion()
{
    auto* controller = getBoundDocumentController();

    if (controller == nullptr)
        return;

    auto* modification = controller->getModificationToEdit();

    if (modification == boundModification)
        return;

    boundModification = modification;

    toolBar.reset();
    editorView.reset();
    inspector.reset();

    if (boundModification == nullptr)
        return;

    if (boundModification->getState() != Modification::State::ready)
    {
        controller->requestAnalysis (*boundModification);
        return;
    }

    auto& document = boundModification->getDocument();

    toolBar = std::make_unique<ToolBar> (document);
    toolBar->setHostMode (true);
    toolBar->onToolChosen = [this] (NoteGrid::Tool tool)
    {
        if (editorView != nullptr)
            editorView->getGrid().setTool (tool);
    };

    editorView = std::make_unique<EditorView> (document);
    inspector = std::make_unique<Inspector> (document);

    addAndMakeVisible (*toolBar);
    addAndMakeVisible (*editorView);
    addAndMakeVisible (*inspector);

    resized();
}

void Editor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);

    if (editorView != nullptr)
        return;

    auto message = caption;

    if (boundModification != nullptr)
    {
        switch (boundModification->getState())
        {
            case Modification::State::analysing:
                message = "Listening to the region... "
                        + juce::String (juce::roundToInt (100.0f * boundModification->getProgress())) + "%";
                break;

            case Modification::State::failed:
                message = boundModification->getError();
                break;

            case Modification::State::idle:
                message = "Waiting for the host to allow the region to be read.";
                break;

            case Modification::State::ready:
                break;
        }
    }

    graphics.setColour (boundModification != nullptr
                            && boundModification->getState() == Modification::State::failed
                        ? Palette::alert
                        : Palette::dimText);
    graphics.setFont (juce::FontOptions { TypeScale::caption });
    graphics.drawFittedText (message, getLocalBounds().reduced (60), juce::Justification::centred, 6);
}

void Editor::resized()
{
    auto bounds = getLocalBounds();

    if (toolBar != nullptr)
        toolBar->setBounds (bounds.removeFromTop (ToolBar::preferredHeight));

    if (inspector != nullptr)
        inspector->setBounds (bounds.removeFromBottom (Inspector::preferredHeight));

    if (editorView != nullptr)
        editorView->setBounds (bounds);
}
}
