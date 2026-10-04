// SPDX-License-Identifier: GPL-3.0-or-later
// The Gradient panel and the gradient tool in the editor.
#include "core/gradient.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/style.h"
#include "editor/editor.h"

using namespace leinwand;
using Catch::Approx;
using editor::Editor;
using editor::Tool;

namespace {

constexpr double kPick = 4.0;

core::ObjectPtr Rect(const std::string& id, double x, double y, double w, double h) {
  core::PathObject path;
  path.common.id = id;
  path.common.appearance = {core::Fill{core::RgbColor{1, 0, 0}}};
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

const core::Gradient& GradientOf(const Editor& editor, const std::string& id) {
  const core::Fill* fill =
      core::FrontFill(core::CommonOf(*editor.document().FindObject(id)).appearance);
  REQUIRE(fill);
  REQUIRE(fill->gradient);
  return *fill->gradient;
}

}  // namespace

TEST_CASE("Applying a gradient lays it across each object, then retypes it") {
  Editor editor(With({Rect("a", 0, 0, 100, 40), Rect("b", 200, 0, 50, 50)}));
  editor.SelectAll();
  editor.ApplyGradient(core::GradientType::kLinear);
  const core::Gradient& a = GradientOf(editor, "a");
  CHECK(a.start == core::Point{0, 20});
  CHECK(a.end == core::Point{100, 20});
  CHECK(GradientOf(editor, "b").start == core::Point{200, 25});
  REQUIRE(editor.Style().gradient);

  editor.ApplyGradient(core::GradientType::kRadial);
  const core::Gradient& radial = GradientOf(editor, "a");
  CHECK(radial.type == core::GradientType::kRadial);
  CHECK(radial.start == core::Point{50, 20});  // The middle of the old axis.
  editor.Undo();
  CHECK(GradientOf(editor, "a").type == core::GradientType::kLinear);
}

TEST_CASE("Angle, stops and their colors through the editor") {
  Editor editor(With({Rect("a", 0, 0, 100, 100)}));
  editor.SelectAll();
  editor.ApplyGradient(core::GradientType::kLinear);
  editor.SetGradientAngle(90);
  CHECK(core::GradientAngle(GradientOf(editor, "a")) == Approx(90));

  const int added = editor.AddGradientStop(0.5);
  CHECK(added == 1);
  CHECK(editor.gradient_stop() == 1);
  // With a stop selected, a fill color goes to the stop.
  editor.SetFill(core::Color{core::RgbColor{0, 1, 0}});
  const core::Gradient& g = GradientOf(editor, "a");
  REQUIRE(g.stops.size() == 3);
  CHECK(std::get<core::RgbColor>(g.stops[1].color).g == 1.0);

  CHECK(editor.MoveGradientStop(1, 0.9) == 1);
  CHECK(GradientOf(editor, "a").stops[1].offset == 0.9);
  editor.SetGradientStopOpacity(1, 0.25);
  editor.SetGradientStopMidpoint(0, 0.2);
  CHECK(GradientOf(editor, "a").stops[1].opacity == 0.25);
  CHECK(GradientOf(editor, "a").stops[0].midpoint == 0.2);
  editor.RemoveGradientStop(1);
  CHECK(GradientOf(editor, "a").stops.size() == 2);
  CHECK(editor.gradient_stop() == -1);
}

TEST_CASE("A selected stop is let go when the selection changes") {
  Editor editor(With({Rect("a", 0, 0, 100, 100), Rect("b", 200, 0, 100, 100)}));
  editor.SelectAll();
  editor.ApplyGradient(core::GradientType::kLinear);
  editor.SelectGradientStop(0);
  CHECK(editor.gradient_stop() == 0);
  editor.Deselect();
  CHECK(editor.gradient_stop() == -1);
}

TEST_CASE("A solid color replaces the gradient when no stop is selected") {
  Editor editor(With({Rect("a", 0, 0, 100, 100)}));
  editor.SelectAll();
  editor.ApplyGradient(core::GradientType::kLinear);
  editor.SetFill(core::Color{core::RgbColor{0, 0, 1}});
  const core::Fill* fill =
      core::FrontFill(core::CommonOf(*editor.document().FindObject("a")).appearance);
  CHECK(!fill->gradient);
}

TEST_CASE("The gradient tool drags out the axis and moves its ends") {
  Editor editor(With({Rect("a", 0, 0, 100, 100)}));
  editor.SelectAll();
  editor.SetTool(Tool::kGradient);
  editor.PointerDown({10, 10}, {}, kPick);
  editor.PointerMove({60, 80}, {});
  editor.PointerUp({60, 80}, {});
  CHECK(GradientOf(editor, "a").start == core::Point{10, 10});
  CHECK(GradientOf(editor, "a").end == core::Point{60, 80});
  REQUIRE(editor.overlay().gradient_line);
  CHECK(editor.overlay().gradient_line->second == core::Point{60, 80});

  // Shift constrains to 45 degrees.
  editor.PointerDown({0, 50}, {}, kPick);
  editor.PointerMove({100, 55}, {.shift = true});
  editor.PointerUp({100, 55}, {.shift = true});
  CHECK(GradientOf(editor, "a").end.y == Approx(50));

  // Dragging the end square moves only the end.
  const core::Point end = GradientOf(editor, "a").end;
  editor.PointerDown(end, {}, kPick);
  editor.PointerMove({80, 90}, {});
  editor.PointerUp({80, 90}, {});
  CHECK(GradientOf(editor, "a").start == core::Point{0, 50});
  CHECK(GradientOf(editor, "a").end == core::Point{80, 90});
}

TEST_CASE("The gradient tool follows a group's transform") {
  core::GroupObject group;
  group.common.id = "g";
  group.children = {Rect("a", 0, 0, 100, 100)};
  group.transform = core::Matrix::Translate(100, 0);
  Editor editor(With({core::MakeObject(std::move(group))}));
  editor.SelectAll();
  editor.SetTool(Tool::kGradient);
  editor.PointerDown({110, 10}, {}, kPick);
  editor.PointerMove({190, 10}, {});
  editor.PointerUp({190, 10}, {});
  CHECK(GradientOf(editor, "a").start == core::Point{10, 10});  // In the group's coordinates.
  CHECK(GradientOf(editor, "a").end == core::Point{90, 10});
  REQUIRE(editor.overlay().gradient_line);
  CHECK(editor.overlay().gradient_line->first == core::Point{110, 10});
}
