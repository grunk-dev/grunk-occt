#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Downloads, builds, and installs adOCCT (https://github.com/dlr-sp/adOCCT) into
# .deps/adocct, built against the ADOL-C install produced by `pixi run
# install_adolc` - run via `pixi run install_adocct` (see README.md's "Building
# with AD support"). Idempotent: skips the build if that prefix already has an
# OpenCASCADEConfig.cmake (i.e. install completed last time).
#
# Installs to .deps/, not build/_deps/ - deliberately outside the CMake build
# directory (see install_adolc.sh's own comment for why: a routine `rm -rf build`
# must never force rebuilding all of OCCT from scratch too). Only the throwaway
# clone+build directory lives under build/_deps/.
#
# adOCCT is a full OCCT source build - same third-party dependency closure as any
# OCCT build (freetype etc.) - reused here via CMAKE_PREFIX_PATH=$CONDA_PREFIX
# (conda-forge's own `occt` package, already a root dependency, pulls the same
# closure) rather than resolved a second time. USE_ADOLC=ON/ADOLC_REVERSE_MODE=OFF
# (tapeless/forward mode, matching this repo's own forward-mode-only getADValue/
# setADValue usage - see ../src/occt_ad_support.hpp) and 3RDPARTY_ADOLC_DIR are
# taken directly from grunk's own cad_autodiff example, which built adOCCT
# successfully this way.
#
# NOT run/verified in the environment this script was written in (no adOCCT/ADOL-C
# build access - see DESIGN.md's "What could not be verified in this environment").
# OCCT's own third-party dependency resolution is large and version-sensitive
# enough that you may need to adjust individual 3RDPARTY_*_DIR variables for your
# system - consult adOCCT's own README and OCCT's own INSTALL.md if the cmake
# invocation below can't resolve a dependency CMAKE_PREFIX_PATH doesn't cover.
set -euo pipefail

ROOT="${PIXI_PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ADOLC_PREFIX="$ROOT/.deps/adolc"
ADOCCT_PREFIX="$ROOT/.deps/adocct"

if [ -f "$ADOCCT_PREFIX/lib/cmake/opencascade/OpenCASCADEConfig.cmake" ] || \
   [ -f "$ADOCCT_PREFIX/lib64/cmake/opencascade/OpenCASCADEConfig.cmake" ]; then
    echo "adOCCT already installed at $ADOCCT_PREFIX - skipping (delete this directory to force a rebuild)."
    exit 0
fi

if [ ! -f "$ADOLC_PREFIX/include/adolc/adtl.h" ]; then
    echo "ADOL-C not found at $ADOLC_PREFIX - run 'pixi run install_adolc' first." >&2
    exit 1
fi

if [ -z "${CONDA_PREFIX:-}" ]; then
    echo "CONDA_PREFIX is not set - run this via 'pixi run install_adocct', not directly." >&2
    exit 1
fi

mkdir -p "$ROOT/build/_deps"
cd "$ROOT/build/_deps"
if [ ! -d adOCCT ]; then
    git clone https://github.com/dlr-sp/adOCCT.git
fi
cd adOCCT

mkdir -p build && cd build
cmake -G Ninja \
    -D CMAKE_INSTALL_PREFIX="$ADOCCT_PREFIX" \
    -D CMAKE_PREFIX_PATH="$CONDA_PREFIX" \
    -D USE_ADOLC=ON \
    -D ADOLC_REVERSE_MODE=OFF \
    -D 3RDPARTY_ADOLC_DIR="$ADOLC_PREFIX" \
    ..
ninja install

echo "adOCCT installed to $ADOCCT_PREFIX"
