// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/style.h"

#include <algorithm>
#include <map>
#include <utility>
#include <variant>

namespace leinwand::core {

namespace {

template <typename T>
auto FindFront(Appearance& appearance) {
  return std::find_if(appearance.begin(), appearance.end(),
                      [](const AppearanceItem& item) { return std::holds_alternative<T>(item); });
}

// A copy of `object` with `edit` applied to its common fields; groups pass
// appearance edits on to their contents.
ObjectPtr Edited(const ObjectPtr& object, const std::function<void(ObjectCommon&)>& edit,
                 bool into_groups) {
  return std::visit(
      [&](const auto& o) -> ObjectPtr {
        using T = std::decay_t<decltype(o)>;
        T copy = o;
        if constexpr (std::is_same_v<T, GroupObject>) {
          if (into_groups) {
            for (auto& child : copy.children) child = Edited(child, edit, true);
            return MakeObject(std::move(copy));
          }
        }
        edit(copy.common);
        return MakeObject(std::move(copy));
      },
      object->base());
}

Document EditObjects(const Document& document, const IdSet& ids,
                     const std::function<void(ObjectCommon&)>& edit, bool into_groups) {
  std::map<std::string, ObjectPtr> replacements;
  for (const auto& found : FindObjects(document, WithoutNested(document, ids))) {
    replacements[CommonOf(*found.object).id] = Edited(found.object, edit, into_groups);
  }
  return ReplaceObjects(document, replacements);
}

}  // namespace

const Fill* FrontFill(const Appearance& appearance) {
  for (const auto& item : appearance) {
    if (const auto* fill = std::get_if<Fill>(&item)) return fill;
  }
  return nullptr;
}

const Stroke* FrontStroke(const Appearance& appearance) {
  for (const auto& item : appearance) {
    if (const auto* stroke = std::get_if<Stroke>(&item)) return stroke;
  }
  return nullptr;
}

void SetFillPaint(Appearance& appearance, const std::optional<Color>& paint) {
  const auto it = FindFront<Fill>(appearance);
  if (!paint) {
    if (it != appearance.end()) appearance.erase(it);
  } else if (it != appearance.end()) {
    std::get<Fill>(*it).paint = *paint;
  } else {
    appearance.push_back(Fill{*paint});
  }
}

void SetStrokePaint(Appearance& appearance, const std::optional<Color>& paint) {
  const auto it = FindFront<Stroke>(appearance);
  if (!paint) {
    if (it != appearance.end()) appearance.erase(it);
  } else if (it != appearance.end()) {
    std::get<Stroke>(*it).paint = *paint;
  } else {
    appearance.insert(appearance.begin(), Stroke{*paint});
  }
}

void SwapFillAndStroke(Appearance& appearance) {
  const Fill* fill = FrontFill(appearance);
  const Stroke* stroke = FrontStroke(appearance);
  const std::optional<Color> fill_paint = fill ? std::optional{fill->paint} : std::nullopt;
  const std::optional<Color> stroke_paint = stroke ? std::optional{stroke->paint} : std::nullopt;
  SetFillPaint(appearance, stroke_paint);
  SetStrokePaint(appearance, fill_paint);
}

Document EditAppearance(const Document& document, const IdSet& ids,
                        const std::function<void(Appearance&)>& edit) {
  return EditObjects(document, ids, [&](ObjectCommon& common) { edit(common.appearance); }, true);
}

Document SetOpacity(const Document& document, const IdSet& ids, double opacity) {
  const double clamped = std::clamp(opacity, 0.0, 1.0);
  return EditObjects(document, ids, [&](ObjectCommon& common) { common.opacity = clamped; }, false);
}

ProcessColor Tinted(const ProcessColor& color, double tint) {
  return std::visit(
      [&](const auto& c) -> ProcessColor {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, RgbColor>) {
          return RgbColor{1 - tint * (1 - c.r), 1 - tint * (1 - c.g), 1 - tint * (1 - c.b)};
        } else if constexpr (std::is_same_v<T, CmykColor>) {
          return CmykColor{c.c * tint, c.m * tint, c.y * tint, c.k * tint};
        } else {
          return GrayColor{1 - tint * (1 - c.gray)};
        }
      },
      color);
}

Document RemoveSwatch(const Document& document, const std::string& id) {
  const Swatch* swatch = document.FindSwatch(id);
  if (!swatch) return document;
  const ProcessColor full = swatch->color;
  auto plain = [&](const Color& paint) -> std::optional<Color> {
    std::optional<ProcessColor> replaced;
    if (const auto* ref = std::get_if<SwatchRef>(&paint); ref && ref->swatch_id == id) {
      replaced = full;
    } else if (const auto* spot = std::get_if<SpotColor>(&paint); spot && spot->swatch_id == id) {
      replaced = Tinted(full, spot->tint);
    }
    if (!replaced) return std::nullopt;
    return std::visit([](const auto& c) -> Color { return c; }, *replaced);
  };
  Document result = EditAppearance(document, AllObjectIds(document), [&](Appearance& a) {
    for (auto& item : a) {
      std::visit(
          [&](auto& paintable) {
            using T = std::decay_t<decltype(paintable)>;
            if constexpr (!std::is_same_v<T, UnknownAppearanceItem>) {
              if (auto color = plain(paintable.paint)) paintable.paint = *color;
            }
          },
          item);
    }
  });
  std::erase_if(result.swatches, [&](const Swatch& s) { return s.id == id; });
  return result;
}

RgbColor ToRgb(const ProcessColor& color) {
  if (const auto* rgb = std::get_if<RgbColor>(&color)) return *rgb;
  if (const auto* gray = std::get_if<GrayColor>(&color))
    return {gray->gray, gray->gray, gray->gray};
  const auto& cmyk = std::get<CmykColor>(color);
  return {(1 - cmyk.c) * (1 - cmyk.k), (1 - cmyk.m) * (1 - cmyk.k), (1 - cmyk.y) * (1 - cmyk.k)};
}

std::optional<RgbColor> ToRgb(const Color& paint, const Document& document) {
  return std::visit(
      [&](const auto& c) -> std::optional<RgbColor> {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, SpotColor>) {
          const Swatch* swatch = document.FindSwatch(c.swatch_id);
          if (!swatch) return std::nullopt;
          const RgbColor full = ToRgb(swatch->color);
          return ToRgb(ProcessColor{Tinted(full, c.tint)});
        } else if constexpr (std::is_same_v<T, SwatchRef>) {
          const Swatch* swatch = document.FindSwatch(c.swatch_id);
          if (!swatch) return std::nullopt;
          return ToRgb(swatch->color);
        } else {
          return ToRgb(ProcessColor{c});
        }
      },
      paint);
}

}  // namespace leinwand::core
