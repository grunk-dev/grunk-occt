// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Exercises occt_plugin through a real grunk::recipe (parameters:/steps:/recipes:/
// outputs:, dependency tracking, reactive re-evaluation on set_value), not just
// test_load.cpp's/every other test_*.cpp's raw create_env()/eval() harness - none of
// which ever goes through the recipe/parametric layer. examples/run_example.py
// already exercises this same layer end-to-end via grunk's Python bindings, but only
// as an external script, not as a fast, always-run C++ unit test - this is the
// "one true grunk::recipe-based end-to-end test" the migration plan's Step 0 noted
// as deferred until several modules existed, reusing cavity_model.grr.yml itself
// (Step 6.5) rather than inlining a separate minimal recipe, so this test and that
// example can never silently drift apart.
//
// EXAMPLES_DIR is set by CMakeLists.txt to the absolute path of examples/.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>
#include <grunk/recipe.hpp>

#include <gtest/gtest.h>

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include "../src/occt_ad_support.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

class RecipeTest : public ::testing::Test {
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
        state_->load_lua_plugin_file(
            grunk::PluginInfo{"occt_helpers", "1.0.0"},
            std::string(EXAMPLES_DIR) + "/plugins/occt_helpers.lua"
        );
    }

    static void TearDownTestSuite()
    {
        delete state_;
        state_ = nullptr;
    }

    static grunk::state* state_;
};

grunk::state* RecipeTest::state_ = nullptr;

} // namespace

// Reads examples/cavity_model.grr.yml (the same file run_example.py runs), overrides
// its cavity_h parameter, and confirms export_brep's own "ok" output - the same
// check run_example.py itself makes, just as a fast in-process C++ test rather than
// a subprocess running a Python script. Also confirms outputs: reads back correctly
// through the C++ Recipe API, not just the Python bindings.
TEST_F(RecipeTest, CavityModelRecipeProducesOkOutput)
{
    auto recipe = state_->read(std::string(EXAMPLES_DIR) + "/cavity_model.grr.yml");

    recipe.get_feature("cavity_h").set_value(60.0);
    EXPECT_TRUE(recipe.get_output("ok").value().as<bool>());
}

// Reactive re-evaluation through the recipe layer: changing a parameter that a
// sub-recipe three levels deep (see cavity_model.grr.yml's recipes: block) depends
// on must still flow through to the top-level "ok" output, exactly like
// test_geom.cpp's BezierCurveFromVectorOfPoints etc. confirm for a single undecorated
// environment - this is the same guarantee, but through the parametric/dependency-
// tracked layer this test file is specifically about.
TEST_F(RecipeTest, CavityModelReactsToParameterChange)
{
    auto recipe = state_->read(std::string(EXAMPLES_DIR) + "/cavity_model.grr.yml");
    auto ok = recipe.get_output("ok");

    recipe.get_feature("cavity_h").set_value(60.0);
    EXPECT_TRUE(ok.value().as<bool>());

    recipe.get_feature("cavity_h").set_value(45.0);
    EXPECT_TRUE(ok.value().as<bool>());
}

// gp_Pnt/gp_Vec/gp_Dir as independent (parameter-typed, not just step-local) recipe
// values - mirrors grunk's own tests/cpp/recipe_simple.cpp,
// TEST(Recipe, simple_userdata_parameters), which is what src/gp/gp_extras.hpp's own
// sol::meta_function::to_string registration for these three types is modeled on.
// Confirms recipe.to_string() produces the expected "occt.gp_Pnt.new(...)"-shaped Lua
// expression (grunk::serialize throws "Cannot serialize an opaque type ..." for any
// userdata type without a tostring metamethod - this is the failure mode being
// guarded against) and that writing the recipe out and reading it back reproduces the
// same values - not just that serialization doesn't throw.
TEST_F(RecipeTest, GpTypesRoundTripAsIndependentParameters)
{
    std::string const path = (std::filesystem::temp_directory_path() / "grunk_occt_gp_types_round_trip.grr.yml").string();

    {
        auto recipe = state_->create_recipe();
        recipe["p"] = state_->feature<gp_Pnt>(gp_Pnt(1.0, 2.5, -3.0));
        recipe["v"] = state_->feature<gp_Vec>(gp_Vec(0.0, 1.0, 0.0));
        recipe["d"] = state_->feature<gp_Dir>(gp_Dir(1.0, 0.0, 0.0));
        recipe.tag();

        std::string const out = recipe.to_string();
        EXPECT_NE(out.find("occt.gp_Pnt.new(1.0, 2.5, -3.0)"), std::string::npos);
        EXPECT_NE(out.find("occt.gp_Vec.new(0.0, 1.0, 0.0)"), std::string::npos);
        EXPECT_NE(out.find("occt.gp_Dir.new(1.0, 0.0, 0.0)"), std::string::npos);

        state_->write(path, recipe);
    }

    auto recipe = state_->read(path);
    auto p = recipe.get_feature("p").value().as<gp_Pnt>();
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(p.X()), 1.0);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(p.Y()), 2.5);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(p.Z()), -3.0);

    auto v = recipe.get_feature("v").value().as<gp_Vec>();
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(v.X()), 0.0);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(v.Y()), 1.0);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(v.Z()), 0.0);

    auto d = recipe.get_feature("d").value().as<gp_Dir>();
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(d.X()), 1.0);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(d.Y()), 0.0);
    EXPECT_DOUBLE_EQ(occt_plugin::to_double(d.Z()), 0.0);

    std::remove(path.c_str());
}
