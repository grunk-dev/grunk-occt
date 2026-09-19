// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch (Step 6, see DESIGN.md/migration plan) - TopExp_Explorer's
// More()/Next()/Current() iterator pattern has no generator support (same category of
// gap as legacy grocc's own iterator-shaped classes), so there is currently no way to
// pull an individual sub-shape (e.g. a single face) out of a compound shape like a
// BRepPrimAPI_MakeBox/MakeCylinder result from Lua. first_face is a thin, single-use
// wrapper around the first TopAbs_FACE TopExp_Explorer finds - net-new relative to any
// one OCCT header, same category as shape_io.hpp's own export_brep, needed by
// tests/test_brepoffsetapi.cpp: BRepOffsetAPI_MakeThickSolid::MakeThickSolidBySimple
// (and BRepOffsetAPI_MakeOffsetShape::PerformBySimple, its base class equivalent)
// explicitly require "a non-closed shell or face" as input, not a closed solid - see
// BRepOffsetAPI_MakeThickSolid.hxx's own doc comment.

#pragma once

#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS.hxx>

#include <grunk/dynamic.hpp>

#include <stdexcept>

inline void register_topexp_extras(grunk::plugin_namespace& ns)
{
    ns.register_function(
        "first_face",
        [](TopoDS_Shape const& shape) -> TopoDS_Face {
            TopExp_Explorer explorer(shape, TopAbs_FACE);
            if (!explorer.More()) {
                throw std::runtime_error("first_face: shape contains no TopAbs_FACE sub-shape");
            }
            return TopoDS::Face(explorer.Current());
        }
    );
}
