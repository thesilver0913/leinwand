// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor/editor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <variant>

using namespace leinwand;
using Catch::Approx;
using core::IdSet;
using core::Point;
using core::Rect;
using editor::Editor;
using editor::Handle;
using editor::Modifiers;

namespace {

constexpr double kPick = 4.0;
const Modifiers kNone;
const Modifiers kShift{.shift = true};
const Modifiers kAlt{.alt = true};

core::ObjectPtr Square(const std::string& id, double x, double y, double size = 100) {
  core::PathObject object;
  object.common.id = id;
  object.common.appearance = {core::Fill{core::RgbColor{1, 0, 0}}};
  object.path.anchors = {{{x, y}}, {{x + size, y}}, {{x + size, y + size}}, {{x, y + size}}};
  object.path.closed = true;
  return core::MakeObject(std::move(object));
}

// Squares a (0,0), b (200,0), c (400,0), 100 pt each, back to front.
core::Document Sample() {
  core::Layer layer;
  layer.id = "layer";
  layer.children = {Square("a", 0, 0), Square("b", 200, 0), Square("c", 400, 0)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

// The page position of a square's corners.
Rect BoundsOf(const Editor& editor, const std::string& id) {
  const auto found = core::FindObjects(editor.document(), {id});
  REQUIRE(found.size() == 1);
  const auto& path = std::get<core::PathObject>(*found[0].object).path;
  Rect r;
  for (const auto& a : path.anchors) r = r.Union(found[0].to_document.Map(a.position));
  return r;
}

void Drag(Editor& editor, Point from, Point to, Modifiers modifiers = {}) {
  editor.PointerDown(from, modifiers, kPick);
  editor.PointerMove({(from.x + to.x) / 2, (from.y + to.y) / 2}, modifiers);
  editor.PointerMove(to, modifiers);
  editor.PointerUp(to, modifiers);
}

void Click(Editor& editor, Point at, Modifiers modifiers = {}) {
  editor.PointerDown(at, modifiers, kPick);
  editor.PointerUp(at, modifiers);
}

int ObjectCount(const Editor& editor) {
  return static_cast<int>(core::AllObjectIds(editor.document()).size());
}

}  // namespace

TEST_CASE("Clicking selects; shift-click toggles; clicking empty space deselects") {
  Editor editor(Sample());
  Click(editor, {50, 50});
  CHECK(editor.selection() == IdSet{"a"});
  Click(editor, {250, 50}, kShift);
  CHECK(editor.selection() == IdSet{"a", "b"});
  Click(editor, {50, 50}, kShift);
  CHECK(editor.selection() == IdSet{"b"});
  Click(editor, {50, 300});
  CHECK(editor.selection().empty());
  CHECK_FALSE(editor.history().CanUndo());  // Selection alone is not an undo step.
}

TEST_CASE("A marquee selects what it touches, and shift adds to the selection") {
  Editor editor(Sample());
  Drag(editor, {-10, -10}, {250, 20});
  CHECK(editor.selection() == IdSet{"a", "b"});
  Drag(editor, {450, 150}, {480, 90}, kShift);
  CHECK(editor.selection() == IdSet{"a", "b", "c"});
}

TEST_CASE("Dragging moves the selection as one undo step") {
  Editor editor(Sample());
  Click(editor, {50, 50});
  Click(editor, {250, 50}, kShift);
  Drag(editor, {250, 50}, {260, 80});
  CHECK(BoundsOf(editor, "a") == Rect{10, 30, 110, 130});
  CHECK(BoundsOf(editor, "b") == Rect{210, 30, 310, 130});
  CHECK(BoundsOf(editor, "c") == Rect{400, 0, 500, 100});  // Not selected.
  CHECK(editor.history().undo_action() == "move");
  editor.Undo();
  CHECK(BoundsOf(editor, "a") == Rect{0, 0, 100, 100});
}

TEST_CASE("Shift constrains a move to 45 degrees") {
  Editor editor(Sample());
  Drag(editor, {50, 50}, {150, 60}, kShift);  // Nearly horizontal.
  CHECK(BoundsOf(editor, "a").top == Approx(0));
  CHECK(BoundsOf(editor, "a").left == Approx(100));
}

TEST_CASE("A press that does not move does not edit") {
  Editor editor(Sample());
  Drag(editor, {50, 50}, {51, 50});  // Below the drag threshold.
  CHECK_FALSE(editor.history().CanUndo());
}

TEST_CASE("Alt-drag moves a copy and leaves the original") {
  Editor editor(Sample());
  Drag(editor, {50, 50}, {50, 250}, kAlt);
  CHECK(ObjectCount(editor) == 4);
  CHECK(BoundsOf(editor, "a") == Rect{0, 0, 100, 100});
  REQUIRE(editor.selection().size() == 1);
  CHECK(BoundsOf(editor, *editor.selection().begin()) == Rect{0, 200, 100, 300});
  CHECK(editor.history().undo_action() == "duplicate");
  editor.Undo();
  CHECK(ObjectCount(editor) == 3);
}

TEST_CASE("Corner handles scale from the opposite corner, or the centre with alt") {
  Editor editor(Sample());
  Click(editor, {50, 50});
  Drag(editor, {100, 100}, {200, 150});  // Bottom-right handle.
  CHECK(BoundsOf(editor, "a") == Rect{0, 0, 200, 150});
  editor.Undo();
  Drag(editor, {100, 100}, {150, 150}, kAlt);
  CHECK(BoundsOf(editor, "a") == Rect{-50, -50, 150, 150});
}

TEST_CASE("Shift keeps proportions; side handles scale one axis") {
  Editor editor(Sample());
  Click(editor, {50, 50});
  Drag(editor, {100, 100}, {300, 150}, kShift);
  CHECK(BoundsOf(editor, "a") == Rect{0, 0, 300, 300});
  editor.Undo();
  Drag(editor, {100, 50}, {150, 90});  // Right side handle: y ignored.
  CHECK(BoundsOf(editor, "a") == Rect{0, 0, 150, 100});
}

TEST_CASE("Dragging just outside a corner rotates around the centre") {
  Editor editor(Sample());
  Click(editor, {50, 50});
  CHECK(editor.HoverAt({110, 110}, kPick).kind == editor::Hover::Kind::kRotate);
  // From the bottom-right diagonal to the bottom-left diagonal: a quarter turn.
  Drag(editor, {110, 110}, {-10, 110}, kShift);
  const Rect r = BoundsOf(editor, "a");
  CHECK(r.left == Approx(0).margin(1e-9));
  CHECK(r.right == Approx(100));  // A square looks the same after 90 degrees...
  const auto& path =
      std::get<core::PathObject>(*core::FindObjects(editor.document(), {"a"})[0].object).path;
  CHECK(path.anchors[0].position.x == Approx(100));  // ...but its first corner moved.
  CHECK(path.anchors[0].position.y == Approx(0).margin(1e-9));
}

TEST_CASE("Group, ungroup, arrange, delete and nudge are all undoable") {
  Editor editor(Sample());
  editor.SelectAll();
  CHECK(editor.selection() == IdSet{"a", "b", "c"});

  editor.Group();
  REQUIRE(editor.selection().size() == 1);
  const std::string group = *editor.selection().begin();
  CHECK(std::holds_alternative<core::GroupObject>(
      *core::FindObjects(editor.document(), {group})[0].object));
  Click(editor, {250, 50});
  CHECK(editor.selection() == IdSet{group});  // Clicking a member selects the group.

  editor.Nudge(10, 0);
  CHECK(BoundsOf(editor, "b").left == Approx(210));

  editor.Ungroup();
  CHECK(editor.selection() == IdSet{"a", "b", "c"});
  CHECK(BoundsOf(editor, "b").left == Approx(210));  // Position kept.

  // Clicking an object that is already selected keeps the whole selection
  // (so it can be dragged); deselect first to pick just one.
  editor.Deselect();
  Click(editor, {60, 50});
  CHECK(editor.selection() == IdSet{"a"});
  editor.Arrange(core::Arrange::kBringToFront);
  editor.Delete();
  CHECK(ObjectCount(editor) == 2);

  // Every step undoes, back to the start.
  for (const char* action : {"delete", "arrange", "ungroup", "move", "group"}) {
    CHECK(editor.history().undo_action() == action);
    editor.Undo();
  }
  CHECK_FALSE(editor.history().CanUndo());
  CHECK(ObjectCount(editor) == 3);
  CHECK(BoundsOf(editor, "b") == Rect{200, 0, 300, 100});
}

TEST_CASE("Commands with nothing to act on record nothing") {
  Editor editor(Sample());
  editor.Delete();
  editor.Group();
  editor.Ungroup();  // Nothing selected.
  Click(editor, {50, 50});
  editor.Ungroup();                            // Not a group.
  editor.Arrange(core::Arrange::kSendToBack);  // Already at the back.
  CHECK_FALSE(editor.history().CanUndo());
}

TEST_CASE("The overlay shows the bounding box, and the marquee while dragging one") {
  Editor editor(Sample());
  Click(editor, {50, 50});
  const auto overlay = editor.overlay();
  REQUIRE(overlay.bounding_box);
  CHECK(*overlay.bounding_box == Rect{0, 0, 100, 100});
  CHECK(editor::HandlePosition(*overlay.bounding_box, Handle::kRight) == Point{100, 50});

  editor.PointerDown({600, 300}, kNone, kPick);
  editor.PointerMove({650, 350}, kNone);
  CHECK(editor.overlay().marquee == Rect{600, 300, 650, 350});
  editor.PointerUp({650, 350}, kNone);
  CHECK_FALSE(editor.overlay().marquee);
}
