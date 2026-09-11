#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Один файл внутри снимка.
struct SnapshotFile {
  std::string path;   // относительный путь
  bool existed = true; // существовал ли файл до действия (false → при откате удалить)
};

// Метаданные снимка (одно «действие» агента = один запрос пользователя).
struct SnapshotInfo {
  std::string id;        // e.g. "20260909-143207-001"
  std::string title;     // описание действия (первые символы запроса)
  std::string isoTime;
  long long epochMs = 0;
  std::vector<SnapshotFile> files;
};

// Система отката: перед КАЖДЫМ изменением файла агентом (или удалением)
// содержимое файла складывается в <проект>/.deepseekide/snapshots/<id>/.
// Откат восстанавливает byte-to-byte состояние файлов до действия.
class SnapshotManager {
public:
  void SetRoot(const std::filesystem::path& projectRoot);
  std::filesystem::path SnapRoot() const;
  bool EnabledForProject() const { return !mRoot.empty(); }

  // Жизненный цикл одного действия агента.
  void Begin(const std::string& title);
  void Commit();   // если изменений не было — снимок не создаётся
  void Abandon();  // сбросить незакоммиченное состояние без создания снимка
  bool IsActive() const { return mActive; }

  // Зафиксировать pre-image файла перед записью/удалением.
  void RecordFileChange(const std::filesystem::path& absPath);

  // История (новые сверху).
  std::vector<SnapshotInfo> List() const;

  // Откаты. Возвращают человекочитаемый лог в `log`.
  bool Restore(const std::string& id, std::string& log) const;         // только это действие
  bool RollbackThrough(const std::string& id, std::string& log) const; // это + всё новее
  bool RollbackLast(std::string& log) const;
  bool Delete(const std::string& id);

private:
  std::filesystem::path mRoot;                 // корень проекта
  bool mActive = false;
  SnapshotInfo mCurrent;
  int mSeq = 0;
};
