#include "ui/StatusBar.h"

#include <cstdio>

#include "imgui.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

void StatusBar::SetStatus(const std::string& s) {
  std::lock_guard<std::mutex> lk(mMtx);
  mStatus = s;
}

std::string StatusBar::Status() const {
  std::lock_guard<std::mutex> lk(mMtx);
  return mStatus;
}

float StatusBar::Render(float fps) {
  const float h = ImGui::GetFrameHeight() + 8.0f;
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
  ImGui::SetNextWindowViewport(vp->ID);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
  ImGui::Begin("##statusbar", nullptr, flags);
  ImGui::PopStyleVar(2);

  const auto& C = theme::Colors();
  bool open = mCb.projectOpen && mCb.projectOpen();

  // Проект
  widgets::Badge(open ? "● проект" : "○ нет проекта", open ? C.ok : C.textDim);
  ImGui::SameLine();
  if (open) {
    std::string p = mCb.projectPath();
    if (p.size() > 60) p = "…" + p.substr(p.size() - 59);
    ImGui::TextDisabled("%s", p.c_str());
    ImGui::SameLine();
  }
  widgets::VerticalSep();

  // Статус агента
  bool busy = mCb.agentBusy && mCb.agentBusy();
  if (busy) {
    widgets::Spinner("##sb_spin", 6.0f, 2.0f);
    ImGui::SameLine();
  }
  ImGui::TextDisabled("%s", Status().c_str());
  ImGui::SameLine();

  // Справа: модель + fps
  std::string model = mCb.modelName ? mCb.modelName() : "";
  char right[160];
  std::snprintf(right, sizeof(right), "UTF-8  |  %s  |  %.0f fps", model.c_str(), fps);
  float w = ImGui::CalcTextSize(right).x + 8;
  widgets::RightSide(w, [&] {
    ImGui::TextUnformatted(right);
  });

  ImGui::End();
  return h;
}
