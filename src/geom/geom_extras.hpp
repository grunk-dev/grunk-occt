// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch for the geom module - net-new free functions with no
// OCCT header counterpart.
//
// opencascade::handle<T>::DownCast is a static member of the Handle template itself,
// not of the class it's parameterized on - the generator (codegen/generate.py) only
// introspects each module's own class definitions, so it has no way to discover or
// emit this generically (see codegen/README.md's "Known gaps"). Registered here by
// hand so the null-Handle path (a failed/empty DownCast) can be exercised from Lua -
// see tests/test_geom.cpp's negative-path check.
//
// Also opts Geom_Curve into grunk's with_std_vector() (Geom_Curve.as_vec(...)),
// producing a real std::vector<Handle(Geom_Curve)> from Lua-side Handle(Geom_*Curve)
// values (any registered Geom_Curve subtype, not just Geom_Curve itself) - needed by
// grunk-geoml's geoml.interpolate_curves, whose own src/curve_vec_extras.hpp hand-
// rolled this exact vector before grunk gained with_std_vector<VecElement>() support
// for unique-usertype (Handle(X)) element types (2026-09-29, grunk commit 13ab623 -
// see DESIGN.md's "Handle(Derived)->Handle(Base) argument conversion" follow-up).
// Requires "../../generated/sol_base_classes.hpp" (SOL_BASE_CLASSES/SOL_DERIVED_CLASSES
// for the whole Geom_Curve family) visible in *this* translation unit specifically -
// with_std_vector<Handle<T>>() consults sol::base<T>/sol::derive<T> per-TU, same as
// any other Handle(X) push/pull (see usertype_proxy.hpp's own with_std_vector doc
// comment) - occt_plugin.cpp (which calls register_geom_extras) is a hand-written
// file the generator never touches, so it would otherwise never see these.

#pragma once

#include <Geom_Curve.hxx>
#include <Geom_Geometry.hxx>
#include <Geom_Line.hxx>

#include <grunk/dynamic.hpp>

#include "../occt_sol_traits.hpp"
#include "../../generated/sol_base_classes.hpp"

inline void register_geom_extras(grunk::plugin_namespace& ns)
{
    // A genuinely null Handle(Geom_Geometry) - the simplest possible source of the
    // "null Handle surfaces safely as nil" path tests/test_geom.cpp exercises,
    // without needing a second concrete Geom_Geometry subtype registered.
    ns.register_function(
        "null_geom_geometry",
        []() -> Handle(Geom_Geometry) { return Handle(Geom_Geometry)(); }
    );

    // Real OCCT RTTI-based downcast: succeeds (non-null) when the handle actually
    // points to a Geom_Line, returns a null handle otherwise (including when the
    // input itself is already null) - never throws.
    //
    // Takes its Handle(Geom_Geometry) parameter BY VALUE, not by reference - the
    // same "by-value emit_param()" convention codegen/hooks.py already enforces
    // for every *generated* Handle(X) parameter (see this file's own header
    // comment and DESIGN.md's "Handle(Derived)->Handle(Base) argument conversion"
    // entries), needed here for a second, independent reason found only once
    // grunk's own SOL_ALL_SAFETIES_ON define was turned on (see grunk's own
    // DynamicFeature/dynamic CMakeLists.txt): sol2's *checked* unique-usertype
    // getter (SOL_SAFE_GETTER, gated behind that define) validates a Handle(X)
    // argument's own metatable directly, with no base-class/rebind_actual_type
    // awareness at all - unlike the *unchecked* getter, which does consult
    // add_bases/rebind_actual_type and so tolerated a Handle(Derived) argument
    // being bound to a Handle(Base) const& parameter just fine before. Passing
    // by value sidesteps the checked getter's own unique-usertype branch
    // entirely (a byvalue argument's ownership - and thus its exact type - is
    // resolved before the checked-getter machinery this function's own
    // by-reference form triggered), the same way the generator's own hand-off
    // already worked around this. A hand-written function like this one is
    // exactly the kind of escape hatch that can drift from the generator's own
    // convention unnoticed - this was caught only because the plugin generation
    // itself is used as a stress test for grunk core's own default settings.
    ns.register_function(
        "downcast_to_geom_line",
        [](Handle(Geom_Geometry) geometry) -> Handle(Geom_Line) {
            return Handle(Geom_Line)::DownCast(geometry);
        }
    );

    ns.modify_type<Geom_Curve>("Geom_Curve").with_std_vector<opencascade::handle<Geom_Curve>>();
}
