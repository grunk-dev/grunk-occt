# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""This plugin's own CodeGenerator subclass (see grunk.codegen.generate.CodeGenerator's
own docstring for the extension mechanism) - teaches the otherwise library-agnostic
shared generator every OpenCASCADE (OCCT)-specific idiom this plugin's own headers
use: Standard_Real's AD-portability dual-emission (gated by GRUNK_OCCT_WITH_ADOLC,
built and consumed entirely in this file - grunk's own generator has no concept of
either), Standard_Transient/opencascade::handle<T>'s heap-allocated,
reference-counted construction, and NCollection_Array1/2<T>'s conversion to/from a
plain std::vector<T>. Referenced from codegen/config.yml's `code_generator: hooks.py`
key.

See src/collections.hpp/src/occt_ad_support.hpp for the hand-written
occt_plugin::to_real/to_array1/to_array2/to_real_array1/to_real_array2 helper
functions this file's own emit_param() calls into, and src/occt_sol_traits.hpp for
the sol::unique_usertype_traits<opencascade::handle<T>> specialization that makes
emit_constructor()'s own "handle" mode actually work at the sol2 binding level -
codegen/config.yml's own `includes_for_kinds`/`includes_for_ctor_modes` keys pull all
three headers into a module's generated .cpp only when this file's own classification
actually produced a kind/ctor-mode needing them.
"""

import re

from grunk.codegen import generate

# OCCT's own reference-counted-object root - a class deriving from this
# (anywhere in its base chain) is always heap-allocated and refcounted via
# Handle(T), never value-constructed (see classify_ctor_mode/emit_bases below) -
# grunk's own generator has no such concept at all, this is entirely this
# plugin's own idiom to recognize and act on.
TRANSIENT_ROOT = "Standard_Transient"

# The OCCT typedef this plugin's own headers use for a scalar that needs
# AD-portability dual-emission (see emit_param/emit_callable below) - grunk's own
# generator has no concept of this at all, only a `real` kind name this file itself
# chose and is now responsible for.
STANDARD_REAL = "Standard_Real"
# OCCT typedef aliases for plain C++ fundamentals - added to the generator's own
# small built-in CXX_FUNDAMENTAL_PASSTHROUGH_TYPES baseline (bare "double"/"bool"/
# "unsigned int"/"size_t"), which OCCT's own headers never spell that way.
OCCT_TYPEDEF_PASSTHROUGH = {"Standard_Integer", "Standard_Boolean"}

# opencascade::handle<T> (OCCT's own reference-counted smart-pointer template) -
# accepted only if T is itself a registered type (see classify_param below).
HANDLE_PATTERN = re.compile(r"^opencascade::handle<\s*(\w+)\s*>$")

# NCollection_Array1<T>/NCollection_Array2<T> (e.g. TColgp_Array1OfPnt,
# TColStd_Array1OfReal) are typedefs, invisible as such to a parameter's own
# (non-canonical) spelling - only libclang's *canonical* type resolution reveals the
# actual template, which is what Param.canonical carries and these patterns match
# against - see src/collections.hpp's own "NCollection_Array1/2 support".
ARRAY1_PATTERN = re.compile(r"^NCollection_Array1<\s*(.+?)\s*>$")
ARRAY2_PATTERN = re.compile(r"^NCollection_Array2<\s*(.+?)\s*>$")
# Canonical resolution also resolves Standard_Real/Standard_Integer/Standard_Boolean
# themselves away to their own underlying fundamental type (double/int/bool) - an
# array element's spelling never comes back as "Standard_Real", only "double".
REAL_ELEMENT_SPELLINGS = {"double", "float"}
PASSTHROUGH_ELEMENT_SPELLINGS = {"int", "bool"}

# The C++ preprocessor macro gating this plugin's own AD-portability dual-emission -
# entirely this file's own concern; grunk's own generator never sees this name.
AD_MACRO = "GRUNK_OCCT_WITH_ADOLC"

# The C++ namespace this plugin's own to_real/to_array1/to_array2/to_real_array1/
# to_real_array2 conversion helpers live in (see src/collections.hpp/
# src/occt_ad_support.hpp) - a plain string literal this file's own emit_param()
# uses, invisible to grunk's own generator.
NS = "occt_plugin"


def _array_element_supported(elem: str, registered_types: set[str]) -> bool:
    """Whether elem - an NCollection_Array1/2's own already-canonical template
    argument spelling, e.g. "gp_Pnt" or "double" - is itself a collection element
    type this plugin can convert via src/collections.hpp's to_array1/to_array2.
    Deliberately does NOT extend AD-portability's #ifdef GRUNK_OCCT_WITH_ADOLC
    dual-emission to array elements (see codegen README's "Known gaps") - a "real"
    array element is always plain double, in both AD and non-AD builds."""
    return (
        elem in REAL_ELEMENT_SPELLINGS
        or elem in PASSTHROUGH_ELEMENT_SPELLINGS
        or elem in registered_types
    )


def _array_element_type(canonical: str, pattern: re.Pattern) -> str:
    """Re-extracts the same element type _array_element_supported already found
    supported, for emission - called only after classify_param has already
    matched, so the match here is guaranteed to succeed."""
    m = pattern.match(generate.strip_type(canonical))
    assert m is not None, f"expected {canonical!r} to match {pattern.pattern!r}"
    return m.group(1).strip()


class Hooks(generate.CodeGenerator):
    OVERLOAD_AMBIGUITY_KINDS = {"real"}
    REVERSIBLE_OPERAND_KINDS = frozenset({"passthrough", "real"})

    # Set/cleared around a second emit_callable() pass for a "real"-kind callable
    # (see emit_callable below) - private to this file, invisible to
    # grunk.codegen.generate itself, which has no "variant"/AD concept at all.
    _ad_variant = False

    def classify_param(self, param, registered_types, registered_enums):
        # Non-const-reference ("out") parameters ARE supported for a registered
        # object (see Param.kind's own docstring) but NOT for Standard_Real - Lua
        # has no mutable-number-by-reference concept. This is a real, common OCCT
        # idiom (D0/D1/D2/D3, Coord's 3-out-param overload, ...), not a corner
        # case - the *recommended* way to consume it from a recipe is grunk's own
        # "module" mechanism, not something this generator needs to encode.
        if param.base == STANDARD_REAL:
            return None if param.is_mutable_ref else "real"
        if param.base in OCCT_TYPEDEF_PASSTHROUGH:
            return None if param.is_mutable_ref else "passthrough"
        # Handle(X) (i.e. opencascade::handle<X>) - accepted only if X is
        # itself a type being registered in this run (unlike a plain class
        # value/reference parameter's own "object" kind - see Param.kind()'s
        # own docstring - which sol2 genuinely doesn't need pre-registered).
        # Confirmed the hard way this is still needed here specifically:
        # sol::unique_usertype_traits<opencascade::handle<T>> pushes T through
        # a *different* sol2 code path than plain automatic userdata boxing,
        # one that probes for comparison-operator/container support to build
        # T's own metatable the first time - X is registered with
        # automagic_flags::none precisely to suppress that (see
        # register_type's own use of it), so a T without that (an unrelated
        # OCCT class with no operator</operator==, e.g. Standard_Type,
        # BRepTools_History, TopLoc_Datum3D, TopoDS_TShape) fails to compile
        # rather than falling back gracefully.
        m = HANDLE_PATTERN.match(param.base)
        if m and m.group(1) in registered_types:
            return "handle"
        # NCollection_Array1<X>/NCollection_Array2<X> - by value or const-reference
        # only (mirrors Standard_Real's own out-param rule above: no Lua-side
        # identity to mutate a bare std::vector<X> in place through, unlike a
        # registered object/Handle). Parameter-only: param.name is empty only for
        # the synthetic Param a Callable's own rejection_reason/accepted_arities
        # build to check a *return* type, so an array-typed return stays rejected
        # (no decided Lua-facing shape yet for a from_array1/2-converted return
        # value).
        #
        # Once m1/m2 actually matches, this IS an NCollection_Array1/2 parameter -
        # everything from here on is a definitive commitment, never "no opinion"
        # (None): either accept it (a supported, non-mutable-ref element) or
        # reject it outright (generate.REJECTED) for an unsupported element OR a
        # mutable reference (no Lua-side identity to mutate a bare array in
        # place through - see this method's own comment above). Plain None here
        # for either reason used to let Param.kind()'s own generic
        # BARE_TYPE_NAME fallback silently accept the param's *surface*
        # spelling - almost always a bare-identifier typedef (e.g.
        # "TColgp_Array1OfPnt" for "NCollection_Array1<gp_Pnt>") - as a plain
        # "object" instead (a real, confirmed bug for both cases - see
        # generate.REJECTED's own module-level docstring and
        # test_hooks.py::test_classify_param_array1_array2).
        if param.name and param.canonical:
            canon_base = generate.strip_type(param.canonical)
            m1 = ARRAY1_PATTERN.match(canon_base)
            if m1:
                if param.is_mutable_ref or not _array_element_supported(m1.group(1), registered_types):
                    return generate.REJECTED
                return "array1"
            m2 = ARRAY2_PATTERN.match(canon_base)
            if m2:
                if param.is_mutable_ref or not _array_element_supported(m2.group(1), registered_types):
                    return generate.REJECTED
                return "array2"
        return None

    def classify_ctor_mode(self, class_name, base_chain):
        # Standard_Transient-derived types are never value-constructed by
        # convention (OCCT's own idiom is always heap-allocate + refcount via
        # Handle(T)) - grunk's own generator has no notion of this at all,
        # this override is entirely this file's own responsibility (see
        # TRANSIENT_ROOT above).
        return "handle" if TRANSIENT_ROOT in base_chain else None

    def emit_bases(self, class_name, chain):
        # .add_bases<...>() only needs the chain up to (and including)
        # Standard_Transient itself - OCCT's own internal allocator/RTTI mixins
        # further up aren't meaningful Lua-side base classes and aren't
        # registered types themselves, so truncate right there rather than
        # walking base_chain()'s raw result all the way to its natural end.
        if TRANSIENT_ROOT in chain:
            return chain[: chain.index(TRANSIENT_ROOT) + 1]
        return chain

    def emit_constructor(self, class_name, ctor_mode, param_decls, args_str):
        # Only ever called for ctor_mode == "handle" (see classify_ctor_mode
        # above) - heap-allocate and let occt_sol_traits.hpp's
        # sol::unique_usertype_traits<opencascade::handle<T>> bridge the result to
        # Lua as if it were a T instance.
        return (f"[]({', '.join(param_decls)}) -> opencascade::handle<{class_name}> "
                f"{{ return opencascade::handle<{class_name}>(new {class_name}({args_str})); }}")

    def emit_param(self, param, kind):
        if kind == "real":
            if self._ad_variant:
                return f"sol::object {param.name}", f"{NS}::to_real({param.name})"
            return None  # primary variant: plain Standard_Real spelling, builtin fallback is correct
        if kind == "handle":
            # Emitted BY VALUE (opencascade::handle<X>, not the original
            # declaration's own "const opencascade::handle<X>&") - required for
            # sol2's own Handle(Derived)->Handle(X) unique-usertype base-class
            # conversion to have any chance of applying at all. Root-caused
            # precisely (see DESIGN.md's "interpolate_curves segfault, real
            # root cause found" and its own follow-up entries): sol2's
            # qualified_checker/qualified_getter (the base-aware code path,
            # consulted during overload resolution and multi-parameter
            # argument binding - sol::detail::inheritance<T>::type_unique_cast,
            # which needs SOL_BASE_CLASSES/SOL_DERIVED_CLASSES, generated/
            # sol_base_classes.hpp) explicitly excludes reference parameters
            # (`!std::is_reference_v<X>`) from that whole mechanism - a
            # by-reference Handle(X) parameter, however it's registered, can
            # only ever accept an *exact*-type match. A Handle is a cheap-to-
            # copy smart pointer (an O(1) refcount increment, not a deep copy),
            # so passing every one of them by value instead is always safe and
            # is what actually enables this - confirmed by a live rebuild of
            # BRepBuilderAPI_MakeFace's own generated constructor overload set
            # (a Handle(Geom_Surface) parameter, previously accepting only a
            # Handle(Geom_Surface) argument exactly, now correctly also
            # accepting a Handle(Geom_BSplineSurface)/any other registered
            # subtype).
            m = HANDLE_PATTERN.match(param.base)
            handle_type = f"opencascade::handle<{m.group(1)}>"
            return f"{handle_type} {param.name}", param.name
        if kind == "array1":
            elem = _array_element_type(param.canonical, ARRAY1_PATTERN)
            # A real-element array1 (e.g. TColStd_Array1OfReal) needs
            # to_real_array1, not to_array1<double> - see src/collections.hpp's
            # own comment for why: the Lua-side vector is always
            # std::vector<double> regardless of build, but the OCCT array's own
            # element type is Standard_Real, a distinct C++ type from double
            # under GRUNK_OCCT_WITH_ADOLC.
            fn = "to_real_array1" if elem in REAL_ELEMENT_SPELLINGS else "to_array1"
            return f"std::vector<{elem}> const& {param.name}", f"{NS}::{fn}({param.name})"
        if kind == "array2":
            elem = _array_element_type(param.canonical, ARRAY2_PATTERN)
            fn = "to_real_array2" if elem in REAL_ELEMENT_SPELLINGS else "to_array2"
            return (
                f"std::vector<{elem}> const& {param.name}_flat, Standard_Integer {param.name}_rows, "
                f"Standard_Integer {param.name}_cols",
                f"{NS}::{fn}({param.name}_flat, {param.name}_rows, {param.name}_cols)",
            )
        return None

    def emit_return_type(self, return_spelling):
        # A Handle(X)-returning method/constructor that returns it by reference
        # (e.g. Value() const -> const Handle(SomeCurve)&) is emitted as a
        # by-value opencascade::handle<X> return instead of the raw reference -
        # found the hard way: pushing a bare Handle<X> onto the Lua stack by
        # value works fine everywhere else in this project (every constructor's
        # own Handle(X) return), but pushing one *by reference* hits a sol2 code
        # path (unqualified_pusher::push_keyed's set_undefined_methods_on) that
        # tries to instantiate default operator</operator== comparisons for the
        # underlying OCCT type X itself - most OCCT geometry classes don't
        # declare either, so this fails to compile. A Handle is just a
        # cheap-to-copy smart pointer (incrementing a refcount, O(1)), so
        # returning by value instead is always safe and sidesteps the whole
        # issue - unlike a plain (non-Handle) registered type's own
        # const-reference return, which does *not* hit this and is left
        # untouched.
        if "&" in return_spelling and HANDLE_PATTERN.match(generate.strip_type(return_spelling)):
            return generate.strip_type(return_spelling)
        return None

    def emit_callable(self, c, kinds, ctor_mode):
        # Standard_Real AD-portability dual-emission: the ONLY place this
        # plugin's own hooks.py needs more than one textual variant of the same
        # callable - see grunk.codegen.generate.CodeGenerator.emit_callable's own
        # docstring for why this is entirely this file's own responsibility, not
        # a mechanism grunk's own generator provides. Calling super() twice, with
        # a private flag flipped in between (read back by this file's own
        # emit_param() above), is what produces the two variants; grunk's own
        # generator never sees "real" or GRUNK_OCCT_WITH_ADOLC at all.
        if c.kind == "operator" or "real" not in kinds:
            return super().emit_callable(c, kinds, ctor_mode)
        plain = super().emit_callable(c, kinds, ctor_mode)
        self._ad_variant = True
        try:
            alternate = super().emit_callable(c, kinds, ctor_mode)
        finally:
            self._ad_variant = False
        return f"\n#if defined({AD_MACRO})\n            {alternate}\n#else\n            {plain}\n#endif\n        "
