# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

cmake_minimum_required(VERSION 3.15)
project(grunk-occt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# occt_plugin (below) links the static grunk libraries into a shared object, which
# requires their objects to be position-independent - set this before grunk's own
# add_subdirectory call so it applies there too.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")

option(GRUNK_OCCT_TESTS "Build the test harness" ON)

# Generated-code policy (decided in Step 0, see DESIGN.md): bindings produced by the
# libclang-based generator (starting Step 2) are checked into generated/ as the
# source of truth for normal builds, mirroring legacy grocc's GROCC_GENERATE_CODE -
# clang/libclang availability can't be assumed on every build host/platform. This
# flag is a placeholder until Step 2 adds the real `generate_code` custom target.
option(GRUNK_OCCT_GENERATE_CODE "Regenerate OCCT bindings using the codegen tool (requires clang/libclang)" OFF)

# --- grunk (FetchContent, pinned to a specific commit - see README.md/DESIGN.md for
# why: grunk's plugin interface and 0.5.0 rewrite are only on `main`, not yet tagged).
# Bump this pin deliberately, in its own PR, gated by the full grunk-occt test suite -
# never as a side effect of an unrelated change.
include(FetchContent)

FetchContent_Declare(grunk
    GIT_REPOSITORY https://github.com/grunk-dev/grunk.git
    GIT_TAG ac5a90d18cbb78c21c96829e535b4d17c6322fb1
)

FetchContent_GetProperties(grunk)
if(NOT grunk_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(grunk)

    if(EXISTS "${grunk_SOURCE_DIR}/cmake/grunkConfig.cmake.in")
        file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/cmake")
        file(COPY "${grunk_SOURCE_DIR}/cmake/grunkConfig.cmake.in"
             DESTINATION "${CMAKE_SOURCE_DIR}/cmake"
             NO_SOURCE_PERMISSIONS)
    endif()

    set(GRUNK_WITH_TASKFLOW OFF CACHE BOOL "" FORCE)
    set(GRUNK_WITH_DYNAMIC ON CACHE BOOL "" FORCE)
    set(GRUNK_WITH_RECIPE ON CACHE BOOL "" FORCE)
    set(GRUNK_TESTS OFF CACHE BOOL "" FORCE)
    set(GRUNK_PYTHON OFF CACHE BOOL "" FORCE)

    include_directories(${grunk_BINARY_DIR})
    add_subdirectory(${grunk_SOURCE_DIR} ${grunk_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# --- OCCT (conda-forge `occt` package; find_package name is `OpenCASCADE`, set by
# OCCT's own upstream CMake config - independent of what the conda package is called).
find_package(OpenCASCADE REQUIRED)

# --- occt plugin: built as its own shared object, loaded at runtime via
# grunk::plugin::load_native (never linked directly by a consuming application) -
# see src/occt_plugin.cpp.
add_library(occt_plugin SHARED src/occt_plugin.cpp)
target_link_libraries(occt_plugin PRIVATE grunk::plugin ${OpenCASCADE_LIBRARIES})
target_include_directories(occt_plugin PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# --- test harness: loads occt_plugin via grunk::plugin::load_native and drives it
# through raw Lua via the sol2 state directly (see DESIGN.md - deliberately not routed
# through grunk::recipe's YAML layer yet).
if(GRUNK_OCCT_TESTS)
    enable_testing()
    add_executable(occt_plugin_tests tests/test_load.cpp)
    target_link_libraries(occt_plugin_tests PRIVATE grunk::plugin)
    target_compile_definitions(occt_plugin_tests PRIVATE
        OCCT_PLUGIN_SO_PATH="$<TARGET_FILE:occt_plugin>"
    )
    add_dependencies(occt_plugin_tests occt_plugin)
    add_test(NAME occt_plugin_tests COMMAND occt_plugin_tests)
endif()
