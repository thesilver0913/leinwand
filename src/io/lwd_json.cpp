// SPDX-License-Identifier: GPL-3.0-or-later
// document.json (spec 3.2): the model to JSON and back.
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <variant>

#include "io/lwd.h"

namespace leinwand::io {

namespace {

using Json = nlohmann::ordered_json;
using namespace leinwand::core;

struct Corrupt : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// --- Values ----------------------------------------------------------------

// Rounded to 6 decimals; whole numbers are written without a fraction.
Json Num(double v) {
  const double rounded = std::round(v * 1e6) / 1e6;
  if (rounded == std::trunc(rounded) && std::abs(rounded) < 1e15) {
    return static_cast<std::int64_t>(rounded);
  }
  return rounded;
}

Json Pair(double x, double y) { return Json::array({Num(x), Num(y)}); }

double NumberOf(const Json& j) {
  if (!j.is_number()) throw Corrupt("a number was expected");
  return j.get<double>();
}

Point PointOf(const Json& j) {
  if (!j.is_array() || j.size() != 2) throw Corrupt("a point [x, y] was expected");
  return {NumberOf(j[0]), NumberOf(j[1])};
}

Json MatrixJson(const Matrix& m) {
  return Json::array({Num(m.a), Num(m.b), Num(m.c), Num(m.d), Num(m.e), Num(m.f)});
}

Matrix MatrixOf(const Json& j) {
  if (!j.is_array() || j.size() != 6) throw Corrupt("a matrix [a, b, c, d, e, f] was expected");
  return {NumberOf(j[0]), NumberOf(j[1]), NumberOf(j[2]),
          NumberOf(j[3]), NumberOf(j[4]), NumberOf(j[5])};
}

Json RectJson(const Rect& r) {
  return Json::array({Num(r.left), Num(r.top), Num(r.width()), Num(r.height())});
}

Rect RectOf(const Json& j) {
  if (!j.is_array() || j.size() != 4) throw Corrupt("a rectangle [x, y, w, h] was expected");
  return Rect::FromXYWH(NumberOf(j[0]), NumberOf(j[1]), NumberOf(j[2]), NumberOf(j[3]));
}

std::string StringOf(const Json& j, const char* key) {
  const auto it = j.find(key);
  if (it == j.end() || !it->is_string()) throw Corrupt(std::string("missing \"") + key + "\"");
  return it->get<std::string>();
}

// --- Enumerations ----------------------------------------------------------

template <typename E, size_t N>
struct Names {
  std::array<std::pair<E, const char*>, N> entries;
  const char* Of(E value) const {
    for (const auto& [e, name] : entries) {
      if (e == value) return name;
    }
    return entries[0].second;
  }
  std::optional<E> Find(const std::string& name) const {
    for (const auto& [e, n] : entries) {
      if (name == n) return e;
    }
    return std::nullopt;
  }
};

constexpr Names<BlendMode, 16> kBlendModes{{{
    {BlendMode::kNormal, "normal"},
    {BlendMode::kDarken, "darken"},
    {BlendMode::kMultiply, "multiply"},
    {BlendMode::kColorBurn, "colorBurn"},
    {BlendMode::kLighten, "lighten"},
    {BlendMode::kScreen, "screen"},
    {BlendMode::kColorDodge, "colorDodge"},
    {BlendMode::kOverlay, "overlay"},
    {BlendMode::kSoftLight, "softLight"},
    {BlendMode::kHardLight, "hardLight"},
    {BlendMode::kDifference, "difference"},
    {BlendMode::kExclusion, "exclusion"},
    {BlendMode::kHue, "hue"},
    {BlendMode::kSaturation, "saturation"},
    {BlendMode::kColor, "color"},
    {BlendMode::kLuminosity, "luminosity"},
}}};
constexpr Names<StrokeCap, 3> kCaps{
    {{{StrokeCap::kButt, "butt"}, {StrokeCap::kRound, "round"}, {StrokeCap::kSquare, "square"}}}};
constexpr Names<StrokeJoin, 3> kJoins{{{{StrokeJoin::kMiter, "miter"},
                                        {StrokeJoin::kRound, "round"},
                                        {StrokeJoin::kBevel, "bevel"}}}};
constexpr Names<StrokeAlign, 3> kAligns{{{{StrokeAlign::kCenter, "center"},
                                          {StrokeAlign::kInside, "inside"},
                                          {StrokeAlign::kOutside, "outside"}}}};
constexpr Names<AnchorKind, 3> kAnchorKinds{{{{AnchorKind::kCorner, "corner"},
                                              {AnchorKind::kSmooth, "smooth"},
                                              {AnchorKind::kSymmetric, "symmetric"}}}};
constexpr Names<CornerKind, 3> kCornerKinds{{{{CornerKind::kRound, "round"},
                                              {CornerKind::kInvertedRound, "invertedRound"},
                                              {CornerKind::kChamfer, "chamfer"}}}};
constexpr Names<FillRule, 2> kFillRules{
    {{{FillRule::kNonZero, "nonZero"}, {FillRule::kEvenOdd, "evenOdd"}}}};

// Reading one JSON object: known keys are taken out as they are read; what
// is left at the end is kept as unknown fields. An enumeration value this
// version does not know stays in the unknown fields too (the model keeps its
// default), so it is written back as it was.
class Reader {
 public:
  explicit Reader(const Json& j) : rest_(j) {
    if (!j.is_object()) throw Corrupt("an object was expected");
  }
  const Json* Take(const char* key) {
    const auto it = rest_.find(key);
    if (it == rest_.end()) return nullptr;
    taken_.push_back(*it);
    rest_.erase(it);
    return &taken_.back();
  }
  double Number(const char* key, double fallback) {
    const Json* j = Take(key);
    return j ? NumberOf(*j) : fallback;
  }
  bool Bool(const char* key, bool fallback) {
    const Json* j = Take(key);
    if (!j) return fallback;
    if (!j->is_boolean()) throw Corrupt(std::string("\"") + key + "\" must be true or false");
    return j->get<bool>();
  }
  std::string String(const char* key, const std::string& fallback = {}) {
    const Json* j = Take(key);
    if (!j) return fallback;
    if (!j->is_string()) throw Corrupt(std::string("\"") + key + "\" must be a string");
    return j->get<std::string>();
  }
  template <typename E, size_t N>
  E Enum(const char* key, const Names<E, N>& names, E fallback) {
    const auto it = rest_.find(key);
    if (it == rest_.end()) return fallback;
    if (it->is_string()) {
      if (const auto value = names.Find(it->get<std::string>())) {
        rest_.erase(it);
        return *value;
      }
    }
    return fallback;  // Unknown value: left in the unknown fields.
  }
  std::string Unknown() const { return rest_.empty() ? std::string() : rest_.dump(); }

 private:
  Json rest_;
  std::deque<Json> taken_;  // Keeps taken values alive for the returned pointers.
};

// Adds unknown fields kept from reading, after the known ones.
void AppendUnknown(Json& j, const std::string& unknown) {
  if (unknown.empty()) return;
  const Json extra = Json::parse(unknown, nullptr, false);
  if (!extra.is_object()) return;
  for (const auto& [key, value] : extra.items()) {
    if (!j.contains(key)) j[key] = value;
  }
}

// --- Colors ----------------------------------------------------------------

Json ColorJson(const Color& color) {
  return std::visit(
      [](const auto& c) -> Json {
        using T = std::decay_t<decltype(c)>;
        Json j;
        if constexpr (std::is_same_v<T, RgbColor>) {
          j["space"] = "rgb";
          j["values"] = Json::array({Num(c.r), Num(c.g), Num(c.b)});
        } else if constexpr (std::is_same_v<T, CmykColor>) {
          j["space"] = "cmyk";
          j["values"] = Json::array({Num(c.c), Num(c.m), Num(c.y), Num(c.k)});
        } else if constexpr (std::is_same_v<T, GrayColor>) {
          j["space"] = "gray";
          j["values"] = Json::array({Num(c.gray)});
        } else if constexpr (std::is_same_v<T, SpotColor>) {
          j["space"] = "spot";
          j["swatch"] = c.swatch_id;
          if (c.tint != 1.0) j["tint"] = Num(c.tint);
        } else {
          j["swatch"] = c.swatch_id;
        }
        return j;
      },
      color);
}

Color ColorOf(const Json& j) {
  if (!j.is_object()) throw Corrupt("a color was expected");
  const std::string space = j.value("space", "");
  auto values = [&](size_t n) {
    const auto it = j.find("values");
    if (it == j.end() || !it->is_array() || it->size() != n) throw Corrupt("bad color values");
    std::vector<double> v;
    for (const auto& x : *it) v.push_back(NumberOf(x));
    return v;
  };
  if (space == "rgb") {
    const auto v = values(3);
    return RgbColor{v[0], v[1], v[2]};
  }
  if (space == "cmyk") {
    const auto v = values(4);
    return CmykColor{v[0], v[1], v[2], v[3]};
  }
  if (space == "gray") return GrayColor{values(1)[0]};
  if (space == "spot") return SpotColor{StringOf(j, "swatch"), j.value("tint", 1.0)};
  if (space.empty() && j.contains("swatch")) return SwatchRef{StringOf(j, "swatch")};
  throw Corrupt("unknown color space \"" + space + "\"");
}

ProcessColor ProcessColorOf(const Json& j) {
  const Color color = ColorOf(j);
  if (const auto* rgb = std::get_if<RgbColor>(&color)) return *rgb;
  if (const auto* cmyk = std::get_if<CmykColor>(&color)) return *cmyk;
  if (const auto* gray = std::get_if<GrayColor>(&color)) return *gray;
  throw Corrupt("a swatch must hold a process color");
}

// --- Appearance --------------------------------------------------------------

Json AppearanceJson(const Appearance& appearance) {
  Json items = Json::array();
  for (const auto& item : appearance) {
    if (const auto* fill = std::get_if<Fill>(&item)) {
      Json j;
      j["type"] = "fill";
      j["paint"] = ColorJson(fill->paint);
      if (fill->opacity != 1.0) j["opacity"] = Num(fill->opacity);
      if (fill->blend_mode != BlendMode::kNormal) j["blendMode"] = kBlendModes.Of(fill->blend_mode);
      AppendUnknown(j, fill->unknown_fields);
      items.push_back(std::move(j));
    } else if (const auto* stroke = std::get_if<Stroke>(&item)) {
      Json j;
      j["type"] = "stroke";
      j["paint"] = ColorJson(stroke->paint);
      j["width"] = Num(stroke->width);
      if (stroke->cap != StrokeCap::kButt) j["cap"] = kCaps.Of(stroke->cap);
      if (stroke->join != StrokeJoin::kMiter) j["join"] = kJoins.Of(stroke->join);
      if (stroke->miter_limit != 10.0) j["miterLimit"] = Num(stroke->miter_limit);
      if (stroke->align != StrokeAlign::kCenter) j["align"] = kAligns.Of(stroke->align);
      if (!stroke->dashes.empty()) {
        Json dashes = Json::array();
        for (double d : stroke->dashes) dashes.push_back(Num(d));
        j["dashes"] = std::move(dashes);
      }
      if (stroke->dash_offset != 0.0) j["dashOffset"] = Num(stroke->dash_offset);
      if (stroke->opacity != 1.0) j["opacity"] = Num(stroke->opacity);
      if (stroke->blend_mode != BlendMode::kNormal) {
        j["blendMode"] = kBlendModes.Of(stroke->blend_mode);
      }
      AppendUnknown(j, stroke->unknown_fields);
      items.push_back(std::move(j));
    } else {
      items.push_back(Json::parse(std::get<UnknownAppearanceItem>(item).json));
    }
  }
  return items;
}

Appearance AppearanceOf(const Json& j, ImportReport& report, const std::string& id) {
  if (!j.is_array()) throw Corrupt("\"appearance\" must be a list");
  Appearance appearance;
  for (const auto& item : j) {
    const std::string type = item.is_object() ? item.value("type", "") : "";
    if (type == "fill") {
      Reader r(item);
      r.Take("type");
      Fill fill;
      const Json* paint = r.Take("paint");
      if (!paint) throw Corrupt("a fill needs \"paint\"");
      fill.paint = ColorOf(*paint);
      fill.opacity = r.Number("opacity", 1.0);
      fill.blend_mode = r.Enum("blendMode", kBlendModes, BlendMode::kNormal);
      fill.unknown_fields = r.Unknown();
      appearance.push_back(std::move(fill));
    } else if (type == "stroke") {
      Reader r(item);
      r.Take("type");
      Stroke stroke;
      const Json* paint = r.Take("paint");
      if (!paint) throw Corrupt("a stroke needs \"paint\"");
      stroke.paint = ColorOf(*paint);
      stroke.width = r.Number("width", 1.0);
      stroke.cap = r.Enum("cap", kCaps, StrokeCap::kButt);
      stroke.join = r.Enum("join", kJoins, StrokeJoin::kMiter);
      stroke.miter_limit = r.Number("miterLimit", 10.0);
      stroke.align = r.Enum("align", kAligns, StrokeAlign::kCenter);
      if (const Json* dashes = r.Take("dashes")) {
        if (!dashes->is_array()) throw Corrupt("\"dashes\" must be a list");
        for (const auto& d : *dashes) stroke.dashes.push_back(NumberOf(d));
      }
      stroke.dash_offset = r.Number("dashOffset", 0.0);
      stroke.opacity = r.Number("opacity", 1.0);
      stroke.blend_mode = r.Enum("blendMode", kBlendModes, BlendMode::kNormal);
      stroke.unknown_fields = r.Unknown();
      appearance.push_back(std::move(stroke));
    } else {
      // Effects (phase 3) and anything newer: kept in place.
      appearance.push_back(UnknownAppearanceItem{item.dump()});
      report.Add("appearance item \"" + type + "\"", ReportAction::kPreserved, id);
    }
  }
  return appearance;
}

// --- Paths -------------------------------------------------------------------

Json AnchorsJson(const PathData& path) {
  Json anchors = Json::array();
  for (const auto& a : path.anchors) {
    Json j;
    j["p"] = Pair(a.position.x, a.position.y);
    if (!(a.handle_in == Point{})) j["in"] = Pair(a.handle_in.x, a.handle_in.y);
    if (!(a.handle_out == Point{})) j["out"] = Pair(a.handle_out.x, a.handle_out.y);
    if (a.kind != AnchorKind::kCorner) j["kind"] = kAnchorKinds.Of(a.kind);
    anchors.push_back(std::move(j));
  }
  return anchors;
}

std::vector<Anchor> AnchorsOf(const Json& j) {
  if (!j.is_array()) throw Corrupt("\"anchors\" must be a list");
  std::vector<Anchor> anchors;
  for (const auto& a : j) {
    if (!a.is_object() || !a.contains("p")) throw Corrupt("an anchor needs \"p\"");
    Anchor anchor;
    anchor.position = PointOf(a["p"]);
    if (a.contains("in")) anchor.handle_in = PointOf(a["in"]);
    if (a.contains("out")) anchor.handle_out = PointOf(a["out"]);
    if (a.contains("kind") && a["kind"].is_string()) {
      anchor.kind = kAnchorKinds.Find(a["kind"].get<std::string>()).value_or(AnchorKind::kCorner);
    }
    anchors.push_back(anchor);
  }
  return anchors;
}

// --- Objects -----------------------------------------------------------------

void WriteCommon(Json& j, const ObjectCommon& common, const char* type) {
  j["id"] = common.id;
  j["type"] = type;
  if (!common.name.empty()) j["name"] = common.name;
  if (!common.visible) j["visible"] = false;
  if (common.locked) j["locked"] = true;
  if (common.opacity != 1.0) j["opacity"] = Num(common.opacity);
  if (common.blend_mode != BlendMode::kNormal) j["blendMode"] = kBlendModes.Of(common.blend_mode);
}

Json ShapeJson(const ShapeParams& shape) {
  return std::visit(
      [](const auto& s) -> Json {
        using T = std::decay_t<decltype(s)>;
        Json j;
        if constexpr (std::is_same_v<T, RectangleShape>) {
          j["kind"] = "rectangle";
          j["width"] = Num(s.width);
          j["height"] = Num(s.height);
          if (std::any_of(s.corners.begin(), s.corners.end(),
                          [](const Corner& c) { return !(c == Corner{}); })) {
            Json corners = Json::array();
            for (const auto& c : s.corners) {
              Json corner;
              corner["radius"] = Num(c.radius);
              if (c.kind != CornerKind::kRound) corner["kind"] = kCornerKinds.Of(c.kind);
              corners.push_back(std::move(corner));
            }
            j["corners"] = std::move(corners);
          }
        } else if constexpr (std::is_same_v<T, EllipseShape>) {
          j["kind"] = "ellipse";
          j["width"] = Num(s.width);
          j["height"] = Num(s.height);
          if (s.pie_start != 0.0) j["pieStart"] = Num(s.pie_start);
          if (s.pie_end != 360.0) j["pieEnd"] = Num(s.pie_end);
        } else if constexpr (std::is_same_v<T, PolygonShape>) {
          j["kind"] = "polygon";
          j["sides"] = s.sides;
          j["radius"] = Num(s.radius);
          if (s.corner_radius != 0.0) j["cornerRadius"] = Num(s.corner_radius);
        } else if constexpr (std::is_same_v<T, StarShape>) {
          j["kind"] = "star";
          j["points"] = s.points;
          j["outerRadius"] = Num(s.outer_radius);
          j["innerRadius"] = Num(s.inner_radius);
        } else {
          j["kind"] = "line";
          j["length"] = Num(s.length);
        }
        return j;
      },
      shape);
}

std::optional<ShapeParams> ShapeOf(const Json& j) {
  if (!j.is_object()) throw Corrupt("\"shape\" must be an object");
  const std::string kind = j.value("kind", "");
  auto num = [&](const char* key, double fallback) {
    return j.contains(key) ? NumberOf(j[key]) : fallback;
  };
  if (kind == "rectangle") {
    RectangleShape s{num("width", 0), num("height", 0), {}};
    if (j.contains("corners")) {
      const Json& corners = j["corners"];
      if (!corners.is_array() || corners.size() != 4) throw Corrupt("a rectangle has 4 corners");
      for (size_t i = 0; i < 4; ++i) {
        s.corners[i].radius = corners[i].contains("radius") ? NumberOf(corners[i]["radius"]) : 0;
        if (corners[i].contains("kind") && corners[i]["kind"].is_string()) {
          s.corners[i].kind =
              kCornerKinds.Find(corners[i]["kind"].get<std::string>()).value_or(CornerKind::kRound);
        }
      }
    }
    return s;
  }
  if (kind == "ellipse") {
    return EllipseShape{num("width", 0), num("height", 0), num("pieStart", 0), num("pieEnd", 360)};
  }
  if (kind == "polygon") {
    return PolygonShape{static_cast<int>(num("sides", 6)), num("radius", 0),
                        num("cornerRadius", 0)};
  }
  if (kind == "star") {
    return StarShape{static_cast<int>(num("points", 5)), num("outerRadius", 0),
                     num("innerRadius", 0)};
  }
  if (kind == "line") return LineShape{num("length", 0)};
  return std::nullopt;  // A newer kind: the object is kept as it is.
}

Json ObjectJson(const ObjectPtr& object);

Json PreservedJson(const PreservedObject& o) {
  if (o.format != "lwd") {
    // Foreign content (an SVG element): a type of its own.
    Json j;
    WriteCommon(j, o.common, "preserved");
    j["format"] = o.format;
    j["data"] = o.data;
    if (o.bounds) j["bounds"] = RectJson(*o.bounds);
    if (!o.transform.IsIdentity()) j["transform"] = MatrixJson(o.transform);
    AppendUnknown(j, o.common.unknown_fields);
    return j;
  }
  Json j = Json::parse(o.data, nullptr, false);
  if (!j.is_object()) j = Json::object();
  // The common fields may have been edited (name, visibility, lock).
  j["id"] = o.common.id;
  auto set = [&](const char* key, bool is_default, Json value) {
    if (is_default) {
      j.erase(key);
    } else {
      j[key] = std::move(value);
    }
  };
  set("name", o.common.name.empty(), o.common.name);
  set("visible", o.common.visible, false);
  set("locked", !o.common.locked, true);
  set("opacity", o.common.opacity == 1.0, Num(o.common.opacity));
  if (o.transform.IsIdentity()) return j;
  // Moved: the content goes into a group that carries the move, which any
  // version understands.
  Json group;
  group["id"] = o.common.id + "-moved";
  group["type"] = "group";
  group["transform"] = MatrixJson(o.transform);
  group["children"] = Json::array({std::move(j)});
  return group;
}

Json ObjectJson(const ObjectPtr& object) {
  return std::visit(
      [&](const auto& o) -> Json {
        using T = std::decay_t<decltype(o)>;
        Json j;
        if constexpr (std::is_same_v<T, PreservedObject>) {
          return PreservedJson(o);
        } else if constexpr (std::is_same_v<T, PathObject>) {
          WriteCommon(j, o.common, "path");
          if (o.path.closed) j["closed"] = true;
          j["anchors"] = AnchorsJson(o.path);
        } else if constexpr (std::is_same_v<T, CompoundPathObject>) {
          WriteCommon(j, o.common, "compoundPath");
          if (o.fill_rule != FillRule::kNonZero) j["fillRule"] = kFillRules.Of(o.fill_rule);
          Json subpaths = Json::array();
          for (const auto& p : o.subpaths) {
            Json sub;
            if (p.closed) sub["closed"] = true;
            sub["anchors"] = AnchorsJson(p);
            subpaths.push_back(std::move(sub));
          }
          j["subpaths"] = std::move(subpaths);
        } else if constexpr (std::is_same_v<T, GroupObject>) {
          WriteCommon(j, o.common, "group");
          if (o.clipped) j["clipped"] = true;
          if (!o.transform.IsIdentity()) j["transform"] = MatrixJson(o.transform);
          Json children = Json::array();
          for (const auto& child : o.children) children.push_back(ObjectJson(child));
          j["children"] = std::move(children);
        } else {
          WriteCommon(j, o.common, "shape");
          j["shape"] = ShapeJson(o.shape);
          if (!o.transform.IsIdentity()) j["transform"] = MatrixJson(o.transform);
        }
        if (!o.common.appearance.empty()) j["appearance"] = AppearanceJson(o.common.appearance);
        AppendUnknown(j, o.common.unknown_fields);
        return j;
      },
      object->base());
}

ObjectCommon CommonOf(Reader& r, ImportReport& report) {
  ObjectCommon common;
  common.id = r.String("id");
  if (common.id.empty()) throw Corrupt("an object without \"id\"");
  common.name = r.String("name");
  common.visible = r.Bool("visible", true);
  common.locked = r.Bool("locked", false);
  common.opacity = r.Number("opacity", 1.0);
  common.blend_mode = r.Enum("blendMode", kBlendModes, BlendMode::kNormal);
  if (const Json* appearance = r.Take("appearance")) {
    common.appearance = AppearanceOf(*appearance, report, common.id);
  }
  return common;
}

ObjectPtr Preserved(const Json& j, ImportReport& report, const std::string& what) {
  Reader r(j);
  PreservedObject o;
  o.common.id = r.String("id");
  if (o.common.id.empty()) throw Corrupt("an object without \"id\"");
  o.common.name = r.String("name");
  o.common.visible = r.Bool("visible", true);
  o.common.locked = r.Bool("locked", false);
  o.common.opacity = r.Number("opacity", 1.0);
  o.format = "lwd";
  o.data = j.dump();
  // Types added after 1.0 carry "bounds" so that older versions can show a
  // frame for them (see docs/implementation-notes.md).
  if (j.contains("bounds")) o.bounds = RectOf(j["bounds"]);
  report.Add(what, ReportAction::kPreserved, o.common.id);
  return MakeObject(std::move(o));
}

ObjectPtr ObjectOf(const Json& j, ImportReport& report) {
  if (!j.is_object()) throw Corrupt("an object was expected");
  const std::string type = j.value("type", "");
  if (type == "preserved") {
    Reader r(j);
    r.Take("type");
    PreservedObject o;
    o.common = CommonOf(r, report);
    o.format = r.String("format");
    o.data = r.String("data");
    if (const Json* bounds = r.Take("bounds")) o.bounds = RectOf(*bounds);
    if (const Json* m = r.Take("transform")) o.transform = MatrixOf(*m);
    o.common.unknown_fields = r.Unknown();
    return MakeObject(std::move(o));  // Reported when it was first imported.
  }
  if (type != "path" && type != "compoundPath" && type != "group" && type != "shape") {
    return Preserved(j, report, "object type \"" + type + "\"");
  }
  if (type == "shape" && j.contains("shape") && !ShapeOf(j["shape"])) {
    return Preserved(j, report, "shape kind \"" + j["shape"].value("kind", "") + "\"");
  }
  Reader r(j);
  r.Take("type");
  ObjectCommon common = CommonOf(r, report);
  ObjectPtr result;
  if (type == "path") {
    PathObject o;
    o.path.closed = r.Bool("closed", false);
    const Json* anchors = r.Take("anchors");
    if (anchors) o.path.anchors = AnchorsOf(*anchors);
    o.common = std::move(common);
    o.common.unknown_fields = r.Unknown();
    return MakeObject(std::move(o));
  }
  if (type == "compoundPath") {
    CompoundPathObject o;
    o.fill_rule = r.Enum("fillRule", kFillRules, FillRule::kNonZero);
    if (const Json* subpaths = r.Take("subpaths")) {
      if (!subpaths->is_array()) throw Corrupt("\"subpaths\" must be a list");
      for (const auto& s : *subpaths) {
        PathData p;
        p.closed = s.value("closed", false);
        if (s.contains("anchors")) p.anchors = AnchorsOf(s["anchors"]);
        o.subpaths.push_back(std::move(p));
      }
    }
    o.common = std::move(common);
    o.common.unknown_fields = r.Unknown();
    return MakeObject(std::move(o));
  }
  if (type == "group") {
    GroupObject o;
    o.clipped = r.Bool("clipped", false);
    if (const Json* m = r.Take("transform")) o.transform = MatrixOf(*m);
    if (const Json* children = r.Take("children")) {
      if (!children->is_array()) throw Corrupt("\"children\" must be a list");
      for (const auto& child : *children) o.children.push_back(ObjectOf(child, report));
    }
    o.common = std::move(common);
    o.common.unknown_fields = r.Unknown();
    return MakeObject(std::move(o));
  }
  ShapeObject o;
  const Json* shape = r.Take("shape");
  if (!shape) throw Corrupt("a shape needs \"shape\"");
  o.shape = *ShapeOf(*shape);
  if (const Json* m = r.Take("transform")) o.transform = MatrixOf(*m);
  o.common = std::move(common);
  o.common.unknown_fields = r.Unknown();
  return MakeObject(std::move(o));
}

// --- Layers and the document -----------------------------------------------

Json LayerJson(const Layer& layer) {
  Json j;
  j["id"] = layer.id;
  j["type"] = "layer";
  if (!layer.name.empty()) j["name"] = layer.name;
  if (!layer.visible) j["visible"] = false;
  if (layer.locked) j["locked"] = true;
  if (!layer.printable) j["printable"] = false;
  Json children = Json::array();
  for (const auto& child : layer.children) {
    if (const auto* object = std::get_if<ObjectPtr>(&child)) {
      children.push_back(ObjectJson(*object));
    } else {
      children.push_back(LayerJson(*std::get<LayerPtr>(child)));
    }
  }
  j["children"] = std::move(children);
  AppendUnknown(j, layer.unknown_fields);
  return j;
}

LayerPtr LayerOf(const Json& j, ImportReport& report) {
  Reader r(j);
  r.Take("type");
  Layer layer;
  layer.id = r.String("id");
  if (layer.id.empty()) throw Corrupt("a layer without \"id\"");
  layer.name = r.String("name");
  layer.visible = r.Bool("visible", true);
  layer.locked = r.Bool("locked", false);
  layer.printable = r.Bool("printable", true);
  if (const Json* children = r.Take("children")) {
    if (!children->is_array()) throw Corrupt("\"children\" must be a list");
    for (const auto& child : *children) {
      if (child.is_object() && child.value("type", "") == "layer") {
        layer.children.push_back(LayerOf(child, report));
      } else {
        layer.children.push_back(ObjectOf(child, report));
      }
    }
  }
  layer.unknown_fields = r.Unknown();
  return MakeLayer(std::move(layer));
}

}  // namespace

std::string WriteDocumentJson(const Document& document, std::string_view app_version) {
  Json j;
  j["format"] = {{"version", std::to_string(kFormatMajor) + "." + std::to_string(kFormatMinor)},
                 {"app", std::string(app_version)}};
  Json settings;
  settings["colorMode"] = document.settings.color_mode == ColorMode::kCmyk ? "cmyk" : "rgb";
  settings["iccProfile"] = document.settings.icc_profile;
  AppendUnknown(settings, document.settings.unknown_fields);
  j["settings"] = std::move(settings);

  Json artboards = Json::array();
  for (const auto& a : document.artboards) {
    Json board;
    board["id"] = a.id;
    if (!a.name.empty()) board["name"] = a.name;
    board["bounds"] = RectJson(a.bounds);
    AppendUnknown(board, a.unknown_fields);
    artboards.push_back(std::move(board));
  }
  j["artboards"] = std::move(artboards);

  Json swatches = Json::array();
  for (const auto& s : document.swatches) {
    Json swatch;
    swatch["id"] = s.id;
    swatch["name"] = s.name;
    if (s.kind == Swatch::Kind::kSpot) swatch["kind"] = "spot";
    swatch["color"] = ColorJson(std::visit([](const auto& c) -> Color { return c; }, s.color));
    AppendUnknown(swatch, s.unknown_fields);
    swatches.push_back(std::move(swatch));
  }
  j["swatches"] = std::move(swatches);

  Json layers = Json::array();
  for (const auto& layer : document.layers) layers.push_back(LayerJson(*layer));
  j["layers"] = std::move(layers);
  AppendUnknown(j, document.unknown_fields);
  return j.dump(2) + "\n";
}

LoadResult ReadDocumentJson(std::string_view text) {
  LoadResult result;
  const Json j = Json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    result.error = LoadError::kCorrupt;
    result.message = "document.json is not valid JSON";
    return result;
  }
  try {
    Reader r(j);
    const Json* format = r.Take("format");
    if (!format || !format->is_object() || !format->contains("version")) {
      throw Corrupt("no format version");
    }
    const std::string version = StringOf(*format, "version");
    const int major = std::atoi(version.c_str());
    if (major <= 0) throw Corrupt("bad format version \"" + version + "\"");
    if (major > kFormatMajor) {
      result.error = LoadError::kNewerVersion;
      result.message = version;
      return result;
    }

    Document document;
    if (const Json* settings = r.Take("settings")) {
      Reader s(*settings);
      document.settings.color_mode =
          s.String("colorMode", "rgb") == "cmyk" ? ColorMode::kCmyk : ColorMode::kRgb;
      document.settings.icc_profile = s.String("iccProfile", document.settings.icc_profile);
      document.settings.unknown_fields = s.Unknown();
    }
    if (const Json* artboards = r.Take("artboards")) {
      for (const auto& a : *artboards) {
        Reader b(a);
        Artboard board;
        board.id = b.String("id");
        board.name = b.String("name");
        const Json* bounds = b.Take("bounds");
        if (!bounds) throw Corrupt("an artboard needs \"bounds\"");
        board.bounds = RectOf(*bounds);
        board.unknown_fields = b.Unknown();
        document.artboards.push_back(std::move(board));
      }
    }
    if (const Json* swatches = r.Take("swatches")) {
      for (const auto& s : *swatches) {
        Reader w(s);
        Swatch swatch;
        swatch.id = w.String("id");
        swatch.name = w.String("name");
        swatch.kind =
            w.String("kind", "process") == "spot" ? Swatch::Kind::kSpot : Swatch::Kind::kProcess;
        const Json* color = w.Take("color");
        if (!color) throw Corrupt("a swatch needs \"color\"");
        swatch.color = ProcessColorOf(*color);
        swatch.unknown_fields = w.Unknown();
        document.swatches.push_back(std::move(swatch));
      }
    }
    if (const Json* layers = r.Take("layers")) {
      if (!layers->is_array()) throw Corrupt("\"layers\" must be a list");
      for (const auto& layer : *layers) document.layers.push_back(LayerOf(layer, result.report));
    }
    document.unknown_fields = r.Unknown();
    result.document = std::move(document);
  } catch (const std::exception& e) {
    result.document.reset();
    result.error = LoadError::kCorrupt;
    result.message = e.what();
  }
  return result;
}

}  // namespace leinwand::io
