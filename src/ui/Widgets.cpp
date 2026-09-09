#include "ui/Widgets.h"

#include <cstdio>
#include <cmath>
#include <cstring>

#include "imgui_internal.h"  // ImGuiWindow, ItemSize, ItemAdd, GImGui, ImRect
#include "ui/Theme.h"

namespace widgets {

bool Spinner(const char* id, float radius, float thickness, ImU32 color) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window->SkipItems) return false;

  ImGuiContext& g = *GImGui;
  const ImGuiID uid = window->GetID(id);
  const ImVec2 pos = window->DC.CursorPos;
  const float size = radius * 2.0f + thickness;
  const ImRect bb(pos, ImVec2(pos.x + size, pos.y + size));
  ImGui::ItemSize(bb);
  if (!ImGui::ItemAdd(bb, uid)) return false;

  if (color == 0) color = theme::Colors().accent;
  ImDrawList* dl = window->DrawList;
  const ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);

  const int segments = 28;
  const float time = (float)g.Time;
  const float start = time * 4.5f;
  const float sweep = IM_PI * 1.35f * (0.55f + 0.45f * sinf(time * 2.1f)) + 0.35f;

  ImVec2 points[64];
  int n = 0;
  for (int i = 0; i < segments && n < 63; ++i) {
    const float a = start + (float)i / (float)(segments - 1) * sweep;
    points[n++] = ImVec2(center.x + cosf(a) * radius, center.y + sinf(a) * radius);
  }
  dl->AddPolyline(points, n, color, ImDrawFlags_None, thickness);
  return true;
}

void Badge(const char* text, ImU32 bg, ImU32 fg) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  ImGuiContext& g = *GImGui;
  ImVec2 textSize = ImGui::CalcTextSize(text);
  ImVec2 pad(7.0f, 3.0f);
  ImVec2 pos = window->DC.CursorPos;
  ImVec2 end(pos.x + textSize.x + pad.x * 2, pos.y + textSize.y + pad.y * 2);
  window->DrawList->AddRectFilled(pos, end, bg, 5.0f);
  ImGui::SetCursorScreenPos(ImVec2(pos.x + pad.x, pos.y + pad.y));
  ImGui::PushStyleColor(ImGuiCol_Text, fg);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  ImGui::SetCursorScreenPos(ImVec2(end.x + g.Style.ItemSpacing.x, pos.y));
  ImGui::Dummy(ImVec2(0, end.y - pos.y));
}

void DimText(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

bool SectionHeader(const char* text, bool* openStorage) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  const float pad = ImGui::GetStyle().ItemSpacing.y * 0.5f;
  ImGui::Dummy(ImVec2(0, pad));
  if (openStorage) {
    char id[128];
    std::snprintf(id, sizeof(id), "##section_%s", text);
    bool open = *openStorage;
    const char* arrow = open ? "▼" : "▶";
    char label[256];
    std::snprintf(label, sizeof(label), "%s  %s", arrow, text);
    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_DontClosePopups)) {
      *openStorage = open = !open;
    }
    ImGui::PopStyleColor();
    return open;
  }
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  return true;
}

void RightSide(float contentWidth, const std::function<void()>& draw) {
  const float avail = ImGui::GetContentRegionAvail().x;
  const float startX = ImGui::GetCursorPosX();
  const float target = startX + avail - contentWidth;
  if (target > startX) ImGui::SetCursorPosX(target);
  ImGui::BeginGroup();
  draw();
  ImGui::EndGroup();
}

bool AccentButton(const char* label, const ImVec2& size) {
  const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(theme::Colors().accent);
  ImGui::PushStyleColor(ImGuiCol_Button, accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(accent.x * 1.15f, accent.y * 1.15f, accent.z * 1.05f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(accent.x * 0.85f, accent.y * 0.85f, accent.z, 1.0f));
  bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(3);
  return pressed;
}

void VerticalSep() {
  ImGui::SameLine(0, 6);
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine(0, 6);
}

}  // namespace widgets
