// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch (see DESIGN.md/codegen/README.md's "NCollection_Array1/2
// support") - occt.gp_Pnt.as_vec(...) (grunk's own with_std_vector(), Geom_BSplineCurve's
// own Poles constructor argument) only ever builds std::vector<VecElement> from
// *userdata* arguments (it explicitly rejects anything else - see grunk/dynamic/
// usertype_proxy.hpp's own with_std_vector()/convert_one comment), which is exactly
// right for a registered usertype element like gp_Pnt but can't work at all for a
// fundamental-typed one (double/Standard_Integer) - a plain Lua number is never
// userdata. A bare Lua table (`{0.0, 1.0, 2.0, 3.0}`) looks like the obvious
// alternative for those, and sol2 *does* support automatically converting one table
// argument to std::vector<double>/std::vector<int> - but only the *first* such
// argument in a given call; found the hard way, empirically, with a minimal
// standalone sol2 repro (not specific to this project's own generated code or to
// AD): a function taking two separate std::vector<double> const& parameters (e.g.
// Geom_BSplineCurve's own (Poles, Knots, Multiplicities, Degree) constructor has
// *two* primitive-element arrays, Knots and Multiplicities) accepts a table for the
// first one and rejects a table for the second with "expected userdata, received
// table" - a real sol2 limitation, not a bug in the code generator's own array1/
// array2 support (codegen/README.md's "NCollection_Array1/2 support") itself.
//
// real_vec/int_vec below sidestep this the same way occt.gp_Pnt.as_vec already does
// for a registered type: build the std::vector<T> in a real bound C++ function
// (taking sol::variadic_args, not a sol::table, so every element is read as a plain
// Lua number, not required to already be userdata) and return it - sol2's own
// automatic container support turns *that* return value into proper userdata, which
// then binds to a std::vector<T> const& parameter directly, with no ambiguous table
// conversion involved at all. occt.gp_Pnt.as_vec(...)/occt.real_vec(...)/
// occt.int_vec(...) compose freely as a result: pass Knots as
// occt.real_vec(0.0, 1.0, ...) and Multiplicities as occt.int_vec(3, 1, ...)
// alongside Poles as occt.gp_Pnt.as_vec(...) in the same call.

#pragma once

#include <grunk/dynamic.hpp>

#include <sol/sol.hpp>

#include <vector>

inline void register_vector_extras(grunk::plugin_namespace& ns)
{
    ns.register_function("real_vec", [](sol::variadic_args va) {
        std::vector<double> v;
        v.reserve(va.size());
        for (auto const& a : va) {
            v.push_back(a.get<double>());
        }
        return v;
    });
    ns.register_function("int_vec", [](sol::variadic_args va) {
        std::vector<int> v;
        v.reserve(va.size());
        for (auto const& a : va) {
            v.push_back(a.get<int>());
        }
        return v;
    });
}
