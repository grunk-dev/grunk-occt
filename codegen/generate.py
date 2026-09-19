#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""grunk-occt's code generator: parses OCCT headers (via libclang) driven by a
declarative config (codegen/config.yml) and emits grunk plugin registration code
(generated/<module>.cpp) targeting grunk's register_type/add_constructors/
add_member_function API.

See codegen/README.md for the full design (what's accepted, what's rejected and why,
known gaps) - this is a deliberately narrow MVP (Step 2 of the migration plan):
constructors/instance methods only, and only a small allowlist of "simple" parameter/
return types can be bound generically. Anything else is silently skipped and reported
in the summary printed at the end of a run - review that output when adding a new
module, since it's the primary way to discover what needs a hand-written escape hatch.

A module's `headers` list accepts glob patterns (e.g. "gp_*.hxx") as well as literal
filenames, expanded against --occt-include-dir - see expand_headers(). Each matched
header is scanned for every class/struct/enum/namespace *directly* declared in it (not
just one matching the filename stem, and not anything nested or only transitively
included) - see discover_entities().
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

import clang.cindex as cindex
import yaml


def system_cxx_include_dirs() -> list[str]:
    """libclang (unlike invoking `clang++`/`g++` directly) doesn't auto-discover the
    system C++ toolchain's own header search path (type_traits, utility, ...) - probe
    the actual compiler (see CMAKE_CXX_COMPILER/$CXX, falling back to "c++") for it,
    the same way `c++ -E -x c++ -v -` reports it. Linux/macOS only for now (MSVC
    doesn't support this invocation) - see codegen/README.md's "Known gaps"; not a
    blocker since codegen is opt-in and generated/ output is checked into git."""
    compiler = os.environ.get("CXX", "c++")
    try:
        proc = subprocess.run(
            [compiler, "-E", "-x", "c++", "-v", "-"],
            input="", capture_output=True, text=True, check=False,
        )
    except FileNotFoundError:
        return []
    output = proc.stderr
    match = re.search(r"#include <\.\.\.> search starts here:\n(.*?)\nEnd of search list\.", output, re.S)
    if not match:
        return []
    return [line.strip() for line in match.group(1).splitlines() if line.strip()]

def expand_headers(patterns: list[str], occt_include_dir: Path) -> tuple[list[str], set[str]]:
    """Expands a module's `headers` list: a literal filename is kept as-is (and must
    exist), a glob pattern (contains any of *?[) is matched against
    occt_include_dir and expanded to every matching filename, sorted for a
    deterministic, reviewable diff. Revives legacy grocc's own config.yml.in
    convention (`gp_*.hxx`) - our own config previously enumerated every header by
    hand, which both undersold how many OCCT types are one glob away and made
    growing a module tedious. Deduplicates across all patterns in the list (in case
    two overlap) while preserving first-seen order.

    Returns (headers, glob_matched) - glob_matched is the subset that came from a
    wildcard rather than being named explicitly, which process_module uses to
    decide how strict to be about a header defining nothing this generator cares
    about (see its own docstring): a hand-picked literal header with zero
    class/struct/enum/namespace definitions is almost certainly a typo/mistake and
    stays a hard error, but a glob is *expected* to occasionally sweep in a header
    that doesn't declare a real type at all - e.g. gp_*.hxx matching gp_Vec2f.hxx,
    which is only `typedef NCollection_Vec2<Standard_ShortReal> gp_Vec2f;` - which
    should be skipped quietly rather than failing the whole run.

    Known hazard (see DESIGN.md): a prefix-style glob can accidentally sweep in an
    unrelated header family with the same filename prefix (e.g. `TopoDS*.hxx` would
    also match `TopoDSToStep_*.hxx`, a completely different OCCT package) - the
    blacklist mechanism (a bare ClassName) is the way to exclude something an
    over-eager glob picked up, same as excluding anything else this generator
    shouldn't bind."""
    seen: set[str] = set()
    result: list[str] = []
    glob_matched: set[str] = set()
    for pattern in patterns:
        is_glob = any(ch in pattern for ch in "*?[")
        if is_glob:
            matches = sorted(p.name for p in occt_include_dir.glob(pattern))
            if not matches:
                raise RuntimeError(f"header glob {pattern!r} matched no files in {occt_include_dir}")
        else:
            if not (occt_include_dir / pattern).exists():
                raise RuntimeError(f"header {pattern!r} not found in {occt_include_dir}")
            matches = [pattern]
        for m in matches:
            if is_glob:
                glob_matched.add(m)
            if m not in seen:
                seen.add(m)
                result.append(m)
    return result, glob_matched


STANDARD_REAL = "Standard_Real"
PASSTHROUGH_TYPES = {"Standard_Integer", "Standard_Boolean"}
TRANSIENT_ROOT = "Standard_Transient"
HANDLE_PATTERN = re.compile(r"^opencascade::handle<\s*(\w+)\s*>$")

# NCollection_Array1<T>/NCollection_Array2<T> (e.g. TColgp_Array1OfPnt, TColStd_Array1OfReal)
# are typedefs, invisible as such to a parameter's own (non-canonical) spelling - only
# libclang's *canonical* type resolution reveals the actual template ("const
# NCollection_Array1<gp_Pnt> &"), which is what Param.canonical carries and these
# patterns match against (see Param.kind's "Array1OfX/Array2OfX" section below and
# codegen/README.md's "NCollection_Array1/2 support").
ARRAY1_PATTERN = re.compile(r"^NCollection_Array1<\s*(.+?)\s*>$")
ARRAY2_PATTERN = re.compile(r"^NCollection_Array2<\s*(.+?)\s*>$")
# Canonical resolution also resolves Standard_Real/Standard_Integer/Standard_Boolean
# themselves away to their own underlying fundamental type (double/int/bool) - an
# array element's spelling never comes back as "Standard_Real", only "double".
REAL_ELEMENT_SPELLINGS = {"double", "float"}
PASSTHROUGH_ELEMENT_SPELLINGS = {"int", "bool"}


def array_element_supported(elem: str, registered_types: set[str]) -> bool:
    """Whether elem - an NCollection_Array1/2's own already-canonical template
    argument spelling, e.g. "gp_Pnt" or "double" - is itself a collection element
    type this generator can convert via src/collections.hpp's to_array1/to_array2.
    Deliberately does NOT extend AD-portability's #ifdef GRUNK_OCCT_WITH_ADOLC
    dual-emission to array elements (see codegen/README.md's "Known gaps") - a
    "real" array element is always plain double, in both AD and non-AD builds."""
    return (
        elem in REAL_ELEMENT_SPELLINGS
        or elem in PASSTHROUGH_ELEMENT_SPELLINGS
        or elem in registered_types
    )


def array_element_type(canonical: str, pattern: re.Pattern) -> str:
    """Re-extracts the same element type array_element_supported already found
    supported, for emission (see emit_lambda) - called only after kind() has
    already matched, so the match here is guaranteed to succeed."""
    m = pattern.match(strip_type(canonical))
    assert m is not None, f"expected {canonical!r} to match {pattern.pattern!r}"
    return m.group(1).strip()

AD_MACRO = "GRUNK_OCCT_WITH_ADOLC"

GENERATED_BANNER = """\
// AUTO-GENERATED by codegen/generate.py from codegen/config.yml - do not edit by
// hand, your changes will be overwritten the next time this module is regenerated.
// See DESIGN.md's "Generated-code policy" and codegen/README.md.
"""


def strip_type(spelling: str) -> str:
    """Base type name with const/reference/whitespace stripped, e.g.
    "const gp_Vec &" -> "gp_Vec"."""
    s = re.sub(r"\bconst\b", "", spelling)
    s = s.replace("&", "")
    return re.sub(r"\s+", " ", s).strip()


@dataclass
class Param:
    spelling: str
    name: str
    # Canonical (typedef-resolved) spelling, e.g. "const TColgp_Array1OfPnt &" ->
    # "const NCollection_Array1<gp_Pnt> &" - only populated for genuine parameters
    # (see collect_class_callables/collect_namespace_callables), never for the
    # synthetic Param Callable.rejection_reason constructs to check a return type;
    # kind() uses that (name == "") to deliberately keep NCollection_Array1/2
    # support parameter-only for now - see kind()'s own comment.
    canonical: str = ""

    @property
    def base(self) -> str:
        return strip_type(self.spelling)

    @property
    def is_mutable_ref(self) -> bool:
        return "&" in self.spelling and "const" not in self.spelling

    def kind(self, registered_types: set[str], registered_enums: set[str]) -> str | None:
        # Non-const-reference ("out") parameters ARE supported here, but only when
        # the base type is itself an OCCT object/Handle usertype - Lua/sol2 userdata
        # for a registered class already has stable, mutable identity (the same
        # userdata the caller passed in gets mutated in place, exactly like OOP
        # mutation works in Lua natively), so `void D0(Standard_Real U, gp_Pnt& P)
        # const` binds and behaves correctly with zero special-casing. What's NOT
        # supported is a non-const reference to a *primitive* (Standard_Real/
        # Standard_Integer/Standard_Boolean) - Lua has no mutable-number-by-reference
        # concept, there is no userdata to mutate in place for a bare number. See
        # codegen/README.md's "Non-const-reference ('out') parameters" section - this
        # is a real, common OCCT idiom (D0/D1/D2/D3, Coord's 3-out-param overload,
        # ...), not a corner case, and the *recommended* way to consume it from a
        # recipe is grunk's own "module" mechanism (raw/undecorated Lua calls inside
        # a module function, whose own return value is what gets tracked
        # parametrically) - not something this generator needs to encode itself.
        if self.base == STANDARD_REAL:
            return None if self.is_mutable_ref else "real"
        if self.base in PASSTHROUGH_TYPES:
            return None if self.is_mutable_ref else "passthrough"
        # A plain C-style enum (e.g. GeomAbs_Shape) - sol2 marshals it as a plain
        # value with zero special-casing needed in the emitted lambda body (see
        # codegen/README.md's "Enum support"), exactly like Standard_Integer/
        # Boolean above - reuses the "passthrough" kind rather than a new one.
        if self.base in registered_enums:
            return None if self.is_mutable_ref else "passthrough"
        if self.base in registered_types:
            return "object"
        # Handle(X) (i.e. opencascade::handle<X>) - accepted only if X is itself a
        # type being registered in this run (see Param docs / codegen/README.md's
        # Step 3 section on Standard_Transient/Handle(T) support). Bridged to Lua via
        # occt_sol_traits.hpp's sol::unique_usertype_traits<opencascade::handle<T>>
        # specialization - no special conversion needed in the emitted lambda body,
        # the type is simply passed/returned as-is (mutable reference included).
        m = HANDLE_PATTERN.match(self.base)
        if m and m.group(1) in registered_types:
            return "handle"
        # NCollection_Array1<X>/NCollection_Array2<X> (see codegen/README.md's
        # "NCollection_Array1/2 support") - by value or const-reference only
        # (is_mutable_ref check mirrors Standard_Real's own out-param rule above: no
        # Lua-side identity to mutate a bare std::vector<X> in place through, unlike
        # a registered object/Handle). Parameter-only: self.name is empty only for
        # the synthetic Param Callable.rejection_reason builds to check a *return*
        # type, so an array-typed return stays rejected exactly as before - see
        # codegen/README.md's "Known gaps" for why (no decided Lua-facing shape yet
        # for a from_array1/2-converted return value).
        if self.name and self.canonical:
            canon_base = strip_type(self.canonical)
            m1 = ARRAY1_PATTERN.match(canon_base)
            if m1 and array_element_supported(m1.group(1), registered_types):
                return None if self.is_mutable_ref else "array1"
            m2 = ARRAY2_PATTERN.match(canon_base)
            if m2 and array_element_supported(m2.group(1), registered_types):
                return None if self.is_mutable_ref else "array2"
        return None


@dataclass
class Callable:
    class_name: str
    kind: str  # "constructor" | "method"
    cpp_name: str
    params: list[Param]
    return_spelling: str | None  # None for constructors
    is_const: bool
    is_static: bool = False  # static class method, or a namespace free function - no "self"

    def rejection_reason(self, registered_types: set[str], registered_enums: set[str]) -> str | None:
        for p in self.params:
            if p.kind(registered_types, registered_enums) is None:
                return f"unsupported parameter type '{p.spelling.strip()}'"
        if self.return_spelling is not None and self.return_spelling.strip() != "void":
            if Param(self.return_spelling, "").kind(registered_types, registered_enums) is None:
                return f"unsupported return type '{self.return_spelling.strip()}'"
        return None


@dataclass
class Entity:
    name: str
    cursor: cindex.Cursor
    kind: str  # "class" | "enum" | "namespace"


def discover_entities(tu: cindex.TranslationUnit, header_path: Path) -> list[Entity]:
    """Finds every class/struct, enum, and namespace declared *directly* in
    header_path (not nested inside another class/namespace, and not merely
    transitively #included from it) - see codegen/README.md's "Lifting the
    one-class-per-header assumption". Each becomes its own Entity, independent of
    the header's own filename: a header may define zero, one, or several (a real
    case once `headers` accepts glob patterns - see expand_headers() - rather than
    each header being hand-picked to match exactly one wanted class).

    Only *direct* children of the translation unit are considered - this is what
    excludes a class's own protected/private nested helper classes (see
    codegen/README.md's "Known gaps") and a class-scoped member enum, without
    needing a separate filter for each. A class/struct must be a real definition
    (`is_definition()`, excludes forward declarations); an enum must additionally
    have a name (skips anonymous enums, which have no symbolic type to register).
    "std" is excluded by name even though it technically qualifies (headers using
    OCCT's hashing support commonly reopen `namespace std { template<> struct
    hash<...> {...}; }` right there, at true TU-child scope) - harmless either way
    (render_module skips every "namespace" entity's own registration regardless,
    and a plain namespace reopen has no free FUNCTION_DECLs for
    collect_namespace_callables to find), just noise in the discovered-entity list.

    Performance note: uses `tu.cursor.get_children()` (direct children of the
    translation unit only), *not* `tu.cursor.walk_preorder()` (which recurses into
    every cursor's entire subtree - function bodies, nested expressions, template
    instantiation internals, the works). Since every entity this function looks for
    is by definition a direct TU child already, walk_preorder()'s extra recursion
    buys nothing here but cost: for a single moderately-included header, it visits
    on the order of 10^5 cursors where get_children() visits closer to 10^3 -
    confirmed empirically (0.6s vs 0.01s on TopoDS_Shape.hxx) - the difference
    between this finishing in well under a second and a full regen run not
    finishing inside a 10-minute timeout at all. Found the hard way."""
    interesting_kinds = (
        cindex.CursorKind.CLASS_DECL,
        cindex.CursorKind.STRUCT_DECL,
        cindex.CursorKind.ENUM_DECL,
        cindex.CursorKind.NAMESPACE,
    )
    header_path_resolved = header_path.resolve()
    entities: list[Entity] = []
    for cursor in tu.cursor.get_children():
        if cursor.kind not in interesting_kinds:
            continue
        loc_file = cursor.location.file
        if loc_file is None or Path(loc_file.name).resolve() != header_path_resolved:
            continue
        if cursor.kind in (cindex.CursorKind.CLASS_DECL, cindex.CursorKind.STRUCT_DECL) and cursor.is_definition():
            # Excludes an explicit template specialization defined right in a
            # header (a real case, not hypothetical: gp_TrsfNLerp.hxx defines
            # `class NCollection_Lerp<gp_Trsf> { ... };` directly) - its cursor.kind
            # is an ordinary CLASS_DECL and cursor.spelling is just the bare
            # template name ("NCollection_Lerp", no argument), which would
            # otherwise get registered as if it were a plain, template-free class
            # named that - cursor.type.spelling, unlike .spelling, does include the
            # template argument ("NCollection_Lerp<gp_Trsf>"), so a mismatch
            # between the two is exactly the signal a specialization is present.
            # Matches this project's existing "no NCollection type is ever exposed
            # to Lua directly" policy (see DESIGN.md's collections/Step 5 note) -
            # this isn't a new exclusion so much as that policy applying here too.
            if strip_type(cursor.type.spelling) != cursor.spelling:
                continue
            entities.append(Entity(cursor.spelling, cursor, "class"))
        elif cursor.kind == cindex.CursorKind.ENUM_DECL and cursor.is_definition() and cursor.spelling:
            entities.append(Entity(cursor.spelling, cursor, "enum"))
        elif cursor.kind == cindex.CursorKind.NAMESPACE and cursor.spelling != "std":
            entities.append(Entity(cursor.spelling, cursor, "namespace"))
    return entities


def enum_constant_names(enum_cursor: cindex.Cursor) -> list[str]:
    """The enumerator names of an ENUM_DECL cursor, in declaration order (e.g.
    ["GeomAbs_C0", "GeomAbs_C1", ...] for GeomAbs_Shape) - see codegen/README.md's
    "Enum support" for how these become both the Lua-side key and the bare C++
    value in the emitted new_enum(...) call."""
    return [c.spelling for c in enum_cursor.get_children() if c.kind == cindex.CursorKind.ENUM_CONSTANT_DECL]


@dataclass
class ParsedHeader:
    tu: cindex.TranslationUnit
    entities: list[Entity]


def parse_headers(headers: list[str], occt_include_dir: Path, clang_args: list[str],
                   cache: dict[str, ParsedHeader]) -> None:
    """Parses each header not already in cache and discovers its entities, storing
    both under cache[header] - shared by main()'s whole-config discovery pass and
    process_module()'s own per-module pass so each header is only ever parsed
    once, regardless of how many modules reference it. Parsing (and, for a
    heavily-included header, walking its AST) is the dominant cost here - Standard
    C++ header parsing runs in the ~0.1-0.5s range per header even for a single
    pass, so avoiding a second full pass over the same header roughly halves total
    runtime once a config has any overlap between the whole-config discovery scan
    and each module's own processing (which, since Step 6's "gc"/"geom" split,
    a wildcard-expanded config will have far more of than the original
    one-header-per-module design ever did)."""
    index = cindex.Index.create()
    for header in headers:
        if header in cache:
            continue
        header_path = occt_include_dir / header
        tu = index.parse(str(header_path), args=clang_args)
        diags = [d for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error]
        if diags:
            raise RuntimeError(f"clang errors parsing {header}:\n" + "\n".join(str(d) for d in diags))
        cache[header] = ParsedHeader(tu, discover_entities(tu, header_path))


def collect_namespace_callables(namespace_cursors: list[cindex.Cursor], ns_name: str) -> list[Callable]:
    """Returns every free FUNCTION_DECL directly inside the given namespace
    fragment(s) as unfiltered, is_static=True Callables (see codegen/README.md's
    "Static-only 'namespace classes'" - a namespace free function and a class's
    static method are emitted identically: no self, ns_name::cpp_name(args))."""
    callables: list[Callable] = []
    for target in namespace_cursors:
        for child in target.get_children():
            if child.kind != cindex.CursorKind.FUNCTION_DECL:
                continue
            if child.spelling.startswith("operator"):
                continue  # operators not handled yet - see codegen/README.md's "Known gaps"
            params = [Param(arg.type.spelling, arg.spelling or f"arg{i}", arg.type.get_canonical().spelling)
                      for i, arg in enumerate(child.get_arguments())]
            callables.append(Callable(ns_name, "method", child.spelling, params,
                                       child.result_type.spelling, is_const=False, is_static=True))
    return callables


def find_class_definition(tu: cindex.TranslationUnit, class_name: str) -> cindex.Cursor | None:
    """Finds the CLASS_DECL/STRUCT_DECL definition of class_name anywhere in the
    translation unit - used to walk base classes from other, transitively-included
    headers (e.g. Geom_Line's own header doesn't define Geom_Curve, but
    Geom_Curve's full definition is still visible, transitively included).

    Uses `tu.cursor.get_children()`, not `tu.cursor.walk_preorder()` - same
    performance reasoning as discover_entities(): every class this generator's
    base-chain walk ever looks up is, like discover_entities' own targets, a
    direct child of the translation unit (a #include textually inserts the
    included file's top-level declarations at that point - it doesn't nest them
    under anything), so the full recursive walk was doing a great deal of
    unnecessary work descending into function bodies and nested expressions."""
    for cursor in tu.cursor.get_children():
        if cursor.kind not in (cindex.CursorKind.CLASS_DECL, cindex.CursorKind.STRUCT_DECL):
            continue
        if cursor.spelling == class_name and cursor.is_definition():
            return cursor
    return None


def base_chain(tu: cindex.TranslationUnit, class_cursor: cindex.Cursor) -> list[str]:
    """Direct parent first, then grandparent, etc. Stops after Standard_Transient
    (inclusive) or when no further base is found. OCCT's class hierarchies are
    single-inheritance throughout the modules this generator targets - only the
    first CXX_BASE_SPECIFIER is followed (see codegen/README.md's "Known gaps" for
    multiple inheritance)."""
    chain: list[str] = []
    current = class_cursor
    while True:
        bases = [c for c in current.get_children() if c.kind == cindex.CursorKind.CXX_BASE_SPECIFIER]
        if not bases:
            break
        base_name = strip_type(bases[0].type.spelling)
        chain.append(base_name)
        if base_name == TRANSIENT_ROOT:
            break
        next_cursor = find_class_definition(tu, base_name)
        if next_cursor is None:
            break
        current = next_cursor
    return chain


def has_explicit_destructor(class_cursor: cindex.Cursor) -> bool:
    return any(c.kind == cindex.CursorKind.DESTRUCTOR for c in class_cursor.get_children())


def owns_resource_unsafely(tu: cindex.TranslationUnit, class_cursor: cindex.Cursor, chain: list[str]) -> bool:
    """True if class_cursor or any class in its ancestor chain declares an explicit
    destructor - found the hard way (see DESIGN.md's "Copying an algorithm/builder
    class segfaults"): BRepAlgoAPI_Cut (via BRepAlgoAPI_BuilderAlgo) owns a raw
    pointer member (BOPAlgo_PPaveFiller) freed in ~BRepAlgoAPI_BuilderAlgo(), with no
    user-declared copy constructor - the implicitly-generated one shallow-copies the
    pointer, so returning such a type *by value* from a constructor lambda (sol2
    moving/copying it into the usertype's own storage, on top of the guaranteed-elided
    return itself) double-frees it once both copies are eventually destroyed. Not
    Standard_Transient-derived, so Step 3's Handle(T) heap-allocation path doesn't
    apply here (this generator's own opencascade::handle<T> emission requires being
    Transient-derived to interoperate with the rest of OCCT's Handle-based API) -
    heap-allocate via std::unique_ptr<T> instead (see emit_lambda), which never
    copies/moves T itself at all, regardless of whether T is *actually* safe to copy.
    A class with no explicit destructor anywhere in its chain (gp_Pnt, TopoDS_Shape,
    ...) is assumed trivially/safely copyable and keeps the plain by-value path."""
    if has_explicit_destructor(class_cursor):
        return True
    for base_name in chain:
        base_cursor = find_class_definition(tu, base_name)
        if base_cursor is not None and has_explicit_destructor(base_cursor):
            return True
    return False


def collect_class_callables(target: cindex.Cursor, class_name: str) -> list[Callable]:
    """Returns target's public constructors/methods (static and instance) as
    unfiltered Callables - target must already be the class's own CLASS_DECL/
    STRUCT_DECL definition cursor (see find_class_definition). A static method (e.g.
    BRepTools::Write) is marked is_static=True and rendered identically to a
    namespace free function - see collect_namespace_callables/codegen/README.md."""
    callables: list[Callable] = []
    for child in target.get_children():
        if child.access_specifier != cindex.AccessSpecifier.PUBLIC:
            continue

        if child.kind == cindex.CursorKind.CONSTRUCTOR:
            kind, cpp_name, return_spelling, is_const, is_static = "constructor", class_name, None, False, False
        elif child.kind == cindex.CursorKind.CXX_METHOD:
            if child.spelling.startswith("operator"):
                continue  # operators not handled yet - see codegen/README.md's "Known gaps"
            kind, cpp_name, return_spelling, is_const, is_static = (
                "method", child.spelling, child.result_type.spelling,
                child.is_const_method(), child.is_static_method(),
            )
        else:
            continue

        params = [Param(arg.type.spelling, arg.spelling or f"arg{i}", arg.type.get_canonical().spelling)
                  for i, arg in enumerate(child.get_arguments())]
        callables.append(Callable(class_name, kind, cpp_name, params, return_spelling, is_const, is_static))
    return callables


def emit_lambda(c: Callable, kinds: list[str], ad_branch: bool, ctor_mode: str) -> str:
    param_decls = []
    call_args = []
    for p, k in zip(c.params, kinds):
        if ad_branch and k == "real":
            param_decls.append(f"sol::object {p.name}")
            call_args.append(f"occt_plugin::to_real({p.name})")
        elif k == "array1":
            elem = array_element_type(p.canonical, ARRAY1_PATTERN)
            param_decls.append(f"std::vector<{elem}> const& {p.name}")
            call_args.append(f"occt_plugin::to_array1({p.name})")
        elif k == "array2":
            elem = array_element_type(p.canonical, ARRAY2_PATTERN)
            param_decls.append(f"std::vector<{elem}> const& {p.name}_flat, Standard_Integer {p.name}_rows, Standard_Integer {p.name}_cols")
            call_args.append(f"occt_plugin::to_array2({p.name}_flat, {p.name}_rows, {p.name}_cols)")
        else:
            param_decls.append(f"{p.spelling} {p.name}")
            call_args.append(p.name)

    if c.kind == "constructor":
        args_str = ", ".join(call_args)
        if ctor_mode == "handle":
            # Standard_Transient-derived types are never value-constructed by
            # convention (OCCT's own idiom is always heap-allocate + refcount via
            # Handle(T)) - heap-allocate here and let occt_sol_traits.hpp's
            # sol::unique_usertype_traits<opencascade::handle<T>> bridge the result
            # to Lua as if it were a T instance (see codegen/README.md's "Step 3").
            return (f"[]({', '.join(param_decls)}) -> opencascade::handle<{c.class_name}> "
                    f"{{ return opencascade::handle<{c.class_name}>(new {c.class_name}({args_str})); }}")
        if ctor_mode == "unique_ptr":
            # Not Transient-derived, but owns a resource unsafely under copy/move
            # (see DESIGN.md's "Copying an algorithm/builder class segfaults" -
            # owns_resource_unsafely()) - heap-allocate via std::unique_ptr<T>, which
            # sol2 accepts directly as usertype-constructor storage and never
            # copies/moves T itself, regardless of whether that's actually safe.
            return (f"[]({', '.join(param_decls)}) {{ return std::make_unique<{c.class_name}>({args_str}); }}")
        return f"[]({', '.join(param_decls)}) {{ return {c.class_name}({args_str}); }}"

    if c.is_static:
        # Static class method or namespace free function - no "self" (see
        # codegen/README.md's "Static-only 'namespace classes'"). Always
        # fully-qualified (::) - correct whether class_name is a genuine C++ class
        # (BRepTools::Write) or a namespace (TopoDS::Face).
        all_decls = ", ".join(param_decls)
        call_expr = f"{c.class_name}::{c.cpp_name}({', '.join(call_args)})"
    else:
        self_decl = f"{c.class_name} const& self" if c.is_const else f"{c.class_name}& self"
        all_decls = ", ".join([self_decl] + param_decls)
        call_expr = f"self.{c.cpp_name}({', '.join(call_args)})"
    ret = (c.return_spelling or "void").strip()
    if ret == "void":
        return f"[]({all_decls}) {{ {call_expr}; }}"
    if "&" in ret and HANDLE_PATTERN.match(strip_type(ret)):
        # A Handle(X)-returning method/constructor that returns it by reference
        # (e.g. GC_MakeSegment::Value() const -> const Handle(Geom_TrimmedCurve)&)
        # is emitted as a by-value opencascade::handle<X> return instead of the raw
        # reference - found the hard way (GC_MakeSegment/GC_MakeCircle, Step 6):
        # pushing a bare Handle<X> onto the Lua stack by value works fine
        # everywhere else in this project (every constructor's own Handle(X)
        # return), but pushing one *by reference* hits a sol2 code path
        # (unqualified_pusher::push_keyed's set_undefined_methods_on) that tries
        # to instantiate default operator</operator== comparisons for the
        # underlying OCCT type X itself - most OCCT geometry classes (including
        # Geom_TrimmedCurve/Geom_Circle) don't declare either, so this fails to
        # compile. A Handle is just a cheap-to-copy smart pointer (incrementing a
        # refcount, O(1)), so returning by value instead is always safe and
        # sidesteps the whole issue - unlike a plain (non-Handle) registered
        # type's own const-reference return (e.g. Geom_BezierCurve::Pole()'s
        # `const gp_Pnt&`), which does *not* hit this and is left untouched.
        ret = strip_type(ret)
    return f"[]({all_decls}) -> {ret} {{ return {call_expr}; }}"


def emit_callable_expr(c: Callable, kinds: list[str], ctor_mode: str) -> str:
    has_real = "real" in kinds
    plain = emit_lambda(c, kinds, ad_branch=False, ctor_mode=ctor_mode)
    if not has_real:
        return plain
    ad_form = emit_lambda(c, kinds, ad_branch=True, ctor_mode=ctor_mode)
    return f"\n#if defined({AD_MACRO})\n            {ad_form}\n#else\n            {plain}\n#endif\n        "


@dataclass
class ModuleResult:
    name: str
    headers: list[str]
    extra_includes: list[str] = field(default_factory=list)
    accepted_ctors: dict[str, list[tuple[Callable, list[str]]]] = field(default_factory=dict)
    accepted_methods: dict[str, dict[str, list[tuple[Callable, list[str]]]]] = field(default_factory=dict)
    rejected: list[tuple[Callable, str]] = field(default_factory=list)
    blacklisted: list[Callable] = field(default_factory=list)
    is_transient: dict[str, bool] = field(default_factory=dict)
    ctor_mode: dict[str, str] = field(default_factory=dict)  # "value" | "unique_ptr" | "handle"
    bases: dict[str, list[str]] = field(default_factory=dict)
    entity_kind: dict[str, str] = field(default_factory=dict)  # "class" | "enum" | "namespace"
    entity_names: list[str] = field(default_factory=list)  # discovery order - replaces the old
    # one-name-per-header assumption (Path(h).stem for h in headers); a single header can now
    # contribute zero, one, or several entities (see discover_entities).
    enum_values: dict[str, list[str]] = field(default_factory=dict)  # enum name -> its enumerator names


def process_module(module: dict, cache: dict[str, ParsedHeader], registered_types: set[str], registered_enums: set[str]) -> ModuleResult:
    """registered_types/registered_enums are the sets of every class/enum name
    across ALL modules in this run (not just this module's own headers) - a type
    registered by module A is a valid parameter/return type for a callable in
    module B, matching how legacy grocc's cross-module type visibility worked.
    Computed by main() from the same `cache` this function reads (see
    parse_headers()) before any module is actually processed - see
    codegen/README.md's "Type registration spans the whole config".

    module["headers"] is already fully expanded (glob patterns resolved to literal
    filenames - see expand_headers()) and each header may define more than one
    entity - see discover_entities(). `cache` must already hold a ParsedHeader for
    every one of them (main() calls parse_headers() up front for exactly this
    reason - each header is parsed/discovered once, not once per module that
    happens to reference it and again here).

    module["extra_includes"] (optional) is a *different* concept, also revived from
    legacy grocc's config.yml.in: extra #include lines the generated .cpp needs for
    a type it uses only by reference/pointer (so this module's own headers only
    forward-declare it, e.g. BRepBndLib.hxx's `class TopoDS_Shape;`) but never
    registers itself - sol2's own usertype binding needs the *complete* type
    definition in this translation unit regardless of whether some other module's
    .cpp already has it. Not a source of new registrable classes - see
    codegen/README.md's "extra_includes" section."""
    headers = module["headers"]
    blacklist = set(module.get("blacklist", []))
    glob_matched = module.get("_glob_matched_headers", set())

    result = ModuleResult(name=module["name"], headers=headers, extra_includes=module.get("extra_includes", []))

    for header in headers:
        tu, entities = cache[header].tu, cache[header].entities
        if not entities:
            if header in glob_matched:
                # Expected, occasional glob noise (see expand_headers' own
                # docstring - e.g. gp_*.hxx matching gp_Vec2f.hxx, a bare typedef
                # with no class/struct/enum of its own) - skip quietly rather than
                # failing the whole run. A *literal*, hand-picked header with
                # nothing in it is still a hard error below - almost certainly a
                # typo, not something to silently ignore.
                continue
            raise RuntimeError(f"no class/struct/enum/namespace definitions found in {header}")

        for entity in entities:
            name = entity.name
            if name in blacklist:
                continue  # a bare ClassName/EnumName in the blacklist skips it entirely
            result.entity_kind[name] = entity.kind
            result.entity_names.append(name)

            if entity.kind == "enum":
                result.enum_values[name] = enum_constant_names(entity.cursor)
                continue  # no callables - see render_module's own enum handling

            if entity.kind == "class":
                chain = base_chain(tu, entity.cursor)
                result.bases[name] = chain
                result.is_transient[name] = TRANSIENT_ROOT in chain
                if result.is_transient[name]:
                    result.ctor_mode[name] = "handle"
                elif owns_resource_unsafely(tu, entity.cursor, chain):
                    result.ctor_mode[name] = "unique_ptr"
                else:
                    result.ctor_mode[name] = "value"
                callables = collect_class_callables(entity.cursor, name)
            else:
                # Namespace (e.g. TopoDS.hxx's `namespace TopoDS { ... }` - see
                # codegen/README.md's "Static-only 'namespace classes'"). No base
                # chain/Transient concept applies. Treated as a single fragment -
                # every namespace this generator has targeted so far is fully
                # defined in one header, so reopening across multiple headers in
                # the same module isn't handled (would need merging by name here).
                callables = collect_namespace_callables([entity.cursor], name)

            result.accepted_methods.setdefault(name, {})

            for c in callables:
                symbol = f"{c.class_name}::{c.cpp_name}"
                if symbol in blacklist:
                    result.blacklisted.append(c)
                    continue

                reason = c.rejection_reason(registered_types, registered_enums)
                if reason is not None:
                    result.rejected.append((c, reason))
                    continue

                kinds = [p.kind(registered_types, registered_enums) for p in c.params]
                if c.kind == "constructor":
                    result.accepted_ctors.setdefault(name, []).append((c, kinds))
                else:
                    result.accepted_methods[name].setdefault(c.cpp_name, []).append((c, kinds))

    return result


def render_module(result: ModuleResult) -> tuple[str, str]:
    """Returns (header_source, cpp_source)."""
    header_source = GENERATED_BANNER + f"""
#pragma once

#include <grunk/dynamic.hpp>

void register_{result.name}(grunk::plugin_namespace& ns);
"""

    includes = "\n".join(f"#include <{h}>" for h in result.headers + result.extra_includes)
    any_transient = any(result.is_transient.values())
    any_array = any(
        k in ("array1", "array2")
        for overloads in result.accepted_ctors.values()
        for _, kinds in overloads
        for k in kinds
    ) or any(
        k in ("array1", "array2")
        for methods in result.accepted_methods.values()
        for overloads in methods.values()
        for _, kinds in overloads
        for k in kinds
    )
    all_class_names = result.entity_names
    registered_here = set(all_class_names)

    free_function_parts = []  # static class methods + namespace free functions with no naming collision
    extra_member_functions: dict[str, list[str]] = {}  # class_name -> extra ".add_member_function(...)" lines

    for class_name in all_class_names:
        ctor_mode = result.ctor_mode.get(class_name, "value")

        for method_name, overloads in list(result.accepted_methods.get(class_name, {}).items()):
            if not overloads[0][0].is_static:
                continue
            if len(overloads) == 1:
                c, kinds = overloads[0]
                fun_expr = emit_callable_expr(c, kinds, ctor_mode)
            else:
                fun_expr = "sol::overload(\n        " + ",\n        ".join(
                    emit_callable_expr(c, kinds, ctor_mode) for c, kinds in overloads
                ) + "\n    )"

            composed_name = f"{class_name}_{method_name}"
            if composed_name in registered_here:
                # Naming collision (e.g. TopoDS::Face(shape) composes to the same
                # name as the registered TopoDS_Face class itself - a real case, not
                # hypothetical: every TopoDS namespace downcast helper collides with
                # its own destination leaf type this way). Rather than clobbering the
                # type's own occt.TopoDS_Face table with a plain function, attach it
                # as that type's own .DownCast(shape) member instead - mirrors Step
                # 3's Handle(T)::DownCast naming (src/geom/geom_extras.hpp) for a
                # consistent Lua-side idiom regardless of which OCCT downcast
                # mechanism (Handle-based vs. TopoDS-namespace-based) is underneath.
                # See codegen/README.md's "Static-only 'namespace classes'".
                extra_member_functions.setdefault(composed_name, []).append(
                    f'        .add_member_function("DownCast", {fun_expr})'
                )
            else:
                free_function_parts.append(f'    ns.register_function("{composed_name}", {fun_expr});')

    body_parts = []
    for class_name in all_class_names:
        entity_kind = result.entity_kind.get(class_name)
        if entity_kind == "namespace":
            continue  # no register_type for a genuine namespace - nothing else to do
        if entity_kind == "enum":
            # A plain C-style enum (see codegen/README.md's "Enum support") -
            # sol2's own new_enum(...) is a non-const sol::table method, but
            # plugin_namespace::table() returns `sol::table const&` - constructing
            # a fresh (cheap, reference-semantics) sol::table copy from it sidesteps
            # that constness mismatch without needing a new grunk API. No explicit
            # enum-type template argument needed: this sol2 overload deduces it
            # from the value arguments themselves. Each enumerator's own bare C++
            # name doubles as its Lua-side string key (e.g.
            # occt.GeomAbs_Shape.GeomAbs_C0).
            enumerator_pairs = ",\n            ".join(
                f'"{value}", {value}' for value in result.enum_values.get(class_name, [])
            )
            body_parts.append(
                f'    sol::table(ns.table()).new_enum("{class_name}",\n            {enumerator_pairs}\n        );'
            )
            continue

        ctor_mode = result.ctor_mode.get(class_name, "value")
        instance_methods = {
            method_name: overloads
            for method_name, overloads in result.accepted_methods.get(class_name, {}).items()
            if not overloads[0][0].is_static
        }

        lines = [f'    ns.register_type<{class_name}, sol::automagic_flags::none>("{class_name}")']

        ctors = result.accepted_ctors.get(class_name, [])
        if ctors:
            ctor_exprs = [emit_callable_expr(c, kinds, ctor_mode) for c, kinds in ctors]
            lines.append("        .add_constructors(\n            " + ",\n            ".join(ctor_exprs) + "\n        )")

        for method_name, overloads in instance_methods.items():
            if len(overloads) == 1:
                c, kinds = overloads[0]
                fun_expr = emit_callable_expr(c, kinds, ctor_mode)
            else:
                fun_expr = "sol::overload(\n            " + ",\n            ".join(
                    emit_callable_expr(c, kinds, ctor_mode) for c, kinds in overloads
                ) + "\n        )"
            lines.append(f'        .add_member_function("{method_name}", {fun_expr})')

        lines.extend(extra_member_functions.get(class_name, []))

        # Standard_Transient/Handle(T) support (Step 3, also used for plain
        # value-type inheritance since Step 4, e.g. TopoDS_Face : TopoDS_Shape):
        # register the full ancestor chain so Lua's colon-call dispatch (and
        # argument-position upcasting) can find a method/accept a value via a base
        # class not registered on the derived type itself.
        bases = result.bases.get(class_name, [])
        if bases:
            lines.append(f"        .add_bases<{', '.join(bases)}>()")

        body_parts.append("\n".join(lines) + ";")

    extra_includes = '\n#include "../src/occt_sol_traits.hpp"' if any_transient else ""
    extra_includes += '\n#include "../src/collections.hpp"\n#include <vector>' if any_array else ""

    cpp_source = GENERATED_BANNER + f"""
#include "{result.name}.hpp"

{includes}

#include <memory>

#include "../src/occt_ad_support.hpp"{extra_includes}

void register_{result.name}(grunk::plugin_namespace& ns)
{{
{chr(10).join(body_parts)}
{chr(10).join(free_function_parts)}
}}
"""
    return header_source, cpp_source


def build_clang_args(occt_include_dir: Path) -> list[str]:
    args = ["-x", "c++", "-std=c++17", f"-I{occt_include_dir}"]
    args += [f"-isystem{d}" for d in system_cxx_include_dirs()]
    return args


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path(__file__).parent / "config.yml")
    parser.add_argument("--occt-include-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    config = yaml.safe_load(args.config.read_text())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    clang_args = build_clang_args(args.occt_include_dir)

    for module in config["modules"]:
        module["headers"], module["_glob_matched_headers"] = expand_headers(module["headers"], args.occt_include_dir)

    # Every header across the whole config is parsed/discovered exactly once here
    # (see parse_headers()'s own docstring) - both registered_types/registered_enums
    # (computed from this same cache, below) and each module's own process_module()
    # call read from it rather than re-parsing.
    cache: dict[str, ParsedHeader] = {}
    for module in config["modules"]:
        parse_headers(module["headers"], args.occt_include_dir, clang_args, cache)

    registered_types = {e.name for p in cache.values() for e in p.entities if e.kind == "class"}
    registered_enums = {e.name for p in cache.values() for e in p.entities if e.kind == "enum"}

    for module in config["modules"]:
        result = process_module(module, cache, registered_types, registered_enums)
        header_source, cpp_source = render_module(result)
        (args.output_dir / f"{result.name}.hpp").write_text(header_source)
        (args.output_dir / f"{result.name}.cpp").write_text(cpp_source)

        n_accepted = sum(len(v) for v in result.accepted_ctors.values()) + sum(
            len(o) for m in result.accepted_methods.values() for o in m.values()
        )
        print(f"[{result.name}] {n_accepted} accepted, {len(result.rejected)} rejected, "
              f"{len(result.blacklisted)} blacklisted", file=sys.stderr)
        for c, reason in result.rejected:
            print(f"  rejected {c.class_name}::{c.cpp_name}: {reason}", file=sys.stderr)
        for c in result.blacklisted:
            print(f"  blacklisted {c.class_name}::{c.cpp_name}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
