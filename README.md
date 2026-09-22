<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# grunk-occt

A [grunk](https://github.com/grunk-dev/grunk) plugin that exposes [OpenCASCADE](https://dev.opencascade.org/)
(OCCT) as building blocks for grunk models: construct points, curves, surfaces, and solids, combine
them with boolean operations, and get a fully parametric, reactive CAD model - change one input and
everything downstream recomputes automatically.

**Status**: early / WIP. The OCCT surface currently wrapped covers `gp` (points, directions,
vectors, axes, transformations), `geom`/`geomapi`/`gc` (curves and surfaces, including B-splines and
Bezier curves/surfaces), and the shape-building/boolean-op modules (`topods`, `brepbuilderapi`,
`brepprimapi`, `brepalgoapi`, `brepoffsetapi`, `brepfilletapi`, `bnd`, `brep`, `breptools`,
`toploc`). It grows as real models need more of OCCT - see `examples/` for what's already
demonstrated end to end, and open an issue if something you need isn't covered yet.

## A quick look

```yaml
uses:
  grunk: 0.5.0
  occt: 0.1.0
parameters:
  radius: 1.0
  height: 5.0
steps: |
  origin = occt.gp_Pnt.new(0.0, 0.0, 0.0)
  axis = occt.gp_Dir.new(0.0, 0.0, 1.0)
  ax2 = occt.gp_Ax2.new(origin, axis, occt.gp_Dir.new(1.0, 0.0, 0.0))
  cylinder = occt.BRepPrimAPI_MakeCylinder.new(ax2, radius, height):as(occt.BRepBuilderAPI_MakeShape):Shape()
  ok = occt.export_brep(cylinder, "cylinder.brep")
outputs:
  cylinder: cylinder
  ok: ok
```

Change `radius`/`height` (interactively, from Python, or by overriding a parameter when the recipe
is called from elsewhere) and the cylinder recomputes - that's the whole point of building on grunk
rather than scripting OCCT directly. `examples/cavity_model.grr.yml` and `examples/tank_model.grr.yml`
are complete, runnable models (a gas turbine cavity and a hydrogen tank) built the same way, with
named, reusable sub-recipes.

## Installing and building

Requires [pixi](https://pixi.sh/):

```
pixi run -e default build     # builds grunk (pinned commit, see below) then occt_plugin
pixi run -e default test      # runs the test suite
pixi run -e default run_examples   # runs the example recipes under examples/
pixi run -e default install   # installs occt_plugin (see "Using it in your own project" below)
```

`build` (and every other task below) clones, builds, and installs a pinned grunk commit into
`.deps/grunk` first if it isn't there already (`scripts/install_grunk.cmake`, idempotent - delete
`.deps/grunk` to force a rebuild) - `occt_plugin` links against it dynamically (`find_package
(grunk)`, not a vendored copy), so this only ever needs to happen once.

`occt_plugin` is a runtime-loaded grunk plugin, loaded by path via
`grunk::plugin::load_native` (or the equivalent Python call) - it's never linked against directly.
A minimal Python example, once built:

```python
import grunk

def main():
    state = grunk.state()
    grunk.plugin.load_native(state, "build/libocct_plugin.so")
    recipe = state.read("my_model.grr.yml")
    recipe.get_feature("radius").set_value(2.0)
    print(recipe.get_output("ok").value().as_bool())

main()  # keep state/recipe scoped to a function - state must outlive
        # everything read from it, and a function's own local scope gives a
        # well-defined teardown order that a top-level script does not
```

## Using it in your own project

```
pixi run -e default install   # -> build/install/lib/libocct_plugin.so (.dylib on macOS, .dll on Windows)
```

Point `grunk::plugin::load_native` (or its Python equivalent) at the installed path. There's no
CMake package to `find_package()` - just the one shared library. `CMAKE_INSTALL_PREFIX` defaults to
`build/install`; override it via `pixi.toml`'s `configure` task or `cmake --install build --prefix <path>`.

`libocct_plugin.so` itself dynamically links grunk's own shared libraries (`.deps/grunk`, an
absolute path baked in at build time - see `DESIGN.md`'s "grunk: pre-built dependency, not
vendored") - loading it from a machine/directory that doesn't have that same `.deps/grunk` install
present needs `grunk`'s own shared libraries reachable another way (e.g. `LD_LIBRARY_PATH`, or a
`pixi run install_grunk` of your own pointed at the same path) until this plugin ships as a real,
independently relocatable binary package.

## Algorithmic differentiation (AD)

`occt_plugin` can also be built against [adOCCT](https://github.com/dlr-sp/adOCCT), a fork of OCCT
that carries an [ADOL-C](https://github.com/coin-or/ADOL-C) derivative alongside every
`Standard_Real`, so a recipe's outputs can be differentiated with respect to its parameters with no
extra modeling work. It's a build option (`GRUNK_OCCT_ENABLE_AD`), not a separate plugin - build it
into its own directory so both variants can coexist:

```
pixi run -e ad install_grunk    # build/install grunk into .deps/grunk-ad (idempotent)
pixi run -e ad install_adolc    # build/install ADOL-C (idempotent)
pixi run -e ad install_adocct   # build/install adOCCT against it (idempotent)
pixi run -e ad build
pixi run -e ad test             # includes a closed-form derivative check
pixi run -e ad install          # -> build-ad/install/lib/{libocct_plugin.so,libadtl_plugin.so}
```

(`.deps/grunk-ad`, not `.deps/grunk` - the `ad` environment has its own conda environment, and so
needs its own separate grunk build - see `DESIGN.md`'s "grunk: pre-built dependency, not vendored".
Like the other `install_*` tasks, this one also runs automatically as part of `build`/`test`/etc.
via `configure`'s own dependency chain - spelled out here for visibility.)

The `ad` environment is Linux-only for now and entirely separate from `default` - it never installs
the stock `occt` package, and vice versa. See `DESIGN.md` for the underlying design (why AD is a
build option rather than a separate project, the `Standard_Real` dual-emission convention that makes
one source tree serve both variants) and known limitations (a `dlopen`/SONAME hazard if a consuming
process loads both variants at once).

## Project structure

```
codegen/    # libclang-based code generator that produces generated/ from codegen/config.yml
generated/  # generator output - checked into git, builds without clang/libclang needed
src/        # plugin entry point (occt_plugin.cpp) + a handful of hand-written registrations
            # for the few things the generator can't produce on its own
examples/   # runnable example recipes - see examples/README.md
tests/      # one gtest file per OCCT module wrapped
```

## Dependencies

| Dependency | Source |
| -- | -- |
| grunk | pinned commit, built and installed automatically into `.deps/grunk` (`scripts/install_grunk.cmake`) |
| occt (OpenCASCADE) | conda-forge, `7.9.3` |
| lua, sol2, taskflow, yaml-cpp, libparametric | conda-forge (needed to build grunk itself) |
| python, pyyaml, libclang | conda-forge/PyPI, only needed to regenerate `generated/` |
| gtest | conda-forge, the test framework |
| ADOL-C, adOCCT | built from source, only needed for the AD build (see above) |

## License

This project is licensed under the Mozilla Public License 2.0 - see the
[license file](LICENSES/MPL-2.0.txt) for details.
