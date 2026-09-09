#pragma once

#include <functional>

#include "imgui.h"

namespace widgets {

// Крутящийся индикатор загрузки.
bool Spinner(const char* id, float radius = 8.0f, float thickness = 2.5f, ImU32 color = 0);

// Цветной бейдж (закруглённый прямоугольник с текстом).
void Badge(const char* text, ImU32 bg, ImU32 fg = IM_COL32(255, 255, 255, 255));

// Мелкий серый текст-подпись.
void DimText(const char* text);

// Заголовок секции КАПСОМ (как в VS Code).
bool SectionHeader(const char* text, bool* openStorage = nullptr);

// Рисует блок в правой части строки (нужна оценка его ширины).
void RightSide(float contentWidth, const std::function<void()>& draw);

// Кнопка с акцентным фоном.
bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0));

// Разделитель + отступы по красоте.
void VerticalSep();

}  // namespace widgets
