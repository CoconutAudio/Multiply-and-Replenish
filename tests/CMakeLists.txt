set(TEST_SOURCES
    EditDocumentTests.cpp
    MelFilterBankTests.cpp
    MelSpectrogramTests.cpp
    NoteSegmenterTests.cpp
    PitchConversionsTests.cpp
    ScaleTests.cpp
    SincResamplerTests.cpp
    SmoothingTests.cpp
    ZeroPhaseFilterTests.cpp)

add_executable(tests ${TEST_SOURCES})

target_link_libraries(tests
    PRIVATE
        engine
        GTest::gtest_main)

target_compile_definitions(tests PRIVATE
    MULTIPLYANDREPLENISH_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mr_warnings(${TEST_SOURCES})

mr_strict_warnings(
    MelFilterBankTests.cpp
    PitchConversionsTests.cpp
    ScaleTests.cpp
    SincResamplerTests.cpp
    SmoothingTests.cpp)

include(GoogleTest)
gtest_discover_tests(tests)
