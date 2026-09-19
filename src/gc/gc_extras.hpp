// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch (Step 6.5, see DESIGN.md/migration plan) - GC_MakeArcOfCircle
// ::Value() returns Handle(Geom_TrimmedCurve), and BRepBuilderAPI_MakeEdge's only
// curve-taking constructors accept Handle(Geom_Curve) (the abstract base), never a
// concrete Handle(Geom_TrimmedCurve) - the exact same "Handle(Derived) does not
// satisfy a Handle(Base) function/constructor parameter" sol2 limitation as
// GeomAPI_ProjectPointOnCurve (see DESIGN.md's geomapi note and
// https://github.com/grunk-dev/grunk-occt/issues/1), surfacing again here because
// examples/tank_model.grr.yml's own cylinder_segment sub-recipe needs exactly this
// three-point-arc-to-edge conversion (ported from legacy grocc). Rather than fix the
// general sol2 problem (deferred, tracked in the issue above), this narrow free
// function does the whole GC_MakeArcOfCircle -> BRepBuilderAPI_MakeEdge chain in one
// C++ call: the upcast from Handle(Geom_TrimmedCurve) to Handle(Geom_Curve) happens
// in plain C++ at the BRepBuilderAPI_MakeEdge call site below, never crossing the
// sol2 argument-matching boundary that can't do it, so it works today without
// waiting on that issue.

#pragma once

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>

#include <grunk/dynamic.hpp>

inline void register_gc_extras(grunk::plugin_namespace& ns)
{
    ns.register_function(
        "edge_from_arc",
        [](gp_Pnt const& p1, gp_Pnt const& p_on_arc, gp_Pnt const& p2) -> TopoDS_Edge {
            GC_MakeArcOfCircle arc_maker(p1, p_on_arc, p2);
            return BRepBuilderAPI_MakeEdge(arc_maker.Value()).Edge();
        }
    );
}
