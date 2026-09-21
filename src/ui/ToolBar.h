#pragma once

#include "common/Scale.h"
#include "ui/EditTool.h"
#include "ui/IconButton.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace multiplyandreplenish
{
/** @brief The bar across the top: open and save, key and scale, play, undo and redo.

    Nothing here carries a caption; the buttons are glyphs and the tooltips say what they do.
*/
class ToolBar final : public juce::Component
{
public:
    /** @brief @p canSave is false in the plug-in, where the host keeps the project. */
    explicit ToolBar (bool canSave);

    static constexpr int preferredHeight = 48;

    std::function<void()> onOpen;
    std::function<void()> onSave;
    std::function<void()> onUndo;
    std::function<void()> onRedo;
    std::function<void()> onPlayPause;
    std::function<void (EditTool)> onToolChosen;
    std::function<void (int)> onTranspose;
    std::function<void (const Scale&)> onScaleChanged;

    void setPlaying (bool isPlaying);
    void setUndoRedoEnabled (bool canUndo, bool canRedo);
    void setScale (const Scale& scale);

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void scaleBoxesChanged();
    void chooseTool (EditTool tool);

    IconButton openButton { Icons::Glyph::file, "Open" };
    IconButton saveButton { Icons::Glyph::save, "Save" };
    IconButton undoButton { Icons::Glyph::undo, "Undo" };
    IconButton redoButton { Icons::Glyph::redo, "Redo" };
    IconButton editButton { Icons::Glyph::pencil, "Edit" };
    IconButton cutButton { Icons::Glyph::scissors, "Cut" };
    IconButton transposeUpButton { Icons::Glyph::chevronUp, "Transpose up a scale step" };
    IconButton transposeDownButton { Icons::Glyph::chevronDown, "Transpose down a scale step" };
    IconButton playButton { Icons::Glyph::play, "Play" };

    juce::ComboBox keyBox;
    juce::ComboBox scaleBox;

    bool isPlayingNow { false };
    bool showsSave;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolBar)
};
}
