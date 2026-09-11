#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Узел дерева файлов проекта.
struct ProjectNode {
  std::string name;
  std::string relPath;  // относительно корня проекта, '/'-разделители
  bool isDir = false;
  std::uintmax_t size = 0;
  std::vector<ProjectNode> children;
};

// Управляет выбранной папкой проекта: дерево файлов, безопасные пути.
class ProjectManager {
public:
  void SetRoot(const std::filesystem::path& root);
  void Clear();
  bool IsOpen() const { return mOpen; }
  const std::filesystem::path& Root() const { return mRoot; }
  std::string RootStr() const;

  // Построить дерево заново (вызывать из UI-потока).
  void Refresh();
  const ProjectNode& Tree() const { return mTree; }

  // Флажок «файловая структура изменилась» — выставляется инструментами агента.
  void MarkDirty() { mDirty.store(true); }
  bool ConsumeDirty() { return mDirty.exchange(false); }

  // ------------------------------------------------------------- безопасность
  // Превращает относительный путь в абсолютный ВНУТРИ root.
  // Возвращает false, если путь пытается выйти за пределы проекта.
  static bool SafeJoin(const std::filesystem::path& root, const std::string& rel,
                       std::filesystem::path& out, std::string& err);

  // Служебные пути, к которым у инструментов нет доступа на запись.
  static bool IsProtectedRel(const std::string& rel);

  // Список файлов проекта в плоском виде (для list_files/search).
  static void Flatten(const ProjectNode& node, std::vector<const ProjectNode*>& out);

private:
  void Build(ProjectNode& node, const std::filesystem::path& abs, int depth, long long& budget);

  std::filesystem::path mRoot;
  ProjectNode mTree;
  bool mOpen = false;
  std::atomic<bool> mDirty{false};
};
