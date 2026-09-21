<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# grunk-occt

A [grunk](https://github.com/grunk-dev/grunk) plugin exposing OpenCASCADE (OCCT) as building
blocks (types/functions) for grunk dynamic models, registered under the Lua namespace `occt`.

This is a from-scratch rewrite of [grocc](https://gitlab.dlr.de/paradigms/grocc), grunk's legacy
OCCT plugin, targeting the current, rewritten grunk (Lua/sol2 dynamic layer, native `grunk::plugin`
ABI, pixi/conda-forge build) rather than the old Conan-based, `reflect`-based grunk v0.3.2. See
`DESIGN.md` for the concrete decisions made along the way, and the migration plan this repo is
being built against for the full step-by-step rationale.

**Status**: early / WIP. Wrapped so far, all generated automatically by `codegen/` (see below),
not hand-written: `gp` (`gp_Pnt`, `gp_Dir`, `gp_Vec`, `gp_Ax1`, `gp_Trsf`), `geom` (`Geom_Geometry`,
`Geom_Curve`, `Geom_Line` - `Standard_Transient`/`Handle(T)` support, `add_bases<...>()`), and
shapes (`topods`, `brepbuilderapi`, `brepprimapi`, `brepalgoapi` - plain value-type inheritance,
static-method/namespace flattening, box/cylinder/cut boolean-op parity with legacy grocc's own
smoke test). `src/geom/geom_extras.hpp` and `src/shape_io.hpp` are small hand-written escape
hatches (`Handle(T)::DownCast`, `BRepTools::Write` - both need something the generator can't
introspect/expand).

## Build & test

All workflows go through [pixi](https://pixi.sh/):

```
pixi run configure [Debug|Release]   # cmake configure into build/
pixi run build                       # cmake --build build/
pixi run test                        # ctest --test-dir build
pixi run all                         # configure + build + test
pixi run generate_code               # regenerate generated/ from codegen/config.yml (needs
                                      # -D GRUNK_OCCT_GENERATE_CODE=ON at configure time)
```

## Building with AD support

`occt_plugin` can also be built against [adOCCT](https://github.com/dlr-sp/adOCCT), a fork of OCCT
that redefines `Standard_Real` to carry an [ADOL-C](https://github.com/coin-or/ADOL-C) algorithmic
derivative alongside its value, activating an AD-aware variant of every generated binding with no
source changes (see `DESIGN.md`'s "AD validation milestone"/"`Standard_Real` / AD-portability
rule"). This is the one accepted platform exception in the migration plan - **Linux-only,
best-effort, no CI** (adOCCT/ADOL-C's own build is involved enough that this was never attempted to
be made CI-friendly).

It's a build **option** on this same project (`GRUNK_OCCT_ENABLE_AD`), not a separate repo/CMake
tree - pick one variant per build directory:

```bash
pixi run install_adolc     # download, build, install ADOL-C 2.7.2 into .deps/adolc (idempotent)
pixi run install_adocct     # clone, build, install adOCCT into .deps/adocct (idempotent, involved)
pixi run configure_ad       # cmake configure into build-ad/, with GRUNK_OCCT_ENABLE_AD=ON
pixi run build_ad           # cmake --build build-ad/
pixi run test_ad             # ctest --test-dir build-ad (tests/test_ad.cpp - a closed-form
                              # derivative check on occt_plugin's own generated bindings)
pixi run all_ad               # all of the above, in order
```

`install_adocct`'s own `cmake`/`ninja install` invocation was not run/verified in the environment
these instructions were written in (no adOCCT/ADOL-C build access - see `DESIGN.md`'s "What could
not be verified in this environment") - the CMake-side wiring (`GRUNK_OCCT_ENABLE_AD`'s
`find_package(OpenCASCADE)`/`pkg_check_modules(ADOLC)` branch, `occt_plugin`'s AD-gated compile
definitions/linking) *was* verified end-to-end against a real ADOL-C install and a stand-in OCCT,
confirming it fails only where expected (`Standard_Adouble` genuinely not existing outside adOCCT
itself) - see `scripts/install_adocct.sh`'s own comment for what adjusting `3RDPARTY_*_DIR`
variables for your system might involve if adOCCT's own third-party dependency resolution needs it.

**Runtime note**: the AD-enabled `occt_plugin` links against ADOL-C's own `libadolc.so` and
adOCCT's own OCCT libraries at runtime, not just at link time - `pixi.toml`'s own `LD_LIBRARY_PATH`
(via `$PIXI_PROJECT_ROOT`, covering `.deps/adolc`/`.deps/adocct`) already handles this for every
`pixi run` task.

**dlopen/SONAME hazard, documented not solved**: `grunk::plugin::load_native` never `dlclose`s. A
consuming application that loads *both* the stock `occt_plugin` (`build/`) and the AD-enabled one
(`build-ad/`) into the same process risks the dynamic linker resolving the second's OCCT symbol
references against the first's already-mapped ones if their SONAMEs collide - pick one variant per
process.

## Project structure

```
codegen/            # libclang-based code generator (see codegen/README.md)
generated/           # its output - checked into git, compiled unconditionally
src/                 # occt_plugin.cpp (plugin entry point) + hand-written escape hatches
scripts/             # ADOL-C/adOCCT bootstrap scripts (see "Building with AD support" above)
tests/               # one gtest file (TEST_F suite) per module, one runUnitTests binary total;
                     # test_ad.cpp only builds/runs under GRUNK_OCCT_ENABLE_AD
examples/            # ported legacy recipes, run via `pixi run run_examples` (Step 6.5)
reference/legacy-recipes/  # legacy grocc-only example recipes, ported in Step 6.5
```

## Dependencies

| Dependency | Source |
| -- | -- |
| grunk | FetchContent from GitHub, pinned to a specific commit (see `DESIGN.md`) |
| occt (OpenCASCADE) | conda-forge, pinned to `7.9.3` |
| lua, sol2, taskflow, yaml-cpp, libparametric | conda-forge (needed to build grunk itself from source) |
| python, pyyaml, libclang (PyPI) | conda-forge/PyPI, needed only for `codegen/` (opt-in) |
| gtest | conda-forge, the test framework (`runUnitTests`) |
| ADOL-C, adOCCT | built from source via `pixi run install_adolc`/`install_adocct` - only needed for `GRUNK_OCCT_ENABLE_AD` (see "Building with AD support") |

## License

This project is licensed under the Mozilla Public License 2.0.
