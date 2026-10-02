// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/document.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "core/id.h"

using namespace leinwand::core;

namespace {

ObjectPtr MakeTriangle(const std::string& id) {
  PathObject path;
  path.common.id = id;
  path.common.appearance = {Fill{RgbColor{1, 0, 0}}};
  path.path.anchors = {{{100, 50}}, {{150, 150}}, {{50, 150}}};
  path.path.closed = true;
  return MakeObject(std::move(path));
}

Document MakeDocument() {
  GroupObject group;
  group.common.id = "g1";
  group.children = {MakeTriangle("p2"), MakeTriangle("p3")};

  Layer sublayer;
  sublayer.id = "l2";
  sublayer.children = {MakeTriangle("p4")};

  Layer layer;
  layer.id = "l1";
  layer.children = {MakeTriangle("p1"), MakeObject(std::move(group)),
                    MakeLayer(std::move(sublayer))};

  Document document;
  document.artboards = {{"a1", "Artboard 1", Rect::FromXYWH(0, 0, 595, 842)}};
  document.swatches = {{"s1", "Red", Swatch::Kind::kProcess, RgbColor{1, 0, 0}}};
  document.layers = {MakeLayer(std::move(layer)), MakeLayer({.id = "l3"})};
  return document;
}

}  // namespace

TEST_CASE("VisitObjects walks layers, sublayers and groups back to front") {
  std::vector<std::string> ids;
  VisitObjects(MakeDocument(), [&](const Object& o) { ids.push_back(CommonOf(o).id); });
  CHECK(ids == std::vector<std::string>{"p1", "g1", "p2", "p3", "p4"});
}

TEST_CASE("FindObject and FindSwatch look up by id") {
  const Document document = MakeDocument();
  REQUIRE(document.FindObject("p3") != nullptr);
  CHECK(std::holds_alternative<PathObject>(document.FindObject("p3")->base()));
  CHECK(std::holds_alternative<GroupObject>(document.FindObject("g1")->base()));
  CHECK(document.FindObject("nope") == nullptr);
  REQUIRE(document.FindSwatch("s1") != nullptr);
  CHECK(document.FindSwatch("s1")->name == "Red");
  CHECK(document.FindSwatch("s2") == nullptr);
}

TEST_CASE("Copying a document shares unchanged layers and objects") {
  const Document original = MakeDocument();
  Document edited = original;

  // Replace the first object of the first layer; everything else is shared.
  Layer layer = *original.layers[0];
  layer.children[0] = MakeTriangle("p1-edited");
  edited.layers[0] = MakeLayer(std::move(layer));

  CHECK(original.FindObject("p1") != nullptr);  // The original is untouched.
  CHECK(edited.FindObject("p1") == nullptr);
  CHECK(edited.FindObject("p1-edited") != nullptr);
  CHECK(edited.layers[1] == original.layers[1]);
  CHECK(std::get<ObjectPtr>(edited.layers[0]->children[1]) ==
        std::get<ObjectPtr>(original.layers[0]->children[1]));
}

TEST_CASE("PathData counts segments for open, closed and degenerate paths") {
  PathData path;
  CHECK(path.segment_count() == 0);
  path.anchors = {{{0, 0}}};
  CHECK(path.segment_count() == 0);  // An isolated point.
  path.anchors.push_back({{10, 0}});
  path.anchors.push_back({{10, 10}});
  CHECK(path.segment_count() == 2);
  path.closed = true;
  CHECK(path.segment_count() == 3);
}

TEST_CASE("IdGenerator issues unique ids and skips reserved ones") {
  IdGenerator ids(42);
  ids.Reserve("taken0");
  std::vector<std::string> seen;
  for (int i = 0; i < 1000; ++i) {
    const std::string id = ids.Next();
    CHECK(id.size() == 6);
    CHECK(id != "taken0");
    seen.push_back(id);
  }
  std::sort(seen.begin(), seen.end());
  CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
  CHECK(IdGenerator(7).Next() == IdGenerator(7).Next());  // Deterministic per seed.
}
