// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 0 built an empty grunk plugin proving the whole toolchain (pixi -> cmake ->
// conda-forge OCCT -> dlopen -> grunk::state). Step 1 hand-wrote the first real
// module (gp); Step 2's generator now reproduces (and exceeds) it - see
// generated/gp.cpp/codegen/README.md.

#include <grunk/dynamic.hpp>
#include <grunk/plugin/loader.hpp>

#include "../generated/gp.hpp"

namespace {

void register_occt(grunk::state& state, grunk::PluginInfo const& info)
{
    auto ns = state.begin_plugin(info);
    register_gp(ns);
}

} // anonymous namespace

GRUNK_PLUGIN_EXPORT grunk::PluginInfo grunk_plugin_info()
{
    return grunk::PluginInfo{"occt", "0.1.0"};
}

GRUNK_PLUGIN_REGISTER(register_occt)
