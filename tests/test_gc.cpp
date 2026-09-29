// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// GC_MakeSegment/GC_MakeCircle are registered as part of the geom module
// (generated/geom.cpp), not their own file - see DESIGN.md's "Handle(X) return type
// requires being registered in the same generated .cpp as X itself" for why.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include "../src/occt_ad_support.hpp"

namespace {

class GcTest : public ::testing::Test {
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

grunk::state* GcTest::state_ = nullptr;

} // namespace

TEST_F(GcTest, MakeSegmentBetweenTwoPoints)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        p1 = occt.gp_Pnt.new(0., 0., 0.)
        p2 = occt.gp_Pnt.new(3., 4., 0.)

        maker = occt.GC_MakeSegment.new(p1, p2)
        is_done = maker:IsDone()

        segment = maker:Value()
        -- The trimmed curve's parametrization is arc-length along the underlying
        -- (infinite) line, not normalized [0, 1] - Value(FirstParameter()) is P1,
        -- Value(LastParameter()) is P2 (confirmed empirically: querying at a
        -- literal 1.0 instead lands 1 unit from P1 towards P2, not at P2 itself).
        first = segment:FirstParameter()
        last = segment:LastParameter()
        start = segment:Value(first)
        finish = segment:Value(last)

        sx = start:X(); sy = start:Y(); sz = start:Z()
        fx = finish:X(); fy = finish:Y(); fz = finish:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("is_done"));
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("sx")), 0.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("sy")), 0.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("sz")), 0.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("fx")), 3.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("fy")), 4.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("fz")), 0.);
}

// GC_MakeCircle's axis+radius constructor - Value() returns Handle(Geom_Circle),
// reached via add_bases<Geom_Conic, Geom_Curve, Geom_Geometry, Standard_Transient>()
// for a colon-call to Value(u) (inherited from Geom_Curve).
TEST_F(GcTest, MakeCircleFromAxisAndRadius)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        origin = occt.gp_Pnt.new(0., 0., 0.)
        z_axis = occt.gp_Dir.new(0., 0., 1.)
        axis = occt.gp_Ax1.new(origin, z_axis)

        maker = occt.GC_MakeCircle.new(axis, 5.0)
        is_done = maker:IsDone()

        circle = maker:Value()
        p0 = circle:Value(0.0)
        p0x = p0:X(); p0y = p0:Y(); p0z = p0:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("is_done"));
    // A circle of radius 5 centered at the origin, in the XY plane (normal = Z),
    // passes through (5, 0, 0) at parameter 0.
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("p0x")), 5., 1e-9);
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("p0y")), 0., 1e-9);
    EXPECT_NEAR(occt_plugin::to_double(env.get<sol::object>("p0z")), 0., 1e-9);
}

// GC_MakeArcOfCircle::Value() returns Handle(Geom_TrimmedCurve), fed directly (no
// C++-side upcast, no escape hatch) into BRepBuilderAPI_MakeEdge's Handle(Geom_Curve)
// constructor - src/gc/gc_extras.hpp's own occt.edge_from_arc used to do this upcast
// in plain C++ because the generated binding couldn't; now that grunk-dev/grunk-occt#1
// is fixed (see DESIGN.md's "Handle(Derived)->Handle(Base) argument conversion"), that
// escape hatch is gone and this is the regression test for the native path.
TEST_F(GcTest, ArcToEdgeNoEscapeHatch)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        p1 = occt.gp_Pnt.new(0.0, 0.0, 0.0)
        p_on_arc = occt.gp_Pnt.new(1.0, 1.0, 0.0)
        p2 = occt.gp_Pnt.new(2.0, 0.0, 0.0)

        arc = occt.GC_MakeArcOfCircle.new(p1, p_on_arc, p2)
        curve = arc:Value()
        edge = occt.BRepBuilderAPI_MakeEdge.new(curve):Edge()
        is_null = edge:IsNull()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<bool>("is_null"));
}
