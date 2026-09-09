#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

class ToolRegistry;
class SnapshotManager;
struct Settings;

// События от рабочего потока агента в UI-поток.
struct AgentEvent {
  enum class Type {
    Status,     // текстовый статус ("Запрос к DeepSeek…")
    Token,      // порция стримящегося ответа
    ToolCall,   // модель вызвала инструмент (text = Describe)
    ToolResult, // инструмент отработал (text = обрезанный вывод, ok)
    Done,       // ответ завершён (text = финальный content если не стримился)
    Error,      // ошибка
    Cancelled   // отменено пользователем
  };
  Type type;
  std::string text;
  bool ok = true;
};

// Агент: цикл «модель → инструменты → модель …», снимки до/после, история.
// Вся сетевая работа — в отдельном потоке; UI забирает события через DrainEvents().
class Agent {
public:
  Agent() = default;
  ~Agent();

  void Wire(Settings* settings, ToolRegistry* tools, SnapshotManager* snaps);

  // Новая задача. Если агент занят — вызов игнорируется (вернёт false).
  bool Ask(const std::string& userText);
  void Cancel();
  bool Busy() const { return mBusy.load(); }

  std::vector<AgentEvent> DrainEvents();
  void ClearHistory();

  // Сохранение истории чата в проект (только user/assistant текст).
  void SaveHistory(const std::filesystem::path& file) const;
  void LoadHistory(const std::filesystem::path& file);
  // Пары (роль, текст) для восстановления ленты чата в UI.
  std::vector<std::pair<std::string, std::string>> HistoryPairs() const;

  const char* LastModelUsed() const { return mLastModel.c_str(); }

private:
  void ThreadMain(std::string userText);
  void Push(AgentEvent::Type type, std::string text, bool ok = true);

  Settings* mSettings = nullptr;
  ToolRegistry* mTools = nullptr;
  SnapshotManager* mSnaps = nullptr;

  mutable std::mutex mMtx;
  std::deque<AgentEvent> mEvents;
  std::vector<nlohmann::json> mHistory;  // OpenAI-формат messages

  std::thread mThread;
  std::atomic<bool> mBusy{false};
  std::atomic<bool> mCancel{false};
  std::string mLastModel;
};
