add_executable(tuner_tests
    EditDocumentTests.cpp
    MelFilterBankTests.cpp
    MelSpectrogramTests.cpp
    NoteSegmenterTests.cpp
    PitchConversionsTests.cpp
    ScaleTests.cpp
    SincResamplerTests.cpp
    SmoothingTests.cpp
    ZeroPhaseFilterTests.cpp)

target_link_libraries(tuner_tests
    PRIVATE
        tuner_engine
        tuner_warnings
        GTest::gtest_main)

target_compile_definitions(tuner_tests PRIVATE
    TUNER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    TUNER_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../res/models")

tuner_apply_strict_warnings(
    MelFilterBankTests.cpp
    PitchConversionsTests.cpp
    ScaleTests.cpp
    SincResamplerTests.cpp
    SmoothingTests.cpp)

include(GoogleTest)
gtest_discover_tests(tuner_tests)
