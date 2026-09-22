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
| `cavity_model.grr.yml` | `examples/gasturbine_cavity/cavity_model.grr.yml`, identical on all 4 branches | 0.1.1 | Clean, hand-written, well-structured (named sub-recipes: `cavity_sketch`, `cylinder`). `gp_Pnt`/`gp_Dir`/`gp_Vec`/`gp_Ax1`/`gp_Trsf` (+ `SetTranslation`), `BRepBuilderAPI_{MakePolygon,MakeFace,Transform}`, `BRepPrimAPI_{MakeRevol,MakeHalfSpace}`, `BRepAlgoAPI_{Fuse,Cut}`. **Ported** - see `../../examples/cavity_model.grr.yml`. |
| `tank_model.grr.yml` | `examples/hydrogen_tank_hexa/tank_model.grr.yml`, identical on all 4 branches | 0.1.2 | Clean, hand-written (named sub-recipes: `dome`, `cylinder`, `cylinder_segment`, `spherical_segment`). Adds `gp_Ax2`, `BRepPrimAPI_{MakeBox,MakeSphere,MakePrism}`, `BRepAlgoAPI_Common`, `GC_MakeArcOfCircle`, `TopoDS_Compound` + `BRep_Builder::{MakeCompound,Add}`, `gp_Trsf::{SetRotation,SetMirror}`. **Ported** - see `../../examples/tank_model.grr.yml`. |
| `20241017.grr.yml` | `examples/gasturbine_cavity_update/20241017.grr.yml`, `seat_example` branch only | 0.1.1 | Newer iteration of `cavity_model.grr.yml`'s own cavity (its own `cavity_sketch`/`cylinder` sub-recipes), two revolved sectors grouped into a `TopoDS_CompSolid` (`BRep_Builder::MakeCompSolid`/`Add`) instead of a `Compound`, two half-spaces instead of one. **Ported** - see `../../examples/20241017.grr.yml`. |
| `fuel_D150.grr` | `examples/wing_fuel_system/fuel_D150.grr`, `tigl_wingbox_and_fuel_system_example` branch only | 0.1.4 | **Large** (2598 lines) - looks like an exported/serialized parameter dump (UUID-like feature names) rather than hand-authored, mostly `gp_Pnt`/`gp_Dir` literals. **Not planned to be ported** (explicit decision - not useful as documentation), but its own OCCT surface was audited for binding coverage - see `../../DESIGN.md`'s "Step 6.5 coverage audit". |
| `fuelsystem.grr.yml` | `examples/wing_fuel_system/fuelsystem.grr.yml`, `tigl_wingbox_and_fuel_system_example` branch only | 0.1.4 | **Large** (1866 lines), same style as `fuel_D150.grr`. Same status: **not planned to be ported**, audited for coverage only. |

## Step 6.5 status

All three grocc-only recipes worth hand-porting (`cavity_model.grr.yml`, `tank_model.grr.yml`,
`20241017.grr.yml`) are ported, under `../../examples/`. `fuel_D150.grr`/`fuelsystem.grr.yml` are
deliberately not ported (large auto-exported dumps, not documentation value) - see
`../../DESIGN.md`'s "Step 6.5 coverage audit" for what OCCT surface they (and the six
`tigl`/`geo`-using recipes excluded from this directory entirely, listed above) actually need,
audited without porting them, so the binding can still cover their functionality incrementally.
