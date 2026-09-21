#include "common/PitchCurve.h"
#include "common/NoteSegmenter.h"

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

/** @brief A melody that holds each pitch in turn, every note the same length. */
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
}

TEST (NoteSegmenter, FindsOneNotePerHeldPitch)
{
    const auto melody = makeMelody ({ 60.0, 64.0, 67.0 }, 50);

    const auto notes = segmentNotes (melody, {});

    ASSERT_EQ (notes.size(), 3u);
    EXPECT_NEAR (notes[0].semitones, 60.0f, 0.05f);
    EXPECT_NEAR (notes[1].semitones, 64.0f, 0.05f);
    EXPECT_NEAR (notes[2].semitones, 67.0f, 0.05f);
}

TEST (NoteSegmenter, MeasuresHowFlatTheSingerWas)
{
    const auto melody = makeMelody ({ 59.7 }, 60);

    const auto notes = segmentNotes (melody, {});

    ASSERT_EQ (notes.size(), 1u);
    EXPECT_NEAR (notes[0].semitones, 59.7f, 0.05f);
    EXPECT_TRUE (notes[0].isAsPlayed());
}

TEST (NoteSegmenter, IgnoresAPitchTooBriefToBeANote)
{
    auto melody = makeMelody ({ 60.0 }, 100);

    for (int frameIndex = 50; frameIndex < 52; ++frameIndex)
        melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] = toFrequency (64.0);

    const auto notes = segmentNotes (melody, {});

    EXPECT_EQ (notes.size(), 1u);
}

TEST (NoteSegmenter, SilenceEndsANote)
{
    auto melody = makeMelody ({ 60.0, 60.0 }, 40);

    for (int frameIndex = 35; frameIndex < 45; ++frameIndex)
        melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] = 0.0f;

    const auto notes = segmentNotes (melody, {});

    EXPECT_EQ (notes.size(), 2u);
}

TEST (NoteSegmenter, TargetsFollowTheScale)
{
    const auto melody = makeMelody ({ 61.0 }, 60);

    const auto notes = segmentNotes (melody, {});

    ASSERT_EQ (notes.size(), 1u);
    EXPECT_NEAR (notes[0].semitones, 61.0f, 0.05f);
}

TEST (PitchCurve, AnalysingATakeLeavesItExactlyAsPlayed)
{
    const auto melody = makeMelody ({ 59.7 }, 100);
    auto notes = segmentNotes (melody, {});
    measureDeviation (melody, notes);

    const auto composed = composeMelody (melody, notes);

    ASSERT_EQ (composed.composedSemitones.size(), melody.fundamentalFrequencyHz.size());

    for (std::size_t frameIndex = 0; frameIndex < composed.composedSemitones.size(); ++frameIndex)
        ASSERT_NEAR (composed.composedSemitones[frameIndex], composed.sungSemitones[frameIndex], 1.0e-3f);
}

TEST (PitchCurve, MovingANoteMovesTheWholeCurveWithIt)
{
    const auto melody = makeMelody ({ 59.7 }, 100);
    auto notes = segmentNotes (melody, {});
    measureDeviation (melody, notes);

    for (auto& note : notes)
        note.semitones = 60.0f;

    const auto composed = composeMelody (melody, notes);

    EXPECT_NEAR (composed.composedSemitones[50], 60.0f, 0.02f);
}

TEST (PitchCurve, TheBaseArrivesAtANoteRatherThanSteppingOntoIt)
{
    const auto melody = makeMelody ({ 60.0, 67.0 }, 100);
    auto notes = segmentNotes (melody, {});
    measureDeviation (melody, notes);

    ASSERT_EQ (notes.size(), 2u);

    const auto composed = composeMelody (melody, notes);

    EXPECT_NEAR (composed.baseSemitones[50], 60.0f, 0.02f);
    EXPECT_NEAR (composed.baseSemitones[150], 67.0f, 0.02f);

    const auto atBoundary = composed.baseSemitones[100];

    EXPECT_GT (atBoundary, 60.5f);
    EXPECT_LT (atBoundary, 66.5f);

    for (int frameIndex = 91; frameIndex <= 109; ++frameIndex)
        ASSERT_GE (composed.baseSemitones[static_cast<std::size_t> (frameIndex)],
                   composed.baseSemitones[static_cast<std::size_t> (frameIndex - 1)]);
}

TEST (PitchCurve, VibratoSurvivesBeingMoved)
{
    auto melody = makeMelody ({ 60.0 }, 200);

    for (int frameIndex = 0; frameIndex < melody.getNumFrames(); ++frameIndex)
    {
        const auto seconds = static_cast<double> (frameIndex) / frameRate;
        melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] =
            toFrequency (59.6 + 0.4 * std::sin (2.0 * 3.14159265 * 5.5 * seconds));
    }

    auto notes = segmentNotes (melody, {});
    measureDeviation (melody, notes);

    for (auto& note : notes)
        note.semitones += 3.0f;

    const auto composed = composeMelody (melody, notes);

    const auto depth = [] (const std::vector<float>& curve)
    {
        auto lowest = curve[60];
        auto highest = curve[60];

        for (std::size_t index = 60; index < 140; ++index)
        {
            lowest = std::min (lowest, curve[index]);
            highest = std::max (highest, curve[index]);
        }

        return highest - lowest;
    };

    EXPECT_NEAR (depth (composed.composedSemitones), depth (composed.sungSemitones), 0.02f);
}

TEST (PitchCurve, VibratoCanBeScaledAway)
{
    auto melody = makeMelody ({ 60.0 }, 200);

    for (int frameIndex = 0; frameIndex < melody.getNumFrames(); ++frameIndex)
    {
        const auto seconds = static_cast<double> (frameIndex) / frameRate;
        melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] =
            toFrequency (60.0 + 0.5 * std::sin (2.0 * 3.14159265 * 5.5 * seconds));
    }

    auto notes = segmentNotes (melody, {});
    measureDeviation (melody, notes);

    notes[0].vibrato = 0.0f;

    const auto composed = composeMelody (melody, notes);

    auto lowest = composed.composedSemitones[60];
    auto highest = composed.composedSemitones[60];

    for (std::size_t index = 60; index < 140; ++index)
    {
        lowest = std::min (lowest, composed.composedSemitones[index]);
        highest = std::max (highest, composed.composedSemitones[index]);
    }

    EXPECT_LT (highest - lowest, 0.05f);
}

TEST (PitchCurve, TiltingLeansTheNoteWithoutMovingItsCentre)
{
    const auto melody = makeMelody ({ 60.0 }, 100);
    auto notes = segmentNotes (melody, {});
    measureDeviation (melody, notes);

    ASSERT_EQ (notes.size(), 1u);
    notes[0].tiltRight = 1.0f;

    const auto composed = composeMelody (melody, notes);

    EXPECT_NEAR (composed.composedSemitones[50], 60.0f, 0.05f);
    EXPECT_NEAR (composed.composedSemitones[95] - composed.composedSemitones[5], 1.0f, 0.1f);
}

TEST (PitchCurve, AnUnvoicedGapIsCarriedAcrossRatherThanLeftEmpty)
{
    auto melody = makeMelody ({ 60.0, 67.0 }, 50);

    for (int frameIndex = 45; frameIndex < 55; ++frameIndex)
    {
        melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] = 0.0f;
        melody.confidence[static_cast<std::size_t> (frameIndex)] = 0.0f;
    }

    const auto dense = interpolateThroughUnvoiced (melody);

    for (const auto value : dense)
        ASSERT_GT (value, 0.0f);

    EXPECT_NEAR (dense[50], std::sqrt (dense[40] * dense[60]), dense[50] * 0.05f);
}
