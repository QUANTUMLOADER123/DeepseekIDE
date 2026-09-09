#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

struct Settings;

// Модальный выбор папки проекта (чистый ImGui, без платформенных диалогов).
class FolderPickerDialog {
public:
  void Open(const std::filesystem::path& startDir);
  // Рисовать каждый кадр. Вернёт true один раз — папка выбрана, см. Result().
  bool Render();
  const std::filesystem::path& Result() const { return mResult; }

private:
  void Scan();
  void GoUp();

  bool mOpen = false;
  bool mJustOpened = false;
  std::filesystem::path mCur;
  std::filesystem::path mResult;
  std::vector<std::pair<std::string, bool>> mEntries;  // (имя, isDir)
  std::string mError;
  char mPathBuf[1024]{};
  int mSelected = -1;
};

// Модальные настройки: API-ключ, модель, внешний вид, безопасность.
class SettingsDialog {
public:
  struct Callbacks {
    std::function<void()> applyTheme;
    std::function<void()> rebuildFonts;
    std::function<void()> applyEditorStyle;
  };

  void Open(Settings* settings, Callbacks cb);
  bool Render();  // true — настройки изменены и сохранены

private:
  Settings* m = nullptr;
  Callbacks mCb;
  bool mOpen = false;
  bool mJustOpened = false;
  // Локальная копия редактируемых полей (применяются по «Сохранить»).
  char mApiKey[256]{};
  char mBaseUrl[256]{};
  char mModel[128]{};
  float mTemp = 0.3f;
  int mMaxSteps = 32;
  bool mAllowShell = false;
  int mShellTimeout = 60;
  bool mAutoSnapshot = true;
  int mTheme = 0;
  int mUiFont = 17;
  int mCodeFont = 16;
  int mTabSize = 4;
  bool mShowWs = false;
  bool mKeyVisible = false;
};

// «О программе».
class AboutDialog {
public:
  void Open() { mOpen = true; mJustOpened = true; }
  void Render();

private:
  bool mOpen = false;
  bool mJustOpened = false;
};
