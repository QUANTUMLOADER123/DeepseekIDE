#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "TextEditor.h"  // santaclose ImGuiColorTextEdit

// Открытая вкладка редактора.
struct EditorTab {
  std::filesystem::path absPath;
  std::string title;        // "src/main.cpp"
  TextEditor editor;
  int savedUndoIndex = 0;   // состояние «сохранено»
  bool changedOnDisk = false; // изменён снаружи (агентом) при наличии локальных правок

  bool Dirty() const { return editor.GetUndoIndex() != savedUndoIndex; }
};

// Менеджер вкладок редактора (GUI-слой).
class EditorManager {
public:
  std::vector<EditorTab> tabs;
  int active = -1;
  int closeRequest = -1;

  void Open(const std::filesystem::path& abs, const std::filesystem::path& root);
  int Find(const std::filesystem::path& abs) const;
  void Save(int idx, std::string* err = nullptr);
  void SaveAll(bool onlyDirty = true);
  bool AnyDirty() const;
  int DirtyCount() const;

  // Файл изменился извне (агент). Если вкладка не «грязная» — перечитываем.
  void OnExternalChange(const std::filesystem::path& abs);

  void ApplyStyles(int tabSize, bool showWhitespace);
  void RequestClose(int idx) { closeRequest = idx; }

  static TextEditor::LanguageDefinitionId LanguageFor(const std::filesystem::path& p);
  static const char* LanguageName(const std::filesystem::path& p);
};
