// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/transform.h"

#include <variant>

namespace leinwand::core {

PathData Transformed(const PathData& path, const Matrix& matrix) {
  PathData result = path;
  for (auto& anchor : result.anchors) {
    anchor.position = matrix.Map(anchor.position);
    anchor.handle_in = matrix.MapVector(anchor.handle_in);
    anchor.handle_out = matrix.MapVector(anchor.handle_out);
  }
  return result;
}

ObjectPtr Transformed(const ObjectPtr& object, const Matrix& matrix) {
  if (matrix.IsIdentity()) return object;
  return std::visit(
      [&](const auto& o) -> ObjectPtr {
        using T = std::decay_t<decltype(o)>;
        T copy = o;
        if constexpr (std::is_same_v<T, PathObject>) {
          copy.path = Transformed(o.path, matrix);
        } else if constexpr (std::is_same_v<T, CompoundPathObject>) {
          for (auto& subpath : copy.subpaths) subpath = Transformed(subpath, matrix);
        } else {
          copy.transform = matrix * o.transform;
        }
        return MakeObject(std::move(copy));
      },
      object->base());
}

}  // namespace leinwand::core
