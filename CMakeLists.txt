cmake_minimum_required(VERSION 3.25)

project(MultiplyAndReplenish VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BUILD_TESTS "Build the unit tests" ON)

add_subdirectory(libs/JUCE SYSTEM)

# ONNX Runtime (prebuilt)
set(ORT_VERSION 1.19.2)
if(WIN32)
    set(ORT_NAME onnxruntime-win-x64-${ORT_VERSION}.zip)
    set(ORT_LIB lib/onnxruntime.lib)
    set(ORT_RUNTIME lib/onnxruntime.dll)
    set(ORT_RUNTIME_NAME onnxruntime.dll)
elseif(APPLE)
    set(ORT_NAME onnxruntime-osx-universal2-${ORT_VERSION}.tgz)
    set(ORT_LIB lib/libonnxruntime.dylib)
    set(ORT_RUNTIME lib/libonnxruntime.${ORT_VERSION}.dylib)
    set(ORT_RUNTIME_NAME libonnxruntime.${ORT_VERSION}.dylib)
else()
    set(ORT_NAME onnxruntime-linux-x64-${ORT_VERSION}.tgz)
    set(ORT_LIB lib/libonnxruntime.so)
    set(ORT_RUNTIME lib/libonnxruntime.so.${ORT_VERSION})
    set(ORT_RUNTIME_NAME libonnxruntime.so.1)
endif()

include(FetchContent)
FetchContent_Declare(onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_NAME})
FetchContent_MakeAvailable(onnxruntime)

# Warning flags go on our own sources only, never on the JUCE modules built into a target.
function(mr_warnings)
    if(MSVC)
        set(flags /W4 /permissive-)
    else()
        set(flags -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align -Woverloaded-virtual)
    endif()
    set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS ${flags})
endfunction()

function(mr_strict_warnings)
    if(NOT MSVC)
        set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS
            -Wdouble-promotion -Wfloat-conversion -Wconversion -Wsign-conversion
            -Wold-style-cast -Wuseless-cast -Wcast-qual -Wextra-semi)
    endif()
endfunction()

# Engine: DSP, models and editing logic. No plugin code, so the tests can link it.
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/dsp/*.cpp src/nn/*.cpp src/common/*.cpp)

add_library(engine STATIC ${ENGINE_SOURCES})
target_include_directories(engine PUBLIC src)
target_include_directories(engine SYSTEM PUBLIC ${onnxruntime_SOURCE_DIR}/include)
target_link_libraries(engine PUBLIC
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    ${onnxruntime_SOURCE_DIR}/${ORT_LIB})
target_compile_definitions(engine
    PUBLIC JUCE_USE_MP3AUDIOFORMAT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
           JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    PRIVATE MULTIPLYANDREPLENISH_DEVELOPMENT_MODEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/res/models")
mr_warnings(${ENGINE_SOURCES})
mr_strict_warnings(src/dsp/MelFilterBank.cpp src/dsp/SincResampler.cpp
                   src/dsp/Smoothing.cpp src/dsp/ZeroPhaseFilter.cpp)

# Plugin: VST3 and Standalone. Under ARA the host hands over the audio already on its timeline.
set(ARA_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libs/ARA_SDK)
if(EXISTS ${ARA_SDK_PATH}/ARA_Library/Debug/ARADebug.h)
    juce_set_ara_sdk_path(${ARA_SDK_PATH})
    set(ARA_AVAILABLE TRUE)
else()
    set(ARA_AVAILABLE FALSE)
    message(STATUS "ARA SDK not found, building without ARA: git submodule update --init --recursive libs/ARA_SDK")
endif()

juce_add_plugin(MultiplyAndReplenish
    PRODUCT_NAME "Multiply and Replenish"
    COMPANY_NAME "Pitch Innovations"
    BUNDLE_ID com.pitchinnovations.multiplyandreplenish
    PLUGIN_MANUFACTURER_CODE Pinn
    PLUGIN_CODE Mpar
    FORMATS VST3 Standalone
    IS_ARA_EFFECT $<BOOL:${ARA_AVAILABLE}>
    ARA_FACTORY_ID "\"com.pitchinnovations.multiplyandreplenish.arafactory.1\""
    ARA_DOCUMENT_ARCHIVE_ID "\"com.pitchinnovations.multiplyandreplenish.aradocumentarchive.1\""
    ARA_ANALYSIS_TYPES kARAContentTypeNotes
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE)

file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS src/*.cpp src/ui/*.cpp)
target_sources(MultiplyAndReplenish PRIVATE ${PLUGIN_SOURCES})
mr_warnings(${PLUGIN_SOURCES})

if(ARA_AVAILABLE)
    target_sources(MultiplyAndReplenish PRIVATE src/ara/AraSupport.cpp src/ara/DocumentController.cpp)
    target_compile_definitions(MultiplyAndReplenish PUBLIC JucePlugin_Enable_ARA=1)
endif()

target_compile_definitions(MultiplyAndReplenish PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MultiplyAndReplenish
    PRIVATE engine
    PUBLIC juce::juce_audio_processors juce::juce_gui_extra juce::juce_recommended_config_flags)

# The runtime must sit next to each built plugin under the name the binary asks for (its
# SONAME), or the loader falls back to whatever version the system has installed.
foreach(format VST3 Standalone)
    set(target MultiplyAndReplenish_${format})
    set_target_properties(${target} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$<IF:$<PLATFORM_ID:Darwin>,@loader_path,$ORIGIN>")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${onnxruntime_SOURCE_DIR}/${ORT_RUNTIME} $<TARGET_FILE_DIR:${target}>/${ORT_RUNTIME_NAME})
endforeach()

if(BUILD_TESTS)
    enable_testing()
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/googletest)
    add_subdirectory(tests)
endif()
