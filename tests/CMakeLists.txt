add_executable(rvctuner_tests
    BinaryMatrixTests.cpp
    EditDocumentTests.cpp
    MelFilterBankTests.cpp
    MelSpectrogramTests.cpp
    NoteSegmenterTests.cpp
    PitchConversionsTests.cpp
    ScaleTests.cpp
    SincResamplerTests.cpp
    SmoothingTests.cpp
    ZeroPhaseFilterTests.cpp)

target_link_libraries(rvctuner_tests
    PRIVATE
        rvctuner_engine
        rvctuner_warnings
        GTest::gtest_main)

target_compile_definitions(rvctuner_tests PRIVATE
    RVCTUNER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    RVCTUNER_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../res/models")

rvctuner_apply_strict_warnings(
    MelFilterBankTests.cpp
    PitchConversionsTests.cpp
    ScaleTests.cpp
    SincResamplerTests.cpp
    SmoothingTests.cpp)

include(GoogleTest)
gtest_discover_tests(rvctuner_tests)
