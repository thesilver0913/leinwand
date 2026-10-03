// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/layers.h"

#include <catch2/catch_test_macros.hpp>
#include <variant>

#include "core/edit.h"

using namespace leinwand::core;

namespace {

ObjectPtr Dot(const std::string& id, double x) {
  PathObject path;
  path.common.id = id;
  path.path.anchors = {{{x, 0}}, {{x + 1, 0}}};
  return MakeObject(std::move(path));
}

// "back" holds "a" and the group "g" (moved by (100,0)) with "b";
// "front" holds "c".
Document Sample() {
  GroupObject group;
  group.common.id = "g";
  group.transform = Matrix::Translate(100, 0);
  group.children = {Dot("b", 0)};
  Layer back;
  back.id = "back";
  back.children = {Dot("a", 0), MakeObject(group)};
  Layer front;
  front.id = "front";
  front.children = {Dot("c", 0)};
  Document document;
  document.layers = {MakeLayer(std::move(back)), MakeLayer(std::move(front))};
  return document;
}

std::vector<std::string> ChildIds(const Layer& layer) {
  std::vector<std::string> ids;
  for (const auto& child : layer.children) {
    if (const auto* o = std::get_if<ObjectPtr>(&child)) {
      ids.push_back(CommonOf(**o).id);
    } else {
      ids.push_back(std::get<LayerPtr>(child)->id);
    }
  }
  return ids;
}

Point FirstAnchor(const Document& document, const std::string& id) {
  const auto found = FindObjects(document, {id});
  REQUIRE(found.size() == 1);
  const auto& path = std::get<PathObject>(*found[0].object).path;
  return found[0].to_document.Map(path.anchors[0].position);
}

}  // namespace

TEST_CASE("Layers and objects can be hidden, locked and renamed") {
  const Document document = Sample();
  Document result = SetItemVisible(document, "front", false);
  CHECK_FALSE(result.layers[1]->visible);
  CHECK(result.layers[0] == document.layers[0]);  // Untouched layers are shared.
  result = SetItemLocked(result, "b", true);
  CHECK(CommonOf(*result.FindObject("b")).locked);
  result = RenameItem(result, "back", "Background");
  CHECK(result.layers[0]->name == "Background");
  CHECK(SetItemVisible(document, "nope", false).layers == document.layers);
}

TEST_CASE("Moving reorders within a layer and across layers") {
  const Document document = Sample();
  Document result = MoveItem(document, "a", "back", 1);  // In front of the group.
  CHECK(ChildIds(*result.layers[0]) == std::vector<std::string>{"g", "a"});
  result = MoveItem(document, "c", "back", 0);
  CHECK(ChildIds(*result.layers[0]) == std::vector<std::string>{"c", "a", "g"});
  CHECK(result.layers[1]->children.empty());
  result = MoveItem(document, "front", "", 0);  // Layers reorder too.
  CHECK(result.layers[0]->id == "front");
}

TEST_CASE("Objects moved into or out of a transformed group stay in place") {
  const Document document = Sample();
  Document result = MoveItem(document, "a", "g", 0);
  CHECK(FirstAnchor(result, "a") == Point{0, 0});
  result = MoveItem(document, "b", "front", 1);
  CHECK(FirstAnchor(result, "b") == Point{100, 0});
  CHECK(ParentOf(result, "b") == "front");
}

TEST_CASE("Invalid moves change nothing") {
  const Document document = Sample();
  CHECK(MoveItem(document, "a", "", 0).layers == document.layers);       // Object at top level.
  CHECK(MoveItem(document, "front", "g", 0).layers == document.layers);  // Layer into a group.
  CHECK(MoveItem(document, "back", "back", 0).layers == document.layers);
  CHECK(MoveItem(document, "g", "g", 0).layers == document.layers);
}

TEST_CASE("Art goes only into visible, unlocked layers") {
  const Document document = Sample();
  CHECK(LayerAcceptsArt(document, "back"));
  CHECK_FALSE(LayerAcceptsArt(SetItemLocked(document, "back", true), "back"));
  CHECK_FALSE(LayerAcceptsArt(document, "g"));  // A group, not a layer.
  CHECK_FALSE(LayerAcceptsArt(document, "nope"));
}

TEST_CASE("Layers are added in front of a layer and removed with their contents") {
  Layer layer;
  layer.id = "new";
  Document result = AddLayer(Sample(), layer, "back");
  REQUIRE(result.layers.size() == 3);
  CHECK(result.layers[1]->id == "new");
  result = RemoveLayer(result, "back");
  CHECK(result.layers.size() == 2);
  CHECK_FALSE(result.FindObject("a"));
}
