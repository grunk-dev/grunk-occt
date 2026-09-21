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

# --- AD build (Step 7, see DESIGN.md's "AD validation milestone" and "Single
# project, AD as a build option"): recompiles this *exact same* plugin source
# against an AD-enabled OCCT fork (adOCCT, https://github.com/dlr-sp/adOCCT)
# instead of stock OCCT, activating every generated module's own #ifdef-gated
# dual-emission overload (see src/occt_ad_support.hpp). Linux-only/best-effort -
# the one accepted platform exception in the migration plan.
#
# A build *option* on this one project, not a separate CMake tree: an earlier
# design used a standalone occt_adolc/ subdirectory project instead, justified by
# "find_package(OpenCASCADE) only resolves once per configure" - true, but doesn't
# actually apply here, since this option picks *one* OpenCASCADE per configure
# (stock or adOCCT), never both from the same configure. Consolidating removed
# real, growing duplication between two near-identical CMakeLists.txt files (the
# grunk FetchContent pin below, the whole translation-unit-grouping block further
# down) - see DESIGN.md for the full reasoning. Pick one variant per build
# directory (e.g. `build/` for stock, `build-ad/` for AD) - see README.md's
# "Building with AD support".
option(GRUNK_OCCT_ENABLE_AD "Build against an AD-enabled OCCT fork (adOCCT) instead of stock OCCT - requires ADOCCT_DIR, see README.md's \"Building with AD support\"" OFF)
if(GRUNK_OCCT_ENABLE_AD)
    set(ADOCCT_DIR "" CACHE PATH "Install prefix of adOCCT (dlr-sp/adOCCT), built with -DUSE_ADOLC=ON")
    if(NOT ADOCCT_DIR)
        message(FATAL_ERROR "GRUNK_OCCT_ENABLE_AD is ON but ADOCCT_DIR is not set - see README.md's \"Building with AD support\".")
    endif()
endif()

# --- grunk (FetchContent, pinned to a specific commit - see README.md/DESIGN.md for
# why: grunk's plugin interface and 0.5.0 rewrite are only on `main`, not yet tagged).
# Bump this pin deliberately, in its own PR, gated by the full grunk-occt test suite -
# never as a side effect of an unrelated change.
include(FetchContent)

FetchContent_Declare(grunk
    GIT_REPOSITORY https://github.com/grunk-dev/grunk.git
    GIT_TAG c16f519888eae62564145b1ba4c025c1e6dcc6c2
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

# --- OCCT: stock (conda-forge `occt` package) by default, or adOCCT instead when
# GRUNK_OCCT_ENABLE_AD is ON - find_package name is `OpenCASCADE` either way (set by
# OCCT's own upstream CMake config, independent of which build/package provides it).
# Only one of these two branches ever runs per configure.
if(GRUNK_OCCT_ENABLE_AD)
    # NO_DEFAULT_PATH + explicit PATHS: CMAKE_PREFIX_PATH (above) also has
    # conda-forge's own stock occt==7.9.3 on it (needed for Lua/sol2/etc regardless
    # of this option) - its own OpenCASCADEConfig.cmake would otherwise be found
    # ahead of adOCCT's, silently linking against the wrong, non-AD OCCT.
    find_package(OpenCASCADE CONFIG REQUIRED PATHS "${ADOCCT_DIR}" NO_DEFAULT_PATH)

    # ADOL-C itself: found via pkg-config - needs ADOL-C's own lib{,64}/pkgconfig
    # directory on PKG_CONFIG_PATH (see README.md's "Building with AD support").
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ADOLC REQUIRED IMPORTED_TARGET GLOBAL adolc)
else()
    find_package(OpenCASCADE REQUIRED)
endif()

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
#
# Translation-unit grouping is read from generated/translation_units.txt, written by
# codegen/generate.py from config.yml's own translation_units: section (see its
# comment there) - not decided here. One line per translation unit, each a
# space-separated list of module names (generated/<name>.cpp), except a single line
# "*" meaning "one translation unit for everything, including src/occt_plugin.cpp"
# (config.yml's mode: unity).
#
# This grouping exists at all because of a real sol2/grunk compile error: a
# function/method returning a Handle(X) (opencascade::handle<X>) by value only
# compiles correctly in the *same* translation unit as the one that calls X's own
# register_type<X, ...>() (X ends up needing operator</operator== it doesn't have
# otherwise) - see DESIGN.md's "Handle(X) return type requires being in the same
# translation unit as X" for the confirmed repro, and its own gc/geom entry in
# config.yml's translation_units.groups. This project's own generated code grew
# large enough that compiling *everything* as one translation unit (mode: unity,
# this project's original design) made a single compiler invocation need ~12-13GB
# of RAM and ~4 minutes for that one file alone - reliably OOM-killing GitHub
# Actions' ubuntu-latest CI runners (see DESIGN.md's "Translation-unit grouping").
# mode: separate (config.yml's default) instead compiles each module as its own
# translation unit except the pairs groups: names, keeping every single compiler
# invocation's own resource usage bounded regardless of how large the whole
# plugin's generated code grows - while a single-module line needs no synthesized
# wrapper at all (generated/<name>.cpp is already its own translation unit; CMake's
# normal dependency scanning already covers it), a multi-module line still gets one,
# the same #include-based mechanism mode: unity uses for everything, just scoped to
# that one group.
file(STRINGS "${CMAKE_SOURCE_DIR}/generated/translation_units.txt" OCCT_TU_LINES)

set(OCCT_PLUGIN_SOURCES "")

list(LENGTH OCCT_TU_LINES OCCT_TU_LINE_COUNT)
if(OCCT_TU_LINE_COUNT EQUAL 1 AND OCCT_TU_LINES STREQUAL "*")
    file(GLOB GENERATED_SOURCES ${CMAKE_SOURCE_DIR}/generated/*.cpp)

    set(OCCT_PLUGIN_UNITY_CPP "${CMAKE_BINARY_DIR}/occt_plugin_unity.cpp")
    set(_unity_lines "// AUTO-GENERATED unity build file - see CMakeLists.txt. Do not edit or commit.\n")
    string(APPEND _unity_lines "#include \"${CMAKE_SOURCE_DIR}/src/occt_plugin.cpp\"\n")
    foreach(_generated_src ${GENERATED_SOURCES})
        string(APPEND _unity_lines "#include \"${_generated_src}\"\n")
    endforeach()
    file(WRITE "${OCCT_PLUGIN_UNITY_CPP}" "${_unity_lines}")

    # The unity file's own #includes aren't visible to CMake's dependency scanner
    # from its own path alone - list src/occt_plugin.cpp and every generated/*.cpp
    # as OBJECT_DEPENDS so editing any of them still triggers a rebuild without a
    # reconfigure.
    set_source_files_properties("${OCCT_PLUGIN_UNITY_CPP}" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/src/occt_plugin.cpp;${GENERATED_SOURCES}"
    )
    list(APPEND OCCT_PLUGIN_SOURCES "${OCCT_PLUGIN_UNITY_CPP}")
else()
    # src/occt_plugin.cpp is always its own translation unit here - it never
    # returns a Handle(X) itself, only calls each module's own
    # register_<module>(ns) function (see its own top comment), so it never needs
    # to share a TU with anything.
    list(APPEND OCCT_PLUGIN_SOURCES "${CMAKE_SOURCE_DIR}/src/occt_plugin.cpp")

    set(_tu_index 0)
    foreach(_tu_line ${OCCT_TU_LINES})
        separate_arguments(_tu_modules UNIX_COMMAND "${_tu_line}")
        list(LENGTH _tu_modules _tu_module_count)

        if(_tu_module_count EQUAL 1)
            list(APPEND OCCT_PLUGIN_SOURCES "${CMAKE_SOURCE_DIR}/generated/${_tu_line}.cpp")
        else()
            set(_tu_cpp "${CMAKE_BINARY_DIR}/occt_plugin_tu_${_tu_index}.cpp")
            set(_tu_content "// AUTO-GENERATED translation-unit file - see CMakeLists.txt. Do not edit or commit.\n")
            set(_tu_depends "")
            foreach(_tu_module ${_tu_modules})
                set(_tu_module_src "${CMAKE_SOURCE_DIR}/generated/${_tu_module}.cpp")
                string(APPEND _tu_content "#include \"${_tu_module_src}\"\n")
                list(APPEND _tu_depends "${_tu_module_src}")
            endforeach()
            file(WRITE "${_tu_cpp}" "${_tu_content}")
            set_source_files_properties("${_tu_cpp}" PROPERTIES OBJECT_DEPENDS "${_tu_depends}")
            list(APPEND OCCT_PLUGIN_SOURCES "${_tu_cpp}")
        endif()

        math(EXPR _tu_index "${_tu_index} + 1")
    endforeach()
endif()

add_library(occt_plugin SHARED ${OCCT_PLUGIN_SOURCES})
target_link_libraries(occt_plugin PRIVATE grunk::plugin ${OpenCASCADE_LIBRARIES})
target_include_directories(occt_plugin PRIVATE ${OpenCASCADE_INCLUDE_DIR})
if(GRUNK_OCCT_ENABLE_AD)
    # Activates every generated module's own #ifdef GRUNK_OCCT_WITH_ADOLC dual-
    # emission overload (see src/occt_ad_support.hpp) and register_standard_real_ad's
    # own registration of Standard_Real/Standard_Adouble itself, called first thing
    # inside register_occt() (src/occt_plugin.cpp) - without it, is<Standard_Real>()
    # could never be true for anything and every derivative would silently read 0.
    target_compile_definitions(occt_plugin PRIVATE GRUNK_OCCT_WITH_ADOLC)
    target_link_libraries(occt_plugin PRIVATE PkgConfig::ADOLC)
endif()
# MSVC only: the unity file's own object now has more COFF sections than the
# default format allows ("fatal error C1128: number of sections exceeded object
# file format limit") - a well-known consequence of a single translation unit
# this large (found on Windows CI once the gp module's own wildcard expansion,
# see DESIGN.md, pushed occt_plugin_unity.cpp's generated code past MSVC's
# default limit). /bigobj raises that limit; GCC/Clang have no equivalent
# limitation; MSVC's own /bigobj sacrifices nothing to be enabled unconditionally
# whether truly needed or not.
target_compile_options(occt_plugin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)

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

    set(OCCT_TEST_SOURCES
        tests/test_load.cpp
        tests/test_recipe.cpp
        tests/test_gp.cpp
        tests/test_geom.cpp
        tests/test_shapes.cpp
        tests/test_collections.cpp
        tests/test_bnd.cpp
        tests/test_brep.cpp
        tests/test_breptools.cpp
        tests/test_toploc.cpp
        tests/test_gc.cpp
        tests/test_gp_wildcard.cpp
        tests/test_brepoffsetapi.cpp
        tests/test_geomapi.cpp
        tests/test_brepfilletapi.cpp
    )
    if(GRUNK_OCCT_ENABLE_AD)
        # AD validation milestone (Step 7, see DESIGN.md) - a closed-form derivative
        # check, only meaningful (and only compiles cleanly, since occt.Standard_Real
        # only exists) when occt_plugin was actually built against adOCCT.
        list(APPEND OCCT_TEST_SOURCES tests/test_ad.cpp)
    endif()

    add_executable(runUnitTests ${OCCT_TEST_SOURCES})
    target_link_libraries(runUnitTests PRIVATE grunk::plugin grunk::recipe GTest::gtest_main)
    target_compile_definitions(runUnitTests PRIVATE
        OCCT_PLUGIN_SO_PATH="$<TARGET_FILE:occt_plugin>"
        EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
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
