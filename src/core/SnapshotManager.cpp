#include "core/SnapshotManager.h"

#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <set>
#include <system_error>

#include "app/Platform.h"
#include "core/ProjectManager.h"

namespace fs = std::filesystem;

void SnapshotManager::SetRoot(const fs::path& projectRoot) {
  mRoot = projectRoot;
  mActive = false;
  mCurrent = SnapshotInfo{};
  mSeq = 0;
}

fs::path SnapshotManager::SnapRoot() const {
  return mRoot / ".deepseekide" / "snapshots";
}

void SnapshotManager::Begin(const std::string& title) {
  if (mRoot.empty()) return;
  if (mActive) Commit();  // предыдущее действие не было завершено — дожимаем

  mCurrent = SnapshotInfo{};
  mCurrent.epochMs = platform::NowMillis();
  mCurrent.isoTime = platform::NowIso();
  char id[64];
  std::snprintf(id, sizeof(id), "%s-%03d",
                mCurrent.isoTime.substr(0, 19).c_str(), ++mSeq % 1000);
  std::string sid = id;
  for (auto& c : sid)
    if (c == ':' || c == 'T') c = '-';
  mCurrent.id = sid;
  mCurrent.title = title;
  mActive = true;
}

void SnapshotManager::Commit() {
  if (!mActive) return;
  mActive = false;
  if (mCurrent.files.empty()) return;  // изменений не было — снимок не нужен

  fs::path dir = SnapRoot() / platform::StrToPath(mCurrent.id);
  std::error_code ec;

  nlohmann::json j;
  j["id"] = mCurrent.id;
  j["title"] = mCurrent.title;
  j["iso_time"] = mCurrent.isoTime;
  j["epoch_ms"] = mCurrent.epochMs;
  j["files"] = nlohmann::json::array();
  for (const auto& f : mCurrent.files)
    j["files"].push_back({{"path", f.path}, {"existed", f.existed}});
  platform::WriteTextFile(dir / "meta.json", j.dump(2));
}

void SnapshotManager::Abandon() {
  if (!mActive) return;
  mActive = false;
  if (mCurrent.files.empty()) return;
  // Удаляем частично созданный снимок.
  std::error_code ec;
  fs::remove_all(SnapRoot() / platform::StrToPath(mCurrent.id), ec);
  mCurrent = SnapshotInfo{};
}

void SnapshotManager::RecordFileChange(const fs::path& absPath) {
  if (!mActive || mRoot.empty()) return;

  std::error_code ec;
  fs::path rel = fs::relative(absPath, mRoot, ec);
  if (ec) return;
  std::string relStr = platform::PathToStr(rel);
  std::replace(relStr.begin(), relStr.end(), '\\', '/');

  for (const auto& f : mCurrent.files)
    if (f.path == relStr) return;  // уже сохранили оригинал

  SnapshotFile rec;
  rec.path = relStr;
  rec.existed = fs::exists(absPath, ec) && !fs::is_directory(absPath, ec);

  if (rec.existed) {
    fs::path dest = SnapRoot() / platform::StrToPath(mCurrent.id) / "files" /
                    platform::StrToPath(relStr);
    fs::create_directories(dest.parent_path(), ec);
    fs::copy_file(absPath, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) return;  // не смогли сохранить — не записываем и в мету
  }
  mCurrent.files.push_back(std::move(rec));
}

std::vector<SnapshotInfo> SnapshotManager::List() const {
  std::vector<SnapshotInfo> out;
  std::error_code ec;
  fs::path root = SnapRoot();
  if (mRoot.empty() || !fs::is_directory(root, ec)) return out;

  for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator();
       it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    std::string text;
    if (!platform::ReadTextFile(it->path() / "meta.json", text)) continue;
    try {
      auto j = nlohmann::json::parse(text);
      SnapshotInfo info;
      info.id = j.value("id", it->path().filename().string());
      info.title = j.value("title", "");
      info.isoTime = j.value("iso_time", "");
      info.epochMs = j.value("epoch_ms", 0LL);
      for (const auto& f : j.value("files", nlohmann::json::array())) {
        SnapshotFile rec;
        rec.path = f.value("path", "");
        rec.existed = f.value("existed", true);
        if (!rec.path.empty()) info.files.push_back(std::move(rec));
      }
      out.push_back(std::move(info));
    } catch (...) {
    }
  }
  // Новые сверху. Тай-брейк по id: он содержит ISO-время и счётчик,
  // поэтому лексикографический порядок == хронологический даже в одну миллисекунду.
  std::sort(out.begin(), out.end(), [](const SnapshotInfo& a, const SnapshotInfo& b) {
    if (a.epochMs != b.epochMs) return a.epochMs > b.epochMs;
    return a.id > b.id;
  });
  return out;
}

static bool ApplyRestore(const fs::path& projectRoot, const fs::path& snapDir,
                         const SnapshotInfo& info, std::string& log) {
  bool anything = false;
  for (auto fit = info.files.rbegin(); fit != info.files.rend(); ++fit) {
    const SnapshotFile& rec = *fit;
    fs::path target;
    std::string err;
    if (!ProjectManager::SafeJoin(projectRoot, rec.path, target, err)) {
      log += "  [пропуск] " + rec.path + ": " + err + "\n";
      continue;
    }
    std::error_code ec;
    if (rec.existed) {
      fs::path src = snapDir / "files" / platform::StrToPath(rec.path);
      if (!fs::exists(src, ec)) {
        log += "  [пропуск] нет сохранённой копии " + rec.path + "\n";
        continue;
      }
      fs::create_directories(target.parent_path(), ec);
      fs::copy_file(src, target, fs::copy_options::overwrite_existing, ec);
      if (ec)
        log += "  [ошибка] не удалось восстановить " + rec.path + "\n";
      else {
        log += "  [OK] восстановлен " + rec.path + "\n";
        anything = true;
      }
    } else {
      if (fs::exists(target, ec)) {
        fs::remove(target, ec);
        if (ec)
          log += "  [ошибка] не удалось удалить " + rec.path + "\n";
        else {
          log += "  [OK] удалён созданный файл " + rec.path + "\n";
          anything = true;
        }
      }
    }
  }
  return anything;
}

bool SnapshotManager::Restore(const std::string& id, std::string& log) const {
  for (const auto& info : List()) {
    if (info.id != id) continue;
    log += "Откат действия «" + info.title + "» (" + info.id + ")\n";
    bool ok = ApplyRestore(mRoot, SnapRoot() / platform::StrToPath(id), info, log);
    if (ok) {
      // Запись снимка удаляем: «откатить последнее» теперь шагает назад по истории.
      std::error_code ec;
      fs::remove_all(SnapRoot() / platform::StrToPath(id), ec);
    }
    return ok;
  }
  log += "Снимок не найден: " + id + "\n";
  return false;
}

bool SnapshotManager::RollbackThrough(const std::string& id, std::string& log) const {
  auto all = List();  // новые сверху
  std::set<std::string> done;
  bool found = false;
  for (const auto& info : all) {
    SnapshotInfo filtered;
    filtered.id = info.id;
    filtered.title = info.title;
    for (const auto& f : info.files)
      if (done.insert(f.path).second) filtered.files.push_back(f);
    if (!filtered.files.empty()) {
      log += "Откат действия «" + info.title + "» (" + info.id + ")\n";
      ApplyRestore(mRoot, SnapRoot() / platform::StrToPath(info.id), filtered, log);
    }
    std::error_code ec;
    fs::remove_all(SnapRoot() / platform::StrToPath(info.id), ec);
    if (info.id == id) {
      found = true;
      break;
    }
  }
  if (!found) log += "Снимок не найден: " + id + "\n";
  return found;
}

bool SnapshotManager::RollbackLast(std::string& log) const {
  auto all = List();
  if (all.empty()) {
    log += "История снимков пуста — откатывать нечего.\n";
    return false;
  }
  return Restore(all.front().id, log);
}

bool SnapshotManager::Delete(const std::string& id) {
  std::error_code ec;
  return fs::remove_all(SnapRoot() / platform::StrToPath(id), ec) > 0;
}
