// SPDX-License-Identifier: GPL-3.0-or-later
// The type tool, editing text, the Character and Paragraph panels and
// Create Outlines in the editor.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/style.h"
#include "editor/editor.h"
#include "geometry/bezier.h"

using namespace leinwand;
using Catch::Approx;
using editor::Editor;
using editor::TextMove;
using editor::Tool;

namespace {

constexpr double kPick = 4.0;

core::Document Empty() {
  core::Layer layer;
  layer.id = "l1";
  core::Document document;
  document.artboards = {{"ab", "Artboard 1", {0, 0, 1000, 800}, {}, 0}};
  document.layers = {core::MakeLayer(std::move(layer))};
  return document;
}

const core::TextObject* OnlyText(const Editor& editor) {
  const core::TextObject* found = nullptr;
  core::VisitObjects(editor.document(), [&](const core::Object& o) {
    if (const auto* t = std::get_if<core::TextObject>(&o)) found = t;
  });
  return found;
}

// What is drawn, composition included.
std::u32string ShownTextOf(const Editor& editor) {
  std::u32string text;
  core::VisitObjects(editor.shown_document(), [&](const core::Object& o) {
    if (const auto* t = std::get_if<core::TextObject>(&o); t && t->story) text = t->story->text;
  });
  return text;
}

std::u32string TextOf(const Editor& editor) {
  const core::TextObject* text = OnlyText(editor);
  return text && text->story ? text->story->text : U"";
}

void Click(Editor& editor, core::Point p) {
  editor.PointerDown(p, {}, kPick);
  editor.PointerUp(p, {});
}

}  // namespace

TEST_CASE("The type tool makes point text where it is clicked, once something is typed") {
  Editor editor(Empty());
  editor.SetTool(Tool::kType);
  Click(editor, {100, 200});
  CHECK(editor.text_editing());
  CHECK(!editor.history().CanUndo());  // Nothing recorded yet.
  editor.InsertText(U"Leinwand ");
  editor.InsertText(U"で文字");
  REQUIRE(OnlyText(editor));
  CHECK(TextOf(editor) == U"Leinwand で文字");
  CHECK(OnlyText(editor)->transform.Map({0, 0}) == core::Point{100, 200});
  // Typing in a row is one step.
  editor.Undo();
  CHECK(!OnlyText(editor));
  CHECK(!editor.text_editing());
}

TEST_CASE("Leaving new text untouched leaves nothing behind") {
  Editor editor(Empty());
  editor.SetTool(Tool::kType);
  Click(editor, {10, 10});
  editor.EndTextEdit();
  CHECK(!OnlyText(editor));
  CHECK(!editor.history().CanUndo());
}

TEST_CASE("Caret keys, selections, deleting and paragraph breaks") {
  Editor editor(Empty());
  editor.SetTool(Tool::kType);
  Click(editor, {10, 50});
  editor.InsertText(U"abc");
  editor.MoveCaret(TextMove::kLeft, false);
  editor.MoveCaret(TextMove::kLeft, true);
  CHECK(editor.SelectedText() == U"b");
  editor.InsertText(U"X");
  CHECK(TextOf(editor) == U"aXc");
  editor.MoveCaret(TextMove::kEnd, false);
  editor.InsertText(U"\r\ndef");  // Any line break ends the paragraph.
  CHECK(TextOf(editor) == U"aXc\ndef");
  CHECK(OnlyText(editor)->story->paragraphs.size() == 2);
  editor.MoveCaret(TextMove::kLineStart, false);
  editor.DeleteBackward();  // Joins the paragraphs.
  CHECK(TextOf(editor) == U"aXcdef");
  editor.MoveCaret(TextMove::kStart, false);
  editor.DeleteForward();
  CHECK(TextOf(editor) == U"Xcdef");
  editor.SelectAllText();
  CHECK(editor.SelectedText() == U"Xcdef");
}

TEST_CASE("Deleting the last character removes the text when editing ends") {
  Editor editor(Empty());
  editor.SetTool(Tool::kType);
  Click(editor, {10, 50});
  editor.InsertText(U"a");
  editor.DeleteBackward();
  editor.EndTextEdit();
  CHECK(!OnlyText(editor));
}

TEST_CASE("The IME's composition shows but is not part of the text until committed") {
  Editor editor(Empty());
  editor.SetTool(Tool::kType);
  Click(editor, {10, 50});
  editor.InsertText(U"a");
  editor.SetPreedit(U"にほん", 3);
  CHECK(ShownTextOf(editor) == U"aにほん");  // Shown, not in the document yet.
  CHECK(TextOf(editor) == U"a");
  CHECK(editor.overlay().text_underlines.size() == 1);
  CHECK(editor.CaretRect().has_value());
  editor.InsertText(U"日本");  // The IME commits.
  CHECK(TextOf(editor) == U"a日本");
  CHECK(editor.overlay().text_underlines.empty());
  CHECK(editor.overlay().text_caret.has_value());
}

TEST_CASE("Clicking text with the type tool edits it; double-clicking with selection too") {
  Editor editor(Empty());
  editor.SetTool(Tool::kType);
  Click(editor, {100, 100});
  editor.InsertText(U"hello world");
  editor.EndTextEdit();
  CHECK(!editor.text_editing());
  // Back in with a double-click on "hello".
  editor.SetTool(Tool::kSelection);
  editor.DoubleClick({101, 96}, kPick);
  CHECK(editor.tool() == Tool::kType);
  CHECK(editor.text_editing());
  editor.SelectWordAt({101, 96});
  CHECK(editor.SelectedText() == U"hello");
}

TEST_CASE("Character and paragraph styles: the selection, the next typing, or whole texts") {
  Editor editor(Empty());
  editor.SetTool(Tool::kType);
  Click(editor, {10, 50});
  editor.InsertText(U"abcd");
  editor.MoveCaret(TextMove::kLeft, true);
  editor.MoveCaret(TextMove::kLeft, true);
  editor.EditCharacterStyle([](core::CharacterStyle& s) { s.size = 30; });
  const core::Story& story = *OnlyText(editor)->story;
  CHECK(core::StyleAt(story, 0).size == 12);
  CHECK(core::StyleAt(story, 2).size == 30);
  CHECK(editor.TextStyle().characters.size() == 1);
  // No selection: applies to what is typed next.
  editor.MoveCaret(TextMove::kEnd, false);
  editor.EditCharacterStyle([](core::CharacterStyle& s) { s.tracking = 50; });
  editor.InsertText(U"e");
  CHECK(core::StyleAt(*OnlyText(editor)->story, 4).tracking == 50);
  editor.EditParagraphStyle([](core::ParagraphStyle& p) { p.align = core::TextAlign::kCenter; });
  CHECK(OnlyText(editor)->story->paragraphs[0].align == core::TextAlign::kCenter);
  // Not editing: the whole selected text.
  editor.EndTextEdit();
  editor.SetTool(Tool::kSelection);
  editor.EditCharacterStyle([](core::CharacterStyle& s) { s.font.style = "Bold"; });
  for (const auto& run : OnlyText(editor)->story->characters) CHECK(run.style.font.style == "Bold");
}

TEST_CASE("Create Outlines keeps the text, and Revert Outlines brings it back") {
  Editor editor(Empty());
  editor.SetTool(Tool::kType);
  Click(editor, {10, 50});
  editor.InsertText(U"永 A");
  editor.EndTextEdit();
  editor.SetTool(Tool::kSelection);
  const std::string text_id = *editor.selection().begin();
  editor.Nudge(5, 0);
  editor.CreateOutlines();
  REQUIRE(editor.selection().size() == 1);
  const auto* group =
      std::get_if<core::GroupObject>(editor.document().FindObject(*editor.selection().begin()));
  REQUIRE(group);
  CHECK(group->children.size() == 2);  // 永 and A; the space has no outline.
  REQUIRE(group->outlined_text);
  CHECK(!OnlyText(editor));  // The kept text is not part of the artwork.
  // The outlines move; the text comes back where they are.
  editor.Nudge(0, 10);
  editor.RevertOutlines();
  const core::TextObject* text = OnlyText(editor);
  REQUIRE(text);
  CHECK(text->common.id == text_id);
  CHECK(text->transform.Map({0, 0}).x == Approx(15));
  CHECK(text->transform.Map({0, 0}).y == Approx(60));
  CHECK(text->story->text == U"永 A");
}

TEST_CASE("Panels used while typing show through, and selecting elsewhere ends editing") {
  Editor editor(Empty());
  editor.SetTool(Tool::kType);
  Click(editor, {10, 50});
  editor.InsertText(U"abc");
  editor.SetPreedit(U"にほ", 2);
  // A fill change while composing reaches the document and stays visible.
  editor.SetFill(core::Color{core::RgbColor{0, 0, 1}});
  const core::Fill* fill = core::FrontFill(core::CommonOf(*OnlyText(editor)).appearance);
  REQUIRE(fill);
  // Diagnostics for a failure seen only on Linux and macOS.
  UNSCOPED_INFO("paint index " << fill->paint.index() << ", items "
                               << core::CommonOf(*OnlyText(editor)).appearance.size()
                               << ", undo '" << editor.history().undo_action() << "'");
  if (const auto* rgb = std::get_if<core::RgbColor>(&fill->paint)) {
    UNSCOPED_INFO("rgb " << rgb->r << " " << rgb->g << " " << rgb->b);
  }
  CHECK(fill->paint == core::Color{core::RgbColor{0, 0, 1}});
  CHECK(TextOf(editor) == U"abc");           // The composition stays out of the document,
  CHECK(ShownTextOf(editor) == U"abcにほ");  // and still shows.
  editor.Select({});
  CHECK(!editor.text_editing());
  CHECK(TextOf(editor) == U"abc");
}
