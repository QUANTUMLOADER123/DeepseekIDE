#pragma once

#include <filesystem>
#include <string>

// Глобальные настройки DeepSeekIDE. Хранятся в JSON:
//   Linux/macOS: ~/.deepseekide/settings.json (или ~/Library/Application Support)
//   Windows:     %APPDATA%/DeepSeekIDE/settings.json
struct Settings {
  // --- DeepSeek API ---
  std::string apiKey;                               // ключ с platform.deepseek.com
  std::string baseUrl = "https://api.deepseek.com"; // совместим с OpenAI-форматом
  std::string model   = "deepseek-chat";            // deepseek-chat | deepseek-reasoner
  float temperature   = 0.3f;
  int   maxSteps      = 32;                         // лимит итераций агента за одну задачу

  // --- Агент ---
  bool allowShell     = false;  // разрешить инструменту run_command выполнять команды
  int  shellTimeout   = 60;     // сек
  bool autoSnapshot   = true;   // автоматически снимать копии файлов перед изменениями

  // --- Внешний вид ---
  int  theme          = 0;      // 0 = DeepSeek Dark, 1 = Nord, 2 = Light
  int  uiFontSize     = 17;     // px (до масштабирования DPI)
  int  codeFontSize   = 16;
  int  editorTabSize  = 4;
  bool showWhitespace = false;

  // --- Последний проект ---
  std::string lastProject;

  static std::filesystem::path SettingsPath();
  static Settings Load();
  bool Save() const;
};
