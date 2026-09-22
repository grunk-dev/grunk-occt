<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# Design decisions

This file records design decisions, bugs found and fixed, and known limitations that aren't
obvious from reading the code alone, so they don't need re-discovering later.

## Scope

- **Platforms**: Linux, macOS, and Windows from the start (`pixi.toml`'s
  `["linux-64", "osx-64", "win-64"]`). The one accepted exception is AD-enabled builds,
  which stay Linux-only given `adOCCT`/ADOL-C's own build complexity - but they are not
  CI-exempt: `.github/workflows/ci.yml`'s `build-and-test-ad` job installs ADOL-C/adOCCT,
  then configures/builds/tests/runs the examples for the `ad` environment, on every PR
  (Linux-only, with `.deps/adolc`/`.deps/adocct` cached across runs so it isn't rebuilding
  both from source every time).
- **Namespace**: the plugin registers as `PluginInfo{"occt", version}` → Lua namespace `occt`
  (`occt.gp_Pnt`, `occt.BRepBuilderAPI_MakeEdge`, ...).
- **grunk pin**: grunk is a pre-built, pre-installed dependency now, found via `find_package(grunk
  REQUIRED)` in `CMakeLists.txt` - not vendored via `FetchContent` anymore (see "grunk: pre-built
  dependency, not vendored" below for the full story of why). `scripts/install_grunk.cmake`'s own
  `GRUNK_REV` pins a specific commit (currently `a372159c47d3722e138634ad9997c8037c6fd422`), not a
  tag — grunk's plugin interface and 0.5.0 rewrite are only on `main`, untagged as of this writing.
  Bump the pin deliberately, in its own PR, gated by the full grunk-occt test suite - never as a
  side effect of an unrelated change. A recent bump (from `1ad286bfdc937ff75735c20db4358eb282f48c02`)
  picked up [grunk-dev/grunk#284](https://github.com/grunk-dev/grunk/pull/284) (`GRUNK_PLUGIN_INFO`, an
  MSVC C4190 fix for `grunk_plugin_info`'s `extern "C"` return of a non-POD `PluginInfo`) -
  `src/occt_plugin.cpp` now uses the macro instead of the hand-written form.

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

## Windows CI compiler

Windows GitHub Actions runners have a MinGW toolchain on `PATH` by default, which CMake picks up
ahead of MSVC's `cl.exe` (not on `PATH` without an explicit developer-shell setup) - this produced
a build using MinGW/GCC instead of MSVC, surfacing as e.g. a `NOMINMAX` redefinition warning
between grunk's own headers and MinGW's `os_defines.h`. Fixed by adding an `egor-tensin/vs-shell@v2`
step (`arch: x64`) before `setup-pixi` in `.github/workflows/ci.yml`, matching grunk's own CI
(`.github/workflows/build-test.yml`) - this puts `cl.exe`/`link.exe` on `PATH` (and sets
`INCLUDE`/`LIB`) so CMake's `-G Ninja` (used on all three platforms, matching grunk's own
`pyproject.toml` configure task) picks up MSVC instead.

## Test-harness shape

The test harness (`tests/test_*.cpp`, one file per module) loads the plugin via
`grunk::plugin::load_native` and drives it through **raw Lua via
`grunk::state::create_env()`** directly - not through `grunk::recipe`'s YAML+Lua layer. This keeps
plugin-binding bugs isolated from the recipe layer. `tests/test_recipe.cpp` complements this with a
`grunk::recipe`-based end-to-end test: it reads `cavity_model.grr.yml` (the same example recipe
`examples/run_example.py` already runs end-to-end via Python) through the plain C++
`grunk::recipe` API, so the recipe layer has a fast, always-run unit test of its own, not only an
external Python example script.

**Test framework: gtest** (matches both legacy grocc's own test suite and grunk's own
`tests/cpp/`). One `runUnitTests` binary (`CMakeLists.txt`, via `find_package(GTest REQUIRED)` +
`GTest::gtest_main`), built from all `tests/test_*.cpp` files; each contributes its own
`TEST_F`-based suite (a `class <Module>Test : public ::testing::Test` fixture loading the plugin
once per suite via `SetUpTestSuite`/`TearDownTestSuite`, not once per individual test case - the
same `SetUpTestCase`-once-per-suite pattern legacy grocc's `test_gp_Pnt.cpp`/
`test_simple_boolean_op.cpp` used, just under gtest's current `SetUpTestSuite` naming).
`gtest_discover_tests(runUnitTests)` registers every individual `TEST_F` case as its own `ctest`
entry - `ctest --test-dir build` reports each one (and can filter via `-R <regex>`), not one
pass/fail per module/file.

**Gotcha**: `grunk::state::create_env()`'s resulting
environment's `__index` fallback is `original_env` (grunk's own registered-symbol table), *not*
Lua's base library - so plain Lua globals like `assert`, `print`, `tostring`, `pairs`, ... are
**not** reachable inside an `env.eval("...")` string, only whatever's actually registered (e.g.
`occt`, `grunk`, or a module). This differs from `create_parametric_env()`, which grunk's own
`tests/cpp/plugin_loader.cpp` uses successfully with `assert(...)` inside `eval(...)` - it isn't
Standard_Lua-vs-grunk difference so much as which fallback table each environment chains to.
Consequence for smoke tests: check registrations via the `environment`
wrapper's own `operator[]`/`get<T>()` (C++-side) rather than via a Lua-side `assert(...)` call
inside `create_env()`'s environment, or switch to `create_parametric_env()` if a test genuinely
needs Lua-side `assert`/`print` and doesn't mind the added dependency-tracking behavior.

## Copying an algorithm/builder class segfaults - fixed via std::unique_ptr<T>

Registering `BRepAlgoAPI_Cut`'s
constructor the same way as every other class so far
(`[](Args...) { return BRepAlgoAPI_Cut(Args...); }`) built and ran, but **segfaulted
at program exit** with heap corruption inside `BRepAlgoAPI_BuilderAlgo::~BRepAlgoAPI_BuilderAlgo()`
(confirmed via gdb backtrace). Root cause: `BRepAlgoAPI_BuilderAlgo` (one of
`BRepAlgoAPI_Cut`'s bases) owns a raw pointer member (`BOPAlgo_PPaveFiller myDSFiller`)
freed in its destructor, with **no user-declared copy constructor** - the
compiler-generated one shallow-copies the pointer. Returning the object *by value*
from the constructor lambda means sol2 moves/copies it into the usertype's own
storage on top of the (already copy-elided) function return - with no real move
constructor available either (declaring a destructor suppresses the implicit move,
falling back to copy), this ends up shallow-copying `myDSFiller` at least once. Both
copies eventually get destroyed (once as the lambda's own temporary, once as the
Lua-owned instance at state teardown) and free the same pointer twice.

This is **not** the `Standard_Transient`/`Handle(T)` case solved elsewhere -
`BRepAlgoAPI_Cut` isn't Transient-derived, and nothing else in OCCT expects it
wrapped in a `Handle`. Fixed generically in the generator
(`codegen/generate.py`'s `owns_resource_unsafely()`): a class is heap-allocated via
`std::unique_ptr<T>` (`return std::make_unique<T>(args...);`, which sol2 accepts
natively as constructor-return storage and never copies/moves `T` itself) instead of
returned by value, whenever it - or any class in its ancestor chain - declares an
**explicit destructor**. A class with no explicit destructor anywhere in its chain
(`gp_Pnt`, `TopoDS_Shape`, ...) is assumed safely copyable and keeps the plain
by-value path. This is a conservative heuristic (a declared destructor doesn't
*prove* unsafe copying, just correlates with it strongly enough in OCCT's own class
design to be worth defaulting to safe) - see `codegen/README.md` for the full
rationale and its own "Known gaps" note on where this could still be wrong.

## Non-const-reference ("out") parameters are supported, deliberately

Initially the generator rejected every non-const-reference
parameter outright, treating out-params as generically unsupportable. That was wrong,
corrected after review: OCCT's OO APIs use mutate-through-reference constantly
(`Geom_Curve::D0/D1/D2/D3`, `gp_Pnt::Coord`'s 3-out-param overload, ...) and it isn't
optional to support them - see `codegen/README.md`'s "Non-const-reference ('out')
parameters" for the corrected rule (accepted when the mutated type is a registered
object/`Handle`, rejected only for a bare primitive reference, which Lua genuinely
has no representation for).

Binding these directly (`register_type`/`add_member_function`) works with zero
special-casing - sol2 already supports mutating an existing userdata in place, this
project's own `tests/test_geom.cpp` confirms it (`Geom_Curve::D0` mutating a `gp_Pnt&`
passed in from Lua). The distinct, separate concern is **using** such a call from
inside a tracked `grunk::recipe` (as opposed to the raw `create_env()` harness this
project's tests use so far): grunk's dependency-tracking decorator
(`create_parametric_env()`) assumes a call's return value is its only output, so a
mutation-based out-param wouldn't be tracked correctly if called directly inside a
decorated/parametric environment. grunk's own **"module"** mechanism
(`Recipe::insert_module_script`/`run_module_script` - see `grunk/dynamic/state.hpp`'s
docs) is the documented way to bridge this: a module function's own *body* runs in a
raw/undecorated context (so it can freely construct a placeholder, mutate it through
as many out-param calls as needed, and build up the result imperatively), while the
module function *as a whole*, called from outside, is treated as a single pure/
parametric unit - only its final return value is tracked. This project hasn't needed
that bridge yet in its own recipes (none of the ported example recipes call an
out-param method directly from `steps:`), but it's the mechanism to reach for once
out-param-heavy OCCT calls need to appear inside an actual recipe, not just this
project's own `create_env()`-based tests.

**Confirmed, not just theoretical**: the sibling `grunk-adolc` repo hit exactly this
hazard directly, for a different mutating method (`adtl.adouble::setADValue`, not an
OCCT out-param, but the identical shape of problem) - a qualified call to a mutating
method placed directly in `steps:` compiles and runs with no error, but its effect is
silently lost, because a `steps:` statement becomes a lazily-evaluated
`grunk::ActionDynamic` node whose `eval()` only ever writes back its own *return*
value; a call whose result nothing downstream reads may simply never be triggered by
grunk's lazy pull-based evaluation at all. See `grunk-adolc/DESIGN.md`'s "Mutating
calls inside `steps:` may silently never run under lazy evaluation" section and
`grunk-adolc/tests/test_recipe.cpp` for the confirmed repro - this project's own
`tests/test_recipe.cpp` (added alongside that discovery) exercises `cavity_model.grr.
yml` end-to-end through the recipe layer for the first time, but does not itself
exercise an out-param call from `steps:` - that specific case remains unexercised
here, now with a confirmed-elsewhere reason to be careful about it whenever it comes
up.

## Null Handle arguments segfault (filed as grunk-dev/grunk#281)

Passing a Lua value that is `nil` as an argument to a function/method parameter typed
`Handle(T) const&` (bridged via `occt_sol_traits.hpp`'s
`sol::unique_usertype_traits<opencascade::handle<T>>`) **segfaults**, rather than
either erroring cleanly or default-constructing a null handle. Confirmed via gdb: the
crash is inside the registered lambda itself, meaning sol2 handed it something
invalid rather than refusing the call or passing a genuine null handle. This is the
*opposite* direction of the (correctly-working) `is_null` path: a null `Handle(T)`
**returned** from a function surfaces safely as Lua `nil` (confirmed working,
`tests/test_geom.cpp`'s `null_geom_geometry` check) - it's specifically passing `nil`
back **in** as an argument that crashes.

**Consequence for this codebase**: never write (hand-written or generated) a test, a
recipe, or registration code that passes a Lua `nil` into a `Handle(T)`-typed
parameter. `tests/test_geom.cpp`'s negative-path check was redesigned around this -
it only ever inspects a *returned* Handle for nilness, never passes one in.

This is a real robustness gap (a Lua script should never be able to crash the host
process) - filed as [grunk-dev/grunk#281](https://github.com/grunk-dev/grunk/issues/281).

## `add_member_function` and overloaded OCCT methods

Unlike `add_constructors` (which wraps all its arguments in `sol::overload(...)` automatically),
`add_member_function(key, fun)` registers exactly one `fun` under `key` - calling it twice with the
same key overwrites, it doesn't merge overloads. Several `gp_*` methods are themselves overloaded in
OCCT (e.g. `gp_Trsf::SetTranslation`, `SetRotation`, `SetMirror` each have 2+ C++ overloads) - taking
`&Type::Method` bare in those cases is ambiguous and fails to compile. Fix: an explicit
`static_cast<RetType (Type::*)(ArgTypes...)>(&Type::Method)` picks the exact overload needed (see
`src/gp/gp_registration.hpp`). When a name genuinely needs more than one overload exposed to Lua
under the same key, pass `sol::overload(overload1, overload2)` as `fun` instead (per
`usertype_proxy::add_member_function`'s own doc comment) - not needed yet in the `gp` module, but
expect it in modules with more heavily overloaded methods.

## `automagic_flags` policy

Default to `sol::automagic_flags::none` for every OCCT type (OCCT types generally lack
`operator==`/`operator<`, which sol2's default "automagic" registration wants). A specific type is
only opted into fuller automagic after explicit human confirmation it's safe (e.g. simple value
types with no dangerous operator surface, following the `gp_Pnt` precedent in grunk's
`cad_autodiff` example). This is a generator-wide rule, not a per-type ad hoc choice.

## Code generator

`codegen/generate.py` (libclang-based, see `codegen/README.md` for full design/scope)
targets the `gp` module first and reproduces - and exceeds - the earlier hand-written
registrations it replaced: 166 constructors/methods accepted automatically (vs. ~15
hand-picked), 48 rejected with a recorded reason each (unregistered types like
`gp_XYZ`/`gp_Ax2`, non-const-reference out-params, `Standard_OStream&`, ...), 1
deliberately blacklisted (`gp_Pnt::BaryCenter`, a checkpoint proving the blacklist
mechanism works end to end). The hand-written `src/gp/gp_registration.hpp` was deleted
once `generated/gp.cpp` reproduced its own tests unchanged - generated and hand-written
versions of the same module must never coexist/drift.

The AD-portability macro name is **`GRUNK_OCCT_WITH_ADOLC`** (not defined anywhere yet
- only the plain branch of every `#ifdef`-gated dual form is compiled today). The
generator bakes this in automatically for every `Standard_Real` parameter; hand-written
escape-hatch code should use the same macro name for consistency.

## `Standard_Real` / AD-portability rule

Every constructor/function parameter of type `Standard_Real` is emitted by the generator as an
`#ifdef`-gated pair: a plain-fundamental-type overload for ordinary builds, and a
`sol::object`-based mixed-conversion overload for a future AD-enabled build
(mirroring `geoml_registration.hpp`'s `gp_Pnt` constructor in grunk's `cad_autodiff` example). This
makes the deferred AD milestone a recompile against an AD-enabled OCCT build, not a source rewrite.

## Collections: `std::vector<T>` <-> `NCollection_Array1/2`

Validated with `Geom_BezierCurve` (`codegen/config.yml`'s `geom` module gained
`Geom_BoundedCurve.hxx`/`Geom_BezierCurve.hxx`).

`src/collections.hpp` provides the actual conversion, independent of the generator:
`to_array1<T>(std::vector<T> const&) -> NCollection_Array1<T>` /
`from_array1<T>(NCollection_Array1<T> const&) -> std::vector<T>`, 1-based (`[1,
v.size()]`, matching OCCT's own array convention throughout its docs), and the
`NCollection_Array2` equivalents `to_array2`/`from_array2`.

- **`NCollection_Array2`'s Lua-side shape, decided explicitly** (not folded into the
  1D decision): a single **flat `std::vector<T>` plus separate `rows`/`cols`
  counts** (row-major: row `r`, column `c` is at index `r*cols + c`) - **not** a
  nested `std::vector<std::vector<T>>`. Deliberately reconsidered after an initial
  nested-vector draft: a nested vector's own type doesn't prevent a jagged
  (non-rectangular) input (each inner vector can have a different length, silently
  wrong rather than a type error), and it costs one heap allocation per row instead
  of one contiguous buffer for the whole array. A flat vector is rectangular *by
  construction* (`to_array2` checks `flat.size() == rows*cols` and throws
  otherwise) and contiguous. `tests/test_collections.cpp` covers the round trip and
  both error cases directly (no real OCCT binding needs 2D yet, so this has no
  other coverage).
- **Generator-level support** (`codegen/generate.py`'s `Param.kind()`/`emit_lambda` -
  see codegen/README.md's "NCollection_Array1/2 support" for the full mechanism):
  a by-value or const-reference parameter whose *canonical* (typedef-resolved) type
  is `NCollection_Array1<X>`/`NCollection_Array2<X>` - which is what
  `TColgp_Array1OfPnt`/`TColStd_Array1OfReal`/... actually are, invisible as such
  without canonical resolution - is accepted automatically whenever `X` is itself a
  supported element type (a registered class, or `double`/`Standard_Integer`/
  `Standard_Boolean`), and the generated lambda converts through
  `collections.hpp` before calling the real OCCT function. This is what makes
  `Geom_BezierCurve`'s *own* two constructors (`(TColgp_Array1OfPnt const&)` and
  `(TColgp_Array1OfPnt const&, TColStd_Array1OfReal const&)`) both auto-generate
  as ordinary `add_constructors(...)` overloads - no hand-written escape hatch
  needed for construction. `NCollection_Array2` support in the generator is
  implemented symmetrically but, like the `collections.hpp` side, has no real OCCT
  binding to validate it yet.
- **Deliberately out of scope for this pass** (rejected exactly as before, recorded
  with a reason - see codegen/README.md's "Known gaps"): a **mutable-reference**
  array out-param (e.g. `Geom_BezierCurve::Poles(TColgp_Array1OfPnt& P)`) and an
  array **return type** (e.g. `Poles() const -> const TColgp_Array1OfPnt&`,
  `Weights()`). Both need a decided answer to a question the parameter side
  doesn't: what happens on a size mismatch (out-param) or which Lua-facing shape a
  freshly-`from_array1`/`from_array2`-converted return value takes - deferred until
  a concrete consumer forces the decision, per the general "don't design for
  hypothetical requirements" principle this project follows throughout.

**Gotcha found while implementing this**: a bare Lua table of `occt.gp_Pnt` values is
**not** accepted directly as a `std::vector<gp_Pnt> const&` function parameter - sol2
has no generic table-to-`std::vector<Usertype>` conversion for an ordinary function
parameter (confirmed empirically: it fails with "expected userdata, received table",
i.e. sol2 expects an already-constructed `std::vector<gp_Pnt>` *object*, not
something to build from a table on the fly - unrelated to the array-conversion work
above, since this is sol2's own container-argument binding, not anything
`collections.hpp`/the generator's array support does). The vector has to be built
explicitly first, via grunk's own `with_std_vector()`-provided `as_vec(...)`/
`new_vec()` - opted into `gp_Pnt` by hand (`src/gp/gp_extras.hpp`, using
`modify_type<gp_Pnt>` since `gp_Pnt` is already registered generically by the
generator) since the generator doesn't emit this for any type yet.
`tests/test_geom.cpp`'s `BezierCurveFromVectorOfPoints` calls
`occt.gp_Pnt.as_vec(p1, p2, p3)` before passing the result to
`occt.Geom_BezierCurve.new(poles)`.

## Known issue: `Handle(T)` upcast argument-checking fails in Debug (`SOL_SAFE_FUNCTION_CALLS`) builds

Found independently of any particular change (reproduces identically on unmodified
`main`, confirmed via `git stash`): `tests/test_geom.cpp`'s
`NullHandleSurfacesAsNil` (`downcast_to_geom_line(Handle(Geom_Geometry) const&)`
called with a genuine `Handle(Geom_Line)` value, upcast via `add_bases`) **fails**
when grunk-occt is built with `CMAKE_BUILD_TYPE=Debug`, with sol2's own "unrecognized
userdata (not pushed by sol?)" error - the exact same failure mode as a
never-fully-explained synthetic `Handle<T>` repro found earlier during
investigation, except this reproduces with a **real OCCT type**, gated purely by
Debug vs. Release.

Root cause, now much better understood after the grunk `with_std_vector` fix (see
[grunk-dev/grunk#282](https://github.com/grunk-dev/grunk/pull/282)): sol2's
`SOL_SAFE_FUNCTION_CALLS` (on by default in Debug builds, off by default in
Release/`NDEBUG` builds) gates whether a function argument gets a full type-safety
check before binding. The two paths apparently disagree about whether a
`unique_usertype_traits`-wrapped `Handle(Derived)` value satisfies a `Handle(Base)
const&` parameter: the *unchecked* path (Release, what every test/CI job so far has
exercised - `pixi.toml`'s `configure` task defaults to Release, and CI
(`.github/workflows/ci.yml`) always passes `Release` explicitly) accepts it, the
*checked* path (Debug) incorrectly rejects it.

**Consequence for this codebase**: not fixed here - deep sol2-internal territory,
same category as grunk-dev/grunk#281 (the `BezierCurveFromVectorOfPoints` test
passes in both Debug and Release - this is exclusively about the pre-existing
`downcast_to_geom_line` case). grunk-occt's only currently-tested/supported build
configuration is Release (matches
CI); a Debug build should be understood as not fully validated for `Handle(T)`
upcasting until this is root-caused upstream in sol2/grunk. Filed as
[grunk-dev/grunk#283](https://github.com/grunk-dev/grunk/issues/283).

## Module coverage sweep

Each module gets its own smoke test plus the full accumulated regression suite - no
blacklist entries carried over from legacy grocc's `config.yml.in` without
re-deriving the reason fresh; so far every module has needed none of its own.

- **`bnd`** (`Bnd_Box`, `BRepBndLib`): introduced the `extra_includes` config field
  (revived from legacy grocc's own schema) - a module's own headers can
  forward-declare a type they only use by reference (`BRepBndLib.hxx`'s
  `class TopoDS_Shape;`), but sol2's usertype binding needs that type's *complete*
  definition in the generated `.cpp` regardless of it being fully registered
  elsewhere - see codegen/README.md's own "extra_includes" section.
- **`brep`** (`BRep_Builder`, `BRep_Tool`): both classes have a much larger real API
  than what's accepted (most overloads need `Geom_Surface`/`Geom2d_Curve`/`Poly_*`/
  `gp_Pnt2d`/`TopLoc_Location`/`GeomAbs_Shape`/`TopAbs_ShapeEnum`, none registered
  yet) - 37 accepted, 67 rejected, all recorded. `BRep_Builder` has no explicit
  constructor of its own (a plain compiler-synthesized default one, invisible to
  the generator's AST walk - the same "missing implicit constructors" category as
  any other compiler-synthesized-only constructor) - added via
  a hand-written escape hatch (`src/brep/brep_extras.hpp`, `modify_type<BRep_Builder>
  ().add_constructors(...)`, following `gp_extras.hpp`'s established pattern).
  `tests/test_brep.cpp` also found that `BRep_Tool::IsClosed` on a `Solid` isn't the
  "is this box geometrically closed" check its name suggests - for anything that
  isn't a `Shell`/`Wire`, it falls back to `TopoDS_Shape::Closed()`, a plain stored
  flag `BRepPrimAPI_MakeBox` never sets (confirmed empirically: false for a
  freshly-built box) - documented in the test itself, not just here.
- **`breptools`** (`BRepTools`, a static-only "namespace class"): 22 accepted, 20
  rejected (mutable `Standard_Real&`/`Standard_Boolean&` out-params, `Standard_
  OStream&`/`Standard_CString` for `Write`/`Read`/`Dump` - never colliding with
  `src/shape_io.hpp`'s own hand-written `export_brep`, which exists for exactly
  that gap - plus assorted unregistered types). What's left is still real:
  `Update`/`Clean`/`CleanGeometry`/`RemoveUnusedPCurves`/`Triangulation`/`Compare`/
  `OuterWire`/`IsReallyClosed`/`RemoveInternals`. No new generator work or
  hand-written escape hatch needed - the third module in a row proving out
  `extra_includes` (this time for the seven `TopoDS_*` types `BRepTools.hxx` only
  forward-declares) rather than needing something new.
- **`toploc`** (`TopLoc_Location` only, not the whole legacy `TopLoc_*.hxx` glob -
  the rest are `TopLoc_Location`'s own internal list/hashing machinery, never
  meant to be exposed to Lua on their own): 16 accepted, 5 rejected (the
  `Handle(TopLoc_Datum3D)` constructor, operators - skipped by name prefix, not a
  real rejection). A plain value type, buildable from a `gp_Trsf`.
  - **More consequential than bnd/brep/breptools**: `TopLoc_Location` was
    previously *unregistered*, so every earlier module's `TopLoc_Location&`/
    `const&` parameter or return was rejected for that reason alone. A full
    `generate_code` regen (this is how the generator always works - `main()`
    processes every configured module fresh every run, using one
    `registered_types` set computed up front, see codegen/README.md's "Type
    registration spans the whole config") retroactively **accepted 7 more
    callables with no config/generator change of their own**: `topods` gained
    `TopoDS_Shape::Location`/`Located`/`Move`/`Moved` (54 -> 59 accepted);
    `brep` gained the `TopLoc_Location`-taking overloads of `BRep_Builder::
    MakeEdge`/`UpdateEdge` (37 -> 39 accepted). `breptools`'s own `TopLoc_Location`
    out-params (`UVBounds`'s siblings) stayed rejected regardless - each also has
    a still-unsupported mutable `Standard_Real&`/`Bnd_Box2d&` parameter blocking
    it too.
  - `TopoDS_Shape.hxx` already fully `#include`s `TopLoc_Location.hxx` (not just a
    forward declaration), so `topods`'s own `extra_includes` needed no change;
    `BRep_Builder.hxx` only forward-declares it, but `brep`'s existing
    `extra_includes` entry for `TopoDS_Shape.hxx` pulls in the complete
    `TopLoc_Location` definition transitively too - neither module needed a new
    `extra_includes` entry, confirmed by just building rather than assumed from
    reading headers alone.
  - `tests/test_toploc.cpp`'s `TopoDsShapeLocationRoundTrip` exercises one of the
    retroactively-unlocked `topods` callables directly (`box:Moved(loc, true)`),
    not just `TopLoc_Location` in isolation.
- **`gc`** (`GC_Root`, `GC_MakeSegment`, `GC_MakeCircle`): two of legacy grocc's
  "GC" construction-algorithm classes, both extending `GC_Root` (registered here
  too, so `add_bases<GC_Root>()` dispatch can reach `IsDone()`). 8 accepted, 8
  rejected - `GC_MakeSegment`'s other three constructors need `gp_Lin`
  (unregistered); 3 of `GC_MakeCircle`'s 7 constructors need `gp_Circ`/`gp_Ax2`
  (also unregistered). `Value()` returns `Handle(Geom_TrimmedCurve)`/
  `Handle(Geom_Circle)` - this surfaced a real, general compile-time limitation,
  described next.
- **`brepoffsetapi`** (`BRepOffsetAPI_*`, whole-module wildcard): legacy grocc's own
  `config.yml.in` already wildcarded this entire module
  (`BRepOffsetAPI_*.hxx`) with one blacklist entry
  (`BRepOffsetAPI_FindContigousEdges` - a real OCCT bug, undefined symbol `NbEdges`
  at Windows link time, not a toolchain/generator artifact - re-derived fresh
  rather than carried forward from grocc uncritically, but kept since the
  underlying OCCT issue has no reason to be gone). 103 accepted, 50 rejected
  (`GeomAbs_Shape`/`GeomAbs_JoinType`/`Approx_ParametrizationType`/other unregistered
  enums, `TopTools_ListOfShape&`/`const&` out-params and returns, `Standard_OStream&`,
  `Handle(Law_Function)`/`Handle(OSD_FileSystem)`, mutable `Standard_Real&`/
  `Standard_Boolean&` out-params - the same categories of gap as brep/breptools).
  `BRepOffsetAPI_Sewing.hxx` is a bare `typedef BRepBuilderAPI_Sewing
  BRepOffsetAPI_Sewing;` (no class of its own) - the same glob-noise case as
  `gp_Vec2f.hxx`, skipped quietly by `expand_headers`'s existing handling rather
  than a new gap. No `extra_includes` needed: every accepted signature's
  `TopoDS_*`/`gp_*`/`Message_ProgressRange` types are already registered by prior
  modules and each `BRepOffsetAPI_*.hxx` header fully includes what it needs (also
  true of `brepbuilderapi`/`brepprimapi`/`brepalgoapi`, unlike bnd/brep/breptools's
  forward-declare-only headers - confirmed by building, not assumed).
  - **`BRepOffsetAPI_MakeThickSolid::MakeThickSolidBySimple` requires a non-closed
    shell or face, not a closed solid** - stated in the header's own doc comment
    ("Non-closed shell or face is expected as input"), but found the hard way first:
    calling it on a `BRepPrimAPI_MakeBox`/`MakeCylinder` result compiles fine but
    throws a non-`std::exception` OCCT failure at `Shape()`/`Build()` time (surfaces
    in Lua as sol2's generic `"caught (...) exception"`, not a message naming the
    real cause - GRUNK_PLUGIN_REGISTER's own `catch (...)` branch, doing exactly its
    documented job, but with no `.what()` to report). No `TopExp_Explorer`-equivalent
    is registered (its `More()`/`Next()`/`Current()` iterator pattern has no
    generator support, same category of gap as legacy grocc's own iterator-shaped
    classes), so there was no existing way to pull a single face out of a solid
    either - added `src/topexp_extras.hpp`'s `occt.first_face(shape)` (a thin,
    single-use `TopExp_Explorer` wrapper returning the first `TopAbs_FACE` found) as
    a hand-written escape hatch, same category as `shape_io.hpp`'s `export_brep`.
    `tests/test_brepoffsetapi.cpp`'s `ThickSolidFromBox` shells a single face of the
    boolean-op smoke test's own cylinder (not the box - a box's sharp corners were
    tried too before isolating the real cause, a closed vs. non-closed input, not
    corner geometry).
- **`geomapi`** (`GeomAPI_*`, whole-module wildcard): legacy grocc's own
  `config.yml.in` wildcarded this whole module too, with one blacklist entry
  (`GeomAPI_Interpolate::ClearTangents` - legacy's own comment notes it's a Windows
  link error, the definition apparently missing from OCCT's own
  `GeomAPI_Interpolate.cxx`); re-derived fresh and kept, same
  reasoning as `brepoffsetapi`'s carried-forward entry (a real OCCT library gap, not
  a toolchain/generator artifact).
  - **A new generator gap**: `GeomAPI_ExtremaCurveCurve`, `GeomAPI_ExtremaCurveSurface`,
    and `GeomAPI_ProjectPointOnSurf` each hold an `Extrema_Ext{CC,CS,PS}` member whose
    own implicit move/copy constructor is deleted (a private field deep in its own
    dependency chain forces this), but none of the three has an *explicit* destructor
    of its own or anywhere in its base chain - so `owns_resource_unsafely` (which only
    checks for an explicit destructor, see its own doc comment under "Copying an
    algorithm/builder class segfaults") doesn't route them to the `unique_ptr` ctor
    mode, and the plain by-value ctor mode fails to compile
    (`use of deleted function ...(&&)`/`use of deleted function ...(&)`). Detecting
    genuine move/copy-constructibility in general would need full semantic analysis
    beyond what libclang's cursor-level AST walk gives us - out of scope for this
    pass; blacklisted as bare class names (`codegen/config.yml`) rather than taught
    to the generator. 68 accepted before this blacklist addition, 40 after.
  - **A second, more consequential new limitation, found writing this module's own
    test**: `GeomAPI_ProjectPointOnCurve`'s constructor/`Init` take a
    `Handle(Geom_Curve) const&` - but `Geom_Curve` is abstract, so the only way to
    supply one from Lua is to upcast a registered concrete subtype's Handle (e.g.
    `Handle(Geom_Line)`). This fails at the sol2 dispatch layer: `"no matching
    function call takes this number of arguments and the specified types"` -
    confirmed both via the constructor and via a separate `:Init()` call on a
    default-constructed instance (ruling out a constructor-overload-set-specific
    issue). This is a different mechanism from the Handle(Derived) upcast that
    *does* work today - calling an inherited *method* through `add_bases`
    (`test_geom.cpp`'s `ValueViaInheritedGeomCurve`) - because that dispatches by
    looking up the method name against the object's own registered base-class
    chain, whereas satisfying a `Handle(Base) const&` *function parameter* would
    require sol2 to reconstruct a brand-new `Handle(Base)` value from the stored
    `Handle(Derived)` userdata. Root-caused with an isolated, OCCT/grunk-free sol2
    repro: this is a genuine **sol2 3.5.0 limitation**, not specific to
    `src/occt_sol_traits.hpp`'s `unique_usertype_traits<opencascade::handle<T>>`
    specialization - sol2's own built-in `std::shared_ptr<T>` hits the identical
    gates/crash for a `Derived` satisfying a `Base` parameter, so this isn't
    something a different trait implementation could fix on its own. **Not fixed
    here** - tracked as
    [grunk-dev/grunk-occt#1](https://github.com/grunk-dev/grunk-occt/issues/1)
    (full sol2-internals writeup and the recommended fix), deliberately kept as an
    *occt-specific generator customization point* rather than a generic generator
    feature: `codegen/generate.py` is planned to eventually move into the `grunk`
    repo itself so other plugins can reuse it (a longer-term goal, deliberately out
    of scope for now)
    - this Handle(T)/base-casting problem is entirely specific to OCCT's handle
    idiom, not something a hypothetical future non-OCCT plugin using the same
    generator core would ever hit, so its fix belongs in this repo's own
    generator config/extension point when the move happens, not baked into a
    shared core. Different category from the Debug/Release `Handle(T)` upcast bug
    above ([grunk-dev/grunk#283](https://github.com/grunk-dev/grunk/issues/283)),
    which stays a grunk issue since it's about grunk/sol2's own build configuration,
    not a generator-emitted-code shape decision. Consequence: `GeomAPI_
    ProjectPointOnCurve`/`ProjectPointOnCurve`-shaped APIs taking a `Handle(Geom_
    Curve)`/`Handle(Geom_Surface)` parameter remain registered (still valid, useful
    C++) but are not currently *callable from Lua* with any real curve/surface -
    this will recur for any future module accepting a `Geom_Curve`/`Geom_Surface`
    handle as a parameter (not just as a return value or `self`), e.g.
    `BRep_Builder::MakeEdge`'s own `Handle(Geom_Curve)` overloads (accepted by the
    generator, never yet exercised from Lua either). `tests/test_geomapi.cpp`'s own
    `InterpolateBSplineSurfaceFromPointGrid` sidesteps this entirely -
    `GeomAPI_PointsToBSplineSurface::Interpolate` takes a 2D grid of `gp_Pnt` (via
    the `NCollection_Array2<gp_Pnt>` conversion above), no `Handle(Geom_*)` parameter
    involved.
- **`brepfilletapi`** (`BRepFilletAPI_*`, whole-module wildcard): legacy grocc's own
  `config.yml.in` wildcarded this whole module too, with **no** blacklist entries at
  all - re-derived fresh (verified, not just carried forward) and
  confirmed to still need none. 106 accepted, 30 rejected (unregistered enums/
  `Handle(Law_Function)`/`Handle(TopOpeBRepBuild_HBuilder)`, `TopTools_ListOfShape`/
  `TopTools_SequenceOfShape` returns, mutable `Standard_Real&` out-params - same
  categories as prior modules).
  - `BRepFilletAPI_MakeFillet` has **zero** accepted constructors: its only real
    constructor takes a trailing `ChFi3d_FilletShape` with a default value (at
    this point the generator didn't yet expand default arguments -
    **superseded by the "Default-argument expansion" section below**, which
    gives it a real constructor), so it's rejected outright - same category as
    `geomapi`'s own `GeomAPI_PointsToBSpline` (also blocked by a trailing
    `GeomAbs_Shape` default at the time, also fixed by that same section). It
    stays registered (its other members are still real, valid C++) but isn't
    instantiable from Lua today.
  - `BRepFilletAPI_MakeChamfer(const TopoDS_Shape&)` has no such trailing default
    enum, so it's the module's actual smoke-test candidate. Its `Add(Dis, E)` needs
    a `TopoDS_Edge` - extended `src/topexp_extras.hpp` with `occt.first_edge(shape)`,
    symmetric with the existing `first_face`. `tests/test_brepfilletapi.cpp`'s
    `ChamferOneEdgeOfBox` chamfers one edge of the smoke-test box and passed first try.

## Handle(X) return type requires being in the same translation unit as X

Found while adding `gc`: `GC_MakeSegment`/`GC_MakeCircle`'s `Value()` methods (in
their own `generated/gc.cpp`) return `Handle(Geom_TrimmedCurve)`/`Handle(Geom_Circle)`
by value - types registered in a *different* generated file, `generated/geom.cpp`.
Compiling `gc.cpp` on its own failed with:

```
error: no match for call to '(std::less<void>) (Geom_TrimmedCurve&, Geom_TrimmedCurve&)'
error: no match for 'operator<' (operand types are 'Geom_TrimmedCurve' and 'Geom_TrimmedCurve')
```

(and the `equal_to`/`operator==` equivalent). `Geom_TrimmedCurve`/`Geom_Circle`
declare neither operator - unremarkable, almost no OCCT geometry class does, and
plenty of other `Handle(X)`-for-such-an-X returns already work fine elsewhere in
this project (e.g. `Geom_BezierCurve::Copy() const -> Handle(Geom_Geometry)`,
structurally identical: a single, non-overloaded `add_member_function` returning a
`Handle(X)` where `X` also lacks comparison operators).

**Confirmed empirically, not just theorized**: compiling `generated/geom.cpp` in
isolation (`g++ -fsyntax-only`) always succeeds, including `Geom_TrimmedCurve`'s
*own* constructor (also `Handle(Geom_TrimmedCurve)`-returning). Moving
`GC_MakeSegment`/`GC_MakeCircle`'s registrations bodily into `geom.cpp` (as a
one-off test, not the shipped fix) made the whole error disappear. So the
determining factor is genuinely "is this Handle(X)-returning code being compiled in
the same translation unit as X's own `register_type<X, ...>()` call" - not anything
about `X` itself, `automagic_flags`, or whether the return is by value vs. by
reference (both were tried and ruled out; see the traced instantiation chain below).

The instantiation chain runs through sol2's own fallback path for a value pushed
onto the Lua stack whose usertype metatable might not exist yet:
`unqualified_pusher::push_keyed` -> `set_undefined_methods_on<Handle<X>>` ->
`insert_default_registrations<Handle<X>>` -> `comparsion_operator_wrap<Handle<X>,
std::less<>>`, whose body dereferences the handle down to `X&` before comparing.
`insert_default_registrations` gates this whole block on `is_automagical<T>::value`,
which (per `sol/types.hpp`) is essentially always `true` for an ordinary class and
does not appear to depend on the `automagic_flags` passed to `X`'s own
`register_type<X, Flags>()` at all - so *why* this succeeds when compiled alongside
`X`'s own registration, in the same `.cpp`, was not fully root-caused (a
`SOL_SAFE_FUNCTION_CALLS`/instantiation-context-dependent SFINAE check is the most
likely mechanism, by analogy with the Debug/Release discrepancy in
[grunk-dev/grunk#283](https://github.com/grunk-dev/grunk/issues/283), but this
wasn't confirmed the same way). Deliberately **not** filed as a grunk issue - this
is specific to how this project structures Handle(T)-heavy OCCT bindings across
multiple generated files, not a general grunk/sol2 usage pattern - so it's recorded
here instead.

**Fix**: `occt_plugin` is built as a single translation unit (a "unity build" -
`CMakeLists.txt` generates `${CMAKE_BINARY_DIR}/occt_plugin_unity.cpp`, `#include`ing
`src/occt_plugin.cpp` and every `generated/*.cpp`, and compiles only that one file,
rather than each `generated/*.cpp` as its own translation unit). This mirrors legacy
grocc's own `concatenate` config option (`config.yml.in`'s `settings.concatenate`),
applied here unconditionally rather than as an opt-in toggle. It eliminates the
*whole class* of this bug outright - `config.yml`'s modules can be split along
whatever logical lines make sense (matching legacy's own module boundaries, e.g. `gc`
staying separate from `geom` despite returning its types) without having to route
around this compile error module-by-module as more cross-referencing modules are
added later. The generated unity file is a build artifact (like any
`.o`), not checked in - written under `CMAKE_BINARY_DIR`, already covered by
`.gitignore`'s `/build/` entry.

**Superseded by "Translation-unit grouping" below**: applying this fix
*unconditionally* to the whole plugin (rather than only to the specific modules that
actually need it) is what eventually grew `occt_plugin_unity.cpp` large enough to
OOM-kill CI - the trade-off paragraph that used to be here accepted a real cost
(incremental rebuild time) without anticipating the actual failure mode that trade-off
would eventually cause. The underlying compile error and its fix (same-TU placement)
are still exactly as described above; only the *scope* of what gets merged changed -
see below for the current, config-driven mechanism (`config.yml`'s
`translation_units.groups`), which still relies on this same fix, just no longer
applies it to every module unconditionally.

## Translation-unit grouping

Found in CI, not locally: `ubuntu-latest` GitHub Actions runs started dying partway
through the "Build" step with `ninja: build stopped: interrupted by user` and exit
code 143 - misleading wording (Ninja's own generic message for *any* termination
signal, not literally a person cancelling) that obscured the real cause for several
runs (some over 4 minutes, `[22/25] ... test_brepfilletapi.cpp.o` was the last output
before the runner just fell silent for 8 minutes and died).

Measured directly (not guessed at): compiling `occt_plugin_unity.cpp` (the previous
section's unconditional single translation unit for the *entire* plugin) alone, on a
machine with abundant RAM/cores, needs **~12-13GB of peak RSS and ~4 minutes**,
regardless of optimization level (`-O3`/`-O1`/`-O0` all land within ~10% of each
other in both time and memory - the cost is dominated by the front end parsing and
instantiating thousands of `sol::overload(...)` templates concatenated into one file,
not the optimizer backend, so no compiler-flag tweak was going to fix this).
GitHub's hosted `ubuntu-latest` runners have 4 vCPUs / 16GB total system memory - a
single process needing 12+GB, running alongside the OS and anything else `ninja
--parallel` has going concurrently, reliably exhausts that.

**Fix**: `config.yml`'s new top-level `translation_units` key (see
`codegen/README.md`'s own entry and `generate.py`'s `compute_translation_units()`)
makes the "one giant TU" choice a config knob instead of a hardcoded default.
`mode: separate` (the new default) compiles each module as its own translation
unit; `groups` names sets of modules that must still share one, for the same
Handle(X) reason as before. `generate.py` writes the resolved grouping to
`generated/translation_units.txt` (one manifest line per translation unit, or a
single line `*` for `mode: unity`); `CMakeLists.txt` reads it at configure time and
builds accordingly - a single-module line compiles that module's own
`generated/<name>.cpp` directly (no synthesized wrapper needed, real incremental
rebuilds, real parallelism), a multi-module line gets a small synthesized
`#include`-based wrapper file, same mechanism `mode: unity` uses for everything.

**Real risk this surfaced, not just a hypothetical**: splitting a translation unit
that used to be "everything" risks losing more than the one already-known gc/geom
Handle(X) pairing - any module whose generated code references another type only
*coincidentally* completed by some unrelated module's own `#include` (rather than by
its own headers or `extra_includes`) would silently compile in the old unity build
and break once split. This happened for real, found only by actually attempting the
split and building each module standalone, not by static analysis:

- `geomapi`'s `GeomAPI_IntCS::Segment`/`GeomAPI_IntSS::Line` return `Handle(Geom_Curve)`
  by value - a second, previously-undiscovered instance of the exact same gc/geom
  problem. Added to the same group: `groups: [[gc, geom, geomapi]]`.
- `gc`, `bnd`, `brepalgoapi`, and `brepbuilderapi` each referenced one or more
  `gp_*`/`TopoDS_*` types (`gp_Cylinder`, `gp_Cone`, `gp_Circ`, `gp_Elips`, `gp_Hypr`,
  `gp_Lin`, `gp_Parab`, `gp_Pln`, plus 2D variants and `TopoDS_CompSolid`) that were
  only ever forward-declared by the headers those modules actually wrap (e.g.
  `GC_MakeCylindricalSurface.hxx`'s own `class gp_Cylinder;`, via a transitive
  `Geom_CylindricalSurface.hxx` include) - complete only because `gp.cpp`/`topods.cpp`
  happened to also be in the same old unity TU. Fixed via `extra_includes` (the
  pre-existing escape hatch for exactly this shape of problem, previously used only
  for `TopoDS_*`/`TopLoc_Location` forward-decl gaps discovered under the always-unity
  build) - see each module's own `config.yml` comment for the specific list. A stale
  comment on `gc`'s own entry ("No extra_includes needed... confirmed by building")
  is exactly the trap this describes: "confirmed by building" under the old unity
  build only ever proved *concatenated-with-everything-else* compiled, not standalone.

Found systematically, not one build-fail-and-patch cycle at a time (each full build
took ~2 minutes): a `g++ -fsyntax-only` probe compiling each `generated/<module>.cpp`
directly (or the `gc`/`geom`/`geomapi` group together) with the real project's own
include flags, extracted once via `ninja -t commands`, surfaces every incomplete-type
error across the whole plugin in one pass, since `-fsyntax-only` skips code
generation entirely and GCC reports every independent error in a single invocation
rather than stopping at the first.

**Result, verified**: a full clean build of the whole project (plugin + all 37
tests) now completes in under 2 minutes with default parallelism, peaking at ~5.2GB
RSS *across every concurrent job combined* - down from one single process alone
needing ~12-13GB. All 37 tests and both example recipes (`pixi run -e default run_examples`)
still pass unchanged. `mode: unity` remains available and was verified to still
produce the exact same manifest (`*`) and unity-build behavior as before, for anyone
who wants the original one-invocation build back.

## Generator redesign: enums, multi-class-per-header, wildcard headers

The generator needed to support binding C++ enums (many rejected callables
return/take one - `GeomAbs_Shape`, `TopAbs_Orientation`, ...), the original "one
class per header, named for the header's filename" assumption didn't hold for
every OCCT header (a
glob can match a typedef-only header, an enum-only header, or occasionally more than
one class), and the original hand-picked `gp` module (5 headers) was missing
`gp_Lin`/`gp_Ax2`/`gp_Circ`/... - real, ubiquitous OCCT types needed as parameters/
returns across many other modules already in this project, not a hypothetical
concern. See `codegen/README.md`'s own "Lifting the one-class-per-header assumption"
/ "Wildcard header patterns" / "Enum support" sections for the full technical detail
(AST discovery mechanism, the `NCollection_Lerp<gp_Trsf>` template-specialization
pitfall found along the way, the `get_children()` vs. `walk_preorder()` performance
finding, sol2's `new_enum` API and the `plugin_namespace::table()` constness
workaround) - this note is about the concrete *outcome*.

`gp`'s config changed from 5 hand-picked headers to `gp_*.hxx` wholesale (matching
legacy grocc's own convention), plus two blacklist entries re-derived fresh
(`gp_Pnt::BaryCenter`, the pre-existing blacklist checkpoint;
`gp_VectorWithNullMagnitude`, an exception type, not a geometric value type). `gp`
alone went from 166 to 1255
accepted callables. More importantly, since `registered_types`/`registered_enums`
are computed globally across the whole config before any module is processed (see
codegen/README.md's "Type registration spans the whole config"), this **retroactively
unlocked real, previously-rejected coverage in modules whose own config didn't
change at all**:

| module | accepted before | accepted after | example newly-unlocked callable |
|---|---|---|---|
| `geom` | 137 | 148 | `Geom_Line::Lin() -> gp_Lin`, `Geom_Conic::SetAxis(gp_Ax2 const&)` |
| `brepprimapi` | 18 | 22 | `BRepPrimAPI_MakeCylinder(gp_Ax2 const&, R, H)` (a *positioned* cylinder - the axis-less overload was the only one available before) |
| `bnd` | 50 | 52 | `Bnd_Box::IsOut(gp_Lin const&)`, `IsOut(gp_Pln const&)` |
| `brep` | 39 | 42 | (assorted) |
| `gc` | 8 | 15 | `GC_MakeCircle`'s `gp_Circ`-based constructors (3 of its 7 total, previously all needing unregistered `gp_Circ`/`gp_Ax2`) |

This directly validates the concern that prompted the redesign: `gp_Lin`/`gp_Ax2`
etc. really were needed in multiple places already in this project, and hand-picking
a module's headers one at a time doesn't scale to that - a wildcard plus accurate
per-header discovery does. `geom`'s own `extra_includes` grew to cover
`gp_Lin.hxx`/`gp_Circ.hxx`/`gp_Ax2.hxx` (needed for the same by-value-return reason as
every other `extra_includes` entry in this project - `Geom_Line.hxx`/
`Geom_Circle.hxx`/`Geom_Conic.hxx` only forward-declare them).

`tests/test_gp_wildcard.cpp` (`GpWildcardTest`) exercises four of these directly:
enum values reachable from Lua (`occt.gp_TrsfForm.gp_Identity`), `Geom_Line::Lin()`,
a positioned `BRepPrimAPI_MakeCylinder`, and `GC_MakeCircle` from a `gp_Circ` - not
`gp` in isolation (already covered by `tests/test_gp.cpp`), specifically the
retroactively-unlocked call sites in *other* modules.

**Known, deliberately unaddressed gap**: `NCollection_Lerp::Interpolate` (used
internally by `gp_QuaternionNLerp`/`gp_QuaternionSLerp`/`gp_TrsfNLerp`) takes a bare
`double` parameter rather than the `Standard_Real` typedef alias most of OCCT uses -
`Param.kind()`'s `Standard_Real` check is a literal string match against the
*spelling* "Standard_Real", not a canonical-type comparison, so this one rejects.
Low-value fix (this is about the only place in OCCT's own headers observed writing
`double` instead of `Standard_Real`) - noted here rather than carrying an unexplained
rejection silently.

**Windows CI fallout from the `gp` wildcard expansion**: `occt_plugin_unity.cpp`
(the unity build, see its own section above) failed to compile on Windows CI only,
with:

```
fatal error C1128: number of sections exceeded object file format limit: compile with /bigobj
```

A well-known MSVC limitation, not specific to this project - a single translation
unit generating enough object-file sections (unsurprising once `gp` alone
contributes ~1255 registered callables to that one TU) can exceed the default COFF
format's section-count limit. Linux/macOS (GCC/Clang) have no equivalent limit, so
this never surfaced there. Fixed with CMake's standard answer:
`target_compile_options(occt_plugin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)` -
gated to MSVC only via a generator expression (a no-op, not merely harmless, on
every other compiler/platform). Could not be verified against a real MSVC compiler
in this environment - confirmed only that the generator-expression gating doesn't
regress the Linux build, and that `/bigobj` is CMake's own long-standing, standard
remedy for exactly this MSVC error class; real confirmation is Windows CI itself
going green on this fix.

## Example recipes, and a real gap between recipe-level and module-level dynamic dispatch

`examples/cavity_model.grr.yml`, `examples/cavity_model_v2.grr.yml` (ported from
legacy's own `20241017.grr.yml`), and `examples/tank_model.grr.yml` (see
`examples/README.md`) port legacy grocc's own recipes to the new
`grunk::recipe` YAML+Lua format, sharing one small pure-Lua grunk plugin
(`examples/plugins/occt_helpers.lua`, declared
explicitly in each recipe/sub-recipe's own `uses:` block - see `examples/README.md`'s
"Why a plugin, not a shared module" for why it's a plugin rather than a module
registered ambiently by the runner) loaded by `examples/run_example.py` (grunk's
Python bindings - no bundled `ctest` integration coverage for this script, since
building grunk's Python bindings isn't currently part of `grunk-occt`'s own
CMake/pixi setup). Getting `cavity_model.grr.yml`/`tank_model.grr.yml` there
needed:

- **Widening `brepbuilderapi`/`brepprimapi`/`brepalgoapi`/`gc` to whole-module
  wildcards** (matching legacy grocc's own config, same pattern as `brepoffsetapi`/
  `geomapi`/`brepfilletapi` before them) - the two recipes need `MakeFace`/
  `MakePolygon`/`MakeWire`/`MakeEdge`/`Transform`, `MakeRevol`/`MakeHalfSpace`/
  `MakeSphere`/`MakePrism`, `Fuse`/`Common`, and `GC_MakeArcOfCircle`, none of which
  the earlier hand-picked subsets included. Needed a new generator feature to get
  there: `expand_headers`'s `exclude_headers` (`codegen/config.yml`'s
  `exclude_headers:` key) - `BRepBuilderAPI_CellFilter.hxx`/`VertexInspector.hxx`/
  `FastSewing.hxx`/`BndBoxTreeSelector.hxx` don't even *parse* standalone
  (`NCollection_CellFilter<gp_XY>`/`NCollection_UBTree` instantiated without
  `gp_XY`'s own header) - a hard clang error the existing blacklist mechanism can't
  reach (blacklist only excludes a symbol *after* its header parses).
- **`TopoDS_Builder` was never actually registered**, despite `brep`'s own config
  comment claiming "nothing it declares is needed from Lua" - wrong, found while
  writing `occt_helpers.compound`: `MakeCompound`/`Add` (needed to assemble a
  `TopoDS_Compound`) live on `TopoDS_Builder`, not `BRep_Builder` itself. Corrected
  by adding `TopoDS_Builder.hxx` to the `brep` module's own headers - the base class
  must be *registered*, with its own real member functions, for `add_bases<...>()`
  dispatch to have anything to find; it is not enough for the type to just be
  visible/complete for pointer casting, contrary to that comment's prior claim.
- **`src/math_extras.hpp`** (`occt.cos`/`sin`/`sqrt`/`pi`) - grunk's Lua state only
  opens `sol::lib::base` (`grunk/dynamic/state.hpp`), not `sol::lib::math`, so
  angle-based positioning (both recipes need real trigonometry) had nothing to call
  without this.
- **`src/gc/gc_extras.hpp`** (`occt.edge_from_arc`) - `tank_model`'s
  `cylinder_segment` sub-recipe needs `GC_MakeArcOfCircle::Value()` (returns
  `Handle(Geom_TrimmedCurve)`) fed into `BRepBuilderAPI_MakeEdge` (only accepts
  `Handle(Geom_Curve)`, the abstract base) - the exact "Handle(Derived) does not
  satisfy a Handle(Base) parameter" sol2 limitation tracked as
  [grunk-dev/grunk-occt#1](https://github.com/grunk-dev/grunk-occt/issues/1). Rather
  than wait on that fix, this one call site is done entirely in C++ (construct the
  arc, build the edge from it, return only the finished `TopoDS_Edge`), so the
  Handle upcast happens inside plain C++, never crossing into sol2's argument
  matching at all.

`examples/cavity_model_v2.grr.yml` (ported later, once `cavity_model.grr.yml`/
`tank_model.grr.yml` were already working, from legacy's own `20241017.grr.yml`)
needed no new *module* - every OCCT class/method it uses was already covered by
the module-level work above - just two small additions:

- **`occt.acos`/`occt.tan`** (`src/math_extras.hpp`) - this recipe's own
  `angle_diff_stator` formula needs both; only `cos`/`sin`/`sqrt`/`pi` existed
  before.
- **`occt_helpers.comp_solid`** (`examples/plugins/occt_helpers.lua`) - this
  recipe groups its two revolved sectors into a `TopoDS_CompSolid` (meant to be
  fused with the cylinders as one connected solid), not a `TopoDS_Compound` (an
  unstructured group) the way `tank_model.grr.yml`'s own `compound()` helper
  builds. Added as its own function (`BRep_Builder::MakeCompSolid` instead of
  `MakeCompound`, same `IsNull()` guard, same reason for it) rather than
  generalizing `compound()` to take the container type as a parameter - matches
  this file's own established style of small, explicit, near-duplicate helpers
  (`translate`/`rotate`/`mirror_point` already are) over a shared abstraction.

### Recipe-level dynamic dispatch did not walk `add_bases<...>()` - module-level dispatch did (fixed upstream, `grunk-dev/grunk#285`)

**Resolved**: fixed upstream in `grunk` (`grunk-dev/grunk#285`, fix commit `63aad18`) and adopted
here once this repo's own `grunk` pin was bumped past it. The `:as(occt.BRepBuilderAPI_MakeShape):
Shape()`-style casts this section originally documented as a required workaround have been removed
from `examples/cavity_model.grr.yml`/`cavity_model_v2.grr.yml`/`tank_model.grr.yml` and
`examples/bspline_curve.grr.yml`'s own qualified-call workaround (`occt.Geom_Curve.Value(curve,
0.5)`) has been replaced with the plain `curve:Value(0.5)` form - verified to return the identical
value to the qualified form it replaced, and the full test suite (42/42) still passes. The rest of
this section is kept for its still-accurate explanation of *why* the gap existed and how it was
found.

The single most surprising discovery of this milestone, found the hard way writing
`cavity_model.grr.yml`'s `cylinder` sub-recipe:

```lua
-- used to fail: "Feature: type "occt.BRepPrimAPI_MakeCylinder" has no member
--                "Shape" - use :as(Type) explicitly if this is intentional."
-- (now works directly, see the "Resolved" note above)
cylinder = occt.BRepPrimAPI_MakeCylinder.new(ax2, radius, height):Shape()
```

`Shape()` is a real, accepted, generated member function - just not *directly* on
`BRepPrimAPI_MakeCylinder`'s own usertype, only reached via
`add_bases<BRepBuilderAPI_MakeShape, BRepBuilderAPI_Command>()` (see
`generated/brepprimapi.cpp`). That distinction matters because a recipe's own
`steps:`/`recipes:` Lua runs in grunk's "decorated"/parametric environment, where
every value is a `DynamicFeature`, and `DynamicFeature`'s own `__index` metamethod
(`grunk/dynamic/state.hpp`) resolves `feature:SomeMethod(...)` by looking `SomeMethod`
up **directly in the exact type recorded at construction time** (`m_type_registry
[type_hint]`, a flat table lookup) - it does not walk sol2's own C++ inheritance
metadata the way a plain, undecorated Lua call does. This is not a grunk-occt bug;
it is documented, intentional behavior with its own escape hatch, `DynamicFeature::
as(usertype_table)`:

```lua
-- works: :as() re-points the lookup at BRepBuilderAPI_MakeShape's own method
-- table instead of BRepPrimAPI_MakeCylinder's.
cylinder = occt.BRepPrimAPI_MakeCylinder.new(ax2, radius, height)
    :as(occt.BRepBuilderAPI_MakeShape):Shape()
```

Before #285 was fixed, every `BRepPrimAPI_Make*`/`BRepAlgoAPI_*`/
`BRepBuilderAPI_Transform` call in both ported recipes needed this `:as(occt.
BRepBuilderAPI_MakeShape):Shape()` form whenever it appeared directly in a
recipe's own `steps:`/`recipes:` (never inside a module - see below). Now that
`DynamicFeature::__index` walks `add_bases<...>()` too, the plain `:Shape()` form
works the same way everywhere, recipe-level or module-level.

**Module/plugin bodies do not have this problem** - confirmed empirically, not
assumed: `examples/plugins/occt_helpers.lua`'s own `translate`/`rotate`/
`mirror_point` call `occt.BRepBuilderAPI_Transform.new(...):Shape()` with the
*plain* form, no `:as()`, and it works (a pure-Lua plugin, loaded via
`state.load_lua_plugin_file`, is a thin wrapper over `run_module_file` - same
execution semantics as a plain module for everything below). A module/plugin call
is one opaque, non-parametric compute node from the calling recipe's point of view
(see the plugin `uses:`/Modules section in `grunk`'s own README/docs) - internally,
its own `occt.*` calls run as ordinary, undecorated values, and plain sol2 dispatch
*does* walk `add_bases<...>()` correctly, the same as every raw-Lua
`tests/test_*.cpp` string in this whole project. First attempted the opposite fix
(adding a `shape_of()` wrapper *inside* the module, calling `:as()` there) - that
fails too, with `attempt to call a nil value (method 'as')`, because by the time a
value crosses **into** a module/plugin call as one of its parameters, it has
already been unwrapped to a plain object - there is no `DynamicFeature` left inside
one for `:as()` (a `DynamicFeature`-only method) to be called on at all. `:as()`
can only ever be used directly in a recipe's own `steps:`/`recipes:`, never inside
a module or plugin.

**A related, narrower gotcha in the same area, and a real bug (not just an API
quirk) found via it**: a module/plugin parameter that is a *table* of features
(e.g. `occt_helpers.polygon_face(points)`, `points` built as `{p0, p1, ...}`
directly in a recipe's own `steps:`) only has its *top-level* unwrapping done
automatically at the call boundary - the table itself arrives as a plain Lua
table, but each individual element inside it is still the original
`DynamicFeature`:

```lua
-- fails: "no matching function call takes this number of arguments and the
--         specified types" - points[i] is a DynamicFeature, not a raw gp_Pnt.
mp:Add(points[i])

-- "works" in the sense that it doesn't error - but see below, it can silently
-- read back the WRONG value:
mp:Add(points[i]:value())
```

Worse: this table-wrapping path doesn't just need an extra `:value()` call, it
can silently produce the **wrong value** - a tracked value read back correctly
when passed as a *direct* module argument reads back as a stale/default value
when the exact same value is first wrapped in a Lua table and *that table* is
what crosses the module-call boundary. Confirmed with a minimal repro
independent of this project's own code (bare `grunk`, a trivial pure-Lua
plugin, no OCCT involved) - filed upstream as
[grunk-dev/grunk#289](https://github.com/grunk-dev/grunk/issues/289). This is
exactly what was silently wrong in `examples/cavity_model.grr.yml`'s
`cavity_sketch` sub-recipe: its caller's parameter overrides never took
effect, because the 8 points it builds were passed to
`occt_helpers.polygon_face` as a table.

The fix used throughout `occt_helpers.lua` (`polygon_face`/`wire_face`/
`compound`) is to avoid the table boundary entirely: each function takes its
elements as trailing Lua varargs (`function polygon_face(...) ... end`,
`select('#', ...)`/`select(i, ...)` to iterate) instead of a single table
parameter. Each vararg element is a genuinely separate module argument, so it
is unwrapped exactly like any other direct top-level parameter (`shape`,
`vec`, `axis`, ...) - no `:value()` call needed, and no stale-value bug.

**Relationship to grunk-dev/grunk#269/#270**: that PR added `DynamicFeature`'s
type-hint storage in the first place - before it, `feature:method(...)` colon-call
syntax on a type-erased, not-yet-evaluated `DynamicFeature` couldn't work at all
(see #269's own issue text: the whole point was making `feature:method()` resolve
without evaluating `feature`, which breaks lazy evaluation). It was never about,
and its own issue text never mentions, walking a type's C++ inheritance
(`add_bases<...>()`) chain - `DynamicFeature::__index`'s lookup
(`m_type_registry[type_hint][key]`, `grunk/dynamic/state.hpp`) is a flat,
exact-type table lookup by design, the same way `sol::usertype::add_bases<...>()`
(`grunk/dynamic/usertype_proxy.hpp`) only ever sets `ut[sol::base_classes]` for
sol2's own casting - two independent mechanisms that were never wired together.
grunk's own history already has one precedent for exactly this shape of gap
(commit `428b3a4`, predating #270: "When registering base class member functions,
we have to register them explicitly by adding a base class or by wrapping it with
the derived class as first argument to be recognized as a member function") - this
generator's own heavy reliance on `add_bases<...>()` for code reuse (nearly every
accepted OCCT class reaches `Shape()`/`IsDone()` this way) is what makes the gap
show up constantly here, in a way a smaller, less inheritance-heavy plugin
wouldn't. Filed as
[grunk-dev/grunk#285](https://github.com/grunk-dev/grunk/issues/285) - unlike the
OCCT/`Handle(T)` sol2 issue above, this one is entirely about grunk's own
dynamic-dispatch design, not anything OCCT-specific, so it belongs in `grunk`
itself, not here.

**Done**: `#285` landed upstream and this repo's own `grunk` pin has been bumped
past it. `examples/cavity_model.grr.yml`/`examples/cavity_model_v2.grr.yml`/
`examples/tank_model.grr.yml`/`examples/plugins/occt_helpers.lua` no longer use
the `:as(occt.BRepBuilderAPI_MakeShape):Shape()`-style casts - every call site
uses the plain `:Shape()` form now, verified against the full test suite
(42/42).

Not filed as a grunk issue - both behaviors are grunk's own documented, deliberate
design (see `state.hpp`'s own doc comments on `Feature.__index`/`DynamicFeature::
as`), not bugs; this section exists so the next recipe written against `occt_plugin`
doesn't have to rediscover it from a cryptic runtime error.

### `fuel_D150.grr`/`fuelsystem.grr.yml` - not ported

Two legacy example recipes, both large (1800+ line) auto-exported parameter
dumps rather than hand-authored recipes - a poor fit for hand-porting, so they
were never added under `examples/`. `cavity_model.grr.yml`/
`cavity_model_v2.grr.yml`/`tank_model.grr.yml` already exercise every module
either of them would add (`gp`, `geom`/`gc`, `topods`, `brep`, `brepbuilderapi`,
`brepprimapi`, `brepalgoapi`, `message_progress`) plus sub-recipes 3 levels deep
and a shared module.

### OCCT surface coverage audit

A follow-up pass checked whether the binding's coverage maps the functionality
of every OCCT/`grocc`-tag usage across the legacy recipes above plus six more
recipes that use plugins beyond `grunk-occt`'s own scope (`tigl`/`geo`, CAD
aircraft-geometry plugins not part of this project) - `CPACS_wingbox.grr.yml`,
`seat_model.grr.yml`, `load_carrier.grr`,
`wing_fuselage_configuration/{fused_wing_fuselage,solid_fuselage,solid_wing}.grr`,
`hello_world/box.grr.yml`, `wingBody/wing_body.grr.yml` - checked class-by-class,
method-by-method against what `generated/*.cpp`/`src/**/*.hpp` actually
register (only each recipe's own OCCT/`grocc`-tag usage matters here, the
`tigl::`/`geo::` tags themselves are out of scope).

**Result**: `fuel_D150.grr`/`fuelsystem.grr.yml` themselves needed nothing new -
every symbol they use (`TColgp_Array1OfPnt`+`SetValue`+`Handle_Geom_BezierCurve`,
`BRepOffsetAPI_MakePipe`, `BRepOffsetAPI_ThruSections`+`AddWire`, `gp_Circ`,
`BRepBuilderAPI_MakeWire`+`Add`, ...) already maps onto already-registered
functionality. Two real gaps turned up, both in the excluded
`tigl`/`geo`-adjacent recipes, both now fixed:

- **`Geom_BSplineSurface`** (needed for `ExchangeUV` in
  `fused_wing_fuselage.grr`/`solid_fuselage.grr`/`solid_wing.grr`) and
  **`Geom_BezierSurface`** (needed for `wing_body.grr.yml`'s own
  `Handle_Geom_BezierSurface`) were not registered at all - the surface-side
  counterparts of the already-registered `Geom_BSplineCurve`/`Geom_BezierCurve`.
  Added to the `geom` module (`Geom_Surface`/`Geom_BoundedSurface` as the base
  classes first, matching the existing curve-chain convention) - their own
  constructors needed no new generator machinery at all, reusing `to_array2`/
  `to_real_array2` (`src/collections.hpp`) exactly the way
  `GeomAPI_PointsToBSplineSurface::Interpolate` already does (`tests/
  test_geomapi.cpp`). Verified with two new tests (`tests/test_geom.cpp`,
  `BSplineSurfaceExchangeUVSwapsParameters`/`BezierSurfaceFromPoleGridExchangeUV`)
  checking `ExchangeUV`'s own defining property directly (`Value(u,v)` after
  equals `Value(v,u)` before, on an asymmetric pole grid), not just that it runs.
- **`gp_GTrsf2d.hxx`** needed adding to the `geom` module's own
  `extra_includes` - `Geom_Surface::ParametricTransformation` returns
  `gp_GTrsf2d` by value, only forward-declared (`friend class gp_GTrsf2d;` in
  `gp_Mat2d.hxx`) by anything the module already included - same "return/take by
  value but only forward-declared" story as `gp_Lin`/`gp_Circ`/`gp_Ax2` before
  it.
- **Registering `Geom_Surface` newly type-accepted two functions in *other*
  modules that return `Handle(Geom_Surface)`** - `BRep_Tool::Surface` (`brep`)
  and `BRepFilletAPI_MakeFillet::ComputedSurface` (`brepfilletapi`), both
  previously rejected outright ("unsupported return type") since Geom_Surface
  wasn't a registered type at all. Hit exactly the known
  `Handle(X)-returning-code-only-compiles-in-X's-own-translation-unit` issue
  `codegen/config.yml`'s own `translation_units` comment already documents and
  warns to watch for - fixed the documented way, adding `brep`/`brepfilletapi`
  to the `[gc, geom, geomapi]` group rather than blacklisting either function (a
  first attempt did blacklist `BRep_Tool::Surface` before this was recognized as
  the same already-solved problem, then reverted once it was).

Everything else checked across all 8 files was already covered - some only
after checking more carefully than a first grep suggested: `BRep_Builder::
MakeShell`/`MakeSolid` initially looked unregistered, but both live on the base
`TopoDS_Builder`, reachable via `BRep_Builder`'s own already-established
`add_bases<TopoDS_Builder>()` (see this section's own earlier note on that).
No ambiguous/unmapped symbols turned up - every `grocc::` tag across all 8 files
resolved cleanly to a real OCCT class/method name.

### Default-argument expansion

Writing the example recipes kept running into the generator's own all-or-nothing
handling of a C++ default value: `BRepAlgoAPI_Cut(S1, S2, theRange =
Message_ProgressRange())` needed a real `Message_ProgressRange` from Lua every
single time (the `message_progress` module exists purely because of
this), and `BRepFilletAPI_MakeFillet(S, FShape = ChFi3d_Rational)` had **zero**
accepted constructors at all (its only constructor's sole non-defaulted parameter
came right before the unsupported, but genuinely optional, `ChFi3d_FilletShape`).
Both are the same underlying gap: this generator never expanded a defaulted
trailing parameter into a shorter overload the way legacy grocc's own generator
did - it just required every parameter, defaulted or not, from Lua.

Fixed by teaching the generator to detect a real C++ default value per parameter
(`param_has_default()`, via a libclang child-cursor heuristic - a `PARM_DECL`'s
own children include a `TYPE_REF`/`TEMPLATE_REF` for its *type*; a default value
shows up as an additional, different child kind entirely, confirmed empirically
against real OCCT headers rather than assumed) and emit one overload per reachable
argument count (`Callable.accepted_arities()`) instead of one all-or-nothing
decision. Crucially, the generator never needs to know the default's actual
*value* - a shorter overload just omits the trailing parameter(s) it doesn't pass,
and C++ itself supplies the real default at that call site. Full mechanism and
worked examples in `codegen/README.md`'s own "Default-argument expansion" section.

Concrete, immediate unlocks from one regeneration, no config changes needed:
`BRepFilletAPI_MakeFillet` gets a real constructor for the first time (previously
completely uninstantiable from Lua); `BRepAlgoAPI_Cut`/`Fuse`/`Common` no longer
require an explicit `Message_ProgressRange` argument (gained a 2-arg overload -
`message_progress`'s own module docstring is now more "why this module still
exists for the cases that do want one explicitly" than "why every boolean op
needs one"); `GeomAPI_PointsToBSpline` gained three real, callable overloads. Net
effect across the whole config, one regeneration, zero other changes: `gp`
1255→1262, `geom` 148→152, `brepbuilderapi` 210→248, `brepprimapi` 87→99,
`brepalgoapi` 55→76, `bnd` 52→55, `brep` 49→50, `breptools` 22→31, `geomapi`
40→74, `brepfilletapi` 106→110 accepted. Verified: full regeneration, clean
rebuild, all 35 tests plus both example recipes still pass with zero regressions.

## AD validation milestone

Recompiles this repo's existing registration code (unmodified: `src/occt_plugin.cpp` and
`generated/*.cpp`, compiled exactly as they are for a plain build) against
[adOCCT](https://github.com/dlr-sp/adOCCT), a fork of OCCT redefining `Standard_Real` to be
`Standard_Adouble` (a thin wrapper around ADOL-C's `adtl::adouble`), with `GRUNK_OCCT_WITH_ADOLC`
defined so every `#ifdef`-gated dual-emission overload (see "`Standard_Real` / AD-portability rule"
above, and "Default-argument expansion" for how it's actually emitted) switches to its
AD branch. This is the milestone the AD-portability rule was designed for from the start - the
whole point was for this to be a recompile, not a source rewrite, and that held.

### Single project, AD as a build option

`GRUNK_OCCT_ENABLE_AD` (`CMakeLists.txt`) - not a separate CMake tree. An earlier design used a
standalone `occt_adolc/` subdirectory project instead, justified by "`find_package(OpenCASCADE
...)` only resolves once per CMake configure run" (mirroring `cad_autodiff`'s own
`plugins/geoml_adolc/CMakeLists.txt` precedent). That's true, but doesn't actually apply here:
it's a real constraint on producing *both* stock and AD-enabled outputs from *one* configure, which
this project never needed to do - `GRUNK_OCCT_ENABLE_AD` picks *one* `find_package(OpenCASCADE
...)` branch per configure (see `CMakeLists.txt`'s own `if(GRUNK_OCCT_ENABLE_AD) ... else() ...
endif()`), never both, so the "resolves once" limitation never triggers. Reconsidered directly
because of this: a standalone project bought nothing this option doesn't, at the cost of real,
growing duplication between two near-identical `CMakeLists.txt` files (the grunk dependency pin -
`FetchContent` at the time this decision was made, since replaced by `find_package` - and the whole
translation-unit-grouping block) that would only have kept drifting apart the more
either project changed. Consolidating removed that duplication outright - `occt_plugin` is one
target either way, `GRUNK_OCCT_ENABLE_AD` only changes what it links against and which extra
compile definition it gets, not its name. Pick one variant per build directory (`build/` for
stock, `build-ad/` for AD - see README.md's "Algorithmic differentiation (AD)" and `pixi.toml`'s
`ad` environment); a single configure never needs both.

### Bootstrap automation (`scripts/install_adolc.sh`, `scripts/install_adocct.sh`)

Two idempotent shell scripts, wired into `pixi.toml`'s `ad` environment as `install_adolc`/
`install_adocct` (plus `configure`/`build`/`test`/`all` for the rest) - mirrors the same pattern
`grunk-adolc`'s own `scripts/install_adolc.sh` already established for that repo, so building the
AD variant here needs no more manual copy-paste shell than building the plain one does.

One deliberate difference from `grunk-adolc`'s own script, found the hard way while verifying this
one: both scripts install to **`.deps/`** (`.deps/adolc`, `.deps/adocct`), not `build/_deps/` -
outside the CMake build directory entirely. A routine `rm -rf build` (reconfiguring, switching
build types, or just the regression check run while writing this) would otherwise silently destroy
an already-built ADOL-C/adOCCT install too, forcing a full rebuild of all of OCCT the next time -
discovered directly, not anticipated in advance, when exactly that happened during this milestone's
own verification. Only the throwaway clone+build directories (safe to lose) still live under
`build/_deps/`, matching CMake's own `FetchContent` naming convention for what they actually are.
`grunk-adolc`'s own script has this same latent fragility (`build/_deps/ADOL-C`) - worth the same
fix there as a follow-up, not done as part of this milestone.

### `occt.Standard_Real` and `grunk-adolc`'s `adtl.adouble` are two distinct Lua types

Raised directly, worth recording even though not fixed here: this AD build registers
`Standard_Real`/`Standard_Adouble` as its own native sol2 usertype (`register_standard_real_ad`,
below) - a completely separate Lua-visible type from `adtl.adouble`, the sibling `grunk-adolc`
repo's own binding for the *same* underlying ADOL-C concept. Not just a naming difference: the two
are registered through fundamentally different mechanisms, for a real reason neither side can drop.
`grunk-adolc`'s `adtl_plugin.cpp` deliberately never links ADOL-C's own C++ headers directly (its
whole design point is staying a thin, general-purpose bridge around a *precompiled* SWIG-Lua
module, `adtl.so`) - so it can only expose `adtl::adouble` via `register_external_type`'s runtime
SWIG-Lua proxy mechanism, which has no compile-time C++ type information at all. `occt_ad_support.
hpp` needs the opposite: real, compile-time `Standard_Adouble` type information, because every one
of the hundreds of generated OCCT overloads needs sol2's *native* `register_type<T>()` marshalling
to work at all. A native sol2 usertype and a SWIG-Lua proxy userdata are different C-level
representations - there is no way to "just depend on grunk-adolc and reuse its binding" here; there
is no C++ implementation to share between them in the first place, only matching *method names*
(`getValue`/`getADValue`/`setADValue`) because both expose the same real ADOL-C API. This is also
the same split `cad_autodiff` (the reference material both `register_standard_real_ad` and
`adtl_plugin.cpp` were ported from) already made, for what's presumably this same reason.

The real, currently-unsolved consequence: a value can't cross between the two today. Seeding an AD
direction via `adtl.adouble.new(...)` (grunk-adolc) and passing the result into `occt.gp_Pnt.new
(...)` (this build) would not work - `to_real()`'s own `o.is<Standard_Real>()` check would not
recognize a foreign SWIG-Lua userdata, so it would either throw or silently fall back to treating
it as a fresh, non-differentiated value. This is exactly the interop the future `grunk-geoml`
(combining `grunk-adolc` with a differentiated CAD plugin) will need solved - a custom sol2 stack
getter/pusher bridging the two representations, most likely - deliberately **not** attempted
speculatively here: building that bridge only makes sense once `grunk-geoml` is real and it's
clear exactly what needs to cross the boundary, not before.

### The `Standard_Real` registration gap

The single genuinely load-bearing piece of new code this milestone needed (as opposed to
"recompile and it works"): `src/occt_ad_support.hpp`'s existing `to_real(sol::object o)` -
`o.is<Standard_Real>() ? o.as<Standard_Real>() : Standard_Real(o.as<double>())` - already existed
from the AD-portability rule's own inception, but `o.is<Standard_Real>()` can never be `true` for
anything unless `Standard_Real` itself (== `Standard_Adouble` under this build) has its own
registered sol2 usertype somewhere. Nothing in the plain build ever registered it - there was no
need to until an
AD-enabled build actually existed to exercise that branch. Without this registration, the AD
milestone would not fail to compile or to run - it would silently take the "else" branch on every
single dual-emission overload throughout every generated module, discarding whatever AD tape/
derivative an argument already carried and treating it as a brand-new value. The only symptom would
be "every derivative is 0", found (if at all) much later and far from its actual cause.

Fixed by `register_standard_real_ad()` (`src/occt_ad_support.hpp`, called first thing inside
`register_occt()` in `src/occt_plugin.cpp`, gated the same `#if defined(GRUNK_OCCT_WITH_ADOLC)` as
everything else here) - registers `occt.Standard_Real` as a real Lua usertype with a
`double`-taking constructor and ADOL-C's own `getValue`/`getADValue`/`setADValue` introspection,
ported directly from `cad_autodiff`'s `geoml_registration.hpp` (the identical block, verified
byte-for-byte equivalent in intent - not OCCT-plugin-specific in any way, it only ever touches
`Standard_Adouble`'s own API). Registration order among the other `register_*(ns)` calls in
`register_occt()` doesn't actually matter (all registration completes before any Lua runs) - placed
first anyway so nothing needs to reason about ordering again if a second AD-only registration is
ever added.

Everything else - every generated constructor/method's own dual-emission `to_real()` call, every
plain `Standard_Real`-returning accessor (`gp_Pnt::X()` etc., automatically wrapped as an
`occt.Standard_Real` return value once the type is registered, no dual emission needed on returns,
only on parameters) - already existed unchanged and needed no new code at all.

### Validation methodology

`tests/test_ad.cpp` (only built when `GRUNK_OCCT_ENABLE_AD` is ON - see `CMakeLists.txt`) mirrors
`cad_autodiff`'s own README ("Verifying the
derivative", the cubic-Bezier-weight closed-form check) but exercises this repo's own *generated*
`Geom_BezierCurve`/`gp_Pnt` bindings directly, not `geoml_registration.hpp`'s hand-written ones, and
uses its own round control-point numbers (only the methodology carries over, not the specific
figures): pole 0's X coordinate is seeded as an AD variable (`occt.Standard_Real.new(1.0)`,
`:setADValue(0, 1.0)`), every other pole coordinate a plain Lua number; the curve is evaluated at
`u=0.5`, where the four cubic Bernstein weights are `(0.125, 0.375, 0.375, 0.125)`. `dx(0.5)/dX0`
must equal pole 0's own weight, `0.125` (the curve is affine in each pole coordinate, so this holds
for any seeded value); `dy(0.5)/dX0` must be exactly `0`, since the seeded variable never appears in
any pole's Y coordinate - a real check (unlike a same-value, single-coordinate check alone) that a
derivative direction hasn't leaked across coordinates, e.g. via a constructor overload binding the
wrong parameter or an AD tape variable getting reused by mistake.

### The dlopen/SONAME collision hazard

Documented here, not solved (matches `cad_autodiff`'s own README, which raises this and defers it
the same way): `grunk::plugin::load_native` never `dlclose`s. A consuming application that loads
*both* the stock `occt_plugin` (`build/libocct_plugin.so`) and the AD-enabled one
(`build-ad/libocct_plugin.so` - same target name, different build directory, see "Single project,
AD as a build option" above) into the same process risks the dynamic linker silently resolving the
second's OCCT symbol references against the first's already-mapped ones if their SONAMEs collide -
which they very well might, since adOCCT is a fork of the same OCCT sharing its library naming.
This is not exercised or guarded against anywhere in this repo's own tests (each test binary loads
exactly one variant, never both) - a consuming application is expected to pick one variant per
process until symbol-versioning/`RTLD_LOCAL` isolation is investigated as its own, separate task,
exactly as `cad_autodiff`'s README leaves it.

### A related hazard, found outside the AD case: Python bindings + native plugin duplicating grunk's own dispatch state (resolved)

**Resolved**, not just documented: `grunk` no longer vendors itself via `FetchContent` into every
consumer's own build. `grunk`'s own `grunk_recipe`/`grunk_plugin`/`grunk` targets are built as
**SHARED** libraries (previously static-by-default), with proper install-time RPATH handling
(`CMAKE_INSTALL_RPATH_USE_LINK_PATH` plus `$ORIGIN`/`@loader_path`/an explicit conda-environment
path), and `grunk-occt` now depends on a single pre-built, pre-installed grunk via `find_package
(grunk REQUIRED)` (see `scripts/install_grunk.cmake`, `.deps/grunk`) instead of vendoring grunk's
source and compiling a private copy into `occt_plugin.so`. `grunk`'s own Python bindings
(`grunk/python/_bindings.cpp`) now dynamically link that same shared `grunk` build too (bundled
into the wheel's own `grunk/` package directory) rather than statically embedding it.

This was originally found while re-verifying `examples/run_example.py` after the grunk pin bump
that retired the `#285` workaround (see "Recipe-level dynamic dispatch" above): `run_example.py`
crashed (`free(): invalid pointer`, or - with the `:as()` casts still in place - an even earlier,
silent abort with no Python-level exception at all) even though the exact same recipe files, read
through the exact same `occt_plugin.so`, passed cleanly through this repo's own C++ test suite. The
original hypothesis here was that two independently compiled copies of grunk's *header-only*
dispatch machinery (`DynamicFeature::__index`/`add_bases<...>()`, `grunk/dynamic/state.hpp`) were
somehow keeping separate registry state - further investigation (verifying the fix, below) showed
this specific mechanism was not quite right: that dispatch state lives as ordinary *member data* on
the `grunk::state` C++ object itself, not in any static/global storage, so it is not literally
duplicated by having two compiled copies of the code around - two independently vendored,
independently compiled copies of grunk (different commits, different compiler flags, different
`GRUNK_WITH_*` option combinations between `grunk-occt`'s own FetchContent-forced settings and
whatever built the Python wheel) are a much more direct explanation for a real ABI/behavioral
mismatch between the two. Either way, the fix is the same: use one shared, identically-built grunk
everywhere. Confirmed working, not just plausible: `grunk`'s own `test_native_plugin_load` (Python
test) loads a plugin `.so` built from a *separate* CMake configure into a Python-created
`grunk.state()` - exactly this repo's own cross-build-tree scenario, reduced to its essentials -
and passes cleanly once grunk's libraries are SHARED, with both sides pointed at the same install.
`grunk-occt`'s own suite (`pixi run -e default test`, `-e ad test`) continues to pass unchanged.

### What could and could not be verified in this environment

Superseded by later work in this same environment: `scripts/install_adocct.sh`'s own `cmake`/
`ninja install` invocation (building adOCCT itself, `-DUSE_ADOLC=ON -DADOLC_REVERSE_MODE=OFF`) *has*
since been run for real, end to end, in this sandbox - see "AD validation milestone" above, whose
39/39-then-40/40 `pixi run -e ad test` results (closed-form Bezier derivative check plus, once
`bspline_curve.grr.yml` existed, an AD-vs-finite-difference check on a real recipe) are the actual
evidence, not the reasoning-from-precedent this section originally described. `.github/workflows/
ci.yml`'s own `build-and-test-ad` job now runs this on every PR (Linux-only, matching `pixi.toml`'s
`ad` feature), caching `.deps/adolc`/`.deps/adocct` so it doesn't rebuild ADOL-C/adOCCT from source
on every run.

What was verified earlier, before adOCCT/ADOL-C build access was available, kept here for history:

- **`scripts/install_adolc.sh`** - run for real, end to end, producing a working ADOL-C 2.7.2
  install (ADOL-C has no adOCCT-scale third-party dependency closure, so this was actually
  feasible before adOCCT itself was).
- **`GRUNK_OCCT_ENABLE_AD`'s own CMake wiring** - configured for real against that real ADOL-C
  install (via `pkg_check_modules(ADOLC ...)`, which found it correctly) and a stand-in
  `ADOCCT_DIR` (stock OCCT's own install, just to exercise the `find_package(OpenCASCADE CONFIG
  ... PATHS ... NO_DEFAULT_PATH)` branch structurally). The build then failed exactly where
  expected - `Standard_Adouble` genuinely not existing outside real adOCCT - and nowhere else,
  confirming the option/`pkg_check_modules`/compile-definition/linking wiring itself is correct.
  Also verified: `GRUNK_OCCT_ENABLE_AD=ON` with `ADOCCT_DIR` unset fails loudly with the intended
  message rather than some other CMake error.
- **The plain (AD-off) build** - a full clean reconfigure/build/test/`run_examples` after this
  whole refactor, confirming zero regressions from consolidating `occt_adolc/` away (37/37 tests,
  both example recipes).

### Reusing one shared `Standard_Real`/`adtl::adouble` registration, not two

Superseding the "two distinct Lua types" section above: raised directly - the AD ecosystem's real
long-term goal is AD-enabled workflows where a differentiated `geoml` plugin and this project's own
AD-enabled `occt_plugin` pass `adouble` values to each other, which two independently-registered
native sol2 usertypes for the "same" underlying value can never do (see the section this replaces
for why they were ever two in the first place). The fix isn't a bridge between them - it's not
having two: sol2 usertypes are keyed by C++ type identity, shared automatically across every plugin
loaded into the same `lua_State`, so **one** native `register_type<adtl::adouble>()` call, done
once by `grunk-adolc` (the "intermediate ADOL-C plugin"), is automatically visible to this project's
own generated code once both plugins are loaded into the same `grunk::state` - no bridging code
needed, provided `Standard_Adouble` (adOCCT's own redefinition of `Standard_Real`) and ADOL-C's own
`adtl::adouble` are the same C++ type (confirm this once adOCCT is actually available - not verified
here).

This retires `register_standard_real_ad()` from `occt_ad_support.hpp` entirely (grunk-adolc's own
registration replaces it) and replaces `grunk-adolc`'s previous SWIG-Lua (`swig-adol-c`/`adtl.so`)
binding with a native one generated by this same generator, pointed at ADOL-C's own public
`adtl.h` header instead of a `.i` file - see `grunk-adolc/DESIGN.md` for that side of the story
(what changed there, and why the SWIG-based plugin was kept on its own `swig-adtl-plugin` branch
rather than deleted).

`register_occt()` (`src/occt_plugin.cpp`) does **not** check that grunk-adolc's plugin is loaded.
An earlier version of this function did - a `state.plugins()` scan for `"adtl"`, thrown as a clear
error if missing - but that check ran at `occt_plugin`'s own registration time, which is stricter
than the real requirement: registering doesn't touch Lua, so the two plugins' *registration* calls
can happen in either order; only *use* (any AD-enabled OCCT Lua call, via `to_real()`) actually
needs `adtl` to already be loaded by then. The eager check rejected a perfectly valid load order
(`occt_plugin` first, `adtl` second) as if it were broken. Rather than patch that in a second,
ad-hoc way local to this repo (e.g. moving the check into `to_real()` itself, checked lazily and
cached), this is being pushed upstream instead: `grunk-dev/grunk#287` proposes a
general, load-order-independent mechanism for a plugin to declare "I need plugin X loaded (in any
order), checked once all plugins are loaded rather than at each one's own load time" - the same
shape as the recipe-level `uses:` mechanism grunk already has, but for plugin-to-plugin
dependencies rather than recipe-to-plugin ones. Until that lands, this requirement is enforced by
nothing but this comment and the one above it in `occt_ad_support.hpp` - get the order wrong and
`to_real()` silently takes its plain-double branch instead of failing loudly (see the
`Standard_Real` registration gap section above for exactly what that looks like).

The **operator overload and friend-free-function generator support** this needed (`+ - * / == <
<=`, plus ADOL-C's own `sin`/`cos`/...) lives in grunk-occt's generator (`codegen/generate.py`),
not grunk-adolc's own copy of anything - see `codegen/README.md`'s "Operator overloads and friend
free functions" for the mechanism, and its own note on the real bug this surfaced (an incorrect
"reversed operand always exists" assumption, caught by regenerating this project's own OCCT config
before it ever reached grunk-adolc). Generator centralization into a shared location (so
grunk-adolc doesn't need its own full copy of `generate.py`) remains a separate, not-yet-done task -
grunk-adolc's own `codegen/` is, for now, a deliberate, temporary duplicate of this one.

## Packaging

`occt_plugin` (and, under `GRUNK_OCCT_ENABLE_AD`, grunk-adolc's own `adtl_plugin`) is a
runtime-loaded grunk plugin - `grunk::plugin::load_native` dlopens it by path, no consuming
application ever `target_link_libraries()`s against it. This shapes what "packaging" means here,
deliberately narrower than grunk's own `install()` (`grunk/CMakeLists.txt`):

- **No CMake package config** (`occt_pluginConfig.cmake`/exported targets, the way grunk's own
  `install()` produces `grunkConfig.cmake` for `find_package(grunk)`) - there's no linkable target
  to export in the first place, so a package config here would be pure ceremony: nothing a
  consumer could meaningfully `find_package()` and `target_link_libraries()` against. The one
  thing a downstream consumer actually needs - where the built `.so`/`.dylib`/`.dll` ended up -
  is exactly what a plain `install(TARGETS occt_plugin LIBRARY/RUNTIME/ARCHIVE DESTINATION ...)`
  already provides, via `GNUInstallDirs`'s own per-platform convention
  (`${CMAKE_INSTALL_LIBDIR}`/`${CMAKE_INSTALL_BINDIR}`).
- **`add_dependencies(occt_plugin adtl_plugin)`** under `GRUNK_OCCT_ENABLE_AD` - found while
  verifying `install` actually works end-to-end, not assumed: `adtl_plugin` (grunk-adolc's own
  target, FetchContent'd in `EXCLUDE_FROM_ALL`) previously only got built because
  `runUnitTests`' own `add_dependencies(runUnitTests adtl_plugin)` happened to pull it in whenever
  `GRUNK_OCCT_TESTS` was left at its default (`ON`). `cmake --install` only installs what was
  actually built - with `GRUNK_OCCT_TESTS=OFF` and `GRUNK_OCCT_ENABLE_AD=ON` together (a real,
  supported combination, just untested until this check), nothing would have forced `adtl_plugin`
  to build, and `install` would have installed `occt_plugin` alone with no error, silently missing
  half of what an AD-enabled consumer needs. Fixed by making `occt_plugin` itself (always part of
  the default `all` target, never excluded) depend on `adtl_plugin`'s build, independent of
  whether tests are enabled.
- **Verified for real**, not just written and assumed correct: `pixi run -e default install`/
  `pixi run -e ad install` both run end-to-end, `pixi run -e default install` installing to
  `build/install/lib/libocct_plugin.so` (Linux path - `RUNTIME`/`ARCHIVE` destinations for
  macOS/Windows are structurally correct per `GNUInstallDirs`'s own documented convention but not
  independently verified on those platforms in this environment), and the full test suite still
  passes afterward in both environments.
- **grunk-adolc gets the identical treatment** (`GNUInstallDirs` + `install(TARGETS adtl_plugin
  ...)`, its own `pixi run install` task) so it's independently installable when built standalone
  too - verified there directly (`libadtl_plugin.so` installs correctly on its own), not only as a
  side effect of grunk-occt's own `GRUNK_OCCT_ENABLE_AD` FetchContent - see its own `DESIGN.md` for
  the mirrored entry.
- **`grunk-adolc` pin bumped from `dd7dac1` to `233516a`** once grunk-adolc's own `install()`
  addition was committed and pushed, to pick it up - matches this same file's own pin-bump policy
  elsewhere ("Bump this pin deliberately, in its own PR, gated by the full test suite - never as a
  side effect of an unrelated change", `CMakeLists.txt`'s own comment on the `grunk` pin). The pin
  bump alone turned out **not to be sufficient** - see the next point, found immediately while
  verifying it.
- **The real remaining bug: `EXCLUDE_FROM_ALL` silently drops a subdirectory's `install()` rules
  too, not just its default-build membership.** After the pin bump, `pixi run -e ad install` was
  still installing `occt_plugin` alone - `adtl_plugin` built correctly (confirmed present under
  `build-ad/_deps/grunk-adolc-build/`) but `build-ad/cmake_install.cmake` had no `include(...)` of
  `_deps/grunk-adolc-build/cmake_install.cmake` at all, even though that file itself had a
  perfectly correct `install(TARGETS adtl_plugin ...)` rule. Root cause: the `grunk-adolc`
  `add_subdirectory(... EXCLUDE_FROM_ALL)` call (originally added only to keep it out of the
  default `all` target) - `EXCLUDE_FROM_ALL` turns out to *also* exclude that subdirectory's own
  install rules from ever being reached by the parent's `cmake --install`, a CMake behavior that's
  easy to miss since it's not really about the `all` target at all. Fixed by dropping
  `EXCLUDE_FROM_ALL` entirely from that one `add_subdirectory` call - safe here specifically
  because `GRUNK_ADOLC_TESTS`/`GRUNK_ADOLC_GENERATE_CODE` are already forced `OFF` just above it,
  so `adtl_plugin` is the *only* target grunk-adolc's own `CMakeLists.txt` defines in this
  configuration; nothing extra ends up in `all` as a result. This also made the earlier
  `add_dependencies(occt_plugin adtl_plugin)` fix (the first, insufficient attempt at this same
  underlying problem) genuinely redundant - removed, since `adtl_plugin` is now unconditionally
  part of `all` under `GRUNK_OCCT_ENABLE_AD` regardless. Verified for real, not just reasoned
  through: a full reconfigure + build + `install` (showing *both* `libocct_plugin.so` and
  `libadtl_plugin.so` in the installer's own output) + the full test suite (43/43) + `run_examples`,
  all green.

## grunk: pre-built dependency, not vendored

This repo used to pull grunk's own *source* in via `FetchContent_Declare(grunk ...)` +
`add_subdirectory`, compiling a private copy of `grunk::core`/`grunk::dynamic`/`grunk::recipe`/
`grunk::plugin` directly into `occt_plugin.so`'s own build. That's gone: `CMakeLists.txt` now does
a plain `find_package(grunk REQUIRED)` against a pre-built, pre-installed grunk (`scripts/
install_grunk.cmake`, run automatically via `pixi run configure`'s own `depends-on`, installing
into `.deps/grunk` - or `.deps/grunk-ad` for the `ad` environment, see below). Directly caused by
tracking down "A related hazard, found outside the AD case" above - see that section for the full
story of what broke and why this fixes it, not repeated here.

**Why a `cmake -P` script, not a shell script**: every other bootstrap in this repo (`scripts/
install_adolc.sh`, `scripts/install_adocct.sh`) is bash - fine for them, since they only ever run
in the `ad` environment, which is Linux-only by design (`pixi.toml`'s own `[feature.ad]`,
`platforms = ["linux-64"]`). `install_grunk` runs in the `default` environment too, which targets
Windows as well (`pixi.toml`'s own top-level `platforms`), and Windows has no bash by default.
`cmake -P scripts/install_grunk.cmake` is a plain CMake script - cmake is already a hard dependency
on every platform this repo targets, so it works identically everywhere without a second,
batch/PowerShell-flavored copy of the same bootstrap logic to keep in sync.

**Two separate installs, one per pixi environment** (`.deps/grunk` for `default`, `.deps/grunk-ad`
for `ad`, via `scripts/install_grunk.cmake`'s own optional `GRUNK_INSTALL_NAME` variable): found
the hard way, not designed in from the start - the first version shared one `.deps/grunk` between
both environments, and CMake's own "cannot generate a safe runtime search path... cycle in the
constraint graph" warning (while configuring the `ad` environment, RPATH entries pointing partly
at `.pixi/envs/default/lib`) was the tell. `default` and `ad` are two independent conda
environments (`.pixi/envs/default`, `.pixi/envs/ad`), each with its own build of Lua/yaml-cpp;
whichever environment's own `install_grunk` task ran *first* would "win", and the other would
silently link against a grunk built against a different environment's own Lua/yaml-cpp than the
one it's actually running in. Giving each environment its own install prefix (and its own
`build/_deps/<name>` scratch checkout, so the two don't collide there either) fixes this at the
root, not just papers over the symptom.

**RPATH** (`CMakeLists.txt`'s own `CMAKE_INSTALL_RPATH_USE_LINK_PATH` + `$ORIGIN`/`@loader_path`/an
explicit `$ENV{CONDA_PREFIX}/lib` entry, mirroring grunk's own identical treatment of itself) is
what lets `occt_plugin.so`/`adtl_plugin.so` find grunk's shared libraries at runtime without
`LD_LIBRARY_PATH` - verified directly via `ldd`/`readelf` with `LD_LIBRARY_PATH` explicitly unset,
on both the build-tree and `cmake --install`ed artifacts, in both the `default` and `ad`
environments. Windows gets the equivalent treatment a different way, since RPATH doesn't exist
there at all: `occt_plugin`'s own `POST_BUILD` step copies `$<TARGET_RUNTIME_DLLS:occt_plugin>`
(everything it transitively depends on as a DLL, grunk's own libraries included) next to itself,
and `runUnitTests` gets the identical treatment for the same reason (`pixi run test` runs
`ctest --test-dir build` straight out of the build tree, before any install step ever runs) - not
independently verified on an actual Windows machine in this environment, but directly modeled on
the same fix grunk found necessary for its own `runUnitTests.exe`/plugin-fixture `.dll`s (see
grunk's own `tests/cpp/CMakeLists.txt`).

**CI**: `.github/workflows/ci.yml`'s `build-and-test` job now has its own "Cache grunk"/"Install
grunk" steps (mirroring the pre-existing ADOL-C/adOCCT pattern in `build-and-test-ad`, keyed on
`scripts/install_grunk.cmake` so a cache hit skips straight past rebuilding grunk from source), and
`build-and-test-ad`'s existing cache step now covers `.deps/grunk-ad` alongside `.deps/adolc`/
`.deps/adocct`.

## sol2 overload-resolution ambiguity under `GRUNK_OCCT_WITH_ADOLC` - the real `tank_model.grr.yml` AD bug

**Fixed, not just worked around**: `examples/tank_model.grr.yml` used to fail under the AD-enabled
`occt_plugin` - one of the dome's side-wedge boolean intersections
(`BRepAlgoAPI_Common`/`BRepAlgoAPI_Cut`) came back empty (`IsDone() == false`, not merely an empty-
but-successful result) where the plain build's own boolean op didn't. This was assumed, in earlier
investigation (before this session), to be a genuine adOCCT numerical/tolerance difference -
plausible on its face (boolean operations are exactly the kind of algorithm sensitive to tiny
floating-point differences), but wrong. The real cause has nothing to do with adOCCT's own
arithmetic at all.

**Root cause**: a sol2 constructor/method overload-resolution ambiguity, specific to
`GRUNK_OCCT_WITH_ADOLC` builds. Every `Standard_Real` parameter's emitted C++ type becomes
`sol::object` under this macro (the AD dual-emission pattern - see "`Standard_Real` / AD-portability
rule" above), a deliberate, necessary choice: `sol::object` is what lets a single lambda accept
either a plain Lua number or an already-AD-tracked value (`occt_plugin::to_real()` tells them apart
at runtime). The problem: `sol::object` also matches *any* Lua value, including a completely
unrelated registered usertype. sol2 tries an overload set's candidates in the order they were
registered and commits to the first one whose parameter count and per-parameter type checks
succeed - and a `sol::object` parameter's type check always succeeds, for anything. So an all-real
overload of some arity (e.g. `BRepPrimAPI_MakeSphere(Standard_Real R, Standard_Real angle1,
Standard_Real angle2)`, three `sol::object` parameters under AD) could silently shadow a
same-arity, genuinely more specific one (`BRepPrimAPI_MakeSphere(gp_Ax2 const&, Standard_Real R,
Standard_Real angle)`) that was only ever meant to apply when the first argument really is an axis -
whichever was registered first wins, regardless of what was actually passed.

The code generator (`codegen/generate.py`) had no awareness of this hazard: overloads were emitted
in whatever order `collect_class_callables`' own libclang cursor-walk encountered them, i.e.
declaration order in the OCCT header - and OCCT itself, unsurprisingly, tends to declare the "just
numbers" constructor before the "positioned by an axis/point" ones. `examples/tank_model.grr.yml`'s
own `dome` sub-recipe's `spherical_segment` calls
`occt.BRepPrimAPI_MakeSphere.new(ax_1, radius, angle1)` (a 3-argument call meaning "axis, radius,
angle") - under AD, this was being silently routed to the 3-`sol::object` `(R, angle1, angle2)`
overload instead, with `R = to_real(ax_1)` (an axis, converted to a number via
`sol::object::as<double>()` on a non-numeric userdata - undefined/implementation-defined, not a
clean failure), `angle1 = to_real(radius)`, `angle2 = to_real(angle1)`. The resulting sphere is a
real, valid, non-null `TopoDS_Shape` (so `wedge:IsNull()` alone never caught it) - just the *wrong*
one, with nothing resembling the intended geometry. Intersecting two such wrongly-constructed wedges
is what actually produced `IsDone() == false`.

**How this was actually found** (worth recording - the debugging path, not just the answer): a
scratch, uncommitted standalone C++ program linking adOCCT directly, reproducing the exact same
wedge construction with the exact same numeric inputs, *succeeded* - immediately ruling out adOCCT's
own algorithm and the exact input values (confirmed bit-identical via hex-formatted output at every
step) as the cause. A scratch Lua-callable self-test function, registered directly in
`occt_plugin.cpp` and doing the identical construction in raw C++ (no sol2 constructor overload
resolution involved at all), *also* succeeded when called from Lua - narrowing the fault to
specifically going through a *registered, overloaded* sol2 constructor. Temporary `fprintf`
instrumentation added directly to each candidate `BRepPrimAPI_MakeSphere` overload's lambda body
(reverted before this fix was committed - see git history if the exact experiment matters later)
confirmed directly which overload actually ran: the 3-`sol::object` one, not the `gp_Ax2`-taking
one, for a call whose first argument plainly *was* a `gp_Ax2`. Manually reordering just that one
constructor's overload registration in the generated file (moving the axis-taking one first) fixed
it outright, which is what turned "a hypothesis" into "a confirmed, minimal, reproducible root
cause" before touching the generator itself.

**The fix**: `ambiguity_safe_overload_order()` in `codegen/generate.py`, applied at every point an
overload set gets turned into a `sol::overload(...)`/`.add_constructors(...)` expression
(constructors, instance methods, static methods/free functions - operators too, though no operator
overload set has actually hit this in practice). A stable sort, keyed on `(arity, count of "real"
kinds)` - within each argument-count group (sol2 already dispatches on count first, so different
counts never compete), a signature with fewer `Standard_Real`-turned-`sol::object` parameters (i.e.
more parameters sol2 can genuinely type-check against a concrete usertype) is tried before one with
more. Applied unconditionally, not just under `GRUNK_OCCT_WITH_ADOLC` - in the plain build,
`Standard_Real` is genuinely `double`, so sol2 can already disambiguate every one of these cases by
real type-checking regardless of order, making the reorder a no-op there rather than something that
needs its own separate, conditional code path.

**Scope**: not a one-off `BRepPrimAPI_MakeSphere` quirk. Regenerating with this fix touched 11 of
the 15 generated modules (378 changed lines total, confirmed via diff to be pure reordering - zero
content changes, same lambdas, same bodies, only registration order) - `BRepPrimAPI_MakeCone`'s own
4-argument overloads (`(R1, R2, H, angle)` vs `(gp_Ax2 const&, R1, R2, H)`) had the exact same
latent ambiguity, and `gp.cpp` alone had 238 changed lines. This was a real, previously-undiscovered
bug across a large fraction of the AD-enabled binding surface - not something Step 10's own #285/
#288 workaround-removal verification could ever have caught (that only exercised the *default*,
non-AD environment's test suite; the AD environment's own 43 tests happened to never hit an affected
overload set with an ambiguous argument at the ambiguous position).

**Verified**: `pixi run -e default test` (42/42) and `pixi run -e ad test` (43/43) both still pass
after regeneration - confirming the reordering really is behaviorally inert everywhere it isn't
needed. `examples/tank_model.grr.yml` now runs to completion under the AD environment with **no**
fallback/error path taken at all (`pixi run -e ad run_examples` - no "KNOWN ISSUE" message, since
that whole code path was removed from `run_example.py`; `occt_helpers.lua`'s own `compound()`
`IsNull()` guard - general defensive code, not specific to this bug - never triggers for this
recipe anymore either).
