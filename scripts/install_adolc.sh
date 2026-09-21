#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

# Downloads, builds, and installs ADOL-C 2.7.2 into .deps/adolc - run via
# `pixi run install_adolc` (see README.md's "Building with AD support"). Idempotent:
# skips the whole build if that prefix already looks populated, so re-running this
# after a partial failure elsewhere doesn't rebuild ADOL-C from scratch every time.
# Close to grunk-adolc's own scripts/install_adolc.sh - a generic ADOL-C bootstrap,
# not specific to either repo, except for *where* it installs to: .deps/, not
# build/_deps/ - deliberately outside the CMake build directory, so a routine
# `rm -rf build` (reconfiguring, switching build types, ...) never forces
# rebuilding ADOL-C (or, via install_adocct.sh, all of adOCCT) from scratch too.
# Only the throwaway clone+build directory lives under build/_deps/, matching
# CMake's own FetchContent naming convention for what it is - a scratch checkout,
# not the thing that needs to survive.
#
# Needs autoreconf/automake/libtool on PATH (see README.md's Prerequisites) - not a
# pixi dependency of this repo, since nothing else here needs autotools.
set -euo pipefail

ROOT="${PIXI_PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ADOLC_PREFIX="$ROOT/.deps/adolc"

if [ -f "$ADOLC_PREFIX/include/adolc/adtl.h" ]; then
    echo "ADOL-C already installed at $ADOLC_PREFIX - skipping (delete this directory to force a rebuild)."
    exit 0
fi

mkdir -p "$ROOT/build/_deps"
cd "$ROOT/build/_deps"

if [ ! -d ADOL-C ]; then
    git clone https://github.com/coin-or/ADOL-C.git
fi
cd ADOL-C
git checkout releases/2.7.2

mkdir -p "$ADOLC_PREFIX"
autoreconf -fi
./configure --prefix="$ADOLC_PREFIX" --enable-atrig-erf --with-boost=no
make -j
make install

echo "ADOL-C installed to $ADOLC_PREFIX"
ls "$ADOLC_PREFIX"/lib* 2>/dev/null
