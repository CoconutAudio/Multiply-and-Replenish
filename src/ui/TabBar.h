#pragma once

#include "ui/IconButton.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace multiplyandreplenish
{
/** @brief One tab per edit of the take, numbered, and a plus that adds another.

    Each tab is the same audio with its own edit of the melody; playing sings all of them, so a
    second tab moved up a third is a harmony.
*/
class TabBar final : public juce::Component
{
public:
    TabBar();

    static constexpr int preferredHeight = 34;

    std::function<void (int)> onTabSelected;
    std::function<void()> onAddTab;
    std::function<void (int)> onTabClosed;

    void setTabs (int numTabs, int activeTab);
    void setAddEnabled (bool isEnabled);

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

private:
    static constexpr int tabWidth = 56;
    static constexpr int closeSize = 16;

    /** @brief The x that closes a tab; only shown while there is more than one. */
    [[nodiscard]] juce::Rectangle<int> getCloseBounds (int index) const;
    [[nodiscard]] int getTabAt (juce::Point<int> position) const;

    [[nodiscard]] juce::Rectangle<int> getTabBounds (int index) const;

    IconButton addButton { Icons::Glyph::plus, "New tab" };

    int numTabs { 0 };
    int activeTab { 0 };
    int hoveredClose { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabBar)
};
}
