// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// The code generator itself was extended (see DESIGN.md/codegen/README.md) to
// lift the one-class-per-header assumption, accept glob header patterns, and
// support plain C-style enums. The "gp" module's config now
// wraps gp_*.hxx wholesale instead of five hand-picked headers, retroactively
// unlocking real, previously-rejected coverage across several other modules too
// (Geom_Line::Lin, BRepPrimAPI_MakeCylinder's positioned constructor,
// GC_MakeCircle's gp_Circ-based constructors, ...) since gp_Lin/gp_Ax2/gp_Circ/
// etc. are now registered types. This file exercises exactly those newly-unlocked
// cases, not gp in isolation (already covered by test_gp.cpp).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include "../src/occt_ad_support.hpp"

namespace {

class GpWildcardTest : public ::testing::Test {
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

grunk::state* GpWildcardTest::state_ = nullptr;

} // namespace

// gp_TrsfForm is a plain C-style enum (gp_TrsfForm.hxx) - the generator's new_enum
// support (see codegen/README.md's "Enum support") makes its enumerators reachable
// as ordinary Lua values, not just silently rejected as an unsupported parameter/
// return type.
TEST_F(GpWildcardTest, EnumValuesAreReachableFromLua)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        identity = occt.gp_TrsfForm.gp_Identity
        rotation = occt.gp_TrsfForm.gp_Rotation
        are_different = (identity ~= rotation)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("are_different"));
}

// gp_Lin was previously unregistered - Geom_Line::Lin() (returning gp_Lin by
// value) was rejected outright before the gp module's wildcard expansion.
TEST_F(GpWildcardTest, GeomLineLinAccessor)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        origin = occt.gp_Pnt.new(1., 2., 3.)
        x_axis = occt.gp_Dir.new(1., 0., 0.)
        line = occt.Geom_Line.new(origin, x_axis)

        lin = line:Lin()
        loc = lin:Location()
        lx = loc:X(); ly = loc:Y(); lz = loc:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("lx")), 1.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("ly")), 2.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("lz")), 3.);
}

// BRepPrimAPI_MakeCylinder(gp_Ax2 const&, R, H) - a positioned cylinder - was
// rejected before gp_Ax2 was registered (only the axis-less (R, H) overload
// worked). gp_Ax2 is exactly the kind of "ubiquitous OCCT type" the wildcard
// expansion was meant to unlock without hand-picking every consumer's needs one
// at a time.
TEST_F(GpWildcardTest, PositionedCylinderViaGpAx2)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        origin = occt.gp_Pnt.new(0., 0., 10.)
        z_axis = occt.gp_Dir.new(0., 0., 1.)
        axis = occt.gp_Ax2.new(origin, z_axis)

        cyl = occt.BRepPrimAPI_MakeCylinder.new(axis, 3., 20.)
        shape = cyl:Shape()
        is_null = shape:IsNull()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<bool>("is_null"));
}

// GC_MakeCircle(gp_Circ const&) - rejected before gp_Circ was registered (only
// the three-point/axis-based overloads worked).
TEST_F(GpWildcardTest, MakeCircleFromGpCirc)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        origin = occt.gp_Pnt.new(0., 0., 0.)
        z_axis = occt.gp_Dir.new(0., 0., 1.)
        axis = occt.gp_Ax2.new(origin, z_axis)
        circ = occt.gp_Circ.new(axis, 7.0)

        maker = occt.GC_MakeCircle.new(circ)
        is_done = maker:IsDone()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("is_done"));
}
