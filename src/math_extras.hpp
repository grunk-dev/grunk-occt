// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch (Step 6.5, see DESIGN.md/migration plan) - grunk's own
// Lua state only opens sol::lib::base (grunk/dynamic/state.hpp's state constructor),
// not sol::lib::math, so plain Lua scripts (a recipe's steps:, or a grunk module)
// have no cos/sin/acos/tan/sqrt/pi available at all. Angle-based positioning (gp_Ax1/gp_Ax2
// construction, gp_Trsf::SetRotation) is common enough in OCCT modeling - the
// examples/ recipes ported from legacy grocc need it directly - that it's simpler to
// expose the handful of trig primitives actually needed here than to work around
// their absence with pre-computed literal parameters (which would make derived
// positions stop reacting to the parameter they're actually derived from, defeating
// the point of a parametric recipe). Not literally an OCCT-specific type/function
// (no corresponding OCCT header), same "net-new free function" category as
// shape_io.hpp's own export_brep - kept here rather than in one of the ported
// examples themselves since any future recipe could equally need it.
//
// error() below is a related but separate gap: it's not that sol::lib::base isn't
// loaded (it is - error/assert are genuinely part of it), it's that a recipe's own
// "decorated"/parametric Lua environment (steps:/modules:) falls back to grunk's own
// registered-symbol table, not to Lua's base library at all, for anything not found
// there first (see tests/test_load.cpp's own comment on the same restriction for
// assert) - so plain Lua globals like error/assert simply aren't reachable from a
// recipe or module either, the same as cos/sin/pi above. occt.error(msg) throws a
// real C++ exception (std::runtime_error), which grunk's own dynamic-action
// evaluation already turns into a normal, catchable Lua error either way (confirmed
// via examples/plugins/occt_helpers.lua's own compound(), the first caller that
// needed to fail loudly - a null/empty shape reaching TopoDS_Builder::Add crashes
// the whole process outright instead of raising anything at all).

#pragma once

#include <grunk/dynamic.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

inline void register_math_extras(grunk::plugin_namespace& ns)
{
    ns.register_function("cos", [](double x) { return std::cos(x); });
    ns.register_function("sin", [](double x) { return std::sin(x); });
    ns.register_function("acos", [](double x) { return std::acos(x); });
    ns.register_function("tan", [](double x) { return std::tan(x); });
    ns.register_function("sqrt", [](double x) { return std::sqrt(x); });
    // M_PI is a POSIX/glibc extension, not standard C++ (undefined on MSVC without
    // _USE_MATH_DEFINES) - std::acos(-1.0) is the portable way to get pi from <cmath>.
    ns.register_function("pi", []() { return std::acos(-1.0); });
    ns.register_function("error", [](std::string const& message) -> void {
        throw std::runtime_error(message);
    });
}
