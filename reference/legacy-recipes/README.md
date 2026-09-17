<!--
SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>

SPDX-License-Identifier: MPL-2.0
-->

# Legacy grocc-only example recipes

These are `.grr`/`.grr.yml` recipes found on unmerged branches of the (new) `grunk` repo
(`examples/` directories on `tigl_wingbox_and_fuel_system_example`, `seat_example`,
`wingbody_example`, `workshop`), saved here verbatim as reference material. They target legacy
grunk (v0.3.x) and legacy `grocc` and **do not run as-is** - they're the source material for
Step 6.5 of the migration plan (port legacy plugin-using example recipes), once `grunk-occt`
covers the OCCT surface each one needs.

Several other recipes were found on the same branches but use additional plugins beyond `grocc`
(`tigl`, `geo`) - excluded here since they're not reproducible with `grunk-occt` alone:
`CPACS_wingbox.grr.yml`, `seat_model.grr.yml`, `load_carrier.grr`,
`wing_fuselage_configuration/{fused_wing_fuselage,solid_fuselage,solid_wing}.grr`,
`hello_world/box.grr.yml`, `wingBody/wing_body.grr.yml`.

## Recipes (grocc-only, confirmed by grepping every `!<...>` tag in each file)

| File | Source | grocc version | What it exercises |
|---|---|---|---|
| `cavity_model.grr.yml` | `examples/gasturbine_cavity/cavity_model.grr.yml`, identical on all 4 branches | 0.1.1 | Clean, hand-written, well-structured (named sub-recipes: `cavity_sketch`, `cylinder`). `gp_Pnt`/`gp_Dir`/`gp_Vec`/`gp_Ax1`/`gp_Trsf` (+ `SetTranslation`), `BRepBuilderAPI_{MakePolygon,MakeFace,Transform}`, `BRepPrimAPI_{MakeRevol,MakeHalfSpace}`, `BRepAlgoAPI_{Fuse,Cut}`. **Best first candidate** - smallest, most readable, good module spread. |
| `tank_model.grr.yml` | `examples/hydrogen_tank_hexa/tank_model.grr.yml`, identical on all 4 branches | 0.1.2 | Clean, hand-written (named sub-recipes: `dome`, `cylinder`, `cylinder_segment`, `spherical_segment`). Adds `gp_Ax2`, `BRepPrimAPI_{MakeBox,MakeSphere,MakePrism}`, `BRepAlgoAPI_Common`, `GC_MakeArcOfCircle`, `TopoDS_Compound` + `BRep_Builder::{MakeCompound,Add}`, `gp_Trsf::{SetRotation,SetMirror}`. **Second candidate** - together with `cavity_model.grr.yml`, covers nearly every module legacy grocc wrapped (gp, TopoDS, BRep, BRepBuilderAPI, BRepPrimAPI, BRepAlgoAPI, GC). |
| `20241017.grr.yml` | `examples/gasturbine_cavity_update/20241017.grr.yml`, `seat_example` branch only | 0.1.1 | Same family as `cavity_model.grr.yml`, updated parameters - lower priority (redundant module coverage), useful as a second data point once `cavity_model.grr.yml` passes. |
| `fuel_D150.grr` | `examples/wing_fuel_system/fuel_D150.grr`, `tigl_wingbox_and_fuel_system_example` branch only | 0.1.4 | **Large** (2598 lines) - looks like an exported/serialized parameter dump (UUID-like feature names) rather than hand-authored, mostly `gp_Pnt`/`gp_Dir` literals. Good later stress test once core coverage exists; not useful as documentation. |
| `fuelsystem.grr.yml` | `examples/wing_fuel_system/fuelsystem.grr.yml`, `tigl_wingbox_and_fuel_system_example` branch only | 0.1.4 | **Large** (1866 lines), same style as `fuel_D150.grr`. Same role: later stress test, not documentation. |

## How to use these in Step 6.5

Port `cavity_model.grr.yml` first (smallest, cleanest, clearest documentation value), then
`tank_model.grr.yml`. Porting means: rewriting to the new recipe format (new grunk's YAML+Lua
`steps:` syntax, `occt.*` namespace calls instead of `grocc::...` tags) once `grunk-occt` covers
the modules each one needs (see the table above for exactly which). `20241017.grr.yml`,
`fuel_D150.grr`, and `fuelsystem.grr.yml` are lower priority - port them later, or only as
additional stress-test integration coverage once the core two are working.
