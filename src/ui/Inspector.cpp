#include "ui/Inspector.h"

#include "ui/PanelLookAndFeel.h"

#include <cmath>

namespace tuner
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;
}

Inspector::Inspector (EditDocument& documentToEdit)
    : document (documentToEdit)
{
    setOpaque (true);
    document.addListener (this);

    const auto configure = [this] (juce::Slider& slider, double minimum, double maximum,
                                   double interval, const juce::String& suffix,
                                   std::function<void (Note&, double)> apply,
                                   const juce::String& actionName)
    {
        slider.setRange (minimum, maximum, interval);
        slider.setTextValueSuffix (suffix);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        slider.onValueChange = [this, &slider, apply, actionName]
        {
            if (isRefreshing)
                return;

            const auto value = slider.getValue();
            applyToSelection ([apply, value] (Note& note) { apply (note, value); }, actionName);
        };

        addAndMakeVisible (slider);
    };

    configure (correctionSlider, 0.0, 100.0, 1.0, "%",
               [] (Note& note, double value) { note.correction = static_cast<float> (value / 100.0); },
               "correct the note");

    configure (vibratoSlider, 0.0, 200.0, 1.0, "%",
               [] (Note& note, double value) { note.vibrato = static_cast<float> (value / 100.0); },
               "change the vibrato");

    configure (driftSlider, 0.0, 200.0, 1.0, "%",
               [] (Note& note, double value) { note.drift = static_cast<float> (value / 100.0); },
               "change the drift");

    configure (gainSlider, -12.0, 12.0, 0.5, " dB",
               [] (Note& note, double value) { note.gainDecibels = static_cast<float> (value); },
               "change the level");

    asSungButton.onClick = [this]
    {
        if (isRefreshing)
            return;

        const auto shouldLeave = asSungButton.getToggleState();
        applyToSelection ([shouldLeave] (Note& note) { note.isEnabled = ! shouldLeave; },
                          shouldLeave ? "leave as sung" : "correct the note");
    };

    addAndMakeVisible (asSungButton);

    detailSlider.setRange (40.0, 400.0, 5.0);
    detailSlider.setValue (document.getSegmenterSettings().minimumNoteMilliseconds,
                           juce::dontSendNotification);
    detailSlider.setTextValueSuffix (" ms");
    detailSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    detailSlider.onValueChange = [this]
    {
        if (isRefreshing)
            return;

        auto settings = document.getSegmenterSettings();
        settings.minimumNoteMilliseconds = detailSlider.getValue();
        document.setSegmenterSettings (settings);
    };

    addAndMakeVisible (detailSlider);

    refresh();
}

Inspector::~Inspector()
{
    document.removeListener (this);
}

std::vector<int> Inspector::getSelectedIndices() const
{
    std::vector<int> indices;

    const auto& selection = document.getSelection();

    for (int range = 0; range < selection.getNumRanges(); ++range)
        for (auto index = selection.getRange (range).getStart(); index < selection.getRange (range).getEnd(); ++index)
            indices.push_back (index);

    return indices;
}

void Inspector::applyToSelection (std::function<void (Note&)> change, const juce::String& actionName)
{
    document.modifyNotes (getSelectedIndices(), std::move (change), actionName);
}

void Inspector::melodyChanged() { refresh(); }
void Inspector::selectionChanged() { refresh(); }

void Inspector::refresh()
{
    const juce::ScopedValueSetter<bool> guard { isRefreshing, true };

    const auto indices = getSelectedIndices();
    const auto& notes = document.getNotes();

    const auto hasSelection = ! indices.empty();

    const std::initializer_list<juce::Component*> controls { &correctionSlider, &vibratoSlider,
                                                            &driftSlider, &gainSlider, &asSungButton };

    for (auto* component : controls)
        component->setEnabled (hasSelection);

    if (! hasSelection)
    {
        summary = notes.empty() ? "nothing analysed yet"
                                : juce::String (notes.size()) + " notes, none selected";
        repaint();
        return;
    }

    const auto& first = notes[static_cast<std::size_t> (indices.front())];

    correctionSlider.setValue (100.0 * first.correction, juce::dontSendNotification);
    vibratoSlider.setValue (100.0 * first.vibrato, juce::dontSendNotification);
    driftSlider.setValue (100.0 * first.drift, juce::dontSendNotification);
    gainSlider.setValue (first.gainDecibels, juce::dontSendNotification);
    asSungButton.setToggleState (! first.isEnabled, juce::dontSendNotification);

    if (indices.size() == 1)
    {
        const auto cents = juce::roundToInt (100.0 * first.getError());

        summary = Scale::getNoteName (first.targetNote) + "   sung "
                + juce::String (cents > 0 ? "+" : "") + juce::String (cents) + " cents   "
                + juce::String (document.getTimeForFrame (first.getNumFrames()), 2) + " s";
    }
    else
    {
        summary = juce::String (indices.size()) + " notes selected";
    }

    repaint();
}

void Inspector::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::bar);

    graphics.setColour (Palette::edge);
    graphics.fillRect (0, 0, getWidth(), 1);

    PanelLookAndFeel::drawTrackedText (graphics,
                                       summary.toUpperCase(),
                                       juce::Rectangle<float> (static_cast<float> (Metrics::margin),
                                                               8.0f, 320.0f, 16.0f),
                                       juce::Justification::left,
                                       TypeScale::value,
                                       Metrics::tracking,
                                       Palette::text);

    const std::pair<const juce::Component*, juce::String> labels[] = {
        { &correctionSlider, "CORRECTION" },
        { &vibratoSlider, "VIBRATO" },
        { &driftSlider, "DRIFT" },
        { &gainSlider, "LEVEL" },
        { &detailSlider, "SHORTEST NOTE" }
    };

    for (const auto& [component, label] : labels)
        PanelLookAndFeel::drawTrackedText (graphics,
                                           label,
                                           juce::Rectangle<float> (static_cast<float> (component->getX()),
                                                                   static_cast<float> (component->getY()) - 13.0f,
                                                                   static_cast<float> (component->getWidth()),
                                                                   12.0f),
                                           juce::Justification::left,
                                           TypeScale::label,
                                           Metrics::tracking,
                                           Palette::dimText);
}

void Inspector::resized()
{
    auto bounds = getLocalBounds().reduced (Metrics::margin, 8);

    bounds.removeFromLeft (330);
    bounds.removeFromTop (18);

    auto row = bounds.removeFromTop (22);

    const auto place = [&row] (juce::Component& component, int width)
    {
        component.setBounds (row.removeFromLeft (width));
        row.removeFromLeft (Metrics::gap * 2);
    };

    place (correctionSlider, 170);
    place (vibratoSlider, 170);
    place (driftSlider, 170);
    place (gainSlider, 160);
    place (asSungButton, 132);
    place (detailSlider, 172);
}
}
