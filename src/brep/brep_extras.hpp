// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch - BRep_Builder has no explicit constructor of its own
// (it's a stateless builder class; a plain compiler-synthesized default
// constructor). The generator's AST walk only ever sees an explicitly-declared
// CONSTRUCTOR cursor, so an implicit one is invisible to it - a "missing implicit
// constructors" case, the same category as e.g. TopoDS_Shape's own converting
// constructors from leaf shape types.
//
// Uses modify_type, not register_type: BRep_Builder is already registered
// generically by the generator (generated/brep.cpp, with its real methods and
// add_bases<TopoDS_Builder>()) - this only adds the missing constructor on top of
// that existing registration (register_brep must run first - see occt_plugin.cpp's
// ordering).

#pragma once

#include <BRep_Builder.hxx>

#include <grunk/dynamic.hpp>

inline void register_brep_extras(grunk::plugin_namespace& ns)
{
    ns.modify_type<BRep_Builder>("BRep_Builder").add_constructors(
        []() { return BRep_Builder(); }
    );
}
