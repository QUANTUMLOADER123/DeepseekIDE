#include "ui/SnapshotsPanel.h"

#include "app/Platform.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

void SnapshotsPanel::Render() {
  if (ImGui::Begin("Снимки и откат")) {
    const auto& C = theme::Colors();

    if (widgets::AccentButton("↺  Откатить последнее действие", ImVec2(-1, 0)))
      mCb.rollbackLast();
    ImGui::SameLine();

    if (ImGui::Button("Обновить")) mDirty = true;
    ImGui::Separator();

    if (mCb.hasProject && !mCb.hasProject()) {
      widgets::DimText("Откройте папку проекта — здесь появится история изменений,\n"
                       "сделанных агентом. Каждое действие откатывается одной кнопкой.");
    } else {
      if (mDirty) {
        mCache = mSnaps->List();
        mDirty = false;
      }
      if (mCache.empty()) {
        widgets::DimText("Пока пусто. Как только агент изменит файлы,\n"
                         "здесь появится запись с возможностью отката.");
      }

      ImGui::BeginChild("##snap_list", ImVec2(0, 0), false);
      int idx = 0;
      for (const auto& s : mCache) {
        ImGui::PushID(s.id.c_str());

        // Карточка снимка
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(C.aiBubble));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::BeginChild(ImGui::GetID("card"), ImVec2(0, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

        ImGui::PushStyleColor(ImGuiCol_Text, C.accentSoft);
        ImGui::Text("%s", platform::FormatMillis(s.epochMs).c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", platform::TimeAgo(s.epochMs).c_str());

        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX());
        ImGui::TextWrapped("%s", s.title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TextDisabled("файлов: %d", (int)s.files.size());

        if (!s.files.empty() && widgets::SectionHeader("ФАЙЛЫ", &mOpenFiles)) {
          if (fonts::G().monoSmall) ImGui::PushFont(fonts::G().monoSmall);
          int shown = 0;
          for (const auto& f : s.files) {
            if (++shown > 10) {
              ImGui::TextDisabled("…и ещё %d", (int)s.files.size() - 10);
              break;
            }
            ImGui::Bullet();
            ImGui::TextUnformatted(f.path.c_str());
            if (!f.existed) {
              ImGui::SameLine();
              ImGui::TextDisabled("(новый)");
            }
          }
          if (fonts::G().monoSmall) ImGui::PopFont();
        }

        ImGui::Dummy(ImVec2(0, 2));
        char btnLabel[128];
        std::snprintf(btnLabel, sizeof(btnLabel), "← Откатить сюда%s",
                      idx == 0 ? "" : " (включая все новые)");
        if (ImGui::SmallButton(btnLabel)) mCb.rollbackThrough(s.id);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Восстановить файлы из этого снимка и откатить все действия новее него");
        ImGui::SameLine();
        if (ImGui::SmallButton("Удалить запись")) {
          mCb.forget(s.id);
          mDirty = true;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::PopID();
        ++idx;
      }
      ImGui::EndChild();
    }
  }
  ImGui::End();
}
