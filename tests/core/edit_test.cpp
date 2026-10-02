// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/edit.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <variant>
#include <vector>

#include "core/transform.h"

using namespace leinwand::core;

namespace {

PathData Square(double x, double y, double size = 10) {
  PathData path;
  path.anchors = {{{x, y}}, {{x + size, y}}, {{x + size, y + size}}, {{x, y + size}}};
  path.closed = true;
  return path;
}

ObjectPtr Path(const std::string& id, double x = 0, double y = 0) {
  PathObject object;
  object.common.id = id;
  object.path = Square(x, y);
  return MakeObject(std::move(object));
}

ObjectPtr Group(const std::string& id, std::vector<ObjectPtr> children, Matrix transform = {}) {
  GroupObject group;
  group.common.id = id;
  group.children = std::move(children);
  group.transform = transform;
  return MakeObject(std::move(group));
}

// Layer "l1": a, b, g(c, d), e   (back to front)
Document Sample(Matrix group_transform = {}) {
  Layer layer;
  layer.id = "l1";
  layer.children = {Path("a"), Path("b", 20),
                    Group("g", {Path("c", 40), Path("d", 60)}, group_transform), Path("e", 80)};
  Layer other;
  other.id = "l2";
  other.children = {Path("x")};
  Document document;
  document.layers = {MakeLayer(std::move(layer)), MakeLayer(std::move(other))};
  return document;
}

// Ids in painting order, with group contents in parentheses.
std::string Order(const std::vector<ObjectPtr>& objects);
std::string Order(const ObjectPtr& object) {
  const auto* group = std::get_if<GroupObject>(&*object);
  return CommonOf(*object).id + (group ? "(" + Order(group->children) + ")" : "");
}
std::string Order(const std::vector<ObjectPtr>& objects) {
  std::string result;
  for (const auto& o : objects) result += (result.empty() ? "" : " ") + Order(o);
  return result;
}
std::string Order(const Document& document, int layer = 0) {
  std::vector<ObjectPtr> objects;
  for (const auto& child : document.layers[layer]->children) {
    objects.push_back(std::get<ObjectPtr>(child));
  }
  return Order(objects);
}

Point FirstAnchor(const Document& document, const std::string& id) {
  const auto found = FindObjects(document, {id});
  REQUIRE(found.size() == 1);
  const auto& path = std::get<PathObject>(*found[0].object).path;
  return found[0].to_document.Map(path.anchors[0].position);
}

}  // namespace

TEST_CASE("FindObjects returns objects in painting order with their parent transform") {
  const Document document = Sample(Matrix::Translate(100, 0));
  const auto found = FindObjects(document, {"e", "c", "a"});
  REQUIRE(found.size() == 3);
  CHECK(CommonOf(*found[0].object).id == "a");
  CHECK(CommonOf(*found[1].object).id == "c");
  CHECK(found[1].to_document == Matrix::Translate(100, 0));
  CHECK(CommonOf(*found[2].object).id == "e");
}

TEST_CASE("WithoutNested drops objects inside selected groups") {
  CHECK(WithoutNested(Sample(), {"g", "c", "a"}) == IdSet{"a", "g"});
  CHECK(WithoutNested(Sample(), {"c"}) == IdSet{"c"});
}

TEST_CASE("Edits share everything they do not touch") {
  const Document before = Sample();
  const Document after = RemoveObjects(before, {"b"});
  CHECK(Order(after) == "a g(c d) e");
  CHECK(after.layers[1] == before.layers[1]);  // Other layer: same pointer.
  CHECK(std::get<ObjectPtr>(after.layers[0]->children[1]) ==
        std::get<ObjectPtr>(before.layers[0]->children[2]));  // Group g untouched.
  CHECK(RemoveObjects(before, {"nope"}).layers[0] == before.layers[0]);
}

TEST_CASE("RemoveObjects and ReplaceObjects reach into groups") {
  CHECK(Order(RemoveObjects(Sample(), {"c", "e"})) == "a b g(d)");
  CHECK(Order(ReplaceObjects(Sample(), {{"d", Path("z")}})) == "a b g(c z) e");
}

TEST_CASE("TransformObjects works in document coordinates, also inside transformed groups") {
  const Document document = Sample(Matrix::Scale(2, 2));
  const Document moved = TransformObjects(document, {"a", "c"}, Matrix::Translate(5, 7));
  CHECK(FirstAnchor(moved, "a") == Point{5, 7});
  CHECK(FirstAnchor(document, "c") == Point{80, 0});  // 40 scaled by the group.
  CHECK(FirstAnchor(moved, "c") == Point{85, 7});     // Moved 5,7 on the page, not 10,14.
}

TEST_CASE("Transforming a group changes its matrix and keeps the children shared") {
  const Document document = Sample();
  const Document moved = TransformObjects(document, {"g", "c"}, Matrix::Translate(1, 0));
  const auto& before = std::get<GroupObject>(*FindObjects(document, {"g"})[0].object);
  const auto& after = std::get<GroupObject>(*FindObjects(moved, {"g"})[0].object);
  CHECK(after.transform == Matrix::Translate(1, 0));
  CHECK(after.children == before.children);  // c is moved once, through the group.
  CHECK(FirstAnchor(moved, "c") == Point{41, 0});
}

TEST_CASE("ArrangeObjects reorders within the object's own list") {
  CHECK(Order(ArrangeObjects(Sample(), {"a"}, Arrange::kBringToFront)) == "b g(c d) e a");
  CHECK(Order(ArrangeObjects(Sample(), {"e"}, Arrange::kSendToBack)) == "e a b g(c d)");
  CHECK(Order(ArrangeObjects(Sample(), {"a", "b"}, Arrange::kBringForward)) == "g(c d) a b e");
  CHECK(Order(ArrangeObjects(Sample(), {"e"}, Arrange::kSendBackward)) == "a b e g(c d)");
  CHECK(Order(ArrangeObjects(Sample(), {"c"}, Arrange::kBringForward)) == "a b g(d c) e");
  // Already in front: nothing changes, and nothing is copied.
  const Document document = Sample();
  CHECK(ArrangeObjects(document, {"e"}, Arrange::kBringToFront).layers[0] == document.layers[0]);
}

TEST_CASE("GroupObjects groups at the frontmost member and keeps positions") {
  const Document document = Sample(Matrix::Translate(100, 0));
  const Document grouped = GroupObjects(document, {"a", "c", "b"}, "new");
  CHECK(Order(grouped) == "g(new(a b c) d) e");
  CHECK(FirstAnchor(grouped, "a") == Point{0, 0});
  CHECK(FirstAnchor(grouped, "c") == Point{140, 0});  // Still where group g put it.
}

TEST_CASE("UngroupObjects releases children with the group transform applied") {
  IdSet released;
  const Document ungrouped = UngroupObjects(Sample(Matrix::Translate(100, 0)), {"g"}, &released);
  CHECK(Order(ungrouped) == "a b c d e");
  CHECK(released == IdSet{"c", "d"});
  CHECK(FirstAnchor(ungrouped, "c") == Point{140, 0});
}

TEST_CASE("DuplicateObjects puts fresh copies in front of the originals") {
  IdGenerator ids(1);
  for (const auto& id : AllObjectIds(Sample())) ids.Reserve(id);
  IdSet copies;
  const Document duplicated = DuplicateObjects(Sample(), {"a", "g"}, ids, &copies);
  REQUIRE(copies.size() == 2);
  const auto all = AllObjectIds(duplicated);
  CHECK(all.size() == 7 + 2 + 2);  // a and g, plus g's two children.
  // The copy of g sits right after g and holds copies of c and d.
  const auto& layer = duplicated.layers[0]->children;
  const auto& copy_of_g = std::get<GroupObject>(*std::get<ObjectPtr>(layer[4]));
  CHECK(copies.contains(copy_of_g.common.id));
  CHECK(copy_of_g.children.size() == 2);
  CHECK(CommonOf(*copy_of_g.children[0]).id != "c");
}

TEST_CASE("Transformed bakes paths and prepends to group matrices") {
  PathData path;
  path.anchors = {{{10, 0}, {-5, 0}, {5, 0}}};
  const PathData moved = Transformed(path, Matrix::Translate(1, 2) * Matrix::Scale(2, 2));
  CHECK(moved.anchors[0].position == Point{21, 2});
  CHECK(moved.anchors[0].handle_in == Point{-10, 0});  // Handles scale but do not move.
  CHECK(moved.anchors[0].handle_out == Point{10, 0});

  const ObjectPtr group = Group("g", {Path("c")}, Matrix::Scale(2, 2));
  const ObjectPtr shifted = Transformed(group, Matrix::Translate(3, 0));
  CHECK(std::get<GroupObject>(*shifted).transform == Matrix::Translate(3, 0) * Matrix::Scale(2, 2));
  CHECK(Transformed(group, Matrix{}) == group);  // Identity: same object.
}
