// SPDX-License-Identifier: GPL-3.0-or-later
// Clipping masks and the Transparency panel in the editor.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/transform.h"
#include "editor/editor.h"
#include "geometry/bezier.h"

using namespace leinwand;
using Catch::Approx;
using editor::Editor;

namespace {

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

const core::Object& Only(const Editor& editor) {
  REQUIRE(editor.selection().size() == 1);
  return *editor.document().FindObject(*editor.selection().begin());
}

}  // namespace

TEST_CASE("Make Clipping Mask groups the selection under the frontmost path") {
  Editor editor(
      With({Rect("a", 0, 0, 50, 50), Rect("b", 60, 0, 50, 50), Rect("c", 20, 20, 60, 10)}));
  editor.SelectAll();
  editor.MakeClippingMask();
  const auto& group = std::get<core::GroupObject>(Only(editor));
  CHECK(group.clipped);
  REQUIRE(group.children.size() == 3);
  CHECK(core::CommonOf(*group.children.back()).id == "c");
  CHECK(core::CommonOf(*group.children.back()).appearance.empty());  // Unpainted.

  editor.ReleaseClippingMask();
  CHECK(!std::get<core::GroupObject>(Only(editor)).clipped);
  editor.Undo();
  editor.Undo();
  CHECK(editor.document().FindObject("c"));
  CHECK(!core::CommonOf(*editor.document().FindObject("c")).appearance.empty());
}

TEST_CASE("A group cannot be the clipping path") {
  Editor editor(With({Rect("a", 0, 0, 50, 50), Rect("b", 60, 0, 50, 50)}));
  editor.Select({"b"});
  editor.Group();
  editor.SelectAll();
  const auto before = editor.history().revision();
  editor.MakeClippingMask();
  CHECK(editor.history().revision() == before);
}

TEST_CASE("Make Opacity Mask: the frontmost object masks the others") {
  Editor editor(
      With({Rect("a", 0, 0, 50, 50), Rect("b", 60, 0, 50, 50), Rect("m", 0, 0, 110, 50)}));
  editor.SelectAll();
  editor.MakeOpacityMask();
  // The two others are grouped; the mask leaves the layer.
  const core::Object& masked = Only(editor);
  REQUIRE(std::holds_alternative<core::GroupObject>(masked));
  const auto& mask = core::CommonOf(masked).mask;
  REQUIRE(mask);
  CHECK(core::CommonOf(*mask->art).id == "m");
  CHECK(mask->clip);
  CHECK(!mask->invert);
  CHECK(!editor.document().FindObject("m"));
  CHECK(editor.Transparency().mask.has_value());

  editor.SetMaskClip(false);
  editor.SetMaskInvert(true);
  CHECK(!editor.Transparency().mask->clip);
  CHECK(editor.Transparency().mask->invert);

  // Moving the object moves its mask.
  editor.Nudge(10, 0);
  const auto& moved = core::CommonOf(Only(editor)).mask;
  CHECK(geometry::Bounds(*moved->art).left == Approx(10));

  editor.ReleaseOpacityMask();
  REQUIRE(editor.document().FindObject("m"));
  CHECK(editor.selection().size() == 2);
  CHECK(editor.selection().contains("m"));
  for (const auto& id : editor.selection()) {
    CHECK(!core::CommonOf(*editor.document().FindObject(id)).mask);
  }
  CHECK(geometry::Bounds(*editor.document().FindObject("m")).left == Approx(10));
}

TEST_CASE("A single object keeps its identity under a mask") {
  Editor editor(With({Rect("a", 0, 0, 50, 50), Rect("m", 0, 0, 50, 50)}));
  editor.SelectAll();
  editor.MakeOpacityMask();
  CHECK(editor.selection() == core::IdSet{"a"});
  CHECK(core::CommonOf(*editor.document().FindObject("a")).mask);
}

TEST_CASE("Blend mode and isolated blending through the editor") {
  Editor editor(With({Rect("a", 0, 0, 50, 50), Rect("b", 60, 0, 50, 50)}));
  editor.SelectAll();
  editor.SetBlendMode(core::BlendMode::kMultiply);
  CHECK(editor.Transparency().blend_mode == core::BlendMode::kMultiply);
  CHECK(!editor.Transparency().blend_mixed);
  editor.Select({"a"});
  editor.SetBlendMode(core::BlendMode::kScreen);
  editor.SelectAll();
  CHECK(editor.Transparency().blend_mixed);

  CHECK(!editor.Transparency().has_group);
  editor.Group();
  editor.SetIsolated(true);
  CHECK(editor.Transparency().isolated);
  CHECK(std::get<core::GroupObject>(Only(editor)).isolated);
}

TEST_CASE("Duplicated masks get fresh ids for their art") {
  Editor editor(With({Rect("a", 0, 0, 50, 50), Rect("m", 0, 0, 50, 50)}));
  editor.SelectAll();
  editor.MakeOpacityMask();
  core::IdGenerator ids;
  core::IdSet copies;
  const core::Document doubled = core::DuplicateObjects(editor.document(), {"a"}, ids, &copies);
  REQUIRE(copies.size() == 1);
  const auto& copy_mask = core::CommonOf(*doubled.FindObject(*copies.begin())).mask;
  REQUIRE(copy_mask);
  CHECK(core::CommonOf(*copy_mask->art).id != "m");
  CHECK(core::AllObjectIds(doubled).contains("m"));
}
