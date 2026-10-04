// SPDX-License-Identifier: GPL-3.0-or-later
// Text (spec 5, 7.2): the type tool, editing text in place (with IME
// composition), the Character and Paragraph panels, and Create Outlines.
#include <algorithm>
#include <map>

#include "core/transform.h"
#include "editor/editor.h"
#include "geometry/bezier.h"
#include "geometry/hit_test.h"
#include "text/layout.h"

namespace leinwand::editor {

namespace {

using core::Matrix;
using core::ObjectPtr;
using core::Point;

ObjectPtr WithStory(const core::TextObject& text, core::Story story) {
  core::TextObject copy = text;
  copy.story = std::make_shared<const core::Story>(std::move(story));
  return core::MakeObject(std::move(copy));
}

// Rewrites every text object under `object` (into groups, but not into
// the text kept by outlines).
ObjectPtr EditTexts(const ObjectPtr& object,
                    const std::function<ObjectPtr(const core::TextObject&)>& edit) {
  if (const auto* group = std::get_if<core::GroupObject>(object.get())) {
    core::GroupObject copy = *group;
    bool changed = false;
    for (auto& child : copy.children) {
      ObjectPtr edited = EditTexts(child, edit);
      changed = changed || edited != child;
      child = std::move(edited);
    }
    return changed ? core::MakeObject(std::move(copy)) : object;
  }
  if (const auto* text = std::get_if<core::TextObject>(object.get())) return edit(*text);
  return object;
}

void CollectTexts(const core::Object& object, std::vector<const core::TextObject*>& out) {
  if (const auto* group = std::get_if<core::GroupObject>(&object)) {
    for (const auto& child : group->children) CollectTexts(*child, out);
  } else if (const auto* text = std::get_if<core::TextObject>(&object)) {
    out.push_back(text);
  }
}

}  // namespace

// --- State -----------------------------------------------------------------

const core::TextObject* Editor::EditedText(Matrix* to_document) const {
  if (text_.id.empty()) return nullptr;
  if (text_.pending) {
    if (to_document) *to_document = Matrix{};
    return &std::get<core::TextObject>(*text_.pending);
  }
  const auto found = core::FindObjects(history_.current().document, {text_.id});
  if (found.size() != 1) return nullptr;
  if (to_document) *to_document = found[0].to_document;
  return std::get_if<core::TextObject>(found[0].object.get());
}

std::pair<std::size_t, std::size_t> Editor::TextRange() const {
  return {std::min(text_.caret, text_.anchor), std::max(text_.caret, text_.anchor)};
}

void Editor::UpdateTextPreview() {
  text_preview_.reset();
  if (text_.id.empty()) return;
  if (!text_.pending && text_.preedit.empty()) return;
  Matrix to_document;
  const core::TextObject* text = EditedText(&to_document);
  if (!text) return;
  core::TextObject shown = *text;
  if (!text_.preedit.empty()) {
    const core::CharacterStyle typing = TypingStyle();
    shown.story = std::make_shared<const core::Story>(
        core::Inserted(*text->story, text_.caret, text_.preedit, &typing));
  }
  const core::EditorState& base = history_.current();
  core::EditorState state{base.document, {text_.id}};
  if (text_.pending) {
    state.document = WithNewObject(base.document, core::MakeObject(std::move(shown)));
  } else {
    state.document =
        core::ReplaceObjects(base.document, {{text_.id, core::MakeObject(std::move(shown))}});
  }
  text_preview_ = std::move(state);
}

core::CharacterStyle Editor::TypingStyle() const {
  if (text_.pending_style) return *text_.pending_style;
  const core::TextObject* text = EditedText();
  if (!text || !text->story) return text_style_;
  const auto [from, to] = TextRange();
  return core::StyleAt(*text->story, from == 0 ? 0 : from - 1);
}

void Editor::BeginTextEdit(const std::string& id, std::size_t caret) {
  text_ = {};
  text_.id = id;
  text_.caret = text_.anchor = caret;
  text_preview_.reset();
  history_.SetSelection({id});
}

void Editor::EndTextEdit() {
  if (text_.id.empty()) return;
  const std::string id = text_.id;
  const bool pending = text_.pending != nullptr;
  text_ = {};
  text_preview_.reset();
  if (pending) return;  // Never typed into: nothing was recorded.
  // An emptied text goes away, as in Illustrator.
  const auto found = core::FindObjects(history_.current().document, {id});
  if (found.size() == 1) {
    const auto* text = std::get_if<core::TextObject>(found[0].object.get());
    if (text && text->story && text->story->text.empty()) {
      Commit("delete", {core::RemoveObjects(history_.current().document, {id}), {}});
      return;
    }
  }
  history_.SetSelection({id});
}

void Editor::ValidateTextEdit() {
  if (text_.id.empty()) return;
  const core::TextObject* text = EditedText();
  if (!text || !text->story) {
    text_ = {};
    text_preview_.reset();
    return;
  }
  const std::size_t size = text->story->text.size();
  text_.caret = std::min(text_.caret, size);
  text_.anchor = std::min(text_.anchor, size);
  text_.preedit.clear();
  UpdateTextPreview();
}

// --- Editing ---------------------------------------------------------------

void Editor::CommitStory(core::Story story, std::size_t caret, const std::string& action,
                         bool typing) {
  Matrix to_document;
  const core::TextObject* text = EditedText(&to_document);
  if (!text) return;
  ObjectPtr edited = WithStory(*text, std::move(story));
  const core::EditorState& base = history_.current();
  core::EditorState state{base.document, {text_.id}};
  if (text_.pending) {
    state.document = WithNewObject(base.document, edited);
  } else {
    state.document = core::ReplaceObjects(base.document, {{text_.id, edited}});
  }
  // Typing in a row is one undo step.
  const bool merge = typing && text_.typing && !text_.pending && history_.undo_action() == action;
  if (merge) {
    history_.Amend(action, std::move(state));
  } else {
    Commit(action, std::move(state));
  }
  text_.pending = nullptr;
  text_.pending_style.reset();
  text_.typing = typing;
  text_.caret = text_.anchor = caret;
  text_.goal_x.reset();
  UpdateTextPreview();
}

void Editor::InsertText(std::u32string_view inserted) {
  const core::TextObject* text = EditedText();
  if (!text || !text->story) return;
  // Line breaks of any kind end paragraphs.
  std::u32string clean;
  for (std::size_t i = 0; i < inserted.size(); ++i) {
    char32_t c = inserted[i];
    if (c == U'\r') {
      if (i + 1 < inserted.size() && inserted[i + 1] == U'\n') continue;
      c = U'\n';
    }
    if (c == U' ' || c == U' ') c = U'\n';
    if (c < 0x20 && c != U'\n' && c != U'\t') continue;
    clean.push_back(c);
  }
  text_.preedit.clear();
  if (clean.empty()) {
    UpdateTextPreview();
    return;
  }
  const auto [from, to] = TextRange();
  const core::CharacterStyle style = TypingStyle();
  core::Story story = core::Erased(*text->story, from, to);
  story = core::Inserted(story, from, clean, &style);
  CommitStory(std::move(story), from + clean.size(), "typing", true);
}

void Editor::DeleteBackward() {
  const core::TextObject* text = EditedText();
  if (!text || !text->story) return;
  auto [from, to] = TextRange();
  if (from == to) {
    if (from == 0) return;
    from = text::PreviousBoundary(*text::LayoutOf(text->story), from);
  }
  CommitStory(core::Erased(*text->story, from, to), from, "typing", true);
}

void Editor::DeleteForward() {
  const core::TextObject* text = EditedText();
  if (!text || !text->story) return;
  auto [from, to] = TextRange();
  if (from == to) {
    if (to >= text->story->text.size()) return;
    to = text::NextBoundary(*text::LayoutOf(text->story), to);
  }
  CommitStory(core::Erased(*text->story, from, to), from, "typing", true);
}

void Editor::MoveCaret(TextMove move, bool extend) {
  const core::TextObject* text = EditedText();
  if (!text || !text->story) return;
  const text::LayoutPtr layout = text::LayoutOf(text->story);
  const auto [from, to] = TextRange();
  std::size_t caret = text_.caret;
  const double x = text_.goal_x.value_or(text::CaretX(*layout, caret));
  bool keep_goal = false;
  switch (move) {
    case TextMove::kLeft:
      // Without Shift, a selection collapses to its start.
      caret = !extend && from != to ? from : text::PreviousBoundary(*layout, caret);
      break;
    case TextMove::kRight:
      caret = !extend && from != to ? to : text::NextBoundary(*layout, caret);
      break;
    case TextMove::kUp:
      caret = text::LineUp(*layout, caret, x);
      keep_goal = true;
      break;
    case TextMove::kDown:
      caret = text::LineDown(*layout, caret, x);
      keep_goal = true;
      break;
    case TextMove::kLineStart:
      caret = layout->lines.empty() ? 0 : layout->lines[text::LineOf(*layout, caret)].start;
      break;
    case TextMove::kLineEnd:
      caret = layout->lines.empty() ? 0 : layout->lines[text::LineOf(*layout, caret)].end;
      break;
    case TextMove::kStart:
      caret = 0;
      break;
    case TextMove::kEnd:
      caret = text->story->text.size();
      break;
  }
  text_.caret = caret;
  if (!extend) text_.anchor = caret;
  text_.goal_x = keep_goal ? std::optional<double>(x) : std::nullopt;
  text_.typing = false;
  text_.pending_style.reset();
}

void Editor::SelectAllText() {
  const core::TextObject* text = EditedText();
  if (!text || !text->story) return;
  text_.anchor = 0;
  text_.caret = text->story->text.size();
  text_.typing = false;
}

void Editor::SelectWordAt(Point p) {
  Matrix to_document;
  const core::TextObject* text = EditedText(&to_document);
  if (!text || !text->story) return;
  const auto inverse = (to_document * text->transform).Inverted();
  if (!inverse) return;
  const std::size_t index = text::CaretAt(*text::LayoutOf(text->story), inverse->Map(p));
  const auto [from, to] = text::WordAt(*text->story, index);
  text_.anchor = from;
  text_.caret = to;
  text_.typing = false;
}

std::u32string Editor::SelectedText() const {
  const core::TextObject* text = EditedText();
  if (!text || !text->story) return {};
  const auto [from, to] = TextRange();
  return text->story->text.substr(from, to - from);
}

void Editor::SetPreedit(std::u32string preedit, std::size_t cursor) {
  if (text_.id.empty()) return;
  // Composing replaces the selection.
  if (!preedit.empty()) {
    const auto [from, to] = TextRange();
    if (from != to) {
      const core::TextObject* text = EditedText();
      if (text && text->story) {
        const core::CharacterStyle style = TypingStyle();
        CommitStory(core::Erased(*text->story, from, to), from, "typing", true);
        text_.pending_style = style;
      }
    }
  }
  text_.preedit = std::move(preedit);
  text_.preedit_cursor = std::min(cursor, text_.preedit.size());
  UpdateTextPreview();
}

std::optional<core::Rect> Editor::CaretRect() const {
  Matrix to_document;
  const core::TextObject* text = EditedText(&to_document);
  if (!text) return std::nullopt;
  const core::TextObject* shown = text;
  // The caret inside the composition while composing.
  core::TextObject composing;
  std::size_t index = text_.caret;
  if (!text_.preedit.empty()) {
    composing = *text;
    composing.story = std::make_shared<const core::Story>(
        core::Inserted(*text->story, text_.caret, text_.preedit));
    shown = &composing;
    index += text_.preedit_cursor;
  }
  const auto [top, bottom] = text::CaretLine(*text::LayoutOf(shown->story), index);
  const Matrix m = to_document * text->transform;
  return core::Rect::FromPoint(m.Map(top)).Union(m.Map(bottom));
}

void Editor::TextOverlay(Overlay& overlay) const {
  Matrix to_document;
  const core::TextObject* text = EditedText(&to_document);
  if (!text) return;
  const Matrix m = to_document * text->transform;
  // What is shown: with the composition in it.
  core::StoryPtr story = text->story;
  if (!text_.preedit.empty()) {
    story = std::make_shared<const core::Story>(
        core::Inserted(*text->story, text_.caret, text_.preedit));
  }
  const text::LayoutPtr layout = text::LayoutOf(story);
  auto quad = [&](const core::Rect& r) {
    return std::array<Point, 4>{m.Map({r.left, r.top}), m.Map({r.right, r.top}),
                                m.Map({r.right, r.bottom}), m.Map({r.left, r.bottom})};
  };
  if (!text_.preedit.empty()) {
    for (const core::Rect& r :
         text::SelectionBoxes(*layout, text_.caret, text_.caret + text_.preedit.size())) {
      const double y = r.bottom - (r.bottom - r.top) * 0.08;
      overlay.text_underlines.push_back({m.Map({r.left, y}), m.Map({r.right, y})});
    }
    const auto [top, bottom] = text::CaretLine(*layout, text_.caret + text_.preedit_cursor);
    overlay.text_caret = std::pair{m.Map(top), m.Map(bottom)};
    return;
  }
  const auto [from, to] = TextRange();
  for (const core::Rect& r : text::SelectionBoxes(*layout, from, to)) {
    overlay.text_selection.push_back(quad(r));
  }
  if (from == to) {
    const auto [top, bottom] = text::CaretLine(*layout, text_.caret);
    overlay.text_caret = std::pair{m.Map(top), m.Map(bottom)};
  }
}

// --- The type tool ---------------------------------------------------------

void Editor::TypeDown(Point p, Modifiers modifiers, double pick) {
  // On text: edit it, with the caret where it was pressed.
  const auto hit = geometry::HitTest(history_.current().document, p, pick);
  if (hit) {
    const auto found = core::FindObjects(history_.current().document, {hit->leaf_id});
    if (found.size() == 1) {
      if (const auto* text = std::get_if<core::TextObject>(found[0].object.get())) {
        const auto inverse = (found[0].to_document * text->transform).Inverted();
        const std::size_t index =
            inverse ? text::CaretAt(*text::LayoutOf(text->story), inverse->Map(p)) : 0;
        if (text_.id == hit->leaf_id && modifiers.shift) {
          text_.caret = index;
        } else {
          if (text_.id != hit->leaf_id) EndTextEdit();
          BeginTextEdit(hit->leaf_id, index);
        }
        text_.preedit.clear();
        UpdateTextPreview();
        drag_.kind = DragKind::kTextSelect;
        return;
      }
    }
  }
  // Elsewhere: new point text with its first baseline at the press.
  EndTextEdit();
  core::TextObject text;
  text.common.id = ids_.Next();
  text.common.appearance = {core::Fill{core::RgbColor{0, 0, 0}}};
  text.story = std::make_shared<const core::Story>(
      core::MakeStory(ids_.Next(), U"", text_style_, paragraph_style_));
  text.transform = Matrix::Translate(p.x, p.y);
  text_ = {};
  text_.id = text.common.id;
  text_.pending = core::MakeObject(std::move(text));
  UpdateTextPreview();
}

void Editor::TypeDrag() {
  Matrix to_document;
  const core::TextObject* text = EditedText(&to_document);
  if (!text || text_.pending) return;
  const auto inverse = (to_document * text->transform).Inverted();
  if (!inverse) return;
  text_.caret = text::CaretAt(*text::LayoutOf(text->story), inverse->Map(drag_.current));
  text_.typing = false;
}

void Editor::DoubleClick(Point p, double pick) {
  if (tool() == Tool::kType) {
    SelectWordAt(p);
    return;
  }
  if (tool() != Tool::kSelection && tool() != Tool::kDirectSelection) return;
  // Double-clicking text with a selection tool edits it with the type tool.
  const auto hit = geometry::HitTest(history_.current().document, p, pick);
  if (!hit) return;
  const auto found = core::FindObjects(history_.current().document, {hit->leaf_id});
  if (found.size() != 1 || !std::holds_alternative<core::TextObject>(*found[0].object)) return;
  drag_ = {};
  SetTool(Tool::kType);
  TypeDown(p, {}, pick);
  drag_ = {};
}

// --- Character and Paragraph panels ------------------------------------------

TextStyleState Editor::TextStyle() const {
  TextStyleState state;
  if (const core::TextObject* text = EditedText(); text && text->story) {
    const auto [from, to] = TextRange();
    state.editing = true;
    state.characters = text_.pending_style ? std::vector<core::CharacterStyle>{*text_.pending_style}
                                           : core::StylesIn(*text->story, from, to);
    const std::size_t first = core::ParagraphOf(*text->story, from);
    const std::size_t last = to > from ? core::ParagraphOf(*text->story, to - 1) : first;
    for (std::size_t p = first; p <= last && p < text->story->paragraphs.size(); ++p) {
      state.paragraphs.push_back(text->story->paragraphs[p]);
    }
    return state;
  }
  std::vector<const core::TextObject*> texts;
  for (const auto& found :
       core::FindObjects(document(), core::WithoutNested(document(), selection()))) {
    CollectTexts(*found.object, texts);
  }
  for (const core::TextObject* text : texts) {
    if (!text->story) continue;
    for (const auto& run : text->story->characters) state.characters.push_back(run.style);
    for (const auto& paragraph : text->story->paragraphs) state.paragraphs.push_back(paragraph);
  }
  state.selected_text = !texts.empty();
  if (state.characters.empty()) state.characters.push_back(text_style_);
  if (state.paragraphs.empty()) state.paragraphs.push_back(paragraph_style_);
  return state;
}

void Editor::EditCharacterStyle(const std::function<void(core::CharacterStyle&)>& edit) {
  if (const core::TextObject* text = EditedText(); text && text->story) {
    const auto [from, to] = TextRange();
    if (from == to) {
      // Nothing selected: the style to type with next.
      core::CharacterStyle style = TypingStyle();
      edit(style);
      if (text->story->text.empty()) {
        // An empty text takes it at once (it has nothing else to show).
        core::Story story = core::WithCharacterStyle(*text->story, 0, 0, edit);
        const bool pending = text_.pending != nullptr;
        if (pending) {
          core::TextObject copy = *text;
          copy.story = std::make_shared<const core::Story>(std::move(story));
          text_.pending = core::MakeObject(std::move(copy));
          text_style_ = style;
          UpdateTextPreview();
          return;
        }
        CommitStory(std::move(story), from, "character style", false);
        return;
      }
      text_.pending_style = style;
      return;
    }
    const std::size_t caret = text_.caret, anchor = text_.anchor;
    CommitStory(core::WithCharacterStyle(*text->story, from, to, edit), caret, "character style",
                false);
    text_.anchor = anchor;
    return;
  }
  // Not editing: every selected text, and the style for new text.
  edit(text_style_);
  if (selection().empty()) return;
  std::map<std::string, ObjectPtr> replacements;
  for (const auto& found :
       core::FindObjects(document(), core::WithoutNested(document(), selection()))) {
    ObjectPtr edited = EditTexts(found.object, [&](const core::TextObject& t) -> ObjectPtr {
      if (!t.story) return core::MakeObject(t);
      return WithStory(t, core::WithCharacterStyle(*t.story, 0, t.story->text.size(), edit));
    });
    if (edited != found.object) replacements[core::CommonOf(*found.object).id] = edited;
  }
  if (replacements.empty()) return;
  Commit("character style", {core::ReplaceObjects(document(), replacements), selection()});
}

void Editor::EditParagraphStyle(const std::function<void(core::ParagraphStyle&)>& edit) {
  if (const core::TextObject* text = EditedText(); text && text->story) {
    const auto [from, to] = TextRange();
    const std::size_t caret = text_.caret, anchor = text_.anchor;
    core::Story story = core::WithParagraphStyle(*text->story, from, to, edit);
    if (text_.pending) {
      core::TextObject copy = *text;
      copy.story = std::make_shared<const core::Story>(std::move(story));
      text_.pending = core::MakeObject(std::move(copy));
      edit(paragraph_style_);
      UpdateTextPreview();
      return;
    }
    CommitStory(std::move(story), caret, "paragraph style", false);
    text_.anchor = anchor;
    return;
  }
  edit(paragraph_style_);
  if (selection().empty()) return;
  std::map<std::string, ObjectPtr> replacements;
  for (const auto& found :
       core::FindObjects(document(), core::WithoutNested(document(), selection()))) {
    ObjectPtr edited = EditTexts(found.object, [&](const core::TextObject& t) -> ObjectPtr {
      if (!t.story) return core::MakeObject(t);
      return WithStory(t, core::WithParagraphStyle(*t.story, 0, t.story->text.size(), edit));
    });
    if (edited != found.object) replacements[core::CommonOf(*found.object).id] = edited;
  }
  if (replacements.empty()) return;
  Commit("paragraph style", {core::ReplaceObjects(document(), replacements), selection()});
}

// --- Create Outlines -----------------------------------------------------------

void Editor::CreateOutlines() {
  EndTextEdit();
  if (selection().empty()) return;
  std::map<std::string, ObjectPtr> replacements;
  core::IdSet selected;
  for (const auto& found :
       core::FindObjects(document(), core::WithoutNested(document(), selection()))) {
    const std::string id = core::CommonOf(*found.object).id;
    ObjectPtr edited = EditTexts(found.object, [&](const core::TextObject& t) -> ObjectPtr {
      // A group of one compound path per glyph, in the text's paint, that
      // keeps the text for Type > Revert Outlines (spec 7.5).
      core::GroupObject group;
      group.common = t.common;
      group.common.id = ids_.Next();
      group.common.appearance.clear();
      core::TextObject kept = t;
      kept.common.mask.reset();
      group.outlined_text = core::MakeObject(std::move(kept));
      const text::LayoutPtr layout = text::LayoutOf(t.story);
      for (auto& glyph : text::GlyphOutlines(*layout)) {
        core::CompoundPathObject path;
        path.common.id = ids_.Next();
        path.common.appearance = t.common.appearance;
        for (auto& contour : glyph)
          path.subpaths.push_back(core::Transformed(contour, t.transform));
        group.children.push_back(core::MakeObject(std::move(path)));
      }
      return core::MakeObject(std::move(group));
    });
    if (edited == found.object) {
      selected.insert(id);
      continue;
    }
    replacements[id] = edited;
    selected.insert(core::CommonOf(*edited).id);
  }
  if (replacements.empty()) return;
  Commit("create outlines", {core::ReplaceObjects(document(), replacements), selected});
}

void Editor::RevertOutlines() {
  if (selection().empty()) return;
  std::map<std::string, ObjectPtr> replacements;
  core::IdSet selected;
  for (const auto& found :
       core::FindObjects(document(), core::WithoutNested(document(), selection()))) {
    const auto* group = std::get_if<core::GroupObject>(found.object.get());
    const std::string id = core::CommonOf(*found.object).id;
    if (!group || !group->outlined_text) {
      selected.insert(id);
      continue;
    }
    // The text where the outlines are now, with the group's opacity,
    // blending and mask.
    ObjectPtr text = core::Transformed(group->outlined_text, group->transform);
    core::TextObject restored = std::get<core::TextObject>(*text);
    const core::Appearance appearance = restored.common.appearance;
    const std::string text_id = restored.common.id;
    restored.common = group->common;
    restored.common.id = text_id;
    restored.common.appearance = appearance;
    replacements[id] = core::MakeObject(std::move(restored));
    selected.insert(text_id);
  }
  if (replacements.empty()) return;
  Commit("revert outlines", {core::ReplaceObjects(document(), replacements), selected});
}

}  // namespace leinwand::editor
