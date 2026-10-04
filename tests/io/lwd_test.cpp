// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/lwd.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <variant>

#include "core/style.h"
#include "render/test_document.h"

using namespace leinwand;
using io::LoadError;

namespace {

std::string RoundTrip(const core::Document& document) {
  const std::string first = io::WriteDocumentJson(document, "test");
  const auto loaded = io::ReadDocumentJson(first);
  REQUIRE(loaded.document);
  return io::WriteDocumentJson(*loaded.document, "test");
}

}  // namespace

TEST_CASE("The showcase document survives a JSON round trip unchanged") {
  const core::Document document = render::MakeShowcaseDocument();
  const std::string json = io::WriteDocumentJson(document, "test");
  CHECK(RoundTrip(document) == json);
  // Spot colors, swatch references, clip groups and hidden layers are all in
  // there (live shapes have a test of their own).
  for (const char* expected :
       {"\"spot\"", "\"swatch\"", "\"clipped\"", "\"visible\"", "\"mask\"", "\"invert\""}) {
    CHECK(json.find(expected) != std::string::npos);
  }
}

TEST_CASE("Live shapes keep their parameters") {
  core::ShapeObject rect;
  rect.common.id = "r";
  core::RectangleShape params{100, 50, {}};
  params.corners[1] = {8, core::CornerKind::kChamfer};
  rect.shape = params;
  rect.transform = core::Matrix::Rotate(0.5) * core::Matrix::Translate(10, 20);
  core::ShapeObject star;
  star.common.id = "s";
  star.shape = core::StarShape{7, 40, 15};
  core::Layer layer;
  layer.id = "l";
  layer.children = {core::MakeObject(rect), core::MakeObject(star)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  const auto loaded = io::ReadDocumentJson(io::WriteDocumentJson(document, "test"));
  REQUIRE(loaded.document);
  const auto& r = std::get<core::ShapeObject>(*loaded.document->FindObject("r"));
  CHECK(std::get<core::RectangleShape>(r.shape) == params);
  CHECK(std::abs(r.transform.a - rect.transform.a) < 1e-6);
  CHECK(std::get<core::StarShape>(
            std::get<core::ShapeObject>(*loaded.document->FindObject("s")).shape)
            .points == 7);
}

TEST_CASE("Artboards keep their bleed") {
  core::Document document = core::NewDocument("Layer 1", 100, 50, 8.5);
  const auto loaded = io::ReadDocumentJson(io::WriteDocumentJson(document, "test"));
  REQUIRE(loaded.document);
  CHECK(loaded.document->artboards.front().bleed == 8.5);
  CHECK(loaded.document->artboards.front().bounds.width() == 100);
}

TEST_CASE("document.json follows the spec's shape: fixed key order, defaults omitted") {
  core::PathObject path;
  path.common.id = "o7f3k2";
  path.path.closed = true;
  path.path.anchors = {
      {{100, 50}}, {{150, 150}}, {{50, 150}, {20, 0}, {0, -30}, core::AnchorKind::kSmooth}};
  core::Stroke stroke{core::RgbColor{0, 0, 0}};
  stroke.width = 2;
  path.common.appearance = {stroke, core::Fill{core::RgbColor{1, 0, 0}}};
  core::Layer layer;
  layer.id = "layer";
  layer.children = {core::MakeObject(path)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};

  const auto j = nlohmann::ordered_json::parse(io::WriteDocumentJson(document, "test"));
  const auto& object = j["layers"][0]["children"][0];
  std::vector<std::string> keys;
  for (const auto& [key, value] : object.items()) keys.push_back(key);
  CHECK(keys == std::vector<std::string>{"id", "type", "closed", "anchors", "appearance"});
  CHECK(object["anchors"][0].dump() == R"({"p":[100,50]})");
  CHECK(object["anchors"][2].dump() ==
        R"({"p":[50,150],"in":[20,0],"out":[0,-30],"kind":"smooth"})");
  CHECK(object["appearance"][0].dump() ==
        R"({"type":"stroke","paint":{"space":"rgb","values":[0,0,0]},"width":2})");
  CHECK(j["format"]["version"] == "1.4");
}

TEST_CASE("Unknown fields, enum values and object types are kept and written back") {
  const std::string json = R"({
  "format": {"version": "1.4", "app": "Leinwand 9"},
  "settings": {"colorMode": "rgb", "iccProfile": "sRGB", "rulerOrigin": [1, 2]},
  "artboards": [],
  "swatches": [],
  "layers": [
    {"id": "l", "type": "layer", "color": "red", "children": [
      {"id": "a", "type": "path", "anchors": [{"p": [0, 0]}], "blendMode": "plusDarker",
       "futureField": {"x": 1},
       "appearance": [{"type": "effect", "kind": "dropShadow"},
                      {"type": "fill", "paint": {"space": "rgb", "values": [1, 0, 0]}, "grain": 3}]},
      {"id": "b", "type": "hologram", "bounds": [10, 20, 30, 40], "sparkle": true}
    ]}
  ],
  "symbols": [{"id": "s1"}]
})";
  const auto loaded = io::ReadDocumentJson(json);
  REQUIRE(loaded.document);
  // The placeholder has a frame from "bounds" and is reported.
  const auto* b = loaded.document->FindObject("b");
  REQUIRE(b);
  const auto* preserved = std::get_if<core::PreservedObject>(b);
  REQUIRE(preserved);
  REQUIRE(preserved->bounds);
  CHECK(preserved->bounds->width() == 30);
  CHECK(loaded.report.rows.size() == 2);  // The hologram and the effect.

  const auto out = nlohmann::ordered_json::parse(io::WriteDocumentJson(*loaded.document, "test"));
  CHECK(out["settings"]["rulerOrigin"] == nlohmann::ordered_json::parse("[1, 2]"));
  CHECK(out["symbols"][0]["id"] == "s1");
  const auto& layer = out["layers"][0];
  CHECK(layer["color"] == "red");
  const auto& a = layer["children"][0];
  CHECK(a["blendMode"] == "plusDarker");
  CHECK(a["futureField"]["x"] == 1);
  CHECK(a["appearance"][0]["kind"] == "dropShadow");
  CHECK(a["appearance"][1]["grain"] == 3);
  CHECK(layer["children"][1]["sparkle"] == true);
}

TEST_CASE("A moved placeholder is written inside a transformed group") {
  core::PreservedObject o;
  o.common.id = "b";
  o.format = "lwd";
  o.data = R"({"id":"b","type":"hologram"})";
  o.transform = core::Matrix::Translate(5, 0);
  core::Layer layer;
  layer.id = "l";
  layer.children = {core::MakeObject(o)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  const auto out = nlohmann::ordered_json::parse(io::WriteDocumentJson(document, "test"));
  const auto& group = out["layers"][0]["children"][0];
  CHECK(group["type"] == "group");
  CHECK(group["transform"][4] == 5);
  CHECK(group["children"][0]["type"] == "hologram");
}

TEST_CASE("Versions: a newer major is refused, a newer minor opens") {
  CHECK(io::ReadDocumentJson(R"({"format":{"version":"2.0"}})").error == LoadError::kNewerVersion);
  CHECK(io::ReadDocumentJson(R"({"format":{"version":"1.9"},"layers":[]})").document);
  CHECK(io::ReadDocumentJson(R"({"layers":[]})").error == LoadError::kCorrupt);
  CHECK(io::ReadDocumentJson("not json").error == LoadError::kCorrupt);
  const auto broken = io::ReadDocumentJson(
      R"({"format":{"version":"1.0"},"layers":[{"id":"l","children":[{"type":"path"}]}]})");
  CHECK(broken.error == LoadError::kCorrupt);
  CHECK_FALSE(broken.message.empty());
}

TEST_CASE("The container starts with an uncompressed mimetype and round-trips") {
  const core::Document document = render::MakeShowcaseDocument();
  const auto bytes = io::WriteLwd(document, "test", {0x89, 'P', 'N', 'G'});
  REQUIRE(bytes.size() > 100);
  // ZIP local header: name at offset 30, stored data right after it.
  const std::string head(bytes.begin() + 30, bytes.begin() + 38);
  CHECK(head == "mimetype");
  const std::string mime(bytes.begin() + 38, bytes.begin() + 38 + io::kMimeType.size());
  CHECK(mime == io::kMimeType);
  const auto loaded = io::ReadLwd(bytes);
  REQUIRE(loaded.document);
  CHECK(io::WriteDocumentJson(*loaded.document, "test") == io::WriteDocumentJson(document, "test"));
  CHECK(io::ReadLwd({1, 2, 3}).error == LoadError::kNotLeinwand);
}

TEST_CASE("Saving replaces the file only after the new one reads back") {
  const auto dir = std::filesystem::temp_directory_path() / "leinwand-io-test";
  std::filesystem::create_directories(dir);
  const auto path = dir / "doc.lwd";
  std::filesystem::remove(path);
  std::string error;
  REQUIRE(io::SaveLwd(path, render::MakeShowcaseDocument(), "test", {}, &error));
  CHECK_FALSE(std::filesystem::exists(dir / "~doc.lwd.tmp"));
  const auto loaded = io::LoadLwd(path);
  REQUIRE(loaded.document);
  CHECK(loaded.document->layers.size() == 2);
  CHECK(io::LoadLwd(dir / "missing.lwd").error == LoadError::kNotFound);
  {
    std::ofstream junk(dir / "junk.lwd", std::ios::binary);
    junk << "hello";
  }
  CHECK(io::LoadLwd(dir / "junk.lwd").error == LoadError::kNotLeinwand);
  std::filesystem::remove_all(dir);
}

TEST_CASE("A book cover keeps its template settings") {
  core::CoverSpec spec{419.53, 595.28, 36, 0.283, std::nullopt, 8.5};
  const core::Document document = core::WithCover({}, spec);
  const auto loaded = io::ReadDocumentJson(io::WriteDocumentJson(document, "test"));
  REQUIRE(loaded.document);
  REQUIRE(loaded.document->cover);
  CHECK(*loaded.document->cover == spec);
  spec.spine = 12.0;
  const auto given = io::ReadDocumentJson(io::WriteDocumentJson(core::WithCover({}, spec), "test"));
  CHECK(given.document->cover->spine == 12.0);
}

TEST_CASE("Gradients survive a JSON round trip") {
  core::PathObject path;
  path.common.id = "g";
  path.path.anchors = {{{0, 0}}, {{10, 0}}, {{10, 10}}};
  core::Fill fill{core::RgbColor{1, 0, 0}};
  fill.gradient = core::Gradient{
      core::GradientType::kRadial,
      {{0.0, core::RgbColor{1, 0, 0}, 1.0, 0.3}, {1.0, core::CmykColor{0, 0, 0, 1}, 0.5, 0.5}},
      {5, 5},
      {10, 5},
      0.5,
      core::Point{4, 4}};
  core::Stroke stroke{core::RgbColor{0, 0, 0}};
  stroke.gradient = core::Gradient{
      core::GradientType::kLinear,
      {{0.0, core::RgbColor{0, 0, 1}, 1.0, 0.5}, {1.0, core::RgbColor{0, 1, 0}, 1.0, 0.5}},
      {0, 0},
      {10, 10}};
  path.common.appearance = {stroke, fill};
  core::Layer layer;
  layer.id = "l";
  layer.children = {core::MakeObject(path)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  const auto loaded = io::ReadDocumentJson(io::WriteDocumentJson(document, "test"));
  REQUIRE(loaded.document);
  const auto& a = core::CommonOf(*loaded.document->FindObject("g")).appearance;
  CHECK(core::FrontFill(a)->gradient == fill.gradient);
  CHECK(core::FrontStroke(a)->gradient == stroke.gradient);
}

TEST_CASE("Opacity masks and isolated groups survive a JSON round trip") {
  core::PathObject art;
  art.common.id = "art";
  art.path.anchors = {{{0, 0}}, {{10, 0}}, {{10, 10}}};
  core::GroupObject group;
  group.common.id = "g";
  group.isolated = true;
  group.common.mask = std::make_shared<const core::OpacityMask>(
      core::OpacityMask{core::MakeObject(art), false, true});
  core::Layer layer;
  layer.id = "l";
  layer.children = {core::MakeObject(group)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  const std::string json = io::WriteDocumentJson(document, "test");
  CHECK(json.find("\"clip\": false") != std::string::npos);
  const auto loaded = io::ReadDocumentJson(json);
  REQUIRE(loaded.document);
  const auto& g = std::get<core::GroupObject>(*loaded.document->FindObject("g"));
  CHECK(g.isolated);
  REQUIRE(g.common.mask);
  CHECK(!g.common.mask->clip);
  CHECK(g.common.mask->invert);
  CHECK(core::CommonOf(*g.common.mask->art).id == "art");
  // A mask without its art is a broken file, not a silent drop.
  CHECK(!io::ReadDocumentJson(
             R"({"format": {"version": "1.3"}, "layers": [{"id": "l", "type": "layer",
             "children": [{"id": "p", "type": "path", "mask": {"clip": false}}]}]})")
             .document);
}

TEST_CASE("Text survives a JSON round trip: stories, styles and kept outlines") {
  core::CharacterStyle bold;
  bold.font = {"Source Sans 3", "Bold", "SourceSans3-Bold"};
  bold.size = 20;
  bold.leading = 30;
  bold.tracking = 50;
  bold.kerning = core::KerningMode::kNone;
  core::Story story = core::MakeStory("story1", U"日本語 and\nEnglish");
  story = core::WithCharacterStyle(story, 4, 7, [&](core::CharacterStyle& s) { s = bold; });
  story = core::WithParagraphStyle(story, 8, 8, [](core::ParagraphStyle& p) {
    p.align = core::TextAlign::kRight;
    p.space_before = 6;
  });
  core::TextObject text;
  text.common.id = "t";
  text.story = std::make_shared<const core::Story>(story);
  text.transform = core::Matrix::Translate(10, 20);
  core::GroupObject outlines;
  outlines.common.id = "g";
  core::TextObject kept = text;
  kept.common.id = "kept";
  core::Story kept_story = story;
  kept_story.id = "story2";
  kept.story = std::make_shared<const core::Story>(kept_story);
  outlines.outlined_text = core::MakeObject(kept);
  core::Layer layer;
  layer.id = "l";
  layer.children = {core::MakeObject(text), core::MakeObject(outlines)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};

  const std::string json = io::WriteDocumentJson(document, "test");
  const auto j = nlohmann::json::parse(json);
  REQUIRE(j["stories"].size() == 2);  // Written once each, at the top level.
  CHECK(j["stories"][0]["text"] == "日本語 and\nEnglish");
  CHECK(j["layers"][0]["children"][0]["story"] == "story1");
  CHECK(j["layers"][0]["children"][0].contains("bounds"));  // For older versions.
  const auto loaded = io::ReadDocumentJson(json);
  REQUIRE(loaded.document);
  const auto& t = std::get<core::TextObject>(*loaded.document->FindObject("t"));
  CHECK(*t.story == story);
  CHECK(t.transform == text.transform);
  const auto& g = std::get<core::GroupObject>(*loaded.document->FindObject("g"));
  REQUIRE(g.outlined_text);
  CHECK(std::get<core::TextObject>(*g.outlined_text).story->id == "story2");
  CHECK(RoundTrip(document) == json);
  // A text whose story is missing is a broken file.
  auto broken = j;
  broken.erase("stories");
  CHECK(io::ReadDocumentJson(broken.dump()).error == LoadError::kCorrupt);
}

TEST_CASE("Two different stories under one id are written apart; bad run lengths are refused") {
  core::TextObject a;
  a.common.id = "a";
  a.story = std::make_shared<const core::Story>(core::MakeStory("same", U"one"));
  core::TextObject b;
  b.common.id = "b";
  b.story = std::make_shared<const core::Story>(core::MakeStory("same", U"two"));
  core::Layer layer;
  layer.id = "l";
  layer.children = {core::MakeObject(a), core::MakeObject(b)};
  core::Document document;
  document.layers = {core::MakeLayer(std::move(layer))};
  const auto loaded = io::ReadDocumentJson(io::WriteDocumentJson(document, "test"));
  REQUIRE(loaded.document);
  CHECK(std::get<core::TextObject>(*loaded.document->FindObject("a")).story->text == U"one");
  CHECK(std::get<core::TextObject>(*loaded.document->FindObject("b")).story->text == U"two");

  CHECK(io::ReadDocumentJson(
            R"({"format": {"version": "1.4"}, "stories": [{"id": "s", "text": "ab",
            "characters": [{"length": -1}]}], "layers": []})")
            .error == LoadError::kCorrupt);
}

// Writes the showcase as a .lwd file for trying the app with: test-output/showcase.lwd.
TEST_CASE("Showcase as .lwd", "[.][showcase-lwd]") {
  const auto path = std::filesystem::path(LEINWAND_TEST_OUTPUT_DIR) / "showcase.lwd";
  std::filesystem::create_directories(path.parent_path());
  CHECK(io::SaveLwd(path, render::MakeShowcaseDocument(), "test", {}, nullptr));
}

TEST_CASE("Saving works under a Japanese file name, without leaving the temporary file") {
  const auto dir = std::filesystem::temp_directory_path() / "leinwand-io-test";
  std::filesystem::create_directories(dir);
  const std::filesystem::path path = dir / std::filesystem::path(u8"図面 テスト.lwd");
  std::filesystem::remove(path);
  std::string error;
  REQUIRE(io::SaveLwd(path, render::MakeShowcaseDocument(), "test", {}, &error));
  CHECK(io::LoadLwd(path).document);
  CHECK_FALSE(std::filesystem::exists(dir / std::filesystem::path(u8"~図面 テスト.lwd.tmp")));
}
