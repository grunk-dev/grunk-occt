<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# Design decisions

This file tracks decisions made during the grocc → grunk-occt migration that aren't obvious from
reading the code, so they don't need re-litigating later. See the migration plan for the full
step-by-step rationale; this file only records the *outcomes*.

## Scope

- **Platforms**: Linux, macOS, and Windows from the start (`pixi.toml`'s
  `["linux-64", "osx-64", "win-64"]`). The one accepted exception is AD-enabled builds
  (a later, deferred milestone), which may stay Linux-only given `adOCCT`/ADOL-C's own
  build complexity.
- **Namespace**: the plugin registers as `PluginInfo{"occt", version}` → Lua namespace `occt`
  (`occt.gp_Pnt`, `occt.BRepBuilderAPI_MakeEdge`, ...).
- **grunk pin**: `CMakeLists.txt`'s `FetchContent_Declare(grunk ...)` pins a specific commit
  (`ac5a90d18cbb78c21c96829e535b4d17c6322fb1`), not a tag — grunk's plugin interface and 0.5.0
  rewrite are only on `main`, untagged as of this writing. Bump the pin deliberately, in its own
  PR, gated by the full grunk-occt test suite - never as a side effect of an unrelated change.

## OCCT dependency

- Plain **conda-forge** OCCT, pinned to **7.9.3** (chosen close to `adOCCT`'s own base, OCCT
  V7_9_0, to minimize future drift if/when an AD-enabled build is attempted).
- **Naming correction found while scaffolding**: conda-forge's own OCCT feedstock is published as
  the package `occt`, *not* `opencascade` - `opencascade` is specifically DLR's `dlr-sc`-channel
  package name (what legacy `grocc`'s Conan recipe and grunk's `cad_autodiff` example both used).
  `pixi.toml` depends on `occt==7.9.3` from `conda-forge` only. `find_package(OpenCASCADE REQUIRED)`
  in `CMakeLists.txt` is unaffected - that name comes from OCCT's own upstream CMake config,
  independent of what the conda package is called. Verified `occt==7.9.3` is published on
  conda-forge for all three target platforms before committing this pin.
- The conda-forge `occt` feedstock ships two build variants per version: `all` (with VTK-based
  visualization modules, e.g. `TKIVtk`) and `novtk`. `pixi.toml` pins `build = "novtk_*"` -
  grunk-occt doesn't need OCCT's visualization/VTK bridge, and the `all` variant's own CMake config
  (`OpenCASCADEVisualizationTargets.cmake`) fails at `find_package(OpenCASCADE)` time unless VTK's
  CMake package is also present, which would be an unnecessary extra dependency here. Verified the
  `novtk` build of `7.9.3` is published on all three target platforms.

## Generated-code policy

Bindings produced by the libclang-based generator (starting once the generator itself exists) are
**checked into `generated/`** as the source of truth for ordinary builds - clang/libclang
availability can't be assumed on every build host/platform, mirroring legacy grocc's
`GROCC_GENERATE_CODE` convention. `CMakeLists.txt`'s `GRUNK_OCCT_GENERATE_CODE` option (default
OFF) gates an opt-in regeneration path; a normal build never needs clang/libclang installed.

## Test-harness shape

The test harness (`tests/test_load.cpp`, growing over subsequent steps) loads the plugin via
`grunk::plugin::load_native` and drives it through **raw Lua via `grunk::state::create_env()`**
directly - not through `grunk::recipe`'s YAML+Lua layer. This keeps plugin-binding bugs isolated
from the separately-young recipe layer. A `grunk::recipe`-based end-to-end test is added later
(once several OCCT modules exist), to validate realistic recipe usage - see Step 6.5 of the
migration plan (porting legacy plugin-using example recipes) for that milestone.

**Gotcha found while implementing Step 0's harness**: `grunk::state::create_env()`'s resulting
environment's `__index` fallback is `original_env` (grunk's own registered-symbol table), *not*
Lua's base library - so plain Lua globals like `assert`, `print`, `tostring`, `pairs`, ... are
**not** reachable inside an `env.eval("...")` string, only whatever's actually registered (e.g.
`occt`, `grunk`, or a module). This differs from `create_parametric_env()`, which grunk's own
`tests/cpp/plugin_loader.cpp` uses successfully with `assert(...)` inside `eval(...)` - it isn't
Standard_Lua-vs-grunk difference so much as which fallback table each environment chains to.
Consequence for every later step's smoke tests: check registrations via the `environment`
wrapper's own `operator[]`/`get<T>()` (C++-side) rather than via a Lua-side `assert(...)` call
inside `create_env()`'s environment, or switch to `create_parametric_env()` if a test genuinely
needs Lua-side `assert`/`print` and doesn't mind the added dependency-tracking behavior.

## `automagic_flags` policy

Default to `sol::automagic_flags::none` for every OCCT type (OCCT types generally lack
`operator==`/`operator<`, which sol2's default "automagic" registration wants). A specific type is
only opted into fuller automagic after explicit human confirmation it's safe (e.g. simple value
types with no dangerous operator surface, following the `gp_Pnt` precedent in grunk's
`cad_autodiff` example). This is a generator-wide rule, not a per-type ad hoc choice.

## `Standard_Real` / AD-portability rule

Every constructor/function parameter of type `Standard_Real` is emitted (by hand today, by the
generator from Step 2 onward) as an `#ifdef`-gated pair: a plain-fundamental-type overload for
ordinary builds, and a `sol::object`-based mixed-conversion overload for a future AD-enabled build
(mirroring `geoml_registration.hpp`'s `gp_Pnt` constructor in grunk's `cad_autodiff` example). This
makes the deferred AD milestone a recompile against an AD-enabled OCCT build, not a source rewrite.
