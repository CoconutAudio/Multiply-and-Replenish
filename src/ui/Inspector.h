#pragma once

#include "edit/EditDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvctuner
{
/** @brief What the selected notes are doing, and the dials that change it for all of them at once. */
class Inspector final : public juce::Component,
                        private EditDocument::Listener
{
public:
    explicit Inspector (EditDocument& document);
    ~Inspector() override;

    static constexpr int preferredHeight = 64;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void melodyChanged() override;
    void selectionChanged() override;

    void refresh();

    [[nodiscard]] std::vector<int> getSelectedIndices() const;

    void applyToSelection (std::function<void (Note&)> change, const juce::String& actionName);

    EditDocument& document;

    juce::Slider correctionSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider vibratoSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider driftSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider gainSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::ToggleButton asSungButton { "Leave as sung" };

    juce::Slider detailSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::String summary { "no note selected" };

    bool isRefreshing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Inspector)
};
}
