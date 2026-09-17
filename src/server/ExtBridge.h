#pragma once

#include <mutex>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

class ToolRegistry;
class SnapshotManager;

// Мост расширения: обрабатывает сторону сервера для браузерного
// расширения DeepSeekIDE. Content script присылает ответы ассистента,
// мы — разбираем ops (тот же парсер, что у CDP-драйвера), ПРИМЕНЯЕМ их
// автоматически (тот же OpsApply), собираем системные заметки самообслу
// живания (тот же AutoNote) и возвращаем текст, который расширение
// вставит в чат. Потокобезопасно (вызывается из потоков http-сервера).
class ExtBridge {
public:
  struct Cfg {
    ToolRegistry* tools = nullptr;
    SnapshotManager* snaps = nullptr;
    class ProjectManager* project = nullptr;
    int port = 0;  // для ссылки «IDE ↗»
  };

  void Init(const Cfg& cfg);

  // Обработчики (чистые функции поверх состояния):
  nlohmann::json StateJson();
  nlohmann::json PromptJson();
  nlohmann::json HandleAnswer(const nlohmann::json& body);
  nlohmann::json SetAutopilot(const nlohmann::json& body);
  nlohmann::json ResetDialog();  // человек отправил своё сообщение
  nlohmann::json HomeJson() const;

private:
  Cfg mCfg;
  mutable std::mutex mMtx;
  bool mAutopilot = true;
  long long mLastSeenMs = 0;
  int mAnswers = 0;
  int mOpsApplied = 0;
  int mNotesSent = 0;
  std::string mLastReport;                              // последний отчёт ApplyChatOps
  std::set<std::string> mSentFiles, mSentSearches;     // дедуп самообслуживания
};
