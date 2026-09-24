# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""Unit tests for codegen/hooks.py - this plugin's own CodeGenerator subclass
teaching grunk's otherwise library-agnostic shared code generator (grunk.codegen)
every OCCT-specific idiom it needs (see hooks.py's own module docstring). These
tests used to live in grunk's own shared tests/python/test_codegen.py, back when
that module still had OCCT's own literals hardcoded into it directly - now that
they've moved into this plugin's own hooks.py, so has their test coverage: this
is strictly better than before, since it now tests the actual code that ships,
not a copy of OCCT's shape baked into the shared repo.

Run via `pixi run test_hooks` (a plain pytest invocation, no C++ build needed at
all - these tests exercise hooks.py's own Python logic directly, the same way
grunk's own test_codegen.py tests generate.py's)."""

import importlib.util
import sys
from pathlib import Path

import pytest

from grunk.codegen import generate

# hooks.py isn't part of an installed Python package (it's loaded dynamically by
# grunk.codegen.generate.load_code_generator() at codegen-run time, via
# codegen/config.yml's own `code_generator: hooks.py` key) - load it here the
# same way, rather than via a package-relative import, so these tests exercise
# the exact file codegen actually uses.
_HOOKS_PATH = Path(__file__).resolve().parent.parent.parent / "codegen" / "hooks.py"
_spec = importlib.util.spec_from_file_location("occt_codegen_hooks", _HOOKS_PATH)
hooks_module = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = hooks_module
_spec.loader.exec_module(hooks_module)

Hooks = hooks_module.Hooks


def _callable(params, return_spelling="void", is_static=False, class_name="Widget"):
    return generate.Callable(class_name, "method", "Do", params, return_spelling, is_const=False, is_static=is_static)


# ---------------------------------------------------------------------------
# classify_param() - Standard_Real/passthrough typedefs/Handle/array1/array2
# ---------------------------------------------------------------------------

def test_classify_param_standard_real():
    hooks = Hooks()
    assert generate.Param("Standard_Real", "x").kind(set(), set(), hooks) == "real"
    assert generate.Param("Standard_Real &", "x").kind(set(), set(), hooks) is None  # mutable-ref real rejected


def test_classify_param_occt_typedef_passthrough():
    hooks = Hooks()
    assert generate.Param("Standard_Integer", "x").kind(set(), set(), hooks) == "passthrough"
    assert generate.Param("Standard_Boolean &", "x").kind(set(), set(), hooks) is None


def test_classify_param_handle():
    hooks = Hooks()
    assert generate.Param("opencascade::handle<Geom_Curve>", "x").kind({"Geom_Curve"}, set(), hooks) == "handle"
    assert generate.Param("opencascade::handle<Geom_Curve>", "x").kind(set(), set(), hooks) is None  # not registered


def test_classify_param_array1_array2():
    hooks = Hooks()
    p1 = generate.Param("const TColgp_Array1OfPnt &", "pts", "const NCollection_Array1<gp_Pnt> &")
    assert p1.kind({"gp_Pnt"}, set(), hooks) == "array1"
    assert p1.kind(set(), set(), hooks) is None  # gp_Pnt not registered -> element unsupported

    p2 = generate.Param("const TColStd_Array2OfReal &", "m", "const NCollection_Array2<double> &")
    assert p2.kind(set(), set(), hooks) == "array2"  # double is always supported

    p_mut = generate.Param("TColgp_Array1OfPnt &", "pts", "NCollection_Array1<gp_Pnt> &")
    assert p_mut.kind({"gp_Pnt"}, set(), hooks) is None  # mutable array ref rejected


def test_classify_param_return_type_array_stays_unsupported():
    # rejection_reason/accepted_arities build a synthetic Param(spelling, "") to
    # check a *return* type - array kinds are deliberately parameter-only (see
    # classify_param's own comment), so an array-typed return must stay
    # unsupported even though the element itself would otherwise qualify.
    hooks = Hooks()
    ret = generate.Param("NCollection_Array1<gp_Pnt>", "", "NCollection_Array1<gp_Pnt>")
    assert ret.kind({"gp_Pnt"}, set(), hooks) is None


# ---------------------------------------------------------------------------
# classify_ctor_mode() / emit_bases() (both keyed on TRANSIENT_ROOT)
# ---------------------------------------------------------------------------

def test_classify_ctor_mode_transient_base_is_handle():
    hooks = Hooks()
    assert hooks.classify_ctor_mode("Geom_Curve", ["Standard_Transient"]) == "handle"
    assert hooks.classify_ctor_mode("gp_Pnt", []) is None


def test_emit_bases_truncates_at_transient_root():
    hooks = Hooks()
    # A raw base_chain() result reaching further than Standard_Transient (e.g.
    # an OCCT-internal allocator/RTTI mixin above it) is truncated right there -
    # Standard_Transient itself stays included (it's a registered type).
    chain = ["Geom_Geometry", "Standard_Transient", "Some_InternalMixin"]
    assert hooks.emit_bases("Geom_Curve", chain) == ["Geom_Geometry", "Standard_Transient"]


def test_emit_bases_passes_through_when_no_transient_root():
    hooks = Hooks()
    assert hooks.emit_bases("gp_Pnt", []) == []
    assert hooks.emit_bases("SomeValueType", ["SomeBase"]) == ["SomeBase"]


# ---------------------------------------------------------------------------
# emit_param() / emit_constructor() / emit_return_type()
# ---------------------------------------------------------------------------

def test_emit_param_real_primary_variant_falls_back_to_builtin():
    hooks = Hooks()
    assert hooks.emit_param(generate.Param("Standard_Real", "x"), "real") is None


def test_emit_param_real_alternate_variant_uses_to_real():
    hooks = Hooks()
    hooks._ad_variant = True
    decl, arg = hooks.emit_param(generate.Param("Standard_Real", "x"), "real")
    assert decl == "sol::object x"
    assert arg == "occt_plugin::to_real(x)"


def test_emit_param_array1_real_element_uses_to_real_array1():
    hooks = Hooks()
    p = generate.Param("const TColStd_Array1OfReal &", "xs", "const NCollection_Array1<double> &")
    decl, arg = hooks.emit_param(p, "array1")
    assert decl == "std::vector<double> const& xs"
    assert arg == "occt_plugin::to_real_array1(xs)"


def test_emit_param_array1_non_real_element_uses_plain_to_array1():
    hooks = Hooks()
    p = generate.Param("const TColgp_Array1OfPnt &", "pts", "const NCollection_Array1<gp_Pnt> &")
    decl, arg = hooks.emit_param(p, "array1")
    assert arg == "occt_plugin::to_array1(pts)"


def test_emit_param_array2_uses_flat_rows_cols():
    hooks = Hooks()
    p = generate.Param("const TColStd_Array2OfReal &", "m", "const NCollection_Array2<double> &")
    decl, arg = hooks.emit_param(p, "array2")
    assert "m_flat" in decl and "m_rows" in decl and "m_cols" in decl
    assert arg == "occt_plugin::to_real_array2(m_flat, m_rows, m_cols)"


def test_emit_constructor_handle_mode_heap_allocates():
    hooks = Hooks()
    src = hooks.emit_constructor("Geom_Curve", "handle", [], "")
    assert "opencascade::handle<Geom_Curve>" in src
    assert "new Geom_Curve(" in src


def test_emit_return_type_strips_handle_by_reference():
    hooks = Hooks()
    assert hooks.emit_return_type("const opencascade::handle<Geom_Curve>&") == "opencascade::handle<Geom_Curve>"
    assert hooks.emit_return_type("const gp_Pnt&") is None  # not a Handle - left untouched
    assert hooks.emit_return_type("opencascade::handle<Geom_Curve>") is None  # already by-value


# ---------------------------------------------------------------------------
# emit_callable() - Standard_Real AD dual-emission
# ---------------------------------------------------------------------------

def test_emit_callable_no_ad_macro_without_real_kind():
    hooks = Hooks()
    c = _callable([generate.Param("double", "x")], return_spelling="double")
    src = hooks.emit_callable(c, ["passthrough"], "value")
    assert hooks_module.AD_MACRO not in src


def test_emit_callable_wraps_ad_macro_when_real_present():
    hooks = Hooks()
    c = _callable([generate.Param("Standard_Real", "x")], return_spelling="double")
    src = hooks.emit_callable(c, ["real"], "value")
    assert f"#if defined({hooks_module.AD_MACRO})" in src
    assert "occt_plugin::to_real(x)" in src  # the alternate (AD) branch
    assert "Standard_Real x" in src  # the primary (plain) branch keeps the bare spelling


def test_emit_callable_operator_never_gets_ad_wrapping_even_with_real_param():
    # gp_Mat's own operator/(Standard_Real) is a real, not hypothetical, case:
    # an operator whose one parameter genuinely classifies as "real" but must
    # never be dual-emitted - operators are always plain (see
    # emit_operator_lambda's own docstring).
    hooks = Hooks()
    other = generate.Param("const Standard_Real", "theScalar")
    c = generate.Callable("gp_Mat", "operator", "operator/", [other], "gp_Mat", is_const=True,
                           meta_function="division", operator_token="/")
    src = hooks.emit_callable(c, ["real"], "value")
    assert hooks_module.AD_MACRO not in src
    assert src.count("return self / theScalar;") == 1


# ---------------------------------------------------------------------------
# OVERLOAD_AMBIGUITY_KINDS / REVERSIBLE_OPERAND_KINDS
# ---------------------------------------------------------------------------

def test_overload_ambiguity_prefers_fewer_real_kinds_within_same_arity():
    all_real = (_callable([generate.Param("Standard_Real", "a")]), ["real"])
    concrete = (_callable([generate.Param("gp_Ax2", "a")]), ["object"])
    ordered = generate.ambiguity_safe_overload_order([all_real, concrete], Hooks())
    assert ordered == [concrete, all_real]


def test_reversible_operand_kinds_includes_real():
    assert "real" in Hooks().REVERSIBLE_OPERAND_KINDS
    assert "passthrough" in Hooks().REVERSIBLE_OPERAND_KINDS
