// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 0 milestone: an empty grunk plugin that proves the whole toolchain (pixi ->
// cmake -> conda-forge OCCT -> dlopen -> grunk::state) before any OCCT type/function
// is actually registered. Real registrations start in src/gp/ (Step 1).

#include <grunk/dynamic.hpp>
#include <grunk/plugin/loader.hpp>

namespace {

void register_occt(grunk::state& state, grunk::PluginInfo const& info)
{
    // Mints the "occt" namespace table (state.begin_plugin("occt", ...) -> occt.* in
    // Lua) but registers nothing into it yet - see src/gp/ for the first real types.
    [[maybe_unused]] auto ns = state.begin_plugin(info);
}

} // anonymous namespace

GRUNK_PLUGIN_EXPORT grunk::PluginInfo grunk_plugin_info()
{
    return grunk::PluginInfo{"occt", "0.1.0"};
}

GRUNK_PLUGIN_REGISTER(register_occt)
