# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

cmake_minimum_required(VERSION 3.15)
project(grunk-occt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Step 8 (packaging polish, see DESIGN.md's "Packaging") - standard CMake
# per-platform install subdirectory names (lib/, bin/, ...) for the install()
# call further down.
include(GNUInstallDirs)

# occt_plugin is itself a shared object, so its own sources need to be
# position-independent - grunk's own libraries are SHARED now too (found via
# find_package below) and no longer need this set on their behalf.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")

# occt_plugin (and, in the GRUNK_OCCT_ENABLE_AD branch below, adtl_plugin) links
# grunk's own SHARED libraries (grunk_recipe/grunk_plugin) dynamically, rather than
# each statically embedding its own private copy - see scripts/install_grunk.cmake's
# own comment for why this matters (one physical set of grunk libraries shared by
# every consumer loaded into the same process, not one duplicated per plugin, and
# no possibility of a build-configuration mismatch between independently vendored
# copies). CMAKE_INSTALL_RPATH_USE_LINK_PATH bakes in the absolute install location
# of every library actually linked - including wherever find_package(grunk) below
# resolves to (.deps/grunk by default, see pixi.toml's install_grunk task) - into
# occt_plugin's/adtl_plugin's own installed RPATH, so they find grunk's shared
# libraries at runtime without needing LD_LIBRARY_PATH. No effect on Windows -
# RPATH doesn't exist there; see grunk's own CMakeLists.txt for the platform-DLL-
# co-location alternative used there instead.
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if (DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_INSTALL_RPATH "$ENV{CONDA_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
endif()
if (APPLE)
    list(APPEND CMAKE_INSTALL_RPATH "@loader_path" "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif (NOT WIN32)
    list(APPEND CMAKE_INSTALL_RPATH "$ORIGIN" "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

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

# --- grunk: a pre-built, pre-installed dependency (see scripts/install_grunk.cmake,
# wired into pixi.toml's own `install_grunk` task, `configure`'s own depends-on) -
# not vendored via FetchContent anymore. FetchContent+add_subdirectory built grunk
# from source fresh *inside this project's own build*, meaning every consumer of
# grunk (this plugin, grunk-adolc's own plugin below, grunk's own Python bindings)
# ended up with its own independently compiled copy - each internally consistent,
# but not the *same* copy as any other consumer loaded into the same process (see
# DESIGN.md's "A related hazard, found outside the AD case" for what that caused in
# practice). find_package instead resolves against one shared install (grunk_DIR,
# or CMAKE_PREFIX_PATH - see .deps/grunk, this repo's own default via pixi.toml's
# activation.env) that every consumer pointed at it can share. Bump the pinned
# commit deliberately, in its own PR, gated by the full grunk-occt test suite -
# never as a side effect of an unrelated change (see scripts/install_grunk.cmake's own
# GRUNK_REV, which must move together with this comment whenever the pin changes -
# there is no single source of truth for it the way one CMake variable shared
# between the two files would be, since install_grunk.cmake runs as its own
# `cmake -P` script, in its own separate process, before this configure even starts
# - see pixi.toml's own `configure` task, whose `depends-on` runs it first).
find_package(grunk REQUIRED)

# --- OCCT: stock (conda-forge `occt` package) by default, or adOCCT instead when
# GRUNK_OCCT_ENABLE_AD is ON - find_package name is `OpenCASCADE` either way (set by
# OCCT's own upstream CMake config, independent of which build/package provides it).
# Only one of these two branches ever runs per configure.
if(GRUNK_OCCT_ENABLE_AD)
    # NO_DEFAULT_PATH + explicit PATHS: the pixi `ad` environment (see pixi.toml's
    # [environments]) deliberately has no `occt` package at all, so this guard isn't
    # normally load-bearing there - but CMAKE_PREFIX_PATH still points at whatever
    # $CONDA_PREFIX this was configured with, and someone can always reuse
    # ADOCCT_DIR against a different (occt-containing) environment by hand - defense
    # in depth against silently linking the wrong, non-AD OCCT either way.
    find_package(OpenCASCADE CONFIG REQUIRED PATHS "${ADOCCT_DIR}" NO_DEFAULT_PATH)

    # ADOL-C itself: found via pkg-config - needs ADOL-C's own lib{,64}/pkgconfig
    # directory on PKG_CONFIG_PATH (see README.md's "Building with AD support").
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ADOLC REQUIRED IMPORTED_TARGET GLOBAL adolc)

    # --- grunk-adolc (FetchContent - still vendors grunk-adolc's own *source*,
    # unlike grunk itself above, which is now a pre-built find_package dependency;
    # grunk-adolc's own CMakeLists.txt does its own find_package(grunk REQUIRED) -
    # see this project's own find_package(grunk) comment above): the sibling repo
    # whose own plugin registers
    # adtl::adouble (== Standard_Adouble, under adOCCT) as a native sol2 usertype -
    # see DESIGN.md's "Reusing one shared Standard_Real/adtl::adouble registration,
    # not two" for why occt_plugin itself never registers this type. Builds
    # grunk-adolc's own `adtl_plugin` target; the test harness below loads it
    # alongside occt_plugin. Only ever populated when GRUNK_OCCT_ENABLE_AD is ON -
    # adtl_plugin needs the exact same ADOL-C install occt_plugin was just pointed
    # at above (both must resolve to the same compiled adtl::adouble/Standard_Real
    # type), and has no reason to exist in a plain, non-AD build at all.
    #
    # Bumped from dd7dac1 to 233516a (Step 8: packaging, see DESIGN.md) to pick up
    # grunk-adolc's own install(TARGETS adtl_plugin ...) - this is what makes
    # `pixi run -e ad install` below finally install adtl_plugin alongside
    # occt_plugin, not just build it (see this file's own add_dependencies
    # (occt_plugin adtl_plugin) further down, and DESIGN.md's "Step 8: packaging"
    # for the full story of why that install used to be silently incomplete).
    include(FetchContent)

    FetchContent_Declare(grunk-adolc
        GIT_REPOSITORY https://github.com/grunk-dev/grunk-adolc.git
        GIT_TAG 233516a71e40cf55926f949f7b3a79432d710f9b
    )

    FetchContent_GetProperties(grunk-adolc)
    if(NOT grunk-adolc_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(grunk-adolc)

        # grunk-adolc's own CMakeLists.txt does its own find_package(grunk REQUIRED)
        # (see its own CMakeLists.txt comment) - CMAKE_PREFIX_PATH (the *environment*
        # variable, set by pixi.toml's own activation.env to include .deps/grunk,
        # not just the CMake-language variable this file itself appends to above) is
        # what makes that call resolve to the *same* installed grunk this project's
        # own find_package(grunk) call above already found - one shared install, not
        # a second, independently vendored copy.
        #
        # GRUNK_ADOLC_TESTS OFF is not optional, it's required: grunk-adolc's own
        # CMakeLists.txt (with its own test harness enabled) defines an
        # add_executable(runUnitTests ...) too - the exact same target name this
        # project's own test harness below uses. Leaving it ON would fail the
        # configure outright with a duplicate-target error.
        set(GRUNK_ADOLC_TESTS OFF CACHE BOOL "" FORCE)
        set(GRUNK_ADOLC_GENERATE_CODE OFF CACHE BOOL "" FORCE)

        # No EXCLUDE_FROM_ALL here (unlike the `grunk` add_subdirectory above) -
        # found while verifying Step 8's own install() actually works end-to-end,
        # not assumed: EXCLUDE_FROM_ALL doesn't just drop a subdirectory's targets
        # from the default `all` build target, it also drops that subdirectory's
        # own cmake_install.cmake from ever being `include()`d by the parent's own
        # install script - confirmed empirically (grep for "grunk-adolc" in
        # build-ad/cmake_install.cmake found nothing with EXCLUDE_FROM_ALL, despite
        # adtl_plugin's own cmake_install.cmake, one directory down, correctly
        # having its own install(TARGETS adtl_plugin ...) rule). An explicit
        # add_dependencies(occt_plugin adtl_plugin) further down was the first,
        # insufficient fix attempt - it made adtl_plugin *build* correctly but not
        # *install*, since that's a wholly separate mechanism from EXCLUDE_FROM_ALL.
        # Safe to drop EXCLUDE_FROM_ALL entirely here (unlike `grunk`'s own
        # add_subdirectory, which pulls in far more than this project needs):
        # GRUNK_ADOLC_TESTS is already forced OFF just above, so grunk-adolc's own
        # CMakeLists.txt defines no other target besides adtl_plugin itself in this
        # configuration - nothing extra ends up in `all` as a result.
        add_subdirectory(${grunk-adolc_SOURCE_DIR} ${grunk-adolc_BINARY_DIR})
    endif()
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
    # emission overload (see src/occt_ad_support.hpp's own to_real()) - the actual
    # Standard_Real/Standard_Adouble (== adtl::adouble) registration itself comes
    # from grunk-adolc's own plugin (adtl_plugin, built above), not from anything
    # here; see DESIGN.md's "Reusing one shared Standard_Real/adtl::adouble
    # registration, not two". Without adtl_plugin actually loaded into the same
    # grunk::state occt_plugin is, is<Standard_Real>() below could never be true for
    # anything and every derivative would silently read 0.
    target_compile_definitions(occt_plugin PRIVATE GRUNK_OCCT_WITH_ADOLC)
    target_link_libraries(occt_plugin PRIVATE PkgConfig::ADOLC)

    # adtl_plugin is *not* a link dependency of occt_plugin (the two .so's are
    # independent - a consuming application loads both separately at runtime, see
    # e.g. tests/test_gp.cpp's own SetUpTestSuite) - no add_dependencies needed
    # here for it to get built by a plain `cmake --build .`/`install`: the
    # FetchContent block above no longer adds it with EXCLUDE_FROM_ALL, so
    # adtl_plugin is already an unconditional part of the default `all` target
    # under this option, same as occt_plugin itself.
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

# Windows only - grunk's own libraries are SHARED now (see find_package(grunk)'s
# own comment above), and Windows has no RPATH/RUNPATH mechanism: a DLL loaded via
# an absolute path (grunk::plugin::load_native dlopens occt_plugin this way) has
# its *own* directory searched first for its own dependencies, so grunk_plugin.dll
# etc. (living in .deps/grunk/bin, a completely different directory) need to be
# copied next to occt_plugin.dll itself, not just found via PATH the way a
# conda-installed dependency (Lua's own DLL, already on PATH via pixi's own
# activation) already is.
if (WIN32)
    add_custom_command(TARGET occt_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:occt_plugin>
            $<TARGET_FILE_DIR:occt_plugin>
        COMMAND_EXPAND_LISTS
    )
endif()

# Step 8 (packaging polish, see DESIGN.md's "Packaging"): occt_plugin is a
# runtime-loaded grunk plugin, never linked against by a consuming application
# (grunk::plugin::load_native dlopens it by path - see src/occt_plugin.cpp's own
# top comment) - so, unlike grunk's own install() (grunk/CMakeLists.txt), there is
# no CMake target/package-config to export here, just the built shared library
# itself. LIBRARY/RUNTIME/ARCHIVE cover every platform's own convention for a
# SHARED target - Linux/macOS put the loadable library under LIBRARY, Windows
# under RUNTIME (the .dll) with its import .lib under ARCHIVE (generated
# alongside a SHARED target even though nothing links against it here).
# Under GRUNK_OCCT_ENABLE_AD, `cmake --install` also installs adtl_plugin - not
# declared here, grunk-adolc's own CMakeLists.txt (FetchContent'd above) already
# has its own install(TARGETS adtl_plugin ...), inherited automatically once its
# add_subdirectory runs above.
install(TARGETS occt_plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

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
        # check, only meaningful (and only compiles cleanly, since adtl.adouble/
        # occt.Standard_Real only round-trips AD state under this build) when
        # occt_plugin was actually built against adOCCT.
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
    if(GRUNK_OCCT_ENABLE_AD)
        # adOCCT's own Standard_Adouble.hxx unconditionally #includes <adolc/adtl.h>
        # whenever anything touches Standard_Real (not gated by our own
        # GRUNK_OCCT_WITH_ADOLC macro - that's adOCCT's doing, not ours) - so any TU
        # that reaches an OCCT header at all under this option needs ADOL-C's own
        # include dir, not just occt_plugin's TUs. test_collections.cpp is the one
        # test file that does (see its own comment above); link PkgConfig::ADOLC the
        # same way occt_plugin does above for consistency, since a future AD test
        # calling into ADOL-C directly would otherwise fail to link, not just to
        # compile.
        target_link_libraries(runUnitTests PRIVATE PkgConfig::ADOLC)

        # Every fixture's own SetUpTestSuite (see e.g. tests/test_gp.cpp) loads
        # grunk-adolc's own adtl_plugin into the same grunk::state as occt_plugin,
        # guarded by this same macro (occt_plugin's own generated code already sees
        # it via its own target_compile_definitions above - this is the test
        # executable's copy of the same flag) - see DESIGN.md's "Reusing one shared
        # Standard_Real/adtl::adouble registration, not two".
        target_compile_definitions(runUnitTests PRIVATE
            GRUNK_OCCT_WITH_ADOLC
            ADTL_PLUGIN_SO_PATH="$<TARGET_FILE:adtl_plugin>"
        )
        add_dependencies(runUnitTests adtl_plugin)
    endif()
    add_dependencies(runUnitTests occt_plugin)

    # Windows only - see occt_plugin's own, more detailed comment above. `pixi run
    # test` runs `ctest --test-dir build` directly against the build tree, before
    # any install step, so runUnitTests.exe needs grunk's DLLs (and any other
    # runtime DLL dependency) copied next to itself the same way.
    if (WIN32)
        add_custom_command(TARGET runUnitTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:runUnitTests>
                $<TARGET_FILE_DIR:runUnitTests>
            COMMAND_EXPAND_LISTS
        )
    endif()

    gtest_discover_tests(runUnitTests)
endif()
