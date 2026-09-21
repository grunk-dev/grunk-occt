// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// std::vector<T> <-> NCollection_Array1/2 conversion helpers (Step 5, see
// DESIGN.md/migration plan). Used by the generator itself (codegen/generate.py's
// "array1"/"array2" parameter kind - see codegen/README.md's "NCollection_Array1/2
// support") for any by-value/const-reference NCollection_Array1<T>/
// NCollection_Array2<T> parameter (e.g. TColgp_Array1OfPnt is a typedef for
// NCollection_Array1<gp_Pnt>, invisible as such without canonical type resolution),
// and available for any remaining hand-written escape hatch that needs the same
// conversion in a shape the generator doesn't cover yet (a mutable-reference
// out-param, or a returned array - see codegen/README.md's "Known gaps"). No
// NCollection type is ever itself exposed to Lua - only std::vector<T> crosses the
// Lua boundary, converted at the C++ binding site.

#pragma once

#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <Standard_TypeDef.hxx>

#include <stdexcept>
#include <tuple>
#include <vector>

namespace occt_plugin {

// 1-based ([1, v.size()]), matching every OCCT array-taking API this project has
// needed so far (Geom_BezierCurve's poles/weights, ...) and OCCT's own array
// convention throughout its documentation.
template <typename T>
inline NCollection_Array1<T> to_array1(std::vector<T> const& v)
{
    NCollection_Array1<T> arr(1, static_cast<Standard_Integer>(v.size()));
    for (std::size_t i = 0; i < v.size(); ++i) {
        arr.SetValue(static_cast<Standard_Integer>(i + 1), v[i]);
    }
    return arr;
}

// For an array whose *OCCT* element type is Standard_Real specifically (e.g.
// TColStd_Array1OfReal) - distinct from the plain to_array1<T> above because the
// *Lua-side* vector's own element type is always plain double regardless of build
// (see codegen/generate.py's array_element_supported() - AD dual-emission is
// deliberately not extended to array elements themselves), while the *target*
// OCCT array's element type is Standard_Real, which is a different C++ type from
// double under GRUNK_OCCT_WITH_ADOLC (Standard_Adouble, not a typedef - see
// src/occt_ad_support.hpp's own comment on that). Converting element-by-element
// through Standard_Real's own converting constructor from double (a real value,
// not an AD-tracked one - these are array elements like a B-spline's own knot
// vector, structural data, never themselves a seeded design parameter) is
// correct and compiles in both builds: in a plain one Standard_Real already is
// double, so this is identical to to_array1<double> in every way but name.
inline NCollection_Array1<Standard_Real> to_real_array1(std::vector<double> const& v)
{
    NCollection_Array1<Standard_Real> arr(1, static_cast<Standard_Integer>(v.size()));
    for (std::size_t i = 0; i < v.size(); ++i) {
        arr.SetValue(static_cast<Standard_Integer>(i + 1), Standard_Real(v[i]));
    }
    return arr;
}

template <typename T>
inline std::vector<T> from_array1(NCollection_Array1<T> const& arr)
{
    std::vector<T> v;
    v.reserve(static_cast<std::size_t>(arr.Length()));
    for (Standard_Integer i = arr.Lower(); i <= arr.Upper(); ++i) {
        v.push_back(arr.Value(i));
    }
    return v;
}

// NCollection_Array2's Lua-side shape (see DESIGN.md's "NCollection_Array2 Lua-side
// shape" decision): a single flat std::vector<T> (row-major: row r, column c is at
// index r*cols + c) plus separate row/column counts - not a nested
// std::vector<std::vector<T>>. A nested vector's own type doesn't prevent a jagged
// (non-rectangular) input, and is one heap allocation per row rather than one
// contiguous buffer for the whole array; a flat vector is rectangular by
// construction (its size is exactly rows*cols, checked below) and contiguous.
template <typename T>
inline NCollection_Array2<T> to_array2(std::vector<T> const& flat, Standard_Integer rows, Standard_Integer cols)
{
    if (rows <= 0 || cols <= 0) {
        throw std::invalid_argument("to_array2: rows and cols must both be positive");
    }
    if (flat.size() != static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols)) {
        throw std::invalid_argument("to_array2: flat.size() must equal rows*cols");
    }
    NCollection_Array2<T> arr(1, rows, 1, cols);
    for (Standard_Integer r = 0; r < rows; ++r) {
        for (Standard_Integer c = 0; c < cols; ++c) {
            arr.SetValue(r + 1, c + 1, flat[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)]);
        }
    }
    return arr;
}

// Array2 counterpart to to_real_array1 above - see its own comment for why this
// needs to be a separate function from to_array2<T>, not just a call with
// T=Standard_Real (the source std::vector is always <double>, not <Standard_Real>).
inline NCollection_Array2<Standard_Real> to_real_array2(std::vector<double> const& flat, Standard_Integer rows, Standard_Integer cols)
{
    if (rows <= 0 || cols <= 0) {
        throw std::invalid_argument("to_real_array2: rows and cols must both be positive");
    }
    if (flat.size() != static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols)) {
        throw std::invalid_argument("to_real_array2: flat.size() must equal rows*cols");
    }
    NCollection_Array2<Standard_Real> arr(1, rows, 1, cols);
    for (Standard_Integer r = 0; r < rows; ++r) {
        for (Standard_Integer c = 0; c < cols; ++c) {
            arr.SetValue(r + 1, c + 1, Standard_Real(flat[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)]));
        }
    }
    return arr;
}

// Returns (flat, rows, cols) - see to_array2's own comment for the row-major layout.
template <typename T>
inline std::tuple<std::vector<T>, Standard_Integer, Standard_Integer> from_array2(NCollection_Array2<T> const& arr)
{
    Standard_Integer const rows = arr.NbRows();
    Standard_Integer const cols = arr.NbColumns();
    std::vector<T> flat;
    flat.reserve(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols));
    for (Standard_Integer r = arr.LowerRow(); r <= arr.UpperRow(); ++r) {
        for (Standard_Integer c = arr.LowerCol(); c <= arr.UpperCol(); ++c) {
            flat.push_back(arr.Value(r, c));
        }
    }
    return {std::move(flat), rows, cols};
}

} // namespace occt_plugin
