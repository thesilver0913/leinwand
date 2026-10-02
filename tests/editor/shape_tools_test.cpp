// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <variant>

#include "editor/editor.h"
#include "geometry/bezier.h"

using namespace leinwand;
using Catch::Approx;
using core::Point;
using core::Rect;
using editor::Editor;
using editor::Modifiers;
using editor::Tool;

namespace {

constexpr double kPick = 4.0;
const Modifiers kShift{.shift = true};
const Modifiers kAlt{.alt = true};

core::Document Empty() {
  core::Layer layer;
  layer.id = "layer";
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

void Drag(Editor& editor, Point from, Point to, Modifiers modifiers = {}) {
  editor.PointerDown(from, modifiers, kPick);
  editor.PointerMove(to, modifiers);
  editor.PointerUp(to, modifiers);
}

const core::ShapeObject& Selected(const Editor& editor) {
  REQUIRE(editor.selection().size() == 1);
  const auto found = core::FindObjects(editor.document(), editor.selection());
  REQUIRE(found.size() == 1);
  const auto* shape = std::get_if<core::ShapeObject>(&*found[0].object);
  REQUIRE(shape);
  return *shape;
}

Rect SelectedBounds(const Editor& editor) { return editor.Info()->bounds; }

void CheckRect(const Rect& actual, const Rect& expected) {
  CHECK(actual.left == Approx(expected.left).margin(1e-6));
  CHECK(actual.top == Approx(expected.top).margin(1e-6));
  CHECK(actual.right == Approx(expected.right).margin(1e-6));
  CHECK(actual.bottom == Approx(expected.bottom).margin(1e-6));
}

}  // namespace

TEST_CASE("The rectangle tool draws corner to corner, as one undo step") {
  Editor editor(Empty());
  editor.SetTool(Tool::kRectangle);
  Drag(editor, {10, 20}, {110, 70});
  CHECK(std::holds_alternative<core::RectangleShape>(Selected(editor).shape));
  CheckRect(SelectedBounds(editor), {10, 20, 110, 70});
  CHECK(editor.history().undo_action() == "draw");
  // Illustrator's default style: stroke in front of a white fill.
  CHECK(Selected(editor).common.appearance.size() == 2);
  editor.Undo();
  CHECK(core::AllObjectIds(editor.document()).empty());
}

TEST_CASE("Dragging up and left works, and Shift and Alt shape the result") {
  Editor editor(Empty());
  editor.SetTool(Tool::kEllipse);
  Drag(editor, {100, 100}, {40, 70});
  CheckRect(SelectedBounds(editor), {40, 70, 100, 100});
  Drag(editor, {0, 0}, {50, 20}, kShift);  // Circle: the longer side wins.
  CheckRect(SelectedBounds(editor), {0, 0, 50, 50});
  Drag(editor, {200, 200}, {230, 210}, kAlt);  // From the centre.
  CheckRect(SelectedBounds(editor), {170, 190, 230, 210});
}

TEST_CASE("A click without a drag draws nothing") {
  Editor editor(Empty());
  editor.SetTool(Tool::kRectangle);
  Drag(editor, {10, 10}, {11, 10});
  CHECK(core::AllObjectIds(editor.document()).empty());
  CHECK_FALSE(editor.history().CanUndo());
}

TEST_CASE("Polygons and stars grow from the centre; arrows change the count") {
  Editor editor(Empty());
  editor.SetTool(Tool::kPolygon);
  editor.PointerDown({100, 100}, {}, kPick);
  editor.PointerMove({100, 50}, kShift);
  editor.AdjustToolCount(-3);  // Up/down arrows while dragging.
  editor.PointerUp({100, 50}, kShift);
  const auto& triangle = std::get<core::PolygonShape>(Selected(editor).shape);
  CHECK(triangle.sides == 3);
  CHECK(triangle.radius == Approx(50));
  CHECK(SelectedBounds(editor).top == Approx(50));  // Upright: a vertex at the top.

  editor.SetTool(Tool::kStar);
  Drag(editor, {300, 300}, {300, 260}, kShift);
  const auto& star = std::get<core::StarShape>(Selected(editor).shape);
  CHECK(star.points == 5);
  CHECK(star.outer_radius == Approx(40));
  CHECK(star.inner_radius == Approx(20));
}

TEST_CASE("Lines go from the press to the release, with no fill") {
  Editor editor(Empty());
  editor.SetTool(Tool::kLine);
  Drag(editor, {0, 0}, {30, 40});
  const auto& line = Selected(editor);
  CHECK(std::get<core::LineShape>(line.shape).length == Approx(50));
  CHECK(line.common.appearance.size() == 1);
  CHECK(std::holds_alternative<core::Stroke>(line.common.appearance[0]));
  Drag(editor, {0, 0}, {100, 10}, kShift);  // Snaps to horizontal.
  CheckRect(SelectedBounds(editor), {0, 0, 100, 0});
}

TEST_CASE("New shapes go into the frontmost unlocked layer, or a new one") {
  core::Document document = Empty();
  core::Layer locked;
  locked.id = "locked";
  locked.locked = true;
  document.layers.push_back(core::MakeLayer(std::move(locked)));
  Editor editor(document);
  editor.SetTool(Tool::kRectangle);
  Drag(editor, {0, 0}, {10, 10});
  CHECK(editor.document().layers[0]->children.size() == 1);  // Not the locked top layer.

  Editor bare(core::Document{});
  bare.SetTool(Tool::kRectangle);
  Drag(bare, {0, 0}, {10, 10});
  CHECK(bare.document().layers.size() == 1);
}

TEST_CASE("The panel changes width, height and corner radius of a live shape") {
  Editor editor(Empty());
  editor.SetTool(Tool::kRectangle);
  Drag(editor, {0, 0}, {100, 50});
  editor.SetTool(Tool::kSelection);

  auto rect = std::get<core::RectangleShape>(*editor.Info()->shape);
  rect.width = 200;
  for (auto& corner : rect.corners) corner.radius = 10;
  editor.SetShape(rect);
  const auto& changed = std::get<core::RectangleShape>(Selected(editor).shape);
  CHECK(changed.width == Approx(200));
  CHECK(changed.corners[2].radius == Approx(10));
  CHECK(editor.history().undo_action() == "shape");
  CheckRect(SelectedBounds(editor), {-50, 0, 150, 50});  // Grows around its centre.
  editor.Undo();
  CHECK(std::get<core::RectangleShape>(Selected(editor).shape).width == Approx(100));
}

TEST_CASE("SetBounds moves and resizes the selection; shapes stay live") {
  Editor editor(Empty());
  editor.SetTool(Tool::kEllipse);
  Drag(editor, {0, 0}, {100, 50});
  editor.SetBounds(Rect::FromXYWH(10, 10, 50, 100));
  CheckRect(SelectedBounds(editor), {10, 10, 60, 110});
  const auto& ellipse = std::get<core::EllipseShape>(Selected(editor).shape);
  CHECK(ellipse.width == Approx(50));
  CHECK(ellipse.height == Approx(100));
  CHECK(editor.history().undo_action() == "transform");
}

TEST_CASE("Rotation is absolute for a single shape, counter-clockwise positive") {
  Editor editor(Empty());
  editor.SetTool(Tool::kRectangle);
  Drag(editor, {0, 0}, {100, 50});
  CHECK(editor.Info()->rotation == Approx(0));
  editor.SetRotation(90);
  CHECK(editor.Info()->rotation == Approx(90));
  CheckRect(SelectedBounds(editor), {25, -25, 75, 75});  // Turned about its centre.
  // The rectangle's first corner (top-left) went to the bottom-left: CCW.
  const core::Point corner =
      Selected(editor).transform.Map(core::ShapePath(Selected(editor).shape).anchors[0].position);
  CHECK(corner.x == Approx(25));
  CHECK(corner.y == Approx(75));
  editor.SetRotation(90);  // Same angle again: nothing to record.
  CHECK(editor.history().undo_action() == "rotate");
  editor.Undo();
  CHECK(editor.Info()->rotation == Approx(0));
}
