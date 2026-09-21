#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace multiplyandreplenish
{
/** @brief The flat house style: solid fills, hairline edges, white for every control, colour only for the tabs, no gradients.

    Flat and dark, neutral greys with one saturated colour per tab. Every control is drawn through
    this, so state is carried by fill and colour alone.
*/
class PanelLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();

    struct Palette
    {
        inline static const juce::Colour ground { 0xff101012 };
        inline static const juce::Colour bar { 0xff161618 };
        inline static const juce::Colour card { 0xff202023 };
        inline static const juce::Colour cardRaised { 0xff2a2a2e };
        inline static const juce::Colour well { 0xff0b0b0c };
        inline static const juce::Colour edge { 0xff333338 };
        inline static const juce::Colour rule { 0xff1c1c1f };

        inline static const juce::Colour text { 0xfff0f0f2 };
        inline static const juce::Colour dimText { 0xff8a8a92 };

        inline static const juce::Colour accent { 0xffffffff };
        inline static const juce::Colour alert { 0xffff4d5e };

        inline static const juce::Colour silhouette { 0xff2a2a2e };
        inline static const juce::Colour sungCurve { 0xff77777f };
        inline static const juce::Colour whiteKey { 0xff1e1e21 };
        inline static const juce::Colour blackKey { 0xff121214 };
        inline static const juce::Colour blackKeyRow { 0xff0d0d0f };

        /** @brief The hue of a tab; it colours the tab, its notes and its curve, and cycles past eight. */
        static juce::Colour tab (int index)
        {
            static const juce::Colour hues[] = { juce::Colour { 0xffb08cff }, juce::Colour { 0xffff4fa3 },
                                                 juce::Colour { 0xff2fd8ff }, juce::Colour { 0xff3dff9a },
                                                 juce::Colour { 0xffffb23d }, juce::Colour { 0xffff5c5c },
                                                 juce::Colour { 0xff5c8cff }, juce::Colour { 0xffc65cff } };

            return hues[static_cast<std::size_t> (((index % 8) + 8) % 8)];
        }
    };

    struct TypeScale
    {
        static constexpr float value = 12.5f;
        static constexpr float label = 10.0f;
        static constexpr float caption = 11.5f;
    };

    struct Metrics
    {
        static constexpr int margin = 10;
        static constexpr int gap = 8;
        static constexpr float hairline = 1.0f;
            };

    void drawComboBox (juce::Graphics& graphics,
                       int width,
                       int height,
                       bool isButtonDown,
                       int buttonX,
                       int buttonY,
                       int buttonWidth,
                       int buttonHeight,
                       juce::ComboBox& box) override;

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override;
    juce::Font getComboBoxFont (juce::ComboBox& box) override;

    void drawPopupMenuBackgroundWithOptions (juce::Graphics& graphics,
                                             int width,
                                             int height,
                                             const juce::PopupMenu::Options& options) override;
    juce::Font getPopupMenuFont() override;

    void drawScrollbar (juce::Graphics& graphics,
                        juce::ScrollBar& scrollBar,
                        int x,
                        int y,
                        int width,
                        int height,
                        bool isScrollbarVertical,
                        int thumbStartPosition,
                        int thumbSize,
                        bool isMouseOver,
                        bool isMouseDown) override;

    void drawLinearSlider (juce::Graphics& graphics,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPosition,
                           float minSliderPosition,
                           float maxSliderPosition,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override;
};
}
