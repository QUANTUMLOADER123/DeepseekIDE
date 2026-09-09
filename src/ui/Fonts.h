#pragma once

#include <string>

#include "imgui.h"

namespace fonts {

struct Set {
  ImFont* ui = nullptr;        // Inter — весь интерфейс
  ImFont* uiBig = nullptr;     // Inter крупнее — заголовки/логотип
  ImFont* mono = nullptr;      // JetBrains Mono — редактор и код
  ImFont* monoItalic = nullptr;
  ImFont* monoSmall = nullptr; // для мелких подписей
};

Set& G();

// Ищет папку assets относительно exe/cwd/$DEEPSEEKIDE_ASSETS.
std::string FindAssetsDir();

// (Пере)загружает все шрифты. Вызывать ДО ImGui::NewFrame после смены размера.
bool LoadAll(ImGuiIO& io, float scale, int uiSize, int codeSize);

}  // namespace fonts
