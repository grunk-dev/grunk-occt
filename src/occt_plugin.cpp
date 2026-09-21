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
// this only works because occt_plugin is compiled with the translation-unit
// grouping CMakeLists.txt reads from generated/translation_units.txt (see
// codegen/config.yml's own translation_units: section and DESIGN.md's
// "Translation-unit grouping"/"Handle(X) return type requires being in the same
// translation unit as X"). Step 7 (AD validation milestone, see DESIGN.md)
// compiles this *exact same* source - unmodified below this comment - against an
// AD-enabled OCCT (adOCCT) instead of stock OCCT, with GRUNK_OCCT_WITH_ADOLC
// defined, when configured with -DGRUNK_OCCT_ENABLE_AD=ON (see CMakeLists.txt and
// README.md's "Building with AD support" - a build option on this one project, not
// a separate CMake tree).

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
#include "vector_extras.hpp"
#include "occt_ad_support.hpp"

namespace {

void register_occt(grunk::state& state, grunk::PluginInfo const& info)
{
    auto ns = state.begin_plugin(info);
#if defined(GRUNK_OCCT_WITH_ADOLC)
    // Standard_Adouble is genuinely OCCT's own type (adOCCT's own derived class
    // over grunk-adolc's adtl::adouble - see occt_ad_support.hpp's own comment for
    // why this needs its own registration, not just reusing adtl::adouble's), so it
    // belongs here, not in grunk-adolc, which has no OCCT dependency at all.
    occt_plugin::register_standard_adouble(ns);
#endif
    // No runtime check here that grunk-adolc's own "adtl" plugin is loaded, even
    // though this build (GRUNK_OCCT_WITH_ADOLC) needs it before any AD-enabled OCCT
    // Lua code runs - see occt_ad_support.hpp's own comment and DESIGN.md's
    // "Reusing one shared Standard_Real/adtl::adouble registration, not two" for why
    // and what happens if that requirement isn't met (occt_plugin::to_real's
    // is<Standard_Real>()/is<adtl::adouble>() checks silently take the plain-double
    // fallback branch instead of failing loudly). An earlier version of this
    // function *did* check state.plugins() for "adtl" here, but that check ran at
    // this plugin's own registration time, which wrongly forced a specific load
    // order (adtl must load before occt) rather than the actual requirement (both
    // loaded before any AD Lua code executes) - see grunk-dev/grunk#287, filed to
    // give grunk itself a general, load-order-independent mechanism for a plugin to
    // declare this kind of runtime dependency, rather than every plugin
    // hand-rolling its own state.plugins() scan at the wrong checkpoint. Once that
    // lands, occt_plugin should use it instead of staying silent here.
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
    register_vector_extras(ns);
}

} // anonymous namespace

GRUNK_PLUGIN_INFO("occt", "0.1.0")
GRUNK_PLUGIN_REGISTER(register_occt)
