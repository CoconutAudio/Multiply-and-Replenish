#include "edit/Scale.h"

#include <gtest/gtest.h>

using namespace rvctuner;

TEST (ScaleTest, ChromaticAcceptsEveryNote)
{
    const Scale scale { Scale::Type::chromatic, 0 };

    for (int midiNote = 60; midiNote < 72; ++midiNote)
        EXPECT_TRUE (scale.contains (midiNote));
}

TEST (ScaleTest, MajorHoldsSevenOfTwelve)
{
    const Scale scale { Scale::Type::major, 0 };

    auto numAllowed = 0;

    for (int midiNote = 60; midiNote < 72; ++midiNote)
        numAllowed += scale.contains (midiNote) ? 1 : 0;

    EXPECT_EQ (numAllowed, 7);
    EXPECT_TRUE (scale.contains (64));
    EXPECT_FALSE (scale.contains (61));
}

TEST (ScaleTest, SnapMovesToTheNearestDegree)
{
    const Scale scale { Scale::Type::major, 0 };

    EXPECT_EQ (scale.snap (61.4), 62);
    EXPECT_EQ (scale.snap (60.2), 60);
    EXPECT_EQ (scale.snap (66.0), 65);
}

TEST (ScaleTest, TonicTransposesTheDegrees)
{
    const Scale scale { Scale::Type::major, 2 };

    EXPECT_TRUE (scale.contains (62));
    EXPECT_TRUE (scale.contains (66));
    EXPECT_FALSE (scale.contains (65));
}

TEST (ScaleTest, EditingADegreeMakesTheScaleCustom)
{
    Scale scale { Scale::Type::major, 0 };

    scale.setPitchClassAllowed (1, true);

    EXPECT_EQ (scale.getType(), Scale::Type::custom);
    EXPECT_TRUE (scale.contains (61));
}

TEST (ScaleTest, NamesFollowTheKeyboard)
{
    EXPECT_EQ (Scale::getNoteName (60), "C4");
    EXPECT_EQ (Scale::getNoteName (69), "A4");
    EXPECT_EQ (Scale::getNoteName (61), "C#4");
}
