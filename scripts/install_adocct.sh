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
# X11 dev headers (libx11-dev et al., installed by ci.yml's own "Install
# autotools" step) are a real build-time requirement here even though this
# repo's own registration source never touches AIS_*/V3d_*/OpenGl_*/Xw_* -
# confirmed directly: passing -D BUILD_MODULE_Visualization=OFF does NOT drop
# TKService (the toolkit whose Aspect_DisplayConnection.cxx #includes
# X11/Xlib.h) from the build, because OCCT's own CMakeLists.txt resolves
# BUILD_TOOLKITS transitively from whatever toolkits the *enabled* modules need
# (EXCTRACT_TOOLKIT_FULL_DEPS) - DataExchange's own STEP toolkits (needed by
# ../src/shape_io.hpp's STEPControl_Writer) pull in TKService regardless of
# Visualization's own module flag. The conda-forge `occt` package the *stock*
# (non-AD) build uses (`novtk_*` build string, see [feature.occt.dependencies]
# in pixi.toml) is prebuilt and ships its own X11 runtime closure, so it never
# hit this - adOCCT compiles from source here and has no such luxury.
#
# fontconfig dev headers (libfontconfig1-dev, also installed by ci.yml, not by
# this repo's own pixi.toml) are the same story as X11: adOCCT's own
# adm/cmake/occt_csf.cmake sets CSF_fontconfig to the bare literal
# "fontconfig" (a plain linker name, no 3RDPARTY_*_DIR/CMAKE_PREFIX_PATH
# detection at all, unlike freetype's proper find_package(Freetype)) - so it
# only ever resolves via the compiler/linker's default *system* search paths.
# A conda `fontconfig` package was tried first and confirmed NOT to work for
# this reason (the CI failure was unchanged after adding it) before apt was
# used instead.
#
# BUILD_MODULE_Visualization=OFF *is* still worth setting, though, despite not
# helping with X11/TKService above: TKOpenGl (OpenGl_Window.cxx's own
# GL/glx.h #include) is a Visualization-only toolkit with no DataExchange-side
# pull keeping it around, unlike TKService - confirmed directly (this is what
# a from-source CI build hit next, after the X11 fix, at object 3397/5926:
# "fatal error: GL/glx.h: No such file or directory"). ApplicationFramework
# (OCAF/TDocStd) and Draw (OCCT's own DRAWEXE test harness, pulls in Tcl/Tk)
# are excluded the same way - on the same "never referenced by this plugin's
# own registration source" basis as Visualization, not because either one hit
# an observed build failure. Pruning these three cuts the adOCCT build down
# to only DataExchange/ModelingAlgorithms/ModelingData/FoundationClasses/
# DETools and their real transitive deps, instead of OCCT's entire module set -
# meaningfully reducing both build time and the odds of hitting yet another
# unrelated system-library requirement belonging to a module this plugin
# never uses.
#
# Stopgap, not a permanent fix: this module pruning (and the X11 apt
# dependency in ci.yml) exists only because adOCCT is built from source in CI.
# See DESIGN.md's "AD validation milestone" section - once grunk-occt/adOCCT
# publish prebuilt binary packages, build-and-test-ad should download those
# instead of running this script at all, and this entire file (plus the X11
# apt-get step in ci.yml) should be deleted rather than kept in sync.
#
# Run and verified end-to-end (see DESIGN.md's "What could and could not be verified
# in this environment") and now runs on every PR via .github/workflows/ci.yml's
# build-and-test-ad job. OCCT's own third-party dependency resolution is large and
# version-sensitive enough that you may still need to adjust individual
# 3RDPARTY_*_DIR variables on a machine unlike the CI runner - consult adOCCT's own
# README and OCCT's own INSTALL.md if the cmake invocation below can't resolve a
# dependency CMAKE_PREFIX_PATH doesn't cover.
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
    -D BUILD_MODULE_Visualization=OFF \
    -D BUILD_MODULE_ApplicationFramework=OFF \
    -D BUILD_MODULE_Draw=OFF \
    ..
ninja install

echo "adOCCT installed to $ADOCCT_PREFIX"
