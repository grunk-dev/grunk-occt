// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 1: the first hand-written OCCT module (gp - basic geometric primitives), the
// vertical slice that proves the whole registration pattern end to end before the
// code generator (Step 2) automates it. Scope is deliberately narrow: exactly the
// gp_Pnt/gp_Vec/gp_Dir/gp_Ax1/gp_Trsf constructors and member functions the two
// grocc-only target integration recipes (reference/legacy-recipes/cavity_model.grr.yml,
// tank_model.grr.yml) actually call, plus X/Y/Z accessors for a meaningful smoke test -
// not full coverage of each type's API (that's Step 6's module expansion sweep).
//
// automagic_flags::none is used throughout (see DESIGN.md's "automagic_flags policy"):
// none of these OCCT value types declare operator==/operator<, which sol2's default
// ("automagic") usertype registration wants to synthesize comparisons/other
// metamethods around.
//
// Every constructor/member function taking a bare Standard_Real parameter is
// registered as an #ifdef GRUNK_OCCT_WITH_ADOLC-gated pair (see occt_ad_support.hpp
// and DESIGN.md's "Standard_Real / AD-portability rule") - only the plain branch is
// exercised today.

#pragma once

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>

#include <grunk/dynamic.hpp>

#include "../occt_ad_support.hpp"

inline void register_gp(grunk::plugin_namespace& ns)
{
    ns.register_type<gp_Pnt, sol::automagic_flags::none>("gp_Pnt")
        .add_constructors(
            []() { return gp_Pnt(); },
#if defined(GRUNK_OCCT_WITH_ADOLC)
            [](sol::object x, sol::object y, sol::object z) {
                return gp_Pnt(occt_plugin::to_real(x), occt_plugin::to_real(y), occt_plugin::to_real(z));
            }
#else
            [](Standard_Real x, Standard_Real y, Standard_Real z) { return gp_Pnt(x, y, z); }
#endif
        )
        .add_member_function("X", &gp_Pnt::X)
        .add_member_function("Y", &gp_Pnt::Y)
        .add_member_function("Z", &gp_Pnt::Z);

    ns.register_type<gp_Dir, sol::automagic_flags::none>("gp_Dir")
        .add_constructors(
            []() { return gp_Dir(); },
#if defined(GRUNK_OCCT_WITH_ADOLC)
            [](sol::object x, sol::object y, sol::object z) {
                return gp_Dir(occt_plugin::to_real(x), occt_plugin::to_real(y), occt_plugin::to_real(z));
            }
#else
            [](Standard_Real x, Standard_Real y, Standard_Real z) { return gp_Dir(x, y, z); }
#endif
        )
        .add_member_function("X", &gp_Dir::X)
        .add_member_function("Y", &gp_Dir::Y)
        .add_member_function("Z", &gp_Dir::Z);

    ns.register_type<gp_Vec, sol::automagic_flags::none>("gp_Vec")
        .add_constructors(
            []() { return gp_Vec(); },
            [](gp_Dir const& dir) { return gp_Vec(dir); },
#if defined(GRUNK_OCCT_WITH_ADOLC)
            [](sol::object x, sol::object y, sol::object z) {
                return gp_Vec(occt_plugin::to_real(x), occt_plugin::to_real(y), occt_plugin::to_real(z));
            }
#else
            [](Standard_Real x, Standard_Real y, Standard_Real z) { return gp_Vec(x, y, z); }
#endif
        )
        .add_member_function("X", &gp_Vec::X)
        .add_member_function("Y", &gp_Vec::Y)
        .add_member_function("Z", &gp_Vec::Z)
        .add_member_function("Multiplied",
#if defined(GRUNK_OCCT_WITH_ADOLC)
            [](gp_Vec const& self, sol::object scalar) { return self.Multiplied(occt_plugin::to_real(scalar)); }
#else
            &gp_Vec::Multiplied
#endif
        );

    ns.register_type<gp_Ax1, sol::automagic_flags::none>("gp_Ax1")
        .add_constructors(
            []() { return gp_Ax1(); },
            [](gp_Pnt const& location, gp_Dir const& direction) { return gp_Ax1(location, direction); }
        )
        .add_member_function("Location", &gp_Ax1::Location)
        .add_member_function("Direction", &gp_Ax1::Direction);

    ns.register_type<gp_Trsf, sol::automagic_flags::none>("gp_Trsf")
        .add_constructors([]() { return gp_Trsf(); })
        .add_member_function("SetTranslation",
            static_cast<void (gp_Trsf::*)(gp_Vec const&)>(&gp_Trsf::SetTranslation))
        .add_member_function("SetRotation",
#if defined(GRUNK_OCCT_WITH_ADOLC)
            [](gp_Trsf& self, gp_Ax1 const& axis, sol::object angle) { self.SetRotation(axis, occt_plugin::to_real(angle)); }
#else
            // gp_Trsf::SetRotation is overloaded (also takes a gp_Quaternion) - the
            // explicit member-pointer type picks the (gp_Ax1 const&, Standard_Real)
            // overload used by the target recipes.
            static_cast<void (gp_Trsf::*)(gp_Ax1 const&, Standard_Real)>(&gp_Trsf::SetRotation)
#endif
        )
        .add_member_function("SetMirror",
            static_cast<void (gp_Trsf::*)(gp_Pnt const&)>(&gp_Trsf::SetMirror));
}
