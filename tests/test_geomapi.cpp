// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// GeomAPI_PointsToBSplineSurface::Interpolate takes a 2D grid of gp_Pnt via the
// NCollection_Array2<gp_Pnt> <-> std::vector<gp_Pnt> conversion (src/collections.hpp's
// to_array2, same helper Collections.Array2RoundTrip already exercises) - no
// Handle(Geom_Surface)/Handle(Geom_Curve) parameter involved.
//
// GeomAPI_ProjectPointOnCurve's constructor/Init take a `Handle(Geom_Curve) const&`
// (Geom_Curve is abstract - there is no way to obtain one except by upcasting a real
// registered subtype like Handle(Geom_Line)) - originally the exact case that
// surfaced grunk-dev/grunk-occt#1 ("Handle(Derived) does not satisfy a Handle(Base)
// function/constructor parameter"), and did not work from Lua at all. Fixed for real
// 2026-09-29 (see DESIGN.md's "Handle(Derived)->Handle(Base) argument conversion:
// real root cause and fix") - ProjectPointOntoLine below is the regression test for
// the original bug report.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include "../src/occt_ad_support.hpp"

namespace {

class GeomApiTest : public ::testing::Test {
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

grunk::state* GeomApiTest::state_ = nullptr;

} // namespace

TEST_F(GeomApiTest, InterpolateBSplineSurfaceFromPointGrid)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        p = {}
        for i = 1, 9 do
            p[i] = occt.gp_Pnt.new(i * 1.0, i * 2.0, i * 3.0)
        end
        points = occt.gp_Pnt.as_vec(p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9])

        surface_maker = occt.GeomAPI_PointsToBSplineSurface.new()
        surface_maker:Interpolate(points, 3, 3, false)
        is_done = surface_maker:IsDone()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("is_done"));
}

// Regression test for grunk-dev/grunk-occt#1: a Handle(Geom_Line) (concrete,
// registered subtype) passed directly where GeomAPI_ProjectPointOnCurve's
// constructor expects Handle(Geom_Curve) (the abstract base) - no C++-side upcast,
// no escape hatch, just the generated binding.
TEST_F(GeomApiTest, ProjectPointOntoLine)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        origin = occt.gp_Pnt.new(0.0, 0.0, 0.0)
        dir = occt.gp_Dir.new(1.0, 0.0, 0.0)
        line = occt.Geom_Line.new(origin, dir)

        p = occt.gp_Pnt.new(5.0, 3.0, 0.0)
        proj = occt.GeomAPI_ProjectPointOnCurve.new(p, line)
        nb = proj:NbPoints()

        nearest = proj:NearestPoint()
        nx = nearest:X(); ny = nearest:Y(); nz = nearest:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_EQ(env.get<int>("nb"), 1);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("nx")), 5.0);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("ny")), 0.0);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("nz")), 0.0);
}
