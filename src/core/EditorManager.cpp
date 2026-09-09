#include "core/EditorManager.h"

#include <algorithm>
#include <cctype>

#include "app/Platform.h"

void EditorManager::Open(const std::filesystem::path& abs, const std::filesystem::path& root) {
  int existing = Find(abs);
  if (existing >= 0) {
    active = existing;
    return;
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(abs, ec)) return;
  auto size = std::filesystem::file_size(abs, ec);
  if (!ec && size > 5 * 1024 * 1024) return;  // слишком большой для живого редактора

  EditorTab tab;
  tab.absPath = abs;
  std::string rel = platform::PathToStr(std::filesystem::relative(abs, root, ec));
  if (ec) rel = platform::PathToStr(abs.filename());
  std::replace(rel.begin(), rel.end(), '\\', '/');
  tab.title = rel;

  std::string text;
  platform::ReadTextFile(abs, text);
  tab.editor.SetText(text);
  tab.editor.SetLanguageDefinition(LanguageFor(abs));
  tab.editor.SetPalette(TextEditor::PaletteId::Dark);
  tab.editor.SetShowLineNumbersEnabled(true);
  tab.savedUndoIndex = tab.editor.GetUndoIndex();

  tabs.push_back(std::move(tab));
  active = static_cast<int>(tabs.size()) - 1;
}

int EditorManager::Find(const std::filesystem::path& abs) const {
  for (size_t i = 0; i < tabs.size(); ++i) {
    std::error_code ec;
    if (std::filesystem::equivalent(tabs[i].absPath, abs, ec)) return static_cast<int>(i);
  }
  return -1;
}

void EditorManager::Save(int idx, std::string* err) {
  if (idx < 0 || idx >= static_cast<int>(tabs.size())) return;
  auto& tab = tabs[idx];
  std::string werr;
  if (!platform::WriteTextFile(tab.absPath, tab.editor.GetText(), &werr)) {
    if (err) *err = werr;
    return;
  }
  tab.savedUndoIndex = tab.editor.GetUndoIndex();
  tab.changedOnDisk = false;
}

void EditorManager::SaveAll(bool onlyDirty) {
  for (size_t i = 0; i < tabs.size(); ++i)
    if (!onlyDirty || tabs[i].Dirty()) Save(static_cast<int>(i));
}

bool EditorManager::AnyDirty() const { return DirtyCount() > 0; }

int EditorManager::DirtyCount() const {
  int n = 0;
  for (const auto& t : tabs)
    if (t.Dirty()) ++n;
  return n;
}

void EditorManager::OnExternalChange(const std::filesystem::path& abs) {
  int idx = Find(abs);
  if (idx < 0) return;
  auto& tab = tabs[idx];
  if (!tab.Dirty()) {
    // Локальных правок нет — перечитываем, сохраняя позицию курсора.
    int line = 0, col = 0;
    tab.editor.GetCursorPosition(line, col);
    std::string text;
    if (platform::ReadTextFile(abs, text)) {
      tab.editor.SetText(text);
      int total = static_cast<int>(tab.editor.GetTextLines().size());
      int cl = std::clamp(line, 0, total > 0 ? total - 1 : 0);
      tab.editor.SetCursorPosition(cl, 0);
      tab.editor.SetViewAtLine(line, TextEditor::SetViewAtLineMode::Centered);
      tab.savedUndoIndex = tab.editor.GetUndoIndex();
    }
  } else {
    tab.changedOnDisk = true;  // покажем предупреждение пользователю
  }
}

void EditorManager::ApplyStyles(int tabSize, bool showWhitespace) {
  for (auto& t : tabs) {
    t.editor.SetTabSize(tabSize);
    t.editor.SetShowWhitespacesEnabled(showWhitespace);
  }
}

TextEditor::LanguageDefinitionId EditorManager::LanguageFor(const std::filesystem::path& p) {
  std::string ext = platform::PathToStr(p.extension());
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  using L = TextEditor::LanguageDefinitionId;
  if (ext == ".cpp" || ext == ".cxx" || ext == ".cc" || ext == ".c++" || ext == ".h" ||
      ext == ".hpp" || ext == ".hh" || ext == ".ino")
    return L::Cpp;
  if (ext == ".c") return L::C;
  if (ext == ".cs") return L::Cs;
  if (ext == ".py") return L::Python;
  if (ext == ".lua") return L::Lua;
  if (ext == ".json") return L::Json;
  if (ext == ".sql") return L::Sql;
  if (ext == ".glsl" || ext == ".vert" || ext == ".frag") return L::Glsl;
  if (ext == ".hlsl") return L::Hlsl;
  if (ext == ".as" || ext == ".angelscript") return L::AngelScript;
  return L::None;
}

const char* EditorManager::LanguageName(const std::filesystem::path& p) {
  switch (LanguageFor(p)) {
    case TextEditor::LanguageDefinitionId::Cpp: return "C++";
    case TextEditor::LanguageDefinitionId::C: return "C";
    case TextEditor::LanguageDefinitionId::Cs: return "C#";
    case TextEditor::LanguageDefinitionId::Python: return "Python";
    case TextEditor::LanguageDefinitionId::Lua: return "Lua";
    case TextEditor::LanguageDefinitionId::Json: return "JSON";
    case TextEditor::LanguageDefinitionId::Sql: return "SQL";
    case TextEditor::LanguageDefinitionId::Glsl: return "GLSL";
    case TextEditor::LanguageDefinitionId::Hlsl: return "HLSL";
    case TextEditor::LanguageDefinitionId::AngelScript: return "AngelScript";
    default: return "Текст";
  }
}
