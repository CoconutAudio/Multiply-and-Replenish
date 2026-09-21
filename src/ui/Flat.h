#pragma once

#include <juce_graphics/juce_graphics.h>

/** @brief The look is flat: square edges, solid fills and one-pixel lines, no gradients or glow. */
namespace multiplyandreplenish::Flat
{
/** @brief A solid block with a one-pixel rim of its own colour, a little brighter. */
inline void block (juce::Graphics& graphics, juce::Rectangle<float> bounds, juce::Colour colour)
{
    graphics.setColour (colour);
    graphics.fillRect (bounds);

    graphics.setColour (colour.brighter (0.35f));
    graphics.drawRect (bounds, 1.0f);
}

/** @brief A dark panel with a hairline edge. */
inline void panel (juce::Graphics& graphics, juce::Rectangle<float> bounds, juce::Colour fill, juce::Colour edge)
{
    graphics.setColour (fill);
    graphics.fillRect (bounds);

    graphics.setColour (edge);
    graphics.drawRect (bounds, 1.0f);
}

/** @brief A thin vertical line, for the playhead and the cut guide. */
inline void verticalLine (juce::Graphics& graphics, float x, float top, float bottom, juce::Colour colour,
                          float width = 1.5f)
{
    graphics.setColour (colour);
    graphics.fillRect (x - width * 0.5f, top, width, bottom - top);
}
}
