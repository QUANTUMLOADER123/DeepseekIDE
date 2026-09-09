#include "ui/FileExplorerPanel.h"

#include <cstring>
#include <system_error>

#include "app/Platform.h"
#include "core/ProjectManager.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace fs = std::filesystem;

void FileExplorerPanel::Render() {
  if (ImGui::Begin("Проект")) {
    // --- Панель инструментов ---
    if (ImGui::Button("▶ Открыть папку…")) {
      if (mCb.requestOpenFolder) mCb.requestOpenFolder();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Выбрать папку проекта (Ctrl+O)");
    ImGui::SameLine();
    if (ImGui::Button("↻")) {
      mProject->Refresh();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Обновить дерево файлов");

    ImGui::Separator();

    if (!mProject->IsOpen()) {
      ImGui::Dummy(ImVec2(0, 20));
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("Папка проекта не выбрана.\n\nОткройте папку, и агент DeepSeek сможет "
                         "создавать и изменять файлы внутри неё. Можно также перетащить папку "
                         "в окно приложения.");
      ImGui::PopStyleColor();
      ImGui::Dummy(ImVec2(0, 8));
      if (widgets::AccentButton("Выбрать папку проекта", ImVec2(-1, 36)))
        if (mCb.requestOpenFolder) mCb.requestOpenFolder();
    } else {
      // Путь корня
      ImGui::PushStyleColor(ImGuiCol_Text, theme::Colors().textDim);
      std::string root = mProject->RootStr();
      if (root.size() > 42) root = "…" + root.substr(root.size() - 41);
      ImGui::TextUnformatted(root.c_str());
      ImGui::PopStyleColor();
      ImGui::Separator();

      // Дерево
      ImGui::BeginChild("##file_tree", ImVec2(0, 0), false,
                        ImGuiWindowFlags_HorizontalScrollbar);
      for (const auto& child : mProject->Tree().children) DrawNode(child);
      ImGui::EndChild();
    }
  }
  ImGui::End();
  RenderModals();
}

void FileExplorerPanel::DrawNode(const ProjectNode& node) {
  ImGui::PushID(node.relPath.c_str());
  if (node.isDir) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.relPath.size() < 4) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    bool open = ImGui::TreeNodeEx(node.relPath.c_str(), flags, "▸ %s", node.name.c_str());
    NodeContextMenu(node);
    if (open) {
      for (const auto& c : node.children) DrawNode(c);
      ImGui::TreePop();
    }
  } else {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    ImGui::TreeNodeEx(node.relPath.c_str(), flags, "  %s", node.name.c_str());
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(node.relPath.c_str());
      ImGui::TextDisabled("%s", platform::HumanSize(node.size).c_str());
      ImGui::EndTooltip();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && mCb.openFile) {
      fs::path abs;
      std::string err;
      if (ProjectManager::SafeJoin(mProject->Root(), node.relPath, abs, err)) mCb.openFile(abs);
    }
    NodeContextMenu(node);
  }
  ImGui::PopID();
}

void FileExplorerPanel::NodeContextMenu(const ProjectNode& node) {
  std::string popupId = "ctx_" + node.relPath;
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    ImGui::OpenPopup(popupId.c_str());
  if (ImGui::BeginPopup(popupId.c_str())) {
    if (node.isDir) {
      if (ImGui::MenuItem("Новый файл здесь…")) {
        mOp = PendingOp::NewFile;
        mOpDir = node.relPath;
        mNameBuf[0] = 0;
      }
      if (ImGui::MenuItem("Новая папка здесь…")) {
        mOp = PendingOp::NewFolder;
        mOpDir = node.relPath;
        mNameBuf[0] = 0;
      }
      ImGui::Separator();
    }
    if (ImGui::MenuItem("Открыть в редакторе", nullptr, false, !node.isDir)) {
      if (mCb.openFile) {
        fs::path abs;
        std::string err;
        if (ProjectManager::SafeJoin(mProject->Root(), node.relPath, abs, err)) mCb.openFile(abs);
      }
    }
    if (ImGui::MenuItem("Переименовать…")) {
      mOp = PendingOp::Rename;
      mOpTarget = node.relPath;
      std::snprintf(mNameBuf, sizeof(mNameBuf), "%s", node.name.c_str());
    }
    if (ImGui::MenuItem("Удалить…")) {
      mOp = PendingOp::Delete;
      mOpTarget = node.relPath;
    }
    ImGui::EndPopup();
  }
}

void FileExplorerPanel::RenderModals() {
  auto doPopup = [&](const char* title, auto&& body) {
    if (mOp == PendingOp::None) return;
    std::string want = title;
    // Открываем попап один раз при переходе в состояние
    if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      body();
      ImGui::EndPopup();
    } else {
      mOp = PendingOp::None;  // закрыли крестиком
    }
    (void)want;
  };

  const char* titles[] = {"", "Новый файл", "Новая папка", "Переименовать", "Удалить"};
  const char* title = titles[static_cast<int>(mOp)];
  if (mOp == PendingOp::None) return;

  doPopup(title, [&] {
    bool confirmed = false;
    if (mOp == PendingOp::Delete) {
      ImGui::Text("Удалить «%s»?\n(откат доступен только для действий агента)",
                  mOpTarget.c_str());
      ImGui::Dummy(ImVec2(0, 6));
      if (ImGui::Button("Удалить", ImVec2(120, 0))) confirmed = true;
      ImGui::SameLine();
      if (ImGui::Button("Отмена", ImVec2(120, 0))) mOp = PendingOp::None;
    } else {
      if (mOp == PendingOp::NewFile) ImGui::Text("Имя файла в «%s»:", mOpDir.c_str());
      if (mOp == PendingOp::NewFolder) ImGui::Text("Имя папки в «%s»:", mOpDir.c_str());
      if (mOp == PendingOp::Rename) ImGui::Text("Новое имя для «%s»:", mOpTarget.c_str());
      ImGui::SetNextItemWidth(360);
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      bool enter = ImGui::InputText("##name", mNameBuf, sizeof(mNameBuf),
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Dummy(ImVec2(0, 4));
      if (ImGui::Button("OK", ImVec2(120, 0)) || enter) confirmed = true;
      ImGui::SameLine();
      if (ImGui::Button("Отмена", ImVec2(120, 0))) mOp = PendingOp::None;
    }

    if (confirmed) {
      std::string err;
      fs::path abs;
      std::error_code ec;
      switch (mOp) {
        case PendingOp::NewFile: {
          if (!mNameBuf[0]) break;
          std::string rel = (mOpDir == "." ? "" : mOpDir + "/") + mNameBuf;
          if (ProjectManager::SafeJoin(mProject->Root(), rel, abs, err) &&
              platform::WriteTextFile(abs, "")) {
            mProject->Refresh();
          } else if (!err.empty() && mCb.log) mCb.log("Ошибка: " + err);
          break;
        }
        case PendingOp::NewFolder: {
          if (!mNameBuf[0]) break;
          std::string rel = (mOpDir == "." ? "" : mOpDir + "/") + mNameBuf;
          if (ProjectManager::SafeJoin(mProject->Root(), rel, abs, err)) {
            fs::create_directories(abs, ec);
            mProject->Refresh();
          } else if (mCb.log) mCb.log("Ошибка: " + err);
          break;
        }
        case PendingOp::Rename: {
          if (!mNameBuf[0]) break;
          fs::path from, to;
          std::string errLocal;
          if (ProjectManager::SafeJoin(mProject->Root(), mOpTarget, from, errLocal)) {
            std::string parent =
                mOpTarget.find('/') == std::string::npos
                    ? ""
                    : mOpTarget.substr(0, mOpTarget.find_last_of('/'));
            std::string rel = (parent.empty() ? "" : parent + "/") + mNameBuf;
            if (!ProjectManager::IsProtectedRel(rel) &&
                ProjectManager::SafeJoin(mProject->Root(), rel, to, errLocal)) {
              fs::rename(from, to, ec);
              mProject->Refresh();
            }
          }
          break;
        }
        case PendingOp::Delete: {
          fs::path target;
          std::string errLocal;
          if (ProjectManager::SafeJoin(mProject->Root(), mOpTarget, target, errLocal) &&
              !ProjectManager::IsProtectedRel(mOpTarget)) {
            fs::remove_all(target, ec);
            mProject->Refresh();
          }
          break;
        }
        default: break;
      }
      mOp = PendingOp::None;
      ImGui::CloseCurrentPopup();
    }
  });
}
