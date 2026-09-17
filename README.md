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

**Status**: early / WIP. The `gp` module (`gp_Pnt`, `gp_Dir`, `gp_Vec`, `gp_Ax1`, `gp_Trsf`) is
wrapped - generated automatically by `codegen/` (see below), not hand-written.

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

## Project structure

```
codegen/            # libclang-based code generator (see codegen/README.md)
generated/           # its output - checked into git, compiled unconditionally
src/                 # occt_plugin.cpp (plugin entry point) + hand-written escape hatches
tests/               # one test executable per module, loads occt_plugin.so at runtime
reference/legacy-recipes/  # legacy grocc-only example recipes, ported in Step 6.5
```

## Dependencies

| Dependency | Source |
| -- | -- |
| grunk | FetchContent from GitHub, pinned to a specific commit (see `DESIGN.md`) |
| occt (OpenCASCADE) | conda-forge, pinned to `7.9.3` |
| lua, sol2, taskflow, yaml-cpp, libparametric | conda-forge (needed to build grunk itself from source) |
| python, pyyaml, libclang (PyPI) | conda-forge/PyPI, needed only for `codegen/` (opt-in) |

## License

This project is licensed under the Mozilla Public License 2.0.
