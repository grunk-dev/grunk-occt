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
    GIT_TAG 1ad286bfdc937ff75735c20db4358eb282f48c02
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

# --- code generator (Step 2, see codegen/README.md): opt-in, not wired as a
# dependency of occt_plugin below - regenerated output needs review/diffing before
# being trusted, not silent regeneration on every build (see DESIGN.md's
# "Generated-code policy"). Run via `pixi run generate_code`, which activates the
# pixi environment (python3 + libclang + pyyaml) before invoking this target.
if (GRUNK_OCCT_GENERATE_CODE)
    add_custom_target(generate_code
        COMMAND python3 ${CMAKE_SOURCE_DIR}/codegen/generate.py
                --config ${CMAKE_SOURCE_DIR}/codegen/config.yml
                --occt-include-dir ${OpenCASCADE_INCLUDE_DIR}
                --output-dir ${CMAKE_SOURCE_DIR}/generated
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Regenerating OCCT bindings (codegen/generate.py)"
        VERBATIM
    )
endif()

# --- occt plugin: built as its own shared object, loaded at runtime via
# grunk::plugin::load_native (never linked directly by a consuming application) -
# see src/occt_plugin.cpp. generated/*.cpp are checked into git (see
# codegen/README.md) and compiled unconditionally, regardless of
# GRUNK_OCCT_GENERATE_CODE above.
file(GLOB GENERATED_SOURCES ${CMAKE_SOURCE_DIR}/generated/*.cpp)
add_library(occt_plugin SHARED src/occt_plugin.cpp ${GENERATED_SOURCES})
target_link_libraries(occt_plugin PRIVATE grunk::plugin ${OpenCASCADE_LIBRARIES})
target_include_directories(occt_plugin PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# --- test harness: loads occt_plugin via grunk::plugin::load_native and drives it
# through raw Lua via the sol2 state directly (see DESIGN.md - deliberately not routed
# through grunk::recipe's YAML layer yet). One gtest binary (runUnitTests), matching
# both legacy grocc's and grunk's own test-suite convention - one test *file* per
# module (tests/test_<module>.cpp), each contributing its own TEST_F suite, all
# linked into a single executable; ctest discovers every individual TEST_F case via
# gtest_discover_tests rather than one pass/fail per module.
if(GRUNK_OCCT_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(runUnitTests
        tests/test_load.cpp
        tests/test_gp.cpp
        tests/test_geom.cpp
        tests/test_shapes.cpp
        tests/test_collections.cpp
    )
    target_link_libraries(runUnitTests PRIVATE grunk::plugin GTest::gtest_main)
    target_compile_definitions(runUnitTests PRIVATE
        OCCT_PLUGIN_SO_PATH="$<TARGET_FILE:occt_plugin>"
    )
    # tests/test_collections.cpp includes src/collections.hpp (and gp_Pnt.hxx)
    # directly, and calls into real OCCT code (NCollection_Array1's own bounds
    # checks, gp_Pnt) - unlike every other test file here, which only drives
    # occt_plugin through raw Lua strings and never names an OCCT header itself.
    # Link only the two toolkits that code actually needs (TKernel: Standard_*/
    # NCollection_*; TKMath: gp_Pnt), NOT ${OpenCASCADE_LIBRARIES} (every OCCT
    # toolkit, ~50 libraries including visualization/data-exchange modules this
    # test never touches) - found the hard way: linking the full list directly
    # into the test *executable* (as opposed to occt_plugin, a .so only ever
    # dlopen'd lazily inside a test body) made even `--gtest_list_tests`
    # (gtest_discover_tests' own discovery run, invoked at build time) slow enough
    # to blow its 5s default timeout on macOS CI.
    target_include_directories(runUnitTests PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(runUnitTests PRIVATE TKernel TKMath)
    add_dependencies(runUnitTests occt_plugin)
    gtest_discover_tests(runUnitTests)
endif()
