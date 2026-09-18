// SPDX-FileCopyrightText: 2026 Jan Kleinert <jan.kleinert@dlr.de>
//
// SPDX-License-Identifier: MPL-2.0

// Step 4b milestone: reproduces legacy grocc's test_simple_boolean_op.cpp (box,
// cylinder, cut) - functional parity with the legacy plugin's core demonstrated use
// case. Exercises: TopoDS_Shape + leaf-type plain-value inheritance (add_bases,
// generalized from Step 3's Handle(T) case to ordinary value types - see DESIGN.md),
// BRepBuilderAPI_MakeShape::Shape()/BRepBuilderAPI_Command::IsDone() reached via
// add_bases from BRepPrimAPI_MakeBox/MakeCylinder/BRepAlgoAPI_Cut (a 4-5 level
// inheritance chain, most of it never separately registered - see
// codegen/README.md), Message_ProgressRange unlocking BRepAlgoAPI_Cut's own
// constructor, and TopoDS.hxx's namespace-flattened DownCast helpers
// (occt.TopoDS_Solid.DownCast).

#include <grunk/dynamic.hpp>
#include <grunk/plugin.hpp>

#include <iostream>

namespace {

bool check(bool ok, char const* what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
    }
    return ok;
}

} // namespace

int main()
{
    grunk::state state;
    grunk::plugin::load_native(state, OCCT_PLUGIN_SO_PATH);

    auto env = state.create_env();

    auto res = env.eval(R"(
        box_maker = occt.BRepPrimAPI_MakeBox.new(10., 10., 10.)
        -- IsDone() is checked *after* Shape() deliberately: BRepBuilderAPI_MakeShape::
        -- Shape() calls Build() lazily if not already done, so BRepPrimAPI_MakeBox's
        -- IsDone() is only guaranteed true once Shape() has actually been asked for
        -- (unlike BRepAlgoAPI_Cut below, whose 2-shape constructor builds eagerly).
        box = box_maker:Shape()
        box_done = box_maker:IsDone()
        box_is_null = box:IsNull()

        cyl_maker = occt.BRepPrimAPI_MakeCylinder.new(3., 20.)
        cylinder = cyl_maker:Shape()

        progress = occt.Message_ProgressRange.new()
        cut_maker = occt.BRepAlgoAPI_Cut.new(box, cylinder, progress)
        result = cut_maker:Shape()
        cut_done = cut_maker:IsDone()
        result_is_null = result:IsNull()

        -- TopoDS.hxx's namespace-flattened DownCast helper (Step 4's spike) - box is
        -- a genuine TopoDS_Solid under the hood (BRepPrimAPI_MakeBox always builds
        -- one), so downcasting it back from the generic TopoDS_Shape succeeds.
        as_solid = occt.TopoDS_Solid.DownCast(box)
        as_solid_is_null = as_solid:IsNull()

        export_ok = occt.export_brep(result, "step4_cut.brep")
    )");
    if (!res.valid()) {
        std::cerr << "FAIL: eval failed\n";
        return 1;
    }

    bool ok = true;
    ok &= check(env.get<bool>("box_done"), "BRepPrimAPI_MakeBox::IsDone (via add_bases through BRepBuilderAPI_MakeShape/Command)");
    ok &= check(!env.get<bool>("box_is_null"), "BRepBuilderAPI_MakeShape::Shape() on the box (via add_bases)");
    ok &= check(env.get<bool>("cut_done"), "BRepAlgoAPI_Cut::IsDone (own constructor unlocked by registering Message_ProgressRange)");
    ok &= check(!env.get<bool>("result_is_null"), "BRepAlgoAPI_Cut::Shape() on the cut result");
    ok &= check(!env.get<bool>("as_solid_is_null"), "TopoDS.hxx's TopoDS::Solid downcast helper (occt.TopoDS_Solid.DownCast)");
    ok &= check(env.get<bool>("export_ok"), "export_brep (hand-written escape hatch wrapping BRepTools::Write)");

    if (!ok) {
        return 1;
    }

    std::cout << "OK: shapes (box/cylinder/cut boolean-op smoke test) verified\n";
    return 0;
}
