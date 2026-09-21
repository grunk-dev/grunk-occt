// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// AD-portability rule (see DESIGN.md): every constructor/function parameter of type
// Standard_Real is registered as an #ifdef GRUNK_OCCT_WITH_ADOLC-gated pair - a plain
// Standard_Real overload for ordinary builds (Standard_Real == double, a fundamental
// type sol2 binds a Lua number to directly, no indirection), and a sol::object-based
// mixed-conversion overload for a future AD-enabled build (Standard_Real ==
// Standard_Adouble there, not a fundamental type, so a single overload can't bind a
// call mixing an already-AD-tracked argument with a plain Lua number literal - see
// grunk's cad_autodiff example, plugins/geoml_registration.hpp, which this pattern is
// taken from directly).

#pragma once

#include <grunk/dynamic.hpp>

#include <sol/sol.hpp>

#include <utility>

// Standard_Real itself (a plain typedef for double, unaffected by
// GRUNK_OCCT_WITH_ADOLC) - included unconditionally so to_real()/to_double() below
// are usable from a file that never otherwise names an OCCT header (e.g. a test
// file driving occt_plugin purely through Lua strings - see tests/test_gp.cpp and
// friends), not just from generated/*.cpp, which always transitively has it via
// whatever real OCCT headers it includes first.
#include <Standard_TypeDef.hxx>

#if defined(GRUNK_OCCT_WITH_ADOLC)
#include <adolc/adtl.h>
#endif

namespace occt_plugin {

// Converts a Lua argument to Standard_Real, preserving an AD-tracked value if one
// is present - used throughout generated/*.cpp's own #ifdef GRUNK_OCCT_WITH_ADOLC
// dual-emission overloads (see DESIGN.md's AD-portability rule). Available
// unconditionally (not just under GRUNK_OCCT_WITH_ADOLC, like it used to be): in a
// plain build Standard_Real is just double, so this is a trivial passthrough - but
// having it exist either way means callers (to_double() below, and the test suite
// via that) don't need their own #ifdef to build the same source against both
// variants.
inline Standard_Real to_real(sol::object o)
{
#if defined(GRUNK_OCCT_WITH_ADOLC)
    // Standard_Real (== Standard_Adouble under this build) is *not* the same C++
    // type as ADOL-C's own adtl::adouble, contrary to what an earlier, unverified
    // version of this comment assumed (see DESIGN.md's "Reusing one shared
    // Standard_Real/adtl::adouble registration, not two" for the history) -
    // adOCCT's own Standard_Adouble.hxx defines `class Standard_Adouble : public
    // adtl::adouble { ...adds ostream/istream operators OCCT's own I/O expects,
    // nothing else... }`, a real (if thin) derived class, not a typedef/alias.
    // Confirmed once adOCCT was actually available to build against - it wasn't,
    // when that first version of this file was written. Two consequences:
    //
    // 1. register_standard_adouble() below (called from register_occt(), src/
    //    occt_plugin.cpp) registers Standard_Adouble as its *own* sol2 usertype,
    //    with .add_bases<adtl::adouble>() so Lua code can pass one where the other
    //    is expected. grunk-adolc's own plugin still owns adtl::adouble's own
    //    registration (constructors, arithmetic, adtl.sin/cos/...) - this only adds
    //    the OCCT-specific derived type on top, the same way any other OCCT
    //    Transient/Handle base chain is registered.
    // 2. This function has to accept *both* an already-Standard_Real-typed value
    //    (e.g. one that round-tripped through another OCCT call) and a plain
    //    adtl::adouble one (e.g. fresh from adtl.adouble.new(...) or adtl.sin(...)
    //    - grunk-adolc's own functions only ever produce adtl::adouble, never
    //    Standard_Adouble, since grunk-adolc has no OCCT dependency to know that
    //    type exists at all) - constructing Standard_Real(adtl::adouble) preserves
    //    the seeded AD direction (Standard_Adouble's own converting constructor
    //    from adtl::adouble does a real copy, not a value-only truncation);
    //    falling through to Standard_Real(o.as<double>()) the way an unrecognized
    //    type does would silently lose it instead, precisely the failure mode this
    //    whole function exists to prevent.
    //
    // There's still no runtime check that grunk-adolc's plugin is actually loaded
    // before any of this runs (see occt_plugin.cpp's own register_occt() comment
    // for why an earlier, stricter check was removed, and grunk-dev/grunk#287 for
    // the upstream fix this is waiting on) - if it isn't, neither
    // is<Standard_Real>() nor is<adtl::adouble>() below can ever be true, and every
    // call silently takes the plain-double conversion branch instead of failing
    // loudly.
    if (o.is<Standard_Real>()) {
        return o.as<Standard_Real>();
    }
    if (o.is<adtl::adouble>()) {
        return Standard_Real(o.as<adtl::adouble>());
    }
#endif
    return Standard_Real(o.as<double>());
}

// Reads a Lua value back as a plain double, regardless of build - just the value
// itself in a plain build (Standard_Real already is double there, so to_real() is
// enough on its own), or the AD build's own "primal value"
// (adtl::adouble::getValue(), a real, non-explicit accessor - not the *explicit*
// operator double() conversions adtl::adouble also has, which don't participate in
// ordinary copy-initialization/argument passing, so can't be relied on implicitly)
// otherwise. The test suite uses this instead of sol::object::as<double>() (which
// has no way to convert a registered Standard_Adouble/adtl::adouble usertype to a
// fundamental double under AD) so the exact same test source works, unmodified,
// against both build variants.
inline double to_double(sol::object o)
{
#if defined(GRUNK_OCCT_WITH_ADOLC)
    return to_real(std::move(o)).getValue();
#else
    return to_real(std::move(o));
#endif
}

#if defined(GRUNK_OCCT_WITH_ADOLC)
// .add_bases<adtl::adouble>() (below) covers plain member-function/__index
// dispatch (colon-calls like x:getValue(), and to_double()'s own to_real(...)
// call) - Standard_Adouble genuinely inherits every one of adtl::adouble's own
// methods in C++, so grunk's DynamicFeature lookup finds them straight through
// the base-class chain. It does *not* cover sol2 metamethods (operator overloads)
// though - those are per-usertype metatable entries, never inherited via
// add_bases, so each one Lua code actually needs has to be registered again here,
// explicitly, even though the underlying C++ operator itself is inherited too and
// needs no forwarding logic beyond calling it. Found the hard way: grunk's own
// recipe/DAG layer builds its dependency graph through Lua arithmetic on feature
// values (grunk.__dynamic_mul and friends), which a Standard_Adouble-valued
// feature can't participate in without its own sol::meta_function::multiplication
// etc. - a plain colon-call/to_double() round-trip never exercises this at all.
//
// Both a Standard_Adouble operand and a plain Lua-number (double) operand are
// covered, on either side for the non-commutative operators (-, /, <, <=) -
// mirrors adtl::adouble's own overload set (adtl.h has the same member + friend
// free-function split, adouble op double *and* double op adouble). "==" only ever
// needs one direction here since sol2/Lua already try the metatable of whichever
// operand has one, regardless of position, for a *commutative* comparison.
inline void register_standard_adouble(grunk::plugin_namespace& ns)
{
    ns.register_type<Standard_Adouble, sol::automagic_flags::none>("Standard_Adouble")
        .add_bases<adtl::adouble>()
        .add_member_function(sol::meta_function::addition, sol::overload(
            [](Standard_Adouble const& self, Standard_Adouble const& other) -> Standard_Adouble { return self + other; },
            [](Standard_Adouble const& self, double other) -> Standard_Adouble { return self + other; },
            [](double other, Standard_Adouble const& self) -> Standard_Adouble { return other + self; }
        ))
        .add_member_function(sol::meta_function::subtraction, sol::overload(
            [](Standard_Adouble const& self, Standard_Adouble const& other) -> Standard_Adouble { return self - other; },
            [](Standard_Adouble const& self, double other) -> Standard_Adouble { return self - other; },
            [](double other, Standard_Adouble const& self) -> Standard_Adouble { return other - self; }
        ))
        .add_member_function(sol::meta_function::multiplication, sol::overload(
            [](Standard_Adouble const& self, Standard_Adouble const& other) -> Standard_Adouble { return self * other; },
            [](Standard_Adouble const& self, double other) -> Standard_Adouble { return self * other; },
            [](double other, Standard_Adouble const& self) -> Standard_Adouble { return other * self; }
        ))
        .add_member_function(sol::meta_function::division, sol::overload(
            [](Standard_Adouble const& self, Standard_Adouble const& other) -> Standard_Adouble { return self / other; },
            [](Standard_Adouble const& self, double other) -> Standard_Adouble { return self / other; },
            [](double other, Standard_Adouble const& self) -> Standard_Adouble { return other / self; }
        ))
        .add_member_function(sol::meta_function::unary_minus, [](Standard_Adouble const& self) -> Standard_Adouble { return -self; })
        .add_member_function(sol::meta_function::equal_to, sol::overload(
            [](Standard_Adouble const& self, Standard_Adouble const& other) -> bool { return self == other; },
            [](Standard_Adouble const& self, double other) -> bool { return self == other; }
        ))
        .add_member_function(sol::meta_function::less_than, sol::overload(
            [](Standard_Adouble const& self, Standard_Adouble const& other) -> bool { return self < other; },
            [](Standard_Adouble const& self, double other) -> bool { return self < other; },
            [](double other, Standard_Adouble const& self) -> bool { return other < self; }
        ))
        .add_member_function(sol::meta_function::less_than_or_equal_to, sol::overload(
            [](Standard_Adouble const& self, Standard_Adouble const& other) -> bool { return self <= other; },
            [](Standard_Adouble const& self, double other) -> bool { return self <= other; },
            [](double other, Standard_Adouble const& self) -> bool { return other <= self; }
        ));
}
#endif

} // namespace occt_plugin
