#include "core/ProjectManager.h"

#include <algorithm>
#include <set>
#include <system_error>

#include "app/Platform.h"

namespace {

// Папки, которые не показываем и не сканируем глубоко.
const std::set<std::string>& IgnoredDirs() {
  static const std::set<std::string> k = {
      ".git", ".deepseekide", "node_modules", ".next", ".nuxt", "build", "dist",
      "out", "target", ".cache", ".venv", "venv", "__pycache__", ".idea", ".vs",
      ".gradle", "coverage"};
  return k;
}

}  // namespace

void ProjectManager::SetRoot(const std::filesystem::path& root) {
  std::error_code ec;
  mRoot = std::filesystem::weakly_canonical(root, ec);
  if (ec) mRoot = root;
  mOpen = !mRoot.empty() && std::filesystem::is_directory(mRoot, ec);
  mDirty.store(false);
  Refresh();
}

void ProjectManager::Clear() {
  mRoot.clear();
  mTree = ProjectNode{};
  mOpen = false;
}

std::string ProjectManager::RootStr() const { return platform::PathToStr(mRoot); }

void ProjectManager::Refresh() {
  mTree = ProjectNode{};
  if (!mOpen) return;
  mTree.name = mRoot.filename().string();
  if (mTree.name.empty()) mTree.name = mRoot.string();
  mTree.isDir = true;
  mTree.relPath = ".";
  long long budget = 40000;  // защита от огромных деревьев
  Build(mTree, mRoot, 0, budget);
}

void ProjectManager::Build(ProjectNode& node, const std::filesystem::path& abs, int depth,
                           long long& budget) {
  if (depth > 12) return;
  std::error_code ec;
  std::vector<ProjectNode> dirs, files;
  for (auto it = std::filesystem::directory_iterator(
           abs, std::filesystem::directory_options::skip_permission_denied, ec);
       !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    if (--budget <= 0) return;
    const auto& p = it->path();
    std::string name = platform::PathToStr(p.filename());
    if (name.empty() || name[0] == '.') {
      if (name != ".env" && name != ".gitignore" && name != ".github") continue;
    }
    bool isDir = it->is_directory(ec);
    if (isDir && IgnoredDirs().count(name)) continue;

    ProjectNode child;
    child.name = name;
    child.isDir = isDir;
    std::string rel = platform::PathToStr(std::filesystem::relative(p, mRoot, ec));
    std::replace(rel.begin(), rel.end(), '\\', '/');
    child.relPath = rel;
    if (!isDir) {
      child.size = it->file_size(ec);
      if (ec) child.size = 0;
    }
    (isDir ? dirs : files).push_back(std::move(child));
  }
  auto byName = [](const ProjectNode& a, const ProjectNode& b) {
    return a.name < b.name;
  };
  std::sort(dirs.begin(), dirs.end(), byName);
  std::sort(files.begin(), files.end(), byName);
  node.children.reserve(dirs.size() + files.size());
  for (auto& d : dirs) {
    node.children.push_back(std::move(d));
    Build(node.children.back(), abs / platform::StrToPath(node.children.back().name), depth + 1,
          budget);
  }
  for (auto& f : files) node.children.push_back(std::move(f));
}

bool ProjectManager::SafeJoin(const std::filesystem::path& root, const std::string& relIn,
                              std::filesystem::path& out, std::string& err) {
  namespace fs = std::filesystem;
  std::string rel = relIn;
  std::replace(rel.begin(), rel.end(), '\\', '/');
  if (rel.empty()) rel = ".";

  fs::path relPath = platform::StrToPath(rel);
  if (relPath.is_absolute()) {
    err = "используйте пути относительно корня проекта: " + relIn;
    return false;
  }

  std::error_code ec;
  fs::path joined = fs::weakly_canonical(root / relPath, ec);
  if (ec) {
    // файл ещё не существует — нормализуем вручную
    joined = (root / relPath).lexically_normal();
  }

  const std::string rootStr = platform::PathToStr(root);
  std::string outStr = platform::PathToStr(joined);
  bool ok = outStr == rootStr;
  if (!ok && outStr.size() > rootStr.size() && outStr.compare(0, rootStr.size(), rootStr) == 0) {
    char sep = outStr[rootStr.size()];
    ok = (sep == '/' || sep == '\\');
  }
  if (!ok) {
    err = "путь выходит за пределы проекта (запрещено): " + relIn;
    return false;
  }
  out = joined;
  return true;
}

bool ProjectManager::IsProtectedRel(const std::string& rel) {
  std::string r = rel;
  std::replace(r.begin(), r.end(), '\\', '/');
  if (!r.empty() && r[0] == '/') r.erase(0, 1);
  auto firstSlash = r.find('/');
  std::string first = firstSlash == std::string::npos ? r : r.substr(0, firstSlash);
  return first == ".git" || first == ".deepseekide";
}

void ProjectManager::Flatten(const ProjectNode& node, std::vector<const ProjectNode*>& out) {
  for (const auto& c : node.children) {
    out.push_back(&c);
    if (c.isDir) Flatten(c, out);
  }
}
