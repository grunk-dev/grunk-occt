// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 6 milestone: eighth module expansion sweep addition (see DESIGN.md/migration
// plan). GeomAPI_PointsToBSplineSurface::Interpolate takes a 2D grid of gp_Pnt via
// the Step 5 NCollection_Array2<gp_Pnt> <-> std::vector<gp_Pnt> conversion
// (src/collections.hpp's to_array2, same helper Collections.Array2RoundTrip already
// exercises) - no Handle(Geom_Surface)/Handle(Geom_Curve) parameter involved.
//
// This module's more obvious smoke test candidate, GeomAPI_ProjectPointOnCurve
// (projecting a point onto a real curve, e.g. a Handle(Geom_Line)), does NOT
// currently work from Lua: its constructor/Init take a `Handle(Geom_Curve) const&`
// (Geom_Curve is abstract - there is no way to obtain one except by upcasting a real
// registered subtype like Handle(Geom_Line)), and passing a Handle(Derived) where a
// Handle(Base) parameter is expected fails at the sol2 dispatch layer with "no
// matching function call takes this number of arguments and the specified types" -
// confirmed empirically (both via the constructor and via a separate :Init() call on
// a default-constructed instance, ruling out a constructor-overload-set-specific
// issue). This is different from - and, unlike - the already-documented Handle(T)
// upcast used for *method dispatch* (test_geom.cpp's ValueViaInheritedGeomCurve,
// calling an inherited method through add_bases), which does work; see DESIGN.md's
// "Handle(Derived) does not satisfy a Handle(Base) function/constructor parameter"
// for the general, currently-unfixed limitation this surfaced.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class GeomApiTest : public ::testing::Test {
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
