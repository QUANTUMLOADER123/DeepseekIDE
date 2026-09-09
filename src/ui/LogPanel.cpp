#include "ui/LogPanel.h"

#include "app/Platform.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"

void LogPanel::Add(const std::string& level, const std::string& msg) {
  std::lock_guard<std::mutex> lk(mMtx);
  mLines.push_back({level, platform::NowHMS(), msg});
  if (mLines.size() > 2000) {
    mLines.pop_front();
    if (mRendered > 0) --mRendered;
  }
}

void LogPanel::Clear() {
  std::lock_guard<std::mutex> lk(mMtx);
  mLines.clear();
  mRendered = 0;
  mUi.clear();
}

void LogPanel::Render() {
  if (ImGui::Begin("Журнал")) {
    ImGui::Checkbox("Инфо", &mShowInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Инструменты", &mShowTool);
    ImGui::SameLine();
    ImGui::Checkbox("Команды", &mShowCmd);
    ImGui::SameLine();
    ImGui::Checkbox("Ошибки", &mShowError);
    ImGui::SameLine();
    ImGui::Checkbox("Автопрокрутка", &mAutoScroll);
    ImGui::SameLine();
    if (ImGui::SmallButton("Очистить")) Clear();
    ImGui::Separator();

    // Забираем новые записи
    {
      std::lock_guard<std::mutex> lk(mMtx);
      while (mRendered < mLines.size()) mUi.push_back(mLines[mRendered++]);
      while (mUi.size() > 2000) mUi.pop_front();
    }

    const auto& C = theme::Colors();
    if (fonts::G().monoSmall) ImGui::PushFont(fonts::G().monoSmall);
    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& l : mUi) {
      bool show = (l.level == "info" && mShowInfo) || (l.level == "tool" && mShowTool) ||
                  (l.level == "cmd" && mShowCmd) || (l.level == "error" && mShowError) ||
                  (l.level == "warn" && mShowError);
      if (!show) continue;

      ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
      const char* tag = " i ";
      if (l.level == "tool") { color = C.accentSoft; tag = " ⚙ "; }
      else if (l.level == "cmd") { color = C.ok; tag = " $ "; }
      else if (l.level == "error") { color = C.error; tag = " ✗ "; }
      else if (l.level == "warn") { color = C.warn; tag = " ⚠ "; }

      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::TextUnformatted((l.time + tag + l.text).c_str());
      ImGui::PopStyleColor();
    }
    if (mAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8)
      ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    if (fonts::G().monoSmall) ImGui::PopFont();
  }
  ImGui::End();
}
