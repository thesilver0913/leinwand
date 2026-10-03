// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/marks.h"

namespace leinwand::core {

namespace {

PathData Line(Point a, Point b) {
  PathData path;
  path.anchors = {{a}, {b}};
  return path;
}

}  // namespace

double TrimMarkReach(double bleed, double length) { return bleed + length; }

std::vector<PathData> TrimMarks(const Rect& trim, double bleed, TrimMarkStyle style,
                                double length) {
  std::vector<PathData> marks;
  const Rect b{trim.left - bleed, trim.top - bleed, trim.right + bleed, trim.bottom + bleed};
  // Each corner: sx, sy point outwards; (tx, ty) is the trim corner and
  // (bx, by) the bleed corner.
  struct Corner {
    double tx, ty, bx, by, sx, sy;
  };
  const Corner corners[] = {{trim.left, trim.top, b.left, b.top, -1, -1},
                            {trim.right, trim.top, b.right, b.top, 1, -1},
                            {trim.right, trim.bottom, b.right, b.bottom, 1, 1},
                            {trim.left, trim.bottom, b.left, b.bottom, -1, 1}};
  for (const Corner& c : corners) {
    if (style == TrimMarkStyle::kJapanese) {
      // The trim lines start at the bleed; the bleed lines run on to the
      // trim, so that the two meet in an L at the bleed corner.
      marks.push_back(Line({c.bx + c.sx * length, c.ty}, {c.bx, c.ty}));
      marks.push_back(Line({c.bx + c.sx * length, c.by}, {c.tx, c.by}));
      marks.push_back(Line({c.tx, c.by + c.sy * length}, {c.tx, c.by}));
      marks.push_back(Line({c.bx, c.by + c.sy * length}, {c.bx, c.ty}));
    } else {
      // One line on each trim edge, set off by the bleed.
      marks.push_back(Line({c.bx + c.sx * length, c.ty}, {c.bx, c.ty}));
      marks.push_back(Line({c.tx, c.by + c.sy * length}, {c.tx, c.by}));
    }
  }
  if (style == TrimMarkStyle::kJapanese) {
    // Centre marks: a cross outside the middle of each side.
    const double cx = (trim.left + trim.right) / 2, cy = (trim.top + trim.bottom) / 2;
    const double half = length / 2;
    marks.push_back(Line({cx, b.top - length}, {cx, b.top}));
    marks.push_back(Line({cx - half, b.top - half}, {cx + half, b.top - half}));
    marks.push_back(Line({cx, b.bottom + length}, {cx, b.bottom}));
    marks.push_back(Line({cx - half, b.bottom + half}, {cx + half, b.bottom + half}));
    marks.push_back(Line({b.left - length, cy}, {b.left, cy}));
    marks.push_back(Line({b.left - half, cy - half}, {b.left - half, cy + half}));
    marks.push_back(Line({b.right + length, cy}, {b.right, cy}));
    marks.push_back(Line({b.right + half, cy - half}, {b.right + half, cy + half}));
  }
  return marks;
}

}  // namespace leinwand::core
