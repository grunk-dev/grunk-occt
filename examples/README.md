<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# Examples

Real, runnable `grunk::recipe` YAML files built on the `occt` plugin - the Step 6.5
milestone of the migration plan (see `../DESIGN.md`): documentation showing realistic,
multi-call usage (rather than the isolated unit smoke tests under `../tests/`), and
integration coverage exercising call sequences and data flow across several bindings
together.

Ported from legacy grocc's own example recipes (`../reference/legacy-recipes/`,
`grunk` v0.3.x + legacy `grocc`), rewritten for the new grunk (v0.5.0, YAML `uses:`/
`parameters:`/`steps:`/`recipes:`/`outputs:` shape, Lua `steps:` instead of the old
per-step YAML tags, `occt.*` namespace calls instead of `!<grocc::...>` tags).

## Contents

- `cavity_model.grr.yml` - a gas turbine cavity: a revolved sketch fused with one
  cylinder, a second (rotated, translated, half-space-clipped) cylinder fused in
  too. Two named sub-recipes (`cavity_sketch`, `cylinder`).
- `cavity_model_v2.grr.yml` - ported from legacy's own `20241017.grr.yml` (see
  `../reference/legacy-recipes/README.md`) - a newer, more involved iteration of
  the same gas turbine cavity: two revolved sectors (its own `cavity_sketch`/
  `cylinder` sub-recipes, distinct from `cavity_model.grr.yml`'s own) grouped into
  one `TopoDS_CompSolid` (via `occt_helpers.comp_solid`, not `compound` - see
  below), both axial ends clipped by their own half-space, two cylinders fused in.
  Kept alongside `cavity_model.grr.yml` for now rather than replacing it - they
  may be consolidated later.
- `tank_model.grr.yml` - a hydrogen tank: a "squircle" cylinder (four quarter-wall
  segments plus a square core) capped by a dome (a box core plus five spherical
  wedges). Nested sub-recipes three levels deep (`cylinder` → `cylinder_segment`,
  `dome` → `spherical_segment`).
- `plugins/occt_helpers.lua` - a small, standalone pure-Lua grunk plugin (see "Why a
  plugin, not a shared module" below) with the handful of builder-pattern helpers
  (`translate`, `rotate`, `mirror_point`, `polygon_face`, `wire_face`, `compound`,
  `comp_solid`) these recipes use.
- `run_example.py` - a small, self-contained example script (see "Running the
  examples" below), using grunk's Python bindings - deliberately skips legacy
  grunk's `build_recipe`/`run_recipe` pair, not needed for recipes this size.

Legacy's own `fuel_D150.grr`/`fuelsystem.grr.yml` (see
`../reference/legacy-recipes/README.md`) are **not** ported here, and per an
explicit decision are not planned to be: both are large (1800+ line), auto-exported
parameter dumps rather than hand-authored recipes - explicitly called out in that
README as "not useful as documentation", and their real per-face parameter
structure isn't a good fit for hand-porting. Their own OCCT surface was still
audited (see `../DESIGN.md`'s "Step 6.5 coverage audit") so the binding can cover
their functionality even without a literal port.

## Why a plugin, not a shared module

An earlier version of this milestone registered `occt_helpers` as a plain grunk
module (`state.run_module_file`) from the runner script, with no mention of it
anywhere in either recipe's own YAML - purely an implicit, undocumented runtime
requirement the runner happened to set up first. That works, but it directly
undercuts one of grunk's own stated goals: recipes that are reusable, distributable,
and reproducible with shareable customization. A recipe that silently doesn't work
without a specific *other* file, known only from prose in this README, isn't really
self-describing or distributable on its own.

`occt_helpers.lua` is instead a small, standalone **pure Lua grunk plugin** now:
every `.grr.yml` file here declares it explicitly, right alongside `occt` itself:

```yaml
uses:
  grunk: 0.5.0
  occt: 0.1.0
  occt_helpers: "1.0.0"
```

`grunk::recipe` validates every `uses:` entry (recursively, for every sub-recipe
too - see `Recipe::populate_from_node`) against the plugins actually loaded on the
`grunk::state` before running a single step, and throws a clear `io_error` naming
the missing plugin if it isn't. Declaring `occt_helpers` this way means: (a) the
recipe file itself honestly states what it depends on, not just this README, (b) a
generic recipe-running harness gets an immediate, actionable error instead of a
confusing runtime failure deep inside a Lua call if the dependency is missing, and
(c) `occt_helpers.lua` is trivially shareable on its own - copy the one file
alongside a recipe that declares it, nothing else to wire up. The file's own Lua
content is unchanged from a plain module's shape (`state.load_lua_plugin_file` is a
thin wrapper over `run_module_file` that additionally records a name+version
identity `uses:` can check against - see `plugins/occt_helpers.lua`'s own top
comment) - only *how* it's loaded and declared changed, not what it contains.

Sub-recipes' own private geometry-building helpers could instead live in a
recipe-local `modules:` block (deep-cloned per recipe instance, no separate file at
all) - not used here because `occt_helpers` is shared across *every* recipe and
sub-recipe in all three files, and duplicating its own content into every
`modules:` block would be worse for maintainability than one small, explicitly
declared plugin.

## Running the examples

```
pixi run build                                          # builds occt_plugin
python run_example.py ../build/libocct_plugin.so         # from this directory
```

Requires grunk's own Python bindings (`pip install grunk`, or `pixi run
install_python` from the `grunk` repo itself - not currently built as part of
`grunk-occt`'s own CMake/pixi setup, so there's no bundled CI coverage for this
script yet, unlike the C++ test suite under `../tests/`).

`run_example.py` loads `occt_plugin`, loads `occt_helpers.lua` as a plugin, reads
each recipe, **overrides one of its parameters** (`cavity_h` for
`cavity_model.grr.yml`, `R1` for `cavity_model_v2.grr.yml`, `radius` for
`tank_model.grr.yml` - the same `recipe.get_feature("name").set_value(...)` pattern
you'd use to override any other parameter), and reads back its `ok` output
(`occt.export_brep`'s own return value). Each run writes a `.brep` file
(`cavity_model.brep`, `cavity_model_v2.brep`, `tank_model.brep`) into the current
working directory - open any of them in a CAD viewer that reads BREP (e.g.
FreeCAD) to inspect the result.

To override a *different* parameter, or read back a different feature, edit
`run_example.py` directly - it's meant to be read and modified, not a generic CLI
tool (see the migration plan's own Step 0 decision to skip a
`grunk::recipe`-based end-to-end test until several modules existed, and Step 6.5's
decision to skip legacy grunk's `build_recipe` concept here for the same reason:
one small, explicit example script is simpler than a generic driver for a handful
of recipes this size).

## A note on recipe-level vs. plugin/module-level dynamic dispatch

If you extend either recipe (or write your own against `occt_plugin`) and hit
`"Feature: type ... has no member ..."`, see `../DESIGN.md`'s "Recipe-level dynamic
dispatch does not walk `add_bases<...>()`" section before assuming something is
broken - it's grunk's own documented, deliberate behavior (recipe `steps:`/
`recipes:` need an explicit `:as(BaseType)` cast to reach an inherited method;
plugin/module bodies don't have this problem). Every `.grr.yml` file here and
`plugins/occt_helpers.lua` already show this pattern in context.

Separately: if a plugin/module function needs a *list* of tracked values
(points, edges, shapes, ...), pass them as trailing varargs
(`helpers.polygon_face(p0, p1, ...)`), not as a single Lua table
(`helpers.polygon_face({p0, p1, ...})`) - the latter is not just clunkier, it can
silently read back the wrong (default/stale) value due to an upstream grunk
dependency-tracking bug, [grunk-dev/grunk#289](https://github.com/grunk-dev/grunk/issues/289).
See `../DESIGN.md`'s "A related, narrower gotcha..." section and
`plugins/occt_helpers.lua`'s own `polygon_face`/`wire_face`/`compound` for the
varargs pattern.
