#include "edit/CorrectionCurve.h"
#include "edit/NoteSegmenter.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace tuner;

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

    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});

    ASSERT_EQ (notes.size(), 3u);
    EXPECT_EQ (notes[0].targetNote, 60);
    EXPECT_EQ (notes[1].targetNote, 64);
    EXPECT_EQ (notes[2].targetNote, 67);
}

TEST (NoteSegmenter, MeasuresHowFlatTheSingerWas)
{
    const auto melody = makeMelody ({ 59.7 }, 60);

    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});

    ASSERT_EQ (notes.size(), 1u);
    EXPECT_EQ (notes[0].targetNote, 60);
    EXPECT_NEAR (notes[0].getError(), -0.3, 0.05);
}

TEST (NoteSegmenter, IgnoresAPitchTooBriefToBeANote)
{
    auto melody = makeMelody ({ 60.0 }, 100);

    for (int frameIndex = 50; frameIndex < 52; ++frameIndex)
        melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] = toFrequency (64.0);

    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});

    EXPECT_EQ (notes.size(), 1u);
}

TEST (NoteSegmenter, SilenceEndsANote)
{
    auto melody = makeMelody ({ 60.0, 60.0 }, 40);

    for (int frameIndex = 35; frameIndex < 45; ++frameIndex)
        melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] = 0.0f;

    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});

    EXPECT_EQ (notes.size(), 2u);
}

TEST (NoteSegmenter, TargetsFollowTheScale)
{
    const auto melody = makeMelody ({ 61.0 }, 60);

    const auto notes = segmentNotes (melody, Scale { Scale::Type::major, 0 }, {});

    ASSERT_EQ (notes.size(), 1u);
    EXPECT_EQ (notes[0].targetNote, 60);
}

TEST (CorrectionCurve, FullCorrectionLandsOnTheTarget)
{
    const auto melody = makeMelody ({ 59.7 }, 100);
    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});

    const auto corrected = correctMelody (melody, notes, {}, {});

    ASSERT_EQ (corrected.correctedSemitones.size(), melody.fundamentalFrequencyHz.size());
    EXPECT_NEAR (corrected.correctedSemitones[50], 60.0, 0.02);
}

TEST (CorrectionCurve, NoCorrectionLeavesTheTakeAlone)
{
    const auto melody = makeMelody ({ 59.7 }, 100);
    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});

    CorrectionSettings settings;
    settings.correction = 0.0f;

    const auto corrected = correctMelody (melody, notes, {}, settings);

    EXPECT_NEAR (corrected.correctedSemitones[50], 59.7, 0.02);
    EXPECT_NEAR (corrected.shiftSemitones[50], 0.0, 0.02);
}

TEST (CorrectionCurve, VibratoSurvivesCorrection)
{
    auto melody = makeMelody ({ 60.0 }, 200);

    for (int frameIndex = 0; frameIndex < melody.getNumFrames(); ++frameIndex)
    {
        const auto seconds = static_cast<double> (frameIndex) / frameRate;
        melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] =
            toFrequency (59.6 + 0.4 * std::sin (2.0 * 3.14159265 * 5.5 * seconds));
    }

    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});
    const auto corrected = correctMelody (melody, notes, {}, {});

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

    EXPECT_NEAR (depth (corrected.correctedSemitones), depth (corrected.sungSemitones), 0.1);
}

TEST (CorrectionCurve, VibratoCanBeScaledAway)
{
    auto melody = makeMelody ({ 60.0 }, 200);

    for (int frameIndex = 0; frameIndex < melody.getNumFrames(); ++frameIndex)
    {
        const auto seconds = static_cast<double> (frameIndex) / frameRate;
        melody.fundamentalFrequencyHz[static_cast<std::size_t> (frameIndex)] =
            toFrequency (60.0 + 0.5 * std::sin (2.0 * 3.14159265 * 5.5 * seconds));
    }

    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});

    CorrectionSettings settings;
    settings.vibrato = 0.0f;

    const auto corrected = correctMelody (melody, notes, {}, settings);

    auto lowest = corrected.correctedSemitones[60];
    auto highest = corrected.correctedSemitones[60];

    for (std::size_t index = 60; index < 140; ++index)
    {
        lowest = std::min (lowest, corrected.correctedSemitones[index]);
        highest = std::max (highest, corrected.correctedSemitones[index]);
    }

    EXPECT_LT (highest - lowest, 0.2);
}

TEST (CorrectionCurve, UnvoicedFramesStayUnvoiced)
{
    auto melody = makeMelody ({ 60.0 }, 100);
    melody.fundamentalFrequencyHz[40] = 0.0f;

    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});
    const auto corrected = correctMelody (melody, notes, {}, {});

    EXPECT_FLOAT_EQ (corrected.fundamentalFrequencyHz[40], 0.0f);
    EXPECT_GT (corrected.fundamentalFrequencyHz[41], 0.0f);
}

TEST (CorrectionCurve, ADrawnPitchIsFollowed)
{
    const auto melody = makeMelody ({ 60.0 }, 100);
    const auto notes = segmentNotes (melody, Scale { Scale::Type::chromatic, 0 }, {});

    std::vector<float> drawn (static_cast<std::size_t> (melody.getNumFrames()), 0.0f);

    for (std::size_t index = 30; index < 70; ++index)
        drawn[index] = 63.0f;

    const auto corrected = correctMelody (melody, notes, drawn, {});

    EXPECT_NEAR (corrected.correctedSemitones[50], 63.0, 0.05);
}
