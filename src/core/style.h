// SPDX-License-Identifier: GPL-3.0-or-later
// Fill and stroke editing for the Color, Swatches and Stroke panels and the
// eyedropper (spec 7.2). Until the Appearance panel (phase 3), the panels
// edit the frontmost fill and the frontmost stroke of the stack, as
// Illustrator does.
#pragma once

#include <functional>
#include <optional>

#include "core/appearance.h"
#include "core/document.h"
#include "core/edit.h"

namespace leinwand::core {

const Fill* FrontFill(const Appearance& appearance);
const Stroke* FrontStroke(const Appearance& appearance);

// nullopt is "none": the front fill (stroke) is removed. A color on an
// appearance without one adds it: a fill at the back, a stroke at the front
// (1 pt, Illustrator's defaults otherwise).
void SetFillPaint(Appearance& appearance, const std::optional<Color>& paint);
void SetStrokePaint(Appearance& appearance, const std::optional<Color>& paint);

// Shift+X: the front fill takes the stroke's paint and the other way round;
// "none" swaps too.
void SwapFillAndStroke(Appearance& appearance);

// Applies `edit` to each listed object; a group passes it on to everything
// inside it, as when Illustrator colors a selected group.
Document EditAppearance(const Document& document, const IdSet& ids,
                        const std::function<void(Appearance&)>& edit);

// Object opacity (control bar, Transparency panel).
Document SetOpacity(const Document& document, const IdSet& ids, double opacity);

// Removes a swatch. Colors that use it become the plain color they showed
// (a spot tint becomes the tinted process color), as in Illustrator.
Document RemoveSwatch(const Document& document, const std::string& id);

// A process color at `tint` (0..1) of full strength, mixed with paper white.
ProcessColor Tinted(const ProcessColor& color, double tint);

// The color shown for `paint`, in sRGB: swatch references are resolved (a
// spot tint mixes with paper white); CMYK and gray use the naive conversion
// until color management (phase 4). Null when the swatch is missing.
std::optional<RgbColor> ToRgb(const Color& paint, const Document& document);
RgbColor ToRgb(const ProcessColor& color);

}  // namespace leinwand::core
