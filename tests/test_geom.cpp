// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 3 milestone: Standard_Transient/Handle(T) support + add_bases<...>(), exercised
// on Geom_Line (Transient-derived, array-free - see codegen/config.yml's rationale for
// picking this over Geom_BezierCurve, which needs collections support from Step 5).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class GeomTest : public ::testing::Test {
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

grunk::state* GeomTest::state_ = nullptr;

} // namespace

// Positive path: native colon-call dispatch works on a Handle(Geom_Line)-returned
// feature, including a call to Geom_Curve::Value - a method registered on a BASE
// class, not Geom_Line itself, reached only via .add_bases<Geom_Curve, ...>(). Value()
// internally calls the virtual D0(), which Geom_Line overrides - so this also proves
// virtual dispatch through the base's registered method reaches the derived override.
TEST_F(GeomTest, ValueViaInheritedGeomCurve)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        origin = occt.gp_Pnt.new(0., 0., 0.)
        x_axis = occt.gp_Dir.new(1., 0., 0.)
        line = occt.Geom_Line.new(origin, x_axis)

        p = line:Value(2.0)
        px = p:X(); py = p:Y(); pz = p:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_DOUBLE_EQ(env.get<double>("px"), 2.);
    EXPECT_DOUBLE_EQ(env.get<double>("py"), 0.);
    EXPECT_DOUBLE_EQ(env.get<double>("pz"), 0.);
}

// Negative path: a null Handle(Geom_Geometry) (src/geom/geom_extras.hpp's
// null_geom_geometry) surfaces as Lua nil rather than crashing - exercises
// occt_sol_traits.hpp's is_null directly. Also confirms a genuine Handle(Geom_Line)
// upcast-converts to a Handle(Geom_Geometry) const& parameter (via add_bases) for a
// real DownCast success case. Deliberately never passes a null/nil Handle as a
// function *argument* here - that path segfaults (sol2 does not safely
// default-construct a unique_usertype_traits-backed parameter from Lua nil; see
// DESIGN.md's "Null Handle arguments segfault" note, grunk-dev/grunk#281). Both
// checks below only ever look at a *returned* Handle.
TEST_F(GeomTest, NullHandleSurfacesAsNil)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        origin = occt.gp_Pnt.new(0., 0., 0.)
        x_axis = occt.gp_Dir.new(1., 0., 0.)
        line = occt.Geom_Line.new(origin, x_axis)

        empty = occt.null_geom_geometry()
        empty_is_nil = (empty == nil)

        -- Realistic success path: line (a Handle(Geom_Line)) upcast-converts to the
        -- Handle(Geom_Geometry) const& parameter (via add_bases), DownCast succeeds.
        as_line_again = occt.downcast_to_geom_line(line)
        downcast_ok = (as_line_again ~= nil)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("empty_is_nil"));
    EXPECT_TRUE(env.get<bool>("downcast_ok"));
}

// Out-param path: Geom_Curve::D0(U, gp_Pnt& P) takes a non-const-reference "out"
// parameter - the generator accepts these when the mutated type is itself a
// registered OCCT object (see codegen/README.md's "Non-const-reference ('out')
// parameters"; the existing gp_Pnt userdata passed in from Lua is mutated in place,
// exactly like OOP mutation works in Lua natively). Called directly here (not
// through a grunk::recipe "module") since this test's create_env() harness is
// already an undecorated/non-parametric environment - the module-wrapping pattern
// only matters once this kind of call needs to happen inside a tracked recipe
// (create_parametric_env(), deferred to Step 6.5).
TEST_F(GeomTest, OutParamMutatesInPlace)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        origin = occt.gp_Pnt.new(0., 0., 0.)
        x_axis = occt.gp_Dir.new(1., 0., 0.)
        line = occt.Geom_Line.new(origin, x_axis)

        out_p = occt.gp_Pnt.new(-1., -1., -1.)   -- pre-existing, mutable userdata
        line:D0(3.0, out_p)                      -- mutates out_p in place, returns nothing
        out_px = out_p:X(); out_py = out_p:Y(); out_pz = out_p:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_DOUBLE_EQ(env.get<double>("out_px"), 3.);
    EXPECT_DOUBLE_EQ(env.get<double>("out_py"), 0.);
    EXPECT_DOUBLE_EQ(env.get<double>("out_pz"), 0.);
}
