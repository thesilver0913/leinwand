// SPDX-License-Identifier: GPL-3.0-or-later
// The Align panel, Average and the scissors tool in the editor.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <variant>

#include "editor/editor.h"
#include "geometry/bezier.h"

using namespace leinwand;
using editor::AlignEdge;
using editor::AlignTo;
using editor::Editor;
using editor::Tool;

namespace {

constexpr double kPick = 4.0;

core::ObjectPtr Rect(const std::string& id, double x, double y, double w, double h) {
  core::PathObject path;
  path.common.id = id;
  path.common.appearance = {core::Fill{}};
  path.path.anchors = {{{x, y}}, {{x + w, y}}, {{x + w, y + h}}, {{x, y + h}}};
  path.path.closed = true;
  return core::MakeObject(std::move(path));
}

core::Document With(std::vector<core::ObjectPtr> objects) {
  core::Layer layer;
  layer.id = "l1";
  for (auto& object : objects) layer.children.push_back(std::move(object));
  core::Document document;
  document.artboards = {{"ab", "Artboard 1", {0, 0, 1000, 800}, {}, 0}};
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

// a: 10..60 x 10..30, b: 100..120 x 50..150, c: 300..400 x 20..40.
core::Document Three() {
  return With(
      {Rect("a", 10, 10, 50, 20), Rect("b", 100, 50, 20, 100), Rect("c", 300, 20, 100, 20)});
}

core::Rect BoundsOf(const Editor& editor, const std::string& id) {
  return geometry::Bounds(*editor.document().FindObject(id));
}

void Click(Editor& editor, core::Point p) {
  editor.PointerDown(p, {}, kPick);
  editor.PointerUp(p, {});
}

}  // namespace

TEST_CASE("Align left and center to the selection") {
  Editor editor(Three());
  editor.SelectAll();
  editor.AlignSelection(AlignEdge::kLeft);
  for (const char* id : {"a", "b", "c"}) CHECK(BoundsOf(editor, id).left == Catch::Approx(10));
  editor.Undo();
  editor.AlignSelection(AlignEdge::kVerticalCenter);
  // The selection spans 10..150, so the centre is 80.
  for (const char* id : {"a", "b", "c"}) {
    const auto b = BoundsOf(editor, id);
    CHECK((b.top + b.bottom) / 2 == Catch::Approx(80));
  }
}

TEST_CASE("A click on a selected object makes it the key object, which stays") {
  Editor editor(Three());
  editor.SelectAll();
  Click(editor, {110, 100});  // b
  CHECK(editor.key_object() == "b");
  CHECK(editor.align_to() == AlignTo::kKeyObject);
  editor.AlignSelection(AlignEdge::kRight);
  CHECK(BoundsOf(editor, "b").right == Catch::Approx(120));
  CHECK(BoundsOf(editor, "a").right == Catch::Approx(120));
  CHECK(BoundsOf(editor, "c").right == Catch::Approx(120));
  // A second click clears it.
  Click(editor, {110, 100});
  CHECK(editor.key_object().empty());
}

TEST_CASE("A lone object aligns to the artboard") {
  Editor editor(Three());
  Click(editor, {30, 20});  // a
  editor.AlignSelection(AlignEdge::kHorizontalCenter);
  const auto b = BoundsOf(editor, "a");
  CHECK((b.left + b.right) / 2 == Catch::Approx(500));
}

TEST_CASE("Align to the artboard moves every object") {
  Editor editor(Three());
  editor.SelectAll();
  editor.SetAlignTo(AlignTo::kArtboard);
  editor.AlignSelection(AlignEdge::kBottom);
  for (const char* id : {"a", "b", "c"}) CHECK(BoundsOf(editor, id).bottom == Catch::Approx(800));
}

TEST_CASE("Distribute spreads edges and gaps evenly") {
  Editor editor(Three());
  editor.SelectAll();
  editor.DistributeSelection(AlignEdge::kLeft);
  // Lefts 10 and 300 stay; b goes halfway.
  CHECK(BoundsOf(editor, "b").left == Catch::Approx(155));
  editor.Undo();
  editor.DistributeSpacing(true);
  // Widths 50 + 20 + 100 = 170 over 10..400: two gaps of 110.
  CHECK(BoundsOf(editor, "b").left == Catch::Approx(60 + 110));
  CHECK(BoundsOf(editor, "c").left == Catch::Approx(300));
}

TEST_CASE("Distribute spacing with a key object and a value") {
  Editor editor(Three());
  editor.SelectAll();
  Click(editor, {110, 100});  // b is the key.
  editor.DistributeSpacing(true, 10.0);
  CHECK(BoundsOf(editor, "b").left == Catch::Approx(100));
  CHECK(BoundsOf(editor, "a").right == Catch::Approx(90));
  CHECK(BoundsOf(editor, "c").left == Catch::Approx(130));
}

TEST_CASE("Average moves anchors to their mean") {
  Editor editor(With({Rect("a", 0, 0, 100, 50)}));
  editor.SetTool(Tool::kDirectSelection);
  // Marquee over the top two anchors.
  editor.PointerDown({-10, -10}, {}, kPick);
  editor.PointerMove({110, 10}, {});
  editor.PointerUp({110, 10}, {});
  REQUIRE(editor.anchor_selection().size() == 2);
  editor.AverageAnchors(false, true);  // Vertical: a shared x.
  const auto& path = std::get<core::PathObject>(*editor.document().FindObject("a")).path;
  CHECK(path.anchors[0].position.x == Catch::Approx(50));
  CHECK(path.anchors[1].position.x == Catch::Approx(50));
  CHECK(path.anchors[2].position.x == Catch::Approx(100));
}

TEST_CASE("Scissors cut a closed path open, and an open path in two") {
  Editor editor(With({Rect("a", 0, 0, 100, 50)}));
  editor.SetTool(Tool::kScissors);
  Click(editor, {50, 0});  // The top edge.
  const auto& opened = std::get<core::PathObject>(*editor.document().FindObject("a")).path;
  CHECK(!opened.closed);
  CHECK(opened.anchors.size() == 6);  // The new anchor appears at both ends.
  Click(editor, {100, 25});           // The right edge of the open path.
  CHECK(editor.selection().size() == 2);
  int paths = 0;
  for (const auto& child : editor.document().layers[0]->children) {
    paths += std::holds_alternative<core::ObjectPtr>(child);
  }
  CHECK(paths == 2);
}
