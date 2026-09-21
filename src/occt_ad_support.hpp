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
// taken from directly). GRUNK_OCCT_WITH_ADOLC itself is not yet defined by any build
// in this repo (see the migration plan's deferred AD milestone) - only the plain
// branch is ever exercised today; the AD branch exists so that milestone is a
// recompile, not a source rewrite.

#pragma once

#include <grunk/dynamic.hpp>

#include <sol/sol.hpp>

#include <stdexcept>
#include <string>

namespace occt_plugin {

#if defined(GRUNK_OCCT_WITH_ADOLC)
inline Standard_Real to_real(sol::object o)
{
    return o.is<Standard_Real>() ? o.as<Standard_Real>() : Standard_Real(o.as<double>());
}
#endif

} // namespace occt_plugin

#if defined(GRUNK_OCCT_WITH_ADOLC)
// Step 7 (AD validation milestone, see DESIGN.md): register_ad_support's whole
// reason to exist is that occt_plugin::to_real's own `o.is<Standard_Real>()` check
// above requires *some* usertype to actually be registered for Standard_Real
// (== Standard_Adouble under this build) - without it, `is<Standard_Real>()` can
// never be true for anything, so every dual-emission overload throughout the
// generated code would silently always take the "else" branch (treat every
// argument as a brand-new value, discarding whatever AD tape/derivative it already
// carried) - not a compile error, a silent correctness bug that would only surface
// as "why is my derivative always 0" much later. This is ported directly from
// grunk's own examples/cpp/cad_autodiff example (geoml_registration.hpp's own
// identical AD-only Standard_Real block) - the same, genuinely reusable
// registration, not anything OCCT-plugin-specific: it exposes exactly the AD
// introspection ADOL-C's own adtl::adouble already gives Lua directly (see
// grunk-adolc's own adtl_plugin.cpp) - getValue/getADValue/setADValue.
//
// getADValue/setADValue's direction index is `unsigned int` in ADOL-C's own API,
// adapted to a plain (non-negative-checked) Lua number here, same as the
// cad_autodiff precedent.
inline void register_standard_real_ad(grunk::plugin_namespace& ns)
{
    ns.register_type<Standard_Real>("Standard_Real")
        .add_constructors(
            [](double v) -> Standard_Real { return Standard_Adouble(v); }
        )
        .add_member_function("getValue", [](Standard_Adouble const& self) -> double {
            return self.getValue();
        })
        .add_member_function("getADValue", [](Standard_Adouble const& self, int direction) -> double {
            if (direction < 0) {
                throw std::invalid_argument("getADValue: direction must be >= 0, got " + std::to_string(direction));
            }
            return self.getADValue(static_cast<unsigned int>(direction));
        })
        .add_member_function("setADValue", [](Standard_Adouble& self, int direction, double value) {
            if (direction < 0) {
                throw std::invalid_argument("setADValue: direction must be >= 0, got " + std::to_string(direction));
            }
            self.setADValue(static_cast<unsigned int>(direction), value);
        });
}
#endif
