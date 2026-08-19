#include "plugin/Modification.h"

namespace tuner
{
juce::String Modification::getError() const
{
    const juce::ScopedLock lock { errorLock };
    return errorMessage;
}

void Modification::setError (const juce::String& message)
{
    const juce::ScopedLock lock { errorLock };
    errorMessage = message;
}

void Modification::writeToArchive (juce::OutputStream& stream) const
{
    const auto& notes = document.getNotes();
    const auto& settings = document.getCorrectionSettings();
    const auto& scale = document.getScale();

    stream.writeInt (archiveVersion);

    stream.writeInt (static_cast<int> (scale.getType()));
    stream.writeInt (scale.getTonic());

    stream.writeFloat (settings.correction);
    stream.writeFloat (settings.vibrato);
    stream.writeFloat (settings.drift);
    stream.writeFloat (settings.transitionMilliseconds);

    stream.writeInt (static_cast<int> (notes.size()));

    for (const auto& note : notes)
    {
        stream.writeInt (note.firstFrame);
        stream.writeInt (note.lastFrame);
        stream.writeDouble (note.sungPitch);
        stream.writeInt (note.targetNote);
        stream.writeFloat (note.correction);
        stream.writeFloat (note.vibrato);
        stream.writeFloat (note.drift);
        stream.writeFloat (note.gainDecibels);
        stream.writeBool (note.isEnabled);
    }
}

bool Modification::readFromArchive (juce::InputStream& stream)
{
    if (stream.readInt() != archiveVersion)
        return false;

    EditDocument::State restored;

    const auto scaleType = static_cast<Scale::Type> (stream.readInt());
    restored.scale = Scale { scaleType, stream.readInt() };

    restored.correction.correction = stream.readFloat();
    restored.correction.vibrato = stream.readFloat();
    restored.correction.drift = stream.readFloat();
    restored.correction.transitionMilliseconds = stream.readFloat();

    const auto numNotes = stream.readInt();

    if (numNotes < 0 || numNotes > 1'000'000)
        return false;

    restored.notes.reserve (static_cast<std::size_t> (numNotes));

    for (int index = 0; index < numNotes; ++index)
    {
        Note note;
        note.firstFrame = stream.readInt();
        note.lastFrame = stream.readInt();
        note.sungPitch = stream.readDouble();
        note.targetNote = stream.readInt();
        note.correction = stream.readFloat();
        note.vibrato = stream.readFloat();
        note.drift = stream.readFloat();
        note.gainDecibels = stream.readFloat();
        note.isEnabled = stream.readBool();

        restored.notes.push_back (note);
    }

    restored.drawn = document.getDrawnPitch();
    document.setState (std::move (restored));

    return true;
}

void Modification::readAndDiscard (juce::InputStream& stream)
{
    if (stream.readInt() != archiveVersion)
        return;

    stream.readInt();
    stream.readInt();
    stream.readFloat();
    stream.readFloat();
    stream.readFloat();
    stream.readFloat();

    const auto numNotes = stream.readInt();

    for (int index = 0; index < numNotes && ! stream.isExhausted(); ++index)
    {
        stream.readInt();
        stream.readInt();
        stream.readDouble();
        stream.readInt();
        stream.readFloat();
        stream.readFloat();
        stream.readFloat();
        stream.readFloat();
        stream.readBool();
    }
}
}
