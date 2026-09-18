<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# codegen

A libclang-based generator that parses OCCT headers (driven by `config.yml`) and emits
grunk plugin registration code targeting `register_type`/`add_constructors`/
`add_member_function` (`generated/<module>.{hpp,cpp}`, checked into git - see
`../DESIGN.md`'s "Generated-code policy"). Successor to legacy grocc's
`occt_generator.py`, rebuilt from scratch against the new API (see the migration
plan's Step 2).

## Running it

```
pixi run generate_code   # requires -D GRUNK_OCCT_GENERATE_CODE=ON at configure time
```

or directly:

```
pixi run python codegen/generate.py --occt-include-dir .pixi/envs/default/include/opencascade --output-dir generated
```

Review the stderr summary (accepted/rejected/blacklisted counts, and every rejection's
reason) after every run - it's the primary way to notice a symbol that needs a
hand-written escape hatch, or a blacklist entry that's no longer warranted.

## What it does (Step 2 MVP scope)

- **One class per header**, matched by filename stem (`gp_Pnt.hxx` -> class `gp_Pnt`)
  - true for every OCCT `gp_*` header; a module with a different convention will need
  this assumption revisited.
- **Public constructors and instance methods only.**
- A parameter/return type is accepted only if it is:
  - `Standard_Real` **by value or const-reference** (gets the
    `#ifdef GRUNK_OCCT_WITH_ADOLC`-gated dual-emission form - see `../DESIGN.md`'s
    "Standard_Real / AD-portability rule"),
  - `Standard_Integer`/`Standard_Boolean` **by value or const-reference** (passed
    through as-is, no AD concern - these aren't the type `adOCCT` retargets),
  - a **value, const-reference, or non-const-reference** of another class or
    `Handle(X)` *also being registered in the same generator run* (i.e. one of the
    config's own header stems, across every module - see "Type registration spans
    the whole config" below; a type from a module that's genuinely never wrapped is
    rejected, not silently miscompiled),
  - `NCollection_Array1<X>`/`NCollection_Array2<X>` **by value or const-reference,
    parameters only** (see "NCollection_Array1/2 support" below), or
  - `void` (return type only).
- **Non-const-reference ("out") parameters**: accepted, but only when the mutated
  type is itself a registered object/`Handle` type (e.g. `Geom_Curve::D0(Standard_Real
  U, gp_Pnt& P)`) - **not** a corner case to work around, this is a common, idiomatic
  OCCT pattern (`D0`/`D1`/`D2`/`D3`, `Coord`'s 3-out-param overload, ...) and Lua/sol2
  handles it natively: the existing userdata passed in gets mutated in place, exactly
  like OOP mutation already works in Lua. What's **not** supported is a non-const
  reference to a *primitive* (`Standard_Real&`/`Standard_Integer&`/`Standard_Boolean&`)
  - Lua has no mutable-number-by-reference concept, there is no userdata to mutate for
    a bare number (e.g. `gp_Trsf::Transforms(Standard_Real&, gp_XYZ&)` stays rejected
    on the *first* parameter alone, independent of `gp_XYZ` also being unregistered).
  Consuming a mutate-through-reference method from a **recipe** (as opposed to the
  raw/undecorated `create_env()` this project's tests use so far) is where grunk's own
  "module" mechanism becomes relevant - see `../DESIGN.md`'s note.
- Anything else is **rejected** (recorded with a reason, never silently dropped):
  unregistered class types (`gp_XYZ`, `gp_Ax2`, `gp_Quaternion`, `gp_Mat`,
  `gp_TrsfForm`, ...), `Standard_OStream&`/`Standard_SStream`, and operators (skipped
  by name prefix - see "Known gaps").
- **Constructor emission mode is one of three, decided per class** (see
  `../DESIGN.md`): `"value"` (plain `return T(args...);` - the default, used for
  simple value types like `gp_Pnt`/`TopoDS_Shape`), `"handle"` (heap-allocate +
  `opencascade::handle<T>`-wrap, for `Standard_Transient`-derived classes - see
  below), or `"unique_ptr"` (`return std::make_unique<T>(args...);`, for any class -
  or any class in its ancestor chain - that declares an **explicit destructor**
  without `"handle"` mode applying: found the hard way when `BRepAlgoAPI_Cut`
  segfaulted at program exit under the naive `"value"` emission - see `../DESIGN.md`'s
  "Copying an algorithm/builder class segfaults"). This is a conservative heuristic,
  not a proof of unsafety either way - a class with no explicit destructor anywhere
  in its chain is assumed safely copyable.
- **Overloaded methods** (same Lua name, 2+ accepted C++ overloads - e.g.
  `gp_Trsf::SetTranslation`) are merged into one `sol::overload(...)` call
  automatically. Every emitted callable is a **lambda**, never a bare
  `&Type::Method` - this sidesteps the overload-address-ambiguity gotcha filed as
  [grunk-dev/grunk#280](https://github.com/grunk-dev/grunk/issues/280) entirely,
  since calling an overloaded method with concrete argument types is never
  ambiguous, only *taking its address* is.
- **`Standard_Transient`/`Handle(T)` support** (Step 3): a class whose ancestor chain
  includes `Standard_Transient` gets its constructors emitted as heap-allocating
  factories returning `opencascade::handle<T>` (`new T(args...)`, never a by-value
  `T`) instead of plain value constructors, and its full ancestor chain emitted as
  `.add_bases<Base1, Base2, ...>()` (needed for Lua colon-call dispatch to reach a
  method registered on a *base* class, and for virtual dispatch through it to reach a
  derived override - see `codegen/config.yml`'s `geom` module and
  `tests/test_geom.cpp`). `Handle(X)` (i.e. `opencascade::handle<X>`) parameters/
  returns are accepted wherever `X` is itself a registered type, bridged via
  `../src/occt_sol_traits.hpp`'s `sol::unique_usertype_traits` specialization
  (included automatically when a module has any Transient-derived class).
  **Multiple inheritance is not handled** - only the first `CXX_BASE_SPECIFIER` per
  class is followed; fine for every OCCT class this generator has targeted so far
  (single inheritance throughout), but would need extending if that changes.
- **`NCollection_Array1<X>`/`NCollection_Array2<X>` support (Step 5)**: a
  by-value or const-reference parameter whose type is (or is a typedef for) one of
  these templates - e.g. `TColgp_Array1OfPnt` (`NCollection_Array1<gp_Pnt>`),
  `TColStd_Array1OfReal` (`NCollection_Array1<double>`) - is accepted whenever `X`
  is itself a supported element type (a registered class, or `double`/`float`/
  `Standard_Integer`/`Standard_Boolean`), and rendered as `std::vector<X> const&`
  (array1) or `std::vector<X> const&` plus two extra `Standard_Integer rows, cols`
  parameters (array2 - see `../DESIGN.md`'s "NCollection_Array2 Lua-side shape" for
  why flat + dims, not nested vectors), converted at the call site via
  `../src/collections.hpp`'s `to_array1`/`to_array2`. Detecting this requires
  libclang's *canonical* (typedef-resolved) type - `TColgp_Array1OfPnt`'s own
  spelling never mentions `NCollection_Array1` at all - which is why `Param` also
  carries a `canonical` field (`Param.kind()`'s `ARRAY1_PATTERN`/`ARRAY2_PATTERN`
  match against it, not the ordinary `spelling`). This is what lets e.g.
  `Geom_BezierCurve`'s own constructors (`(TColgp_Array1OfPnt const&)` and
  `(TColgp_Array1OfPnt const&, TColStd_Array1OfReal const&)`) auto-generate as
  ordinary `add_constructors(...)` overloads, with no hand-written escape hatch.
  AD-portability's `#ifdef GRUNK_OCCT_WITH_ADOLC` dual-emission is **not** extended
  to array elements (a "real" array element is always plain `double`, regardless of
  build) - out of scope until Step 7 actually needs it.
  - **Parameter-only, deliberately**: `Param.kind()` only recognizes an array
    canonical type when `self.name` is non-empty, which is true for every genuine
    parameter but never for the synthetic `Param` `Callable.rejection_reason` builds
    to check a *return* type - so a method/constructor **returning** an array type
    (`Geom_BezierCurve::Poles() const -> const TColgp_Array1OfPnt&`, `Weights()`)
    stays rejected, same as a **mutable-reference** array out-param
    (`Poles(TColgp_Array1OfPnt& P)`) - unlike a registered object/`Handle`'s own
    non-const-reference out-param support above, `to_array1`/`from_array1` don't
    have an in-place-mutation story, and no concrete binding has forced a decision
    on either yet. See `../DESIGN.md`'s own note on this boundary.
- **Type registration spans the whole config, not just one module**: `main()`
  computes `registered_types` as the union of every module's class names *before*
  processing any of them, so a later module's callables can reference an earlier
  module's types (e.g. `geom`'s methods take/return `gp_Pnt`/`gp_Vec`/`gp_Trsf`) -
  matches legacy grocc's cross-module type visibility.
- **`extra_includes`** (a module's config, optional, revived from legacy grocc's
  `config.yml.in` - Step 6): extra `#include <...>` lines added to the generated
  `.cpp` (never the `.hpp`, and never a source of new registrable classes - these
  headers are *not* walked for callables) for a type this module's callables use
  only by reference/pointer, where this module's *own* headers therefore only
  forward-declare it (e.g. `BRepBndLib.hxx`'s `class TopoDS_Shape;`, since a
  `const TopoDS_Shape&` parameter needs no more from `BRepBndLib.hxx`'s own point
  of view). sol2's usertype binding needs the type's *complete* definition in
  *this* translation unit regardless of it already being fully defined and
  registered by a different module's own `.cpp` (`generated/topods.cpp` for
  `TopoDS_Shape`) - omitting it fails with a real "invalid use of incomplete type"
  compile error (found on `bnd`, the first module needing it - see
  `codegen/config.yml`'s own comment there).
- **`automagic_flags::none`** is applied to every registered type unconditionally (see
  `../DESIGN.md`'s policy) - the generator does not (yet) have a mechanism for opting
  a specific type into fuller automagic.
- **Blacklist**: `ClassName::MethodName` skips one method (all its overloads); a bare
  `ClassName` skips a whole class. Each entry in `config.yml` should carry an inline
  comment explaining why - re-derive the reason fresh per the migration plan's Step 6
  policy, don't copy a legacy grocc blacklist reason without checking it still applies.
- **Static-only "namespace classes"** (Step 4 spike, see `../DESIGN.md`): OCCT
  expresses this concept two different ways, both handled: a genuine C++ `namespace`
  with free functions (`TopoDS.hxx`'s `namespace TopoDS { const TopoDS_Face&
  Face(const TopoDS_Shape&); ... }`), and an ordinary `class` whose methods happen to
  all be `static` (`BRepTools`). Both are detected automatically per header (try
  `CLASS_DECL`/`STRUCT_DECL` first, fall back to a `NAMESPACE` cursor - no config
  schema change needed) and rendered identically: no "self", called fully-qualified
  (`ClassOrNamespace::Method(args)`). **Chosen naming convention: flattened**
  (`occt.ClassOrNamespace_Method(args)`, e.g. `occt.BRepTools_Write(shape, file)`) -
  *not* a nested Lua table (`occt.BRepTools.Write(...)`). The nested-table option was
  seriously considered (and works fine for a genuine class's statics, via
  `register_type<T>` with zero constructors and `add_member_function` for each
  static), but **cannot work for a real C++ namespace at all** - `TopoDS` has no
  type to `register_type<>()` on - so the flattened convention is the only one that
  generalizes to both cases uniformly, which settled the spike.
  - **Naming collision, handled specially**: `TopoDS`'s own downcast helpers
    (`TopoDS::Face`/`Edge`/`Vertex`/...) each compose to the exact same name as the
    leaf type they cast *to* (`TopoDS_Face`, already a registered class) - not a
    hypothetical, every single one of `TopoDS.hxx`'s functions collides this way.
    Registering `occt.TopoDS_Face` as a plain free function would clobber the
    `TopoDS_Face` usertype's own table. Detected (composed name already in this
    module's `registered_types`) and redirected: attached as that type's own
    `.DownCast(shape)` member instead (`occt.TopoDS_Face.DownCast(shape)`) - mirrors
    Step 3's `Handle(T)::DownCast` naming (`src/geom/geom_extras.hpp`) for one
    consistent Lua-side idiom regardless of which OCCT downcast mechanism is
    underneath. **Only detected within a single module's own class set** - a
    collision between a static/namespace function in one module and a class
    registered in a *different* module wouldn't be caught (not yet hit in practice).
- **No default-argument expansion.** Legacy grocc's generator expanded a method with
  N trailing default arguments into N+1 overloads; this generator does not (a method
  with a default argument is still fully accepted/emitted, it just always requires
  every argument explicitly from Lua - no arity-reduced overloads are synthesized).
- **Toolchain include-path discovery is GCC/Clang-only** (`system_cxx_include_dirs()`
  probes `$CXX`/`c++` with `-E -x c++ -v -`, parsing the "#include <...> search starts
  here" block - this doesn't work for MSVC). Not a blocker today since codegen is
  opt-in and its output is checked into git (regeneration only needs to happen on
  whatever platform a maintainer actually runs it from), but worth fixing before
  relying on Windows-run regeneration.
- **One clang parse per header** (no shared "compilation database"/precompiled
  headers) - fine at this scale (5 headers), may need revisiting for performance once
  Step 6 covers dozens of modules.
