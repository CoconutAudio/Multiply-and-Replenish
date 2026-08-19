cmake_minimum_required(VERSION 3.24)

project(Tuner
    VERSION 0.2.0
    DESCRIPTION "Vocal pitch editor: analyse a sung take, edit its melody, hear it back sung"
    HOMEPAGE_URL "https://github.com/vivekvjyn/Tuner"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(TUNER_BUILD_TESTS "Build the unit tests" ON)
option(TUNER_BUILD_PLUGIN "Build the ARA plug-in" ON)

set(TUNER_ONNXRUNTIME_ROOT "" CACHE PATH
    "ONNX Runtime installation to build against, instead of the system one or a downloaded release")

set(TUNER_ONNXRUNTIME_VERSION "1.19.2" CACHE STRING
    "Version of ONNX Runtime to download when none is installed")

include(cmake/OnnxRuntime.cmake)

foreach(submodule JUCE googletest)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/${submodule}/CMakeLists.txt")
        message(FATAL_ERROR "libs/${submodule} is empty. See the build steps in README.md.")
    endif()
endforeach()

add_subdirectory(libs/JUCE)

set(TUNER_ARA_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libs/ARA_SDK")

if(TUNER_BUILD_PLUGIN AND EXISTS "${TUNER_ARA_SDK_PATH}/ARA_Library/PlugIn/ARAPlug.h")
    juce_set_ara_sdk_path("${TUNER_ARA_SDK_PATH}")
    set(TUNER_ARA_AVAILABLE TRUE)
    message(STATUS "ARA SDK: ${TUNER_ARA_SDK_PATH}")
else()
    set(TUNER_ARA_AVAILABLE FALSE)

    if(TUNER_BUILD_PLUGIN)
        message(STATUS "ARA SDK not checked out; the plug-in will build without ARA. Run:\n"
                       "    git submodule update --init --depth 1 libs/ARA_SDK\n"
                       "    git -C libs/ARA_SDK submodule update --init --depth 1 ARA_API ARA_Library")
    endif()
endif()

add_library(tuner_warnings INTERFACE)

if(MSVC)
    target_compile_options(tuner_warnings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->)
else()
    target_compile_options(tuner_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic
            -Wshadow                # a shadowed sampleRate is a real bug here
            -Wnon-virtual-dtor
            -Wcast-align
            -Woverloaded-virtual>)
endif()

function(tuner_apply_strict_warnings)
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

set(TUNER_ENGINE_SOURCES
    src/dsp/MelFilterBank.cpp
    src/dsp/MelSpectrogram.cpp
    src/dsp/SincResampler.cpp
    src/dsp/Smoothing.cpp
    src/dsp/ZeroPhaseFilter.cpp
    src/edit/CorrectionCurve.cpp
    src/edit/EditDocument.cpp
    src/edit/MidiExport.cpp
    src/edit/NoteSegmenter.cpp
    src/edit/Pipeline.cpp
    src/edit/RenderScheduler.cpp
    src/edit/Scale.cpp
    src/model/FcpeDetector.cpp
    src/model/MelSynthesiser.cpp
    src/model/ModelLibrary.cpp
    src/model/OnnxSession.cpp
    src/model/RmvpeDetector.cpp)

add_library(tuner_engine STATIC ${TUNER_ENGINE_SOURCES})
target_include_directories(tuner_engine PUBLIC src)

target_link_libraries(tuner_engine
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        tuner_onnxruntime
    PRIVATE
        tuner_warnings)

target_compile_definitions(tuner_engine PUBLIC
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_compile_definitions(tuner_engine PRIVATE
    TUNER_DEVELOPMENT_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/res/models")

set(TUNER_UI_SOURCES
    src/ui/EditorView.cpp
    src/ui/Inspector.cpp
    src/ui/MainComponent.cpp
    src/ui/NoteGrid.cpp
    src/ui/PanelLookAndFeel.cpp
    src/ui/ToolBar.cpp
    src/ui/TransportBar.cpp)

add_library(tuner_ui STATIC ${TUNER_UI_SOURCES} src/audio/TransportPlayer.cpp)
target_include_directories(tuner_ui PUBLIC src)

target_link_libraries(tuner_ui
    PUBLIC
        tuner_engine
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_gui_extra
    PRIVATE
        tuner_warnings)

juce_add_gui_app(Tuner
    PRODUCT_NAME "Tuner"
    COMPANY_NAME "Pitch Innovations"
    BUNDLE_ID "com.pitchinnovations.tuner"
    DESCRIPTION "Vocal pitch editor"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources(Tuner PRIVATE src/Main.cpp)
target_include_directories(Tuner PRIVATE src)

target_compile_definitions(Tuner PUBLIC
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Tuner,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Tuner,JUCE_VERSION>")

target_link_libraries(Tuner
    PRIVATE
        tuner_ui
        tuner_warnings
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

tuner_copy_onnxruntime_beside(Tuner)

if(TUNER_BUILD_PLUGIN)
    set(TUNER_PLUGIN_FORMATS VST3 LV2 Standalone)

    if(APPLE)
        list(APPEND TUNER_PLUGIN_FORMATS AU)
    endif()

    juce_add_plugin(TunerPlugin
        PLUGIN_MANUFACTURER_CODE Pinn
        PLUGIN_CODE Tunr
        MANUFACTURER_NAME "Pitch Innovations"
        COMPANY_NAME "Pitch Innovations"
        BUNDLE_ID "com.pitchinnovations.tunerplugin"
        PRODUCT_NAME "Tuner"
        DESCRIPTION "Vocal pitch editor"

        FORMATS ${TUNER_PLUGIN_FORMATS}
        LV2URI "https://github.com/vivekvjyn/Tuner"
        IS_ARA_EFFECT ${TUNER_ARA_AVAILABLE}
        ARA_FACTORY_ID "\"com.pitchinnovations.tuner.arafactory.0.2.0\""
        ARA_DOCUMENT_ARCHIVE_ID "\"com.pitchinnovations.tuner.aradocumentarchive.1\""
        ARA_ANALYSIS_TYPES kARAContentTypeNotes

        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_AUTO_MANIFEST FALSE)

    target_sources(TunerPlugin PRIVATE
        src/plugin/Processor.cpp
        src/plugin/Editor.cpp
        src/plugin/DocumentController.cpp
        src/plugin/PlaybackRenderer.cpp
        src/plugin/Modification.cpp)

    target_include_directories(TunerPlugin PRIVATE src)

    target_compile_definitions(TunerPlugin PUBLIC
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        TUNER_ARA_AVAILABLE=$<BOOL:${TUNER_ARA_AVAILABLE}>)

    target_link_libraries(TunerPlugin
        PRIVATE
            tuner_ui
            tuner_warnings
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)

    foreach(format ${TUNER_PLUGIN_FORMATS})
        if(TARGET TunerPlugin_${format})
            tuner_copy_onnxruntime_beside(TunerPlugin_${format})
        endif()
    endforeach()
endif()

juce_add_console_app(tuner_tune
    PRODUCT_NAME "tuner-tune")

target_sources(tuner_tune PRIVATE tools/TuneCli.cpp)
target_include_directories(tuner_tune PRIVATE src)

target_compile_definitions(tuner_tune PRIVATE
    TUNER_DEVELOPMENT_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/res/models")

target_link_libraries(tuner_tune
    PRIVATE
        tuner_engine
        tuner_warnings
    PUBLIC
        juce::juce_recommended_config_flags)

tuner_copy_onnxruntime_beside(tuner_tune)

tuner_apply_strict_warnings(
    src/dsp/MelFilterBank.cpp
    src/dsp/SincResampler.cpp
    src/dsp/Smoothing.cpp
    src/dsp/ZeroPhaseFilter.cpp)

find_package(Doxygen)

if(DOXYGEN_FOUND)
    add_custom_target(tuner_docs
        COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs"
        COMMENT "Generating API documentation"
        VERBATIM)
endif()

if(TUNER_BUILD_TESTS)
    enable_testing()
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(libs/googletest)
    add_subdirectory(tests)
endif()
