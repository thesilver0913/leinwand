// SPDX-License-Identifier: GPL-3.0-or-later
// Fill and stroke, opacity, the eyedropper and layer commands in the editor.
#include "core/style.h"

#include <catch2/catch_test_macros.hpp>
#include <variant>

#include "editor/editor.h"

using namespace leinwand;
using core::Color;
using core::RgbColor;
using editor::Editor;
using editor::Tool;

namespace {

constexpr double kPick = 4.0;
const RgbColor kRed{1, 0, 0};
const RgbColor kBlue{0, 0, 1};
const RgbColor kGreen{0, 1, 0};

core::ObjectPtr Square(const std::string& id, double x, core::Appearance appearance) {
  core::PathObject path;
  path.common.id = id;
  path.common.appearance = std::move(appearance);
  path.path.anchors = {{{x, 0}}, {{x + 50, 0}}, {{x + 50, 50}}, {{x, 50}}};
  path.path.closed = true;
  return core::MakeObject(std::move(path));
}

// Layer "l1" with a red square "a" at x=0 and a blue-stroked square "b" at
// x=100; layer "l2" on top, empty.
core::Document Sample() {
  core::Layer l1;
  l1.id = "l1";
  l1.children = {Square("a", 0, {core::Fill{kRed}}),
                 Square("b", 100, {core::Stroke{kBlue}, core::Fill{kGreen}})};
  core::Layer l2;
  l2.id = "l2";
  core::Document document;
  document.layers = {core::MakeLayer(std::move(l1)), core::MakeLayer(std::move(l2))};
  return document;
}

const core::Appearance& AppearanceOf(const Editor& editor, const std::string& id) {
  return core::CommonOf(*editor.document().FindObject(id)).appearance;
}

void Click(Editor& editor, core::Point p) {
  editor.PointerDown(p, {}, kPick);
  editor.PointerUp(p, {});
}

}  // namespace

TEST_CASE("Style reports the selection, with mixed values") {
  Editor editor(Sample());
  Click(editor, {25, 25});
  auto style = editor.Style();
  CHECK(style.fill == Color{kRed});
  CHECK_FALSE(style.stroke);
  editor.SelectAll();
  style = editor.Style();
  CHECK(style.fill_mixed);
  CHECK(style.stroke_mixed);
  REQUIRE(style.stroke_style);  // From "b".
  CHECK(style.stroke_style->paint == Color{kBlue});
}

TEST_CASE("Fill and stroke edit the selection in one undo step each") {
  Editor editor(Sample());
  editor.SelectAll();
  editor.SetFill(kBlue);
  CHECK(core::FrontFill(AppearanceOf(editor, "a"))->paint == Color{kBlue});
  CHECK(core::FrontFill(AppearanceOf(editor, "b"))->paint == Color{kBlue});
  CHECK(editor.history().undo_action() == "fill");
  editor.SetStroke(std::nullopt);
  CHECK_FALSE(core::FrontStroke(AppearanceOf(editor, "b")));
  editor.Undo();
  CHECK(core::FrontStroke(AppearanceOf(editor, "b")));
  editor.EditStroke([](core::Stroke& s) { s.width = 5; });
  CHECK(core::FrontStroke(AppearanceOf(editor, "b"))->width == 5);
  CHECK_FALSE(core::FrontStroke(AppearanceOf(editor, "a")));  // No stroke to change.
}

TEST_CASE("With nothing selected, styles change only what new objects get") {
  Editor editor(Sample());
  editor.SetFill(kGreen);
  CHECK_FALSE(editor.history().CanUndo());
  editor.SetTool(Tool::kRectangle);
  editor.PointerDown({200, 200}, {}, kPick);
  editor.PointerMove({250, 250}, {});
  editor.PointerUp({250, 250}, {});
  const auto id = *editor.selection().begin();
  CHECK(core::FrontFill(AppearanceOf(editor, id))->paint == Color{kGreen});
}

TEST_CASE("Selecting an object makes its style the one for new objects") {
  Editor editor(Sample());
  Click(editor, {125, 25});
  CHECK(core::FrontStroke(editor.new_style())->paint == Color{kBlue});
  CHECK(core::FrontFill(editor.new_style())->paint == Color{kGreen});
}

TEST_CASE("Swap and default fill and stroke") {
  Editor editor(Sample());
  Click(editor, {25, 25});
  editor.SwapFillAndStroke();
  CHECK_FALSE(core::FrontFill(AppearanceOf(editor, "a")));
  CHECK(core::FrontStroke(AppearanceOf(editor, "a"))->paint == Color{kRed});
  editor.DefaultFillAndStroke();
  CHECK(core::FrontFill(AppearanceOf(editor, "a"))->paint == Color{RgbColor{1, 1, 1}});
  CHECK(core::FrontStroke(AppearanceOf(editor, "a"))->paint == Color{RgbColor{0, 0, 0}});
}

TEST_CASE("A gesture makes one undo step of many edits") {
  Editor editor(Sample());
  Click(editor, {25, 25});
  editor.BeginGesture();
  for (double o : {0.9, 0.7, 0.5}) editor.SetOpacity(o);
  editor.EndGesture();
  CHECK(core::CommonOf(*editor.document().FindObject("a")).opacity == 0.5);
  editor.Undo();
  CHECK(core::CommonOf(*editor.document().FindObject("a")).opacity == 1.0);
  CHECK_FALSE(editor.history().CanUndo());
}

TEST_CASE("The eyedropper copies the clicked object's appearance to the selection") {
  Editor editor(Sample());
  Click(editor, {25, 25});
  editor.SetTool(Tool::kEyedropper);
  Click(editor, {125, 25});
  CHECK(AppearanceOf(editor, "a") == AppearanceOf(editor, "b"));
  CHECK(editor.history().undo_action() == "eyedropper");
  CHECK(editor.selection() == core::IdSet{"a"});
}

TEST_CASE("Hiding or locking drops the objects from the selection") {
  Editor editor(Sample());
  editor.SelectAll();
  editor.SetItemLocked("a", true);
  CHECK(editor.selection() == core::IdSet{"b"});
  editor.SetItemVisible("l1", false);
  CHECK(editor.selection().empty());
  editor.Undo();
  editor.Undo();
  CHECK(editor.selection().size() == 2);
}

TEST_CASE("New artwork goes into the active layer") {
  Editor editor(Sample());
  editor.SetActiveLayer("l1");
  editor.SetTool(Tool::kRectangle);
  editor.PointerDown({200, 200}, {}, kPick);
  editor.PointerMove({250, 250}, {});
  editor.PointerUp({250, 250}, {});
  CHECK(editor.document().layers[0]->children.size() == 3);
  CHECK(editor.document().layers[1]->children.empty());
}

TEST_CASE("Layers are added, moved and removed, but never the last one") {
  Editor editor(Sample());
  const std::string id = editor.AddLayer("l1", "Layer 3");
  REQUIRE(editor.document().layers.size() == 3);
  CHECK(editor.document().layers[1]->id == id);
  editor.MoveItem("a", id, 0);
  CHECK(editor.document().layers[1]->children.size() == 1);
  editor.RemoveLayer("l1");
  editor.RemoveLayer("l2");
  REQUIRE(editor.document().layers.size() == 1);
  editor.RemoveLayer(id);
  CHECK(editor.document().layers.size() == 1);
}
