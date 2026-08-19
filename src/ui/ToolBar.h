#pragma once

#include "edit/EditDocument.h"
#include "edit/Pipeline.h"
#include "ui/NoteGrid.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvctuner
{
/** @brief The bar across the top: what to open, which tool is in hand, which key, how much
           correction, and which algorithms do the work.
*/
class ToolBar final : public juce::Component,
                      private EditDocument::Listener
{
public:
    ToolBar (EditDocument& document, Pipeline& pipeline);
    ~ToolBar() override;

    static constexpr int preferredHeight = 84;

    std::function<void()> onOpen;
    std::function<void()> onExport;
    std::function<void()> onAnalyse;
    std::function<void (NoteGrid::Tool)> onToolChosen;

    /** @brief The algorithms the user has chosen. */
    [[nodiscard]] EngineOptions getEngineOptions() const;

    void setTool (NoteGrid::Tool tool);

    /** @brief Greys out what cannot be done until a recording is analysed. */
    void setBusy (bool isBusy);

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void melodyChanged() override;

    void applyScale();
    void applyCorrection();

    EditDocument& document;
    Pipeline& pipeline;

    juce::TextButton openButton { "OPEN" };
    juce::TextButton exportButton { "EXPORT" };
    juce::TextButton undoButton { "UNDO" };
    juce::TextButton redoButton { "REDO" };
    juce::TextButton analyseButton { "ANALYSE" };

    juce::TextButton selectTool { "SELECT" };
    juce::TextButton drawTool { "DRAW" };
    juce::TextButton splitTool { "SPLIT" };
    juce::TextButton joinTool { "JOIN" };

    juce::ComboBox keyBox;
    juce::ComboBox scaleBox;
    juce::ComboBox detectorBox;
    juce::ComboBox voiceBox;

    juce::Slider correctionSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider transitionSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider vibratoSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider driftSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    std::vector<std::pair<juce::Component*, juce::String>> labelled;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToolBar)
};
}
