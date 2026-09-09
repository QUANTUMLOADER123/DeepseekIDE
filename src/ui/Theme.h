#pragma once

#include "imgui.h"

namespace theme {

enum class Id { DeepSeekDark = 0, Nord = 1, Light = 2, Count };

const char* Name(Id id);
const char* Name(int id);

void Apply(int id);  // применяет цвета + метрики к текущему стилю

// Палитра фирменных цветов для панелей (чат, статус-бар и т.д.).
struct Palette {
  ImU32 accent;
  ImU32 accentSoft;
  ImU32 accentBg;
  ImU32 ok;
  ImU32 warn;
  ImU32 error;
  ImU32 textDim;
  ImU32 userBubble;
  ImU32 userBubbleBorder;
  ImU32 aiBubble;
  ImU32 aiBubbleBorder;
  ImU32 toolBubble;
  ImU32 codeBg;
};
const Palette& Colors();

}  // namespace theme
