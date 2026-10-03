// SPDX-License-Identifier: GPL-3.0-or-later
// One test per row of the pen tool table in spec 4.2 (M4's completion
// criterion), plus undo and the rubber band.
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
using editor::Editor;
using editor::Modifiers;
using editor::PenAction;
using editor::Tool;

namespace {

constexpr double kPick = 4.0;
const Modifiers kNone;
const Modifiers kAlt{.alt = true};
const Modifiers kShift{.shift = true};

core::Document Empty() {
  core::Layer layer;
  layer.id = "layer";
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

Editor Pen() {
  Editor editor(Empty());
  editor.SetTool(Tool::kPen);
  return editor;
}

void Click(Editor& editor, Point p) {
  editor.PointerDown(p, kNone, kPick);
  editor.PointerUp(p, kNone);
}

void Drag(Editor& editor, Point from, Point to, Modifiers modifiers = {}) {
  editor.PointerDown(from, modifiers, kPick);
  editor.PointerMove(to, modifiers);
  editor.PointerUp(to, modifiers);
}

// The only path object (or the one with `id`).
const core::PathData& PathOf(const Editor& editor, std::string id = "") {
  if (id.empty()) {
    const auto ids = core::AllObjectIds(editor.document());
    REQUIRE(ids.size() == 1);
    id = *ids.begin();
  }
  const auto found = core::FindObjects(editor.document(), {id});
  REQUIRE(found.size() == 1);
  const auto* path = std::get_if<core::PathObject>(&*found[0].object);
  REQUIRE(path);
  return path->path;
}

int ObjectCount(const Editor& editor) {
  return static_cast<int>(core::AllObjectIds(editor.document()).size());
}

void CheckPoint(Point actual, Point expected) {
  CHECK(actual.x == Approx(expected.x).margin(1e-9));
  CHECK(actual.y == Approx(expected.y).margin(1e-9));
}

}  // namespace

TEST_CASE("Row 1: a click adds a corner point (straight segment)") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Click(editor, {100, 0});
  const auto& path = PathOf(editor);
  REQUIRE(path.anchors.size() == 2);
  CHECK(path.anchors[1].kind == AnchorKind::kCorner);
  CHECK(path.anchors[0].handle_out == Point{});
  CHECK(path.anchors[1].handle_in == Point{});
  CHECK_FALSE(path.closed);
}

TEST_CASE("Row 2: a drag adds a smooth point with mirrored handles") {
  Editor editor = Pen();
  Drag(editor, {0, 0}, {0, 50});
  const auto& path = PathOf(editor);
  CHECK(path.anchors[0].kind == AnchorKind::kSmooth);
  CHECK(path.anchors[0].handle_out == Point{0, 50});
  CHECK(path.anchors[0].handle_in == Point{0, -50});
}

TEST_CASE("Row 3: Alt during a drag moves only the outgoing handle") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  editor.PointerDown({100, 0}, kNone, kPick);
  editor.PointerMove({100, 50}, kNone);  // in = (0,-50), out = (0,50)
  editor.PointerMove({150, 0}, kAlt);    // Alt: out follows, in stays.
  editor.PointerUp({150, 0}, kAlt);
  const auto& anchor = PathOf(editor).anchors[1];
  CHECK(anchor.handle_out == Point{50, 0});
  CHECK(anchor.handle_in == Point{0, -50});
  CHECK(anchor.kind == AnchorKind::kCorner);
}

TEST_CASE("Row 4: Shift during a drag snaps the handle to 45 degrees") {
  Editor editor = Pen();
  Drag(editor, {0, 0}, {50, 5}, kShift);
  CheckPoint(PathOf(editor).anchors[0].handle_out, {50, 0});
  Drag(editor, {100, 0}, {140, 38}, kShift);
  const Point out = PathOf(editor).anchors[1].handle_out;
  CHECK(out.x == Approx(out.y));  // 45 degrees.
}

TEST_CASE("Row 5: Space during a drag moves the anchor itself") {
  Editor editor = Pen();
  editor.PointerDown({0, 0}, kNone, kPick);
  editor.PointerMove({0, 20}, kNone);             // A handle (0,20).
  editor.PointerMove({30, 20}, {.space = true});  // Space: anchor moves by (30,0).
  editor.PointerUp({30, 20}, {.space = true});
  const auto& anchor = PathOf(editor).anchors[0];
  CheckPoint(anchor.position, {30, 0});
  CheckPoint(anchor.handle_out, {0, 20});
}

TEST_CASE("Row 6: clicking the last anchor removes its outgoing handle") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Drag(editor, {100, 0}, {100, 50});
  CHECK(editor.PenActionAt({100, 0}, kPick) == PenAction::kRemoveHandle);
  Click(editor, {100, 0});
  const auto& anchor = PathOf(editor).anchors[1];
  CHECK(anchor.handle_out == Point{});
  CHECK(anchor.handle_in == Point{0, -50});   // The incoming curve is kept.
  CHECK(PathOf(editor).anchors.size() == 2);  // No new anchor.
}

TEST_CASE("Row 7: dragging from the last anchor redraws its outgoing handle") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Drag(editor, {100, 0}, {100, 50});
  Drag(editor, {100, 0}, {140, 0});
  const auto& anchor = PathOf(editor).anchors[1];
  CheckPoint(anchor.handle_out, {40, 0});
  CheckPoint(anchor.handle_in, {0, -50});
  CHECK(anchor.kind == AnchorKind::kCorner);
}

TEST_CASE("Row 8: clicking or dragging on the start point closes the path") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Click(editor, {100, 0});
  Click(editor, {100, 100});
  CHECK(editor.PenActionAt({1, 1}, kPick) == PenAction::kClose);
  Click(editor, {1, 1});
  CHECK(PathOf(editor).closed);
  CHECK(PathOf(editor).anchors.size() == 3);
  CHECK_FALSE(editor.drawing_path());

  Editor dragged = Pen();
  Click(dragged, {0, 0});
  Click(dragged, {100, 0});
  Drag(dragged, {0, 0}, {0, -30});  // Closing with a smooth start point.
  const auto& start = PathOf(dragged).anchors[0];
  CHECK(PathOf(dragged).closed);
  CHECK(start.kind == AnchorKind::kSmooth);
  CheckPoint(start.handle_out, {0, -30});
}

TEST_CASE("Row 9: clicking a selected path's segment adds an anchor without changing it") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Drag(editor, {100, 0}, {150, 50});
  editor.FinishPath();
  const auto before = PathOf(editor);
  const Point on_curve = geometry::SegmentAt(before, 0).Evaluate(0.5);
  CHECK(editor.PenActionAt(on_curve, kPick) == PenAction::kAddAnchor);
  Click(editor, on_curve);
  const auto& after = PathOf(editor);
  REQUIRE(after.anchors.size() == 3);
  CheckPoint(geometry::SegmentAt(after, 0).Evaluate(1.0),
             geometry::SegmentAt(before, 0).Evaluate(0.5));
  CHECK(editor.history().undo_action() == "add anchor");
}

TEST_CASE("Row 10: clicking a selected path's anchor deletes it") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Click(editor, {50, 50});
  Click(editor, {100, 0});
  editor.FinishPath();
  CHECK(editor.PenActionAt({50, 50}, kPick) == PenAction::kDeleteAnchor);
  Click(editor, {50, 50});
  REQUIRE(PathOf(editor).anchors.size() == 2);
  CheckPoint(PathOf(editor).anchors[1].position, {100, 0});
}

TEST_CASE("Row 11: clicking an open path's end continues drawing from it") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Click(editor, {100, 0});
  editor.FinishPath();
  CHECK(editor.PenActionAt({100, 0}, kPick) == PenAction::kContinue);
  Click(editor, {100, 0});
  Click(editor, {100, 100});
  REQUIRE(PathOf(editor).anchors.size() == 3);
  CheckPoint(PathOf(editor).anchors[2].position, {100, 100});

  // From the start point: the new anchor goes before it.
  editor.FinishPath();
  Click(editor, {0, 0});
  Click(editor, {0, -50});
  const auto& path = PathOf(editor);
  REQUIRE(path.anchors.size() == 4);
  CheckPoint(path.anchors.back().position, {0, -50});
  CheckPoint(path.anchors[path.anchors.size() - 2].position, {0, 0});
}

TEST_CASE("Row 12: while drawing, clicking another open path's end joins the two") {
  Editor editor = Pen();
  Click(editor, {0, 100});
  Click(editor, {100, 100});
  editor.FinishPath();
  Click(editor, {0, 0});
  Click(editor, {50, 0});
  CHECK(editor.PenActionAt({100, 100}, kPick) == PenAction::kJoin);
  Click(editor, {100, 100});
  CHECK(ObjectCount(editor) == 1);
  const auto& path = PathOf(editor);
  REQUIRE(path.anchors.size() == 4);  // (0,0) (50,0) (100,100) (0,100)
  CheckPoint(path.anchors[2].position, {100, 100});
  CheckPoint(path.anchors[3].position, {0, 100});
  CHECK_FALSE(editor.drawing_path());
}

TEST_CASE("Row 13: holding Ctrl switches to the last selection tool") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Click(editor, {100, 0});
  editor.SetTemporaryTool(editor.last_selection_tool());
  CHECK(editor.tool() == Tool::kSelection);
  Click(editor, {50, 0});  // Selects the path; the pen stops drawing.
  CHECK(editor.selection().size() == 1);
  CHECK_FALSE(editor.drawing_path());
  editor.SetTemporaryTool(std::nullopt);
  CHECK(editor.tool() == Tool::kPen);
}

TEST_CASE("Row 14: holding Alt switches to the anchor point tool") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Drag(editor, {100, 0}, {100, 50});  // A smooth anchor.
  Click(editor, {200, 0});
  editor.SetTemporaryTool(Tool::kConvertAnchor);
  Click(editor, {100, 0});  // Smooth to corner.
  CHECK(PathOf(editor).anchors[1].kind == AnchorKind::kCorner);
  CHECK(PathOf(editor).anchors[1].handle_out == Point{});
  Drag(editor, {100, 0}, {130, 0});  // Corner to smooth by dragging.
  CHECK(PathOf(editor).anchors[1].kind == AnchorKind::kSmooth);
  CheckPoint(PathOf(editor).anchors[1].handle_in, {-30, 0});
}

TEST_CASE("Row 15: Enter, Esc or Ctrl+click on nothing ends the path open") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Click(editor, {100, 0});
  editor.FinishPath();  // Enter or Esc.
  CHECK_FALSE(PathOf(editor).closed);
  Click(editor, {0, 200});  // A new path, not a third anchor.
  CHECK(ObjectCount(editor) == 2);

  editor.SetTemporaryTool(Tool::kSelection);  // Ctrl+click on nothing.
  Click(editor, {500, 500});
  editor.SetTemporaryTool(std::nullopt);
  CHECK_FALSE(editor.drawing_path());
}

TEST_CASE("Each pen click is one undo step, and undo keeps drawing") {
  Editor editor = Pen();
  Click(editor, {0, 0});
  Click(editor, {100, 0});
  Click(editor, {100, 100});
  editor.Undo();
  CHECK(PathOf(editor).anchors.size() == 2);
  CHECK(editor.drawing_path());
  Click(editor, {200, 0});
  CHECK(PathOf(editor).anchors.size() == 3);
  editor.Undo();
  editor.Undo();
  editor.Undo();
  CHECK(ObjectCount(editor) == 0);
  CHECK_FALSE(editor.drawing_path());
}

TEST_CASE("The rubber band previews the next segment") {
  Editor editor = Pen();
  Drag(editor, {0, 0}, {0, 50});
  editor.PointerHover({100, 0});
  const auto band = editor.overlay().rubber_band;
  REQUIRE(band);
  CheckPoint(band->anchors[0].position, {0, 0});
  CheckPoint(band->anchors[0].handle_out, {0, 50});  // Curves out of the last handle.
  CheckPoint(band->anchors[1].position, {100, 0});
}
