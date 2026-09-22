// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// TopLoc_Location registration unlocks every other module's own
// TopLoc_Location parameter/return, previously rejected purely for lack of it -
// TopoDsShapeLocationRoundTrip below exercises one such case (TopoDS_Shape::
// Location/Move, generated/topods.cpp).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class TopLocTest : public ::testing::Test {
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

grunk::state* TopLocTest::state_ = nullptr;

} // namespace

TEST_F(TopLocTest, ConstructFromTrsfAndInvert)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        empty = occt.TopLoc_Location.new()
        is_identity_empty = empty:IsIdentity()

        trsf = occt.gp_Trsf.new()
        trsf:SetTranslation(occt.gp_Vec.new(1., 2., 3.))
        loc = occt.TopLoc_Location.new(trsf)
        is_identity_loc = loc:IsIdentity()

        inverted = loc:Inverted()
        roundtrip = loc:Multiplied(inverted)
        is_identity_roundtrip = roundtrip:IsIdentity()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("is_identity_empty"));
    EXPECT_FALSE(env.get<bool>("is_identity_loc"));
    EXPECT_TRUE(env.get<bool>("is_identity_roundtrip"));
}

// TopoDS_Shape::Location()/Move() (generated/topods.cpp) - retroactively unlocked
// by this module's own existence (TopLoc_Location was unregistered before).
TEST_F(TopLocTest, TopoDsShapeLocationRoundTrip)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box = occt.BRepPrimAPI_MakeBox.new(10., 10., 10.):Shape()
        empty_loc = box:Location()
        was_identity = empty_loc:IsIdentity()

        trsf = occt.gp_Trsf.new()
        trsf:SetTranslation(occt.gp_Vec.new(5., 0., 0.))
        loc = occt.TopLoc_Location.new(trsf)

        moved = box:Moved(loc, true)
        moved_loc = moved:Location()
        moved_is_identity = moved_loc:IsIdentity()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("was_identity"));
    EXPECT_FALSE(env.get<bool>("moved_is_identity"));
}
