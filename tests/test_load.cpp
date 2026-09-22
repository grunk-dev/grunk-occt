// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Loads occt_plugin.so via grunk::plugin::load_native and confirms it's reported as
// loaded, and that its "occt" namespace table is visible to plain Lua - deliberately
// not routed through grunk::recipe's YAML layer (see DESIGN.md's "test-harness
// shape" decision). OCCT_PLUGIN_SO_PATH is set by CMakeLists.txt to the built path
// of the occt_plugin shared library target.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <gtest/gtest.h>

TEST(Load, PluginLoadsAndReportsItself)
{
    grunk::state state;

    grunk::PluginInfo info = grunk::plugin::load_native(state, OCCT_PLUGIN_SO_PATH);

    EXPECT_EQ(info.name, "occt");

    ASSERT_EQ(state.plugins().size(), 1u);
    EXPECT_EQ(state.plugins()[0].name, "occt");

    // create_env()'s environment falls back to grunk's *registered-symbol* table
    // (original_env), not to Lua's base library (see grunk/dynamic/state.hpp's
    // create_env doc comment) - so plain Lua globals like `assert` aren't reachable
    // here. Check the namespace table directly via the environment's own indexing
    // instead of evaluating a Lua script that would need `assert`.
    auto env = state.create_env();
    sol::object occt_ns = env["occt"];
    EXPECT_EQ(occt_ns.get_type(), sol::type::table);
}
