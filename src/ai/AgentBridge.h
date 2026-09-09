#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <set>

class WebChatPanel;
class ToolRegistry;
class SnapshotManager;
class ProjectManager;

// Мост между IDE и живым chat.deepseek.com: собирает контекст проекта,
// отправляет задачу в веб-чат (БЕЗ всяких API-ключей), ждёт ответ,
// выделяет из него блоки ```deepseekide-ops и применяет операции к файлам
// со снапшотом для отката.
class AgentBridge {
public:
  struct Op {
    std::string name;
    nlohmann::json args;
  };

  struct ReplyInfo {
    std::string text;              // ответ без блоков операций
    std::vector<Op> ops;           // найденные операции
    std::vector<std::string> errs; // что не распарсилось
    long long receivedAtMs = 0;    // когда зафиксирован
  };

  enum class Phase {
    Idle,        // ничего не делаем — можно отправлять
    Sending,     // текст передан в страницу
    WaitingSettle, // новый ответ появился — дожидаемся окончания генерации
    Ready,       // ответ зафиксирован, ждёт разбора в GUI
    TimedOut     // чат не ответил
  };

  AgentBridge() = default;

  void Wire(WebChatPanel* panel, ToolRegistry* tools, SnapshotManager* snaps,
            ProjectManager* proj);

  Phase GetPhase() const { return mPhase; }
  bool Busy() const { return mPhase != Phase::Idle; }
  const std::string& LastError() const { return mError; }

  // Главное действие: сформировать промпт с деревом проекта и правилами
  // формата deepseekide-ops, отправить в чат. Возвращает false + errorOut,
  // если чат недоступен/занят.
  bool SendTask(const std::string& task, std::string& errorOut);
  // Отправить «сырой» текст (доп. вопрос, уточнение, содержимое файла).
  bool SendNote(const std::string& text, std::string& errorOut);

  void Cancel();  // снять ожидание (сам чат не трогаем — модель додумает)

  // Вызывать из GUI каждый кадр. Двигает автомат состояний по State панели.
  void Pump();
  // Забрать готовый ответ (true один раз). После Cancel/Reset авто-сброс.
  bool TakeReadyReply(ReplyInfo& out);

  std::string StatusText() const;  // короткая строка для UI

  // Парсинг (тестируемо статически): извлечь операции из текста ответа.
  // known — допустимые имена инструментов; незнакомые → errs.
  static bool ParseOps(const std::string& text, const std::set<std::string>& known,
                       ReplyInfo& out);

  // Применить операции (GUI-поток): снапшот → выполнить → commit.
  // Возвращает человекочитаемый отчёт по строкам.
  std::string ApplyOps(const std::vector<Op>& ops);

  // Список имён операций, известных реестру (для ParseOps и промпта).
  std::set<std::string> KnownOpNames() const;

private:
  std::string BuildPrompt(const std::string& task);
  bool DoSend(const std::string& text, std::string& errorOut);
  static long long NowMs();

  WebChatPanel* mPanel = nullptr;
  ToolRegistry* mTools = nullptr;
  SnapshotManager* mSnaps = nullptr;
  ProjectManager* mProj = nullptr;

  Phase mPhase = Phase::Idle;
  std::string mError;
  int mBaseline = -1;             // сколько ответов было ДО наших слов
  long long mSentAtMs = 0;
  long long mSettleSinceMs = 0;
  long long mLastSeenBusyMs = 0;
  ReplyInfo mReply;
  bool mReplyTaken = true;
  std::string mPendingText;       // текст, который должны были отправить
  int mSendRetries = 0;
};
