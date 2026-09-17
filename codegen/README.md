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
  - `Standard_Real` (gets the `#ifdef GRUNK_OCCT_WITH_ADOLC`-gated dual-emission form -
    see `../DESIGN.md`'s "Standard_Real / AD-portability rule"),
  - `Standard_Integer`/`Standard_Boolean` (passed through as-is, no AD concern - these
    aren't the type `adOCCT` retargets),
  - a **value or const-reference** of another class *also being registered in the same
    generator run* (i.e. one of the module's own header stems - a type from a
    different, not-yet-wrapped module is rejected, not silently miscompiled), or
  - `void` (return type only).
- Anything else is **rejected** (recorded with a reason, never silently dropped):
  non-const-reference out-parameters (`Standard_Real&`, `gp_XYZ&`, ...), unregistered
  class types (`gp_XYZ`, `gp_Ax2`, `gp_Quaternion`, `gp_Mat`, `gp_TrsfForm`, ...),
  `Standard_OStream&`/`Standard_SStream`, operators (skipped by name prefix - see
  "Known gaps"), and static methods.
- **Overloaded methods** (same Lua name, 2+ accepted C++ overloads - e.g.
  `gp_Trsf::SetTranslation`) are merged into one `sol::overload(...)` call
  automatically. Every emitted callable is a **lambda**, never a bare
  `&Type::Method` - this sidesteps the overload-address-ambiguity gotcha filed as
  [grunk-dev/grunk#280](https://github.com/grunk-dev/grunk/issues/280) entirely,
  since calling an overloaded method with concrete argument types is never
  ambiguous, only *taking its address* is.
- **`automagic_flags::none`** is applied to every registered type unconditionally (see
  `../DESIGN.md`'s policy) - the generator does not (yet) have a mechanism for opting
  a specific type into fuller automagic.
- **Blacklist**: `ClassName::MethodName` skips one method (all its overloads); a bare
  `ClassName` skips a whole class. Each entry in `config.yml` should carry an inline
  comment explaining why - re-derive the reason fresh per the migration plan's Step 6
  policy, don't copy a legacy grocc blacklist reason without checking it still applies.

## Known gaps (deliberately out of scope for the Step 2 MVP)

- **Operators are skipped entirely** (any method whose name starts with `operator`) -
  no attempt yet to map `operator+`/`operator*`/etc. to sol2 metamethods.
- **Static methods are skipped.** Needed once a module has OCCT's static-only
  "namespace classes" (`TopoDS`, `BRepTools`, `GC`, ...) - see the migration plan's
  Step 4 spike on that convention; the generator doesn't implement whichever
  convention is chosen yet.
- **No default-argument expansion.** Legacy grocc's generator expanded a method with
  N trailing default arguments into N+1 overloads; this generator does not (a method
  with a default argument is still fully accepted/emitted, it just always requires
  every argument explicitly from Lua - no arity-reduced overloads are synthesized).
- **`Handle(T)`/`Standard_Transient` detection is not implemented yet** - that's Step 3
  (`is_derived(cursor, "Standard_Transient")`, per the migration plan), along with
  `.add_bases<...>()` for inheritance chains.
- **Toolchain include-path discovery is GCC/Clang-only** (`system_cxx_include_dirs()`
  probes `$CXX`/`c++` with `-E -x c++ -v -`, parsing the "#include <...> search starts
  here" block - this doesn't work for MSVC). Not a blocker today since codegen is
  opt-in and its output is checked into git (regeneration only needs to happen on
  whatever platform a maintainer actually runs it from), but worth fixing before
  relying on Windows-run regeneration.
- **One clang parse per header** (no shared "compilation database"/precompiled
  headers) - fine at this scale (5 headers), may need revisiting for performance once
  Step 6 covers dozens of modules.
