// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 1 milestone: exercises the gp module registered by src/gp/gp_registration.hpp
// through raw Lua (see DESIGN.md's "test-harness shape") - constructors, accessors,
// gp_Vec::Multiplied, gp_Ax1's constructor-from-(gp_Pnt,gp_Dir), and gp_Trsf's
// SetTranslation/SetRotation/SetMirror overloads (the exact gp surface the two
// grocc-only target integration recipes need - see
// reference/legacy-recipes/{cavity_model,tank_model}.grr.yml).

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
    auto res = env.eval(R"(
        p = occt.gp_Pnt.new(1., 2., 3.)
        px = p:X(); py = p:Y(); pz = p:Z()

        d = occt.gp_Dir.new(1., 0., 0.)
        dx = d:X(); dy = d:Y(); dz = d:Z()

        v_from_dir = occt.gp_Vec.new(d)
        v_from_dir_x = v_from_dir:X()

        v = occt.gp_Vec.new(2., 0., 0.)
        v3 = v:Multiplied(3.)
        v3x = v3:X()

        ax = occt.gp_Ax1.new(p, d)
        ax_loc_x = ax:Location():X()
        ax_dir_x = ax:Direction():X()

        default_pnt = occt.gp_Pnt.new()
        default_pnt_x = default_pnt:X()

        trsf = occt.gp_Trsf.new()
        trsf:SetTranslation(v)
        trsf:SetRotation(ax, 1.5707963267948966)
        trsf:SetMirror(p)
        trsf_ok = true
    )");

    if (!res.valid()) {
        std::cerr << "FAIL: eval failed\n";
        return 1;
    }

    bool ok = true;
    ok &= check(near(env.get<double>("px"), 1.) && near(env.get<double>("py"), 2.)
                    && near(env.get<double>("pz"), 3.),
                "gp_Pnt constructor/X/Y/Z");
    ok &= check(near(env.get<double>("dx"), 1.) && near(env.get<double>("dy"), 0.)
                    && near(env.get<double>("dz"), 0.),
                "gp_Dir constructor/X/Y/Z");
    ok &= check(near(env.get<double>("v_from_dir_x"), 1.), "gp_Vec(gp_Dir) constructor");
    ok &= check(near(env.get<double>("v3x"), 6.), "gp_Vec::Multiplied");
    ok &= check(near(env.get<double>("ax_loc_x"), 1.) && near(env.get<double>("ax_dir_x"), 1.),
                "gp_Ax1(gp_Pnt, gp_Dir) constructor/Location/Direction");
    ok &= check(near(env.get<double>("default_pnt_x"), 0.), "gp_Pnt default constructor");
    ok &= check(env.get<bool>("trsf_ok"),
                "gp_Trsf default constructor / SetTranslation / SetRotation / SetMirror");

    if (!ok) {
        return 1;
    }

    std::cout << "OK: gp module (gp_Pnt, gp_Vec, gp_Dir, gp_Ax1, gp_Trsf) verified\n";
    return 0;
}
