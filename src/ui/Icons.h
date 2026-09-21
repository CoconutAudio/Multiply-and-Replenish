#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>

namespace multiplyandreplenish::Icons
{
/** @brief The toolbar's glyphs. They are Lucide icons (ISC licence, https://lucide.dev). */
enum class Glyph
{
    file,
    save,
    undo,
    redo,
    play,
    pause,
    plus,
    close,
    pencil,
    scissors,
    chevronUp,
    chevronDown
};

namespace detail
{
    /** @brief Stands in for `currentColor`, which JUCE's SVG parser does not resolve. */
    inline juce::Colour placeholderColour() { return juce::Colour { 0xff010101 }; }

    inline const char* svgSource (Glyph glyph)
    {
        switch (glyph)
        {
            case Glyph::file:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z"/>
                    <path d="M14 2v5a1 1 0 0 0 1 1h5"/></svg>)svg";

            case Glyph::save:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/>
                    <path d="M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7"/>
                    <path d="M7 3v4a1 1 0 0 0 1 1h7"/></svg>)svg";

            case Glyph::undo:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M9 14 4 9l5-5"/>
                    <path d="M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11"/></svg>)svg";

            case Glyph::redo:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="m15 14 5-5-5-5"/>
                    <path d="M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13"/></svg>)svg";

            case Glyph::play:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#010101"
                    stroke="#010101" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z"/></svg>)svg";

            case Glyph::pause:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#010101"
                    stroke="#010101" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <rect x="14" y="3" width="5" height="18" rx="1"/>
                    <rect x="5" y="3" width="5" height="18" rx="1"/></svg>)svg";

            case Glyph::plus:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M5 12h14"/><path d="M12 5v14"/></svg>)svg";

            case Glyph::close:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg>)svg";

            case Glyph::pencil:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z"/>
                    <path d="m15 5 4 4"/></svg>)svg";

            case Glyph::scissors:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <circle cx="6" cy="6" r="3"/><path d="M8.12 8.12 12 12"/><path d="M20 4 8.12 15.88"/>
                    <circle cx="6" cy="18" r="3"/><path d="M14.8 14.8 20 20"/></svg>)svg";

            case Glyph::chevronUp:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round">
                    <path d="m18 15-6-6-6 6"/></svg>)svg";

            case Glyph::chevronDown:
                return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
                    stroke="#010101" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round">
                    <path d="m6 9 6 6 6-6"/></svg>)svg";
        }

        return "";
    }

    /** @brief Parses each glyph once; the message thread is the only caller. */
    inline const juce::Drawable* getTemplate (Glyph glyph)
    {
        static std::map<Glyph, std::unique_ptr<juce::Drawable>> cache;

        if (const auto found = cache.find (glyph); found != cache.end())
            return found->second.get();

        return (cache[glyph] = juce::Drawable::createFromSVGString (svgSource (glyph))).get();
    }
}

/** @brief Draws a glyph in @p colour, fitted and centred in @p area. */
inline void draw (juce::Graphics& graphics, Glyph glyph, juce::Rectangle<float> area, juce::Colour colour)
{
    if (const auto* drawable = detail::getTemplate (glyph))
    {
        const std::unique_ptr<juce::Drawable> instance { drawable->createCopy() };
        instance->replaceColour (detail::placeholderColour(), colour);
        instance->drawWithin (graphics, area, juce::RectanglePlacement::centred, 1.0f);
    }
}
}
