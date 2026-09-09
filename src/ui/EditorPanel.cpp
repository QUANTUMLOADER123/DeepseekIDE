#include "ui/EditorPanel.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "app/Platform.h"
#include "core/EditorManager.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace {

std::string LowerCopy(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

}  // namespace

void EditorPanel::Render() {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (ImGui::Begin("Редактор", nullptr, flags)) {
    if (mEd->tabs.empty()) {
      // Экран-приветствие
      ImVec2 avail = ImGui::GetContentRegionAvail();
      ImGui::Dummy(ImVec2(0, avail.y * 0.28f));
      if (fonts::G().uiBig) ImGui::PushFont(fonts::G().uiBig);
      float w = ImGui::CalcTextSize("DeepSeekIDE").x;
      ImGui::SetCursorPosX((avail.x - w) * 0.5f + ImGui::GetStyle().WindowPadding.x);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::Colors().accent);
      ImGui::TextUnformatted("DeepSeekIDE");
      ImGui::PopStyleColor();
      if (fonts::G().uiBig) ImGui::PopFont();
      ImGui::NewLine();
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      const char* line2 = "или попросите DeepSeek в чате справа создать его за вас.";
      const char* hint =
          "Откройте папку проекта (панель слева), выберите файл в проводнике —\n"
          "или попросите DeepSeek в чате справа создать его за вас.";
      float tw = ImGui::CalcTextSize(line2).x;
      ImGui::SetCursorPosX((avail.x - tw) * 0.5f + ImGui::GetStyle().WindowPadding.x);
      ImGui::TextUnformatted(hint);
      ImGui::PopStyleColor();
    } else {
      // --- Вкладки ---
      if (ImGui::BeginTabBar("##editor_tabs", ImGuiTabBarFlags_Reorderable |
                                                  ImGuiTabBarFlags_AutoSelectNewTabs |
                                                  ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int i = 0; i < (int)mEd->tabs.size(); ++i) {
          auto& tab = mEd->tabs[i];
          bool open = true;
          std::string label = tab.title;
          if (tab.Dirty()) label += " ●";
          ImGuiTabItemFlags tf = 0;
          if (tab.Dirty() || tab.changedOnDisk) tf |= ImGuiTabItemFlags_UnsavedDocument;
          if (ImGui::BeginTabItem(label.c_str(), &open, tf)) {
            mEd->active = i;
            ImGui::EndTabItem();
          }
          if (!open) mEd->RequestClose(i);
        }
        ImGui::EndTabBar();
      }

      if (mEd->active >= 0 && mEd->active < (int)mEd->tabs.size()) {
        auto& tab = mEd->tabs[mEd->active];

        if (tab.changedOnDisk) {
          ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.3f, 0.05f, 0.9f));
          ImGui::BeginChild("##diskwarn", ImVec2(0, ImGui::GetFrameHeightWithSpacing() + 4), false);
          ImGui::Text("⚠ Файл изменён агентом, пока у вас есть несохранённые правки");
          ImGui::SameLine();
          if (ImGui::SmallButton("Перезагрузить с диска")) {
            std::string text;
            if (platform::ReadTextFile(tab.absPath, text)) {
              tab.editor.SetText(text);
              tab.savedUndoIndex = tab.editor.GetUndoIndex();
              tab.changedOnDisk = false;
            }
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("Скрыть")) tab.changedOnDisk = false;
          ImGui::EndChild();
          ImGui::PopStyleColor();
        }

        // --- Поиск (Ctrl+F) ---
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F) &&
            !ImGui::IsAnyItemActive()) {
          mFindOpen = !mFindOpen;
          mFindTabActive = mEd->active;
        }
        if (mFindOpen && mFindTabActive == mEd->active) FindBar(mEd->active);

        // --- Редактор ---
        ImGui::PushFont(fonts::G().mono);
        ImVec2 space = ImGui::GetContentRegionAvail();
        space.y -= ImGui::GetFrameHeightWithSpacing() + 2;  // место под статусную строку
        if (space.y < 40) space.y = 40;
        tab.editor.Render(("##code" + std::to_string(mEd->active)).c_str(), true, space, false);
        ImGui::PopFont();

        StatusLine();
      }
    }
  }
  ImGui::End();
}

void EditorPanel::FindBar(int tabIdx) {
  auto& tab = mEd->tabs[tabIdx];
  ImGui::SetNextItemWidth(260.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  bool enter = ImGui::InputTextWithHint("##find", "Найти в файле…  (Enter — дальше, Esc — закрыть)",
                                        mFindBuf, sizeof(mFindBuf),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::PopStyleVar();
  ImGui::SameLine();
  bool next = ImGui::SmallButton("Дальше");
  ImGui::SameLine();
  if (ImGui::SmallButton("Закрыть")) mFindOpen = false;

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) == 0 &&
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    mFindOpen = false;

  std::string needle = mFindBuf;
  if (needle.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("введите текст для поиска");
    return;
  }

  auto lines = tab.editor.GetTextLines();
  int total = (int)lines.size();
  int curLine = 0, curCol = 0;
  tab.editor.GetCursorPosition(curLine, curCol);

  std::string lowNeedle = LowerCopy(needle);
  int found = -1, foundCol = 0;
  for (int pass = 0; pass < total; ++pass) {
    int i = (curLine + pass + (enter || next ? 1 : 0)) % total;  // циклический поиск вперёд
    std::string low = LowerCopy(lines[i]);
    size_t pos = low.find(lowNeedle);
    if (pos != std::string::npos) {
      found = i;
      foundCol = (int)pos;
      break;
    }
  }

  if (found >= 0 && (enter || next)) {
    tab.editor.SetCursorPosition(found, foundCol);
    tab.editor.SetViewAtLine(found, TextEditor::SetViewAtLineMode::Centered);
  }

  // Подсчёт совпадений
  int count = 0;
  for (auto& l : lines) {
    std::string low = LowerCopy(l);
    size_t p = 0;
    while ((p = low.find(lowNeedle, p)) != std::string::npos) {
      ++count;
      p += lowNeedle.size();
    }
  }
  ImGui::SameLine();
  ImGui::TextDisabled("совпадений: %d", count);
}

void EditorPanel::StatusLine() {
  auto& tab = mEd->tabs[mEd->active];
  ImGui::Separator();
  int line = 0, col = 0;
  tab.editor.GetCursorPosition(line, col);
  int totalLines = (int)tab.editor.GetTextLines().size();

  ImGui::TextDisabled("%s", platform::PathToStr(tab.absPath).c_str());
  ImGui::SameLine();
  widgets::VerticalSep();

  if (tab.Dirty())
    widgets::Badge("● не сохранено", theme::Colors().warn);
  else
    widgets::Badge("✓ сохранено", theme::Colors().ok);
  ImGui::SameLine();
  widgets::VerticalSep();

  ImGui::TextDisabled("%s", EditorManager::LanguageName(tab.absPath));
  ImGui::SameLine();
  widgets::VerticalSep();

  ImGui::TextDisabled("Стр %d, Стб %d | Строк: %d", line + 1, col + 1, totalLines);
  ImGui::SameLine();
  widgets::RightSide(230.0f, [] {
    ImGui::TextDisabled("Ctrl+S — сохранить · Ctrl+F — найти");
  });
  ImGui::Dummy(ImVec2(0, 0));
}
