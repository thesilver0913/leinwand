// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/document_renderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <variant>
#include <vector>

#include "core/style.h"
#include "geometry/bezier.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkScalar.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkLumaColorFilter.h"
#include "include/encode/SkPngEncoder.h"
#include "render/document_renderer_impl.h"
#include "text/layout.h"

namespace leinwand::render {

namespace {

using core::Rect;

SkRect ToSk(const Rect& r) {
  return SkRect::MakeLTRB(static_cast<float>(r.left), static_cast<float>(r.top),
                          static_cast<float>(r.right), static_cast<float>(r.bottom));
}

SkPoint ToSk(core::Point p) { return {static_cast<float>(p.x), static_cast<float>(p.y)}; }

SkMatrix ToSk(const core::Matrix& m) {
  return SkMatrix::MakeAll(static_cast<float>(m.a), static_cast<float>(m.c),
                           static_cast<float>(m.e), static_cast<float>(m.b),
                           static_cast<float>(m.d), static_cast<float>(m.f), 0, 0, 1);
}

SkBlendMode ToSk(core::BlendMode mode) {
  using core::BlendMode;
  switch (mode) {
    case BlendMode::kNormal:
      return SkBlendMode::kSrcOver;
    case BlendMode::kDarken:
      return SkBlendMode::kDarken;
    case BlendMode::kMultiply:
      return SkBlendMode::kMultiply;
    case BlendMode::kColorBurn:
      return SkBlendMode::kColorBurn;
    case BlendMode::kLighten:
      return SkBlendMode::kLighten;
    case BlendMode::kScreen:
      return SkBlendMode::kScreen;
    case BlendMode::kColorDodge:
      return SkBlendMode::kColorDodge;
    case BlendMode::kOverlay:
      return SkBlendMode::kOverlay;
    case BlendMode::kSoftLight:
      return SkBlendMode::kSoftLight;
    case BlendMode::kHardLight:
      return SkBlendMode::kHardLight;
    case BlendMode::kDifference:
      return SkBlendMode::kDifference;
    case BlendMode::kExclusion:
      return SkBlendMode::kExclusion;
    case BlendMode::kHue:
      return SkBlendMode::kHue;
    case BlendMode::kSaturation:
      return SkBlendMode::kSaturation;
    case BlendMode::kColor:
      return SkBlendMode::kColor;
    case BlendMode::kLuminosity:
      return SkBlendMode::kLuminosity;
  }
  return SkBlendMode::kSrcOver;
}

void AppendPath(SkPathBuilder& builder, const core::PathData& path) {
  if (path.anchors.empty()) return;
  builder.moveTo(ToSk(path.anchors[0].position));
  for (int i = 0; i < path.segment_count(); ++i) {
    const auto segment = geometry::SegmentAt(path, i);
    if (segment.p1 == segment.p0 && segment.p2 == segment.p3) {
      builder.lineTo(ToSk(segment.p3));
    } else {
      builder.cubicTo(ToSk(segment.p1), ToSk(segment.p2), ToSk(segment.p3));
    }
  }
  if (path.closed) builder.close();
}

SkPath ToSkPath(const core::Object& object) {
  SkPathBuilder builder;
  // Text is drawn as its glyph outlines, so it paints like any path (fills,
  // strokes, gradients) and looks the same as when outlined.
  const auto* text = std::get_if<core::TextObject>(&object);
  for (const auto& subpath : text ? text::OutlineOf(*text) : core::OutlineOf(object)) {
    AppendPath(builder, subpath);
  }
  builder.setFillType(core::FillRuleOf(object) == core::FillRule::kEvenOdd
                          ? SkPathFillType::kEvenOdd
                          : SkPathFillType::kWinding);
  return builder.detach();
}

core::StrokeAlign EffectiveAlign(const core::Object& object, const core::Stroke& stroke) {
  return core::IsClosed(object) ? stroke.align : core::StrokeAlign::kCenter;
}

// How far a stroke can reach beyond the geometry.
double StrokeOutset(const core::Object& object, const core::Stroke& stroke) {
  double half = stroke.width / 2;
  switch (EffectiveAlign(object, stroke)) {
    case core::StrokeAlign::kInside:
      return 0.0;
    case core::StrokeAlign::kOutside:
      half = stroke.width;
      break;
    case core::StrokeAlign::kCenter:
      break;
  }
  double factor = 1.0;
  if (stroke.join == core::StrokeJoin::kMiter) factor = std::max(factor, stroke.miter_limit);
  if (stroke.cap == core::StrokeCap::kSquare) factor = std::max(factor, std::numbers::sqrt2);
  return half * factor;
}

bool SetPaintColor(SkPaint& paint, const core::Color& color, double opacity,
                   const core::Document& document) {
  const auto rgb = core::ToRgb(color, document);
  if (!rgb) return false;
  paint.setColor4f({static_cast<float>(rgb->r), static_cast<float>(rgb->g),
                    static_cast<float>(rgb->b), static_cast<float>(opacity)});
  return true;
}

// A gradient as a shader (spec 7, "グラデーション"). Each stop's midpoint
// becomes an extra stop where the neighbours mix half and half, so a moved
// midpoint bends the ramp as in Illustrator. False when a stop's color
// cannot be shown or there are fewer than two stops.
bool SetPaintGradient(SkPaint& paint, const core::Gradient& gradient, double opacity,
                      const core::Document& document) {
  if (gradient.stops.size() < 2) return false;
  std::vector<SkColor4f> colors;
  std::vector<float> positions;
  for (size_t i = 0; i < gradient.stops.size(); ++i) {
    const core::GradientStop& stop = gradient.stops[i];
    const auto rgb = core::ToRgb(stop.color, document);
    if (!rgb) return false;
    const SkColor4f color{float(rgb->r), float(rgb->g), float(rgb->b), float(stop.opacity)};
    if (i > 0) {
      const core::GradientStop& prev = gradient.stops[i - 1];
      if (std::abs(prev.midpoint - 0.5) > 1e-6) {
        const SkColor4f a = colors.back();
        positions.push_back(
            float(prev.offset + (stop.offset - prev.offset) * std::clamp(prev.midpoint, 0.0, 1.0)));
        colors.push_back({(a.fR + color.fR) / 2, (a.fG + color.fG) / 2, (a.fB + color.fB) / 2,
                          (a.fA + color.fA) / 2});
      }
    }
    colors.push_back(color);
    positions.push_back(float(std::clamp(stop.offset, 0.0, 1.0)));
  }
  const SkGradient ramp(SkGradient::Colors(colors, positions, SkTileMode::kClamp),
                        SkGradient::Interpolation{});
  const SkPoint start = ToSk(gradient.start), end = ToSk(gradient.end);
  sk_sp<SkShader> shader;
  if (gradient.type == core::GradientType::kLinear) {
    const SkPoint points[2] = {start, end};
    shader = SkShaders::LinearGradient(points, ramp);
  } else {
    const float radius = SkPoint::Distance(start, end);
    if (radius <= 0) return false;
    // The ellipse: squashed across its axis by the aspect.
    const float degrees = SkRadiansToDegrees(std::atan2(end.y() - start.y(), end.x() - start.x()));
    SkMatrix local;
    local.setRotate(-degrees, start.x(), start.y());
    local.postScale(1, float(gradient.aspect), start.x(), start.y());
    local.postRotate(degrees, start.x(), start.y());
    if (gradient.focal && ToSk(*gradient.focal) != start) {
      shader =
          SkShaders::TwoPointConicalGradient(ToSk(*gradient.focal), 0, start, radius, ramp, &local);
    } else {
      shader = SkShaders::RadialGradient(start, radius, ramp, &local);
    }
  }
  if (!shader) return false;
  paint.setShader(std::move(shader));
  paint.setAlphaf(float(opacity));
  return true;
}

SkPaint::Cap ToSk(core::StrokeCap cap) {
  switch (cap) {
    case core::StrokeCap::kRound:
      return SkPaint::kRound_Cap;
    case core::StrokeCap::kSquare:
      return SkPaint::kSquare_Cap;
    case core::StrokeCap::kButt:
      break;
  }
  return SkPaint::kButt_Cap;
}

SkPaint::Join ToSk(core::StrokeJoin join) {
  switch (join) {
    case core::StrokeJoin::kRound:
      return SkPaint::kRound_Join;
    case core::StrokeJoin::kBevel:
      return SkPaint::kBevel_Join;
    case core::StrokeJoin::kMiter:
      break;
  }
  return SkPaint::kMiter_Join;
}

}  // namespace

const DocumentRenderer::Impl::CacheEntry& DocumentRenderer::Impl::Entry(
    const core::ObjectPtr& object) {
  CacheEntry& entry = cache_[object.get()];
  if (entry.owner.lock() != object) {
    // New object, or a new one at the address of a freed one.
    entry = {};
    entry.owner = object;
    const core::Object& o = *object;
    if (const auto* group = std::get_if<core::GroupObject>(&o)) {
      Rect bounds;
      for (const auto& child : group->children) bounds = bounds.Union(Entry(child).bounds);
      entry.bounds = geometry::MapRect(bounds, group->transform);
    } else {
      entry.path = ToSkPath(o);
      double outset = 0.0;
      for (const auto& item : core::CommonOf(o).appearance) {
        if (const auto* stroke = std::get_if<core::Stroke>(&item)) {
          outset = std::max(outset, StrokeOutset(o, *stroke));
        }
      }
      entry.bounds = geometry::Bounds(o).Outset(outset);
      if (std::holds_alternative<core::TextObject>(o) && !entry.path.isEmpty()) {
        // Glyphs may reach past the lines' boxes.
        const SkRect ink = entry.path.getBounds();
        entry.bounds = entry.bounds.Union(
            Rect{ink.left(), ink.top(), ink.right(), ink.bottom()}.Outset(outset));
      }
    }
  }
  entry.last_used = frame_;
  return entry;
}

void DocumentRenderer::Impl::Draw(SkCanvas* canvas, const core::Document& document,
                                  const View& view, int width, int height, const Overlay* overlay) {
  ++frame_;
  stats = {};
  document_ = &document;
  outline_ = overlay && overlay->outline;

  const auto& bg = settings.pasteboard;
  if (settings.artwork_only) {
    canvas->clear(settings.transparent ? SK_ColorTRANSPARENT : SK_ColorWHITE);
  } else {
    canvas->clear(SkColor4f{static_cast<float>(bg.r), static_cast<float>(bg.g),
                            static_cast<float>(bg.b), 1.0f});
  }
  canvas->save();
  canvas->translate(static_cast<float>(view.pan_x), static_cast<float>(view.pan_y));
  canvas->scale(static_cast<float>(view.zoom), static_cast<float>(view.zoom));

  // The part of the document that lands on the target, in document points.
  const Rect visible{-view.pan_x / view.zoom, -view.pan_y / view.zoom,
                     (width - view.pan_x) / view.zoom, (height - view.pan_y) / view.zoom};

  // Artboards: a soft shadow on the pasteboard, white paper, and a hairline
  // border. Shadow and border keep their size in view pixels at any zoom.
  const float px = static_cast<float>(1.0 / view.zoom);
  SkPaint shadow;
  shadow.setColor(SkColorSetARGB(90, 0, 0, 0));
  shadow.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 3 * px));
  SkPaint paper;
  paper.setColor(SK_ColorWHITE);
  SkPaint border;
  border.setColor(SkColorSetARGB(60, 0, 0, 0));
  border.setStyle(SkPaint::kStroke_Style);
  border.setStrokeWidth(0);  // Hairline: one device pixel wide.
  for (const auto& board : document.artboards) {
    if (settings.artwork_only) break;
    const SkRect rect = ToSk(board.bounds);
    canvas->drawRect(rect.makeOffset(0, 2 * px), shadow);
    canvas->drawRect(rect, paper);
    canvas->drawRect(rect, border);
    if (board.bleed > 0) {
      // The bleed guide, red as in Illustrator.
      SkPaint bleed;
      bleed.setColor(SkColorSetARGB(200, 0xe3, 0x48, 0x50));
      bleed.setStyle(SkPaint::kStroke_Style);
      bleed.setStrokeWidth(0);
      const float b = static_cast<float>(board.bleed);
      canvas->drawRect(rect.makeOutset(b, b), bleed);
    }
  }

  for (const auto& layer : document.layers) DrawLayer(canvas, *layer, visible);
  if (overlay)
    DrawOverlay(canvas, document, *overlay, px * static_cast<float>(overlay->pixel_ratio));
  canvas->restore();

  document_ = nullptr;
  PruneCache();
}

void DocumentRenderer::Impl::DrawLayer(SkCanvas* canvas, const core::Layer& layer,
                                       const Rect& visible) {
  if (!layer.visible) return;
  for (const auto& child : layer.children) {
    if (const auto* object = std::get_if<core::ObjectPtr>(&child)) {
      DrawObject(canvas, *object, visible);
    } else {
      DrawLayer(canvas, *std::get<core::LayerPtr>(child), visible);
    }
  }
}

void DocumentRenderer::Impl::DrawObject(SkCanvas* canvas, const core::ObjectPtr& object,
                                        const Rect& visible) {
  const core::ObjectCommon& common = core::CommonOf(*object);
  if (!common.visible || common.opacity <= 0.0) return;
  const CacheEntry& entry = Entry(object);
  if (!entry.bounds.Intersects(visible)) {
    ++stats.culled;
    return;
  }
  ++stats.drawn;

  // Object opacity, blending and the opacity mask apply to the object as a
  // whole, so it is composited from its own layer. So is a group with
  // isolated blending.
  const auto* as_group = std::get_if<core::GroupObject>(object.get());
  const bool masked = !outline_ && common.mask && common.mask->art;
  const bool isolate =
      !outline_ && (common.opacity < 1.0 || common.blend_mode != core::BlendMode::kNormal ||
                    masked || (as_group && as_group->isolated));
  if (isolate) {
    SkPaint layer_paint;
    layer_paint.setAlphaf(static_cast<float>(common.opacity));
    layer_paint.setBlendMode(ToSk(common.blend_mode));
    const SkRect bounds = ToSk(entry.bounds);
    canvas->saveLayer(&bounds, &layer_paint);
  }

  if (const auto* group = std::get_if<core::GroupObject>(object.get())) {
    canvas->save();
    canvas->concat(ToSk(group->transform));
    // Children cull against the visible area in the group's coordinates.
    const auto inverse = group->transform.Inverted();
    const Rect local = inverse ? geometry::MapRect(visible, *inverse) : visible;
    auto end = group->children.end();
    if (group->clipped && !group->children.empty() && !outline_) {
      --end;  // The frontmost child is the clip path and is not painted.
      canvas->clipPath(Entry(group->children.back()).path, /*doAntiAlias=*/true);
    }
    for (auto it = group->children.begin(); it != end; ++it) DrawObject(canvas, *it, local);
    canvas->restore();
  } else if (std::holds_alternative<core::PreservedObject>(*object)) {
    // A placeholder: a frame with a cross, as for missing content.
    SkPaint line;
    line.setColor(SkColorSetARGB(160, 0x80, 0x80, 0x80));
    line.setStyle(SkPaint::kStroke_Style);
    line.setStrokeWidth(0);
    line.setAntiAlias(true);
    canvas->drawPath(entry.path, line);
    const SkRect frame = entry.path.getBounds();
    canvas->drawLine(frame.left(), frame.top(), frame.right(), frame.bottom(), line);
    canvas->drawLine(frame.right(), frame.top(), frame.left(), frame.bottom(), line);
  } else if (outline_) {
    SkPaint line;
    line.setColor(SK_ColorBLACK);
    line.setStyle(SkPaint::kStroke_Style);
    line.setStrokeWidth(0);  // Hairline.
    line.setAntiAlias(true);
    canvas->drawPath(entry.path, line);
  } else {
    DrawShape(canvas, *object, entry.path);
  }

  if (masked) DrawMask(canvas, *common.mask, ToSk(entry.bounds), visible);
  if (isolate) canvas->restore();
}

// Keeps what is drawn so far in the layer where the mask art is light
// (spec 7.2, "不透明マスク"): the art's luminance becomes alpha, composited
// with destination-in.
void DocumentRenderer::Impl::DrawMask(SkCanvas* canvas, const core::OpacityMask& mask,
                                      const SkRect& bounds, const Rect& visible) {
  sk_sp<SkColorFilter> filter = SkLumaColorFilter::Make();
  if (mask.invert) {
    // alpha' = 1 - alpha
    const float invert[20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 1};
    filter = SkColorFilters::Compose(SkColorFilters::Matrix(invert), filter);
  }
  SkPaint layer_paint;
  layer_paint.setColorFilter(std::move(filter));
  layer_paint.setBlendMode(SkBlendMode::kDstIn);
  canvas->saveLayer(&bounds, &layer_paint);
  // Without "Clip" the object shows where there is no mask art: the art
  // goes over white instead of nothing.
  if (!mask.clip) canvas->drawColor(SK_ColorWHITE);
  DrawObject(canvas, mask.art, visible);
  canvas->restore();
}

void DocumentRenderer::Impl::DrawShape(SkCanvas* canvas, const core::Object& object,
                                       const SkPath& path) {
  const core::Appearance& appearance = core::CommonOf(object).appearance;
  // The stack is stored front to back; paint from the back.
  for (auto it = appearance.rbegin(); it != appearance.rend(); ++it) {
    SkPaint paint;
    paint.setAntiAlias(true);
    if (const auto* fill = std::get_if<core::Fill>(&*it)) {
      if (fill->gradient ? !SetPaintGradient(paint, *fill->gradient, fill->opacity, *document_)
                         : !SetPaintColor(paint, fill->paint, fill->opacity, *document_)) {
        continue;
      }
      paint.setBlendMode(ToSk(fill->blend_mode));
      canvas->drawPath(path, paint);
      continue;
    }
    const auto* stroke_item = std::get_if<core::Stroke>(&*it);
    if (!stroke_item) continue;  // Unknown items (from a newer version) are not drawn.
    const auto& stroke = *stroke_item;
    if (stroke.width <= 0.0) continue;
    if (stroke.gradient ? !SetPaintGradient(paint, *stroke.gradient, stroke.opacity, *document_)
                        : !SetPaintColor(paint, stroke.paint, stroke.opacity, *document_)) {
      continue;
    }
    paint.setBlendMode(ToSk(stroke.blend_mode));
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeCap(ToSk(stroke.cap));
    paint.setStrokeJoin(ToSk(stroke.join));
    paint.setStrokeMiter(static_cast<float>(stroke.miter_limit));
    if (!stroke.dashes.empty()) {
      // Skia needs an even count; an odd list repeats, as in SVG.
      std::vector<float> intervals(stroke.dashes.begin(), stroke.dashes.end());
      if (intervals.size() % 2 == 1) {
        intervals.insert(intervals.end(), stroke.dashes.begin(), stroke.dashes.end());
      }
      paint.setPathEffect(
          SkDashPathEffect::Make(intervals, static_cast<float>(stroke.dash_offset)));
    }

    // Inside and outside strokes: draw twice as wide and clip to one side
    // of the path (spec 4.3, "線の位置").
    const core::StrokeAlign align = EffectiveAlign(object, stroke);
    float width = static_cast<float>(stroke.width);
    canvas->save();
    if (align == core::StrokeAlign::kInside) {
      canvas->clipPath(path, SkClipOp::kIntersect, true);
      width *= 2;
    } else if (align == core::StrokeAlign::kOutside) {
      canvas->clipPath(path, SkClipOp::kDifference, true);
      width *= 2;
    }
    paint.setStrokeWidth(width);
    canvas->drawPath(path, paint);
    canvas->restore();
  }
}

namespace {
// Spectrum 2 accent-background-color-default (dark), for selections.
constexpr SkColor kSelection = SkColorSetRGB(0x40, 0x69, 0xfd);
// Smart guides: Illustrator's default magenta.
constexpr SkColor kGuide = SkColorSetRGB(0xff, 0x00, 0xff);

SkPaint Hairline(SkColor color) {
  SkPaint line;
  line.setColor(color);
  line.setStyle(SkPaint::kStroke_Style);
  line.setStrokeWidth(0);  // One device pixel at any scale.
  line.setAntiAlias(true);
  return line;
}
}  // namespace

void DocumentRenderer::Impl::DrawOverlay(SkCanvas* canvas, const core::Document& document,
                                         const Overlay& overlay, float px) {
  // `px` is one view pixel in document units.
  for (const auto& found : core::FindObjects(document, overlay.selection)) {
    canvas->save();
    canvas->concat(ToSk(found.to_document));
    DrawOutline(canvas, found.object, 2.0f * static_cast<float>(overlay.pixel_ratio));
    canvas->restore();
  }

  if (overlay.bounding_box) {
    const SkRect box = ToSk(*overlay.bounding_box);
    SkPaint line;
    line.setColor(kSelection);
    line.setStyle(SkPaint::kStroke_Style);
    line.setStrokeWidth(px);
    canvas->drawRect(box, line);
    SkPaint fill;
    fill.setColor(SK_ColorWHITE);
    const float half = 3.5f * px;
    const SkPoint handles[] = {{box.left(), box.top()},     {box.centerX(), box.top()},
                               {box.right(), box.top()},    {box.right(), box.centerY()},
                               {box.right(), box.bottom()}, {box.centerX(), box.bottom()},
                               {box.left(), box.bottom()},  {box.left(), box.centerY()}};
    for (const SkPoint& h : handles) {
      const SkRect r = SkRect::MakeLTRB(h.x() - half, h.y() - half, h.x() + half, h.y() + half);
      canvas->drawRect(r, fill);
      canvas->drawRect(r, line);
    }
  }

  if (overlay.key_object) {
    // The Align panel's key object: a thick frame (spec 7.2).
    SkPaint key;
    key.setColor(kSelection);
    key.setStyle(SkPaint::kStroke_Style);
    key.setStrokeWidth(3 * px);
    canvas->drawRect(ToSk(*overlay.key_object), key);
  }

  if (overlay.gradient_line) {
    // The gradient annotator (spec 7.2): a dark line under a light one so it
    // shows on any color, a round start and a square end.
    const SkPoint a = ToSk(overlay.gradient_line->first);
    const SkPoint b = ToSk(overlay.gradient_line->second);
    SkPaint under;
    under.setAntiAlias(true);
    under.setColor(SkColorSetARGB(0x99, 0, 0, 0));
    under.setStyle(SkPaint::kStroke_Style);
    under.setStrokeWidth(3 * px);
    canvas->drawLine(a, b, under);
    SkPaint line = under;
    line.setColor(SK_ColorWHITE);
    line.setStrokeWidth(px);
    canvas->drawLine(a, b, line);
    SkPaint fill;
    fill.setAntiAlias(true);
    fill.setColor(SK_ColorWHITE);
    SkPaint edge = line;
    edge.setColor(kSelection);
    const float r = 4.5f * px;
    canvas->drawCircle(a, r, fill);
    canvas->drawCircle(a, r, edge);
    const SkRect end = SkRect::MakeLTRB(b.x() - r, b.y() - r, b.x() + r, b.y() + r);
    canvas->drawRect(end, fill);
    canvas->drawRect(end, edge);
  }

  const float anchor_half = static_cast<float>(overlay.anchor_size / 2 * overlay.pixel_ratio);
  for (const auto& edited : overlay.paths) DrawEditedPath(canvas, edited, px, anchor_half);

  if (overlay.rubber_band) {
    SkPathBuilder band;
    AppendPath(band, *overlay.rubber_band);
    canvas->drawPath(band.detach(), Hairline(kSelection));
  }

  for (const auto& [from, to] : overlay.guides) {
    canvas->drawLine(ToSk(from), ToSk(to), Hairline(kGuide));
  }

  if (overlay.marquee) {
    SkPaint dashes;
    dashes.setColor(SkColorSetRGB(0x33, 0x33, 0x33));
    dashes.setStyle(SkPaint::kStroke_Style);
    dashes.setStrokeWidth(px);
    const float intervals[] = {3 * px, 3 * px};
    dashes.setPathEffect(SkDashPathEffect::Make(intervals, 0));
    canvas->drawRect(ToSk(*overlay.marquee), dashes);
  }
}

void DocumentRenderer::Impl::DrawOutline(SkCanvas* canvas, const core::ObjectPtr& object,
                                         float anchor_half) {
  if (const auto* group = std::get_if<core::GroupObject>(object.get())) {
    canvas->save();
    canvas->concat(ToSk(group->transform));
    for (const auto& child : group->children) DrawOutline(canvas, child, anchor_half);
    canvas->restore();
    return;
  }
  SkPaint line;
  line.setColor(kSelection);
  line.setStyle(SkPaint::kStroke_Style);
  line.setStrokeWidth(0);  // Hairline: one device pixel at any scale.
  line.setAntiAlias(true);
  canvas->drawPath(Entry(object).path, line);

  // Anchor points as small squares, sized in device pixels.
  std::vector<SkPoint> points;
  auto collect = [&](const core::PathData& path) {
    for (const auto& a : path.anchors) points.push_back(ToSk(a.position));
  };
  for (const auto& subpath : core::OutlineOf(*object)) collect(subpath);
  const SkMatrix ctm = canvas->getTotalMatrix();
  ctm.mapPoints(points);
  SkPaint anchor;
  anchor.setColor(kSelection);
  canvas->save();
  canvas->resetMatrix();
  for (const SkPoint& p : points) {
    canvas->drawRect(SkRect::MakeLTRB(p.x() - anchor_half, p.y() - anchor_half, p.x() + anchor_half,
                                      p.y() + anchor_half),
                     anchor);
  }
  canvas->restore();
}

void DocumentRenderer::Impl::DrawEditedPath(SkCanvas* canvas, const EditedPath& edited, float px,
                                            float anchor_half) {
  // `px` is one view pixel in document units; `anchor_half` is in device pixels.
  const core::PathData& path = edited.path;
  SkPathBuilder outline;
  AppendPath(outline, path);
  canvas->drawPath(outline.detach(), Hairline(kSelection));

  // Handles: a line from the anchor to a dot. An open path's outer ends have
  // no segment for their outer handle, so it is not shown.
  const int n = static_cast<int>(path.anchors.size());
  SkPaint dot;
  dot.setColor(kSelection);
  dot.setAntiAlias(true);
  for (int i : edited.with_handles) {
    if (i < 0 || i >= n) continue;
    const core::Anchor& a = path.anchors[i];
    const bool has_in = path.closed || i > 0;
    const bool has_out = path.closed || i < n - 1;
    const std::pair<bool, core::Point> handles[] = {{has_in, a.in_point()},
                                                    {has_out, a.out_point()}};
    for (const auto& [shown, handle] : handles) {
      if (!shown || handle == a.position) continue;
      canvas->drawLine(ToSk(a.position), ToSk(handle), Hairline(kSelection));
      canvas->drawCircle(ToSk(handle), 2.5f * px, dot);
    }
  }

  // Anchors as squares sized in device pixels: filled when selected,
  // hollow otherwise.
  std::vector<SkPoint> points;
  for (const auto& a : path.anchors) points.push_back(ToSk(a.position));
  canvas->getTotalMatrix().mapPoints(points);
  canvas->save();
  canvas->resetMatrix();
  SkPaint fill;
  SkPaint border = Hairline(kSelection);
  border.setStrokeWidth(1);
  border.setAntiAlias(false);
  for (int i = 0; i < n; ++i) {
    const SkPoint p = points[i];
    const SkRect r = SkRect::MakeLTRB(p.x() - anchor_half, p.y() - anchor_half, p.x() + anchor_half,
                                      p.y() + anchor_half);
    fill.setColor(edited.selected.contains(i) ? kSelection : SK_ColorWHITE);
    canvas->drawRect(r, fill);
    canvas->drawRect(r, border);
  }
  canvas->restore();
}

void DocumentRenderer::Impl::PruneCache() {
  // Drop entries not used for a while; their objects were edited away or
  // scrolled off long ago.
  constexpr std::uint64_t kMaxAge = 120;
  if (frame_ % 60 != 0) return;
  std::erase_if(cache_, [&](const auto& kv) {
    return frame_ - kv.second.last_used > kMaxAge || kv.second.owner.expired();
  });
}

DocumentRenderer::DocumentRenderer(RenderSettings settings)
    : impl_(std::make_unique<Impl>(settings)) {}

DocumentRenderer::~DocumentRenderer() = default;

std::vector<std::uint8_t> DocumentRenderer::RenderRaster(const core::Document& document, int width,
                                                         int height, const View& view,
                                                         const Overlay* overlay) {
  const SkImageInfo info =
      SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
  sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
  impl_->Draw(surface->getCanvas(), document, view, width, height, overlay);
  std::vector<std::uint8_t> pixels(info.computeMinByteSize());
  surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0);
  return pixels;
}

DocumentRenderer::Stats DocumentRenderer::last_stats() const { return impl_->stats; }

void DocumentRenderer::SetPasteboard(core::RgbColor color) { impl_->settings.pasteboard = color; }

std::vector<std::uint8_t> DocumentRenderer::ExportPng(const core::Document& document,
                                                      const core::Rect& area, double scale,
                                                      bool transparent) {
  const int width = static_cast<int>(std::ceil(area.width() * scale));
  const int height = static_cast<int>(std::ceil(area.height() * scale));
  // Skia's raster limit, and a sanity bound on memory (about 1 GB).
  if (width <= 0 || height <= 0 || width > 32767 || height > 32767 ||
      static_cast<double>(width) * height > 2.5e8) {
    return {};
  }
  RenderSettings settings;
  settings.artwork_only = true;
  settings.transparent = transparent;
  DocumentRenderer renderer(settings);
  const SkImageInfo info =
      SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
  sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
  if (!surface) return {};
  const View view{-area.left * scale, -area.top * scale, scale};
  renderer.impl_->Draw(surface->getCanvas(), document, view, width, height, nullptr);
  SkPixmap pixmap;
  if (!surface->peekPixels(&pixmap)) return {};
  SkDynamicMemoryWStream stream;
  if (!SkPngEncoder::Encode(&stream, pixmap, {})) return {};
  const sk_sp<SkData> data = stream.detachAsData();
  const auto* bytes = static_cast<const std::uint8_t*>(data->data());
  return {bytes, bytes + data->size()};
}

}  // namespace leinwand::render
