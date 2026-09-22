// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// BRepTools is a static-only "namespace class" (see codegen/README.md's
// "Static-only 'namespace classes'") - exercised here on real shapes from other
// modules (BRepPrimAPI_MakeBox, BRep_Builder) rather than in isolation.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class BRepToolsTest : public ::testing::Test {
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

grunk::state* BRepToolsTest::state_ = nullptr;

} // namespace

// OuterWire(face) would need a real face, extracted from a shape - not yet possible
// (no shape-exploration module registered), and BRep_Builder's own MakeFace/
// UpdateFace overloads that could build one directly need Handle(Geom_Surface),
// unregistered - so this sticks to what's actually reachable today: Update/Clean/
// CleanGeometry/RemoveUnusedPCurves/Triangulation on a real box shape from another
// module (BRepPrimAPI_MakeBox).
TEST_F(BRepToolsTest, UpdateAndCleanOnBoxShape)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box = occt.BRepPrimAPI_MakeBox.new(10., 10., 10.):Shape()

        -- Update/Clean/CleanGeometry/RemoveUnusedPCurves take/return nothing
        -- meaningful to inspect directly - this confirms they're callable on a
        -- real shape without throwing, which is the whole point (they're
        -- maintenance operations, not queries).
        occt.BRepTools_Update(box)
        occt.BRepTools_Clean(box, false)
        occt.BRepTools_CleanGeometry(box)
        occt.BRepTools_RemoveUnusedPCurves(box)

        triangulation_ok = occt.BRepTools_Triangulation(box, 0.1, false)
    )");
    ASSERT_TRUE(res.valid());
    // The box has no triangulation at all (never meshed) - Triangulation()
    // verifies an *existing* one meets the given deflection, so this is False,
    // not an error - confirmed empirically.
    EXPECT_FALSE(env.get<bool>("triangulation_ok"));
}

// Compare(vertex, vertex) and RemoveInternals(shape) on bare vertices built via
// BRep_Builder (the brep module) - exercises BRepTools independent of any
// higher-level shape builder.
TEST_F(BRepToolsTest, CompareVerticesAndRemoveInternals)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        builder = occt.BRep_Builder.new()

        v1 = occt.TopoDS_Vertex.new()
        builder:MakeVertex(v1, occt.gp_Pnt.new(0., 0., 0.), 0.01)

        v2 = occt.TopoDS_Vertex.new()
        builder:MakeVertex(v2, occt.gp_Pnt.new(0., 0., 0.0001), 0.01)

        v3 = occt.TopoDS_Vertex.new()
        builder:MakeVertex(v3, occt.gp_Pnt.new(100., 100., 100.), 0.01)

        near_match = occt.BRepTools_Compare(v1, v2)
        far_mismatch = occt.BRepTools_Compare(v1, v3)

        box = occt.BRepPrimAPI_MakeBox.new(10., 10., 10.):Shape()
        occt.BRepTools_RemoveInternals(box, false)
        box_is_null_after = box:IsNull()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("near_match"));
    EXPECT_FALSE(env.get<bool>("far_mismatch"));
    EXPECT_FALSE(env.get<bool>("box_is_null_after"));
}
