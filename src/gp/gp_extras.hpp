// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Hand-written escape hatch (Step 5, see DESIGN.md/migration plan) - grunk's
// with_std_vector() (Foo.as_vec(...)/Foo.new_vec(), see grunk/dynamic/usertype_proxy.hpp)
// is a grunk-level convenience the generator does not emit for any type yet (see
// codegen/README.md's "Known gaps"); opted in here, by hand, for gp_Pnt - the
// concrete type Step 5's bezier_curve (src/geom/bezier.hpp) needs to collect into a
// std::vector<gp_Pnt> from Lua before converting it into an OCCT array via
// collections.hpp's to_array1().
//
// Uses modify_type, not register_type: gp_Pnt is already registered generically by
// the generator (generated/gp.cpp) - this only adds the as_vec/new_vec members on
// top of that existing registration, it does not re-register the type (register_gp
// must run first - see occt_plugin.cpp's ordering).

#pragma once

#include <gp_Pnt.hxx>

#include <grunk/dynamic.hpp>

inline void register_gp_extras(grunk::plugin_namespace& ns)
{
    ns.modify_type<gp_Pnt>("gp_Pnt").with_std_vector();
}
