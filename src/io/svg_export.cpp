// SPDX-License-Identifier: GPL-3.0-or-later
// The model to SVG (spec 6.1, "書き出しの設定"): presentation attributes,
// layers as Inkscape layers, preserved elements back in place.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <variant>

#include "core/style.h"
#include "geometry/bezier.h"
#include "io/svg.h"

namespace leinwand::io {

namespace {

using namespace leinwand::core;

std::string Escape(const std::string& text) {
  std::string out;
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

class Exporter {
 public:
  Exporter(const Document& document, const SvgExportOptions& options, ImportReport* issues)
      : document_(document), options_(options), issues_(issues) {}

  std::string Run() {
    Rect area;
    if (!options_.whole_document && !document_.artboards.empty()) {
      const size_t index =
          options_.artboard >= 0 && size_t(options_.artboard) < document_.artboards.size()
              ? size_t(options_.artboard)
              : 0;
      area = document_.artboards[index].bounds;
    } else {
      for (const auto& layer : document_.layers) area = area.Union(LayerBounds(*layer));
      if (!area.IsValid()) area = Rect::FromXYWH(0, 0, 100, 100);
    }
    std::ostringstream body;
    for (const auto& layer : document_.layers) WriteLayer(body, *layer, 1);

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\""
           " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
           " xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\""
           " version=\"1.1\""
        << " width=\"" << N(area.width()) << "\" height=\"" << N(area.height()) << "\""
        << " viewBox=\"" << N(area.left) << " " << N(area.top) << " " << N(area.width()) << " "
        << N(area.height()) << "\">\n";
    if (!defs_.str().empty()) out << "  <defs>\n" << defs_.str() << "  </defs>\n";
    out << body.str();
    out << "</svg>\n";
    return out.str();
  }

 private:
  // A number with the chosen decimals, trailing zeros dropped.
  std::string N(double v) const {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", options_.decimals, v);
    std::string s = buffer;
    if (s.find('.') != std::string::npos) {
      while (s.back() == '0') s.pop_back();
      if (s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
  }

  void Issue(const std::string& kind, ReportAction action, const std::string& id) {
    if (issues_) issues_->Add(kind, action, id);
  }

  Rect LayerBounds(const Layer& layer) const {
    Rect bounds;
    for (const auto& child : layer.children) {
      if (const auto* object = std::get_if<ObjectPtr>(&child)) {
        bounds = bounds.Union(geometry::Bounds(**object));
      } else {
        bounds = bounds.Union(LayerBounds(*std::get<LayerPtr>(child)));
      }
    }
    return bounds;
  }

  std::string PathData(const std::vector<core::PathData>& subpaths) const {
    std::ostringstream d;
    for (const auto& path : subpaths) {
      if (path.anchors.empty()) continue;
      d << "M" << N(path.anchors[0].position.x) << " " << N(path.anchors[0].position.y);
      for (int i = 0; i < path.segment_count(); ++i) {
        const auto c = geometry::SegmentAt(path, i);
        if (c.p1 == c.p0 && c.p2 == c.p3) {
          if (i == path.segment_count() - 1 && path.closed) break;  // Z draws it.
          d << "L" << N(c.p3.x) << " " << N(c.p3.y);
        } else {
          d << "C" << N(c.p1.x) << " " << N(c.p1.y) << " " << N(c.p2.x) << " " << N(c.p2.y) << " "
            << N(c.p3.x) << " " << N(c.p3.y);
        }
      }
      if (path.closed) d << "Z";
    }
    return d.str();
  }

  std::string Transform(const Matrix& m) const {
    if (m.IsIdentity()) return "";
    return " transform=\"matrix(" + N(m.a) + " " + N(m.b) + " " + N(m.c) + " " + N(m.d) + " " +
           N(m.e) + " " + N(m.f) + ")\"";
  }

  std::string ColorText(const Color& paint, const std::string& id) {
    if (!std::holds_alternative<RgbColor>(paint)) {
      Issue("CMYK, gray and swatch colors (written as RGB)", ReportAction::kConverted, id);
    }
    const RgbColor rgb = ToRgb(paint, document_).value_or(RgbColor{0, 0, 0});
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02x%02x%02x", static_cast<int>(std::lround(rgb.r * 255)),
                  static_cast<int>(std::lround(rgb.g * 255)),
                  static_cast<int>(std::lround(rgb.b * 255)));
    return hex;
  }

  static const char* BlendName(BlendMode mode) {
    switch (mode) {
      case BlendMode::kNormal:
        return nullptr;
      case BlendMode::kDarken:
        return "darken";
      case BlendMode::kMultiply:
        return "multiply";
      case BlendMode::kColorBurn:
        return "color-burn";
      case BlendMode::kLighten:
        return "lighten";
      case BlendMode::kScreen:
        return "screen";
      case BlendMode::kColorDodge:
        return "color-dodge";
      case BlendMode::kOverlay:
        return "overlay";
      case BlendMode::kSoftLight:
        return "soft-light";
      case BlendMode::kHardLight:
        return "hard-light";
      case BlendMode::kDifference:
        return "difference";
      case BlendMode::kExclusion:
        return "exclusion";
      case BlendMode::kHue:
        return "hue";
      case BlendMode::kSaturation:
        return "saturation";
      case BlendMode::kColor:
        return "color";
      case BlendMode::kLuminosity:
        return "luminosity";
    }
    return nullptr;
  }

  // Attributes for one fill and/or one stroke.
  std::string PaintAttributes(const Fill* fill, const Stroke* stroke, bool fill_first,
                              const std::string& id) {
    std::string a;
    if (fill) {
      a += " fill=\"" + ColorText(fill->paint, id) + "\"";
      if (fill->opacity != 1.0) a += " fill-opacity=\"" + N(fill->opacity) + "\"";
    } else {
      a += " fill=\"none\"";
    }
    if (stroke) {
      a += " stroke=\"" + ColorText(stroke->paint, id) + "\"";
      a += " stroke-width=\"" + N(stroke->width) + "\"";
      if (stroke->cap == StrokeCap::kRound) a += " stroke-linecap=\"round\"";
      if (stroke->cap == StrokeCap::kSquare) a += " stroke-linecap=\"square\"";
      if (stroke->join == StrokeJoin::kRound) a += " stroke-linejoin=\"round\"";
      if (stroke->join == StrokeJoin::kBevel) a += " stroke-linejoin=\"bevel\"";
      if (stroke->join == StrokeJoin::kMiter && stroke->miter_limit != 4.0) {
        a += " stroke-miterlimit=\"" + N(stroke->miter_limit) + "\"";
      }
      if (!stroke->dashes.empty()) {
        std::string dashes;
        for (double d : stroke->dashes) dashes += (dashes.empty() ? "" : " ") + N(d);
        a += " stroke-dasharray=\"" + dashes + "\"";
        if (stroke->dash_offset != 0) a += " stroke-dashoffset=\"" + N(stroke->dash_offset) + "\"";
      }
      if (stroke->opacity != 1.0) a += " stroke-opacity=\"" + N(stroke->opacity) + "\"";
      if (stroke->align != StrokeAlign::kCenter) {
        Issue("inside or outside stroke (written centered)", ReportAction::kApproximated, id);
      }
      if (fill_first && fill) a += " paint-order=\"stroke\"";
    }
    return a;
  }

  std::string CommonAttributes(const ObjectCommon& common) {
    std::string a = " id=\"" + Escape(common.id) + "\"";
    if (!common.name.empty()) a += " data-name=\"" + Escape(common.name) + "\"";
    if (!common.visible) a += " display=\"none\"";
    if (common.opacity != 1.0) a += " opacity=\"" + N(common.opacity) + "\"";
    if (const char* blend = BlendName(common.blend_mode)) {
      a += " style=\"mix-blend-mode:" + std::string(blend) + "\"";
    }
    return a;
  }

  // One element per fill and stroke when the appearance is more than a fill
  // under a stroke (or with paint-order); `geometry` is the element without
  // its closing.
  void WritePainted(std::ostream& out, const ObjectCommon& common, const std::string& geometry,
                    int depth) {
    const std::string indent(depth * 2, ' ');
    std::vector<const AppearanceItem*> items;
    for (const auto& item : common.appearance) {
      if (std::holds_alternative<UnknownAppearanceItem>(item)) {
        Issue("appearance item from a newer version (omitted)", ReportAction::kDiscarded,
              common.id);
        continue;
      }
      items.push_back(&item);
    }
    const Fill* fill = nullptr;
    const Stroke* stroke = nullptr;
    int fills = 0, strokes = 0;
    for (const auto* item : items) {
      if (const auto* f = std::get_if<Fill>(item)) {
        fill = f;
        ++fills;
      } else if (const auto* s = std::get_if<Stroke>(item)) {
        stroke = s;
        ++strokes;
      }
    }
    auto blend_of = [](const AppearanceItem* item) {
      if (const auto* f = std::get_if<Fill>(item)) return f->blend_mode;
      if (const auto* s = std::get_if<Stroke>(item)) return s->blend_mode;
      return BlendMode::kNormal;
    };
    const bool item_blends = std::any_of(items.begin(), items.end(), [&](const AppearanceItem* i) {
      return blend_of(i) != BlendMode::kNormal;
    });
    if (fills <= 1 && strokes <= 1 && !item_blends) {
      // Front to back: the fill first in the list means it lies on top.
      const bool fill_first = fill && stroke && std::get_if<Fill>(items.front()) != nullptr;
      out << indent << geometry << CommonAttributes(common)
          << PaintAttributes(fill, stroke, fill_first, common.id) << "/>\n";
      return;
    }
    // Several, or blended items: a group of copies painted back to front,
    // each with its own blend mode.
    out << indent << "<g" << CommonAttributes(common) << ">\n";
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
      out << indent << "  " << geometry
          << PaintAttributes(std::get_if<Fill>(*it), std::get_if<Stroke>(*it), false, common.id);
      if (const char* blend = BlendName(blend_of(*it))) {
        out << " style=\"mix-blend-mode:" << blend << "\"";
      }
      out << "/>\n";
    }
    out << indent << "</g>\n";
  }

  void WriteObject(std::ostream& out, const ObjectPtr& object, int depth) {
    const std::string indent(depth * 2, ' ');
    std::visit(
        [&](const auto& o) {
          using T = std::decay_t<decltype(o)>;
          if constexpr (std::is_same_v<T, GroupObject>) {
            std::string clip;
            auto end = o.children.end();
            if (o.clipped && !o.children.empty()) {
              --end;
              const std::string clip_id = o.common.id + "-clip";
              defs_ << "    <clipPath id=\"" << Escape(clip_id) << "\"><path d=\""
                    << PathData(OutlineOf(*o.children.back())) << "\"";
              if (FillRuleOf(*o.children.back()) == FillRule::kEvenOdd)
                defs_ << " clip-rule=\"evenodd\"";
              defs_ << "/></clipPath>\n";
              clip = " clip-path=\"url(#" + Escape(clip_id) + ")\"";
            }
            out << indent << "<g" << CommonAttributes(o.common) << Transform(o.transform) << clip
                << ">\n";
            for (auto it = o.children.begin(); it != end; ++it) WriteObject(out, *it, depth + 1);
            out << indent << "</g>\n";
          } else if constexpr (std::is_same_v<T, PreservedObject>) {
            if (o.format != "svg") {
              Issue("content from a newer Leinwand (omitted)", ReportAction::kDiscarded,
                    o.common.id);
              return;
            }
            if (o.transform.IsIdentity()) {
              out << indent << o.data << "\n";
            } else {
              out << indent << "<g" << Transform(o.transform) << ">" << o.data << "</g>\n";
            }
          } else if constexpr (std::is_same_v<T, ShapeObject>) {
            WritePainted(out, o.common, ShapeGeometry(o), depth);
          } else {
            const std::string fill_rule =
                FillRuleOf(*object) == FillRule::kEvenOdd ? " fill-rule=\"evenodd\"" : "";
            WritePainted(out, o.common,
                         "<path d=\"" + PathData(OutlineOf(*object)) + "\"" + fill_rule, depth);
          }
        },
        object->base());
  }

  // A rectangle with even round corners and a full ellipse as SVG shapes, a
  // line as <line>; other live shapes as paths.
  std::string ShapeGeometry(const ShapeObject& o) const {
    if (const auto* rect = std::get_if<RectangleShape>(&o.shape)) {
      const Corner& c = rect->corners[0];
      const bool even = std::all_of(rect->corners.begin(), rect->corners.end(),
                                    [&](const Corner& k) { return k == c; });
      if (even && (c.radius == 0 || c.kind == CornerKind::kRound)) {
        std::string g = "<rect x=\"" + N(-rect->width / 2) + "\" y=\"" + N(-rect->height / 2) +
                        "\" width=\"" + N(rect->width) + "\" height=\"" + N(rect->height) + "\"";
        if (c.radius > 0) g += " rx=\"" + N(c.radius) + "\"";
        return g + Transform(o.transform);
      }
    }
    if (const auto* ellipse = std::get_if<EllipseShape>(&o.shape)) {
      const double sweep = std::fmod(std::abs(ellipse->pie_end - ellipse->pie_start), 360.0);
      if (sweep == 0) {
        return "<ellipse cx=\"0\" cy=\"0\" rx=\"" + N(ellipse->width / 2) + "\" ry=\"" +
               N(ellipse->height / 2) + "\"" + Transform(o.transform);
      }
    }
    if (const auto* line = std::get_if<LineShape>(&o.shape)) {
      return "<line x1=\"" + N(-line->length / 2) + "\" y1=\"0\" x2=\"" + N(line->length / 2) +
             "\" y2=\"0\"" + Transform(o.transform);
    }
    return "<path d=\"" + PathData({ShapePath(o.shape)}) + "\"" + Transform(o.transform);
  }

  void WriteLayer(std::ostream& out, const Layer& layer, int depth) {
    const std::string indent(depth * 2, ' ');
    out << indent << "<g id=\"" << Escape(layer.id) << "\" inkscape:groupmode=\"layer\"";
    if (!layer.name.empty()) out << " inkscape:label=\"" << Escape(layer.name) << "\"";
    if (!layer.visible) out << " display=\"none\"";
    out << ">\n";
    for (const auto& child : layer.children) {
      if (const auto* object = std::get_if<ObjectPtr>(&child)) {
        WriteObject(out, *object, depth + 1);
      } else {
        WriteLayer(out, *std::get<LayerPtr>(child), depth + 1);
      }
    }
    out << indent << "</g>\n";
  }

  const Document& document_;
  SvgExportOptions options_;
  ImportReport* issues_;
  std::ostringstream defs_;
};

}  // namespace

std::string ExportSvg(const Document& document, const SvgExportOptions& options,
                      ImportReport* issues) {
  return Exporter(document, options, issues).Run();
}

}  // namespace leinwand::io
