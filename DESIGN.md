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
