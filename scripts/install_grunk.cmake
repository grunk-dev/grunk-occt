# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Clones, builds, and installs grunk into .deps/grunk (or .deps/<GRUNK_INSTALL_NAME>,
# see below) - run via `cmake -P scripts/install_grunk.cmake` (see pixi.toml's own
# install_grunk task). A plain CMake script (`cmake -P`), not a shell script - unlike
# scripts/install_adolc.sh/install_adocct.sh (Linux-only, matching the `ad` feature
# they belong to), this bootstrap runs as part of the `default` environment, which
# targets Linux, macOS, *and* Windows - bash isn't available on Windows by default,
# but cmake itself is already a hard dependency everywhere this script runs, so a
# CMake script works identically on all three platforms without needing a separate
# batch/PowerShell port. Idempotent: skips the whole build if that prefix already
# looks populated, so re-running this after a partial failure elsewhere doesn't
# rebuild grunk from scratch every time - matches scripts/install_adolc.sh's own
# pattern (see its own comment for why .deps/, not build/_deps/).
#
# Optional -D GRUNK_INSTALL_NAME=<name>: an alternate install-directory name under
# .deps/ (e.g. "grunk-ad" for [feature.ad.tasks]'s own invocation) - defaults to
# "grunk". Needed because this repo has *two* pixi environments (default and ad),
# each its own conda environment with its own Lua/yaml-cpp builds - sharing one
# .deps/grunk between them would mean whichever environment runs this script first
# "wins", and the other silently links occt_plugin/adolc_plugin against a grunk built
# against a *different* environment's Lua/yaml-cpp than the one it's actually
# running in (confirmed the hard way: CMake's own "cannot generate a safe runtime
# search path... cycle in the constraint graph" warning, from .deps/grunk's baked-in
# RPATH pointing at .pixi/envs/default/lib while configuring the ad environment).
#
# CMakeLists.txt used to FetchContent+add_subdirectory grunk's source directly,
# statically linking its compiled libraries into occt_plugin - replaced with this
# bootstrap + find_package(grunk REQUIRED) so occt_plugin (and, via its own
# FetchContent of grunk-adolc's source for the AD build, adolc_plugin too) instead
# dynamically links grunk's own SHARED libraries (grunk's own CMakeLists.txt builds
# grunk_recipe/grunk_plugin as SHARED - see its own top-level CMakeLists.txt
# comment). A plugin that statically embeds its own private copy of grunk, loaded
# into the same process as anything else that does the same (this project's own
# Python bindings via examples/run_example.py, or a sibling plugin), is a real
# hazard this repo's own DESIGN.md documents ("A related hazard, found outside the
# AD case"). Every consumer of grunk pointed at the *same* install (this one, or
# another - see grunk_DIR/CMAKE_PREFIX_PATH) shares one physical set of shared
# libraries instead, eliminating any possibility of a build-configuration/ABI
# mismatch between independently vendored copies too, not just duplicated code.

cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED GRUNK_INSTALL_NAME)
    set(GRUNK_INSTALL_NAME "grunk")
endif()
# Bumped 2026-09-29 from 583da89f0184ef6333000c45573da65a9dd551d0 to
# d9b079905d4eccaa08d51265ab2f6ef6b76786cd - lands grunk.codegen's new
# sol_base_classes.hpp generation (SOL_BASE_CLASSES/SOL_DERIVED_CLASSES,
# aggregated across a whole run, plus a unique_usertype_traits::rebind_actual_type
# member src/occt_sol_traits.hpp now defines), the missing piece that makes
# sol2's own Handle(Derived)->Handle(Base) unique-usertype conversion work at
# all - see DESIGN.md's "Handle(Derived)->Handle(Base) argument conversion:
# real root cause and fix" for the full writeup. Also carries the earlier
# DynamicFeature sol::meta_function::call fix (c0cc16d) this repo never
# consumed before (its own pin was still on the original 583da89f commit).
# Bumped again 2026-09-29 to 13ab6232fdcd7b7e20a916392cfbb607417fb143 - lands
# a fix to with_std_vector()'s own cast_to_element (was taking its element by
# reference, excluded from sol2's base-aware unique-usertype conversion just
# like any other by-reference Handle(X) parameter). Lets Geom_Curve gain a
# native .with_std_vector<opencascade::handle<Geom_Curve>>() - see DESIGN.md.
# Bumped again 2026-09-29 to 40d8a1ebd9f020ba3481b9949437d2aafe95425a - lands a
# real usability fix, not a bug: a mutating member function (non-const `self`,
# void return - e.g. gp_GTrsf::SetValue) called through the decorated/parametric
# environment now throws immediately with a clear message instead of silently
# doing nothing (grunk's own evaluation is lazy/pull-based - the call's own
# result feature, if never read, was simply never evaluated, so the real C++
# mutation never ran at all). Found the hard way in grunk-geoml's own
# wing_loft.grr.yml (three separate bare gp_GTrsf:SetValue(...) statements each
# silently a no-op) - see that repo's own DESIGN.md for the full story.
set(GRUNK_REV "40d8a1ebd9f020ba3481b9949437d2aafe95425a")

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(GRUNK_PREFIX "${PROJECT_ROOT}/.deps/${GRUNK_INSTALL_NAME}")
set(GRUNK_CHECKOUT "${PROJECT_ROOT}/build/_deps/${GRUNK_INSTALL_NAME}")

if(EXISTS "${GRUNK_PREFIX}/lib/cmake/grunk/grunkConfig.cmake")
    message(STATUS "grunk already installed at ${GRUNK_PREFIX} - skipping (delete this directory to force a rebuild).")
    return()
endif()

# A small helper so every step below fails loudly (and stops the script) the same
# way, instead of silently continuing past a broken clone/configure/build/install.
function(run_or_die)
    # ${ARGV} already starts with COMMAND (every call site below spells it out for
    # readability, matching execute_process's own keyword syntax) - do not prepend
    # a second COMMAND here, or execute_process sees "COMMAND COMMAND ..." and
    # fails with "given COMMAND argument with no value".
    execute_process(${ARGV} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Command failed (exit ${_result}): ${ARGV}")
    endif()
endfunction()

if(NOT EXISTS "${GRUNK_CHECKOUT}/.git")
    file(MAKE_DIRECTORY "${PROJECT_ROOT}/build/_deps")
    run_or_die(COMMAND git clone https://github.com/grunk-dev/grunk.git "${GRUNK_INSTALL_NAME}"
        WORKING_DIRECTORY "${PROJECT_ROOT}/build/_deps")
endif()

run_or_die(COMMAND git fetch origin "${GRUNK_REV}" WORKING_DIRECTORY "${GRUNK_CHECKOUT}")
run_or_die(COMMAND git checkout "${GRUNK_REV}" WORKING_DIRECTORY "${GRUNK_CHECKOUT}")

# GRUNK_PYTHON=OFF - this bootstrap only ever needs to satisfy occt_plugin's own
# C++ find_package(grunk REQUIRED); grunk's own Python bindings are a separate,
# pip-managed dependency (see pixi.toml's [pypi-dependencies]).
run_or_die(COMMAND "${CMAKE_COMMAND}" -G Ninja -S . -B build
        -D CMAKE_BUILD_TYPE=Release
        -D CMAKE_INSTALL_PREFIX=${GRUNK_PREFIX}
        -D GRUNK_WITH_TASKFLOW=OFF
        -D GRUNK_WITH_DYNAMIC=ON
        -D GRUNK_WITH_RECIPE=ON
        -D GRUNK_WITH_PLUGIN=ON
        -D GRUNK_TESTS=OFF
        -D GRUNK_PYTHON=OFF
    WORKING_DIRECTORY "${GRUNK_CHECKOUT}")
run_or_die(COMMAND "${CMAKE_COMMAND}" --build build --parallel WORKING_DIRECTORY "${GRUNK_CHECKOUT}")
run_or_die(COMMAND "${CMAKE_COMMAND}" --install build WORKING_DIRECTORY "${GRUNK_CHECKOUT}")

message(STATUS "grunk installed to ${GRUNK_PREFIX}")
