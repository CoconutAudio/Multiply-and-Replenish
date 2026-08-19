#include "edit/MidiExport.h"

namespace rvctuner
{
namespace
{
    /** @brief Ticks per quarter note, with the tempo left at the default 120, so that one second
               is one half note and the notes land where they were sung.
    */
    constexpr int ticksPerQuarterNote = 960;
    constexpr double quarterNotesPerSecond = 2.0;
}

bool writeMidiFile (const juce::File& file, const std::vector<Note>& notes, int frameRate)
{
    if (frameRate <= 0)
        return false;

    juce::MidiMessageSequence sequence;

    const auto toTicks = [frameRate] (int frameIndex)
    {
        return static_cast<double> (frameIndex) / static_cast<double> (frameRate)
             * quarterNotesPerSecond * static_cast<double> (ticksPerQuarterNote);
    };

    for (const auto& note : notes)
    {
        if (note.getNumFrames() <= 0)
            continue;

        const auto velocity = static_cast<juce::uint8> (juce::jlimit (1, 127,
            juce::roundToInt (100.0f * std::pow (10.0f, note.gainDecibels / 20.0f))));

        sequence.addEvent (juce::MidiMessage::noteOn (1, note.targetNote, velocity), toTicks (note.firstFrame));
        sequence.addEvent (juce::MidiMessage::noteOff (1, note.targetNote), toTicks (note.lastFrame));
    }

    sequence.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerQuarterNote);
    midi.addTrack (sequence);

    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream { file.createOutputStream() };

    if (stream == nullptr || ! stream->openedOk())
        return false;

    return midi.writeTo (*stream);
}
}
