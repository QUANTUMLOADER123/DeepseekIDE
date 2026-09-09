#pragma once

class EditorManager;

// Центральная панель — вкладки кода (ImGuiColorTextEdit) + поиск по файлу (Ctrl+F).
class EditorPanel {
public:
  void SetContext(EditorManager* ed) { mEd = ed; }
  void Render();

private:
  void StatusLine();
  void FindBar(int tabIdx);

  EditorManager* mEd = nullptr;
  bool mFindOpen = false;
  char mFindBuf[256]{};
  int mFindTabActive = -1;
};
