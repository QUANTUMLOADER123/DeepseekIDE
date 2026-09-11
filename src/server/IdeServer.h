#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "ai/OpsParser.h"
#include "chat/ChatDriver.h"
#include "core/ProjectManager.h"
#include "core/SnapshotManager.h"

class ToolRegistry;

// Локальный HTTP-сервер DeepSeekIDE: статика (фронтенд) + JSON-API
// с проектом/файлами/инструментами/снимками/журналом и мостом к чат-драйверу.
// Слушает строго 127.0.0.1; /api/* требует токен (X-DeepSeekIDE-Token или ?t=).
class IdeServer {
public:
  IdeServer();
  ~IdeServer();

  struct Cfg {
    ToolRegistry* tools = nullptr;
    ProjectManager* project = nullptr;
    SnapshotManager* snaps = nullptr;
    ChatDriver* chat = nullptr;
    std::filesystem::path webRoot;   // папка assets/web рядом с exe (index.html внутри)
    std::string token;               // доступ к API
    std::function<void(const std::string& projectPath)> onOpenProject;
    // Настройки (тумблер shell и т.п.): чтение/запись со стороны владельца (main).
    std::function<nlohmann::json()> onGetSettings;
    std::function<nlohmann::json(const nlohmann::json& patch)> onSetSettings;
  };

  bool Start(const Cfg& cfg, int port, std::string& errOut);
  void Stop();
  int Port() const { return mPort; }
  bool IsShuttingDown() const;
  std::string Url() const;  // http://127.0.0.1:PORT/?t=TOKEN

  // Лента журнала для API (из любых потоков).
  void Log(const std::string& level, const std::string& msg);

  // Внешнее изменение файла (перечитать в редакторе): publish в /api/events.
  void NotifyFileChanged(const std::string& rel);

  // Фиксация диалога (задача → ответ): вызывается ChatDriver при готовом ответе.
  void RecordSession(const std::string& task, const ParsedOps& parsed);

private:
  struct Impl;
  std::string ServeFile(const std::string& rel, std::string& contentTypeOut, bool& okOut) const;
  nlohmann::json StateJson() const;
  nlohmann::json SnapshotsJson() const;
  struct LogEntry {
    long long atMs = 0;
    std::string level;
    std::string msg;
  };

  Cfg mCfg;
  int mPort = 0;
  std::unique_ptr<Impl> mImpl;

  mutable std::mutex mLogMtx;
  std::deque<LogEntry> mLog;
  int mLogSeq = 0;

  mutable std::mutex mEvMtx;
  std::deque<nlohmann::json> mEvents;  // fileChanged и т.п.
  int mEvSeq = 0;

  mutable std::mutex mSesMtx;
  std::vector<nlohmann::json> mSessions;
  std::filesystem::path mSessionsPath;
  int mSessionSeq = 0;
  void SaveSessionsLocked();
};
