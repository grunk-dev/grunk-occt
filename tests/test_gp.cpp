// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 1/2 milestone: exercises the gp module (generated/gp.cpp) through raw Lua (see
// DESIGN.md's "test-harness shape") - constructors, accessors, gp_Vec::Multiplied,
// gp_Ax1's constructor-from-(gp_Pnt,gp_Dir), and gp_Trsf's SetTranslation/
// SetRotation/SetMirror overloads (the exact gp surface the two grocc-only target
// integration recipes need - see reference/legacy-recipes/{cavity_model,
// tank_model}.grr.yml).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class GpTest : public ::testing::Test {
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

grunk::state* GpTest::state_ = nullptr;

} // namespace

TEST_F(GpTest, PntConstructorAndAccessors)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        p = occt.gp_Pnt.new(1., 2., 3.)
        px = p:X(); py = p:Y(); pz = p:Z()
        default_pnt_x = occt.gp_Pnt.new():X()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_DOUBLE_EQ(env.get<double>("px"), 1.);
    EXPECT_DOUBLE_EQ(env.get<double>("py"), 2.);
    EXPECT_DOUBLE_EQ(env.get<double>("pz"), 3.);
    EXPECT_DOUBLE_EQ(env.get<double>("default_pnt_x"), 0.);
}

TEST_F(GpTest, DirConstructorAndAccessors)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        d = occt.gp_Dir.new(1., 0., 0.)
        dx = d:X(); dy = d:Y(); dz = d:Z()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_DOUBLE_EQ(env.get<double>("dx"), 1.);
    EXPECT_DOUBLE_EQ(env.get<double>("dy"), 0.);
    EXPECT_DOUBLE_EQ(env.get<double>("dz"), 0.);
}

TEST_F(GpTest, VecConstructorsAndMultiplied)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        d = occt.gp_Dir.new(1., 0., 0.)
        v_from_dir_x = occt.gp_Vec.new(d):X()

        v = occt.gp_Vec.new(2., 0., 0.)
        v3x = v:Multiplied(3.):X()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_DOUBLE_EQ(env.get<double>("v_from_dir_x"), 1.);
    EXPECT_DOUBLE_EQ(env.get<double>("v3x"), 6.);
}

TEST_F(GpTest, Ax1ConstructorAndAccessors)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        p = occt.gp_Pnt.new(1., 2., 3.)
        d = occt.gp_Dir.new(1., 0., 0.)
        ax = occt.gp_Ax1.new(p, d)
        ax_loc_x = ax:Location():X()
        ax_dir_x = ax:Direction():X()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_DOUBLE_EQ(env.get<double>("ax_loc_x"), 1.);
    EXPECT_DOUBLE_EQ(env.get<double>("ax_dir_x"), 1.);
}

TEST_F(GpTest, TrsfSetTranslationRotationMirror)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        p = occt.gp_Pnt.new(1., 2., 3.)
        d = occt.gp_Dir.new(1., 0., 0.)
        v = occt.gp_Vec.new(2., 0., 0.)
        ax = occt.gp_Ax1.new(p, d)

        trsf = occt.gp_Trsf.new()
        trsf:SetTranslation(v)
        trsf:SetRotation(ax, 1.5707963267948966)
        trsf:SetMirror(p)
        trsf_ok = true
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("trsf_ok"));
}
