// SPDX-License-Identifier: GPL-3.0-or-later
// Anchor-level path editing for the pen, direct selection and anchor point
// tools (spec 4.2). Pure functions on PathData.
#pragma once

#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "core/path.h"
#include "core/types.h"
#include "geometry/bezier.h"

namespace leinwand::geometry {

// de Casteljau: the two halves of `c` at parameter t, which together trace
// exactly the same curve.
std::pair<CubicBezier, CubicBezier> Split(const CubicBezier& c, double t);

// The point on a segment nearest to `p`, as a parameter and a distance.
struct NearestOnSegment {
  int segment = -1;
  double t = 0.0;
  double distance = 0.0;
};
std::optional<NearestOnSegment> NearestSegment(const core::PathData& path, core::Point p);

// Adds an anchor on `segment` at `t` without changing the path's shape.
// Returns the new anchor's index.
int InsertAnchor(core::PathData& path, int segment, double t);

// Removes an anchor; its neighbours keep their handles (as Illustrator's
// delete anchor point does). A closed path left with fewer than two anchors
// opens.
void RemoveAnchor(core::PathData& path, int index);

// Corner: both handles retract. Smooth: handles are pulled out along the
// direction between the neighbours, a third of the way to each.
void MakeCorner(core::PathData& path, int index);
void MakeSmooth(core::PathData& path, int index);

// Moves an anchor (with its handles) to `position`.
void MoveAnchor(core::PathData& path, int index, core::Point position);

enum class Side { kIn, kOut };

// Moves one handle to the absolute point `to`. A smooth anchor keeps the
// other handle on the same line (keeping its length); a symmetric one
// mirrors it. With `split`, the anchor becomes a corner and the other
// handle stays put (Alt in Illustrator).
void MoveHandle(core::PathData& path, int index, Side side, core::Point to, bool split = false);

// Bends segment `segment` so the curve passes through `to` at parameter `t`
// by moving both of its handles (dragging a segment with direct selection).
void DragSegment(core::PathData& path, int segment, double t, core::Point to);

// Cuts the path at an anchor. A closed path opens there (one path); an open
// path splits into two. The anchor is duplicated so both ends keep it.
std::vector<core::PathData> CutAt(const core::PathData& path, int index);

// Deletes anchors together with the segments touching them, as Delete does
// in Illustrator with direct selection: what remains falls apart into the
// runs of unselected anchors, in path order. Runs of a single anchor are
// dropped (no stray points). A closed path with nothing deleted stays whole.
std::vector<core::PathData> DeleteAnchors(const core::PathData& path, const std::set<int>& indices);

// Joins two open paths: the end `a_end` of `a` (true: last anchor, false:
// first) to the end `b_end` of `b`. Coincident endpoints merge into one
// anchor. The result runs from a's free end through to b's free end.
core::PathData Join(const core::PathData& a, bool a_end, const core::PathData& b, bool b_end);

// The same path traversed backwards (handles swap sides).
core::PathData Reversed(const core::PathData& path);

}  // namespace leinwand::geometry
