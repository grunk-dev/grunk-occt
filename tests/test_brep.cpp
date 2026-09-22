// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// BRep_Builder constructs topology from scratch (MakeVertex/MakeEdge/
// MakeFace, all mutating an existing TopoDS_* userdata in place - same pattern as
// bnd's BRepBndLib::Add); BRep_Tool inspects it back out (Pnt, Tolerance, ...) -
// exercised together here as a construct/inspect round trip on a bare TopoDS_Vertex,
// independent of BRepPrimAPI's higher-level shape builders.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

#include "../src/occt_ad_support.hpp"

namespace {

class BRepTest : public ::testing::Test {
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

grunk::state* BRepTest::state_ = nullptr;

} // namespace

TEST_F(BRepTest, MakeVertexAndInspect)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        builder = occt.BRep_Builder.new()
        v = occt.TopoDS_Vertex.new()

        builder:MakeVertex(v, occt.gp_Pnt.new(1., 2., 3.), 0.01)

        p = occt.BRep_Tool_Pnt(v)
        px = p:X(); py = p:Y(); pz = p:Z()
        tol = occt.BRep_Tool_Tolerance(v)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("px")), 1.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("py")), 2.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("pz")), 3.);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(env.get<sol::object>("tol")), 0.01);
}

// BRep_Tool::IsClosed(TopoDS_Shape const&) on a real shape from a different module
// (BRepPrimAPI_MakeBox) - for a Shape that isn't itself a Shell or Wire (a Solid,
// here), IsClosed's docs say it falls back to TopoDS_Shape::Closed(), a plain
// stored flag (see TopoDS_Shape.hxx's Closed()/Closed(bool)) rather than anything
// computed from the box's actual (obviously closed) geometry - BRepPrimAPI_MakeBox
// never sets it, so this is false, confirmed empirically. Still a real exercise of
// the binding on a real, non-trivial shape from another module - just not the
// intuitive "is this box geometrically closed" answer the name suggests.
TEST_F(BRepTest, IsClosedOnBoxShape)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box = occt.BRepPrimAPI_MakeBox.new(10., 10., 10.):Shape()
        box_is_closed = occt.BRep_Tool_IsClosed(box)
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<bool>("box_is_closed"));
}
