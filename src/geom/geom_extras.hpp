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

#pragma once

#include <Geom_Geometry.hxx>
#include <Geom_Line.hxx>

#include <grunk/dynamic.hpp>

#include "../occt_sol_traits.hpp"

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
    ns.register_function(
        "downcast_to_geom_line",
        [](Handle(Geom_Geometry) const& geometry) -> Handle(Geom_Line) {
            return Handle(Geom_Line)::DownCast(geometry);
        }
    );
}
