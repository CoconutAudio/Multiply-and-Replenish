# ONNX Runtime, in three ways: a path the caller gave, one already installed, or the official
# release for this platform downloaded and unpacked.

include(FetchContent)

set(onnxruntimeHints "")
set(onnxruntimeLibraryHints "")

if(TUNER_ONNXRUNTIME_ROOT)
    set(onnxruntimeHints "${TUNER_ONNXRUNTIME_ROOT}/include/onnxruntime" "${TUNER_ONNXRUNTIME_ROOT}/include")
    set(onnxruntimeLibraryHints "${TUNER_ONNXRUNTIME_ROOT}/lib" "${TUNER_ONNXRUNTIME_ROOT}/bin")
endif()

find_path(TUNER_ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS ${onnxruntimeHints}
    PATH_SUFFIXES onnxruntime)

find_library(TUNER_ONNXRUNTIME_LIBRARY onnxruntime HINTS ${onnxruntimeLibraryHints})

if(NOT TUNER_ONNXRUNTIME_INCLUDE_DIR OR NOT TUNER_ONNXRUNTIME_LIBRARY)
    if(WIN32)
        set(onnxruntimeArchive "onnxruntime-win-x64-${TUNER_ONNXRUNTIME_VERSION}.zip")
    elseif(APPLE)
        set(onnxruntimeArchive "onnxruntime-osx-universal2-${TUNER_ONNXRUNTIME_VERSION}.tgz")
    else()
        set(onnxruntimeArchive "onnxruntime-linux-x64-${TUNER_ONNXRUNTIME_VERSION}.tgz")
    endif()

    message(STATUS "ONNX Runtime: downloading ${onnxruntimeArchive}")

    FetchContent_Declare(onnxruntime_binary
        URL "https://github.com/microsoft/onnxruntime/releases/download/v${TUNER_ONNXRUNTIME_VERSION}/${onnxruntimeArchive}")
    FetchContent_MakeAvailable(onnxruntime_binary)

    set(TUNER_ONNXRUNTIME_INCLUDE_DIR "${onnxruntime_binary_SOURCE_DIR}/include" CACHE PATH "" FORCE)

    if(WIN32)
        set(TUNER_ONNXRUNTIME_LIBRARY "${onnxruntime_binary_SOURCE_DIR}/lib/onnxruntime.lib" CACHE FILEPATH "" FORCE)
        set(TUNER_ONNXRUNTIME_RUNTIME "${onnxruntime_binary_SOURCE_DIR}/lib/onnxruntime.dll")
    elseif(APPLE)
        set(TUNER_ONNXRUNTIME_LIBRARY "${onnxruntime_binary_SOURCE_DIR}/lib/libonnxruntime.dylib" CACHE FILEPATH "" FORCE)
        set(TUNER_ONNXRUNTIME_RUNTIME "${TUNER_ONNXRUNTIME_LIBRARY}")
    else()
        set(TUNER_ONNXRUNTIME_LIBRARY "${onnxruntime_binary_SOURCE_DIR}/lib/libonnxruntime.so" CACHE FILEPATH "" FORCE)
        set(TUNER_ONNXRUNTIME_RUNTIME "${TUNER_ONNXRUNTIME_LIBRARY}")
    endif()
else()
    set(TUNER_ONNXRUNTIME_RUNTIME "")
endif()

message(STATUS "ONNX Runtime: ${TUNER_ONNXRUNTIME_LIBRARY}")

add_library(tuner_onnxruntime SHARED IMPORTED GLOBAL)

set_target_properties(tuner_onnxruntime PROPERTIES
    IMPORTED_IMPLIB "${TUNER_ONNXRUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${TUNER_ONNXRUNTIME_INCLUDE_DIR}")

if(TUNER_ONNXRUNTIME_RUNTIME)
    set_target_properties(tuner_onnxruntime PROPERTIES IMPORTED_LOCATION "${TUNER_ONNXRUNTIME_RUNTIME}")
else()
    set_target_properties(tuner_onnxruntime PROPERTIES IMPORTED_LOCATION "${TUNER_ONNXRUNTIME_LIBRARY}")
endif()

get_filename_component(TUNER_ONNXRUNTIME_LIBRARY_DIR "${TUNER_ONNXRUNTIME_LIBRARY}" DIRECTORY)

# A downloaded runtime has to travel with whatever links it, since nothing installed it. Every
# name in the set is copied, not just the one CMake points at: the linker records the library's
# soname, which on Linux is the versioned file the unversioned one points to.
set(tunerCopyOnnxRuntimeScript "${CMAKE_BINARY_DIR}/tuner_copy_onnxruntime.cmake")

file(WRITE "${tunerCopyOnnxRuntimeScript}" "\
file(GLOB libraries \"\${libraryDir}/*onnxruntime*\")

foreach(library \${libraries})
    if(NOT IS_DIRECTORY \"\${library}\")
        file(COPY \"\${library}\" DESTINATION \"\${destination}\" FOLLOW_SYMLINK_CHAIN)
    endif()
endforeach()
")

function(tuner_copy_onnxruntime_beside target)
    if(NOT TUNER_ONNXRUNTIME_RUNTIME)
        return()
    endif()

    if(APPLE)
        set(loaderRelativePath "@loader_path")
    else()
        set(loaderRelativePath "$ORIGIN")
    endif()

    set_target_properties(${target} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${loaderRelativePath}")

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DlibraryDir=${TUNER_ONNXRUNTIME_LIBRARY_DIR}"
                "-Ddestination=$<TARGET_FILE_DIR:${target}>"
                -P "${tunerCopyOnnxRuntimeScript}" VERBATIM)
endfunction()
