#include "edit/EditDocument.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace rvctuner;

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

/** @brief A document holding three held notes, one of them flat. */
std::unique_ptr<EditDocument> makeDocument()
{
    auto document = std::make_unique<EditDocument>();

    juce::AudioBuffer<float> audio { 1, 3 * 50 * 441 };
    audio.clear();

    document->setRecording (std::move (audio), 44100.0, {});
    document->setMelody (makeMelody ({ 60.0, 63.7, 67.0 }, 50));

    return document;
}
}

TEST (EditDocumentTest, AnalysisFindsTheNotes)
{
    const auto document = makeDocument();

    ASSERT_EQ (document->getNotes().size(), 3u);
    EXPECT_EQ (document->getNotes()[1].targetNote, 64);
    EXPECT_EQ (document->getNumFrames(), 150);
}

TEST (EditDocumentTest, MovingANoteMovesTheMelody)
{
    auto document = makeDocument();

    document->nudgeNotes ({ 0 }, 2);

    EXPECT_EQ (document->getNotes()[0].targetNote, 62);
    EXPECT_NEAR (document->getCorrectedMelody().correctedSemitones[25], 62.0, 0.05);
}

TEST (EditDocumentTest, EveryEditCanBeUndone)
{
    auto document = makeDocument();

    const auto before = document->getNotes()[0].targetNote;

    document->nudgeNotes ({ 0 }, 3);
    EXPECT_NE (document->getNotes()[0].targetNote, before);

    document->getUndoManager().undo();
    EXPECT_EQ (document->getNotes()[0].targetNote, before);

    document->getUndoManager().redo();
    EXPECT_EQ (document->getNotes()[0].targetNote, before + 3);
}

TEST (EditDocumentTest, SplittingMakesTwoNotesThatCoverTheSameFrames)
{
    auto document = makeDocument();

    const auto first = document->getNotes()[0];
    document->splitNote (0, first.firstFrame + 20);

    ASSERT_EQ (document->getNotes().size(), 4u);
    EXPECT_EQ (document->getNotes()[0].firstFrame, first.firstFrame);
    EXPECT_EQ (document->getNotes()[0].lastFrame, first.firstFrame + 20);
    EXPECT_EQ (document->getNotes()[1].firstFrame, first.firstFrame + 20);
    EXPECT_EQ (document->getNotes()[1].lastFrame, first.lastFrame);
}

TEST (EditDocumentTest, JoiningKeepsTheOuterEdges)
{
    auto document = makeDocument();

    const auto firstFrame = document->getNotes()[0].firstFrame;
    const auto lastFrame = document->getNotes()[1].lastFrame;

    document->mergeNotes ({ 0, 1 });

    ASSERT_EQ (document->getNotes().size(), 2u);
    EXPECT_EQ (document->getNotes()[0].firstFrame, firstFrame);
    EXPECT_EQ (document->getNotes()[0].lastFrame, lastFrame);
}

TEST (EditDocumentTest, ANoteLeftAsSungIsNotCorrected)
{
    auto document = makeDocument();

    document->modifyNotes ({ 1 }, [] (Note& note) { note.isEnabled = false; }, "leave as sung");

    EXPECT_NEAR (document->getCorrectedMelody().correctedSemitones[75], 63.7, 0.05);
}

TEST (EditDocumentTest, ForgettingANoteLeavesItAsSung)
{
    auto document = makeDocument();

    document->removeNotes ({ 1 });

    ASSERT_EQ (document->getNotes().size(), 2u);
    EXPECT_NEAR (document->getCorrectedMelody().correctedSemitones[75], 63.7, 0.15);
}

TEST (EditDocumentTest, ChangingTheScaleRetunesTheNotes)
{
    auto document = makeDocument();

    document->setScale (Scale { Scale::Type::majorPentatonic, 0 }, true);

    EXPECT_EQ (document->getNotes()[1].targetNote, 64);
    EXPECT_TRUE (document->getScale().contains (document->getNotes()[2].targetNote));
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

    EXPECT_GT (document->getCorrectedMelody().gain[25], 1.9f);
    EXPECT_NEAR (document->getCorrectedMelody().gain[125], 1.0f, 0.01f);
}
