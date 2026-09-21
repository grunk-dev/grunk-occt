// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 6 milestone: first module expansion sweep module (see DESIGN.md/migration
// plan). Bnd_Box is a plain value type (Add(gp_Pnt const&), CornerMin/CornerMax
// returning gp_Pnt, ...); BRepBndLib::Add(shape, box&, ...) mutates the existing
// Bnd_Box userdata passed in - the same non-const-reference-to-a-registered-object
// out-param pattern as Step 3's Geom_Curve::D0 (codegen/README.md's
// "Non-const-reference ('out') parameters"), exercised here on a real box shape
// rather than a gp_Pnt.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include "../src/occt_ad_support.hpp"

namespace {

class BndTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        state_ = new grunk::state();
#if defined(GRUNK_OCCT_WITH_ADOLC)
        // Shared Standard_Real/adtl::adouble registration (see ../DESIGN.md's
        // "Reusing one shared Standard_Real/adtl::adouble registration, not two") -
        // grunk-adolc's own plugin has to be loaded into this same grunk::state
        // before anything below can read/write occt.Standard_Real correctly.
        grunk::plugin::load_native(*state_, ADTL_PLUGIN_SO_PATH);
#endif
        grunk::plugin::load_native(*state_, OCCT_PLUGIN_SO_PATH);
    }

    static void TearDownTestSuite()
    {
        delete state_;
        state_ = nullptr;
    }

    static grunk::state* state_;
};

grunk::state* BndTest::state_ = nullptr;

} // namespace

TEST_F(BndTest, BoundingBoxOfBoxShape)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box = occt.BRepPrimAPI_MakeBox.new(10., 20., 30.):Shape()

        bbox = occt.Bnd_Box.new()
        is_void_before = bbox:IsVoid()

        occt.BRepBndLib_Add(box, bbox, true)
        is_void_after = bbox:IsVoid()

        cmin = bbox:CornerMin()
        cmax = bbox:CornerMax()

        cminx = cmin:X(); cminy = cmin:Y(); cminz = cmin:Z()
        cmaxx = cmax:X(); cmaxy = cmax:Y(); cmaxz = cmax:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("is_void_before"));
    EXPECT_FALSE(env.get<bool>("is_void_after"));

    // BRepBndLib::Add enlarges the box by the shape's own tolerance-derived "gap"
    // (documented on Add itself) - corners land a hair outside the exact box
    // dimensions, not exactly on them, hence EXPECT_NEAR rather than
    // EXPECT_DOUBLE_EQ (confirmed empirically: off by ~1e-7).
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("cminx")), 0., 1e-4);
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("cminy")), 0., 1e-4);
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("cminz")), 0., 1e-4);

    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("cmaxx")), 10., 1e-4);
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("cmaxy")), 20., 1e-4);
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("cmaxz")), 30., 1e-4);
}

// Bnd_Box's own value-type API (Set/Add(gp_Pnt)/IsOut), independent of any shape -
// confirms Bnd_Box works as an ordinary registered value type, not just as
// BRepBndLib's out-param destination.
TEST_F(BndTest, DirectPointBoundsAndIsOut)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        bbox = occt.Bnd_Box.new()
        bbox:Set(occt.gp_Pnt.new(0., 0., 0.))
        bbox:Add(occt.gp_Pnt.new(5., 5., 5.))

        inside = occt.gp_Pnt.new(1., 1., 1.)
        outside = occt.gp_Pnt.new(100., 100., 100.)

        inside_is_out = bbox:IsOut(inside)
        outside_is_out = bbox:IsOut(outside)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<bool>("inside_is_out"));
    EXPECT_TRUE(env.get<bool>("outside_is_out"));
}
