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

#include <sol/sol.hpp>

namespace occt_plugin {

#if defined(GRUNK_OCCT_WITH_ADOLC)
inline Standard_Real to_real(sol::object o)
{
    return o.is<Standard_Real>() ? o.as<Standard_Real>() : Standard_Real(o.as<double>());
}
#endif

} // namespace occt_plugin
