// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 3 milestone: Standard_Transient/Handle(T) support + add_bases<...>(), exercised
// on Geom_Line (Transient-derived, array-free - see codegen/config.yml's rationale for
// picking this over Geom_BezierCurve, which needs collections support from Step 5).
//
// Positive path: native colon-call dispatch works on a Handle(Geom_Line)-returned
// feature, including a call to Geom_Curve::Value - a method registered on a BASE
// class, not Geom_Line itself, reached only via .add_bases<Geom_Curve, ...>(). Value()
// internally calls the virtual D0(), which Geom_Line overrides - so this also proves
// virtual dispatch through the base's registered method reaches the derived override.
//
// Negative path: a null Handle(Geom_Geometry) (src/geom/geom_extras.hpp's
// null_geom_geometry) surfaces as Lua nil rather than crashing - exercises
// occt_sol_traits.hpp's is_null directly. Also confirms a genuine Handle(Geom_Line)
// upcast-converts to a Handle(Geom_Geometry) const& parameter (via add_bases) for a
// real DownCast success case.
//
// Out-param path: Geom_Curve::D0(U, gp_Pnt& P) takes a non-const-reference "out"
// parameter - the generator now accepts these when the mutated type is itself a
// registered OCCT object (see codegen/README.md's "Non-const-reference ('out')
// parameters"; the existing gp_Pnt userdata passed in from Lua is mutated in place,
// exactly like OOP mutation works in Lua natively). Called directly here (not
// through a grunk::recipe "module") since this test's create_env() harness is
// already an undecorated/non-parametric environment - the module-wrapping pattern
// only matters once this kind of call needs to happen inside a tracked recipe
// (create_parametric_env(), deferred to Step 6.5).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <cmath>
#include <iostream>

namespace {

bool near(double a, double b, double eps = 1e-9)
{
    return std::fabs(a - b) <= eps;
}

bool check(bool ok, char const* what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
    }
    return ok;
}

} // namespace

int main()
{
    grunk::state state;
    grunk::plugin::load_native(state, OCCT_PLUGIN_SO_PATH);

    auto env = state.create_env();

    // --- Positive path ---
    auto res = env.eval(R"(
        origin = occt.gp_Pnt.new(0., 0., 0.)
        x_axis = occt.gp_Dir.new(1., 0., 0.)
        line = occt.Geom_Line.new(origin, x_axis)

        -- Value() is registered on Geom_Curve, not Geom_Line itself - reachable here
        -- only via add_bases. It calls the virtual D0(), which Geom_Line overrides.
        p = line:Value(2.0)
        px = p:X(); py = p:Y(); pz = p:Z()
    )");
    if (!res.valid()) {
        std::cerr << "FAIL: positive-path eval failed\n";
        return 1;
    }

    bool ok = true;
    ok &= check(near(env.get<double>("px"), 2.) && near(env.get<double>("py"), 0.)
                    && near(env.get<double>("pz"), 0.),
                "Geom_Line via inherited Geom_Curve::Value (add_bases + virtual dispatch)");

    // --- Negative path ---
    // Deliberately never passes a null/nil Handle as a function *argument* here -
    // that path segfaults (sol2 does not safely default-construct a
    // unique_usertype_traits-backed parameter from Lua nil; see DESIGN.md's "null
    // Handle arguments" note). Both checks below only ever look at a *returned*
    // Handle, which is the path occt_sol_traits.hpp's is_null actually guards.
    auto neg_res = env.eval(R"(
        empty = occt.null_geom_geometry()
        empty_is_nil = (empty == nil)

        -- Realistic success path: line (a Handle(Geom_Line)) upcast-converts to the
        -- Handle(Geom_Geometry) const& parameter (via add_bases), DownCast succeeds.
        as_line_again = occt.downcast_to_geom_line(line)
        downcast_ok = (as_line_again ~= nil)
    )");
    if (!neg_res.valid()) {
        std::cerr << "FAIL: negative-path eval failed\n";
        return 1;
    }

    ok &= check(env.get<bool>("empty_is_nil"),
                "null_geom_geometry() surfaces as nil (is_null on a direct function return)");
    ok &= check(env.get<bool>("downcast_ok"),
                "downcast_to_geom_line succeeds on a genuine Geom_Line (upcast via add_bases)");

    // --- Out-param path ---
    auto out_res = env.eval(R"(
        out_p = occt.gp_Pnt.new(-1., -1., -1.)   -- pre-existing, mutable userdata
        line:D0(3.0, out_p)                      -- mutates out_p in place, returns nothing
        out_px = out_p:X(); out_py = out_p:Y(); out_pz = out_p:Z()
    )");
    if (!out_res.valid()) {
        std::cerr << "FAIL: out-param eval failed\n";
        return 1;
    }
    ok &= check(near(env.get<double>("out_px"), 3.) && near(env.get<double>("out_py"), 0.)
                    && near(env.get<double>("out_pz"), 0.),
                "Geom_Curve::D0's non-const-reference gp_Pnt& out-param mutates in place");

    if (!ok) {
        return 1;
    }

    std::cout << "OK: geom module (Standard_Transient/Handle(T), add_bases, null-Handle safety) verified\n";
    return 0;
}
