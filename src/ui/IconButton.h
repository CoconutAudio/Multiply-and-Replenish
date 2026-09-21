#pragma once

#include "ui/Flat.h"
#include "ui/Icons.h"
#include "ui/PanelLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace multiplyandreplenish
{
/** @brief A button drawn as a glyph and nothing else; the tooltip names it.

    A tool button that is chosen fills white.
*/
class IconButton final : public juce::Button
{
public:
    explicit IconButton (Icons::Glyph glyphToDraw, const juce::String& tooltip = {})
        : juce::Button ({}),
          glyph (glyphToDraw)
    {
        setTooltip (tooltip);
    }

    void setGlyph (Icons::Glyph newGlyph)
    {
        if (glyph == newGlyph)
            return;

        glyph = newGlyph;
        repaint();
    }

private:
    using Palette = PanelLookAndFeel::Palette;

    void paintButton (juce::Graphics& graphics, bool isHighlighted, bool isDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        const auto isLit = getToggleState();

        if (isLit)
            Flat::block (graphics, bounds, Palette::accent);
        else if (isEnabled() && (isHighlighted || isDown))
            Flat::panel (graphics, bounds, isDown ? Palette::cardRaised : Palette::card, Palette::edge);

        Icons::draw (graphics, glyph, bounds.reduced (bounds.getWidth() * 0.2f),
                     ! isEnabled() ? Palette::dimText.withAlpha (0.4f)
                                   : (isLit ? Palette::ground : Palette::text));
    }

    Icons::Glyph glyph;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};
}
