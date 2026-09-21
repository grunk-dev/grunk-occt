// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 6 milestone: sixth module expansion sweep addition (see DESIGN.md/migration
// plan). BRepOffsetAPI_MakeThickSolid extends BRepOffsetAPI_MakeOffsetShape, itself
// registered by this same module - shelling a single face of the Step 4b smoke
// test's own cylinder proves that inheritance chain reaches
// BRepBuilderAPI_MakeShape::Shape()/BRepBuilderAPI_Command::IsDone() correctly, same
// reasoning as ShapesTest's own box/cylinder/cut chain.
//
// MakeThickSolidBySimple explicitly requires "a non-closed shell or face" as input
// (see BRepOffsetAPI_MakeThickSolid.hxx's own doc comment) - a closed solid (the
// cylinder itself) throws a non-std::exception OCCT failure at Shape()/Build() time
// (confirmed empirically). occt.first_face (src/topexp_extras.hpp) pulls one
// TopoDS_Face out of the cylinder first.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class BRepOffsetApiTest : public ::testing::Test {
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

grunk::state* BRepOffsetApiTest::state_ = nullptr;

} // namespace

TEST_F(BRepOffsetApiTest, ThickSolidFromBox)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        cyl_maker = occt.BRepPrimAPI_MakeCylinder.new(3., 20.)
        cylinder = cyl_maker:Shape()
        face = occt.first_face(cylinder)

        thick_maker = occt.BRepOffsetAPI_MakeThickSolid.new()
        thick_maker:MakeThickSolidBySimple(face, -0.5)
        shell = thick_maker:Shape()
        shell_is_null = shell:IsNull()
        thick_done = thick_maker:IsDone()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("thick_done"));
    EXPECT_FALSE(env.get<bool>("shell_is_null"));
}
