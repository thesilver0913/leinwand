// SPDX-License-Identifier: GPL-3.0-or-later
// Artboards in the editor: the panel's commands and the artboard tool.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <variant>

#include "core/style.h"
#include "editor/editor.h"
#include "geometry/bezier.h"

using namespace leinwand;
using core::Rect;
using editor::AlignEdge;
using editor::Editor;
using editor::Tool;

namespace {

constexpr double kPick = 4.0;

core::ObjectPtr Square(const std::string& id, double x, double y, double size) {
  core::PathObject path;
  path.common.id = id;
  path.common.appearance = {core::Fill{}};
  path.path.anchors = {{{x, y}}, {{x + size, y}}, {{x + size, y + size}}, {{x, y + size}}};
  path.path.closed = true;
  return core::MakeObject(std::move(path));
}

// Artboard 1 at 0..100, artboard 2 at 200..300; a square on each and one
// straddling neither.
core::Document Sample() {
  core::Layer layer;
  layer.id = "l1";
  layer.children = {Square("on1", 10, 10, 20), Square("on2", 210, 10, 20),
                    Square("loose", 90, 50, 30)};
  core::Document document;
  document.artboards = {{"a1", "Artboard 1", Rect::FromXYWH(0, 0, 100, 100), {}, 0},
                        {"a2", "Artboard 2", Rect::FromXYWH(200, 0, 100, 100), {}, 0}};
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

void Drag(Editor& editor, core::Point from, core::Point to) {
  editor.PointerDown(from, {}, kPick);
  editor.PointerMove({(from.x + to.x) / 2, (from.y + to.y) / 2}, {});
  editor.PointerMove(to, {});
  editor.PointerUp(to, {});
}

Rect BoundsOf(const Editor& editor, const std::string& id) {
  return geometry::Bounds(*editor.document().FindObject(id));
}

}  // namespace

TEST_CASE("Artboards are added, renamed, reordered and removed, with undo") {
  Editor editor(Sample());
  editor.SetArtboardNamePrefix("Board");
  editor.AddArtboard(Rect::FromXYWH(400, 0, 50, 50));
  REQUIRE(editor.document().artboards.size() == 3);
  CHECK(editor.document().artboards[2].name == "Board 3");
  CHECK(editor.active_artboard() == 2);
  editor.RenameArtboard(2, "Cover");
  CHECK(editor.document().artboards[2].name == "Cover");
  editor.MoveArtboard(2, 0);
  CHECK(editor.document().artboards[0].name == "Cover");
  CHECK(editor.active_artboard() == 0);  // The active one moved with it.
  editor.RemoveActiveArtboard();
  CHECK(editor.document().artboards.size() == 2);
  editor.Undo();
  CHECK(editor.document().artboards.size() == 3);
}

TEST_CASE("The last artboard cannot be removed") {
  Editor editor(Sample());
  editor.RemoveActiveArtboard();
  editor.RemoveActiveArtboard();
  CHECK(editor.document().artboards.size() == 1);
}

TEST_CASE("The artboard tool draws a new artboard outside the others") {
  Editor editor(Sample());
  editor.SetTool(Tool::kArtboard);
  Drag(editor, {400, 0}, {500, 80});
  REQUIRE(editor.document().artboards.size() == 3);
  CHECK(editor.document().artboards[2].bounds == Rect::FromXYWH(400, 0, 100, 80));
  CHECK(editor.active_artboard() == 2);
}

TEST_CASE("Moving an artboard carries the artwork lying on it") {
  Editor editor(Sample());
  editor.SetTool(Tool::kArtboard);
  Drag(editor, {50, 80}, {50, 280});  // Artboard 1 down by 200.
  CHECK(editor.document().artboards[0].bounds == Rect::FromXYWH(0, 200, 100, 100));
  CHECK(BoundsOf(editor, "on1").top == Catch::Approx(210));
  CHECK(BoundsOf(editor, "loose").top == Catch::Approx(50));  // Not entirely on it.
  CHECK(BoundsOf(editor, "on2").top == Catch::Approx(10));
}

TEST_CASE("A handle of the active artboard resizes it") {
  Editor editor(Sample());
  editor.SetTool(Tool::kArtboard);
  editor.SetActiveArtboard(1);
  Drag(editor, {300, 100}, {350, 160});  // Bottom-right corner.
  CHECK(editor.document().artboards[1].bounds == Rect::FromXYWH(200, 0, 150, 160));
}

TEST_CASE("Delete with the artboard tool removes the active artboard") {
  Editor editor(Sample());
  editor.SetTool(Tool::kArtboard);
  editor.SetActiveArtboard(1);
  editor.Delete();
  REQUIRE(editor.document().artboards.size() == 1);
  CHECK(editor.document().artboards[0].id == "a1");
  CHECK(editor.document().FindObject("on2") != nullptr);  // Artwork stays.
}

TEST_CASE("Alignment to the artboard uses the active one") {
  Editor editor(Sample());
  editor.SetActiveArtboard(1);
  editor.PointerDown({20, 20}, {}, kPick);
  editor.PointerUp({20, 20}, {});
  editor.AlignSelection(AlignEdge::kLeft);
  CHECK(BoundsOf(editor, "on1").left == Catch::Approx(200));
}

TEST_CASE("SetCover lays the cover out again in one undo step") {
  Editor editor(core::WithCover({}, {100, 150, 40, 0.5, std::nullopt, 9}));
  editor.SetCover({100, 150, 80, 0.5, std::nullopt, 9}, {});
  const auto& boards = editor.document().artboards;
  CHECK(boards[0].bounds.width() == Catch::Approx(220));
  editor.Undo();
  CHECK(editor.document().artboards[0].bounds.width() == Catch::Approx(210));
}

TEST_CASE("Create Trim Marks: around the selection, or the active artboard with its bleed") {
  Editor editor(Sample());
  editor.CreateTrimMarks(core::TrimMarkStyle::kJapanese);
  REQUIRE(editor.selection().size() == 1);
  const auto* marks =
      std::get_if<core::GroupObject>(editor.document().FindObject(*editor.selection().begin()));
  REQUIRE(marks);
  CHECK(marks->children.size() == 24);
  for (const auto& child : marks->children) {
    const auto* stroke = core::FrontStroke(core::CommonOf(*child).appearance);
    REQUIRE(stroke);
    CHECK(stroke->width == Catch::Approx(core::kTrimMarkWidth));
  }
  // Around a selected object: within reach of it.
  editor.Select({"on1"});
  editor.CreateTrimMarks(core::TrimMarkStyle::kWestern);
  const auto* around =
      std::get_if<core::GroupObject>(editor.document().FindObject(*editor.selection().begin()));
  REQUIRE(around);
  CHECK(around->children.size() == 8);
  const core::Rect bounds =
      geometry::Bounds(*editor.document().FindObject(*editor.selection().begin()));
  CHECK(bounds.left >= 10 - core::TrimMarkReach(core::kDefaultBleed) - 1e-6);
}
