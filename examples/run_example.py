#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
#
# SPDX-License-Identifier: MPL-2.0

"""Minimal example script (see README.md) demonstrating how to: load the occt
plugin, load the occt_helpers Lua plugin cavity_model/cavity_model_v2/tank-model
all declare as a dependency in their own uses: block, read a ported example recipe,
override one of its parameters, and read back its "ok" output (occt.export_brep's
own return value). Deliberately skips legacy grunk's build_recipe/run_recipe pair -
this single self-contained script is simpler for the small recipes here.

Also runs bspline_curve.grr.yml (see its own top comment) - unlike
cavity/tank-model, this one is the same recipe file for both occt_plugin variants
(../README.md's "Building with AD support"): given only libocct_plugin.so, it just
checks the primal curve position looks sane; given libadtl_plugin.so too (grunk-
adolc's own plugin, only ever built alongside an AD-enabled occt_plugin), it also
verifies the AD derivative bspline_curve.grr.yml computes against a finite-
difference approximation computed by re-evaluating the very same recipe with its
own p2_x parameter perturbed - mirroring adOCCT's own tests-ad/src/
bspline_curve.cpp verification methodology (this curve has no simple closed form
to check against directly).

Uses grunk's Python bindings (`pip install grunk`, or `pixi run install_python`
from the grunk repo) rather than C++, since most grunk users script recipes from
Python.

Usage:
    python run_example.py /path/to/libocct_plugin.so [/path/to/libadtl_plugin.so]
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


def run_bspline_curve(state: grunk.state, has_ad: bool) -> None:
    recipe_path = EXAMPLES_DIR / "bspline_curve.grr.yml"
    print(f"Running {recipe_path.name} ...")

    recipe = state.read(str(recipe_path))

    mx0 = recipe.get_output("mid_x_value").value().as_float()
    my0 = recipe.get_output("mid_y_value").value().as_float()
    mz0 = recipe.get_output("mid_z_value").value().as_float()
    print(f"  primal position at u=0.5: ({mx0:.6f}, {my0:.6f}, {mz0:.6f})")

    if not has_ad:
        print("  (no libadtl_plugin.so given - skipping derivative check)")
        return

    dx = recipe.get_output("mid_x_deriv").value().as_float()
    dy = recipe.get_output("mid_y_deriv").value().as_float()
    dz = recipe.get_output("mid_z_deriv").value().as_float()
    print(f"  AD d(position)/d(p2_x) at u=0.5: ({dx:.6f}, {dy:.6f}, {dz:.6f})")

    # Perturb p2_x on the *same* recipe (reactive re-evaluation - the AD-seeded
    # direction above was itself read before this mutation, so it isn't affected
    # by it) and re-read the primal outputs to approximate the same derivative by
    # finite differences.
    fd_step = 1e-6
    recipe.get_feature("p2_x").set_value(0.0 + fd_step)
    mx1 = recipe.get_output("mid_x_value").value().as_float()
    my1 = recipe.get_output("mid_y_value").value().as_float()
    mz1 = recipe.get_output("mid_z_value").value().as_float()
    fd = ((mx1 - mx0) / fd_step, (my1 - my0) / fd_step, (mz1 - mz0) / fd_step)
    print(f"  finite-difference check:      ({fd[0]:.6f}, {fd[1]:.6f}, {fd[2]:.6f})")

    tol = 1e-4
    for axis, ad_value, fd_value in zip("xyz", (dx, dy, dz), fd):
        if abs(ad_value - fd_value) > tol:
            sys.exit(f"  AD/finite-difference mismatch on {axis}: {ad_value} vs {fd_value}")
    print("  AD derivative matches finite differences")


def main() -> None:
    if len(sys.argv) not in (2, 3):
        sys.exit(f"usage: {sys.argv[0]} /path/to/libocct_plugin.so [/path/to/libadtl_plugin.so]")
    occt_plugin_path = sys.argv[1]
    adtl_plugin_path = sys.argv[2] if len(sys.argv) == 3 else None

    state = grunk.state()
    if adtl_plugin_path is not None:
        # Loaded before occt_plugin here, but load order between the two doesn't
        # actually matter (see ../src/occt_plugin.cpp's own register_occt()
        # comment) - only that both are loaded before any AD-enabled OCCT Lua
        # code runs, which state.read() below is the first thing to do.
        grunk.plugin.load_native(state, adtl_plugin_path)
    grunk.plugin.load_native(state, occt_plugin_path)
    state.load_lua_plugin_file(
        grunk.PluginInfo("occt_helpers", "1.0.0"),
        str(EXAMPLES_DIR / "plugins" / "occt_helpers.lua"),
    )

    run_recipe(state, EXAMPLES_DIR / "cavity_model.grr.yml", "cavity_h", 60.0)
    run_recipe(state, EXAMPLES_DIR / "cavity_model_v2.grr.yml", "R1", 180.0)
    run_recipe(state, EXAMPLES_DIR / "tank_model.grr.yml", "radius", 1.5)
    run_bspline_curve(state, has_ad=adtl_plugin_path is not None)


if __name__ == "__main__":
    main()
