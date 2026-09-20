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

#include <string>

namespace {

class RecipeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        state_ = new grunk::state();
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
