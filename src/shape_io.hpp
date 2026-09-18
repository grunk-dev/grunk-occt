// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch (Step 4b, see DESIGN.md/migration plan) - every
// BRepTools::Write overload takes a Message_ProgressRange (registered, see
// codegen/config.yml's message_progress module) as its LAST parameter, but always
// with a default value (`= Message_ProgressRange()`); this generator doesn't expand
// default arguments (see codegen/README.md's "Known gaps"), so calling it from Lua
// would otherwise require constructing one explicitly every time
// (occt.BRepTools_Write(shape, filename, occt.Message_ProgressRange.new())). A single
// hand-written free function is simpler than teaching the generator default-argument
// expansion for this one call site - same category as legacy grocc's custom_init.cpp
// free functions (serialize_brep, export_to_step).

#pragma once

#include <BRepTools.hxx>
#include <TopoDS_Shape.hxx>

#include <grunk/dynamic.hpp>

#include <string>

inline void register_shape_io(grunk::plugin_namespace& ns)
{
    ns.register_function(
        "export_brep",
        [](TopoDS_Shape const& shape, std::string const& filename) -> bool {
            return BRepTools::Write(shape, filename.c_str()) != Standard_False;
        }
    );
}
