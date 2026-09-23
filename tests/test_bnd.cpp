// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Bnd_Box is a plain value type (Add(gp_Pnt const&), CornerMin/CornerMax
// returning gp_Pnt, ...); BRepBndLib::Add(shape, box&, ...) mutates the existing
// Bnd_Box userdata passed in - the same non-const-reference-to-a-registered-object
// out-param pattern as Geom_Curve::D0 (codegen/README.md's "Non-const-reference
// ('out') parameters"), exercised here on a real box shape rather than a gp_Pnt.

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

// Bnd_OBB - one of the classes the bnd module gained once its header glob
// widened from Bnd_Box.hxx alone to Bnd_*.hxx. Built directly from a Bnd_Box,
// then checked that its center lands at the box's own midpoint.
TEST_F(BndTest, OBBFromBoxCentersOnBoxMidpoint)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box = occt.Bnd_Box.new()
        box:Set(occt.gp_Pnt.new(0., 0., 0.))
        box:Add(occt.gp_Pnt.new(10., 20., 30.))

        obb = occt.Bnd_OBB.new(box)
        center = obb:Center()
        center_x = center:X(); center_y = center:Y(); center_z = center:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("center_x")), 5., 1e-4);
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("center_y")), 10., 1e-4);
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("center_z")), 15., 1e-4);
}

// Bnd_Sphere - another class from the same widening; a simple value type with
// its own Radius()/Center() accessors, independent of any shape.
TEST_F(BndTest, SphereRadiusAndCenterMatchConstructorArguments)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        center = occt.gp_XYZ.new(1., 2., 3.)
        sphere = occt.Bnd_Sphere.new(center, 4.5, 0, 0)
        radius = sphere:Radius()
        c = sphere:Center()
        cx = c:X(); cy = c:Y(); cz = c:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("radius")), 4.5);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("cx")), 1.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("cy")), 2.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("cz")), 3.);
}

// Bnd_Box2d - the 2D counterpart to Bnd_Box, also newly reachable from the same
// widening (and, transitively, what made BRepTools::UVBounds's own extra_includes
// fix necessary - see codegen/config.yml's breptools module).
TEST_F(BndTest, Box2dUpdateGrowsToIncludeGivenPoint)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        bbox = occt.Bnd_Box2d.new()
        bbox:Update(0., 0.)
        bbox:Update(5., 7.)

        pnt = occt.gp_Pnt2d.new(2., 2.)
        pnt_is_out = bbox:IsOut(pnt)

        far_pnt = occt.gp_Pnt2d.new(100., 100.)
        far_pnt_is_out = bbox:IsOut(far_pnt)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<bool>("pnt_is_out"));
    EXPECT_TRUE(env.get<bool>("far_pnt_is_out"));
}
