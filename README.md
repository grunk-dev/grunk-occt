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

All workflows go through [pixi](https://pixi.sh/). This repo has two pixi environments -
`default` (stock OCCT) and `ad` (see "Building with AD support" below) - each with their own,
otherwise identically-named tasks (`configure`/`build`/`test`/`all`), so every invocation needs an
explicit `-e`/`--environment`:

```
pixi run -e default configure [Debug|Release]   # cmake configure into build/
pixi run -e default build                       # cmake --build build/
pixi run -e default test                        # ctest --test-dir build
pixi run -e default install                     # cmake --install build (see "Packaging / installing" below)
pixi run -e default all                         # configure + build + test
pixi run -e default generate_code               # regenerate generated/ from codegen/config.yml (needs
                                                  # -D GRUNK_OCCT_GENERATE_CODE=ON at configure time)
```

## Building with AD support

`occt_plugin` can also be built against [adOCCT](https://github.com/dlr-sp/adOCCT), a fork of OCCT
that redefines `Standard_Real` to carry an [ADOL-C](https://github.com/coin-or/ADOL-C) algorithmic
derivative alongside its value, activating an AD-aware variant of every generated binding with no
source changes (see `DESIGN.md`'s "AD validation milestone"/"`Standard_Real` / AD-portability
rule"). This is the one accepted platform exception in the migration plan - **Linux-only**, matching
`pixi.toml`'s own `ad` feature (`platforms = ["linux-64"]`) - but it does run in CI:
`.github/workflows/ci.yml`'s `build-and-test-ad` job runs the same sequence below on every PR, with
`.deps/adolc`/`.deps/adocct` cached across runs so it isn't rebuilding ADOL-C/adOCCT from source
every time.

It's a build **option** on this same project (`GRUNK_OCCT_ENABLE_AD`), not a separate repo/CMake
tree - pick one variant per build directory:

```bash
pixi run -e ad install_adolc     # download, build, install ADOL-C 2.7.2 into .deps/adolc (idempotent)
pixi run -e ad install_adocct     # clone, build, install adOCCT into .deps/adocct (idempotent, involved)
pixi run -e ad configure          # cmake configure into build-ad/, with GRUNK_OCCT_ENABLE_AD=ON
pixi run -e ad build              # cmake --build build-ad/
pixi run -e ad test                # ctest --test-dir build-ad (tests/test_ad.cpp - a closed-form
                                    # derivative check on occt_plugin's own generated bindings)
pixi run -e ad all                  # all of the above, in order
```

The `ad` environment (see `pixi.toml`'s `[environments]`) is entirely separate from `default`'s own
- no `occt` package, its own `.pixi/envs/ad`, and its own `LD_LIBRARY_PATH`/`PKG_CONFIG_PATH`
pointing at `.deps/adolc`/`.deps/adocct` - so installing it never affects a plain, non-AD
`pixi run -e default ...` invocation (or vice versa).

`install_adocct`'s own `cmake`/`ninja install` invocation has been run and verified end-to-end in
this environment (see `DESIGN.md`'s "What could and could not be verified in this environment") and
now runs on every PR via `build-and-test-ad` - see `scripts/install_adocct.sh`'s own comment for
what adjusting `3RDPARTY_*_DIR` variables for your system might involve if adOCCT's own third-party
dependency resolution needs it on a machine unlike the CI runner's.

**Runtime note**: the AD-enabled `occt_plugin` links against ADOL-C's own `libadolc.so` and
adOCCT's own OCCT libraries at runtime, not just at link time - `pixi.toml`'s own `ad` environment
sets `LD_LIBRARY_PATH` (via `$PIXI_PROJECT_ROOT`, covering `.deps/adolc`/`.deps/adocct`) to handle
this already, for every `pixi run -e ad ...` task - deliberately *not* set for `-e default`, so a
plain, non-AD build/test never risks resolving OCCT symbols against adOCCT's own, differently-built
libraries instead of stock OCCT's.

**dlopen/SONAME hazard, documented not solved**: `grunk::plugin::load_native` never `dlclose`s. A
consuming application that loads *both* the stock `occt_plugin` (`build/`) and the AD-enabled one
(`build-ad/`) into the same process risks the dynamic linker resolving the second's OCCT symbol
references against the first's already-mapped ones if their SONAMEs collide - pick one variant per
process.

**Known issue, `tank_model.grr.yml` under AD**: one of the `dome` sub-recipe's own side-wedge
boolean intersections (`BRepAlgoAPI_Common`/`BRepAlgoAPI_Cut`) comes back an empty shape under
adOCCT specifically, where the plain build's own boolean op doesn't - a real numerical/algorithmic
difference in the third-party adOCCT fork for this particular geometry, not a bug in grunk-occt's
own binding code (root-caused via `gdb`: `TopoDS_Builder::Add` - called from
`examples/plugins/occt_helpers.lua`'s own `compound()` - dereferences a component shape's `TShape`
unconditionally, with no null check of its own, so handing it that empty shape used to segfault the
whole process outright). `compound()` now checks `IsNull()` before every `Add` and raises a clean,
catchable Lua error instead (via the new `occt.error(...)`, `src/math_extras.hpp` - plain Lua
`error()`/`assert()` aren't reachable from a recipe's own decorated environment at all, the same
restriction `tests/test_load.cpp` already documents), and `run_example.py` reports this specific,
understood failure rather than aborting before reaching the AD derivative check, which is
unaffected. `bspline_curve.grr.yml` itself (both its primal value and, under `-e ad`, its AD
derivative - checked against a finite-difference approximation) was independently verified and
isn't affected by this at all.
(An earlier version of this note misdiagnosed this as a segfault in grunk's own Python bindings,
found while debugging *this* issue through them - that symptom was real but was a **separate**,
unrelated bug: `environment.get_feature(...).value()`'s own Python binding crashes on any value at
all, in both environments identically, confirmed via a bare `grunk.state()` with no plugin loaded;
`recipe.get_output(...).value()` doesn't have this problem and is what every recipe in this repo
already uses, so it was never actually hit outside of ad hoc debugging scripts.)

## Packaging / installing

`occt_plugin` is a runtime-loaded grunk plugin - a consuming application never links against it,
it dlopens it by path via `grunk::plugin::load_native` (see `src/occt_plugin.cpp`'s own top
comment). So there's no CMake package config to `find_package()` here, just the built shared
library itself:

```
pixi run -e default install   # -> build/install/lib/libocct_plugin.so (.dylib on macOS, bin/occt_plugin.dll on Windows)
pixi run -e ad install        # -> build-ad/install/lib/{libocct_plugin.so,libadtl_plugin.so}
```

Point `grunk::plugin::load_native` (or its Python equivalent) at the installed path instead of a
path under `build/`/`build-ad/` directly once you're consuming this outside the build tree -
everything under `examples/`/`tests/` in this repo itself still references the build tree
directly, since they're meant to run right after `build`, not `install`. `CMAKE_INSTALL_PREFIX`
(set by the `configure` task, `build/install`/`build-ad/install`) can be overridden - either edit
`pixi.toml`'s own `configure` task, or run `cmake --install build --prefix <path>` yourself. The
`ad` environment's own `install` installs both `occt_plugin` and grunk-adolc's own `adtl_plugin`
(FetchContent'd alongside it - see `CMakeLists.txt`'s own comment on why this needed an explicit
`add_dependencies(occt_plugin adtl_plugin)`, not just relying on it happening to already be built
for the test harness) - both are needed together for an AD-enabled consumer, same as
`run_example.py`'s own two-argument invocation under `-e ad`.

## Project structure

```
codegen/            # libclang-based code generator (see codegen/README.md)
generated/           # its output - checked into git, compiled unconditionally
src/                 # occt_plugin.cpp (plugin entry point) + hand-written escape hatches
scripts/             # ADOL-C/adOCCT bootstrap scripts (see "Building with AD support" above)
tests/               # one gtest file (TEST_F suite) per module, one runUnitTests binary total;
                     # test_ad.cpp only builds/runs under GRUNK_OCCT_ENABLE_AD
examples/            # ported legacy recipes (Step 6.5) plus bspline_curve.grr.yml (AD
                     # validation, see "Building with AD support" above) - run via
                     # `pixi run -e default run_examples` / `pixi run -e ad run_examples`
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
| ADOL-C, adOCCT | built from source via `pixi run -e ad install_adolc`/`install_adocct` - only needed for `GRUNK_OCCT_ENABLE_AD` (see "Building with AD support") |

## License

This project is licensed under the Mozilla Public License 2.0.
