// SPDX-License-Identifier: GPL-3.0-or-later
// SVG to the model (spec 6.1, "読み込みの対応").
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <pugixml.hpp>
#include <set>
#include <sstream>

#include "core/edit.h"
#include "core/style.h"
#include "core/transform.h"
#include "geometry/bezier.h"
#include "io/svg.h"
#include "io/svg_syntax.h"
#include "text/layout.h"

namespace leinwand::io {

namespace {

using namespace leinwand::core;
using svg::CssRule;

// A paint as specified: none, a color, or a reference (url(#id)) with an
// optional fallback.
struct Paint {
  enum class Kind { kNone, kColor, kUrl, kCurrentColor } kind = Kind::kNone;
  RgbColor color;
  double alpha = 1.0;  // From rgba() or #rrggbbaa.
  std::string url;
  std::optional<RgbColor> fallback;
};

// The inherited properties (SVG 1.1 "Inherited: yes") as they apply to an
// element.
struct Inherited {
  Paint fill{Paint::Kind::kColor, RgbColor{0, 0, 0}};
  Paint stroke;
  double fill_opacity = 1.0;
  double stroke_opacity = 1.0;
  double stroke_width = 1.0;
  StrokeCap cap = StrokeCap::kButt;
  StrokeJoin join = StrokeJoin::kMiter;
  double miter_limit = 4.0;  // SVG's default (Illustrator's is 10).
  std::vector<double> dashes;
  double dash_offset = 0.0;
  FillRule fill_rule = FillRule::kNonZero;
  RgbColor color{0, 0, 0};        // For currentColor.
  bool visible = true;            // visibility
  bool paint_fill_first = false;  // paint-order: stroke drawn below the fill.
  // Text.
  std::string font_family = "sans-serif";
  double font_size = 16.0;  // CSS "medium".
  int font_weight = 400;
  bool italic = false;
  double letter_spacing = 0.0;
  std::string text_anchor = "start";
};

using Properties = std::map<std::string, std::string>;

std::string LocalName(const char* name) {
  const std::string n(name);
  const size_t colon = n.find(':');
  return colon == std::string::npos ? n : n.substr(colon + 1);
}

bool HasPrefix(const char* name) { return std::string(name).find(':') != std::string::npos; }

// Applies a transform to an object's coordinates. Paths and shapes take it
// into their geometry, so their strokes are scaled too, as SVG draws them.
bool Covers(const Rect& outer, const Rect& inner) {
  return outer.left <= inner.left && outer.top <= inner.top && outer.right >= inner.right &&
         outer.bottom >= inner.bottom;
}

ObjectPtr Bake(const ObjectPtr& object, const Matrix& m) {
  if (m.IsIdentity()) return object;
  ObjectPtr result = core::Transformed(object, m);
  if (std::holds_alternative<GroupObject>(*result) ||
      std::holds_alternative<PreservedObject>(*result)) {
    return result;
  }
  const double factor = std::sqrt(std::abs(m.Determinant()));
  if (std::abs(factor - 1.0) < 1e-12) return result;
  return std::visit(
      [&](const auto& o) -> ObjectPtr {
        auto copy = o;
        if constexpr (std::is_same_v<std::decay_t<decltype(o)>, ShapeObject>) {
          // Corner radii scale too (live shapes keep them otherwise).
          if (auto* rect = std::get_if<RectangleShape>(&copy.shape)) {
            for (auto& corner : rect->corners) corner.radius *= factor;
          } else if (auto* polygon = std::get_if<PolygonShape>(&copy.shape)) {
            polygon->corner_radius *= factor;
          }
        }
        for (auto& item : copy.common.appearance) {
          if (auto* stroke = std::get_if<Stroke>(&item)) {
            stroke->width *= factor;
            for (double& d : stroke->dashes) d *= factor;
            stroke->dash_offset *= factor;
          }
        }
        return MakeObject(std::move(copy));
      },
      result->base());
}

std::vector<std::string> Split(const std::string& text) {
  std::vector<std::string> parts;
  std::istringstream in(text);
  std::string part;
  while (in >> part) parts.push_back(part);
  return parts;
}

class Importer {
 public:
  explicit Importer(SvgImport& out) : out_(out) {}

  void Run(const pugi::xml_document& xml) {
    const pugi::xml_node root = xml.document_element();
    if (LocalName(root.name()) != "svg") {
      out_.error = "the root element is not <svg>";
      return;
    }
    root_ = root;
    CollectIds(root);
    CollectStyles(root);

    Document document;
    const Matrix to_document = RootMatrix(root, &document);
    root_matrix_ = to_document;

    Inherited inherited;
    ApplyInherited(root, Specified(root, {}), inherited);
    // Top-level groups marked as Inkscape layers become layers; the rest of
    // the content goes into layers around them, in order.
    Layer current;
    std::vector<LayerChild> definitions;
    auto flush = [&]() {
      if (current.children.empty()) return;
      current.id = FreshId("layer");
      current.name = "Layer " + std::to_string(document.layers.size() + 1);
      document.layers.push_back(MakeLayer(std::move(current)));
      current = Layer{};
    };
    for (pugi::xml_node child : root.children()) {
      if (child.type() != pugi::node_element) continue;
      if (IsLayer(child)) {
        flush();
        document.layers.push_back(ReadLayer(child, inherited, {root}));
        continue;
      }
      const std::string name = LocalName(child.name());
      if (name == "defs" || name == "title" || name == "desc" || name == "metadata" ||
          HasPrefix(child.name())) {
        // Not drawn: kept with the first layer instead of making a layer.
        if (auto object = Element(child, inherited, {root})) definitions.push_back(*object);
        continue;
      }
      if (auto object = Element(child, inherited, {root})) {
        current.children.push_back(Bake(*object, to_document));
      }
    }
    flush();
    if (document.layers.empty()) {
      Layer layer;
      layer.id = FreshId("layer");
      layer.name = "Layer 1";
      document.layers.push_back(MakeLayer(std::move(layer)));
    }
    if (!definitions.empty()) {
      Layer first = *document.layers.front();
      first.children.insert(first.children.begin(), definitions.begin(), definitions.end());
      document.layers.front() = MakeLayer(std::move(first));
    }
    if (css_skipped_ > 0) {
      for (int i = 0; i < css_skipped_; ++i) {
        out_.report.Add("CSS rule (unsupported selector)", ReportAction::kDiscarded);
      }
    }
    out_.document = std::move(document);
  }

 private:
  // An Inkscape layer, with sublayers.
  LayerPtr ReadLayer(pugi::xml_node node, const Inherited& parent,
                     std::vector<pugi::xml_node> ancestors) {
    Layer layer;
    const auto specified = Specified(node, ancestors);
    Inherited inherited = parent;
    ApplyInherited(node, specified, inherited);
    layer.id = Id(node, "layer");
    layer.name = node.attribute("inkscape:label").as_string(layer.id.c_str());
    layer.visible = Value(specified, "display") != "none";
    ancestors.push_back(node);
    for (pugi::xml_node child : node.children()) {
      if (child.type() != pugi::node_element) continue;
      if (IsLayer(child)) {
        layer.children.push_back(ReadLayer(child, inherited, ancestors));
      } else if (auto object = Element(child, inherited, ancestors)) {
        layer.children.push_back(Bake(*object, root_matrix_));
      }
    }
    return MakeLayer(std::move(layer));
  }

  // --- Ids --------------------------------------------------------------------

  void CollectIds(pugi::xml_node node) {
    for (pugi::xml_node child : node.children()) {
      if (child.type() != pugi::node_element) continue;
      if (const char* id = child.attribute("id").value(); *id) by_id_.emplace(id, child);
      CollectIds(child);
    }
  }

  std::string FreshId(const std::string& prefix) {
    std::string id;
    do {
      id = prefix + "-" + std::to_string(++counter_);
    } while (used_ids_.contains(id) || by_id_.contains(id));
    used_ids_.insert(id);
    return id;
  }

  // The element's own id when it is free, else a fresh one.
  std::string Id(pugi::xml_node node, const std::string& prefix) {
    const std::string id = node.attribute("id").value();
    if (!id.empty() && !used_ids_.contains(id)) {
      used_ids_.insert(id);
      return id;
    }
    return FreshId(prefix);
  }

  // --- Styles -----------------------------------------------------------------

  void CollectStyles(pugi::xml_node node) {
    for (pugi::xml_node child : node.children()) {
      if (child.type() != pugi::node_element) continue;
      if (LocalName(child.name()) == "style") {
        std::string css;
        for (pugi::xml_node text : child.children()) css += text.value();
        auto rules = svg::ParseStyleSheet(css, static_cast<int>(rules_.size()), &css_skipped_);
        rules_.insert(rules_.end(), rules.begin(), rules.end());
      }
      CollectStyles(child);
    }
  }

  static bool Matches(const CssRule::Compound& c, pugi::xml_node node) {
    if (!c.element.empty() && LocalName(node.name()) != c.element) return false;
    const std::string id = node.attribute("id").value();
    for (const auto& want : c.ids) {
      if (id != want) return false;
    }
    if (!c.classes.empty()) {
      const auto classes = Split(node.attribute("class").value());
      for (const auto& want : c.classes) {
        if (std::find(classes.begin(), classes.end(), want) == classes.end()) return false;
      }
    }
    return true;
  }

  // `ancestors` runs from the root down to the parent.
  static bool Matches(const CssRule& rule, pugi::xml_node node,
                      const std::vector<pugi::xml_node>& ancestors) {
    if (!Matches(rule.selector.back(), node)) return false;
    int a = static_cast<int>(ancestors.size()) - 1;
    for (int i = static_cast<int>(rule.selector.size()) - 2; i >= 0; --i) {
      while (a >= 0 && !Matches(rule.selector[i], ancestors[a])) --a;
      if (a < 0) return false;
      --a;
    }
    return true;
  }

  // The properties specified on the element: presentation attributes, then
  // matching CSS rules by specificity and order, then the style attribute.
  Properties Specified(pugi::xml_node node, const std::vector<pugi::xml_node>& ancestors) const {
    static const char* const kPresentation[] = {"fill",
                                                "stroke",
                                                "fill-opacity",
                                                "stroke-opacity",
                                                "stroke-width",
                                                "stroke-linecap",
                                                "stroke-linejoin",
                                                "stroke-miterlimit",
                                                "stroke-dasharray",
                                                "stroke-dashoffset",
                                                "fill-rule",
                                                "color",
                                                "visibility",
                                                "display",
                                                "opacity",
                                                "clip-path",
                                                "mask",
                                                "filter",
                                                "mix-blend-mode",
                                                "paint-order",
                                                "clip-rule",
                                                "isolation",
                                                "font-family",
                                                "font-size",
                                                "font-weight",
                                                "font-style",
                                                "letter-spacing",
                                                "text-anchor"};
    Properties p;
    for (const char* name : kPresentation) {
      if (const pugi::xml_attribute a = node.attribute(name)) p[name] = a.value();
    }
    std::vector<const CssRule*> matching;
    for (const auto& rule : rules_) {
      if (Matches(rule, node, ancestors)) matching.push_back(&rule);
    }
    std::sort(matching.begin(), matching.end(), [](const CssRule* a, const CssRule* b) {
      return a->specificity != b->specificity ? a->specificity < b->specificity
                                              : a->order < b->order;
    });
    for (const CssRule* rule : matching) {
      for (const auto& [k, v] : rule->declarations) p[k] = v;
    }
    for (const auto& [k, v] : svg::ParseDeclarations(node.attribute("style").value())) p[k] = v;
    return p;
  }

  static std::string Value(const Properties& p, const char* name) {
    const auto it = p.find(name);
    return it == p.end() ? std::string() : it->second;
  }

  Paint ParsePaint(const std::string& text, const Inherited& inherited) const {
    Paint paint;
    const std::string t(text);
    if (t == "none") return paint;
    if (t == "currentColor") {
      paint.kind = Paint::Kind::kColor;
      paint.color = inherited.color;
      return paint;
    }
    if (t.rfind("url(", 0) == 0) {
      const size_t close = t.find(')');
      paint.kind = Paint::Kind::kUrl;
      std::string ref = t.substr(4, close == std::string::npos ? std::string::npos : close - 4);
      ref.erase(std::remove_if(ref.begin(), ref.end(),
                               [](char c) { return c == '\'' || c == '"' || c == ' '; }),
                ref.end());
      if (!ref.empty() && ref[0] == '#') ref.erase(0, 1);
      paint.url = ref;
      if (close != std::string::npos) {
        const std::string rest = t.substr(close + 1);
        double alpha = 1;
        if (const auto c = svg::ParseColor(rest, &alpha)) paint.fallback = *c;
      }
      return paint;
    }
    double alpha = 1;
    if (const auto c = svg::ParseColor(t, &alpha)) {
      paint.kind = Paint::Kind::kColor;
      paint.color = *c;
      paint.alpha = alpha;
    }
    return paint;
  }

  void ApplyInherited(pugi::xml_node, const Properties& p, Inherited& s) const {
    auto number = [&](const char* name, double& field) {
      const std::string v = Value(p, name);
      if (v.empty() || v == "inherit") return;
      if (const auto n = svg::ParseLength(v)) field = *n;
    };
    if (const std::string v = Value(p, "color"); !v.empty() && v != "inherit") {
      if (const auto c = svg::ParseColor(v)) s.color = *c;
    }
    if (const std::string v = Value(p, "fill"); !v.empty() && v != "inherit") {
      s.fill = ParsePaint(v, s);
    }
    if (const std::string v = Value(p, "stroke"); !v.empty() && v != "inherit") {
      s.stroke = ParsePaint(v, s);
    }
    if (const std::string v = Value(p, "font-family"); !v.empty() && v != "inherit") {
      s.font_family = v;
    }
    if (const std::string v = Value(p, "font-size"); !v.empty() && v != "inherit") {
      if (const auto n = svg::ParseLength(v, s.font_size)) s.font_size = *n;
    }
    if (const std::string v = Value(p, "font-weight"); !v.empty() && v != "inherit") {
      s.font_weight = v == "bold"     ? 700
                      : v == "normal" ? 400
                      : v == "bolder" ? std::min(900, s.font_weight + 300)
                      : v == "lighter"
                          ? std::max(100, s.font_weight - 300)
                          : static_cast<int>(svg::ParseLength(v).value_or(s.font_weight));
    }
    if (const std::string v = Value(p, "font-style"); !v.empty() && v != "inherit") {
      s.italic = v == "italic" || v == "oblique";
    }
    if (const std::string v = Value(p, "letter-spacing"); !v.empty() && v != "inherit") {
      s.letter_spacing = v == "normal" ? 0.0 : svg::ParseLength(v).value_or(0.0);
    }
    if (const std::string v = Value(p, "text-anchor"); !v.empty() && v != "inherit") {
      s.text_anchor = v;
    }
    number("fill-opacity", s.fill_opacity);
    number("stroke-opacity", s.stroke_opacity);
    number("stroke-width", s.stroke_width);
    number("stroke-miterlimit", s.miter_limit);
    number("stroke-dashoffset", s.dash_offset);
    if (const std::string v = Value(p, "stroke-linecap"); !v.empty()) {
      s.cap = v == "round"    ? StrokeCap::kRound
              : v == "square" ? StrokeCap::kSquare
                              : StrokeCap::kButt;
    }
    if (const std::string v = Value(p, "stroke-linejoin"); !v.empty()) {
      s.join = v == "round"   ? StrokeJoin::kRound
               : v == "bevel" ? StrokeJoin::kBevel
                              : StrokeJoin::kMiter;
    }
    if (const std::string v = Value(p, "stroke-dasharray"); !v.empty() && v != "inherit") {
      s.dashes = v == "none" ? std::vector<double>{} : svg::ParseNumberList(v);
    }
    if (const std::string v = Value(p, "fill-rule"); !v.empty()) {
      s.fill_rule = v == "evenodd" ? FillRule::kEvenOdd : FillRule::kNonZero;
    }
    if (const std::string v = Value(p, "visibility"); !v.empty() && v != "inherit") {
      s.visible = v == "visible";
    }
    if (const std::string v = Value(p, "paint-order"); !v.empty()) {
      const auto order = Split(v);
      s.paint_fill_first = !order.empty() && order[0] == "stroke";
    }
  }

  // A fill or stroke as resolved: a color, or a gradient (with its first
  // stop's color shown in the panels).
  struct Resolved {
    Color color;
    double alpha = 1.0;
    std::optional<Gradient> gradient;
  };

  // The gradient a gradient inherits from (href), if any.
  pugi::xml_node Parent(pugi::xml_node gradient) const {
    std::string href =
        gradient.attribute("xlink:href").as_string(gradient.attribute("href").as_string());
    if (!href.empty() && href[0] == '#') href.erase(0, 1);
    const auto it = by_id_.find(href);
    return it == by_id_.end() ? pugi::xml_node() : it->second;
  }

  // The first value of an attribute along the href chain.
  std::string InheritedAttribute(pugi::xml_node gradient, const char* name) const {
    for (int depth = 0; gradient && depth < 8; ++depth, gradient = Parent(gradient)) {
      if (const auto attr = gradient.attribute(name)) return attr.value();
    }
    return {};
  }

  // The stops of the first gradient along the href chain that has any.
  std::vector<pugi::xml_node> Stops(pugi::xml_node gradient) const {
    for (int depth = 0; gradient && depth < 8; ++depth, gradient = Parent(gradient)) {
      std::vector<pugi::xml_node> stops;
      for (pugi::xml_node stop : gradient.children()) {
        if (LocalName(stop.name()) == "stop") stops.push_back(stop);
      }
      if (!stops.empty()) return stops;
    }
    return {};
  }

  // A gradient paint server, in the element's user space. `bbox` is the
  // element's geometric bounds there, for the default objectBoundingBox
  // units. nullopt without stops; one stop paints its color.
  std::optional<Resolved> GradientPaint(pugi::xml_node node, const Rect& bbox,
                                        const std::string& id, const char* what) {
    const bool radial = LocalName(node.name()) == "radialGradient";
    Gradient g;
    g.type = radial ? GradientType::kRadial : GradientType::kLinear;
    // Offsets are numbers or percentages and never go back.
    double last = 0;
    for (pugi::xml_node stop : Stops(node)) {
      const auto p = Specified(stop, {});
      double offset = svg::ParseLength(stop.attribute("offset").as_string("0"), 1.0).value_or(0);
      offset = std::clamp(std::max(offset, last), 0.0, 1.0);
      last = offset;
      auto property = [&](const char* name, const char* fallback) {
        const std::string v = Value(p, name);
        return v.empty() ? std::string(stop.attribute(name).as_string(fallback)) : v;
      };
      double alpha = 1;
      const auto color = svg::ParseColor(property("stop-color", "black"), &alpha);
      const double opacity = svg::ParseLength(property("stop-opacity", "1")).value_or(1.0);
      GradientStop s;
      s.offset = offset;
      s.color = color.value_or(RgbColor{0, 0, 0});
      s.opacity = std::clamp(alpha * opacity, 0.0, 1.0);
      g.stops.push_back(s);
    }
    if (g.stops.empty()) return std::nullopt;
    Resolved result{g.stops.front().color, g.stops.front().opacity, std::nullopt};
    if (g.stops.size() == 1) return result;

    // Gradient space to user space: the bounding box (by default), after the
    // gradientTransform.
    const bool user = InheritedAttribute(node, "gradientUnits") == "userSpaceOnUse";
    if (!user && (!bbox.IsValid() || bbox.width() <= 0 || bbox.height() <= 0)) {
      return result;  // No box to lay the gradient out in.
    }
    Matrix to_user =
        user ? Matrix{}
             : Matrix::Translate(bbox.left, bbox.top) * Matrix::Scale(bbox.width(), bbox.height());
    if (const std::string t = InheritedAttribute(node, "gradientTransform"); !t.empty()) {
      if (const auto m = svg::ParseTransform(t)) to_user = to_user * *m;
    }
    // A coordinate: a number, or a percentage taken as a fraction.
    auto coord = [&](const char* name, double fallback) {
      const std::string v = InheritedAttribute(node, name);
      return v.empty() ? fallback : svg::ParseLength(v, 1.0).value_or(fallback);
    };
    if (!radial) {
      g.start = to_user.Map({coord("x1", 0), coord("y1", 0)});
      g.end = to_user.Map({coord("x2", 1), coord("y2", 0)});
    } else {
      const double cx = coord("cx", 0.5), cy = coord("cy", 0.5), r = coord("r", 0.5);
      g.start = to_user.Map({cx, cy});
      g.end = to_user.Map({cx + r, cy});
      const Point along = to_user.MapVector({r, 0});
      const Point across = to_user.MapVector({0, r});
      const double along_length = std::hypot(along.x, along.y);
      if (along_length > 0) g.aspect = std::hypot(across.x, across.y) / along_length;
      const double fx = coord("fx", cx), fy = coord("fy", cy);
      if (fx != cx || fy != cy) g.focal = to_user.Map({fx, fy});
      // Axes no longer at right angles (a skew) cannot be kept exactly.
      if (std::abs(along.x * across.x + along.y * across.y) > 1e-9 * along_length * along_length) {
        out_.report.Add(std::string(what) + " skewed radial gradient (approximated)",
                        ReportAction::kApproximated, id);
      }
    }
    if (const std::string spread = InheritedAttribute(node, "spreadMethod");
        !spread.empty() && spread != "pad") {
      out_.report.Add(std::string(what) + " gradient spreadMethod \"" + spread + "\" (padded)",
                      ReportAction::kApproximated, id);
    }
    result.gradient = std::move(g);
    return result;
  }

  // The fill or stroke for an object, or none. Patterns are approximated by
  // their fallback color (reported).
  std::optional<Resolved> Resolve(const Paint& paint, const std::string& id, const char* what,
                                  const Rect& bbox) {
    switch (paint.kind) {
      case Paint::Kind::kNone:
        return std::nullopt;
      case Paint::Kind::kColor:
      case Paint::Kind::kCurrentColor:
        return Resolved{paint.color, paint.alpha, std::nullopt};
      case Paint::Kind::kUrl: {
        const auto it = by_id_.find(paint.url);
        const std::string kind = it == by_id_.end() ? "" : LocalName(it->second.name());
        if (kind == "linearGradient" || kind == "radialGradient") {
          return GradientPaint(it->second, bbox, id, what);
        }
        out_.report.Add(std::string(what) + " " + (kind.empty() ? "missing paint server" : kind) +
                            " (fallback color)",
                        ReportAction::kApproximated, id);
        if (paint.fallback) return Resolved{*paint.fallback, 1.0, std::nullopt};
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  // Appearance and common fields from the computed style.
  void ApplyCommon(ObjectCommon& common, const Properties& p, const Inherited& s, bool painted,
                   const Rect& bbox = {}) {
    if (Value(p, "display") == "none" || !s.visible) common.visible = false;
    if (const std::string v = Value(p, "opacity"); !v.empty()) {
      common.opacity = std::clamp(svg::ParseLength(v).value_or(1.0), 0.0, 1.0);
    }
    if (const std::string v = Value(p, "mix-blend-mode"); !v.empty() && v != "normal") {
      static const std::map<std::string, BlendMode> kModes = {
          {"multiply", BlendMode::kMultiply},     {"screen", BlendMode::kScreen},
          {"overlay", BlendMode::kOverlay},       {"darken", BlendMode::kDarken},
          {"lighten", BlendMode::kLighten},       {"color-dodge", BlendMode::kColorDodge},
          {"color-burn", BlendMode::kColorBurn},  {"hard-light", BlendMode::kHardLight},
          {"soft-light", BlendMode::kSoftLight},  {"difference", BlendMode::kDifference},
          {"exclusion", BlendMode::kExclusion},   {"hue", BlendMode::kHue},
          {"saturation", BlendMode::kSaturation}, {"color", BlendMode::kColor},
          {"luminosity", BlendMode::kLuminosity}};
      if (const auto it = kModes.find(v); it != kModes.end()) common.blend_mode = it->second;
    }
    if (!Value(p, "filter").empty() && Value(p, "filter") != "none") {
      out_.report.Add("filter", ReportAction::kDiscarded);
    }
    if (!painted) return;
    Appearance appearance;
    if (const auto stroke = Resolve(s.stroke, common.id, "stroke", bbox);
        stroke && s.stroke_width > 0) {
      Stroke item{stroke->color, stroke->gradient};
      item.width = s.stroke_width;
      item.cap = s.cap;
      item.join = s.join;
      item.miter_limit = s.miter_limit;
      // An odd dash list repeats (as in SVG); all zeros means solid.
      if (std::any_of(s.dashes.begin(), s.dashes.end(), [](double d) { return d > 0; })) {
        item.dashes = s.dashes;
      }
      item.dash_offset = s.dash_offset;
      // A gradient carries its opacity in its stops.
      item.opacity =
          std::clamp(s.stroke_opacity * (stroke->gradient ? 1.0 : stroke->alpha), 0.0, 1.0);
      appearance.push_back(item);
    }
    if (const auto fill = Resolve(s.fill, common.id, "fill", bbox)) {
      Fill item{fill->color, fill->gradient};
      item.opacity = std::clamp(s.fill_opacity * (fill->gradient ? 1.0 : fill->alpha), 0.0, 1.0);
      // Front to back: the stroke is in front unless paint-order says so.
      if (s.paint_fill_first) {
        appearance.insert(appearance.begin(), item);
      } else {
        appearance.push_back(item);
      }
    }
    common.appearance = std::move(appearance);
  }

  // --- Elements ---------------------------------------------------------------

  static bool IsLayer(pugi::xml_node node) {
    return LocalName(node.name()) == "g" &&
           std::string(node.attribute("inkscape:groupmode").value()) == "layer";
  }

  std::optional<ObjectPtr> Element(pugi::xml_node node, const Inherited& parent,
                                   std::vector<pugi::xml_node> ancestors) {
    if (node.type() != pugi::node_element) return std::nullopt;
    const std::string name = LocalName(node.name());
    if (name == "style") return std::nullopt;  // Applied already.
    const bool foreign = HasPrefix(node.name()) && std::string(node.name()).rfind("svg:", 0) != 0;

    const Properties p = Specified(node, ancestors);
    Inherited s = parent;
    ApplyInherited(node, p, s);

    Matrix transform;
    if (const char* t = node.attribute("transform").value(); *t) {
      if (const auto m = svg::ParseTransform(t)) {
        transform = *m;
      } else {
        out_.report.Add("transform (unreadable, ignored)", ReportAction::kDiscarded);
      }
    }

    std::optional<ObjectPtr> object;
    if (!foreign && (name == "g" || name == "a" || name == "switch")) {
      GroupObject group;
      group.common.id = Id(node, "group");
      group.common.name =
          node.attribute("data-name").as_string(node.attribute("inkscape:label").value());
      group.transform = transform;
      ancestors.push_back(node);
      for (pugi::xml_node child : node.children()) {
        if (auto o = Element(child, s, ancestors)) group.children.push_back(*o);
      }
      ApplyCommon(group.common, p, s, false);
      group.isolated = Value(p, "isolation") == "isolate";
      object = MakeObject(std::move(group));
      return WithMask(WithClip(*object, node, p), node, p);
    }
    if (!foreign && name == "svg") {
      // A nested viewport: its content, placed at x, y.
      GroupObject group;
      group.common.id = Id(node, "group");
      group.transform =
          Matrix::Translate(node.attribute("x").as_double(), node.attribute("y").as_double());
      if (node.attribute("viewBox")) {
        out_.report.Add("nested <svg> viewBox (ignored)", ReportAction::kApproximated,
                        group.common.id);
      }
      ancestors.push_back(node);
      for (pugi::xml_node child : node.children()) {
        if (auto o = Element(child, s, ancestors)) group.children.push_back(*o);
      }
      return MakeObject(std::move(group));
    }

    std::optional<std::vector<PathData>> outline;
    std::optional<ShapeObject> shape;
    const std::string id = Id(node, foreign ? "element" : name);
    if (!foreign && name == "path") {
      outline = svg::ParsePathData(node.attribute("d").value());
    } else if (!foreign && name == "rect") {
      const double x = svg::ParseLength(node.attribute("x").value()).value_or(0);
      const double y = svg::ParseLength(node.attribute("y").value()).value_or(0);
      const double w = svg::ParseLength(node.attribute("width").value()).value_or(0);
      const double h = svg::ParseLength(node.attribute("height").value()).value_or(0);
      auto rx = svg::ParseLength(node.attribute("rx").value());
      auto ry = svg::ParseLength(node.attribute("ry").value());
      if (!rx) rx = ry;
      if (!ry) ry = rx;
      const double r = std::min({rx.value_or(0), w / 2, h / 2});
      if (w > 0 && h > 0 && std::abs(rx.value_or(0) - ry.value_or(0)) < 1e-9) {
        ShapeObject live;
        RectangleShape rect{w, h, {}};
        for (auto& corner : rect.corners) corner.radius = r;
        live.shape = rect;
        live.transform = Matrix::Translate(x + w / 2, y + h / 2);
        shape = live;
      } else if (w > 0 && h > 0) {
        // Elliptical corners: a path.
        const double cx = std::min(rx.value_or(0), w / 2), cy = std::min(ry.value_or(0), h / 2);
        std::ostringstream d;
        d << "M" << x + cx << "," << y << " H" << x + w - cx << " A" << cx << "," << cy << " 0 0 1 "
          << x + w << "," << y + cy << " V" << y + h - cy << " A" << cx << "," << cy << " 0 0 1 "
          << x + w - cx << "," << y + h << " H" << x + cx << " A" << cx << "," << cy << " 0 0 1 "
          << x << "," << y + h - cy << " V" << y + cy << " A" << cx << "," << cy << " 0 0 1 "
          << x + cx << "," << y << " Z";
        outline = svg::ParsePathData(d.str());
      } else {
        return std::nullopt;  // Not rendered (SVG: zero size disables it).
      }
    } else if (!foreign && (name == "circle" || name == "ellipse")) {
      const double cx = svg::ParseLength(node.attribute("cx").value()).value_or(0);
      const double cy = svg::ParseLength(node.attribute("cy").value()).value_or(0);
      double rx = 0, ry = 0;
      if (name == "circle") {
        rx = ry = svg::ParseLength(node.attribute("r").value()).value_or(0);
      } else {
        rx = svg::ParseLength(node.attribute("rx").value()).value_or(0);
        ry = svg::ParseLength(node.attribute("ry").value()).value_or(0);
      }
      if (rx <= 0 || ry <= 0) return std::nullopt;
      ShapeObject live;
      live.shape = EllipseShape{rx * 2, ry * 2, 0, 360};
      live.transform = Matrix::Translate(cx, cy);
      shape = live;
    } else if (!foreign && name == "line") {
      PathData line;
      line.anchors = {{{svg::ParseLength(node.attribute("x1").value()).value_or(0),
                        svg::ParseLength(node.attribute("y1").value()).value_or(0)}},
                      {{svg::ParseLength(node.attribute("x2").value()).value_or(0),
                        svg::ParseLength(node.attribute("y2").value()).value_or(0)}}};
      outline = std::vector<PathData>{line};
    } else if (!foreign && (name == "polyline" || name == "polygon")) {
      const auto v = svg::ParseNumberList(node.attribute("points").value());
      PathData poly;
      for (size_t i = 0; i + 1 < v.size(); i += 2) poly.anchors.push_back({{v[i], v[i + 1]}});
      poly.closed = name == "polygon";
      if (poly.anchors.empty()) return std::nullopt;
      outline = std::vector<PathData>{poly};
    }

    if (shape) {
      shape->common.id = id;
      ApplyCommon(shape->common, p, s, true, geometry::Bounds(*MakeObject(*shape)));
      object = Bake(MakeObject(*shape), transform);
      return WithMask(WithClip(*object, node, p), node, p);
    }
    if (outline) {
      std::erase_if(*outline, [](const PathData& d) { return d.anchors.empty(); });
      if (outline->empty()) return std::nullopt;
      Rect bbox;
      for (const PathData& d : *outline) bbox = bbox.Union(geometry::Bounds(d));
      if (outline->size() == 1 && s.fill_rule == FillRule::kNonZero) {
        PathObject path;
        path.common.id = id;
        path.path = outline->front();
        ApplyCommon(path.common, p, s, true, bbox);
        object = MakeObject(std::move(path));
      } else {
        CompoundPathObject compound;
        compound.common.id = id;
        compound.subpaths = *outline;
        compound.fill_rule = s.fill_rule;
        ApplyCommon(compound.common, p, s, true, bbox);
        object = MakeObject(std::move(compound));
      }
      object = Bake(*object, transform);
      return WithMask(WithClip(*object, node, p), node, p);
    }

    if (!foreign && name == "text") {
      if (auto text = Text(node, p, s, transform, ancestors, id)) {
        return WithMask(WithClip(*text, node, p), node, p);
      }
    }

    // Everything else is kept as XML (spec 6.1): written back in place.
    PreservedObject preserved;
    preserved.common.id = id;
    preserved.common.name = "<" + std::string(node.name()) + ">";
    preserved.format = "svg";
    preserved.data = Serialize(node, p, s, ancestors);
    const char* x = node.attribute("x").value();
    const char* w = node.attribute("width").value();
    const char* h = node.attribute("height").value();
    if (*w && *h) {
      const Rect r =
          Rect::FromXYWH(svg::ParseLength(x).value_or(0),
                         svg::ParseLength(node.attribute("y").value()).value_or(0),
                         svg::ParseLength(w).value_or(0), svg::ParseLength(h).value_or(0));
      preserved.bounds = geometry::MapRect(r, transform);
    }
    out_.report.Add("<" + std::string(node.name()) + ">", ReportAction::kPreserved, id);
    return MakeObject(std::move(preserved));
  }

  // The element as XML text, with what it got from outside written onto it:
  // the namespaces of the document, inherited paint, and CSS rules (which
  // are not exported as a style sheet).
  std::string Serialize(pugi::xml_node node, const Properties& p, const Inherited& s,
                        const std::vector<pugi::xml_node>& ancestors) {
    pugi::xml_document copy;
    pugi::xml_node element = copy.append_copy(node);
    for (pugi::xml_attribute a : root_.attributes()) {
      const std::string name = a.name();
      if (name.rfind("xmlns", 0) == 0 && !element.attribute(a.name())) {
        element.append_attribute(a.name()) = a.value();
      }
    }
    InlineCss(element, node, ancestors);
    // Inherited paint that came from groups Leinwand does not write styles on.
    auto paint_text = [](const Paint& paint) -> std::string {
      if (paint.kind == Paint::Kind::kNone) return "none";
      if (paint.kind == Paint::Kind::kUrl) return "url(#" + paint.url + ")";
      char hex[8];
      std::snprintf(hex, sizeof hex, "#%02x%02x%02x",
                    static_cast<int>(std::lround(paint.color.r * 255)),
                    static_cast<int>(std::lround(paint.color.g * 255)),
                    static_cast<int>(std::lround(paint.color.b * 255)));
      return hex;
    };
    if (p.find("fill") == p.end()) element.append_attribute("fill") = paint_text(s.fill).c_str();
    if (p.find("stroke") == p.end() && s.stroke.kind != Paint::Kind::kNone) {
      element.append_attribute("stroke") = paint_text(s.stroke).c_str();
    }
    std::ostringstream out;
    element.print(out, "", pugi::format_raw);
    return out.str();
  }

  // Writes the declarations of matching CSS rules into style attributes of
  // `copy` and its descendants (mirroring `original`).
  void InlineCss(pugi::xml_node copy, pugi::xml_node original,
                 std::vector<pugi::xml_node> ancestors) {
    if (rules_.empty()) return;
    Properties css;
    std::vector<const CssRule*> matching;
    for (const auto& rule : rules_) {
      if (Matches(rule, original, ancestors)) matching.push_back(&rule);
    }
    std::sort(matching.begin(), matching.end(), [](const CssRule* a, const CssRule* b) {
      return a->specificity != b->specificity ? a->specificity < b->specificity
                                              : a->order < b->order;
    });
    for (const CssRule* rule : matching) {
      for (const auto& [k, v] : rule->declarations) css[k] = v;
    }
    if (!css.empty()) {
      for (const auto& [k, v] : svg::ParseDeclarations(original.attribute("style").value()))
        css[k] = v;
      std::string style;
      for (const auto& [k, v] : css) style += k + ":" + v + ";";
      if (pugi::xml_attribute a = copy.attribute("style")) {
        a.set_value(style.c_str());
      } else {
        copy.append_attribute("style") = style.c_str();
      }
    }
    ancestors.push_back(original);
    pugi::xml_node c = copy.first_child();
    for (pugi::xml_node o = original.first_child(); o && c;
         o = o.next_sibling(), c = c.next_sibling()) {
      if (o.type() == pugi::node_element) InlineCss(c, o, ancestors);
    }
  }

  // <text> as point text (spec 6.1): its characters and <tspan>s become a
  // story, a new line wherever the text moves down. Positions within a line
  // are left to the layout. Text on a path stays as XML (phase 3).
  std::optional<ObjectPtr> Text(pugi::xml_node node, const Properties& p, const Inherited& s,
                                const Matrix& transform, std::vector<pugi::xml_node> ancestors,
                                const std::string& id) {
    bool supported = true;
    std::function<void(pugi::xml_node)> check = [&](pugi::xml_node n) {
      for (pugi::xml_node c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        if (LocalName(c.name()) != "tspan") supported = false;
        check(c);
      }
    };
    check(node);
    if (!supported) return std::nullopt;

    struct Chunk {
      std::u32string text;
      Inherited style;
      std::optional<double> x, y;
      double dy = 0;
    };
    std::vector<Chunk> chunks;
    const bool preserve = std::string(node.attribute("xml:space").value()) == "preserve";
    bool positioned_inside = false;
    auto first_of = [](const char* list) -> std::optional<double> {
      const auto values = svg::ParseNumberList(list);
      if (values.empty()) return std::nullopt;
      return values.front();
    };
    std::function<void(pugi::xml_node, const Inherited&, std::optional<double>,
                       std::optional<double>, double, std::vector<pugi::xml_node>)>
        walk = [&](pugi::xml_node n, const Inherited& style, std::optional<double> x,
                   std::optional<double> y, double dy, std::vector<pugi::xml_node> anc) {
          if (svg::ParseNumberList(n.attribute("x").value()).size() > 1 || n.attribute("rotate") ||
              n.attribute("textLength")) {
            positioned_inside = true;
          }
          anc.push_back(n);
          for (pugi::xml_node c : n.children()) {
            if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata) {
              chunks.push_back({core::FromUtf8(c.value()), style, x, y, dy});
              x.reset();
              y.reset();
              dy = 0;
            } else if (c.type() == pugi::node_element) {
              const Properties cp = Specified(c, anc);
              Inherited cs = style;
              ApplyInherited(c, cp, cs);
              std::optional<double> cx = first_of(c.attribute("x").value());
              std::optional<double> cy = first_of(c.attribute("y").value());
              const double cdy = first_of(c.attribute("dy").value()).value_or(0);
              if (first_of(c.attribute("dx").value())) positioned_inside = true;
              walk(c, cs, cx ? cx : x, cy ? cy : y, cdy + dy, anc);
              x.reset();
              y.reset();
              dy = 0;
            }
          }
        };
    walk(node, s, first_of(node.attribute("x").value()), first_of(node.attribute("y").value()),
         first_of(node.attribute("dy").value()).value_or(0), ancestors);

    // White space as SVG treats it.
    for (Chunk& chunk : chunks) {
      std::u32string out;
      for (char32_t c : chunk.text) {
        if (c == U'\n' || c == U'\r') {
          if (preserve) out.push_back(U' ');
          continue;
        }
        if (c == U'\t') c = U' ';
        if (!preserve && c == U' ' && !out.empty() && out.back() == U' ') continue;
        out.push_back(c);
      }
      chunk.text = std::move(out);
    }
    if (!preserve) {
      bool after_space = true;  // Leading space goes.
      for (Chunk& chunk : chunks) {
        if (after_space && !chunk.text.empty() && chunk.text.front() == U' ') {
          chunk.text.erase(0, 1);
        }
        if (!chunk.text.empty()) after_space = chunk.text.back() == U' ';
      }
      for (auto it = chunks.rbegin(); it != chunks.rend(); ++it) {
        if (it->text.empty()) continue;
        if (it->text.back() == U' ') it->text.pop_back();
        break;
      }
    }

    // The story: a paragraph per line.
    core::Story story;
    story.id = FreshId("story");
    story.characters.clear();
    std::vector<double> leadings{0};  // Per paragraph; 0 = auto.
    std::optional<double> origin_x, origin_y, line_y;
    bool indented = false;
    for (const Chunk& chunk : chunks) {
      if (chunk.text.empty() && !chunk.y && chunk.dy == 0) continue;
      double y = line_y.value_or(chunk.y.value_or(0));
      if (chunk.y) y = *chunk.y;
      y += chunk.dy;
      if (!line_y) {
        origin_x = chunk.x.value_or(0);
        origin_y = y;
        line_y = y;
      } else if (std::abs(y - *line_y) > 1e-9) {
        story.text.push_back(U'\n');
        story.characters.push_back({1, story.characters.back().style});
        leadings.push_back(y - *line_y);
        line_y = y;
        if (chunk.x && std::abs(*chunk.x - *origin_x) > 1e-6) indented = true;
      } else if (chunk.x) {
        positioned_inside = true;
      }
      core::CharacterStyle style;
      style.font = FontFor(chunk.style);
      style.size = chunk.style.font_size;
      if (chunk.style.letter_spacing != 0 && style.size > 0) {
        style.tracking = chunk.style.letter_spacing / style.size * 1000;
      }
      story.text += chunk.text;
      story.characters.push_back({chunk.text.size(), style});
    }
    if (!line_y) return std::nullopt;  // No text at all.
    if (story.characters.empty()) story.characters.push_back({0, {}});
    story.paragraphs.assign(leadings.size(), core::ParagraphStyle{});
    const core::TextAlign align = s.text_anchor == "middle" ? core::TextAlign::kCenter
                                  : s.text_anchor == "end"  ? core::TextAlign::kRight
                                                            : core::TextAlign::kLeft;
    for (auto& paragraph : story.paragraphs) paragraph.align = align;
    // Normalise the runs (joins equal neighbours, drops empty ones).
    core::Story normal = core::Inserted(story, 0, U"", nullptr);
    normal = core::WithCharacterStyle(normal, 0, normal.text.size(), [](core::CharacterStyle&) {});
    // Line spacing other than auto becomes the lines' leading.
    for (std::size_t para = 1; para < leadings.size(); ++para) {
      const std::size_t from = core::ParagraphStart(normal, para);
      const std::size_t to = std::max(core::ParagraphEnd(normal, para), from);
      const double leading = leadings[para];
      normal = core::WithCharacterStyle(normal, from == to ? from : from, to,
                                        [&](core::CharacterStyle& c) {
                                          if (std::abs(core::LeadingOf(c) - leading) > 1e-6) {
                                            c.leading = leading;
                                          }
                                        });
    }
    if (positioned_inside || indented) {
      out_.report.Add("text positioned by character or line (laid out again)",
                      ReportAction::kApproximated, id);
    }
    core::TextObject text;
    text.common.id = id;
    text.story = std::make_shared<const core::Story>(std::move(normal));
    text.transform = transform * Matrix::Translate(*origin_x, *origin_y);
    ApplyCommon(text.common, p, s, true);
    return MakeObject(std::move(text));
  }

  // The font for a CSS font-family list, weight and style: the first family
  // there is (generic names mean the default font), in the style closest to
  // the weight.
  core::FontRef FontFor(const Inherited& s) const {
    std::vector<std::string> names;
    std::vector<std::string> listed{""};
    for (char c : s.font_family) {
      if (c == ',') {
        listed.emplace_back();
      } else {
        listed.back() += c;
      }
    }
    for (std::string name : listed) {
      std::erase_if(name, [](char c) { return c == '\'' || c == '"'; });
      while (!name.empty() && name.front() == ' ') name.erase(0, 1);
      while (!name.empty() && name.back() == ' ') name.pop_back();
      if (!name.empty()) names.push_back(name);
    }
    const std::vector<text::FontFamily> families = text::Families();
    for (const std::string& name : names) {
      for (const text::FontFamily& family : families) {
        if (family.name != name || family.styles.empty()) continue;
        std::string best = family.styles.front();
        int best_distance = 1000;
        for (const std::string& style : family.styles) {
          int distance = std::abs(WeightOfStyle(style) - s.font_weight);
          if (IsItalicStyle(style) != s.italic) distance += 50;
          if (distance < best_distance) {
            best_distance = distance;
            best = style;
          }
        }
        return {family.name, best, {}};
      }
    }
    core::FontRef font = core::DefaultFont();
    if (s.font_weight >= 600) {
      font.style = "Bold";
      font.postscript_name.clear();
    }
    if (!names.empty() && names.front() != "sans-serif" && names.front() != "serif" &&
        names.front() != "monospace") {
      // A font that is not here: keep its name (spec 5.2, missing fonts).
      font = {names.front(), s.font_weight >= 600 ? "Bold" : "Regular", {}};
    }
    return font;
  }

  static int WeightOfStyle(const std::string& style) {
    std::string s;
    for (char c : style) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::erase(s, ' ');
    std::erase(s, '-');
    static const std::pair<const char*, int> kWeights[] = {
        {"extralight", 200}, {"ultralight", 200}, {"semibold", 600}, {"demibold", 600},
        {"extrabold", 800},  {"ultrabold", 800},  {"thin", 100},     {"hairline", 100},
        {"light", 300},      {"medium", 500},     {"bold", 700},     {"black", 900},
        {"heavy", 900}};
    for (const auto& [name, weight] : kWeights) {
      if (s.find(name) != std::string::npos) return weight;
    }
    return 400;
  }
  static bool IsItalicStyle(const std::string& style) {
    std::string s;
    for (char c : style) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s.find("italic") != std::string::npos || s.find("oblique") != std::string::npos;
  }

  // The element a "url(#id)" reference points to, if it is a `kind`.
  std::optional<pugi::xml_node> Referenced(const std::string& value, const char* kind) const {
    if (value.rfind("url(", 0) != 0) return std::nullopt;
    std::string ref = value.substr(4, value.find(')') - 4);
    ref.erase(std::remove_if(ref.begin(), ref.end(),
                             [](char c) { return c == '\'' || c == '"' || c == ' ' || c == '#'; }),
              ref.end());
    const auto it = by_id_.find(ref);
    if (it == by_id_.end() || LocalName(it->second.name()) != kind) return std::nullopt;
    return it->second;
  }

  // Whether a filter only inverts colors (as Leinwand writes inverted masks).
  bool IsInvertFilter(const std::string& value) const {
    const auto filter = Referenced(value, "filter");
    if (!filter) return false;
    int count = 0;
    pugi::xml_node only;
    for (pugi::xml_node child : filter->children()) {
      if (child.type() != pugi::node_element) continue;
      ++count;
      only = child;
    }
    if (count != 1 || LocalName(only.name()) != "feColorMatrix") return false;
    const auto v = svg::ParseNumberList(only.attribute("values").value());
    static const double kInvert[20] = {-1, 0, 0,  0, 1, 0, -1, 0, 0, 1,
                                       0,  0, -1, 0, 1, 0, 0,  0, 1, 0};
    return v.size() == 20 && std::equal(v.begin(), v.end(), kInvert);
  }

  // mask="url(#id)": a luminance mask becomes the object's opacity mask
  // (spec 6.1). The mask's content is in the element's user space, which
  // includes its transform; the model keeps it in the parent's.
  ObjectPtr WithMask(const ObjectPtr& object, pugi::xml_node node, const Properties& p) {
    const std::string value = Value(p, "mask");
    if (value.empty() || value == "none") return object;
    const std::string id = CommonOf(*object).id;
    const auto mask_node = Referenced(value, "mask");
    if (!mask_node) {
      out_.report.Add("mask (missing <mask>, ignored)", ReportAction::kDiscarded, id);
      return object;
    }
    if (std::string(mask_node->attribute("maskContentUnits").value()) == "objectBoundingBox") {
      out_.report.Add("mask in object units (ignored)", ReportAction::kDiscarded, id);
      return object;
    }
    if (std::string(mask_node->attribute("mask-type").value()) == "alpha") {
      out_.report.Add("alpha mask (as a luminance mask)", ReportAction::kApproximated, id);
    }
    if (std::string(mask_node->attribute("maskUnits").value()) != "userSpaceOnUse" &&
        (mask_node->attribute("x") || mask_node->attribute("width"))) {
      out_.report.Add("mask region (ignored)", ReportAction::kApproximated, id);
    }
    Matrix element_transform;
    if (const auto m = svg::ParseTransform(node.attribute("transform").value()))
      element_transform = *m;

    OpacityMask mask;
    pugi::xml_node content = *mask_node;
    {
      // Leinwand's inverted masks: everything inside one color-inverting group.
      std::vector<pugi::xml_node> elements;
      for (pugi::xml_node child : content.children()) {
        if (child.type() == pugi::node_element) elements.push_back(child);
      }
      if (elements.size() == 1 && LocalName(elements[0].name()) == "g" &&
          IsInvertFilter(elements[0].attribute("filter").value())) {
        mask.invert = true;
        content = elements[0];
      }
    }
    const Rect masked = geometry::Bounds(*object);
    std::vector<ObjectPtr> art;
    bool first = true;
    for (pugi::xml_node child : content.children()) {
      if (child.type() != pugi::node_element) continue;
      Inherited plain;
      auto o = Element(child, plain, {root_, *mask_node});
      if (!o) continue;
      ObjectPtr placed = Bake(*o, element_transform);
      // A first rectangle in plain white or black over the whole object is
      // the background: white shows the object where there is no other art.
      if (first && LocalName(child.name()) == "rect") {
        const Fill* fill = core::FrontFill(CommonOf(*placed).appearance);
        const RgbColor* rgb =
            fill && !fill->gradient ? std::get_if<RgbColor>(&fill->paint) : nullptr;
        const bool white = rgb && *rgb == RgbColor{1, 1, 1};
        const bool black = rgb && *rgb == RgbColor{0, 0, 0};
        if ((white || black) && fill->opacity == 1.0 && Covers(geometry::Bounds(*placed), masked)) {
          mask.clip = mask.invert ? black : !white;
          first = false;
          continue;
        }
      }
      first = false;
      art.push_back(placed);
    }
    if (art.empty()) {
      if (mask.clip == mask.invert) return object;  // All white: no mask.
      GroupObject empty;
      empty.common.id = FreshId("mask");
      art.push_back(MakeObject(std::move(empty)));
    }
    if (art.size() == 1) {
      mask.art = art.front();
    } else {
      GroupObject group;
      group.common.id = FreshId("mask");
      group.children = std::move(art);
      mask.art = MakeObject(std::move(group));
    }
    return std::visit(
        [&](const auto& o) -> ObjectPtr {
          auto copy = o;
          copy.common.mask = std::make_shared<const OpacityMask>(std::move(mask));
          return MakeObject(std::move(copy));
        },
        object->base());
  }

  // clip-path="url(#id)": the object goes into a clipping group with the
  // clip path's outline (spec 6.1: clipPath to a clipping mask).
  ObjectPtr WithClip(const ObjectPtr& object, pugi::xml_node node, const Properties& p) {
    std::string clip = Value(p, "clip-path");
    if (clip.empty() || clip == "none") return object;
    const std::string id = CommonOf(*object).id;
    if (clip.rfind("url(", 0) != 0) {
      out_.report.Add("clip-path shape (ignored)", ReportAction::kDiscarded);
      return object;
    }
    std::string ref = clip.substr(4, clip.find(')') - 4);
    ref.erase(std::remove_if(ref.begin(), ref.end(),
                             [](char c) { return c == '\'' || c == '"' || c == ' ' || c == '#'; }),
              ref.end());
    const auto it = by_id_.find(ref);
    if (it == by_id_.end() || LocalName(it->second.name()) != "clipPath") {
      out_.report.Add("clip-path (missing <clipPath>, ignored)", ReportAction::kDiscarded);
      return object;
    }
    const pugi::xml_node clip_node = it->second;
    if (std::string(clip_node.attribute("clipPathUnits").value()) == "objectBoundingBox") {
      out_.report.Add("clipPath in object units (ignored)", ReportAction::kDiscarded);
      return object;
    }
    Matrix clip_transform;
    if (const auto m = svg::ParseTransform(clip_node.attribute("transform").value()))
      clip_transform = *m;
    std::vector<PathData> subpaths;
    for (pugi::xml_node child : clip_node.children()) {
      if (child.type() != pugi::node_element) continue;
      Inherited plain;
      const auto saved_report = out_.report;
      auto shape = Element(child, plain, {root_, clip_node});
      out_.report = saved_report;  // The clip's own elements are not content.
      if (!shape) continue;
      for (const auto& outline : OutlineOf(**shape)) {
        subpaths.push_back(core::Transformed(outline, clip_transform));
      }
    }
    if (subpaths.empty()) return object;
    // The clip is in the user space of the element, which includes the
    // element's transform; paths have it applied already.
    Matrix element_transform;
    if (const auto m = svg::ParseTransform(node.attribute("transform").value()))
      element_transform = *m;
    const bool is_group = std::holds_alternative<GroupObject>(*object);
    CompoundPathObject clip_path;
    clip_path.common.id = FreshId("clip");
    clip_path.subpaths = subpaths;
    ObjectPtr clip_object = MakeObject(std::move(clip_path));
    if (!is_group) clip_object = core::Transformed(clip_object, element_transform);
    GroupObject group;
    group.common.id = FreshId("clip-group");
    group.clipped = true;
    if (is_group) {
      // Inside the group's own coordinates: clip sits next to the content.
      const auto& inner = std::get<GroupObject>(*object);
      group.transform = inner.transform;
      GroupObject content = inner;
      content.transform = Matrix{};
      group.children = {MakeObject(std::move(content)), clip_object};
    } else {
      group.children = {object, clip_object};
    }
    out_.report.Add("clip-path (clipping group)", ReportAction::kConverted, id);
    return MakeObject(std::move(group));
  }

  // --- The root ---------------------------------------------------------------

  // Maps the root's user space to points and sets the artboard.
  Matrix RootMatrix(pugi::xml_node root, Document* document) {
    auto to_points = [](const char* text, double fallback) {
      const std::string t(text);
      if (t.empty()) return fallback;
      const auto px = svg::ParseLength(t);
      if (!px) return fallback;
      // Physical units keep their size; px and plain numbers count as points.
      const bool physical = t.find("mm") != std::string::npos ||
                            t.find("cm") != std::string::npos ||
                            t.find("in") != std::string::npos ||
                            t.find("pt") != std::string::npos || t.find("pc") != std::string::npos;
      return physical ? *px * 72.0 / 96.0 : *px;
    };
    const auto vb = svg::ParseNumberList(root.attribute("viewBox").value());
    const bool has_viewbox = vb.size() == 4 && vb[2] > 0 && vb[3] > 0;
    const double width = to_points(root.attribute("width").value(), has_viewbox ? vb[2] : 300);
    const double height = to_points(root.attribute("height").value(), has_viewbox ? vb[3] : 150);
    document->artboards = {{"artboard", "Artboard 1", Rect::FromXYWH(0, 0, width, height), {}}};
    if (!has_viewbox) {
      // User units are px; a physical size scales them.
      const double px = svg::ParseLength(root.attribute("width").value()).value_or(width);
      const double scale = px > 0 ? width / px : 1.0;
      return Matrix::Scale(scale, scale);
    }
    // preserveAspectRatio: the default xMidYMid meet, or none.
    double sx = width / vb[2], sy = height / vb[3];
    double tx = 0, ty = 0;
    if (std::string(root.attribute("preserveAspectRatio").value()).rfind("none", 0) != 0) {
      const double s = std::min(sx, sy);
      tx = (width - vb[2] * s) / 2;
      ty = (height - vb[3] * s) / 2;
      sx = sy = s;
    }
    return Matrix::Translate(tx, ty) * Matrix::Scale(sx, sy) * Matrix::Translate(-vb[0], -vb[1]);
  }

  SvgImport& out_;
  pugi::xml_node root_;
  Matrix root_matrix_;
  std::map<std::string, pugi::xml_node> by_id_;
  std::set<std::string> used_ids_;
  std::vector<CssRule> rules_;
  int css_skipped_ = 0;
  int counter_ = 0;
};

}  // namespace

SvgImport ImportSvg(std::string_view xml) {
  SvgImport result;
  pugi::xml_document document;
  const pugi::xml_parse_result parsed = document.load_buffer(
      xml.data(), xml.size(), pugi::parse_default | pugi::parse_ws_pcdata_single);
  if (!parsed) {
    result.error = parsed.description();
    return result;
  }
  Importer(result).Run(document);
  return result;
}

}  // namespace leinwand::io
