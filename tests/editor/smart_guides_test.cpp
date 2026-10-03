// SPDX-License-Identifier: GPL-3.0-or-later
// Smart guides: snapping and alignment while drawing and moving.
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <variant>

#include "editor/editor.h"
#include "geometry/hit_test.h"

using namespace leinwand;
using Catch::Approx;
using core::Point;
using editor::Editor;
using editor::Tool;

namespace {

constexpr double kPick = 4.0;

// A 100x100 square "sq" at (100,100); its centre is (150,150).
core::Document Square() {
  core::PathObject sq;
  sq.common.id = "sq";
  sq.common.appearance = {core::Fill{core::RgbColor{1, 0, 0}}};
  sq.path.anchors = {{{100, 100}}, {{200, 100}}, {{200, 200}}, {{100, 200}}};
  sq.path.closed = true;
  core::Layer layer;
  layer.id = "layer";
  layer.children = {core::MakeObject(sq)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

void CheckPoint(Point actual, Point expected) {
  CHECK(actual.x == Approx(expected.x).margin(1e-9));
  CHECK(actual.y == Approx(expected.y).margin(1e-9));
}

const core::PathData& PathOf(const Editor& editor, const std::string& id) {
  const auto found = core::FindObjects(editor.document(), {id});
  REQUIRE(found.size() == 1);
  const auto* path = std::get_if<core::PathObject>(&*found[0].object);
  REQUIRE(path);
  return path->path;
}

}  // namespace

TEST_CASE("A pen click near another object's anchor lands on it") {
  Editor editor(Square());
  editor.SetTool(Tool::kPen);
  editor.PointerDown({302, 198}, {}, kPick);  // Only aligns with the bottom edge.
  editor.PointerUp({302, 198}, {});
  editor.PointerDown({202, 102}, {}, kPick);  // Near the square's top-right corner.
  editor.PointerUp({202, 102}, {});
  const auto ids = core::AllObjectIds(editor.document());
  REQUIRE(ids.size() == 2);
  const std::string id = *std::find_if(ids.begin(), ids.end(), [](auto& i) { return i != "sq"; });
  CheckPoint(PathOf(editor, id).anchors[0].position, {302, 200});  // Aligned with the bottom edge.
  CheckPoint(PathOf(editor, id).anchors[1].position, {200, 100});
}

TEST_CASE("Moving a selection aligns it and shows guides") {
  core::Document document = Square();
  core::PathObject dot;
  dot.common.id = "dot";
  dot.common.appearance = {core::Fill{core::RgbColor{0, 0, 1}}};
  dot.path.anchors = {{{0, 0}}, {{10, 0}}, {{10, 10}}, {{0, 10}}};
  dot.path.closed = true;
  document = core::AddObject(document, core::MakeObject(dot), "layer-2");
  Editor editor(document);
  editor.PointerDown({5, 5}, {}, kPick);  // Grab "dot" at its centre.
  editor.PointerMove({60, 148}, {});      // Within 4 of the square's centre line y=150.
  REQUIRE(editor.overlay().guides.size() == 1);
  editor.PointerUp({60, 148}, {});
  CheckPoint(PathOf(editor, "dot").anchors[0].position, {55, 145});  // Moved by (55, 145).
  CHECK(editor.overlay().guides.empty());                            // Only while dragging.
}

TEST_CASE("Dragging an anchor snaps the anchor, not the pointer") {
  core::Document document = Square();
  core::PathObject line;
  line.common.id = "line";
  line.common.appearance = {core::Stroke{core::RgbColor{0, 0, 0}}};
  line.path.anchors = {{{0, 0}}, {{50, 300}}};
  document = core::AddObject(document, core::MakeObject(line), "layer-2");
  Editor editor(document);
  editor.SetTool(Tool::kDirectSelection);
  editor.PointerDown({2, 1}, {}, kPick);  // Grabs the anchor at (0,0), 2 pt off.
  editor.PointerMove({103, 102}, {});
  editor.PointerUp({103, 102}, {});
  // The anchor would land on (101,101); it snaps to the corner (100,100).
  CheckPoint(PathOf(editor, "line").anchors[0].position, {100, 100});
}

TEST_CASE("Smart guides can be turned off") {
  Editor editor(Square());
  editor.SetSmartGuides(false);
  editor.SetTool(Tool::kRectangle);
  editor.PointerDown({202, 102}, {}, kPick);
  editor.PointerMove({250, 150}, {});
  editor.PointerUp({250, 150}, {});
  const auto bounds = editor.SelectionBounds();
  REQUIRE(bounds);
  CHECK(bounds->left == Approx(202));
  CHECK(bounds->top == Approx(102));
}
