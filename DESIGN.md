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
  (currently `c16f519888eae62564145b1ba4c025c1e6dcc6c2`), not a tag — grunk's plugin interface and
  0.5.0 rewrite are only on `main`, untagged as of this writing. Bump the pin deliberately, in its
  own PR, gated by the full grunk-occt test suite - never as a side effect of an unrelated change.
  Latest bump (from `1ad286bfdc937ff75735c20db4358eb282f48c02`) picks up
  [grunk-dev/grunk#284](https://github.com/grunk-dev/grunk/pull/284) (`GRUNK_PLUGIN_INFO`, an
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

The test harness (`tests/test_*.cpp`, one file per module, growing over subsequent steps) loads
the plugin via `grunk::plugin::load_native` and drives it through **raw Lua via
`grunk::state::create_env()`** directly - not through `grunk::recipe`'s YAML+Lua layer. This keeps
plugin-binding bugs isolated from the separately-young recipe layer. A `grunk::recipe`-based
end-to-end test is added later (once several OCCT modules exist), to validate realistic recipe
usage - see Step 6.5 of the migration plan (porting legacy plugin-using example recipes) for that
milestone.

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

## Copying an algorithm/builder class segfaults - fixed via std::unique_ptr<T>

Found in Step 4b (the boolean-op smoke test): registering `BRepAlgoAPI_Cut`'s
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

This is **not** the `Standard_Transient`/`Handle(T)` case Step 3 solved -
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

Initially (Step 3, first pass) the generator rejected every non-const-reference
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
that bridge yet (Step 0 deliberately deferred `grunk::recipe`-based testing), but it's
the mechanism to reach for once out-param-heavy OCCT calls need to appear inside an
actual recipe, not just this project's own `create_env()`-based tests.

## Null Handle arguments segfault (found in Step 3, filed as grunk-dev/grunk#281)

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
expect it in Step 6 modules with more heavily overloaded methods.

## `automagic_flags` policy

Default to `sol::automagic_flags::none` for every OCCT type (OCCT types generally lack
`operator==`/`operator<`, which sol2's default "automagic" registration wants). A specific type is
only opted into fuller automagic after explicit human confirmation it's safe (e.g. simple value
types with no dangerous operator surface, following the `gp_Pnt` precedent in grunk's
`cad_autodiff` example). This is a generator-wide rule, not a per-type ad hoc choice.

## Code generator (Step 2)

`codegen/generate.py` (libclang-based, see `codegen/README.md` for full design/scope)
targets the `gp` module and reproduces - and exceeds - Step 1's hand-written
registrations: 166 constructors/methods accepted automatically (vs. ~15 hand-picked),
48 rejected with a recorded reason each (unregistered types like `gp_XYZ`/`gp_Ax2`,
non-const-reference out-params, `Standard_OStream&`, ...), 1 deliberately blacklisted
(`gp_Pnt::BaryCenter`, the Step 2 checkpoint proving the blacklist mechanism). The
hand-written `src/gp/gp_registration.hpp` was deleted once `generated/gp.cpp` passed
Step 1's tests unchanged - generated and hand-written versions of the same module must
never coexist/drift.

The AD-portability macro name is **`GRUNK_OCCT_WITH_ADOLC`** (not defined anywhere yet
- only the plain branch of every `#ifdef`-gated dual form is compiled today). The
generator bakes this in automatically for every `Standard_Real` parameter; hand-written
escape-hatch code (Steps 4a/6) should use the same macro name for consistency.

## `Standard_Real` / AD-portability rule

Every constructor/function parameter of type `Standard_Real` is emitted (by hand today, by the
generator from Step 2 onward) as an `#ifdef`-gated pair: a plain-fundamental-type overload for
ordinary builds, and a `sol::object`-based mixed-conversion overload for a future AD-enabled build
(mirroring `geoml_registration.hpp`'s `gp_Pnt` constructor in grunk's `cad_autodiff` example). This
makes the deferred AD milestone a recompile against an AD-enabled OCCT build, not a source rewrite.

## Collections: `std::vector<T>` <-> `NCollection_Array1/2` (Step 5)

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
  a concrete consumer forces the decision, per the same "don't design for
  hypothetical requirements" principle as the migration plan's own Step-by-step
  scoping.

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

Found while validating Step 5 (unrelated to it - reproduces identically on
unmodified `main`, confirmed via `git stash`): `tests/test_geom.cpp`'s
`NullHandleSurfacesAsNil` (`downcast_to_geom_line(Handle(Geom_Geometry) const&)`
called with a genuine `Handle(Geom_Line)` value, upcast via `add_bases`) **fails**
when grunk-occt is built with `CMAKE_BUILD_TYPE=Debug`, with sol2's own "unrecognized
userdata (not pushed by sol?)" error - the exact same failure mode as the
never-fully-explained synthetic `Handle<T>` repro documented in
`docs/step5-investigation/`, except this reproduces with a **real OCCT type**, gated
purely by Debug vs. Release.

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
same category as grunk-dev/grunk#281, and orthogonal to Step 5's own deliverable
(Step 5's own new test, `BezierCurveFromVectorOfPoints`, passes in both Debug and
Release - this is exclusively about the pre-existing `downcast_to_geom_line` case).
grunk-occt's only currently-tested/supported build configuration is Release (matches
CI); a Debug build should be understood as not fully validated for `Handle(T)`
upcasting until this is root-caused upstream in sol2/grunk. Filed as
[grunk-dev/grunk#283](https://github.com/grunk-dev/grunk/issues/283).

## Step 6 module expansion sweep

One module per PR, each with its own smoke test plus the full accumulated
regression suite (see the migration plan's own Step 6 policy) - no blacklist
entries carried over from legacy grocc's `config.yml.in` without re-deriving the
reason fresh (Step 0.5's policy); so far every module has needed none of its own.

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
  the generator's AST walk, same "missing implicit constructors" category the
  migration plan's own reference material names this exact class under) - added via
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
  real rejection). A plain value type, buildable from a `gp_Trsf` (registered
  since Step 1).
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
  at Windows link time, not a toolchain/generator artifact - re-derived fresh per
  Step 0.5 policy rather than carried forward uncritically, but kept since the
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
    Step 4b smoke test's own cylinder (not the box - a box's sharp corners were
    tried too before isolating the real cause, a closed vs. non-closed input, not
    corner geometry).
- **`geomapi`** (`GeomAPI_*`, whole-module wildcard): legacy grocc's own
  `config.yml.in` wildcarded this whole module too, with one blacklist entry
  (`GeomAPI_Interpolate::ClearTangents` - legacy's own comment notes it's a Windows
  link error, the definition apparently missing from OCCT's own
  `GeomAPI_Interpolate.cxx`); re-derived fresh per Step 6 policy and kept, same
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
    repo itself so other plugins can reuse it (see the migration plan's own
    "long-term toolkit... explicitly out of scope" decision, already deferred once)
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
    Step 5's `NCollection_Array2<gp_Pnt>` conversion), no `Handle(Geom_*)` parameter
    involved.
- **`brepfilletapi`** (`BRepFilletAPI_*`, whole-module wildcard): legacy grocc's own
  `config.yml.in` wildcarded this whole module too, with **no** blacklist entries at
  all - re-derived fresh per Step 6 policy (verified, not just carried forward) and
  confirmed to still need none. 106 accepted, 30 rejected (unregistered enums/
  `Handle(Law_Function)`/`Handle(TopOpeBRepBuild_HBuilder)`, `TopTools_ListOfShape`/
  `TopTools_SequenceOfShape` returns, mutable `Standard_Real&` out-params - same
  categories as prior modules).
  - `BRepFilletAPI_MakeFillet` has **zero** accepted constructors: its only real
    constructor takes a trailing `ChFi3d_FilletShape` with a default value (at
    this point in the project, the generator didn't yet expand default arguments -
    **superseded by Step 6.5's "Default-argument expansion" section below**, which
    gives it a real constructor), so it's rejected outright - same category as
    `geomapi`'s own `GeomAPI_PointsToBSpline` (also blocked by a trailing
    `GeomAbs_Shape` default at the time, also fixed by that same section). It
    stays registered (its other members are still real, valid C++) but isn't
    instantiable from Lua today.
  - `BRepFilletAPI_MakeChamfer(const TopoDS_Shape&)` has no such trailing default
    enum, so it's the module's actual smoke-test candidate. Its `Add(Dis, E)` needs
    a `TopoDS_Edge` - extended `src/topexp_extras.hpp` with `occt.first_edge(shape)`,
    symmetric with the existing `first_face`. `tests/test_brepfilletapi.cpp`'s
    `ChamferOneEdgeOfBox` chamfers one edge of the Step 4b box and passed first try.
  - This was the last module from the migration plan's own Step 6 list (`Bnd`,
    `BRep`, `BRepTools`, `BRepOffsetAPI`, `Geom`, `GeomAPI`, `BRepFilletAPI`, `GC`,
    `TopLoc`) - Step 6's module expansion sweep is now complete; Step 6.5 (porting
    legacy plugin-using example recipes) is next.

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
added in later Step 6 sweeps. The generated unity file is a build artifact (like any
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
needing ~12-13GB. All 37 tests and both example recipes (`pixi run run_examples`)
still pass unchanged. `mode: unity` remains available and was verified to still
produce the exact same manifest (`*`) and unity-build behavior as before, for anyone
who wants the original one-invocation build back.

## Generator redesign: enums, multi-class-per-header, wildcard headers (Step 6)

Prompted directly by real project needs surfacing during Step 6: the generator
needed to support binding C++ enums (many rejected callables return/take one -
`GeomAbs_Shape`, `TopAbs_Orientation`, ...), the Step 2 MVP's "one class per header,
named for the header's filename" assumption didn't hold for every OCCT header (a
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
legacy grocc's own convention), plus two blacklist entries re-derived fresh per Step
6's policy (`gp_Pnt::BaryCenter`, the pre-existing Step 2 checkpoint;
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

## Step 6.5: example recipes, and a real gap between recipe-level and module-level dynamic dispatch

`examples/cavity_model.grr.yml` and `examples/tank_model.grr.yml` (see
`examples/README.md`) port legacy grocc's own recipes of the same name to the new
`grunk::recipe` YAML+Lua format, sharing one small pure-Lua grunk plugin
(`examples/plugins/occt_helpers.lua`, declared explicitly in each recipe/
sub-recipe's own `uses:` block - see `examples/README.md`'s "Why a plugin, not a
shared module" for why it's a plugin rather than a module registered ambiently by
the runner) loaded by `examples/run_example.py` (grunk's Python bindings - no
bundled `ctest` integration coverage for this script, since building grunk's
Python bindings isn't currently part of `grunk-occt`'s own CMake/pixi setup).
Getting there needed:

- **Widening `brepbuilderapi`/`brepprimapi`/`brepalgoapi`/`gc` to whole-module
  wildcards** (matching legacy grocc's own config, same pattern as `brepoffsetapi`/
  `geomapi`/`brepfilletapi` before them) - the two recipes need `MakeFace`/
  `MakePolygon`/`MakeWire`/`MakeEdge`/`Transform`, `MakeRevol`/`MakeHalfSpace`/
  `MakeSphere`/`MakePrism`, `Fuse`/`Common`, and `GC_MakeArcOfCircle`, none of which
  the Step 4b hand-picked subsets included. Needed a new generator feature to get
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

### Recipe-level dynamic dispatch does not walk `add_bases<...>()` - module-level dispatch does

The single most surprising discovery of this milestone, found the hard way writing
`cavity_model.grr.yml`'s `cylinder` sub-recipe:

```lua
-- fails: "Feature: type "occt.BRepPrimAPI_MakeCylinder" has no member "Shape" -
--         use :as(Type) explicitly if this is intentional."
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

Every `BRepPrimAPI_Make*`/`BRepAlgoAPI_*`/`BRepBuilderAPI_Transform` call in both
ported recipes needs this `:as(occt.BRepBuilderAPI_MakeShape):Shape()` form
whenever it appears directly in a recipe's own `steps:`/`recipes:` (never inside a
module - see below).

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

**A related, narrower gotcha in the same area**: a module/plugin parameter that is
a *table* of features (e.g. `occt_helpers.polygon_face(points)`, `points` built as
`{p0, p1, ...}` directly in a recipe's own `steps:`) only has its *top-level*
unwrapping done automatically at the call boundary - the table itself arrives as a
plain Lua table, but each individual element inside it is still the original
`DynamicFeature`:

```lua
-- fails: "no matching function call takes this number of arguments and the
--         specified types" - points[i] is a DynamicFeature, not a raw gp_Pnt.
mp:Add(points[i])

-- works: DynamicFeature::value() unwraps it to the raw grunk::object first.
mp:Add(points[i]:value())
```

`polygon_face`/`wire_face`/`compound` (`occt_helpers.lua`) all need this
`:value()` call on each table element for exactly this reason; a plain top-level
parameter (`shape`, `vec`, `axis`, ...) never needs it.

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

**grunk-occt follow-up reminder, once #285 lands**: revisit
`examples/cavity_model.grr.yml`/`examples/tank_model.grr.yml`/`examples/plugins/
occt_helpers.lua` and drop the now-unnecessary
`:as(occt.BRepBuilderAPI_MakeShape):Shape()`-style casts sprinkled through both -
they'll still work (an explicit `:as()` never becomes *wrong*, just redundant once
`add_bases<...>()` is walked automatically), but the plain `:Shape()` form will be
simpler and match every other colon-call in these recipes. Not urgent, not
blocking anything - purely a cleanup once the upstream fix is available.

Not filed as a grunk issue - both behaviors are grunk's own documented, deliberate
design (see `state.hpp`'s own doc comments on `Feature.__index`/`DynamicFeature::
as`), not bugs; this section exists so the next recipe written against `occt_plugin`
doesn't have to rediscover it from a cryptic runtime error.

### `fuel_D150.grr`/`fuelsystem.grr.yml` - not ported

See `examples/README.md`'s own note: both are large (1800+ line), auto-exported
parameter dumps (already characterized that way in
`reference/legacy-recipes/README.md`, from before this milestone started), not
hand-authored recipes - a poor fit for hand-porting, and explicitly called out as
"not useful as documentation" rather than silently dropped. `cavity_model.grr.yml`/
`tank_model.grr.yml` already exercise every module either fuel recipe would add
(`gp`, `geom`/`gc`, `topods`, `brep`, `brepbuilderapi`, `brepprimapi`,
`brepalgoapi`, `message_progress`) plus sub-recipes 3 levels deep and a shared
module - the documentation/integration-test goals of this milestone are already
met without them.

### Default-argument expansion

Writing the example recipes kept running into the generator's own all-or-nothing
handling of a C++ default value: `BRepAlgoAPI_Cut(S1, S2, theRange =
Message_ProgressRange())` needed a real `Message_ProgressRange` from Lua every
single time (the `message_progress` module - Step 6 - exists purely because of
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
