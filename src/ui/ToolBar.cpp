#include "ui/ToolBar.h"

#include "Product.h"
#include "ui/PanelLookAndFeel.h"

namespace rvctuner
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;
}

ToolBar::ToolBar (EditDocument& documentToEdit, Pipeline& pipelineToRun)
    : document (documentToEdit),
      pipeline (pipelineToRun)
{
    setOpaque (true);
    document.addListener (this);

    for (auto* button : { &openButton, &exportButton, &undoButton, &redoButton, &analyseButton,
                          &selectTool, &drawTool, &splitTool, &joinTool })
        addAndMakeVisible (button);

    openButton.onClick = [this] { if (onOpen != nullptr) onOpen(); };
    exportButton.onClick = [this] { if (onExport != nullptr) onExport(); };
    analyseButton.onClick = [this] { if (onAnalyse != nullptr) onAnalyse(); };

    undoButton.onClick = [this] { document.getUndoManager().undo(); };
    redoButton.onClick = [this] { document.getUndoManager().redo(); };

    const auto chooseTool = [this] (NoteGrid::Tool tool)
    {
        setTool (tool);

        if (onToolChosen != nullptr)
            onToolChosen (tool);
    };

    selectTool.onClick = [chooseTool] { chooseTool (NoteGrid::Tool::select); };
    drawTool.onClick = [chooseTool] { chooseTool (NoteGrid::Tool::draw); };
    splitTool.onClick = [chooseTool] { chooseTool (NoteGrid::Tool::split); };
    joinTool.onClick = [chooseTool] { chooseTool (NoteGrid::Tool::join); };

    for (auto* button : { &selectTool, &drawTool, &splitTool, &joinTool })
        button->setClickingTogglesState (true);

    selectTool.setToggleState (true, juce::dontSendNotification);

    keyBox.addItemList (Scale::getPitchClassNames(), 1);
    keyBox.setSelectedItemIndex (0, juce::dontSendNotification);
    keyBox.onChange = [this] { applyScale(); };
    addAndMakeVisible (keyBox);

    scaleBox.addItemList (Scale::getTypeNames(), 1);
    scaleBox.setSelectedItemIndex (0, juce::dontSendNotification);
    scaleBox.onChange = [this] { applyScale(); };
    addAndMakeVisible (scaleBox);

    detectorBox.addItemList (EngineOptions::getDetectorNames(), 1);
    detectorBox.setSelectedItemIndex (0, juce::dontSendNotification);
    addAndMakeVisible (detectorBox);

    if (Product::isVoiceBuild)
    {
        for (const auto& entry : pipeline.getVoiceLibrary().getEntries())
            voiceBox.addItem (entry.name, voiceBox.getNumItems() + 1);

        if (voiceBox.getNumItems() > 0)
            voiceBox.setSelectedItemIndex (0, juce::dontSendNotification);

        addAndMakeVisible (voiceBox);
    }

    const auto configureSlider = [this] (juce::Slider& slider, double minimum, double maximum,
                                         double interval, double value, const juce::String& suffix)
    {
        slider.setRange (minimum, maximum, interval);
        slider.setValue (value, juce::dontSendNotification);
        slider.setTextValueSuffix (suffix);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        slider.onValueChange = [this] { applyCorrection(); };
        addAndMakeVisible (slider);
    };

    const auto& settings = document.getCorrectionSettings();

    configureSlider (correctionSlider, 0.0, 100.0, 1.0, 100.0 * settings.correction, "%");
    configureSlider (transitionSlider, 0.0, 300.0, 5.0, settings.transitionMilliseconds, " ms");
    configureSlider (vibratoSlider, 0.0, 200.0, 1.0, 100.0 * settings.vibrato, "%");
    configureSlider (driftSlider, 0.0, 200.0, 1.0, 100.0 * settings.drift, "%");

    labelled = { { &keyBox, "KEY" },
                 { &scaleBox, "SCALE" },
                 { &detectorBox, "PITCH" },
                 { &correctionSlider, "CORRECTION" },
                 { &transitionSlider, "TRANSITION" },
                 { &vibratoSlider, "VIBRATO" },
                 { &driftSlider, "DRIFT" } };

    if (Product::isVoiceBuild)
        labelled.emplace_back (&voiceBox, "VOICE");
}

ToolBar::~ToolBar()
{
    document.removeListener (this);
}

void ToolBar::setTool (NoteGrid::Tool tool)
{
    selectTool.setToggleState (tool == NoteGrid::Tool::select, juce::dontSendNotification);
    drawTool.setToggleState (tool == NoteGrid::Tool::draw, juce::dontSendNotification);
    splitTool.setToggleState (tool == NoteGrid::Tool::split, juce::dontSendNotification);
    joinTool.setToggleState (tool == NoteGrid::Tool::join, juce::dontSendNotification);
}

void ToolBar::setBusy (bool isBusy)
{
    for (auto* button : { &openButton, &exportButton, &analyseButton })
        button->setEnabled (! isBusy);
}

EngineOptions ToolBar::getEngineOptions() const
{
    EngineOptions options;

    options.detector = static_cast<EngineOptions::Detector> (std::max (0, detectorBox.getSelectedItemIndex()));
    options.engine = Product::isVoiceBuild ? EngineOptions::Engine::voiceModel
                                           : EngineOptions::Engine::melVocoder;
    options.voiceName = voiceBox.getText();

    return options;
}

void ToolBar::melodyChanged()
{
    undoButton.setEnabled (document.getUndoManager().canUndo());
    redoButton.setEnabled (document.getUndoManager().canRedo());
}

void ToolBar::applyScale()
{
    document.setScale (Scale { static_cast<Scale::Type> (std::max (0, scaleBox.getSelectedItemIndex())),
                               std::max (0, keyBox.getSelectedItemIndex()) },
                       true);
}

void ToolBar::applyCorrection()
{
    auto settings = document.getCorrectionSettings();

    settings.correction = static_cast<float> (correctionSlider.getValue() / 100.0);
    settings.transitionMilliseconds = static_cast<float> (transitionSlider.getValue());
    settings.vibrato = static_cast<float> (vibratoSlider.getValue() / 100.0);
    settings.drift = static_cast<float> (driftSlider.getValue() / 100.0);

    document.setCorrectionSettings (settings);
}

void ToolBar::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::bar);

    graphics.setColour (Palette::edge);
    graphics.fillRect (0, getHeight() - 1, getWidth(), 1);

    for (const auto& [component, label] : labelled)
        PanelLookAndFeel::drawTrackedText (graphics,
                                           label,
                                           juce::Rectangle<float> (static_cast<float> (component->getX()),
                                                                   static_cast<float> (component->getY()) - 13.0f,
                                                                   static_cast<float> (component->getWidth()),
                                                                   12.0f),
                                           juce::Justification::left,
                                           TypeScale::label,
                                           Metrics::tracking,
                                           Palette::dimText);
}

void ToolBar::resized()
{
    auto bounds = getLocalBounds().reduced (Metrics::margin, 6);

    auto top = bounds.removeFromTop (24);

    const auto place = [&top] (juce::Component& component, int width)
    {
        component.setBounds (top.removeFromLeft (width).reduced (1, 0));
        top.removeFromLeft (4);
    };

    place (openButton, 66);
    place (exportButton, 76);
    top.removeFromLeft (Metrics::gap);
    place (undoButton, 60);
    place (redoButton, 60);
    top.removeFromLeft (Metrics::gap * 2);
    place (selectTool, 68);
    place (drawTool, 60);
    place (splitTool, 60);
    place (joinTool, 56);

    place (analyseButton, 84);
    top.removeFromLeft (Metrics::gap);
    place (detectorBox, 92);

    if (Product::isVoiceBuild)
        place (voiceBox, 116);

    bounds.removeFromTop (14);

    auto lower = bounds.removeFromTop (22);

    const auto placeLower = [&lower] (juce::Component& component, int width)
    {
        component.setBounds (lower.removeFromLeft (width));
        lower.removeFromLeft (Metrics::gap * 2);
    };

    placeLower (keyBox, 62);
    placeLower (scaleBox, 122);
    placeLower (correctionSlider, 178);
    placeLower (transitionSlider, 190);
    placeLower (vibratoSlider, 178);
    placeLower (driftSlider, 178);
}
}
