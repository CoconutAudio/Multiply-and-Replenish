cmake_minimum_required(VERSION 3.24)

project(RVCTuner
    VERSION 0.1.0
    DESCRIPTION "Vocal pitch editor: analyse a sung take, edit its melody, hear it back sung"
    HOMEPAGE_URL "https://github.com/vivekvjyn/RVCTuner"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(RVCTUNER_BUILD_TESTS "Build the unit tests" ON)

set(RVCTUNER_ONNXRUNTIME_ROOT "" CACHE PATH
    "ONNX Runtime installation to build against, when it is not on the default search paths")

if(RVCTUNER_ONNXRUNTIME_ROOT)
    set(onnxruntimeHints
        "${RVCTUNER_ONNXRUNTIME_ROOT}/include/onnxruntime"
        "${RVCTUNER_ONNXRUNTIME_ROOT}/include")
    set(onnxruntimeLibraryHints "${RVCTUNER_ONNXRUNTIME_ROOT}/lib" "${RVCTUNER_ONNXRUNTIME_ROOT}/bin")
else()
    set(onnxruntimeHints "")
    set(onnxruntimeLibraryHints "")
endif()

find_path(RVCTUNER_ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS ${onnxruntimeHints}
    PATH_SUFFIXES onnxruntime)

find_library(RVCTUNER_ONNXRUNTIME_LIBRARY onnxruntime
    HINTS ${onnxruntimeLibraryHints})

if(NOT RVCTUNER_ONNXRUNTIME_INCLUDE_DIR OR NOT RVCTUNER_ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR
        "ONNX Runtime was not found. Install it (Fedora: onnxruntime-devel, macOS: brew install "
        "onnxruntime, Windows: the release zip) and configure again, or point CMake at an existing "
        "copy:\n    cmake -B build -DRVCTUNER_ONNXRUNTIME_ROOT=/path/to/onnxruntime")
endif()

message(STATUS "ONNX Runtime: ${RVCTUNER_ONNXRUNTIME_LIBRARY}")

add_library(rvctuner_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(rvctuner_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${RVCTUNER_ONNXRUNTIME_LIBRARY}"
    IMPORTED_IMPLIB "${RVCTUNER_ONNXRUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${RVCTUNER_ONNXRUNTIME_INCLUDE_DIR}")

foreach(submodule JUCE googletest hnswlib)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/${submodule}/CMakeLists.txt")
        message(FATAL_ERROR "libs/${submodule} is empty. See the build steps in README.md.")
    endif()
endforeach()

add_subdirectory(libs/JUCE)

add_library(rvctuner_hnswlib INTERFACE)
target_include_directories(rvctuner_hnswlib SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libs/hnswlib")

add_library(rvctuner_warnings INTERFACE)

if(MSVC)
    target_compile_options(rvctuner_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->)
else()
    target_compile_options(rvctuner_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic
            -Wshadow                # a shadowed sampleRate is a real bug here
            -Wnon-virtual-dtor
            -Wcast-align
            -Woverloaded-virtual>)
endif()

function(rvctuner_apply_strict_warnings)
    if(MSVC)
        return()
    endif()

    foreach(sourceFile ${ARGN})
        set_property(SOURCE "${sourceFile}" APPEND PROPERTY COMPILE_OPTIONS
            -Wdouble-promotion      # float32 audio silently widened to double
            -Wfloat-conversion
            -Wconversion            # and every other implicit narrowing
            -Wsign-conversion       # a negative index into a std::size_t
            -Wold-style-cast
            -Wuseless-cast
            -Wcast-qual
            -Wextra-semi)
    endforeach()
endfunction()

set(RVCTUNER_ENGINE_SOURCES
    src/common/BinaryMatrix.cpp
    src/dsp/MelFilterBank.cpp
    src/dsp/MelSpectrogram.cpp
    src/dsp/SincResampler.cpp
    src/dsp/Smoothing.cpp
    src/dsp/ZeroPhaseFilter.cpp
    src/edit/CorrectionCurve.cpp
    src/edit/EditDocument.cpp
    src/edit/MidiExport.cpp
    src/edit/Pipeline.cpp
    src/edit/RenderScheduler.cpp
    src/edit/NoteSegmenter.cpp
    src/edit/Scale.cpp
    src/model/FcpeDetector.cpp
    src/model/FeatureRetriever.cpp
    src/model/MelSynthesiser.cpp
    src/model/ModelManifest.cpp
    src/model/OnnxSession.cpp
    src/model/RmvpeDetector.cpp
    src/model/VoiceLoader.cpp
    src/model/VoiceModel.cpp
    src/model/VoiceModelLibrary.cpp
    src/model/VoiceSynthesiser.cpp)

add_library(rvctuner_engine STATIC ${RVCTUNER_ENGINE_SOURCES})
target_include_directories(rvctuner_engine PUBLIC src)

target_link_libraries(rvctuner_engine
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_dsp
        juce::juce_data_structures
        juce::juce_events
        rvctuner_onnxruntime
    PRIVATE
        rvctuner_hnswlib
        rvctuner_warnings)

target_compile_definitions(rvctuner_engine PUBLIC
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_compile_definitions(rvctuner_engine PRIVATE
    RVCTUNER_DEVELOPMENT_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/res/models")

set(RVCTUNER_APP_SOURCES
    src/Main.cpp
    src/audio/TransportPlayer.cpp
    src/ui/EditorView.cpp
    src/ui/Inspector.cpp
    src/ui/MainComponent.cpp
    src/ui/NoteGrid.cpp
    src/ui/PanelLookAndFeel.cpp
    src/ui/ToolBar.cpp
    src/ui/TransportBar.cpp)

# One editor, two builds of it: the engine is fixed at build time so that each app is one thing.
function(rvctuner_add_app target productName isVoiceBuild)
    juce_add_gui_app(${target}
        PRODUCT_NAME "${productName}"
        COMPANY_NAME "Pitch Innovations"
        BUNDLE_ID "com.pitchinnovations.${target}"
        DESCRIPTION "Vocal pitch editor"
        NEEDS_CURL FALSE
        NEEDS_WEB_BROWSER FALSE)

    target_sources(${target} PRIVATE ${RVCTUNER_APP_SOURCES})
    target_include_directories(${target} PRIVATE src)

    target_compile_definitions(${target} PUBLIC
        RVCTUNER_VOICE_BUILD=${isVoiceBuild}
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:${target},JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:${target},JUCE_VERSION>")

    target_link_libraries(${target}
        PRIVATE
            rvctuner_engine
            rvctuner_warnings
            juce::juce_audio_devices
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endfunction()

rvctuner_add_app(RVCTunerMel "RVCTuner Mel" 0)
rvctuner_add_app(RVCTunerVoice "RVCTuner Voice" 1)

juce_add_console_app(rvctuner_tune
    PRODUCT_NAME "rvctuner-tune")

target_sources(rvctuner_tune PRIVATE tools/TuneCli.cpp)
target_include_directories(rvctuner_tune PRIVATE src)

target_compile_definitions(rvctuner_tune PRIVATE
    RVCTUNER_DEVELOPMENT_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/res/models")

target_link_libraries(rvctuner_tune
    PRIVATE
        rvctuner_engine
        rvctuner_warnings
    PUBLIC
        juce::juce_recommended_config_flags)

find_package(Doxygen)

if(DOXYGEN_FOUND)
    add_custom_target(rvctuner_docs
        COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs"
        COMMENT "Generating API documentation"
        VERBATIM)
endif()

if(RVCTUNER_BUILD_TESTS)
    enable_testing()
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(libs/googletest)
    add_subdirectory(tests)
endif()
