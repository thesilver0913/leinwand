// SPDX-License-Identifier: GPL-3.0-or-later
#include "text/font.h"

#include <hb-ot.h>
#include <hb.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include "text/font_impl.h"

namespace leinwand::text {

namespace {

std::atomic<std::uint64_t> next_face_id{1};

std::string Name(hb_face_t* face, hb_ot_name_id_t id) {
  for (hb_language_t language : {hb_language_from_string("en", -1), HB_LANGUAGE_INVALID}) {
    unsigned length = hb_ot_name_get_utf8(face, id, language, nullptr, nullptr);
    if (length == 0) continue;
    std::string name(length + 1, '\0');
    unsigned size = length + 1;
    hb_ot_name_get_utf8(face, id, language, &size, name.data());
    name.resize(size);
    return name;
  }
  return {};
}

bool SameName(const std::string& a, const std::string& b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

// Collects HarfBuzz's drawing calls into closed subpaths (font outlines are
// always closed). Quadratic curves (TrueType) become cubics exactly.
struct OutlineBuilder {
  std::vector<core::PathData> paths;
  double scale = 1.0;  // Font units to em.
  core::Point current;

  core::Point P(float x, float y) const { return {x * scale, -y * scale}; }

  void MoveTo(core::Point p) {
    Close();
    paths.push_back({});
    paths.back().anchors.push_back({p});
    current = p;
  }
  // A broken font may draw without moving first: start a contour then.
  void Begin() {
    if (paths.empty() || paths.back().closed) MoveTo(current);
  }
  void LineTo(core::Point p) {
    Begin();
    paths.back().anchors.push_back({p});
    current = p;
  }
  void CubicTo(core::Point c1, core::Point c2, core::Point p) {
    Begin();
    auto& anchors = paths.back().anchors;
    anchors.back().handle_out = c1 - anchors.back().position;
    anchors.push_back({p, c2 - p, {}});
    current = p;
  }
  void Close() {
    if (paths.empty() || paths.back().closed) return;
    auto& path = paths.back();
    path.closed = true;
    // The contour returns to its start: fold the duplicate end into it.
    if (path.anchors.size() > 1) {
      const core::Anchor& last = path.anchors.back();
      const core::Point d = last.position - path.anchors.front().position;
      if (std::abs(d.x) < 1e-9 && std::abs(d.y) < 1e-9) {
        path.anchors.front().handle_in = last.handle_in;
        path.anchors.pop_back();
      }
    }
  }
};

hb_draw_funcs_t* OutlineFuncs() {
  static hb_draw_funcs_t* funcs = [] {
    hb_draw_funcs_t* f = hb_draw_funcs_create();
    hb_draw_funcs_set_move_to_func(
        f,
        [](hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x, float y, void*) {
          auto* b = static_cast<OutlineBuilder*>(data);
          b->MoveTo(b->P(x, y));
        },
        nullptr, nullptr);
    hb_draw_funcs_set_line_to_func(
        f,
        [](hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x, float y, void*) {
          auto* b = static_cast<OutlineBuilder*>(data);
          b->LineTo(b->P(x, y));
        },
        nullptr, nullptr);
    hb_draw_funcs_set_quadratic_to_func(
        f,
        [](hb_draw_funcs_t*, void* data, hb_draw_state_t*, float cx, float cy, float x, float y,
           void*) {
          auto* b = static_cast<OutlineBuilder*>(data);
          const core::Point p0 = b->current, c = b->P(cx, cy), p = b->P(x, y);
          b->CubicTo(p0 + (c - p0) * (2.0 / 3.0), p + (c - p) * (2.0 / 3.0), p);
        },
        nullptr, nullptr);
    hb_draw_funcs_set_cubic_to_func(
        f,
        [](hb_draw_funcs_t*, void* data, hb_draw_state_t*, float c1x, float c1y, float c2x,
           float c2y, float x, float y, void*) {
          auto* b = static_cast<OutlineBuilder*>(data);
          b->CubicTo(b->P(c1x, c1y), b->P(c2x, c2y), b->P(x, y));
        },
        nullptr, nullptr);
    hb_draw_funcs_set_close_path_func(
        f,
        [](hb_draw_funcs_t*, void* data, hb_draw_state_t*, void*) {
          static_cast<OutlineBuilder*>(data)->Close();
        },
        nullptr, nullptr);
    hb_draw_funcs_make_immutable(f);
    return f;
  }();
  return funcs;
}

}  // namespace

Face::Impl::~Impl() {
  if (font) hb_font_destroy(font);
  if (face) hb_face_destroy(face);
  if (blob) hb_blob_destroy(blob);
}

Face::Face(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Face::~Face() = default;

FacePtr Face::FromData(std::shared_ptr<const std::vector<char>> bytes, int index) {
  if (!bytes || bytes->empty()) return nullptr;
  auto impl = std::make_unique<Impl>();
  impl->bytes = std::move(bytes);
  impl->index = index;
  impl->blob = hb_blob_create(impl->bytes->data(), static_cast<unsigned>(impl->bytes->size()),
                              HB_MEMORY_MODE_READONLY, nullptr, nullptr);
  impl->face = hb_face_create(impl->blob, static_cast<unsigned>(index));
  if (hb_face_get_glyph_count(impl->face) == 0) return nullptr;
  hb_face_make_immutable(impl->face);
  impl->font = hb_font_create(impl->face);
  hb_font_make_immutable(impl->font);
  impl->upem = hb_face_get_upem(impl->face);
  impl->family = Name(impl->face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY);
  if (impl->family.empty()) impl->family = Name(impl->face, HB_OT_NAME_ID_FONT_FAMILY);
  impl->style = Name(impl->face, HB_OT_NAME_ID_TYPOGRAPHIC_SUBFAMILY);
  if (impl->style.empty()) impl->style = Name(impl->face, HB_OT_NAME_ID_FONT_SUBFAMILY);
  impl->postscript_name = Name(impl->face, HB_OT_NAME_ID_POSTSCRIPT_NAME);
  hb_font_extents_t extents{};
  hb_font_get_h_extents(impl->font, &extents);
  impl->ascender = extents.ascender / double(impl->upem);
  impl->descender = -extents.descender / double(impl->upem);
  if (impl->ascender <= 0) impl->ascender = 0.88;
  if (impl->descender <= 0) impl->descender = 0.12;
  impl->id = next_face_id++;
  return FacePtr(new Face(std::move(impl)));
}

const std::string& Face::family() const { return impl_->family; }
const std::string& Face::style() const { return impl_->style; }
const std::string& Face::postscript_name() const { return impl_->postscript_name; }
std::uint64_t Face::id() const { return impl_->id; }
const std::vector<char>& Face::bytes() const { return *impl_->bytes; }
int Face::index() const { return impl_->index; }
double Face::ascender() const { return impl_->ascender; }
double Face::descender() const { return impl_->descender; }

bool Face::HasGlyph(char32_t c) const {
  hb_codepoint_t glyph = 0;
  return hb_font_get_nominal_glyph(impl_->font, c, &glyph) && glyph != 0;
}

std::vector<core::PathData> Face::Outline(std::uint16_t glyph) const {
  OutlineBuilder builder;
  builder.scale = 1.0 / impl_->upem;
  hb_font_draw_glyph(impl_->font, glyph, OutlineFuncs(), &builder);
  builder.Close();
  std::erase_if(builder.paths, [](const core::PathData& p) { return p.anchors.size() < 2; });
  return builder.paths;
}

// --- Folder source ---------------------------------------------------------

FolderFontSource::FolderFontSource(const std::filesystem::path& folder) {
  std::error_code error;
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".otf" || ext == ".ttf" || ext == ".ttc" || ext == ".otc") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  for (const auto& file : files) {
    std::ifstream in(file, std::ios::binary);
    auto bytes = std::make_shared<std::vector<char>>(std::istreambuf_iterator<char>(in),
                                                     std::istreambuf_iterator<char>());
    hb_blob_t* blob = hb_blob_create(bytes->data(), static_cast<unsigned>(bytes->size()),
                                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    const unsigned count = std::max(1u, hb_face_count(blob));
    hb_blob_destroy(blob);
    for (unsigned i = 0; i < count; ++i) {
      if (FacePtr face = Face::FromData(bytes, static_cast<int>(i))) faces_.push_back(face);
    }
  }
}

std::vector<FontFamily> FolderFontSource::Families() const {
  std::vector<FontFamily> families;
  for (const FacePtr& face : faces_) {
    auto it = std::find_if(families.begin(), families.end(),
                           [&](const FontFamily& f) { return f.name == face->family(); });
    if (it == families.end()) {
      families.push_back({face->family(), {}});
      it = families.end() - 1;
    }
    it->styles.push_back(face->style());
  }
  return families;
}

FacePtr FolderFontSource::Find(const core::FontRef& font) const {
  for (const FacePtr& face : faces_) {
    if (SameName(face->family(), font.family) && SameName(face->style(), font.style)) return face;
  }
  if (!font.postscript_name.empty()) {
    for (const FacePtr& face : faces_) {
      if (face->postscript_name() == font.postscript_name) return face;
    }
  }
  return nullptr;
}

FacePtr FolderFontSource::ForCharacter(char32_t c, const core::FontRef& like) const {
  FacePtr any;
  for (const FacePtr& face : faces_) {
    if (!face->HasGlyph(c)) continue;
    if (SameName(face->style(), like.style)) return face;
    if (!any) any = face;
  }
  return any;
}

// --- The registry ----------------------------------------------------------

namespace {

struct Registry {
  std::mutex mutex;
  std::vector<std::shared_ptr<const FontSource>> sources;
  std::uint64_t generation = 1;
  std::map<std::string, FacePtr> faces;  // By font key; null for missing.
  std::map<std::string, FacePtr> substitutes;
  std::map<std::pair<std::string, char32_t>, FacePtr> fallbacks;
};

Registry& registry() {
  static Registry r;
  return r;
}

std::string Key(const core::FontRef& font) {
  return font.family + '\x1f' + font.style + '\x1f' + font.postscript_name;
}

FacePtr FindLocked(Registry& r, const core::FontRef& font) {
  const std::string key = Key(font);
  if (const auto it = r.faces.find(key); it != r.faces.end()) return it->second;
  FacePtr found;
  for (const auto& source : r.sources) {
    if ((found = source->Find(font))) break;
  }
  r.faces[key] = found;
  return found;
}

FacePtr SearchSubstitute(Registry& r, const core::FontRef& font);

FacePtr SubstituteLocked(Registry& r, const core::FontRef& font) {
  if (FacePtr face = FindLocked(r, font)) return face;
  const std::string key = Key(font);
  if (const auto it = r.substitutes.find(key); it != r.substitutes.end()) return it->second;
  return r.substitutes[key] = SearchSubstitute(r, font);
}

FacePtr SearchSubstitute(Registry& r, const core::FontRef& font) {
  // The family in another style, then the default font, then anything.
  for (const auto& source : r.sources) {
    for (const FontFamily& family : source->Families()) {
      if (!SameName(family.name, font.family) || family.styles.empty()) continue;
      if (FacePtr face = FindLocked(r, {family.name, family.styles.front(), {}})) return face;
    }
  }
  if (FacePtr face = FindLocked(r, core::DefaultFont())) return face;
  for (const auto& source : r.sources) {
    for (const FontFamily& family : source->Families()) {
      for (const std::string& style : family.styles) {
        if (FacePtr face = FindLocked(r, {family.name, style, {}})) return face;
      }
    }
  }
  return nullptr;
}

}  // namespace

void SetFontSources(std::vector<std::shared_ptr<const FontSource>> sources) {
  Registry& r = registry();
  std::lock_guard lock(r.mutex);
  r.sources = std::move(sources);
  r.faces.clear();
  r.substitutes.clear();
  r.fallbacks.clear();
  ++r.generation;
}

std::uint64_t FontsGeneration() {
  Registry& r = registry();
  std::lock_guard lock(r.mutex);
  return r.generation;
}

std::vector<FontFamily> Families() {
  Registry& r = registry();
  std::vector<std::shared_ptr<const FontSource>> sources;
  {
    std::lock_guard lock(r.mutex);
    sources = r.sources;
  }
  std::vector<FontFamily> all;
  std::set<std::string> seen;
  for (const auto& source : sources) {
    for (FontFamily& family : source->Families()) {
      if (seen.insert(family.name).second) all.push_back(std::move(family));
    }
  }
  std::sort(all.begin(), all.end(),
            [](const FontFamily& a, const FontFamily& b) { return a.name < b.name; });
  return all;
}

FacePtr FindFace(const core::FontRef& font) {
  Registry& r = registry();
  std::lock_guard lock(r.mutex);
  return FindLocked(r, font);
}

bool IsAvailable(const core::FontRef& font) { return FindFace(font) != nullptr; }

FacePtr FaceOrSubstitute(const core::FontRef& font) {
  Registry& r = registry();
  std::lock_guard lock(r.mutex);
  return SubstituteLocked(r, font);
}

FacePtr FaceForCharacter(char32_t c, const core::FontRef& font) {
  Registry& r = registry();
  std::lock_guard lock(r.mutex);
  FacePtr primary = SubstituteLocked(r, font);
  if (primary && primary->HasGlyph(c)) return primary;
  const auto key = std::make_pair(Key(font), c);
  if (const auto it = r.fallbacks.find(key); it != r.fallbacks.end()) return it->second;
  FacePtr found;
  if (FacePtr fallback = FindLocked(r, core::DefaultFont()); fallback && fallback->HasGlyph(c)) {
    found = fallback;
  }
  for (const auto& source : r.sources) {
    if (found) break;
    found = source->ForCharacter(c, font);
  }
  r.fallbacks[key] = found;
  return found;
}

}  // namespace leinwand::text
