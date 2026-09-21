-- SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
--
-- SPDX-License-Identifier: MPL-2.0

-- A pure Lua grunk plugin (see run_example.py, which loads this file via
-- state.load_lua_plugin_file(PluginInfo{"occt_helpers", "1.0.0"}, ...)) - both
-- .grr.yml recipes declare it explicitly in their own uses: block
-- (occt_helpers: "1.0.0"), the same way they declare occt itself, so a recipe
-- that needs it says so and grunk::recipe validates it's actually loaded before
-- running - see ../README.md's "Why a plugin, not a shared module" section.
--
-- The file's own content is unchanged from grunk's plain-module shape (top-level
-- Lua functions, no explicit registration calls needed) - load_lua_plugin_file is
-- a thin wrapper over run_module_file that additionally records a name+version
-- identity in the state's plugin registry, which is what uses:-block validation
-- checks against.
--
-- Every function below calls at least one occt.* method that takes a non-const
-- reference parameter (gp_Trsf:SetTranslation/SetRotation/SetMirror mutate the
-- Trsf in place; BRepBuilderAPI_MakePolygon/MakeWire:Add and BRep_Builder:Add
-- likewise mutate their own builder/compound argument) - grunk's parametric
-- recipe graph (a recipe's own steps:) refuses to call anything shaped like that
-- directly, since every step there becomes an individually cached graph node and
-- such a call has no well-defined "just recompute this one node" semantics. A
-- plugin/module function call is different: it's one opaque, non-parametric
-- compute node from the recipe's point of view, so whatever it does internally -
-- including these ordinary imperative builder-pattern sequences - is fine.

-- grunk's own "decorated"/parametric Lua environment (what a recipe's steps:/
-- recipes: run under) resolves a colon-call by looking the method up directly in
-- the *exact* type the value was constructed as - it does not walk a C++
-- add_bases<...>() chain the way plain sol2 dispatch does (see grunk/dynamic/
-- state.hpp's Feature.__index doc comment: "Feature: type X has no member Y - use
-- :as(Type) explicitly"). Every BRepPrimAPI_Make*/BRepBuilderAPI_Transform/
-- BRepAlgoAPI_* class here only gets Shape() via add_bases<BRepBuilderAPI_
-- MakeShape, ...>(), so calling :Shape() on one of them directly in a recipe's own
-- steps:/recipes: needs an explicit `:as(occt.BRepBuilderAPI_MakeShape):Shape()`
-- (see both .grr.yml files) - this can NOT be centralized into a module function
-- here: every value crossing INTO a module call is unwrapped to a plain, already-
-- decided-type object first (confirmed empirically - :as() itself, a Feature-only
-- method, is gone by the time such a value reaches a module function's body), so
-- there's no Feature left inside a module to call :as() on in the first place.
--
-- Every occt.* call made directly INSIDE this module's own functions, by
-- contrast, does NOT need this - confirmed empirically: a module's own body runs
-- its occt.* calls as plain, undecorated values (module calls are one opaque,
-- non-parametric compute node - see this file's own top comment - so nothing
-- inside needs to be individually tracked), and plain sol2 dispatch does walk
-- add_bases<...>() correctly there, same as every tests/test_*.cpp file's own raw
-- Lua strings - see translate/rotate/mirror_point's own plain ":Shape()" calls
-- below. Only genuinely *incoming* values - a module's own parameters, populated
-- from the calling recipe's tracked features - need unwrapping.
--
-- polygon_face/wire_face/compound take their list of points/edges/shapes as
-- trailing varargs (...) from their CALLER's side, NOT as a single Lua table
-- argument - this is not a style choice, it works around a real grunk
-- dependency-tracking bug (reported upstream, see grunk-dev/grunk#289): a
-- tracked value read straight back out of an incoming module parameter
-- reflects the caller's current (possibly overridden) value correctly, but
-- the exact same value, if the caller first wraps it in a Lua table and
-- passes *that table* as the module parameter, silently reads back
-- stale/default - dependency tracking does not see through a table built on
-- the CALLER's side of the boundary. Passing each element as its own vararg
-- sidesteps this entirely (confirmed via a minimal repro isolating that
-- table-wrapping + module-call combination as the two jointly-necessary
-- conditions). Each vararg element, like any other top-level module
-- parameter, arrives already unwrapped - no ":value()" needed.
--
-- Each function below still packs its own already-unwrapped `...` into a
-- local table (`{...}`) internally, purely for plain-Lua iteration (`#`/`[i]`)
-- - this is safe and unrelated to the bug above: `select`/`select('#', ...)`
-- aren't reachable from this decorated environment (same restriction as
-- Lua's own error()/assert(), see src/math_extras.hpp's comment on
-- occt.error()), and by the time `...` reaches a function body, its elements
-- are already plain, correctly-valued Lua values - the bug is specifically
-- about a table crossing INTO a module call from the recipe side, not about
-- any table built afterwards, inside the module, from already-arrived values.

-- BRepBuilderAPI_Transform's trailing theCopyGeom/theCopyMesh (both = Standard_False
-- by default) are omitted below - grunk-occt's code generator now expands trailing
-- default-valued C++ parameters into shorter overloads (see ../../DESIGN.md's
-- "Default-argument expansion"), so the 2-arg (shape, trsf) form already means
-- exactly the same thing as explicitly passing (shape, trsf, false, false) used to.

function translate(shape, vec)
    local trsf = occt.gp_Trsf.new()
    trsf:SetTranslation(vec)
    return occt.BRepBuilderAPI_Transform.new(shape, trsf):Shape()
end

function rotate(shape, axis, angle)
    local trsf = occt.gp_Trsf.new()
    trsf:SetRotation(axis, angle)
    return occt.BRepBuilderAPI_Transform.new(shape, trsf):Shape()
end

function mirror_point(shape, point)
    local trsf = occt.gp_Trsf.new()
    trsf:SetMirror(point)
    return occt.BRepBuilderAPI_Transform.new(shape, trsf):Shape()
end

-- Builds a closed planar face from an ordered list of gp_Pnt (passed as
-- varargs - see this file's top comment), via BRepBuilderAPI_MakePolygon (Add
-- one point at a time, then Close) followed by BRepBuilderAPI_MakeFace on the
-- resulting wire. BRepBuilderAPI_MakeFace's own trailing OnlyPlane (default
-- Standard_False) is likewise omitted below.
function polygon_face(...)
    local points = {...}
    local mp = occt.BRepBuilderAPI_MakePolygon.new()
    for i = 1, #points do
        mp:Add(points[i])
    end
    mp:Close()
    return occt.BRepBuilderAPI_MakeFace.new(mp:Wire()):Face()
end

-- Builds a face from an ordered list of already-built TopoDS_Edge (passed as
-- varargs - see this file's top comment), via BRepBuilderAPI_MakeWire (Add
-- each edge) followed by BRepBuilderAPI_MakeFace on the resulting wire.
function wire_face(...)
    local edges = {...}
    local mw = occt.BRepBuilderAPI_MakeWire.new()
    for i = 1, #edges do
        mw:Add(edges[i])
    end
    return occt.BRepBuilderAPI_MakeFace.new(mw:Wire()):Face()
end

-- Builds a TopoDS_Compound out of an arbitrary list of shapes (passed as
-- varargs - see this file's top comment), via BRep_Builder/TopoDS_Builder's
-- MakeCompound + Add.
--
-- IsNull() is checked before every Add - not optional defensiveness, found the
-- hard way: TopoDS_Builder::Add's own OCCT implementation unconditionally
-- dereferences the component's own TShape (aComponent.TShape()->Free(...), its
-- very first statement) with no null check at all, so handing it a null/empty
-- shape - e.g. a BRepAlgoAPI_Cut/Common whose own boolean operation happened to
-- produce an empty result for the particular geometry involved, a legitimate,
-- non-exceptional OCCT outcome, not a bug in the caller - segfaults the whole
-- process instead of raising a catchable error. Confirmed via gdb against a real
-- reproduction (an AD-enabled build's own BRepAlgoAPI_Common producing an empty
-- result for one of tank_model.grr.yml's own dome side-wedge intersections,
-- where the plain build's own boolean op happens not to) - not an AD-specific
-- risk in general, just the first input that ever actually hit it.
function compound(...)
    local shapes = {...}
    local c = occt.TopoDS_Compound.new()
    local builder = occt.BRep_Builder.new()
    builder:MakeCompound(c)
    for i = 1, #shapes do
        local shape = shapes[i]
        if shape:IsNull() then
            -- occt.error(...), not Lua's own error() - unreachable here, same
            -- restriction as assert() (see src/math_extras.hpp's own comment).
            occt.error("occt_helpers.compound: shape " .. i .. " of " .. #shapes .. " is null/empty - refusing to add it (TopoDS_Builder::Add does not handle this and segfaults instead of failing cleanly)")
        end
        builder:Add(c, shape)
    end
    return c
end
