#include "common/EditDocument.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace multiplyandreplenish;

namespace
{
constexpr int frameRate = 100;

float toFrequency (double semitones)
{
    return static_cast<float> (440.0 * std::exp2 ((semitones - 69.0) / 12.0));
}

PitchTrack makeMelody (const std::vector<double>& semitones, int framesPerNote)
{
    PitchTrack melody;
    melody.frameRate = frameRate;

    for (const auto pitch : semitones)
        for (int frameIndex = 0; frameIndex < framesPerNote; ++frameIndex)
        {
            melody.fundamentalFrequencyHz.push_back (toFrequency (pitch));
            melody.confidence.push_back (0.9f);
        }

    return melody;
}

/** @brief A document holding three held notes, one of them 70 cents flat of E. */
std::unique_ptr<EditDocument> makeDocument()
{
    auto document = std::make_unique<EditDocument>();

    juce::AudioBuffer<float> audio { 1, 3 * 50 * 441 };
    audio.clear();

    document->setRecording (std::move (audio), 44100.0, {});
    document->setMelody (makeMelody ({ 60.0, 63.7, 67.0 }, 50));

    return document;
}

/** @brief Moves a note the way dragging it does. */
void nudge (EditDocument& document, int index, int semitones)
{
    document.modifyNotes ({ index },
                          [semitones] (Note& note)
                          {
                              note.semitones += static_cast<float> (semitones);
                              note.lastEditedSemitones = note.semitones;
                          },
                          "move");
}
}

TEST (EditDocumentTest, AnalysisFindsTheNotes)
{
    const auto document = makeDocument();

    ASSERT_EQ (document->getNotes().size(), 3u);
    EXPECT_NEAR (document->getNotes()[1].semitones, 63.7f, 0.05f);
    EXPECT_EQ (document->getNumFrames(), 150);
}

TEST (EditDocumentTest, AnalysingATakeLeavesItExactlyAsPlayed)
{
    const auto document = makeDocument();

    const auto& composed = document->getComposedMelody();

    ASSERT_EQ (composed.composedSemitones.size(), composed.sungSemitones.size());

    for (std::size_t frameIndex = 0; frameIndex < composed.composedSemitones.size(); ++frameIndex)
        ASSERT_NEAR (composed.composedSemitones[frameIndex], composed.sungSemitones[frameIndex], 1.0e-3f);

    for (const auto& note : document->getNotes())
        EXPECT_TRUE (note.isAsPlayed());
}

TEST (EditDocumentTest, MovingANoteMovesTheMelody)
{
    auto document = makeDocument();

    nudge (*document, 0, 2);

    EXPECT_NEAR (document->getNotes()[0].semitones, 62.0f, 1.0e-4f);
    EXPECT_NEAR (document->getComposedMelody().composedSemitones[25], 62.0f, 0.05f);

    EXPECT_NEAR (document->getComposedMelody().composedSemitones[75], 63.7f, 0.05f);
}

TEST (EditDocumentTest, MovingANoteCarriesWhatWasPlayedAroundIt)
{
    auto document = std::make_unique<EditDocument>();

    juce::AudioBuffer<float> audio { 1, 100 * 441 };
    audio.clear();

    PitchTrack melody;
    melody.frameRate = frameRate;

    for (int frameIndex = 0; frameIndex < 100; ++frameIndex)
    {
        const auto wobble = 0.5 * std::sin (2.0 * 3.14159265358979 * 6.0 * frameIndex / frameRate);
        melody.fundamentalFrequencyHz.push_back (toFrequency (60.0 + wobble));
        melody.confidence.push_back (0.9f);
    }

    document->setRecording (std::move (audio), 44100.0, {});
    document->setMelody (std::move (melody));

    ASSERT_EQ (document->getNotes().size(), 1u);

    const auto before = document->getComposedMelody().composedSemitones;

    nudge (*document, 0, 5);

    const auto after = document->getComposedMelody().composedSemitones;

    for (int frameIndex = 20; frameIndex < 80; ++frameIndex)
        ASSERT_NEAR (after[static_cast<std::size_t> (frameIndex)],
                     before[static_cast<std::size_t> (frameIndex)] + 5.0f, 1.0e-3f);
}

TEST (EditDocumentTest, FlatteningTheVibratoLeavesTheNoteWhereItWas)
{
    auto document = makeDocument();

    document->modifyNotes ({ 1 }, [] (Note& note) { note.vibrato = 0.0f; }, "flatten");

    EXPECT_NEAR (document->getComposedMelody().composedSemitones[75], 63.7f, 0.05f);
    EXPECT_NEAR (document->getComposedMelody().deviationSemitones[75], 0.0f, 1.0e-4f);
}

TEST (EditDocumentTest, EveryEditCanBeUndone)
{
    auto document = makeDocument();

    const auto before = document->getNotes()[0].semitones;

    nudge (*document, 0, 3);
    EXPECT_NE (document->getNotes()[0].semitones, before);

    document->getUndoManager().undo();
    EXPECT_EQ (document->getNotes()[0].semitones, before);

    document->getUndoManager().redo();
    EXPECT_NEAR (document->getNotes()[0].semitones, before + 3.0f, 1.0e-4f);
}

TEST (EditDocumentTest, TheNoteUnderAFrameIsFound)
{
    const auto document = makeDocument();

    EXPECT_EQ (document->getNoteAt (25), 0);
    EXPECT_EQ (document->getNoteAt (75), 1);
    EXPECT_EQ (document->getNoteAt (5000), -1);
}

TEST (EditDocumentTest, LevelsReachTheRenderedGain)
{
    auto document = makeDocument();

    document->modifyNotes ({ 0 }, [] (Note& note) { note.gainDecibels = 6.0f; }, "level");

    EXPECT_GT (document->getComposedMelody().gain[25], 1.9f);
    EXPECT_NEAR (document->getComposedMelody().gain[125], 1.0f, 0.01f);
}

TEST (EditDocumentTest, TheScaleIsNotAnEditToTheNotes)
{
    auto document = makeDocument();

    document->setScale (Scale { Scale::Type::majorPentatonic, 0 });

    EXPECT_EQ (document->getScale().getType(), Scale::Type::majorPentatonic);
    EXPECT_FALSE (document->getUndoManager().canUndo());
    EXPECT_NEAR (document->getNotes()[1].semitones, 63.7f, 0.05f);
}

TEST (EditDocumentTest, RestoringAStateReplacesTheNotesWithoutAnUndoStep)
{
    auto document = makeDocument();

    auto state = document->getState();
    state.notes[0].semitones += 2.0f;
    document->setState (std::move (state));

    EXPECT_NEAR (document->getNotes()[0].semitones, 62.0f, 0.05f);
    EXPECT_FALSE (document->getUndoManager().canUndo());
}

TEST (EditDocumentTest, CuttingANoteMakesTwoWithTheSameEdits)
{
    auto document = makeDocument();
    nudge (*document, 1, 2);

    const auto before = document->getNotes()[1];
    document->splitNote (1, before.firstFrame + 20);

    const auto& notes = document->getNotes();
    ASSERT_EQ (notes.size(), 4u);
    EXPECT_EQ (notes[1].lastFrame, notes[2].firstFrame);
    EXPECT_EQ (notes[1].firstFrame, before.firstFrame);
    EXPECT_EQ (notes[2].lastFrame, before.lastFrame);
    EXPECT_EQ (notes[1].sourceLastFrame, notes[2].sourceFirstFrame);
    EXPECT_FLOAT_EQ (notes[1].semitones, before.semitones);
    EXPECT_FLOAT_EQ (notes[2].semitones, before.semitones);
    EXPECT_EQ (static_cast<int> (notes[1].deviation.size()) + static_cast<int> (notes[2].deviation.size()),
               static_cast<int> (before.deviation.size()));
}

TEST (EditDocumentTest, CuttingAtAnEdgeDoesNothing)
{
    auto document = makeDocument();
    const auto note = document->getNotes()[0];

    document->splitNote (0, note.firstFrame);
    document->splitNote (0, note.lastFrame);
    document->splitNote (7, 10);

    EXPECT_EQ (document->getNotes().size(), 3u);
}

TEST (EditDocumentTest, CuttingIsOneUndoStep)
{
    auto document = makeDocument();

    document->splitNote (0, 20);
    ASSERT_EQ (document->getNotes().size(), 4u);

    document->getUndoManager().undo();
    EXPECT_EQ (document->getNotes().size(), 3u);
}

TEST (EditDocumentTest, TransposingMovesEveryNoteAlongTheScaleInOneUndoStep)
{
    auto document = makeDocument();
    document->setScale (Scale { Scale::Type::major, 0 });

    document->transposeByScaleSteps (1);

    const auto& notes = document->getNotes();
    ASSERT_EQ (notes.size(), 3u);
    EXPECT_FLOAT_EQ (notes[0].semitones, 62.0f);
    EXPECT_NEAR (notes[1].semitones, 64.7f, 0.05f);
    EXPECT_FLOAT_EQ (notes[2].semitones, 69.0f);
    EXPECT_FLOAT_EQ (notes[0].lastEditedSemitones, 62.0f);

    document->getUndoManager().undo();
    EXPECT_NEAR (document->getNotes()[0].semitones, 60.0f, 0.05f);
    EXPECT_NEAR (document->getNotes()[1].semitones, 63.7f, 0.05f);
}
