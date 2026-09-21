#include "ui/ToolBar.h"

#include "ui/PanelLookAndFeel.h"

#include <algorithm>

namespace multiplyandreplenish
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using Metrics = PanelLookAndFeel::Metrics;

    constexpr int buttonSize = 36;
    constexpr int comboHeight = 28;
}

ToolBar::ToolBar (bool canSave)
    : showsSave (canSave)
{
    setOpaque (true);

    for (auto* button : { &openButton, &undoButton, &redoButton, &playButton, &editButton, &cutButton,
                          &transposeUpButton, &transposeDownButton })
        addAndMakeVisible (button);

    if (showsSave)
        addAndMakeVisible (saveButton);

    openButton.onClick = [this] { if (onOpen != nullptr) onOpen(); };
    saveButton.onClick = [this] { if (onSave != nullptr) onSave(); };
    undoButton.onClick = [this] { if (onUndo != nullptr) onUndo(); };
    redoButton.onClick = [this] { if (onRedo != nullptr) onRedo(); };
    playButton.onClick = [this] { if (onPlayPause != nullptr) onPlayPause(); };
    editButton.onClick = [this] { chooseTool (EditTool::edit); };
    cutButton.onClick = [this] { chooseTool (EditTool::cut); };
    transposeUpButton.onClick = [this] { if (onTranspose != nullptr) onTranspose (1); };
    transposeDownButton.onClick = [this] { if (onTranspose != nullptr) onTranspose (-1); };

    editButton.setToggleState (true, juce::dontSendNotification);

    keyBox.addItemList (Scale::getPitchClassNames(), 1);
    keyBox.setSelectedItemIndex (0, juce::dontSendNotification);
    keyBox.setTooltip ("Key");
    keyBox.onChange = [this] { scaleBoxesChanged(); };
    addAndMakeVisible (keyBox);

    scaleBox.addItemList (Scale::getTypeNames(), 1);
    scaleBox.setSelectedItemIndex (0, juce::dontSendNotification);
    scaleBox.setTooltip ("Scale");
    scaleBox.onChange = [this] { scaleBoxesChanged(); };
    addAndMakeVisible (scaleBox);

    setUndoRedoEnabled (false, false);
}

void ToolBar::setPlaying (bool isPlaying)
{
    if (isPlayingNow == isPlaying)
        return;

    isPlayingNow = isPlaying;
    playButton.setGlyph (isPlaying ? Icons::Glyph::pause : Icons::Glyph::play);
    playButton.setTooltip (isPlaying ? "Pause" : "Play");
}

void ToolBar::setUndoRedoEnabled (bool canUndo, bool canRedo)
{
    undoButton.setEnabled (canUndo);
    redoButton.setEnabled (canRedo);
}

void ToolBar::setScale (const Scale& scale)
{
    keyBox.setSelectedItemIndex (scale.getTonic(), juce::dontSendNotification);
    scaleBox.setSelectedItemIndex (static_cast<int> (scale.getType()), juce::dontSendNotification);
}

void ToolBar::chooseTool (EditTool tool)
{
    editButton.setToggleState (tool == EditTool::edit, juce::dontSendNotification);
    cutButton.setToggleState (tool == EditTool::cut, juce::dontSendNotification);

    if (onToolChosen != nullptr)
        onToolChosen (tool);
}

void ToolBar::scaleBoxesChanged()
{
    if (onScaleChanged != nullptr)
        onScaleChanged (Scale { static_cast<Scale::Type> (std::max (0, scaleBox.getSelectedItemIndex())),
                                std::max (0, keyBox.getSelectedItemIndex()) });
}

void ToolBar::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::bar);

    graphics.setColour (Palette::edge);
    graphics.fillRect (0, getHeight() - 1, getWidth(), 1);
}

void ToolBar::resized()
{
    auto bounds = getLocalBounds().reduced (Metrics::margin, 0);

    playButton.setBounds (juce::Rectangle<int> (buttonSize + 8, buttonSize).withCentre (getLocalBounds().getCentre()));

    auto right = bounds.removeFromRight (buttonSize * 2 + Metrics::gap);
    redoButton.setBounds (right.removeFromRight (buttonSize).withSizeKeepingCentre (buttonSize, buttonSize));
    right.removeFromRight (Metrics::gap);
    undoButton.setBounds (right.removeFromRight (buttonSize).withSizeKeepingCentre (buttonSize, buttonSize));

    openButton.setBounds (bounds.removeFromLeft (buttonSize).withSizeKeepingCentre (buttonSize, buttonSize));

    if (showsSave)
    {
        bounds.removeFromLeft (Metrics::gap);
        saveButton.setBounds (bounds.removeFromLeft (buttonSize).withSizeKeepingCentre (buttonSize, buttonSize));
    }

    bounds.removeFromLeft (Metrics::margin + Metrics::gap);

    for (auto* button : { &editButton, &cutButton })
        button->setBounds (bounds.removeFromLeft (buttonSize).withSizeKeepingCentre (buttonSize, buttonSize));

    bounds.removeFromLeft (Metrics::margin);

    for (auto* button : { &transposeUpButton, &transposeDownButton })
        button->setBounds (bounds.removeFromLeft (buttonSize).withSizeKeepingCentre (buttonSize, buttonSize));

    bounds.removeFromLeft (Metrics::margin + Metrics::gap);
    keyBox.setBounds (bounds.removeFromLeft (64).withSizeKeepingCentre (64, comboHeight));
    bounds.removeFromLeft (Metrics::gap);
    scaleBox.setBounds (bounds.removeFromLeft (150).withSizeKeepingCentre (150, comboHeight));
}
}
