cmake_minimum_required(VERSION 3.22)

project(ViridianForge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    # Universal binary (Apple Silicon + Intel) unless the caller overrides it.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

option(VF_BUILD_PLUGIN "Build the JUCE app and plugins (needs JUCE)" ON)
option(VF_BUILD_TESTS  "Build the DSP core unit tests"               ON)
option(VF_BUILD_AU     "Also build the Audio Unit on macOS"          ON)

# ---------------------------------------------------------------------------
# JUCE-independent DSP core (always built; tests run anywhere, no audio deps)
add_subdirectory(core)

if(VF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local checkout with -DJUCE_DIR=/path/to/JUCE, otherwise fetch it.
if(VF_BUILD_PLUGIN)
    set(VF_JUCE_TAG "8.0.4" CACHE STRING "JUCE git tag to fetch")
    if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
        add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        ${VF_JUCE_TAG}
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(juce)
    endif()

    add_subdirectory(app)
endif()
