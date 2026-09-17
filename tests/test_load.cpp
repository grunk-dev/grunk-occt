// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 0 milestone: loads occt_plugin.so via grunk::plugin::load_native and confirms
// it's reported as loaded, and that its (still-empty) "occt" namespace table is
// visible to plain Lua - deliberately not routed through grunk::recipe's YAML layer
// (see DESIGN.md's "test-harness shape" decision). OCCT_PLUGIN_SO_PATH is set by
// CMakeLists.txt to the built path of the occt_plugin shared library target.

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <iostream>

int main()
{
    grunk::state state;

    grunk::PluginInfo info = grunk::plugin::load_native(state, OCCT_PLUGIN_SO_PATH);

    if (info.name != "occt") {
        std::cerr << "FAIL: expected plugin name \"occt\", got \"" << info.name << "\"\n";
        return 1;
    }

    if (state.plugins().size() != 1 || state.plugins()[0].name != "occt") {
        std::cerr << "FAIL: occt plugin not reported by state.plugins()\n";
        return 1;
    }

    // create_env()'s environment falls back to grunk's *registered-symbol* table
    // (original_env), not to Lua's base library (see grunk/dynamic/state.hpp's
    // create_env doc comment) - so plain Lua globals like `assert` aren't reachable
    // here. Check the namespace table directly via the environment's own indexing
    // instead of evaluating a Lua script that would need `assert`.
    auto env = state.create_env();
    sol::object occt_ns = env["occt"];
    if (occt_ns.get_type() != sol::type::table) {
        std::cerr << "FAIL: occt namespace table not visible in the environment\n";
        return 1;
    }

    std::cout << "OK: occt plugin " << info.version << " loaded, namespace visible\n";
    return 0;
}
