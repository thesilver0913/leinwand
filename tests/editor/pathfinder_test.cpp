// SPDX-License-Identifier: GPL-3.0-or-later
// The Pathfinder panel's commands in the editor (spec 4.3), on Skia's
// PathOps engine.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <variant>

#include "core/style.h"
#include "editor/editor.h"
#include "geometry/path_ops.h"
#include "render/skia_path_ops.h"

using namespace leinwand;
using core::RgbColor;
using editor::Editor;
using geometry::Pathfinder;
using Outcome = editor::Editor::PathfinderOutcome;

namespace {

const RgbColor kRed{1, 0, 0};
const RgbColor kBlue{0, 0, 1};
const RgbColor kGreen{0, 1, 0};

core::ObjectPtr Square(const std::string& id, double x, double y, double size,
                       core::Appearance appearance) {
  core::PathObject path;
  path.common.id = id;
  path.common.appearance = std::move(appearance);
  path.path.anchors = {{{x, y}}, {{x + size, y}}, {{x + size, y + size}}, {{x, y + size}}};
  path.path.closed = true;
  return core::MakeObject(std::move(path));
}

// Back to front: the given objects in one layer.
core::Document With(std::vector<core::ObjectPtr> objects) {
  core::Layer layer;
  layer.id = "l1";
  for (core::ObjectPtr& object : objects) layer.children.push_back(std::move(object));
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

// A red square "a" (0..100) behind a blue-stroked green square "b" (50..150).
core::Document Overlapping() {
  return With({Square("a", 0, 0, 100, {core::Fill{kRed}}),
               Square("b", 50, 0, 100, {core::Stroke{kBlue}, core::Fill{kGreen}})});
}

const render::SkiaPathOps& Engine() {
  static const render::SkiaPathOps engine;
  return engine;
}

Editor Run(core::Document document, Pathfinder operation, Outcome expected = Outcome::kDone) {
  Editor editor(std::move(document));
  editor.SetPathOpsEngine(&Engine());
  editor.SelectAll();
  CHECK(editor.ApplyPathfinder(operation) == expected);
  return editor;
}

std::vector<core::ObjectPtr> TopLevel(const Editor& editor) {
  std::vector<core::ObjectPtr> objects;
  for (const core::LayerChild& child : editor.document().layers[0]->children) {
    objects.push_back(std::get<core::ObjectPtr>(child));
  }
  return objects;
}

geometry::Region RegionOf(const core::Object& object) {
  if (const auto* path = std::get_if<core::PathObject>(&object)) return {{path->path}};
  const auto& compound = std::get<core::CompoundPathObject>(object);
  return {compound.subpaths, compound.fill_rule};
}

const core::Fill* FillOf(const core::Object& object) {
  return core::FrontFill(core::CommonOf(object).appearance);
}

}  // namespace

TEST_CASE("Unite joins the objects and keeps the frontmost appearance") {
  const Editor editor = Run(Overlapping(), Pathfinder::kUnite);
  REQUIRE(TopLevel(editor).size() == 1);
  const core::Object& result = *TopLevel(editor)[0];
  CHECK(geometry::Area(RegionOf(result)) == Catch::Approx(15000));
  CHECK(FillOf(result)->paint == core::Color{kGreen});
  CHECK(core::FrontStroke(core::CommonOf(result).appearance) != nullptr);
  CHECK(editor.selection() == core::IdSet{core::CommonOf(result).id});
}

TEST_CASE("Minus Front keeps the backmost appearance") {
  const Editor editor = Run(Overlapping(), Pathfinder::kMinusFront);
  REQUIRE(TopLevel(editor).size() == 1);
  CHECK(geometry::Area(RegionOf(*TopLevel(editor)[0])) == Catch::Approx(5000));
  CHECK(FillOf(*TopLevel(editor)[0])->paint == core::Color{kRed});
}

TEST_CASE("Minus Back cuts the frontmost") {
  const Editor editor = Run(Overlapping(), Pathfinder::kMinusBack);
  REQUIRE(TopLevel(editor).size() == 1);
  CHECK(geometry::Area(RegionOf(*TopLevel(editor)[0])) == Catch::Approx(5000));
  CHECK(FillOf(*TopLevel(editor)[0])->paint == core::Color{kGreen});
}

TEST_CASE("Exclude of side-by-side overlap gives a compound path") {
  const Editor editor = Run(Overlapping(), Pathfinder::kExclude);
  REQUIRE(TopLevel(editor).size() == 1);
  CHECK(geometry::Area(RegionOf(*TopLevel(editor)[0])) == Catch::Approx(10000));
}

TEST_CASE("Intersect of objects that do not overlap leaves nothing") {
  const Editor editor = Run(With({Square("a", 0, 0, 10, {core::Fill{kRed}}),
                                  Square("b", 50, 0, 10, {core::Fill{kGreen}})}),
                            Pathfinder::kIntersect);
  CHECK(TopLevel(editor).empty());
  CHECK(editor.selection().empty());
}

TEST_CASE("Divide makes a group of every enclosed region") {
  const Editor editor = Run(Overlapping(), Pathfinder::kDivide);
  REQUIRE(TopLevel(editor).size() == 1);
  const auto& group = std::get<core::GroupObject>(*TopLevel(editor)[0]);
  REQUIRE(group.children.size() == 3);
  double total = 0;
  int green = 0;
  for (const auto& child : group.children) {
    total += geometry::Area(RegionOf(*child));
    green += FillOf(*child)->paint == core::Color{kGreen};
  }
  CHECK(total == Catch::Approx(15000));
  CHECK(green == 2);  // The overlap takes the front's appearance.
}

TEST_CASE("Trim keeps what is visible and drops strokes") {
  const Editor editor = Run(Overlapping(), Pathfinder::kTrim);
  const auto& group = std::get<core::GroupObject>(*TopLevel(editor)[0]);
  REQUIRE(group.children.size() == 2);
  CHECK(geometry::Area(RegionOf(*group.children[0])) == Catch::Approx(5000));
  CHECK(geometry::Area(RegionOf(*group.children[1])) == Catch::Approx(10000));
  for (const auto& child : group.children) {
    CHECK(core::FrontStroke(core::CommonOf(*child).appearance) == nullptr);
  }
}

TEST_CASE("Merge joins visible parts of the same fill") {
  // Two red squares and a green one in front of their overlap.
  const Editor editor = Run(
      With({Square("a", 0, 0, 100, {core::Fill{kRed}}), Square("b", 50, 0, 100, {core::Fill{kRed}}),
            Square("c", 200, 0, 50, {core::Fill{kGreen}})}),
      Pathfinder::kMerge);
  const auto& group = std::get<core::GroupObject>(*TopLevel(editor)[0]);
  REQUIRE(group.children.size() == 2);
  CHECK(geometry::Area(RegionOf(*group.children[0])) == Catch::Approx(15000));
}

TEST_CASE("Crop keeps what lies inside the frontmost and removes it") {
  const Editor editor = Run(Overlapping(), Pathfinder::kCrop);
  const auto& group = std::get<core::GroupObject>(*TopLevel(editor)[0]);
  REQUIRE(group.children.size() == 1);
  CHECK(geometry::Area(RegionOf(*group.children[0])) == Catch::Approx(5000));
  CHECK(FillOf(*group.children[0])->paint == core::Color{kRed});
}

TEST_CASE("Outline gives open edges stroked in their region's fill") {
  const Editor editor = Run(Overlapping(), Pathfinder::kOutline);
  const auto& group = std::get<core::GroupObject>(*TopLevel(editor)[0]);
  REQUIRE(!group.children.empty());
  int open = 0;
  for (const auto& child : group.children) {
    const auto& path = std::get<core::PathObject>(*child);
    open += !path.path.closed;
    CHECK(core::FrontFill(path.common.appearance) == nullptr);
    CHECK(core::FrontStroke(path.common.appearance) != nullptr);
  }
  CHECK(open == static_cast<int>(group.children.size()));
}

TEST_CASE("Pathfinder works on live shapes and groups, and undoes in one step") {
  core::ShapeObject circle;
  circle.common.id = "c";
  circle.common.appearance = {core::Fill{kBlue}};
  core::EllipseShape ellipse;
  ellipse.width = 100;
  ellipse.height = 100;
  circle.shape = ellipse;
  circle.transform = core::Matrix::Translate(100, 50);
  core::GroupObject group;
  group.common.id = "g";
  group.children = {Square("a", 0, 0, 100, {core::Fill{kRed}})};
  Editor editor(With({core::MakeObject(std::move(group)), core::MakeObject(std::move(circle))}));
  editor.SetPathOpsEngine(&Engine());
  editor.SelectAll();
  const core::Document before = editor.document();
  REQUIRE(editor.ApplyPathfinder(Pathfinder::kUnite) == Outcome::kDone);
  REQUIRE(TopLevel(editor).size() == 1);
  editor.Undo();
  CHECK(editor.document().layers == before.layers);
}

TEST_CASE("Pathfinder needs two objects with an area") {
  Run(With({Square("a", 0, 0, 10, {core::Fill{kRed}})}), Pathfinder::kUnite, Outcome::kNothingToDo);
}

namespace {
// An engine that always fails.
struct FailingEngine : geometry::PathOpsEngine {
  std::optional<geometry::Region> Apply(const geometry::Region&, const geometry::Region&,
                                        geometry::BooleanOp) const override {
    return std::nullopt;
  }
  std::optional<geometry::Region> Simplify(const geometry::Region&) const override {
    return std::nullopt;
  }
};
}  // namespace

TEST_CASE("A failing engine leaves the document unchanged") {
  const FailingEngine failing;
  Editor editor(Overlapping());
  editor.SetPathOpsEngine(&failing);
  editor.SelectAll();
  const core::Document before = editor.document();
  CHECK(editor.ApplyPathfinder(Pathfinder::kUnite) == Outcome::kFailed);
  CHECK(editor.document().layers == before.layers);
}

TEST_CASE("Make Compound Path joins paths with the backmost appearance") {
  Editor editor(Overlapping());
  editor.SelectAll();
  editor.MakeCompoundPath();
  REQUIRE(TopLevel(editor).size() == 1);
  const auto& compound = std::get<core::CompoundPathObject>(*TopLevel(editor)[0]);
  CHECK(compound.subpaths.size() == 2);
  CHECK(core::FrontFill(compound.common.appearance)->paint == core::Color{kRed});
  CHECK(core::FrontStroke(compound.common.appearance) == nullptr);

  // Release gives the paths back, each with the compound path's look.
  editor.ReleaseCompoundPath();
  REQUIRE(TopLevel(editor).size() == 2);
  for (const auto& object : TopLevel(editor)) {
    CHECK(std::holds_alternative<core::PathObject>(*object));
    CHECK(FillOf(*object)->paint == core::Color{kRed});
  }
  CHECK(editor.selection().size() == 2);
}

TEST_CASE("A group holding text is not consumed by the pathfinder") {
  core::GroupObject group;
  group.common.id = "g";
  core::TextObject text;
  text.common.id = "t";
  text.story = std::make_shared<const core::Story>(core::MakeStory("s", U"A"));
  group.children = {core::MakeObject(text)};
  core::Layer layer;
  layer.id = "l";
  core::PathObject a;
  a.common.id = "a";
  a.common.appearance = {core::Fill{core::RgbColor{1, 0, 0}}};
  a.path.anchors = {{{0, 0}}, {{10, 0}}, {{10, 10}}, {{0, 10}}};
  a.path.closed = true;
  core::PathObject b = a;
  b.common.id = "b";
  for (auto& anchor : b.path.anchors) anchor.position.x += 5;
  layer.children = {core::MakeObject(a), core::MakeObject(b), core::MakeObject(group)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  editor::Editor editor(document);
  render::SkiaPathOps engine;
  editor.SetPathOpsEngine(&engine);
  editor.SelectAll();
  editor.ApplyPathfinder(geometry::Pathfinder::kUnite);
  CHECK(editor.document().FindObject("t"));
}
