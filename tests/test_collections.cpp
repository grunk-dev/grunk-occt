// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Unit tests for src/collections.hpp - pure C++, no grunk/Lua/plugin involved. Only
// to_array1<gp_Pnt> is otherwise exercised indirectly (Geom_BezierCurve's poles, see
// tests/test_geom.cpp); from_array1 and the whole Array2 path (flat + row/col
// counts, not yet used by any real OCCT binding) have no other coverage.

#include "../src/collections.hpp"

#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

TEST(Collections, Array1RoundTrip)
{
    std::vector<double> const original = {1.0, 2.0, 3.0, 4.0};
    NCollection_Array1<double> const arr = occt_plugin::to_array1(original);

    EXPECT_EQ(arr.Lower(), 1);
    EXPECT_EQ(arr.Upper(), 4);
    EXPECT_EQ(arr.Value(1), 1.0);
    EXPECT_EQ(arr.Value(4), 4.0);

    std::vector<double> const round_tripped = occt_plugin::from_array1(arr);
    EXPECT_EQ(round_tripped, original);
}

TEST(Collections, Array1OfUsertype)
{
    std::vector<gp_Pnt> const poles = {gp_Pnt(0., 0., 0.), gp_Pnt(1., 1., 0.)};
    NCollection_Array1<gp_Pnt> const arr = occt_plugin::to_array1(poles);
    EXPECT_EQ(arr.Value(1).X(), 0.);
    EXPECT_EQ(arr.Value(2).X(), 1.);
}

TEST(Collections, Array2RoundTrip)
{
    // 2 rows x 3 cols, row-major.
    std::vector<double> const flat = {1., 2., 3., 4., 5., 6.};
    NCollection_Array2<double> const arr = occt_plugin::to_array2(flat, 2, 3);

    EXPECT_EQ(arr.NbRows(), 2);
    EXPECT_EQ(arr.NbColumns(), 3);
    EXPECT_EQ(arr.Value(1, 1), 1.);
    EXPECT_EQ(arr.Value(1, 3), 3.);
    EXPECT_EQ(arr.Value(2, 1), 4.);
    EXPECT_EQ(arr.Value(2, 3), 6.);

    auto [round_tripped, rows, cols] = occt_plugin::from_array2(arr);
    EXPECT_EQ(rows, 2);
    EXPECT_EQ(cols, 3);
    EXPECT_EQ(round_tripped, flat);
}

TEST(Collections, Array2RejectsMismatchedSize)
{
    std::vector<double> const flat = {1., 2., 3.}; // 3 elements, but 2*2 = 4 expected
    EXPECT_THROW(occt_plugin::to_array2(flat, 2, 2), std::invalid_argument);
}

TEST(Collections, Array2RejectsNonPositiveDims)
{
    std::vector<double> const flat = {1., 2.};
    EXPECT_THROW(occt_plugin::to_array2(flat, 0, 2), std::invalid_argument);
    EXPECT_THROW(occt_plugin::to_array2(flat, 2, -1), std::invalid_argument);
}
