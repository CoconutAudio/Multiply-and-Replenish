#include "ui/PanelLookAndFeel.h"

#include "ui/Flat.h"

namespace multiplyandreplenish
{
PanelLookAndFeel::PanelLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::ground);
    setColour (juce::PopupMenu::backgroundColourId, Palette::bar);
    setColour (juce::ScrollBar::thumbColourId, Palette::edge);
    setColour (juce::PopupMenu::textColourId, Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::text);
    setColour (juce::ComboBox::textColourId, Palette::text);
    setColour (juce::ComboBox::backgroundColourId, Palette::card);
    setColour (juce::TooltipWindow::backgroundColourId, Palette::cardRaised);
    setColour (juce::TooltipWindow::textColourId, Palette::text);
    setColour (juce::TooltipWindow::outlineColourId, Palette::edge);
}

void PanelLookAndFeel::drawComboBox (juce::Graphics& graphics,
                                     int width,
                                     int height,
                                     bool isButtonDown,
                                     int,
                                     int,
                                     int,
                                     int,
                                     juce::ComboBox& box)
{
    const juce::Rectangle<float> bounds { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
    const auto isHot = box.isMouseOver (true) || isButtonDown;

    Flat::panel (graphics, bounds.reduced (0.5f), isHot ? Palette::cardRaised : Palette::card,
                 box.hasKeyboardFocus (true) ? Palette::accent : Palette::edge);

    const auto arrowCentre = juce::Point<float> { static_cast<float> (width) - 12.0f, bounds.getCentreY() };

    juce::Path arrow;
    arrow.startNewSubPath (arrowCentre.x - 3.5f, arrowCentre.y - 1.5f);
    arrow.lineTo (arrowCentre.x, arrowCentre.y + 2.0f);
    arrow.lineTo (arrowCentre.x + 3.5f, arrowCentre.y - 1.5f);

    graphics.setColour (box.isEnabled() ? Palette::dimText : Palette::dimText.withAlpha (0.4f));
    graphics.strokePath (arrow, juce::PathStrokeType { 1.5f });
}

void PanelLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font PanelLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font { juce::FontOptions { TypeScale::value } };
}

void PanelLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& graphics,
                                                           int width,
                                                           int height,
                                                           const juce::PopupMenu::Options&)
{
    const juce::Rectangle<float> bounds { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };

    Flat::panel (graphics, bounds.reduced (0.5f), Palette::bar, Palette::edge);
}

juce::Font PanelLookAndFeel::getPopupMenuFont()
{
    return juce::Font { juce::FontOptions { TypeScale::value } };
}

void PanelLookAndFeel::drawScrollbar (juce::Graphics& graphics,
                                      juce::ScrollBar&,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      bool isScrollbarVertical,
                                      int thumbStartPosition,
                                      int thumbSize,
                                      bool isMouseOver,
                                      bool isMouseDown)
{
    graphics.setColour (Palette::well);
    graphics.fillRect (x, y, width, height);

    if (thumbSize <= 0)
        return;

    const auto thumb = (isScrollbarVertical
                            ? juce::Rectangle<int> { x + 2, thumbStartPosition, width - 4, thumbSize }
                            : juce::Rectangle<int> { thumbStartPosition, y + 2, thumbSize, height - 4 })
                           .toFloat();

    graphics.setColour (isMouseDown ? Palette::accent
                        : isMouseOver ? Palette::edge.brighter (0.5f)
                                      : Palette::edge);
    graphics.fillRect (thumb);
}

void PanelLookAndFeel::drawLinearSlider (juce::Graphics& graphics,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         float sliderPosition,
                                         float,
                                         float,
                                         juce::Slider::SliderStyle,
                                         juce::Slider& slider)
{
    const juce::Rectangle<float> bounds { static_cast<float> (x),
                                          static_cast<float> (y),
                                          static_cast<float> (width),
                                          static_cast<float> (height) };

    const auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), 3.0f);

    graphics.setColour (Palette::well);
    graphics.fillRect (track);

    graphics.setColour (Palette::accent);
    graphics.fillRect (track.withRight (sliderPosition));

    const auto thumb = juce::Rectangle<float> { 6.0f, bounds.getHeight() - 4.0f }.withCentre ({ sliderPosition, bounds.getCentreY() });

    graphics.setColour (slider.isMouseOverOrDragging() ? Palette::text : Palette::dimText);
    graphics.fillRect (thumb);
}

}
