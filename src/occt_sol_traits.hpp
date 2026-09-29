// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
// SPDX-FileCopyrightText: 2026 Mladen Banocic <mladen.banovic@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <Standard_Handle.hxx>
#include <sol/sol.hpp>

// Lets sol2 treat an OCCT opencascade::handle<T> (i.e. Handle(T)) like any other
// unique-ownership smart pointer, so a Handle can be returned from a registered
// grunk function or pulled back out of a grunk::object via .as<Handle(T)>() without
// a raw-pointer detour - ported verbatim from grunk's cad_autodiff example
// (plugins/occt_sol_traits.hpp).
//
// rebind_actual_type (added 2026-09-28) is the missing piece that makes sol2
// actually perform a Handle(Derived)->Handle(Base) upcast at all - without it, any
// call site that receives some Handle(Geom_BSplineCurve)-shaped value where a
// Handle(Geom_Curve) (or any other base) is expected silently fails (a runtime
// getter returns a null handle; sol2's own overload resolution rejects the call
// outright with "no matching function call") REGARDLESS of add_bases<...>() being
// registered correctly - `add_bases` alone is not enough for a *unique* usertype
// like a smart pointer. sol2's own std::shared_ptr<T> support (sol/
// unique_usertype_traits.hpp's own unique_fallback<std::shared_ptr<T>>) defines the
// exact same member for the exact same reason; grunk-occt's own specialization here
// simply never had it. Root-caused precisely: sol2's inheritance<T>::type_unique_cast
// (sol/inheritance.hpp) is what walks a type's own add_bases<...>() list to satisfy
// a request for a base type at the point a Handle(Derived) value crosses back from
// Lua into C++ - but it only does that walk when
// unique_usertype_traits<T>::rebind_actual_type<Base> exists (gated by
// is_actual_type_rebindable_for_v, sol/unique_usertype_traits.hpp); without it, the
// base list it walks is unconditionally empty (types<>()) and the cast always
// "fails" (returns 0), independent of what add_bases<...>() actually declared. Once
// present, the conversion itself (sol/inheritance.hpp's type_unique_cast_bases:
// `*target = *source;`, a Handle(Base) = Handle(Derived) assignment) compiles and
// works correctly even in an OCCT build without OCCT_HANDLE_NOCAST (no value-
// constructing converting constructor, only an implicit reference-conversion
// operator, opencascade::handle<T>'s own Standard_Handle.hxx) - the assignment's
// right-hand side binds through that reference-conversion operator, then Handle(Base)'s
// own ordinary (always-available, non-templated) copy-assignment operator runs, with
// correct reference counting. Verified live: fixes BRepBuilderAPI_MakeFace's
// generated constructor overload set (a Handle(Geom_Surface) parameter previously
// rejected any Handle(Geom_BSplineSurface)/etc. argument) with zero changes to the
// generated code itself, and fixes the equivalent Handle(Geom_Curve) case
// grunk-geoml's own interpolate_curves/curve_vec needed a local workaround for
// (see that repo's own DESIGN.md) - this is a single, generic, root-cause fix for
// every Handle(X) derived-to-base conversion anywhere sol2 crosses the Lua
// boundary, not specific to any one class.
namespace sol {
    template <typename T>
    struct unique_usertype_traits<opencascade::handle<T>> {
        using type = T;
        using actual_type = opencascade::handle<T>;
        static const bool value = true;

        template <typename X>
        using rebind_actual_type = opencascade::handle<X>;

        static bool is_null(const actual_type& ptr) {
            return ptr.IsNull();
        }

        static type* get(const actual_type& ptr) {
            return ptr.get();
        }
    };
}
