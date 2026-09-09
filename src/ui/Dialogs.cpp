#include "ui/Dialogs.h"

#include <algorithm>
#include <cstring>
#include <system_error>

#include "app/Platform.h"
#include "core/Settings.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace fs = std::filesystem;

// ============================================================ FolderPicker

void FolderPickerDialog::Open(const fs::path& startDir) {
  mOpen = true;
  mJustOpened = true;
  std::error_code ec;
  mCur = fs::is_directory(startDir, ec) ? startDir : platform::HomeDir();
  mSelected = -1;
  mResult.clear();
  Scan();
}

void FolderPickerDialog::Scan() {
  mEntries.clear();
  mError.clear();
  std::error_code ec;
  std::string curStr = platform::PathToStr(mCur);
  std::snprintf(mPathBuf, sizeof(mPathBuf), "%s", curStr.c_str());
  for (auto it = fs::directory_iterator(mCur, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    std::string name = platform::PathToStr(it->path().filename());
    if (name.empty()) continue;
    bool isDir = it->is_directory(ec);
    if (!isDir) continue;  // выбираем папку — файлы не показываем
    if (name[0] == '.' && name != "..") continue;
    mEntries.emplace_back(name, true);
  }
  std::sort(mEntries.begin(), mEntries.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  if (mEntries.empty() && ec) mError = "Нет доступа к этой папке";
  mSelected = -1;
}

void FolderPickerDialog::GoUp() {
  if (mCur.has_parent_path() && mCur != mCur.root_path()) {
    mCur = mCur.parent_path();
    Scan();
  }
}

bool FolderPickerDialog::Render() {
  if (!mOpen) return false;
  if (mJustOpened) {
    ImGui::OpenPopup("Выбор папки проекта");
    mJustOpened = false;
  }

  bool chosen = false;
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Выбор папки проекта", nullptr, ImGuiWindowFlags_NoCollapse)) {
    widgets::DimText("Выберите папку — агент DeepSeek будет работать внутри неё (и только внутри).");

    // Строка пути
    ImGui::PushItemWidth(-110);
    ImGui::InputTextWithHint("##path", "Введите или вставьте путь…", mPathBuf, sizeof(mPathBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue);
    bool enterPressed = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Перейти", ImVec2(100, 0)) || enterPressed) {
      std::error_code ec;
      fs::path target = platform::StrToPath(mPathBuf);
      if (fs::is_directory(target, ec)) {
        mCur = fs::weakly_canonical(target, ec);
        if (ec) mCur = target;
        Scan();
      } else {
        mError = "Папка не существует: " + std::string(mPathBuf);
      }
    }

    // Кнопка «вверх»
    if (ImGui::Button("↑  На уровень выше")) GoUp();
    ImGui::SameLine();
    widgets::DimText(mCur.has_root_path() ? platform::PathToStr(mCur).c_str() : "/");

    // Список папок
    if (!mError.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::Colors().error);
      ImGui::TextUnformatted(mError.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::BeginChild("##dirlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8), true);
    if (mEntries.empty()) ImGui::TextDisabled("(нет подпапок)");
    for (int i = 0; i < (int)mEntries.size(); ++i) {
      const auto& [name, isDir] = mEntries[i];
      char label[600];
      std::snprintf(label, sizeof(label), "%s  %s", "▶", name.c_str());
      if (ImGui::Selectable(label, mSelected == i, ImGuiSelectableFlags_AllowDoubleClick)) {
        mSelected = i;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          mCur /= platform::StrToPath(name);
          Scan();
        }
      }
    }
    ImGui::EndChild();

    // Нижние кнопки
    if (widgets::AccentButton("Выбрать эту папку")) {
      mResult = mCur;
      mOpen = false;
      chosen = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (mSelected >= 0) {
      if (ImGui::Button(("Открыть «" + mEntries[mSelected].first + "»").c_str())) {
        mCur /= platform::StrToPath(mEntries[mSelected].first);
        Scan();
      }
      ImGui::SameLine();
    }
    if (ImGui::Button("Отмена")) {
      mOpen = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  return chosen;
}

// ============================================================ SettingsDialog

void SettingsDialog::Open(Settings* settings, Callbacks cb) {
  m = settings;
  mCb = std::move(cb);
  mOpen = true;
  mJustOpened = true;
  std::snprintf(mApiKey, sizeof(mApiKey), "%s", m->apiKey.c_str());
  std::snprintf(mBaseUrl, sizeof(mBaseUrl), "%s", m->baseUrl.c_str());
  std::snprintf(mModel, sizeof(mModel), "%s", m->model.c_str());
  mTemp = m->temperature;
  mMaxSteps = m->maxSteps;
  mAllowShell = m->allowShell;
  mShellTimeout = m->shellTimeout;
  mAutoSnapshot = m->autoSnapshot;
  mTheme = m->theme;
  mUiFont = m->uiFontSize;
  mCodeFont = m->codeFontSize;
  mTabSize = m->editorTabSize;
  mShowWs = m->showWhitespace;
  mKeyVisible = false;
}

bool SettingsDialog::Render() {
  if (!mOpen) return false;
  if (mJustOpened) {
    ImGui::OpenPopup("Настройки DeepSeekIDE");
    mJustOpened = false;
  }
  bool saved = false;
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(680, 640), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Настройки DeepSeekIDE", nullptr, ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::BeginTabBar("##settings_tabs")) {
      if (ImGui::BeginTabItem("Ассистент")) {
        ImGui::Dummy(ImVec2(0, 6));
        widgets::DimText("AI-ассистент работает через встроенный chat.deepseek.com — "
                         "никаких API-ключей не нужно, достаточно бесплатного аккаунта DeepSeek.");
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::BulletText("Левая половина окна — полноценный чат DeepSeek (вход через Google/аккаунт).");
        ImGui::BulletText("Пишите задачу в строке над редактором справа и жмите «Отправить» — "
                          "IDE сама передаст задачу в чат вместе со структурой проекта.");
        ImGui::BulletText("Ответ с блоками deepseekide-ops появится в окне «Применить?» — "
                          "вы подтверждаете правки, они попадают в снимок (откат в один клик).");
        ImGui::BulletText("Кнопка «Файл» рядом с полем задачи отправляет содержимое активного файла в чат.");
        ImGui::Dummy(ImVec2(0, 6));
        widgets::DimText("Если сайт попросит войти или решить капчу — сделайте это прямо в "
                         "окне чата слева, дальше всё работает автоматически.");
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Безопасность")) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Checkbox("Разрешить агенту выполнять консольные команды (run_command)", &mAllowShell);
        if (mAllowShell) {
          ImGui::PushStyleColor(ImGuiCol_Text, theme::Colors().warn);
          ImGui::TextWrapped("⚠ Внимание: модель сможет запускать команды в папке проекта. Включайте, "
                             "только если доверяете текущему сеансу. Команды логируются в панели «Журнал».");
          ImGui::PopStyleColor();
          ImGui::SliderInt("Таймаут команды, сек", &mShellTimeout, 5, 600);
        }
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::Checkbox("Автоматические снимки перед изменениями (откат)", &mAutoSnapshot);
        widgets::DimText(
            "Перед каждой записью/удалением файла его копия сохраняется в "
            ".deepseekide/snapshots — любое действие агента можно откатить в панели «Снимки».");
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Внешний вид")) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextUnformatted("Тема");
        ImGui::SameLine();
        ImGui::PushItemWidth(240);
        if (ImGui::BeginCombo("##theme", theme::Name(mTheme))) {
          for (int i = 0; i < (int)theme::Id::Count; ++i)
            if (ImGui::Selectable(theme::Name(i), mTheme == i)) mTheme = i;
          ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        ImGui::SliderInt("Шрифт интерфейса", &mUiFont, 13, 24);
        ImGui::SliderInt("Шрифт редактора", &mCodeFont, 11, 26);
        ImGui::SliderInt("Размер Tab в редакторе", &mTabSize, 2, 8);
        ImGui::Checkbox("Показывать пробелы", &mShowWs);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::Dummy(ImVec2(0, 8));
    if (widgets::AccentButton("Сохранить", ImVec2(140, 0))) {
      m->apiKey = mApiKey;
      m->baseUrl = mBaseUrl;
      m->model = mModel;
      m->temperature = mTemp;
      m->maxSteps = mMaxSteps;
      m->allowShell = mAllowShell;
      m->shellTimeout = mShellTimeout;
      m->autoSnapshot = mAutoSnapshot;
      const bool themeChanged = (m->theme != mTheme);
      const bool fontChanged = (m->uiFontSize != mUiFont) || (m->codeFontSize != mCodeFont);
      m->theme = mTheme;
      m->uiFontSize = mUiFont;
      m->codeFontSize = mCodeFont;
      m->editorTabSize = mTabSize;
      m->showWhitespace = mShowWs;
      m->Save();
      if (themeChanged && mCb.applyTheme) mCb.applyTheme();
      if (fontChanged && mCb.rebuildFonts) mCb.rebuildFonts();
      if (mCb.applyEditorStyle) mCb.applyEditorStyle();
      mOpen = false;
      saved = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Отмена", ImVec2(100, 0))) {
      mOpen = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  return saved;
}

// ============================================================ AboutDialog

void AboutDialog::Render() {
  if (!mOpen) return;
  if (mJustOpened) {
    ImGui::OpenPopup("О DeepSeekIDE");
    mJustOpened = false;
  }
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("О DeepSeekIDE", nullptr, ImGuiWindowFlags_NoCollapse)) {
    if (fonts::G().uiBig) ImGui::PushFont(fonts::G().uiBig);
    ImGui::TextUnformatted("DeepSeekIDE");
    if (fonts::G().uiBig) ImGui::PopFont();
    ImGui::TextDisabled("Кастомный AI-IDE на C++ / Dear ImGui · v0.1.0");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::BulletText("Агент DeepSeek (function calling) правит файлы выбранной папки-проекта");
    ImGui::BulletText("Веб-чат chat.deepseek.com во встроенном окне");
    ImGui::BulletText("Снимки файлов и откат любых действий агента");
    ImGui::BulletText("Редактор кода с подсветкой, проводник, журнал команд");
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("Шрифты: Inter и JetBrains Mono (OFL). Лицензия проекта: MIT.");
    ImGui::Dummy(ImVec2(0, 10));
    if (widgets::AccentButton("Закрыть", ImVec2(120, 0))) {
      mOpen = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
