// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/skia_font_source.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"

#if defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#elif defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#else
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#endif

namespace leinwand::render {

namespace {

sk_sp<SkFontMgr> PlatformFontMgr() {
#if defined(_WIN32)
  return SkFontMgr_New_DirectWrite();
#elif defined(__APPLE__)
  return SkFontMgr_New_CoreText(nullptr);
#else
  return SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#endif
}

bool SameName(const std::string& a, const std::string& b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

class SystemFontSource : public text::FontSource {
 public:
  SystemFontSource() : manager_(PlatformFontMgr()) {}

  std::vector<text::FontFamily> Families() const override {
    std::lock_guard lock(mutex_);
    if (!families_listed_ && manager_) {
      families_listed_ = true;
      for (int i = 0; i < manager_->countFamilies(); ++i) {
        SkString name;
        manager_->getFamilyName(i, &name);
        if (name.isEmpty() || name.c_str()[0] == '.') continue;  // Hidden system fonts.
        sk_sp<SkFontStyleSet> set = manager_->createStyleSet(i);
        text::FontFamily family{name.c_str(), {}};
        for (int j = 0; set && j < set->count(); ++j) {
          SkFontStyle style;
          SkString style_name;
          set->getStyle(j, &style, &style_name);
          if (!style_name.isEmpty()) family.styles.push_back(style_name.c_str());
        }
        if (!family.styles.empty()) families_.push_back(std::move(family));
      }
    }
    return families_;
  }

  text::FacePtr Find(const core::FontRef& font) const override {
    if (!manager_ || font.family.empty()) return nullptr;
    sk_sp<SkFontStyleSet> set = manager_->matchFamily(font.family.c_str());
    if (!set) return nullptr;
    for (int j = 0; j < set->count(); ++j) {
      SkFontStyle style;
      SkString name;
      set->getStyle(j, &style, &name);
      if (SameName(name.c_str(), font.style)) return FaceOf(set->createTypeface(j));
    }
    return nullptr;
  }

  text::FacePtr ForCharacter(char32_t c, const core::FontRef& like) const override {
    if (!manager_) return nullptr;
    const char* languages[] = {"ja"};
    sk_sp<SkTypeface> typeface = manager_->matchFamilyStyleCharacter(
        like.family.c_str(), SkFontStyle(), languages, 1, static_cast<SkUnichar>(c));
    return FaceOf(std::move(typeface));
  }

 private:
  // The face for a typeface, read once.
  text::FacePtr FaceOf(sk_sp<SkTypeface> typeface) const {
    if (!typeface) return nullptr;
    std::lock_guard lock(mutex_);
    const auto it = faces_.find(typeface->uniqueID());
    if (it != faces_.end()) return it->second;
    int index = 0;
    std::unique_ptr<SkStreamAsset> stream = typeface->openStream(&index);
    text::FacePtr face;
    if (stream && stream->getLength() > 0) {
      auto bytes = std::make_shared<std::vector<char>>(stream->getLength());
      stream->read(bytes->data(), bytes->size());
      face = text::Face::FromData(std::move(bytes), index);
    }
    faces_[typeface->uniqueID()] = face;
    return face;
  }

  sk_sp<SkFontMgr> manager_;
  mutable std::mutex mutex_;
  mutable bool families_listed_ = false;
  mutable std::vector<text::FontFamily> families_;
  mutable std::map<SkTypefaceID, text::FacePtr> faces_;
};

}  // namespace

std::shared_ptr<const text::FontSource> MakeSystemFontSource() {
  return std::make_shared<SystemFontSource>();
}

}  // namespace leinwand::render
