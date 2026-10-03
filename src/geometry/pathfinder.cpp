// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/pathfinder.h"

#include <cmath>
#include <cstddef>
#include <map>

namespace leinwand::geometry {

namespace {

using core::Anchor;
using core::PathData;
using core::Point;

using Pieces = std::vector<PathfinderPiece>;

// The union of inputs[begin, end); an empty region when the range is empty.
std::optional<Region> UnionOf(const PathOpsEngine& engine, const std::vector<Region>& inputs,
                              size_t begin, size_t end) {
  Region result;
  for (size_t i = begin; i < end; ++i) {
    auto next = engine.Apply(result, inputs[i], BooleanOp::kUnion);
    if (!next) return std::nullopt;
    result = std::move(*next);
  }
  return result;
}

std::optional<Region> Fold(const PathOpsEngine& engine, const std::vector<Region>& inputs,
                           BooleanOp op) {
  Region result = inputs[0];
  for (size_t i = 1; i < inputs.size(); ++i) {
    auto next = engine.Apply(result, inputs[i], op);
    if (!next) return std::nullopt;
    result = std::move(*next);
  }
  return result;
}

Pieces One(Region region, int source) {
  if (region.empty()) return {};
  return {{std::move(region), source}};
}

// Every region the outlines enclose, each taking the appearance of the
// frontmost input covering it.
std::optional<Pieces> Divide(const PathOpsEngine& engine, const std::vector<Region>& inputs) {
  Pieces pieces;
  Region behind;  // The union of the inputs handled so far.
  for (size_t i = 0; i < inputs.size(); ++i) {
    Pieces next;
    for (PathfinderPiece& piece : pieces) {
      auto inside = engine.Apply(piece.region, inputs[i], BooleanOp::kIntersect);
      auto outside = engine.Apply(piece.region, inputs[i], BooleanOp::kDifference);
      if (!inside || !outside) return std::nullopt;
      if (!inside->empty()) next.push_back({std::move(*inside), int(i)});
      if (!outside->empty()) next.push_back({std::move(*outside), piece.source});
    }
    auto fresh = engine.Apply(inputs[i], behind, BooleanOp::kDifference);
    auto grown = engine.Apply(behind, inputs[i], BooleanOp::kUnion);
    if (!fresh || !grown) return std::nullopt;
    if (!fresh->empty()) next.push_back({std::move(*fresh), int(i)});
    behind = std::move(*grown);
    pieces = std::move(next);
  }
  // Disconnected parts of a piece become pieces of their own.
  Pieces split;
  for (const PathfinderPiece& piece : pieces) {
    for (Region& part : SplitIntoPieces(piece.region))
      split.push_back({std::move(part), piece.source});
  }
  return split;
}

// What is visible of each input: it minus everything in front of it.
std::optional<Pieces> Trim(const PathOpsEngine& engine, const std::vector<Region>& inputs) {
  Pieces pieces;
  for (size_t i = 0; i < inputs.size(); ++i) {
    auto front = UnionOf(engine, inputs, i + 1, inputs.size());
    if (!front) return std::nullopt;
    auto visible = engine.Apply(inputs[i], *front, BooleanOp::kDifference);
    if (!visible) return std::nullopt;
    if (!visible->empty()) pieces.push_back({std::move(*visible), int(i)});
  }
  return pieces;
}

std::optional<Pieces> Merge(const PathOpsEngine& engine, const std::vector<Region>& inputs,
                            const std::vector<int>& fill_keys) {
  auto trimmed = Trim(engine, inputs);
  if (!trimmed) return std::nullopt;
  // Pieces of the same fill join; the merged piece sits at the frontmost
  // of them, as the last one listed.
  std::map<int, size_t> first_of_key;
  Pieces merged;
  for (PathfinderPiece& piece : *trimmed) {
    const int key =
        size_t(piece.source) < fill_keys.size() ? fill_keys[size_t(piece.source)] : piece.source;
    auto found = first_of_key.find(key);
    if (found == first_of_key.end()) {
      first_of_key[key] = merged.size();
      merged.push_back(std::move(piece));
      continue;
    }
    PathfinderPiece& into = merged[found->second];
    auto joined = engine.Apply(into.region, piece.region, BooleanOp::kUnion);
    if (!joined) return std::nullopt;
    into.region = std::move(*joined);
    into.source = piece.source;
  }
  return merged;
}

// What is visible of each input behind the frontmost, inside the frontmost.
std::optional<Pieces> Crop(const PathOpsEngine& engine, const std::vector<Region>& inputs) {
  const size_t last = inputs.size() - 1;
  Pieces pieces;
  for (size_t i = 0; i < last; ++i) {
    auto front = UnionOf(engine, inputs, i + 1, last);
    if (!front) return std::nullopt;
    auto visible = engine.Apply(inputs[i], *front, BooleanOp::kDifference);
    if (!visible) return std::nullopt;
    auto inside = engine.Apply(*visible, inputs[last], BooleanOp::kIntersect);
    if (!inside) return std::nullopt;
    if (!inside->empty()) pieces.push_back({std::move(*inside), int(i)});
  }
  return pieces;
}

bool Near(Point a, Point b) { return std::abs(a.x - b.x) < 1e-4 && std::abs(a.y - b.y) < 1e-4; }

// Cuts a closed contour into open paths at the anchors in `cuts`. Without
// cuts the contour stays closed.
std::vector<PathData> CutAt(const PathData& contour, const std::vector<bool>& cuts) {
  std::vector<size_t> at;
  for (size_t i = 0; i < cuts.size(); ++i) {
    if (cuts[i]) at.push_back(i);
  }
  if (at.empty()) return {contour};
  const size_t n = contour.anchors.size();
  std::vector<PathData> parts;
  for (size_t k = 0; k < at.size(); ++k) {
    const size_t from = at[k], to = at[(k + 1) % at.size()];
    PathData part;
    for (size_t i = from;; i = (i + 1) % n) {
      part.anchors.push_back(contour.anchors[i]);
      if (i == to && part.anchors.size() > 1) break;
    }
    part.anchors.front().handle_in = {};
    part.anchors.back().handle_out = {};
    parts.push_back(std::move(part));
  }
  return parts;
}

// The divided regions' edges, cut where regions meet (at anchors that more
// than one piece has).
std::optional<Pieces> Outline(const PathOpsEngine& engine, const std::vector<Region>& inputs) {
  auto divided = Divide(engine, inputs);
  if (!divided) return std::nullopt;
  Pieces pieces;
  for (size_t p = 0; p < divided->size(); ++p) {
    PathfinderPiece edges{{{}, (*divided)[p].region.fill_rule}, (*divided)[p].source};
    for (const PathData& contour : (*divided)[p].region.subpaths) {
      std::vector<bool> cuts(contour.anchors.size(), false);
      for (size_t i = 0; i < contour.anchors.size(); ++i) {
        for (size_t q = 0; q < divided->size() && !cuts[i]; ++q) {
          if (q == p) continue;
          for (const PathData& other : (*divided)[q].region.subpaths) {
            for (const Anchor& a : other.anchors) {
              if (Near(a.position, contour.anchors[i].position)) cuts[i] = true;
            }
          }
        }
      }
      for (PathData& part : CutAt(contour, cuts)) edges.region.subpaths.push_back(std::move(part));
    }
    pieces.push_back(std::move(edges));
  }
  return pieces;
}

}  // namespace

std::optional<std::vector<PathfinderPiece>> RunPathfinder(const PathOpsEngine& engine,
                                                          Pathfinder operation,
                                                          const std::vector<Region>& inputs,
                                                          const std::vector<int>& fill_keys) {
  if (inputs.size() < 2) return std::nullopt;
  const int back = 0, front = int(inputs.size()) - 1;
  switch (operation) {
    case Pathfinder::kUnite: {
      auto r = Fold(engine, inputs, BooleanOp::kUnion);
      if (!r) return std::nullopt;
      return One(std::move(*r), front);
    }
    case Pathfinder::kIntersect: {
      auto r = Fold(engine, inputs, BooleanOp::kIntersect);
      if (!r) return std::nullopt;
      return One(std::move(*r), front);
    }
    case Pathfinder::kExclude: {
      auto r = Fold(engine, inputs, BooleanOp::kExclude);
      if (!r) return std::nullopt;
      return One(std::move(*r), front);
    }
    case Pathfinder::kMinusFront: {
      auto others = UnionOf(engine, inputs, 1, inputs.size());
      if (!others) return std::nullopt;
      auto r = engine.Apply(inputs[0], *others, BooleanOp::kDifference);
      if (!r) return std::nullopt;
      return One(std::move(*r), back);
    }
    case Pathfinder::kMinusBack: {
      auto others = UnionOf(engine, inputs, 0, inputs.size() - 1);
      if (!others) return std::nullopt;
      auto r = engine.Apply(inputs.back(), *others, BooleanOp::kDifference);
      if (!r) return std::nullopt;
      return One(std::move(*r), front);
    }
    case Pathfinder::kDivide:
      return Divide(engine, inputs);
    case Pathfinder::kTrim:
      return Trim(engine, inputs);
    case Pathfinder::kMerge:
      return Merge(engine, inputs, fill_keys);
    case Pathfinder::kCrop:
      return Crop(engine, inputs);
    case Pathfinder::kOutline:
      return Outline(engine, inputs);
  }
  return std::nullopt;
}

bool DropsStrokes(Pathfinder operation) {
  return operation == Pathfinder::kTrim || operation == Pathfinder::kMerge ||
         operation == Pathfinder::kCrop;
}

}  // namespace leinwand::geometry
