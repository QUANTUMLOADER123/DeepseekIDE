#include "ui/Theme.h"

namespace theme {

const char* Name(Id id) { return Name(static_cast<int>(id)); }

const char* Name(int id) {
  switch (id) {
    case 0: return "DeepSeek Dark";
    case 1: return "Nord";
    case 2: return "Light";
    default: return "?";
  }
}

static void ApplyMetrics(ImGuiStyle& s) {
  s.WindowPadding = ImVec2(12, 10);
  s.FramePadding = ImVec2(10, 6);
  s.CellPadding = ImVec2(8, 4);
  s.ItemSpacing = ImVec2(10, 8);
  s.ItemInnerSpacing = ImVec2(8, 6);
  s.IndentSpacing = 18.0f;
  s.ScrollbarSize = 13.0f;
  s.GrabMinSize = 12.0f;
  s.WindowBorderSize = 1.0f;
  s.ChildBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  s.FrameBorderSize = 0.0f;
  s.TabBorderSize = 0.0f;
  s.WindowRounding = 10.0f;
  s.ChildRounding = 6.0f;
  s.FrameRounding = 6.0f;
  s.PopupRounding = 8.0f;
  s.ScrollbarRounding = 8.0f;
  s.GrabRounding = 6.0f;
  s.TabRounding = 6.0f;
  s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
  s.WindowMenuButtonPosition = ImGuiDir_None;
  s.ColorButtonPosition = ImGuiDir_Right;
  s.SeparatorTextBorderSize = 2.0f;
  s.DockingSeparatorSize = 2;
}

static ImVec4 RGBA(int r, int g, int b, float a = 1.0f) {
  return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

static void ApplyDeepSeekDark(ImGuiStyle& style) {
  ImVec4* c = style.Colors;
  const ImVec4 accent = RGBA(0x4D, 0x6B, 0xFE);       // фирменный синий DeepSeek
  const ImVec4 accentHover = RGBA(0x62, 0x7D, 0xFF);
  const ImVec4 accentActive = RGBA(0x3A, 0x58, 0xE8);
  const ImVec4 bg = RGBA(0x0B, 0x0E, 0x17);
  const ImVec4 bgPanel = RGBA(0x10, 0x14, 0x20);
  const ImVec4 bgChild = RGBA(0x0D, 0x11, 0x1C);
  const ImVec4 bgWidget = RGBA(0x17, 0x1C, 0x2E);
  const ImVec4 bgWidgetHover = RGBA(0x1E, 0x24, 0x3A);
  const ImVec4 text = RGBA(0xE6, 0xE9, 0xF2);
  const ImVec4 textDim = RGBA(0x8A, 0x93, 0xAC);
  const ImVec4 border = RGBA(0x2A, 0x31, 0x4A);

  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = textDim;
  c[ImGuiCol_WindowBg] = bg;
  c[ImGuiCol_ChildBg] = bgChild;
  c[ImGuiCol_PopupBg] = RGBA(0x12, 0x16, 0x26, 0.98f);
  c[ImGuiCol_Border] = border;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0.5f);
  c[ImGuiCol_FrameBg] = bgWidget;
  c[ImGuiCol_FrameBgHovered] = bgWidgetHover;
  c[ImGuiCol_FrameBgActive] = RGBA(0x24, 0x2C, 0x46);
  c[ImGuiCol_TitleBg] = RGBA(0x0A, 0x0D, 0x16);
  c[ImGuiCol_TitleBgActive] = RGBA(0x13, 0x18, 0x2C);
  c[ImGuiCol_TitleBgCollapsed] = RGBA(0x0A, 0x0D, 0x16, 0.8f);
  c[ImGuiCol_MenuBarBg] = RGBA(0x0C, 0x10, 0x1B);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.2f);
  c[ImGuiCol_ScrollbarGrab] = RGBA(0x2C, 0x34, 0x50);
  c[ImGuiCol_ScrollbarGrabHovered] = RGBA(0x3A, 0x44, 0x66);
  c[ImGuiCol_ScrollbarGrabActive] = RGBA(0x47, 0x52, 0x7A);
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accentActive;
  c[ImGuiCol_Button] = RGBA(0x1B, 0x21, 0x36);
  c[ImGuiCol_ButtonHovered] = bgWidgetHover;
  c[ImGuiCol_ButtonActive] = RGBA(0x26, 0x2E, 0x4A);
  c[ImGuiCol_Header] = RGBA(0x1A, 0x20, 0x34);
  c[ImGuiCol_HeaderHovered] = RGBA(0x22, 0x29, 0x42);
  c[ImGuiCol_HeaderActive] = RGBA(0x2A, 0x33, 0x52);
  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_SeparatorHovered] = accentHover;
  c[ImGuiCol_SeparatorActive] = accent;
  c[ImGuiCol_ResizeGrip] = RGBA(0x4D, 0x6B, 0xFE, 0.25f);
  c[ImGuiCol_ResizeGripHovered] = RGBA(0x4D, 0x6B, 0xFE, 0.5f);
  c[ImGuiCol_ResizeGripActive] = RGBA(0x4D, 0x6B, 0xFE, 0.8f);
  c[ImGuiCol_Tab] = RGBA(0x11, 0x15, 0x24);
  c[ImGuiCol_TabHovered] = RGBA(0x24, 0x2B, 0x46);
  c[ImGuiCol_TabActive] = RGBA(0x1B, 0x22, 0x3A);
  c[ImGuiCol_TabUnfocused] = RGBA(0x0E, 0x12, 0x1F);
  c[ImGuiCol_TabUnfocusedActive] = RGBA(0x16, 0x1B, 0x2D);
  c[ImGuiCol_TabSelectedOverline] = accent;
  c[ImGuiCol_DockingPreview] = RGBA(0x4D, 0x6B, 0xFE, 0.45f);
  c[ImGuiCol_DockingEmptyBg] = RGBA(0x05, 0x06, 0x0A);
  c[ImGuiCol_PlotLines] = accent;
  c[ImGuiCol_PlotLinesHovered] = accentHover;
  c[ImGuiCol_PlotHistogram] = accent;
  c[ImGuiCol_PlotHistogramHovered] = accentHover;
  c[ImGuiCol_TableHeaderBg] = RGBA(0x14, 0x18, 0x28);
  c[ImGuiCol_TableBorderStrong] = border;
  c[ImGuiCol_TableBorderLight] = RGBA(0x1F, 0x25, 0x3A);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = RGBA(0xFF, 0xFF, 0xFF, 0.02f);
  c[ImGuiCol_TextLink] = accentHover;
  c[ImGuiCol_TextSelectedBg] = RGBA(0x4D, 0x6B, 0xFE, 0.35f);
  c[ImGuiCol_DragDropTarget] = accent;
  c[ImGuiCol_NavHighlight] = accent;
  c[ImGuiCol_NavWindowingHighlight] = RGBA(0xFF, 0xFF, 0xFF, 0.7f);
  c[ImGuiCol_NavWindowingDimBg] = RGBA(0x00, 0x00, 0x00, 0.55f);
  c[ImGuiCol_ModalWindowDimBg] = RGBA(0x02, 0x03, 0x06, 0.62f);
}

static void ApplyNord(ImGuiStyle& style) {
  ImVec4* c = style.Colors;
  const ImVec4 accent = RGBA(0x88, 0xC0, 0xD0);
  const ImVec4 accentHover = RGBA(0x8F, 0xBC, 0xBB);
  const ImVec4 bg = RGBA(0x2E, 0x34, 0x40);
  const ImVec4 bgPanel = RGBA(0x3B, 0x42, 0x52);
  const ImVec4 bgWidget = RGBA(0x43, 0x4C, 0x5E);
  const ImVec4 text = RGBA(0xEC, 0xEF, 0xF4);

  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = RGBA(0x7B, 0x88, 0xA1);
  c[ImGuiCol_WindowBg] = bg;
  c[ImGuiCol_ChildBg] = RGBA(0x33, 0x3A, 0x47);
  c[ImGuiCol_PopupBg] = RGBA(0x3B, 0x42, 0x52, 0.98f);
  c[ImGuiCol_Border] = RGBA(0x4C, 0x56, 0x6A);
  c[ImGuiCol_FrameBg] = bgWidget;
  c[ImGuiCol_FrameBgHovered] = RGBA(0x4C, 0x56, 0x6A);
  c[ImGuiCol_FrameBgActive] = RGBA(0x5E, 0x6A, 0x82);
  c[ImGuiCol_TitleBg] = RGBA(0x2B, 0x30, 0x3A);
  c[ImGuiCol_TitleBgActive] = bgPanel;
  c[ImGuiCol_MenuBarBg] = bgPanel;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.2f);
  c[ImGuiCol_ScrollbarGrab] = RGBA(0x4C, 0x56, 0x6A);
  c[ImGuiCol_ScrollbarGrabHovered] = RGBA(0x5E, 0x6A, 0x82);
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_Button] = bgWidget;
  c[ImGuiCol_ButtonHovered] = RGBA(0x4C, 0x56, 0x6A);
  c[ImGuiCol_ButtonActive] = RGBA(0x5E, 0x6A, 0x82);
  c[ImGuiCol_Header] = bgWidget;
  c[ImGuiCol_HeaderHovered] = RGBA(0x4C, 0x56, 0x6A);
  c[ImGuiCol_HeaderActive] = RGBA(0x5E, 0x6A, 0x82);
  c[ImGuiCol_Separator] = RGBA(0x4C, 0x56, 0x6A);
  c[ImGuiCol_Tab] = RGBA(0x34, 0x3B, 0x49);
  c[ImGuiCol_TabHovered] = RGBA(0x4C, 0x56, 0x6A);
  c[ImGuiCol_TabActive] = bgPanel;
  c[ImGuiCol_TabUnfocused] = RGBA(0x2E, 0x34, 0x40);
  c[ImGuiCol_TabUnfocusedActive] = RGBA(0x38, 0x3F, 0x4D);
  c[ImGuiCol_TabSelectedOverline] = accent;
  c[ImGuiCol_DockingPreview] = RGBA(0x88, 0xC0, 0xD0, 0.4f);
  c[ImGuiCol_DockingEmptyBg] = RGBA(0x24, 0x29, 0x32);
  c[ImGuiCol_TextLink] = accentHover;
  c[ImGuiCol_TextSelectedBg] = RGBA(0x88, 0xC0, 0xD0, 0.35f);
  c[ImGuiCol_ModalWindowDimBg] = RGBA(0x1A, 0x1E, 0x25, 0.65f);
}

static void ApplyLight(ImGuiStyle& style) {
  ImGui::StyleColorsLight(&style);
  ImVec4* c = style.Colors;
  const ImVec4 accent = RGBA(0x3A, 0x58, 0xE8);
  c[ImGuiCol_WindowBg] = RGBA(0xF5, 0xF6, 0xFA);
  c[ImGuiCol_ChildBg] = RGBA(0xF0, 0xF1, 0xF7);
  c[ImGuiCol_PopupBg] = RGBA(0xFF, 0xFF, 0xFF, 0.98f);
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_Tab] = RGBA(0xE4, 0xE6, 0xF0);
  c[ImGuiCol_TabActive] = RGBA(0xFF, 0xFF, 0xFF);
  c[ImGuiCol_TabHovered] = RGBA(0xEE, 0xF0, 0xF9);
  c[ImGuiCol_TabSelectedOverline] = accent;
  c[ImGuiCol_DockingPreview] = RGBA(0x3A, 0x58, 0xE8, 0.35f);
  c[ImGuiCol_TextSelectedBg] = RGBA(0x3A, 0x58, 0xE8, 0.25f);
  c[ImGuiCol_TextLink] = accent;
}

void Apply(int id) {
  ImGuiStyle& style = ImGui::GetStyle();
  switch (id) {
    case 1: ImGui::StyleColorsDark(&style); ApplyNord(style); break;
    case 2: ApplyLight(style); break;
    default: ImGui::StyleColorsDark(&style); ApplyDeepSeekDark(style); break;
  }
  ApplyMetrics(style);
}

const Palette& Colors() {
  static Palette p = [] {
    Palette q;
    auto U = [](int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); };
    q.accent = U(0x4D, 0x6B, 0xFE);
    q.accentSoft = U(0x7C, 0x92, 0xFF);
    q.accentBg = U(0x4D, 0x6B, 0xFE, 40);
    q.ok = U(0x3F, 0xB9, 0x50);
    q.warn = U(0xD2, 0x99, 0x22);
    q.error = U(0xF8, 0x51, 0x49);
    q.textDim = U(0x8A, 0x93, 0xAC);
    q.userBubble = U(0x26, 0x30, 0x55, 240);
    q.userBubbleBorder = U(0x4D, 0x6B, 0xFE, 120);
    q.aiBubble = U(0x14, 0x19, 0x29, 240);
    q.aiBubbleBorder = U(0x2A, 0x31, 0x4A);
    q.toolBubble = U(0x12, 0x16, 0x24, 200);
    q.codeBg = U(0x0A, 0x0D, 0x16);
    return q;
  }();
  return p;
}

}  // namespace theme
