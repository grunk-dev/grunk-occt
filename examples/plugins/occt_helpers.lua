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
-- from the calling recipe's tracked features - need unwrapping, and only when
-- extracted from inside a table argument (see polygon_face/wire_face/compound's
-- own ":value()" calls below): a plain top-level parameter (shape/vec/axis/...)
-- already arrives unwrapped, but a table's individual elements do not.

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

-- Builds a closed planar face from an ordered list of gp_Pnt (a Lua array/table),
-- via BRepBuilderAPI_MakePolygon (Add one point at a time, then Close) followed by
-- BRepBuilderAPI_MakeFace on the resulting wire. BRepBuilderAPI_MakeFace's own
-- trailing OnlyPlane (default Standard_False) is likewise omitted below.
function polygon_face(points)
    local mp = occt.BRepBuilderAPI_MakePolygon.new()
    for i = 1, #points do
        mp:Add(points[i]:value())
    end
    mp:Close()
    return occt.BRepBuilderAPI_MakeFace.new(mp:Wire()):Face()
end

-- Builds a face from an ordered list of already-built TopoDS_Edge (a Lua
-- array/table), via BRepBuilderAPI_MakeWire (Add each edge) followed by
-- BRepBuilderAPI_MakeFace on the resulting wire.
function wire_face(edges)
    local mw = occt.BRepBuilderAPI_MakeWire.new()
    for i = 1, #edges do
        mw:Add(edges[i]:value())
    end
    return occt.BRepBuilderAPI_MakeFace.new(mw:Wire()):Face()
end

-- Builds a TopoDS_Compound out of an arbitrary list of shapes (a Lua array/table),
-- via BRep_Builder/TopoDS_Builder's MakeCompound + Add.
function compound(shapes)
    local c = occt.TopoDS_Compound.new()
    local builder = occt.BRep_Builder.new()
    builder:MakeCompound(c)
    for i = 1, #shapes do
        builder:Add(c, shapes[i]:value())
    end
    return c
end
