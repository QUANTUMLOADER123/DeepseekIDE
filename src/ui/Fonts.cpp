#include "ui/Fonts.h"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace fonts {

Set& G() {
  static Set s;
  return s;
}

// Кириллица + латиница + набор символов для интерфейса.
static const ImWchar* GlyphRanges(ImFontAtlas& atlas) {
  static const ImWchar ranges[] = {
      0x0020, 0x00FF,  // Latin
      0x0400, 0x052F,  // Cyrillic
      0x2013, 0x2044,  // тире, кавычки, …
      0x20AC, 0x20AC,  // €
      0x2116, 0x2116,  // №
      0x2190, 0x2199,  // стрелки ← ↑ → ↓
      0x21BA, 0x21BB,  // ↺ ↻
      0x25A0, 0x25FF,  // геометрические фигуры ● ▶ ▼
      0x2713, 0x2713,  // ✓
      0x26A0, 0x26A0,  // ⚠
      0,
  };
  (void)atlas;
  return ranges;
}

std::string FindAssetsDir() {
  if (const char* env = std::getenv("DEEPSEEKIDE_ASSETS")) {
    if (fs::exists(fs::path(env) / "fonts")) return env;
  }
  const char* candidates[] = {"assets", "../assets", "../../assets", "../share/deepseekide/assets",
                              "/usr/local/share/deepseekide/assets", "/usr/share/deepseekide/assets"};
  for (const char* c : candidates)
    if (fs::exists(fs::path(c) / "fonts")) return fs::absolute(fs::path(c)).string();
  return "assets";
}

static ImFont* Load(ImFontAtlas& atlas, const std::string& file, float size) {
  ImFontConfig cfg;
  cfg.OversampleH = 3;
  cfg.OversampleV = 2;
  cfg.PixelSnapH = true;
  std::snprintf(cfg.Name, sizeof(cfg.Name), "%s, %.0fpx", file.c_str(), size);
  return atlas.AddFontFromFileTTF(file.c_str(), size, &cfg, GlyphRanges(atlas));
}

bool LoadAll(ImGuiIO& io, float scale, int uiSize, int codeSize) {
  const std::string dir = FindAssetsDir();
  auto& f = G();

  io.Fonts->Clear();

  f.ui = Load(*io.Fonts, dir + "/fonts/Inter-Regular.ttf", (float)uiSize * scale);
  f.uiBig = Load(*io.Fonts, dir + "/fonts/Inter-Regular.ttf", (float)uiSize * 1.45f * scale);
  f.mono = Load(*io.Fonts, dir + "/fonts/JetBrainsMono-Regular.ttf", (float)codeSize * scale);
  f.monoItalic = Load(*io.Fonts, dir + "/fonts/JetBrainsMono-Italic.ttf", (float)codeSize * scale);
  f.monoSmall = Load(*io.Fonts, dir + "/fonts/JetBrainsMono-Regular.ttf", (float)codeSize * 0.82f * scale);

  if (!f.ui) {
    // Файлы не найдены — встроенный шрифт без кириллицы лучше, чем падение.
    f.ui = io.Fonts->AddFontDefault();
    f.uiBig = f.ui;
    if (!f.mono) {
      f.mono = f.ui;
      f.monoItalic = f.ui;
      f.monoSmall = f.ui;
    }
    io.FontDefault = f.ui;
    return false;
  }
  io.FontDefault = f.ui;
  return io.Fonts->Build();
}

}  // namespace fonts
