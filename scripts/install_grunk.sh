#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Clones, builds, and installs grunk into .deps/grunk (or .deps/<$1>, see below) -
# run via `pixi run install_grunk`. Idempotent: skips the whole build if that
# prefix already looks populated, so re-running this after a partial failure
# elsewhere doesn't rebuild grunk from scratch every time - matches scripts/
# install_adocct.sh's own pattern exactly (see its own comment for why .deps/, not
# build/_deps/).
#
# Optional $1: an alternate install-directory *name* under .deps/ (e.g. "grunk-ad"
# for [feature.ad.tasks]'s own invocation) - defaults to "grunk". Needed because
# this repo has *two* pixi environments (default and ad), each its own conda
# environment with its own Lua/yaml-cpp builds - sharing one .deps/grunk between
# them would mean whichever environment runs this script first "wins", and the
# other silently links occt_plugin/adtl_plugin against a grunk built against a
# *different* environment's Lua/yaml-cpp than the one it's actually running in
# (confirmed the hard way: CMake's own "cannot generate a safe runtime search
# path... cycle in the constraint graph" warning, from .deps/grunk's baked-in
# RPATH pointing at .pixi/envs/default/lib while configuring the ad environment).
#
# CMakeLists.txt used to FetchContent+add_subdirectory grunk's source directly,
# statically linking its compiled libraries into occt_plugin - replaced with this
# bootstrap + find_package(grunk REQUIRED) so occt_plugin (and, via its own
# FetchContent of grunk-adolc's source for the AD build, adtl_plugin too) instead
# dynamically links grunk's own SHARED libraries (grunk's own CMakeLists.txt builds
# grunk_recipe/grunk_plugin as SHARED - see its own top-level CMakeLists.txt
# comment). A plugin that statically embeds its own private copy of grunk, loaded
# into the same process as anything else that does the same (this project's own
# Python bindings via examples/run_example.py, or a sibling plugin), is a real
# hazard this repo's own DESIGN.md documents ("A related hazard, found outside the
# AD case"). Every consumer of grunk pointed at the *same* install (this one, or
# another - see grunk_DIR below) shares one physical set of shared libraries
# instead, eliminating any possibility of a build-configuration/ABI mismatch
# between independently vendored copies too, not just duplicated compiled code.
set -euo pipefail

ROOT="${PIXI_PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
GRUNK_NAME="${1:-grunk}"
GRUNK_PREFIX="$ROOT/.deps/$GRUNK_NAME"
GRUNK_REV="a372159c47d3722e138634ad9997c8037c6fd422"

if [ -f "$GRUNK_PREFIX/lib/cmake/grunk/grunkConfig.cmake" ]; then
    echo "grunk already installed at $GRUNK_PREFIX - skipping (delete this directory to force a rebuild)."
    exit 0
fi

mkdir -p "$ROOT/build/_deps"
cd "$ROOT/build/_deps"

if [ ! -d "$GRUNK_NAME" ]; then
    git clone https://github.com/grunk-dev/grunk.git "$GRUNK_NAME"
fi
cd "$GRUNK_NAME"
git fetch origin "$GRUNK_REV"
git checkout "$GRUNK_REV"

# GRUNK_PYTHON=OFF - this bootstrap only ever needs to satisfy occt_plugin's own
# C++ find_package(grunk REQUIRED); grunk's own Python bindings are a separate,
# pip-managed dependency (see pixi.toml's [pypi-dependencies]).
cmake -G Ninja -S . -B build \
    -D CMAKE_BUILD_TYPE=Release \
    -D CMAKE_INSTALL_PREFIX="$GRUNK_PREFIX" \
    -D GRUNK_WITH_TASKFLOW=OFF \
    -D GRUNK_WITH_DYNAMIC=ON \
    -D GRUNK_WITH_RECIPE=ON \
    -D GRUNK_WITH_PLUGIN=ON \
    -D GRUNK_TESTS=OFF \
    -D GRUNK_PYTHON=OFF
cmake --build build --parallel
cmake --install build

echo "grunk installed to $GRUNK_PREFIX"
ls "$GRUNK_PREFIX"/lib/libgrunk* 2>/dev/null
