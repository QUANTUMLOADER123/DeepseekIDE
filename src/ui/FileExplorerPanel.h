#pragma once

#include <filesystem>
#include <functional>
#include <string>

class ProjectManager;
struct ProjectNode;

// Левая панель — дерево файлов проекта с контекстными операциями.
class FileExplorerPanel {
public:
  struct Callbacks {
    std::function<void(const std::filesystem::path& abs)> openFile;
    std::function<void()> requestOpenFolder;   // открыть диалог выбора папки
    std::function<void(const std::string& msg)> log;
  };

  void SetContext(ProjectManager* project, Callbacks cb) {
    mProject = project;
    mCb = std::move(cb);
  }
  void Render();

private:
  void DrawNode(const ProjectNode& node);
  void NodeContextMenu(const ProjectNode& node);
  void RenderModals();

  ProjectManager* mProject = nullptr;
  Callbacks mCb;

  // Состояние всплывающих окон «Новый файл/папка/Переименовать»
  enum class PendingOp { None, NewFile, NewFolder, Rename, Delete };
  PendingOp mOp = PendingOp::None;
  std::string mOpDir;    // папка, внутри которой операция (относительный путь)
  std::string mOpTarget; // исходный путь (для rename/delete)
  char mNameBuf[512]{};
};
