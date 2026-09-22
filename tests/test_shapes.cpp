// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// A box/cylinder/cut boolean-op smoke test. Exercises: TopoDS_Shape + leaf-type
// plain-value inheritance (add_bases works for ordinary value types, not just
// Handle(T) - see DESIGN.md),
// BRepBuilderAPI_MakeShape::Shape()/BRepBuilderAPI_Command::IsDone() reached via
// add_bases from BRepPrimAPI_MakeBox/MakeCylinder/BRepAlgoAPI_Cut (a 4-5 level
// inheritance chain, most of it never separately registered - see
// codegen/README.md), Message_ProgressRange unlocking BRepAlgoAPI_Cut's own
// constructor, and TopoDS.hxx's namespace-flattened DownCast helpers
// (occt.TopoDS_Solid.DownCast).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class ShapesTest : public ::testing::Test {
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

grunk::state* ShapesTest::state_ = nullptr;

} // namespace

TEST_F(ShapesTest, BoxAndCylinderShapes)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box_maker = occt.BRepPrimAPI_MakeBox.new(10., 10., 10.)
        -- IsDone() is checked *after* Shape() deliberately: BRepBuilderAPI_MakeShape::
        -- Shape() calls Build() lazily if not already done, so BRepPrimAPI_MakeBox's
        -- IsDone() is only guaranteed true once Shape() has actually been asked for
        -- (unlike BRepAlgoAPI_Cut's own 2-shape constructor, which builds eagerly -
        -- see BooleanCutAndExport below).
        box = box_maker:Shape()
        box_done = box_maker:IsDone()
        box_is_null = box:IsNull()

        cyl_maker = occt.BRepPrimAPI_MakeCylinder.new(3., 20.)
        cylinder = cyl_maker:Shape()
        cylinder_is_null = cylinder:IsNull()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("box_done"));
    EXPECT_FALSE(env.get<bool>("box_is_null"));
    EXPECT_FALSE(env.get<bool>("cylinder_is_null"));
}

TEST_F(ShapesTest, BooleanCutAndExport)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box = occt.BRepPrimAPI_MakeBox.new(10., 10., 10.):Shape()
        cylinder = occt.BRepPrimAPI_MakeCylinder.new(3., 20.):Shape()

        progress = occt.Message_ProgressRange.new()
        cut_maker = occt.BRepAlgoAPI_Cut.new(box, cylinder, progress)
        result = cut_maker:Shape()
        cut_done = cut_maker:IsDone()
        result_is_null = result:IsNull()

        export_ok = occt.export_brep(result, "step4_cut.brep")
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("cut_done"));
    EXPECT_FALSE(env.get<bool>("result_is_null"));
    EXPECT_TRUE(env.get<bool>("export_ok"));
}

// TopoDS.hxx's namespace-flattened DownCast helper - box is a genuine TopoDS_Solid
// under the hood (BRepPrimAPI_MakeBox always builds one), so downcasting it back
// from the generic TopoDS_Shape succeeds.
TEST_F(ShapesTest, TopoDsDowncastHelper)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box = occt.BRepPrimAPI_MakeBox.new(10., 10., 10.):Shape()
        as_solid = occt.TopoDS_Solid.DownCast(box)
        as_solid_is_null = as_solid:IsNull()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_FALSE(env.get<bool>("as_solid_is_null"));
}
