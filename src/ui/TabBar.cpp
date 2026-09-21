#include "ui/TabBar.h"

#include "ui/Flat.h"
#include "ui/PanelLookAndFeel.h"

namespace multiplyandreplenish
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using Metrics = PanelLookAndFeel::Metrics;
    using TypeScale = PanelLookAndFeel::TypeScale;

    constexpr int addButtonSize = 28;
}

TabBar::TabBar()
{
    setOpaque (true);

    addButton.onClick = [this] { if (onAddTab != nullptr) onAddTab(); };
    addAndMakeVisible (addButton);
}

void TabBar::setTabs (int newNumTabs, int newActiveTab)
{
    numTabs = newNumTabs;
    activeTab = newActiveTab;
    hoveredClose = -1;
    resized();
    repaint();
}

void TabBar::setAddEnabled (bool isEnabled)
{
    addButton.setEnabled (isEnabled);
}

juce::Rectangle<int> TabBar::getTabBounds (int index) const
{
    return { Metrics::margin + index * tabWidth, 0, tabWidth, getHeight() };
}

void TabBar::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);

    graphics.setColour (Palette::rule);
    graphics.fillRect (0, getHeight() - 1, getWidth(), 1);

    graphics.setFont (juce::FontOptions { TypeScale::value });

    for (int index = 0; index < numTabs; ++index)
    {
        const auto bounds = getTabBounds (index).toFloat().reduced (3.0f, 4.0f);
        const auto hue = Palette::tab (index);

        if (index == activeTab)
        {
            Flat::block (graphics, bounds, hue);
            graphics.setColour (Palette::ground);
        }
        else
        {
            Flat::panel (graphics, bounds, Palette::card, hue.withAlpha (0.6f));
            graphics.setColour (hue.brighter (0.4f));
        }

        const auto closable = numTabs > 1;

        graphics.drawText (juce::String (index + 1),
                           closable ? bounds.withTrimmedRight (static_cast<float> (closeSize)) : bounds,
                           juce::Justification::centred, false);

        if (closable)
        {
            const auto closeBounds = getCloseBounds (index).toFloat();
            const auto isHovered = index == hoveredClose;
            const auto base = index == activeTab ? Palette::ground : hue.brighter (0.4f);

            if (isHovered)
                Flat::block (graphics, closeBounds, base.withAlpha (0.25f));

            Icons::draw (graphics, Icons::Glyph::close, closeBounds.reduced (3.5f),
                         isHovered ? base : base.withAlpha (0.65f));
        }
    }
}

juce::Rectangle<int> TabBar::getCloseBounds (int index) const
{
    const auto chip = getTabBounds (index).reduced (3, 4);
    return { chip.getRight() - closeSize - 2, chip.getCentreY() - closeSize / 2, closeSize, closeSize };
}

int TabBar::getTabAt (juce::Point<int> position) const
{
    for (int index = 0; index < numTabs; ++index)
        if (getTabBounds (index).contains (position))
            return index;

    return -1;
}

void TabBar::resized()
{
    addButton.setBounds (juce::Rectangle<int> (addButtonSize, addButtonSize)
                             .withCentre ({ getTabBounds (numTabs).getX() + addButtonSize / 2 + Metrics::gap,
                                            getHeight() / 2 }));
}

void TabBar::mouseDown (const juce::MouseEvent& event)
{
    const auto index = getTabAt (event.getPosition());

    if (index < 0)
        return;

    const auto closable = numTabs > 1;

    if (closable && (event.mods.isMiddleButtonDown() || getCloseBounds (index).contains (event.getPosition())))
    {
        if (onTabClosed != nullptr)
            onTabClosed (index);

        return;
    }

    if (onTabSelected != nullptr)
        onTabSelected (index);
}

void TabBar::mouseMove (const juce::MouseEvent& event)
{
    const auto index = getTabAt (event.getPosition());
    const auto hovered = index >= 0 && numTabs > 1 && getCloseBounds (index).contains (event.getPosition())
                             ? index : -1;

    if (hovered != hoveredClose)
    {
        hoveredClose = hovered;
        repaint();
    }
}

void TabBar::mouseExit (const juce::MouseEvent&)
{
    if (hoveredClose != -1)
    {
        hoveredClose = -1;
        repaint();
    }
}
}
