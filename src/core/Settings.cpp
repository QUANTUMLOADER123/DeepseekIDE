#include "core/Settings.h"

#include <nlohmann/json.hpp>

#include "app/Platform.h"

std::filesystem::path Settings::SettingsPath() {
  return platform::ConfigDir() / "settings.json";
}

Settings Settings::Load() {
  Settings s;
  std::string text;
  if (!platform::ReadTextFile(SettingsPath(), text)) return s;
  try {
    auto j = nlohmann::json::parse(text);
    s.apiKey       = j.value("api_key", s.apiKey);
    s.baseUrl      = j.value("base_url", s.baseUrl);
    s.model        = j.value("model", s.model);
    s.temperature  = j.value("temperature", s.temperature);
    s.maxSteps     = j.value("max_steps", s.maxSteps);
    s.allowShell   = j.value("allow_shell", s.allowShell);
    s.shellTimeout = j.value("shell_timeout", s.shellTimeout);
    s.autoSnapshot = j.value("auto_snapshot", s.autoSnapshot);
    s.theme        = j.value("theme", s.theme);
    s.uiFontSize   = j.value("ui_font_size", s.uiFontSize);
    s.codeFontSize = j.value("code_font_size", s.codeFontSize);
    s.editorTabSize = j.value("editor_tab_size", s.editorTabSize);
    s.showWhitespace = j.value("show_whitespace", s.showWhitespace);
    s.chatSplit    = j.value("chat_split", s.chatSplit);
    s.lastProject  = j.value("last_project", s.lastProject);
  } catch (...) {
    // битый конфиг — работаем с дефолтами
  }
  return s;
}

bool Settings::Save() const {
  nlohmann::json j;
  j["api_key"]        = apiKey;
  j["base_url"]       = baseUrl;
  j["model"]          = model;
  j["temperature"]    = temperature;
  j["max_steps"]      = maxSteps;
  j["allow_shell"]    = allowShell;
  j["shell_timeout"]  = shellTimeout;
  j["auto_snapshot"]  = autoSnapshot;
  j["theme"]          = theme;
  j["ui_font_size"]   = uiFontSize;
  j["code_font_size"] = codeFontSize;
  j["editor_tab_size"] = editorTabSize;
  j["show_whitespace"] = showWhitespace;
  j["chat_split"]       = chatSplit;
  j["last_project"]   = lastProject;
  return platform::WriteTextFile(SettingsPath(), j.dump(2));
}
