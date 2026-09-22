// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch - grunk's with_std_vector() (Foo.as_vec(...)/
// Foo.new_vec(), see grunk/dynamic/usertype_proxy.hpp) is a grunk-level
// convenience the generator does not emit for any type yet (see
// codegen/README.md's "Known gaps"); opted in here, by hand, for gp_Pnt - needed to
// collect points into a std::vector<gp_Pnt> from Lua before converting it into an
// OCCT array via collections.hpp's to_array1(), e.g. for Geom_BezierCurve's own
// constructor.
//
// gp_Pnt/gp_Vec/gp_Dir also get a sol::meta_function::to_string metamethod here -
// another grunk-level convenience (see grunk's own tests/cpp/recipe_simple.cpp,
// TEST(Recipe, simple_userdata_parameters)) the generator does not emit for any
// type. Without it, a userdata-typed value - e.g. any of these three types, all of
// which examples/cavity_model.grr.yml and examples/tank_model.grr.yml already use
// as independent (default-valued) sub-recipe parameters, not just step-local
// intermediates - cannot be round-tripped through recipe.to_string()/grunk.write():
// grunk::serialize (grunk/core/parametric_core.hpp) requires a tostring metamethod
// on every userdata value it serializes, throwing "Cannot serialize an opaque
// type ..." otherwise. The string produced is a Lua expression that reconstructs
// the value when read back - "occt.gp_Pnt.new(1.0, 2.0, 3.0)" - the same
// Type.new(...) form these types' own constructors already use, and the same form
// already written by hand into these recipes' own parameters: blocks; ut.name is
// already the fully plugin-qualified name (e.g. "occt.gp_Pnt" - see
// grunk/dynamic/state.hpp's register_type/modify_type), not just "gp_Pnt".
// grunk::to_string(double) (grunk/core/parametric_core.hpp) is reused for each
// coordinate for the same reason recipe_simple.cpp's own MyScalar example does -
// consistent, round-trip-safe numeric formatting (e.g. always keeping ".0" on a
// whole number) rather than hand-rolling it again here. X()/Y()/Z() return
// Standard_Real, which is Standard_Adouble (not a plain double) under an AD build
// (GRUNK_OCCT_WITH_ADOLC) - occt_ad_support.hpp's to_double(Standard_Real) is what
// makes this same source compile against both build variants, same as everywhere
// else in this project that touches a Standard_Real (see its own AD-portability
// rule comment).
//
// Uses modify_type, not register_type, throughout: gp_Pnt/gp_Vec/gp_Dir are already
// registered generically by the generator (generated/gp.cpp) - this only adds
// members on top of those existing registrations, it does not re-register the
// types (register_gp must run first - see occt_plugin.cpp's ordering).

#pragma once

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <grunk/dynamic.hpp>

#include "../occt_ad_support.hpp"

namespace occt_plugin {

template <typename T>
void register_xyz_to_string(grunk::usertype_proxy<T>& ut)
{
    std::string const name = ut.name;
    ut.set(sol::meta_function::to_string, [name](T const& v) {
        return name + ".new("
            + grunk::to_string(occt_plugin::to_double(v.X())) + ", "
            + grunk::to_string(occt_plugin::to_double(v.Y())) + ", "
            + grunk::to_string(occt_plugin::to_double(v.Z())) + ")";
    });
}

} // namespace occt_plugin

inline void register_gp_extras(grunk::plugin_namespace& ns)
{
    auto pnt = ns.modify_type<gp_Pnt>("gp_Pnt").with_std_vector();
    occt_plugin::register_xyz_to_string(pnt);

    auto vec = ns.modify_type<gp_Vec>("gp_Vec");
    occt_plugin::register_xyz_to_string(vec);

    auto dir = ns.modify_type<gp_Dir>("gp_Dir");
    occt_plugin::register_xyz_to_string(dir);
}
