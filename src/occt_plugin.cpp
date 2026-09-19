// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 0 built an empty grunk plugin proving the whole toolchain (pixi -> cmake ->
// conda-forge OCCT -> dlopen -> grunk::state). Step 1 hand-wrote the first real
// module (gp); Step 2's generator now reproduces (and exceeds) it. Step 3 added
// Standard_Transient/Handle(T) + add_bases support (geom module). Step 4 adds plain
// value-type inheritance (topods), static-method/namespace flattening (also topods),
// and the box/cylinder/cut boolean-op smoke test (brepbuilderapi/brepprimapi/
// brepalgoapi) - see generated/*.cpp, codegen/README.md, src/geom/geom_extras.hpp,
// src/shape_io.hpp. Step 5 adds NCollection_Array1/2 <-> std::vector<T> support
// (Geom_BezierCurve, src/collections.hpp). Step 6 is the module expansion sweep -
// bnd (Bnd_Box/BRepBndLib), brep (BRep_Builder/BRep_Tool), breptools
// (BRepTools), toploc (TopLoc_Location), gc (GC_Root/GC_MakeSegment/
// GC_MakeCircle), brepoffsetapi (BRepOffsetAPI_*, whole-module wildcard), and
// geomapi (GeomAPI_*, whole-module wildcard), and brepfilletapi (BRepFilletAPI_*,
// whole-module wildcard) so far - toploc retroactively unlocked several
// TopLoc_Location&/const& parameters in topods/brep that were rejected before it
// existed. gc's own Value() methods return Handle(Geom_TrimmedCurve)/
// Handle(Geom_Circle), registered in a *different* generated file (geom.cpp) -
// this only works because occt_plugin is built as a single translation unit (see
// CMakeLists.txt's unity-build note and DESIGN.md's "Handle(X) return type
// requires being in the same translation unit as X").

#include <grunk/dynamic.hpp>
#include <grunk/plugin/loader.hpp>

#include "../generated/gp.hpp"
#include "../generated/toploc.hpp"
#include "../generated/geom.hpp"
#include "../generated/message_progress.hpp"
#include "../generated/topods.hpp"
#include "../generated/brepbuilderapi.hpp"
#include "../generated/brepprimapi.hpp"
#include "../generated/brepalgoapi.hpp"
#include "../generated/bnd.hpp"
#include "../generated/brep.hpp"
#include "../generated/breptools.hpp"
#include "../generated/gc.hpp"
#include "../generated/brepoffsetapi.hpp"
#include "../generated/geomapi.hpp"
#include "../generated/brepfilletapi.hpp"
#include "gp/gp_extras.hpp"
#include "brep/brep_extras.hpp"
#include "geom/geom_extras.hpp"
#include "gc/gc_extras.hpp"
#include "shape_io.hpp"
#include "topexp_extras.hpp"
#include "math_extras.hpp"

namespace {

void register_occt(grunk::state& state, grunk::PluginInfo const& info)
{
    auto ns = state.begin_plugin(info);
    register_gp(ns);
    register_gp_extras(ns);
    register_toploc(ns);
    register_geom(ns);
    register_geom_extras(ns);
    register_message_progress(ns);
    register_topods(ns);
    register_brepbuilderapi(ns);
    register_brepprimapi(ns);
    register_brepalgoapi(ns);
    register_bnd(ns);
    register_brep(ns);
    register_brep_extras(ns);
    register_breptools(ns);
    register_gc(ns);
    register_gc_extras(ns);
    register_brepoffsetapi(ns);
    register_geomapi(ns);
    register_brepfilletapi(ns);
    register_shape_io(ns);
    register_topexp_extras(ns);
    register_math_extras(ns);
}

} // anonymous namespace

GRUNK_PLUGIN_INFO("occt", "0.1.0")
GRUNK_PLUGIN_REGISTER(register_occt)
