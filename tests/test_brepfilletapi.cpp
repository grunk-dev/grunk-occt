// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 6 milestone: ninth (and, per the original migration plan's Step 6 module
// list, final) module expansion sweep addition (see DESIGN.md/migration plan).
// BRepFilletAPI_MakeChamfer extends BRepFilletAPI_LocalOperation/
// BRepBuilderAPI_MakeShape/BRepBuilderAPI_Command (all reached via add_bases),
// chamfering one edge of the Step 4b smoke test's own box.
//
// BRepFilletAPI_MakeFillet has no accepted constructor at all: its only real
// constructor takes a trailing `ChFi3d_FilletShape` with a default value (this
// generator doesn't expand default arguments - see codegen/README.md's "Known
// gaps"), so it's rejected outright, same category as GeomAPI_PointsToBSpline's own
// GeomAbs_Shape-blocked constructors (DESIGN.md's geomapi note). MakeFillet remains
// registered (its other members are still real, useful C++) but isn't instantiable
// from Lua today - MakeChamfer's single-Shape constructor has no such trailing
// default-enum parameter, so it's the module's actual smoke-test candidate.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

namespace {

class BRepFilletApiTest : public ::testing::Test {
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

grunk::state* BRepFilletApiTest::state_ = nullptr;

} // namespace

TEST_F(BRepFilletApiTest, ChamferOneEdgeOfBox)
{
    auto env = state_->create_env();
    auto res = env.eval(R"(
        box_maker = occt.BRepPrimAPI_MakeBox.new(10., 10., 10.)
        box = box_maker:Shape()
        edge = occt.first_edge(box)

        chamfer_maker = occt.BRepFilletAPI_MakeChamfer.new(box)
        chamfer_maker:Add(1.0, edge)
        chamfered = chamfer_maker:Shape()
        chamfered_is_null = chamfered:IsNull()
        chamfer_done = chamfer_maker:IsDone()
    )");
    ASSERT_TRUE(res.valid());
    EXPECT_TRUE(env.get<bool>("chamfer_done"));
    EXPECT_FALSE(env.get<bool>("chamfered_is_null"));
}
