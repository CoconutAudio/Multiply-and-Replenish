#pragma once

#include "edit/Note.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace rvctuner
{
/** @brief Writes the notes as a MIDI melody, at the pitches the correction put them on.
    @param file       Where to write; overwritten if it exists.
    @param notes      The notes, in time order.
    @param frameRate  The frame grid the notes are counted in.
    @return True when the file was written.
*/
[[nodiscard]] bool writeMidiFile (const juce::File& file,
                                  const std::vector<Note>& notes,
                                  int frameRate);
}
