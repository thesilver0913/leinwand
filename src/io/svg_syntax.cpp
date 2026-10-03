// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/svg_syntax.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <utility>

#include "core/number.h"

namespace leinwand::io::svg {

namespace {

using core::Anchor;
using core::Matrix;
using core::PathData;
using core::Point;

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && IsSpace(s.front())) s.remove_prefix(1);
  while (!s.empty() && IsSpace(s.back())) s.remove_suffix(1);
  return s;
}

// Reads numbers the way SVG path data and lists allow: separated by spaces
// and/or one comma, signs and dots starting new numbers ("1-2.5.5").
class NumberScanner {
 public:
  explicit NumberScanner(std::string_view text) : text_(text) {}

  void SkipSeparators() {
    while (pos_ < text_.size() && IsSpace(text_[pos_])) ++pos_;
    if (pos_ < text_.size() && text_[pos_] == ',') ++pos_;
    while (pos_ < text_.size() && IsSpace(text_[pos_])) ++pos_;
  }

  std::optional<double> Number() {
    SkipSeparators();
    const size_t start = pos_;
    if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
    bool digits = false;
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
      digits = true;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
        ++pos_;
        digits = true;
      }
    }
    if (!digits) {
      pos_ = start;
      return std::nullopt;
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      size_t e = pos_ + 1;
      if (e < text_.size() && (text_[e] == '+' || text_[e] == '-')) ++e;
      if (e < text_.size() && std::isdigit(static_cast<unsigned char>(text_[e]))) {
        pos_ = e;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
      }
    }
    double value = 0;
    std::string token(text_.substr(start, pos_ - start));
    if (!token.empty() && token[0] == '+') token.erase(0, 1);
    if (token.empty() || core::ParseDouble(token, &value) != token.size()) {
      pos_ = start;
      return std::nullopt;
    }
    return value;
  }

  // An arc flag: a single 0 or 1, which may run into the next number.
  std::optional<bool> Flag() {
    SkipSeparators();
    if (pos_ < text_.size() && (text_[pos_] == '0' || text_[pos_] == '1')) {
      return text_[pos_++] == '1';
    }
    return std::nullopt;
  }

  bool AtEnd() {
    while (pos_ < text_.size() && IsSpace(text_[pos_])) ++pos_;
    return pos_ >= text_.size();
  }
  char Peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
  void Advance() { ++pos_; }
  size_t position() const { return pos_; }

 private:
  std::string_view text_;
  size_t pos_ = 0;
};

class PathBuilder {
 public:
  void MoveTo(Point p) {
    Flush();
    current_ = PathData{};
    current_.anchors.push_back({p});
    open_ = true;
    start_ = point_ = p;
  }
  void LineTo(Point p) {
    EnsureStarted();
    current_.anchors.push_back({p});
    point_ = p;
  }
  void CubicTo(Point c1, Point c2, Point p) {
    EnsureStarted();
    Anchor& last = current_.anchors.back();
    last.handle_out = c1 - last.position;
    Anchor next{p};
    next.handle_in = c2 - p;
    current_.anchors.push_back(next);
    point_ = p;
  }
  void Close() {
    if (!open_) return;
    current_.closed = true;
    auto& anchors = current_.anchors;
    // A closing segment drawn explicitly to the start merges into it.
    if (anchors.size() > 1) {
      const Point d = anchors.back().position - anchors.front().position;
      if (std::abs(d.x) < 1e-9 && std::abs(d.y) < 1e-9) {
        anchors.front().handle_in = anchors.back().handle_in;
        anchors.pop_back();
      }
    }
    Flush();
    point_ = start_;
  }
  Point point() const { return point_; }
  std::vector<PathData> Finish() {
    Flush();
    return std::move(paths_);
  }

 private:
  // After Z, drawing continues from the start of the closed subpath.
  void EnsureStarted() {
    if (!open_) MoveTo(point_);
  }
  void Flush() {
    if (!open_) return;
    for (Anchor& a : current_.anchors) {
      // Opposite, collinear handles: a smooth point.
      const Point i = a.handle_in, o = a.handle_out;
      const double cross = i.x * o.y - i.y * o.x;
      const double dot = i.x * o.x + i.y * o.y;
      const double li = std::hypot(i.x, i.y), lo = std::hypot(o.x, o.y);
      if (li > 1e-9 && lo > 1e-9 && dot < 0 && std::abs(cross) < 1e-6 * li * lo) {
        a.kind = core::AnchorKind::kSmooth;
      }
    }
    paths_.push_back(std::move(current_));
    current_ = PathData{};
    open_ = false;
  }

  std::vector<PathData> paths_;
  PathData current_;
  bool open_ = false;
  Point start_, point_;
};

// Endpoint arc to cubic Béziers (SVG 1.1 appendix F.6).
void ArcTo(PathBuilder& b, Point from, double rx, double ry, double angle_deg, bool large,
           bool sweep, Point to) {
  if (from == to) return;
  rx = std::abs(rx);
  ry = std::abs(ry);
  if (rx < 1e-12 || ry < 1e-12) {
    b.LineTo(to);
    return;
  }
  const double phi = angle_deg * std::numbers::pi / 180.0;
  const double cphi = std::cos(phi), sphi = std::sin(phi);
  const double dx = (from.x - to.x) / 2, dy = (from.y - to.y) / 2;
  const double x1 = cphi * dx + sphi * dy, y1 = -sphi * dx + cphi * dy;
  const double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
  if (lambda > 1) {
    rx *= std::sqrt(lambda);
    ry *= std::sqrt(lambda);
  }
  const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  double coef = den == 0 ? 0 : std::sqrt(std::max(0.0, num / den));
  if (large == sweep) coef = -coef;
  const double cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
  const double cx = cphi * cx1 - sphi * cy1 + (from.x + to.x) / 2;
  const double cy = sphi * cx1 + cphi * cy1 + (from.y + to.y) / 2;
  auto angle = [](double ux, double uy, double vx, double vy) {
    return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  };
  const double theta = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
  double delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
  if (!sweep && delta > 0) delta -= 2 * std::numbers::pi;
  if (sweep && delta < 0) delta += 2 * std::numbers::pi;

  const int segments =
      std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (std::numbers::pi / 2) - 1e-9)));
  const double step = delta / segments;
  const double k = 4.0 / 3.0 * std::tan(step / 4);
  auto map = [&](double ux, double uy) {
    return Point{cx + rx * ux * cphi - ry * uy * sphi, cy + rx * ux * sphi + ry * uy * cphi};
  };
  double t = theta;
  for (int i = 0; i < segments; ++i) {
    const double t2 = t + step;
    const double c0 = std::cos(t), s0 = std::sin(t), c1 = std::cos(t2), s1 = std::sin(t2);
    const Point p1 = map(c0 - k * s0, s0 + k * c0);
    const Point p2 = map(c1 + k * s1, s1 - k * c1);
    const Point p3 = i == segments - 1 ? to : map(c1, s1);
    b.CubicTo(p1, p2, p3);
    t = t2;
  }
}

}  // namespace

std::vector<PathData> ParsePathData(std::string_view d) {
  NumberScanner s(d);
  PathBuilder b;
  char command = 0;
  Point last_control;     // For S and T.
  char last_command = 0;  // The previous command, upper case.
  bool started = false;

  while (!s.AtEnd()) {
    const char c = s.Peek();
    if (std::isalpha(static_cast<unsigned char>(c))) {
      command = c;
      s.Advance();
    } else if (command == 0) {
      break;  // Numbers before any command.
    }
    const bool relative = std::islower(static_cast<unsigned char>(command));
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
    if (!started && upper != 'M') break;
    const Point origin = relative ? b.point() : Point{};
    // The next coordinate pair (read in order: x, then y).
    auto next_point = [&]() -> std::optional<Point> {
      const auto x = s.Number();
      if (!x) return std::nullopt;
      const auto y = s.Number();
      if (!y) return std::nullopt;
      return Point{origin.x + *x, origin.y + *y};
    };
    bool ok = true;
    switch (upper) {
      case 'M': {
        const auto p = next_point();
        if (!p) {
          ok = false;
          break;
        }
        b.MoveTo(*p);
        started = true;
        // Further pairs are line-tos.
        command = relative ? 'l' : 'L';
        last_control = *p;
        break;
      }
      case 'L': {
        const auto p = next_point();
        if (!p) {
          ok = false;
          break;
        }
        b.LineTo(*p);
        break;
      }
      case 'H': {
        const auto x = s.Number();
        if (!x) {
          ok = false;
          break;
        }
        b.LineTo({relative ? b.point().x + *x : *x, b.point().y});
        break;
      }
      case 'V': {
        const auto y = s.Number();
        if (!y) {
          ok = false;
          break;
        }
        b.LineTo({b.point().x, relative ? b.point().y + *y : *y});
        break;
      }
      case 'C': {
        const auto c1 = next_point();
        const auto c2 = next_point();
        const auto p = next_point();
        if (!c1 || !c2 || !p) {
          ok = false;
          break;
        }
        b.CubicTo(*c1, *c2, *p);
        last_control = *c2;
        break;
      }
      case 'S': {
        const Point from = b.point();
        const Point c1 =
            last_command == 'C' || last_command == 'S' ? from + (from - last_control) : from;
        const auto c2 = next_point();
        const auto p = next_point();
        if (!c2 || !p) {
          ok = false;
          break;
        }
        b.CubicTo(c1, *c2, *p);
        last_control = *c2;
        break;
      }
      case 'Q':
      case 'T': {
        const Point from = b.point();
        std::optional<Point> q;
        if (upper == 'Q') {
          q = next_point();
        } else {
          q = last_command == 'Q' || last_command == 'T' ? from + (from - last_control) : from;
        }
        const auto p = next_point();
        if (!q || !p) {
          ok = false;
          break;
        }
        // Quadratic to cubic: control points two thirds of the way.
        b.CubicTo(from + (*q - from) * (2.0 / 3.0), *p + (*q - *p) * (2.0 / 3.0), *p);
        last_control = *q;
        break;
      }
      case 'A': {
        // One at a time: the order of reads matters.
        const auto rx = s.Number();
        const auto ry = s.Number();
        const auto rot = s.Number();
        const auto large = s.Flag();
        const auto sweep = s.Flag();
        const auto p = next_point();
        if (!rx || !ry || !rot || !large || !sweep || !p) {
          ok = false;
          break;
        }
        ArcTo(b, b.point(), *rx, *ry, *rot, *large, *sweep, *p);
        break;
      }
      case 'Z':
        b.Close();
        break;
      default:
        ok = false;
        break;
    }
    if (!ok) break;
    last_command = upper == 'M' ? 'L' : upper;
    if (upper == 'Z') {
      // Z takes no numbers; a following number would be an error.
      command = 0;
      started = true;
    }
  }
  return b.Finish();
}

std::optional<Matrix> ParseTransform(std::string_view text) {
  Matrix result;
  size_t pos = 0;
  while (true) {
    while (pos < text.size() && (IsSpace(text[pos]) || text[pos] == ',')) ++pos;
    if (pos >= text.size()) break;
    const size_t name_start = pos;
    while (pos < text.size() && std::isalpha(static_cast<unsigned char>(text[pos]))) ++pos;
    const std::string name(text.substr(name_start, pos - name_start));
    while (pos < text.size() && IsSpace(text[pos])) ++pos;
    if (pos >= text.size() || text[pos] != '(') return std::nullopt;
    const size_t close = text.find(')', pos);
    if (close == std::string_view::npos) return std::nullopt;
    const std::vector<double> v = ParseNumberList(text.substr(pos + 1, close - pos - 1));
    pos = close + 1;
    constexpr double kDeg = std::numbers::pi / 180.0;
    Matrix m;
    if (name == "matrix" && v.size() == 6) {
      m = {v[0], v[1], v[2], v[3], v[4], v[5]};
    } else if (name == "translate" && (v.size() == 1 || v.size() == 2)) {
      m = Matrix::Translate(v[0], v.size() == 2 ? v[1] : 0);
    } else if (name == "scale" && (v.size() == 1 || v.size() == 2)) {
      m = Matrix::Scale(v[0], v.size() == 2 ? v[1] : v[0]);
    } else if (name == "rotate" && (v.size() == 1 || v.size() == 3)) {
      m = Matrix::Rotate(v[0] * kDeg);
      if (v.size() == 3) m = Matrix::Translate(v[1], v[2]) * m * Matrix::Translate(-v[1], -v[2]);
    } else if (name == "skewX" && v.size() == 1) {
      m = {1, 0, std::tan(v[0] * kDeg), 1, 0, 0};
    } else if (name == "skewY" && v.size() == 1) {
      m = {1, std::tan(v[0] * kDeg), 0, 1, 0, 0};
    } else {
      return std::nullopt;
    }
    result = result * m;
  }
  return result;
}

namespace {

struct NamedColor {
  const char* name;
  unsigned rgb;
};

// CSS Color Module Level 3 extended color keywords.
constexpr NamedColor kNamedColors[] = {
    {"aliceblue", 0xf0f8ff},
    {"antiquewhite", 0xfaebd7},
    {"aqua", 0x00ffff},
    {"aquamarine", 0x7fffd4},
    {"azure", 0xf0ffff},
    {"beige", 0xf5f5dc},
    {"bisque", 0xffe4c4},
    {"black", 0x000000},
    {"blanchedalmond", 0xffebcd},
    {"blue", 0x0000ff},
    {"blueviolet", 0x8a2be2},
    {"brown", 0xa52a2a},
    {"burlywood", 0xdeb887},
    {"cadetblue", 0x5f9ea0},
    {"chartreuse", 0x7fff00},
    {"chocolate", 0xd2691e},
    {"coral", 0xff7f50},
    {"cornflowerblue", 0x6495ed},
    {"cornsilk", 0xfff8dc},
    {"crimson", 0xdc143c},
    {"cyan", 0x00ffff},
    {"darkblue", 0x00008b},
    {"darkcyan", 0x008b8b},
    {"darkgoldenrod", 0xb8860b},
    {"darkgray", 0xa9a9a9},
    {"darkgreen", 0x006400},
    {"darkgrey", 0xa9a9a9},
    {"darkkhaki", 0xbdb76b},
    {"darkmagenta", 0x8b008b},
    {"darkolivegreen", 0x556b2f},
    {"darkorange", 0xff8c00},
    {"darkorchid", 0x9932cc},
    {"darkred", 0x8b0000},
    {"darksalmon", 0xe9967a},
    {"darkseagreen", 0x8fbc8f},
    {"darkslateblue", 0x483d8b},
    {"darkslategray", 0x2f4f4f},
    {"darkslategrey", 0x2f4f4f},
    {"darkturquoise", 0x00ced1},
    {"darkviolet", 0x9400d3},
    {"deeppink", 0xff1493},
    {"deepskyblue", 0x00bfff},
    {"dimgray", 0x696969},
    {"dimgrey", 0x696969},
    {"dodgerblue", 0x1e90ff},
    {"firebrick", 0xb22222},
    {"floralwhite", 0xfffaf0},
    {"forestgreen", 0x228b22},
    {"fuchsia", 0xff00ff},
    {"gainsboro", 0xdcdcdc},
    {"ghostwhite", 0xf8f8ff},
    {"gold", 0xffd700},
    {"goldenrod", 0xdaa520},
    {"gray", 0x808080},
    {"grey", 0x808080},
    {"green", 0x008000},
    {"greenyellow", 0xadff2f},
    {"honeydew", 0xf0fff0},
    {"hotpink", 0xff69b4},
    {"indianred", 0xcd5c5c},
    {"indigo", 0x4b0082},
    {"ivory", 0xfffff0},
    {"khaki", 0xf0e68c},
    {"lavender", 0xe6e6fa},
    {"lavenderblush", 0xfff0f5},
    {"lawngreen", 0x7cfc00},
    {"lemonchiffon", 0xfffacd},
    {"lightblue", 0xadd8e6},
    {"lightcoral", 0xf08080},
    {"lightcyan", 0xe0ffff},
    {"lightgoldenrodyellow", 0xfafad2},
    {"lightgray", 0xd3d3d3},
    {"lightgreen", 0x90ee90},
    {"lightgrey", 0xd3d3d3},
    {"lightpink", 0xffb6c1},
    {"lightsalmon", 0xffa07a},
    {"lightseagreen", 0x20b2aa},
    {"lightskyblue", 0x87cefa},
    {"lightslategray", 0x778899},
    {"lightslategrey", 0x778899},
    {"lightsteelblue", 0xb0c4de},
    {"lightyellow", 0xffffe0},
    {"lime", 0x00ff00},
    {"limegreen", 0x32cd32},
    {"linen", 0xfaf0e6},
    {"magenta", 0xff00ff},
    {"maroon", 0x800000},
    {"mediumaquamarine", 0x66cdaa},
    {"mediumblue", 0x0000cd},
    {"mediumorchid", 0xba55d3},
    {"mediumpurple", 0x9370db},
    {"mediumseagreen", 0x3cb371},
    {"mediumslateblue", 0x7b68ee},
    {"mediumspringgreen", 0x00fa9a},
    {"mediumturquoise", 0x48d1cc},
    {"mediumvioletred", 0xc71585},
    {"midnightblue", 0x191970},
    {"mintcream", 0xf5fffa},
    {"mistyrose", 0xffe4e1},
    {"moccasin", 0xffe4b5},
    {"navajowhite", 0xffdead},
    {"navy", 0x000080},
    {"oldlace", 0xfdf5e6},
    {"olive", 0x808000},
    {"olivedrab", 0x6b8e23},
    {"orange", 0xffa500},
    {"orangered", 0xff4500},
    {"orchid", 0xda70d6},
    {"palegoldenrod", 0xeee8aa},
    {"palegreen", 0x98fb98},
    {"paleturquoise", 0xafeeee},
    {"palevioletred", 0xdb7093},
    {"papayawhip", 0xffefd5},
    {"peachpuff", 0xffdab9},
    {"peru", 0xcd853f},
    {"pink", 0xffc0cb},
    {"plum", 0xdda0dd},
    {"powderblue", 0xb0e0e6},
    {"purple", 0x800080},
    {"rebeccapurple", 0x663399},
    {"red", 0xff0000},
    {"rosybrown", 0xbc8f8f},
    {"royalblue", 0x4169e1},
    {"saddlebrown", 0x8b4513},
    {"salmon", 0xfa8072},
    {"sandybrown", 0xf4a460},
    {"seagreen", 0x2e8b57},
    {"seashell", 0xfff5ee},
    {"sienna", 0xa0522d},
    {"silver", 0xc0c0c0},
    {"skyblue", 0x87ceeb},
    {"slateblue", 0x6a5acd},
    {"slategray", 0x708090},
    {"slategrey", 0x708090},
    {"snow", 0xfffafa},
    {"springgreen", 0x00ff7f},
    {"steelblue", 0x4682b4},
    {"tan", 0xd2b48c},
    {"teal", 0x008080},
    {"thistle", 0xd8bfd8},
    {"tomato", 0xff6347},
    {"turquoise", 0x40e0d0},
    {"violet", 0xee82ee},
    {"wheat", 0xf5deb3},
    {"white", 0xffffff},
    {"whitesmoke", 0xf5f5f5},
    {"yellow", 0xffff00},
    {"yellowgreen", 0x9acd32},
};

core::RgbColor FromHex(unsigned rgb) {
  return {((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0};
}

}  // namespace

std::optional<core::RgbColor> ParseColor(std::string_view text, double* alpha) {
  const std::string t = Lower(Trim(text));
  if (t.empty()) return std::nullopt;
  if (t[0] == '#') {
    std::string hex = t.substr(1);
    if (!std::all_of(hex.begin(), hex.end(),
                     [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
      return std::nullopt;
    }
    if (hex.size() == 3 || hex.size() == 4) {
      std::string full;
      for (char c : hex) full += std::string(2, c);
      hex = full;
    }
    if (hex.size() != 6 && hex.size() != 8) return std::nullopt;
    const unsigned value = static_cast<unsigned>(std::stoul(hex.substr(0, 6), nullptr, 16));
    if (hex.size() == 8 && alpha) *alpha = std::stoul(hex.substr(6), nullptr, 16) / 255.0;
    return FromHex(value);
  }
  if (t.rfind("rgb", 0) == 0) {
    const size_t open = t.find('('), close = t.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open)
      return std::nullopt;
    std::string inner = t.substr(open + 1, close - open - 1);
    std::replace(inner.begin(), inner.end(), '/', ' ');
    std::vector<std::string> parts;
    std::string part;
    for (char c : inner) {
      if (c == ',' || IsSpace(c)) {
        if (!part.empty()) parts.push_back(part);
        part.clear();
      } else {
        part += c;
      }
    }
    if (!part.empty()) parts.push_back(part);
    if (parts.size() != 3 && parts.size() != 4) return std::nullopt;
    double v[4] = {0, 0, 0, 1};
    for (size_t i = 0; i < parts.size(); ++i) {
      const bool percent = parts[i].back() == '%';
      if (percent) parts[i].pop_back();
      double x = 0;
      if (core::ParseDouble(parts[i], &x) == 0) return std::nullopt;
      if (i < 3) {
        v[i] = std::clamp(percent ? x / 100.0 : x / 255.0, 0.0, 1.0);
      } else {
        v[i] = std::clamp(percent ? x / 100.0 : x, 0.0, 1.0);
      }
    }
    if (alpha && parts.size() == 4) *alpha = v[3];
    return core::RgbColor{v[0], v[1], v[2]};
  }
  for (const auto& named : kNamedColors) {
    if (t == named.name) return FromHex(named.rgb);
  }
  if (t == "transparent") {
    if (alpha) *alpha = 0;
    return core::RgbColor{0, 0, 0};
  }
  return std::nullopt;
}

std::optional<double> ParseLength(std::string_view text, double percent_of) {
  const std::string t = Lower(Trim(text));
  NumberScanner s(t);
  const auto number = s.Number();
  if (!number) return std::nullopt;
  const std::string unit(Trim(std::string_view(t).substr(s.position())));
  if (unit.empty() || unit == "px") return *number;
  if (unit == "pt") return *number * 96.0 / 72.0;
  if (unit == "pc") return *number * 16.0;
  if (unit == "mm") return *number * 96.0 / 25.4;
  if (unit == "cm") return *number * 96.0 / 2.54;
  if (unit == "in") return *number * 96.0;
  if (unit == "em") return *number * 16.0;
  if (unit == "ex") return *number * 8.0;
  if (unit == "%") return *number * percent_of / 100.0;
  return std::nullopt;
}

std::vector<double> ParseNumberList(std::string_view text) {
  std::vector<double> numbers;
  NumberScanner s(text);
  while (const auto n = s.Number()) numbers.push_back(*n);
  return numbers;
}

std::map<std::string, std::string> ParseDeclarations(std::string_view text) {
  std::map<std::string, std::string> result;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find(';', pos);
    if (end == std::string_view::npos) end = text.size();
    const std::string_view decl = text.substr(pos, end - pos);
    const size_t colon = decl.find(':');
    if (colon != std::string_view::npos) {
      std::string name = Lower(Trim(decl.substr(0, colon)));
      std::string value(Trim(decl.substr(colon + 1)));
      // "!important" is honoured only as plain precedence of the declaration.
      if (const size_t bang = value.find("!important"); bang != std::string::npos) {
        value = std::string(Trim(std::string_view(value).substr(0, bang)));
      }
      if (!name.empty()) result[name] = value;
    }
    pos = end + 1;
  }
  return result;
}

std::vector<CssRule> ParseStyleSheet(std::string_view css, int first_order, int* skipped) {
  // Comments out first.
  std::string text;
  for (size_t i = 0; i < css.size(); ++i) {
    if (css.substr(i, 2) == "/*") {
      const size_t end = css.find("*/", i + 2);
      if (end == std::string_view::npos) break;
      i = end + 1;
      continue;
    }
    text += css[i];
  }
  std::vector<CssRule> rules;
  int order = first_order;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t open = text.find('{', pos);
    if (open == std::string::npos) break;
    const std::string selectors(Trim(std::string_view(text).substr(pos, open - pos)));
    // Find the matching brace (at-rules nest blocks).
    int depth = 1;
    size_t close = open + 1;
    for (; close < text.size() && depth > 0; ++close) {
      if (text[close] == '{') ++depth;
      if (text[close] == '}') --depth;
    }
    const std::string body = text.substr(open + 1, close - open - 2);
    pos = close;
    if (!selectors.empty() && selectors[0] == '@') {
      if (skipped) ++*skipped;
      continue;
    }
    const auto declarations = ParseDeclarations(body);
    size_t start = 0;
    while (start <= selectors.size()) {
      size_t comma = selectors.find(',', start);
      if (comma == std::string::npos) comma = selectors.size();
      const std::string selector(Trim(std::string_view(selectors).substr(start, comma - start)));
      start = comma + 1;
      if (selector.empty()) continue;
      if (selector.find_first_of(">+~[:*") != std::string::npos) {
        if (skipped) ++*skipped;
        continue;
      }
      CssRule rule;
      rule.declarations = declarations;
      rule.order = order++;
      // Descendant parts, then each compound: element, #id, .class.
      size_t p = 0;
      bool bad = false;
      while (p < selector.size()) {
        while (p < selector.size() && IsSpace(selector[p])) ++p;
        size_t e = p;
        while (e < selector.size() && !IsSpace(selector[e])) ++e;
        if (e == p) break;
        const std::string part = selector.substr(p, e - p);
        p = e;
        CssRule::Compound compound;
        size_t i = 0;
        auto ident = [&]() {
          const size_t s = i;
          while (i < part.size() && part[i] != '.' && part[i] != '#') ++i;
          return part.substr(s, i - s);
        };
        compound.element = Lower(ident());
        while (i < part.size()) {
          const char kind = part[i++];
          const std::string name = ident();
          if (name.empty()) bad = true;
          (kind == '#' ? compound.ids : compound.classes).push_back(name);
        }
        rule.specificity += static_cast<int>(compound.ids.size()) * 100 +
                            static_cast<int>(compound.classes.size()) * 10 +
                            (compound.element.empty() ? 0 : 1);
        rule.selector.push_back(std::move(compound));
      }
      if (bad || rule.selector.empty()) {
        if (skipped) ++*skipped;
        continue;
      }
      rules.push_back(std::move(rule));
    }
  }
  return rules;
}

}  // namespace leinwand::io::svg
