// SPDX-License-Identifier: GPL-3.0-or-later
// Direct selection, the anchor point tools and the anchor commands (spec 4.2).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <variant>

#include "editor/editor.h"
#include "geometry/bezier.h"

using namespace leinwand;
using Catch::Approx;
using core::AnchorKind;
using core::Point;
using editor::AnchorRef;
using editor::Editor;
using editor::Modifiers;
using editor::Tool;

namespace {

constexpr double kPick = 4.0;
const Modifiers kNone;

// "p": an open polyline (0,0) (100,0) (100,100); "c": a smooth curve
// (0,200) -> (100,200) bulging down.
core::Document Sample() {
  core::PathObject p;
  p.common.id = "p";
  p.common.appearance = {core::Stroke{core::RgbColor{0, 0, 0}}};
  p.path.anchors = {{{0, 0}}, {{100, 0}}, {{100, 100}}};
  core::PathObject c;
  c.common.id = "c";
  c.common.appearance = {core::Stroke{core::RgbColor{0, 0, 0}}};
  c.path.anchors = {{{0, 200}, {}, {0, 60}, AnchorKind::kSmooth},
                    {{100, 200}, {0, 60}, {0, -60}, AnchorKind::kSmooth},
                    {{200, 200}}};
  core::Layer layer;
  layer.id = "layer";
  layer.children = {core::MakeObject(p), core::MakeObject(c)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

Editor Direct(core::Document document = Sample()) {
  Editor editor(std::move(document));
  editor.SetTool(Tool::kDirectSelection);
  return editor;
}

void Click(Editor& editor, Point p, Modifiers m = {}) {
  editor.PointerDown(p, m, kPick);
  editor.PointerUp(p, m);
}

void Drag(Editor& editor, Point from, Point to, Modifiers m = {}) {
  editor.PointerDown(from, m, kPick);
  editor.PointerMove(to, m);
  editor.PointerUp(to, m);
}

const core::PathData& PathOf(const Editor& editor, const std::string& id) {
  const auto found = core::FindObjects(editor.document(), {id});
  REQUIRE(found.size() == 1);
  const auto* path = std::get_if<core::PathObject>(&*found[0].object);
  REQUIRE(path);
  return path->path;
}

void CheckPoint(Point actual, Point expected, double margin = 1e-9) {
  CHECK(actual.x == Approx(expected.x).margin(margin));
  CHECK(actual.y == Approx(expected.y).margin(margin));
}

}  // namespace

TEST_CASE("Clicking an anchor selects just it; dragging moves just it") {
  Editor editor = Direct();
  Click(editor, {100, 0});
  CHECK(editor.anchor_selection() == std::set<AnchorRef>{{"p", 0, 1}});
  CHECK(editor.selection() == core::IdSet{"p"});
  Drag(editor, {100, 0}, {120, -10});
  const auto& path = PathOf(editor, "p");
  CheckPoint(path.anchors[1].position, {120, -10});
  CheckPoint(path.anchors[0].position, {0, 0});
  CHECK(editor.history().undo_action() == "move anchors");
}

TEST_CASE("Shift-click adds anchors; arrow keys nudge the selected anchors") {
  Editor editor = Direct();
  Click(editor, {0, 0});
  Click(editor, {100, 100}, {.shift = true});
  CHECK(editor.anchor_selection().size() == 2);
  editor.Nudge(1, 0);
  CheckPoint(PathOf(editor, "p").anchors[0].position, {1, 0});
  CheckPoint(PathOf(editor, "p").anchors[1].position, {100, 0});  // Not selected.
  CheckPoint(PathOf(editor, "p").anchors[2].position, {101, 100});
}

TEST_CASE("A marquee selects the anchors inside it") {
  Editor editor = Direct();
  Drag(editor, {90, -10}, {110, 110});
  CHECK(editor.anchor_selection() == std::set<AnchorRef>{{"p", 0, 1}, {"p", 0, 2}});
}

TEST_CASE("Dragging a handle keeps a smooth anchor smooth; Alt splits it") {
  Editor editor = Direct();
  Click(editor, {100, 200});
  Drag(editor, {100, 140}, {160, 200});  // The out handle (0,-60) swings to (60,0).
  const auto& a = PathOf(editor, "c").anchors[1];
  CheckPoint(a.handle_out, {60, 0});
  CheckPoint(a.handle_in, {-60, 0}, 1e-6);  // Still opposite.
  Drag(editor, {160, 200}, {100, 140}, {.alt = true});
  const auto& b = PathOf(editor, "c").anchors[1];
  CHECK(b.kind == AnchorKind::kCorner);
  CheckPoint(b.handle_in, {-60, 0}, 1e-6);  // Left where it was.
}

TEST_CASE("Dragging a curved segment reshapes it; a straight one moves its anchors") {
  Editor editor = Direct();
  const Point mid = geometry::SegmentAt(PathOf(editor, "c"), 0).Evaluate(0.5);
  Drag(editor, mid, mid + Point{0, 20});
  CheckPoint(geometry::SegmentAt(PathOf(editor, "c"), 0).Evaluate(0.5), mid + Point{0, 20}, 0.5);
  CheckPoint(PathOf(editor, "c").anchors[0].position, {0, 200});
  CHECK(editor.history().undo_action() == "reshape");

  Drag(editor, {50, 0}, {50, 10});  // The straight top segment of "p".
  CheckPoint(PathOf(editor, "p").anchors[0].position, {0, 10});
  CheckPoint(PathOf(editor, "p").anchors[1].position, {100, 10});
}

TEST_CASE("Editing a live shape's anchors turns it into a path with the same id") {
  core::ShapeObject rect;
  rect.common.id = "r";
  rect.common.appearance = {core::Fill{core::RgbColor{1, 0, 0}}};
  rect.shape = core::RectangleShape{100, 100};
  rect.transform = core::Matrix::Translate(50, 50);
  core::Layer layer;
  layer.id = "layer";
  layer.children = {core::MakeObject(rect)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};

  Editor editor = Direct(document);
  Drag(editor, {0, 0}, {-10, -10});  // The top-left corner.
  CheckPoint(PathOf(editor, "r").anchors[0].position, {-10, -10});
  editor.Undo();
  CHECK(std::holds_alternative<core::ShapeObject>(
      *core::FindObjects(editor.document(), {"r"})[0].object));
}

TEST_CASE("The add, delete and convert anchor tools") {
  Editor editor = Direct();
  Click(editor, {50, 0});  // Select path "p" (its segment).
  editor.SetTool(Tool::kAddAnchor);
  Click(editor, {50, 0});
  CHECK(PathOf(editor, "p").anchors.size() == 4);
  editor.SetTool(Tool::kDeleteAnchor);
  Click(editor, {100, 0});
  REQUIRE(PathOf(editor, "p").anchors.size() == 3);
  CheckPoint(PathOf(editor, "p").anchors[1].position, {50, 0});
  editor.SetTool(Tool::kConvertAnchor);
  Drag(editor, {50, 0}, {80, 0});
  CHECK(PathOf(editor, "p").anchors[1].kind == AnchorKind::kSmooth);
  Click(editor, {50, 0});
  CHECK(PathOf(editor, "p").anchors[1].kind == AnchorKind::kCorner);
}

TEST_CASE("Anchor commands: convert, remove, cut, join") {
  Editor editor = Direct();
  Click(editor, {100, 0});
  editor.ConvertSelectedAnchors(true);
  CHECK(PathOf(editor, "p").anchors[1].kind == AnchorKind::kSmooth);
  editor.ConvertSelectedAnchors(false);
  CHECK(PathOf(editor, "p").anchors[1].kind == AnchorKind::kCorner);

  Click(editor, {100, 0});
  editor.CutAtSelectedAnchor();  // An open path splits in two.
  CHECK(core::AllObjectIds(editor.document()).size() == 3);
  REQUIRE(editor.selection().size() == 2);

  // Join the two halves back at their shared point.
  Click(editor, {100, 0});  // Both halves have an end here; pick both.
  Drag(editor, {95, -5}, {105, 5});
  CHECK(editor.anchor_selection().size() == 2);
  editor.JoinSelectedEnds();
  CHECK(core::AllObjectIds(editor.document()).size() == 2);
  CHECK(PathOf(editor, "p").anchors.size() == 3);

  Click(editor, {0, 0});
  editor.RemoveSelectedAnchors();
  CHECK(PathOf(editor, "p").anchors.size() == 2);
}

TEST_CASE("Delete removes selected anchors with their segments, splitting the path") {
  Editor editor = Direct();
  Click(editor, {100, 0});
  editor.Delete();
  // "p" loses its corner: (0,0) and (100,100) are lone points, so it is gone.
  CHECK(core::AllObjectIds(editor.document()) == core::IdSet{"c"});

  core::PathObject line;
  line.common.id = "l";
  line.path.anchors = {{{0, 0}}, {{10, 0}}, {{20, 0}}, {{30, 0}}, {{40, 0}}};
  core::Layer layer;
  layer.id = "layer";
  layer.children = {core::MakeObject(line)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  Editor split = Direct(document);
  Click(split, {20, 0});
  split.Delete();
  CHECK(core::AllObjectIds(split.document()).size() == 2);
  CHECK(PathOf(split, "l").anchors.size() == 2);
  CHECK(split.selection().size() == 2);
  split.Undo();
  CHECK(PathOf(split, "l").anchors.size() == 5);
}

TEST_CASE("The overlay shows anchors and the handles near selected ones") {
  Editor editor = Direct();
  Click(editor, {100, 200});
  const auto overlay = editor.overlay();
  CHECK_FALSE(overlay.bounding_box);  // Direct selection has no box.
  REQUIRE(overlay.paths.size() == 1);
  CHECK(overlay.paths[0].selected == std::set<int>{1});
  CHECK(overlay.paths[0].with_handles == std::set<int>{0, 1, 2});
  CHECK(overlay.selection.empty());  // Not outlined twice.
}
