#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""Minimal example script (see README.md) demonstrating how to: load the occt
plugin, load the occt_helpers Lua plugin both recipes declare as a dependency in
their own uses: block, read a ported example recipe, override one of its
parameters, and read back its "ok" output (occt.export_brep's own return value).
Deliberately skips legacy grunk's build_recipe/run_recipe pair - this single
self-contained script is simpler for the two small recipes here.

Uses grunk's Python bindings (`pip install grunk`, or `pixi run install_python`
from the grunk repo) rather than C++, since most grunk users script recipes from
Python.

Usage:
    python run_example.py /path/to/libocct_plugin.so
"""

import sys
from pathlib import Path

import grunk

EXAMPLES_DIR = Path(__file__).resolve().parent


def run_recipe(state: grunk.state, recipe_path: Path, param_to_override: str, override_value: float) -> None:
    print(f"Running {recipe_path.name} ...")

    recipe = state.read(str(recipe_path))
    recipe.get_feature(param_to_override).set_value(override_value)
    print(f"  overrode {param_to_override} = {override_value}")

    ok = recipe.get_output("ok").value().as_bool()
    print(f"  export_brep {'succeeded' if ok else 'FAILED'}")
    if not ok:
        sys.exit(1)


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} /path/to/libocct_plugin.so")
    occt_plugin_path = sys.argv[1]

    state = grunk.state()
    grunk.plugin.load_native(state, occt_plugin_path)
    state.load_lua_plugin_file(
        grunk.PluginInfo("occt_helpers", "1.0.0"),
        str(EXAMPLES_DIR / "plugins" / "occt_helpers.lua"),
    )

    run_recipe(state, EXAMPLES_DIR / "cavity_model.grr.yml", "cavity_h", 60.0)
    run_recipe(state, EXAMPLES_DIR / "tank_model.grr.yml", "radius", 1.5)


if __name__ == "__main__":
    main()
