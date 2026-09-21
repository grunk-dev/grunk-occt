// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// AD validation milestone (Step 7, see ../DESIGN.md): only built when
// GRUNK_OCCT_ENABLE_AD is ON (see ../CMakeLists.txt), in which case occt_plugin was
// compiled against an AD-enabled OCCT fork (adOCCT) instead of stock OCCT, and
// checks a real forward-mode AD derivative against a closed-form value - mirrors
// grunk's own cad_autodiff example (its README's "Verifying the derivative" section,
// the (1-u)^3 cubic-Bezier-weight check), but exercises grunk-occt's own *generated*
// Geom_BezierCurve/gp_Pnt bindings directly (nothing hand-written), and uses this
// repo's own round control-point numbers rather than cad_autodiff's - only the
// methodology (not the specific figures) is carried over.
//
// This is otherwise the exact same test-harness shape as ../tests/test_geom.cpp
// (grunk::state + create_env() + raw Lua via env.eval(), see its own doc comment for
// why: the recipe/YAML layer isn't exercised by any AD test in this repo yet). The
// one difference: occt.Standard_Real itself is a real, distinct Lua usertype under
// this build (register_standard_real_ad, see ../src/occt_ad_support.hpp) - it does
// not exist at all in a plain (non-AD) build, so a plain double can't carry a seeded
// AD direction the way occt.Standard_Real.new(...):setADValue(...) does here.
//
// OCCT_PLUGIN_SO_PATH is the same compile definition every other test file here
// uses (see ../CMakeLists.txt) - occt_plugin is one target either way, GRUNK_OCCT_
// ENABLE_AD only changes what it's linked against, not its name.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class BezierAdTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        state_ = new grunk::state();
        grunk::plugin::load_native(*state_, OCCT_PLUGIN_SO_PATH);
    }

    static void TearDownTestSuite()
    {
        delete state_;
        state_ = nullptr;
    }

    static grunk::state* state_;
};

grunk::state* BezierAdTest::state_ = nullptr;

} // namespace

// Cubic (4-pole) Bezier curve, X0 (pole 0's X coordinate) seeded as an AD variable
// in forward-mode direction 0, every other pole coordinate a plain Lua number. At
// u=0.5 the four cubic Bernstein weights are (0.125, 0.375, 0.375, 0.125) - pole 0's
// weight, 0.125, is exactly dx(0.5)/dX0 (the curve is affine in each pole
// coordinate, so this holds regardless of X0's actual seeded value). dy(0.5)/dX0
// must be exactly 0: X0 only ever appears in a pole's X coordinate, never Y, so a
// nonzero value here would mean a direction leaked across coordinates - a real bug
// class (e.g. gp_Pnt's own constructor overload picking up the wrong parameter, or a
// single shared AD tape variable getting reused by mistake) that a same-value,
// single-coordinate check like cad_autodiff's own could not by itself rule out.
TEST_F(BezierAdTest, DerivativeMatchesClosedFormBernsteinWeight)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        X0 = occt.Standard_Real.new(1.0)
        X0:setADValue(0, 1.0)

        p0 = occt.gp_Pnt.new(X0, 0.0, 0.0)
        p1 = occt.gp_Pnt.new(2.0, 1.0, 0.0)
        p2 = occt.gp_Pnt.new(3.0, 1.0, 0.0)
        p3 = occt.gp_Pnt.new(4.0, 1.0, 0.0)

        poles = occt.gp_Pnt.as_vec(p0, p1, p2, p3)
        curve = occt.Geom_BezierCurve.new(poles)

        mid = curve:Value(0.5)
        mx = mid:X()
        my = mid:Y()

        mx_value = mx:getValue()
        mx_deriv = mx:getADValue(0)
        my_value = my:getValue()
        my_deriv = my:getADValue(0)
    )");
    ASSERT_TRUE(res.valid());

    // x(0.5) = 0.125*X0 + 0.375*2 + 0.375*3 + 0.125*4, X0 = 1.0
    EXPECT_NEAR(env.get<double>("mx_value"), 2.5, 1e-9);
    EXPECT_NEAR(env.get<double>("mx_deriv"), 0.125, 1e-9);

    // y(0.5) = 0.125*0 + 0.375*1 + 0.375*1 + 0.125*1 - constant in X0
    EXPECT_NEAR(env.get<double>("my_value"), 0.875, 1e-9);
    EXPECT_NEAR(env.get<double>("my_deriv"), 0.0, 1e-9);
}
