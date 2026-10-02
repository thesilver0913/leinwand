// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/document_renderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <variant>
#include <vector>

#include "geometry/bezier.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkDashPathEffect.h"
#include "render/document_renderer_impl.h"

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
  if (const auto* path = std::get_if<core::PathObject>(&object)) {
    AppendPath(builder, path->path);
  } else if (const auto* compound = std::get_if<core::CompoundPathObject>(&object)) {
    for (const auto& subpath : compound->subpaths) AppendPath(builder, subpath);
    builder.setFillType(compound->fill_rule == core::FillRule::kEvenOdd ? SkPathFillType::kEvenOdd
                                                                        : SkPathFillType::kWinding);
  }
  return builder.detach();
}

// Inside and outside alignment need a region, so only closed shapes get it.
bool IsClosedShape(const core::Object& object) {
  if (const auto* path = std::get_if<core::PathObject>(&object)) return path->path.closed;
  if (const auto* compound = std::get_if<core::CompoundPathObject>(&object)) {
    return std::all_of(compound->subpaths.begin(), compound->subpaths.end(),
                       [](const core::PathData& p) { return p.closed; });
  }
  return false;
}

core::StrokeAlign EffectiveAlign(const core::Object& object, const core::Stroke& stroke) {
  return IsClosedShape(object) ? stroke.align : core::StrokeAlign::kCenter;
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

core::RgbColor ToRgb(const core::ProcessColor& color) {
  if (const auto* rgb = std::get_if<core::RgbColor>(&color)) return *rgb;
  if (const auto* gray = std::get_if<core::GrayColor>(&color)) {
    return {gray->gray, gray->gray, gray->gray};
  }
  // Naive device CMYK until color management arrives (phase 4).
  const auto& cmyk = std::get<core::CmykColor>(color);
  return {(1 - cmyk.c) * (1 - cmyk.k), (1 - cmyk.m) * (1 - cmyk.k), (1 - cmyk.y) * (1 - cmyk.k)};
}

// Null when the color refers to a swatch the document does not have.
std::optional<core::RgbColor> Resolve(const core::Color& color, const core::Document& document) {
  return std::visit(
      [&](const auto& c) -> std::optional<core::RgbColor> {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, core::SpotColor>) {
          const core::Swatch* swatch = document.FindSwatch(c.swatch_id);
          if (!swatch) return std::nullopt;
          // A tint mixes the full-strength color with paper white.
          const core::RgbColor full = ToRgb(swatch->color);
          return core::RgbColor{1 - c.tint * (1 - full.r), 1 - c.tint * (1 - full.g),
                                1 - c.tint * (1 - full.b)};
        } else if constexpr (std::is_same_v<T, core::SwatchRef>) {
          const core::Swatch* swatch = document.FindSwatch(c.swatch_id);
          if (!swatch) return std::nullopt;
          return ToRgb(swatch->color);
        } else {
          return ToRgb(core::ProcessColor{c});
        }
      },
      color);
}

bool SetPaintColor(SkPaint& paint, const core::Color& color, double opacity,
                   const core::Document& document) {
  const auto rgb = Resolve(color, document);
  if (!rgb) return false;
  paint.setColor4f({static_cast<float>(rgb->r), static_cast<float>(rgb->g),
                    static_cast<float>(rgb->b), static_cast<float>(opacity)});
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
    }
  }
  entry.last_used = frame_;
  return entry;
}

void DocumentRenderer::Impl::Draw(SkCanvas* canvas, const core::Document& document,
                                  const View& view, int width, int height) {
  ++frame_;
  stats = {};
  document_ = &document;

  const auto& bg = settings.pasteboard;
  canvas->clear(SkColor4f{static_cast<float>(bg.r), static_cast<float>(bg.g),
                          static_cast<float>(bg.b), 1.0f});
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
    const SkRect rect = ToSk(board.bounds);
    canvas->drawRect(rect.makeOffset(0, 2 * px), shadow);
    canvas->drawRect(rect, paper);
    canvas->drawRect(rect, border);
  }

  for (const auto& layer : document.layers) DrawLayer(canvas, *layer, visible);
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

  // Object opacity and blending apply to the object as a whole, so it is
  // composited from its own layer.
  const bool isolate = common.opacity < 1.0 || common.blend_mode != core::BlendMode::kNormal;
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
    if (group->clipped && !group->children.empty()) {
      --end;  // The frontmost child is the clip path and is not painted.
      canvas->clipPath(Entry(group->children.back()).path, /*doAntiAlias=*/true);
    }
    for (auto it = group->children.begin(); it != end; ++it) DrawObject(canvas, *it, local);
    canvas->restore();
  } else {
    DrawShape(canvas, *object, entry.path);
  }

  if (isolate) canvas->restore();
}

void DocumentRenderer::Impl::DrawShape(SkCanvas* canvas, const core::Object& object,
                                       const SkPath& path) {
  const core::Appearance& appearance = core::CommonOf(object).appearance;
  // The stack is stored front to back; paint from the back.
  for (auto it = appearance.rbegin(); it != appearance.rend(); ++it) {
    SkPaint paint;
    paint.setAntiAlias(true);
    if (const auto* fill = std::get_if<core::Fill>(&*it)) {
      if (!SetPaintColor(paint, fill->paint, fill->opacity, *document_)) continue;
      paint.setBlendMode(ToSk(fill->blend_mode));
      canvas->drawPath(path, paint);
      continue;
    }
    const auto& stroke = std::get<core::Stroke>(*it);
    if (stroke.width <= 0.0) continue;
    if (!SetPaintColor(paint, stroke.paint, stroke.opacity, *document_)) continue;
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
                                                         int height, const View& view) {
  const SkImageInfo info =
      SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
  sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
  impl_->Draw(surface->getCanvas(), document, view, width, height);
  std::vector<std::uint8_t> pixels(info.computeMinByteSize());
  surface->readPixels(info, pixels.data(), info.minRowBytes(), 0, 0);
  return pixels;
}

DocumentRenderer::Stats DocumentRenderer::last_stats() const { return impl_->stats; }

}  // namespace leinwand::render
