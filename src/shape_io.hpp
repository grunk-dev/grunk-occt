// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch - BRepTools::Write is a static free function in a
// static-only namespace class, outside what codegen/config.yml's breptools module
// wraps; every overload also takes a Message_ProgressRange (registered, see
// codegen/config.yml's message_progress module) as its last parameter, always with
// a default value (`= Message_ProgressRange()`), so calling it from Lua would
// otherwise require constructing one explicitly every time
// (occt.BRepTools_Write(shape, filename, occt.Message_ProgressRange.new())). A
// single hand-written free function with a simpler two-argument signature is
// simpler than extending the generator for this one call site.

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
