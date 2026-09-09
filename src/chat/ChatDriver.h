#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "ai/OpsParser.h"

class ToolRegistry;
class CdpClient;

// Автоматизация chat.deepseek.com через НАСТОЯЩИЙ браузер пользователя
// (Chrome/Edge), запущенный с отладочным портом. Никакого WebView2.
//
// Поток драйвера: следить за вкладкой → поллить JS-состояние → по автомату
// Sending → WaitingSettle → Ready забирать ответ и отдавать Ops.
class ChatDriver {
public:
  struct Cfg {
    ToolRegistry* tools = nullptr;
    std::function<void(const std::string& level, const std::string& msg)> log;
    std::string url = "https://chat.deepseek.com/";
  };

  struct Status {
    std::string stage;        // offline | launching | connecting | online | busy
    std::string browser;      // имя найденного браузера (chrome/msedge/…)
    std::string stageText;    // по-русски коротко («ожидаю ответ…»)
    std::string error;        // последняя ошибка (для UI)
    bool chatPresent = false; // поле ввода видно (залогинен)
    bool busy = false;
  };

  enum class Phase { Idle, Sending, WaitingSettle, Ready, TimedOut };

  explicit ChatDriver(Cfg cfg);
  ~ChatDriver();

  void Start();
  void Stop();

  Status GetStatus() const;
  bool Connected() const { return GetStatus().stage == "online" || GetStatus().stage == "busy"; }

  // Кнопка «Подключить чат»: форсировать поиск/запуск браузера и аттач.
  // Возвращает "" при успехе или текст ошибки.
  std::string ConnectNow();

  // Сообщить задачу (с промптом-обёрткой) или заметку (сырой текст).
  bool SendTask(const std::string& task, std::string& errorOut);
  bool SendNote(const std::string& text, std::string& errorOut);
  void CancelWait();
  void NewChat();

  // Ответ готов? Если да — забираем (один раз). parsed.text/ops/errors.
  bool ReplyReady() const;
  bool TakeReply(ParsedOps& out);

  Phase GetPhase() const;
  std::string LastError() const { std::lock_guard<std::mutex> lk(mMtx); return mError; }

private:
  struct Queued {
    bool isTask = false;
    std::string text;
  };

  void ThreadMain();
  void ThreadBody();                   // одна итерация цикла (для try/catch наверху)
  void TouchStatus(const std::function<void(Status&)>& fn);  // потокобезопасная правка
  bool EnsureBrowserLocked(std::string& err);           // под мьютексом НЕ держать
  bool AttachCdp(std::string& err);
  bool PollState(nlohmann::json& stateOut, std::string& err);
  bool DoSendNow(const Queued& q, std::string& err);
  std::string BuildPrompt(const std::string& task);
  static long long NowMs();

  Cfg mCfg;
  std::unique_ptr<CdpClient> mCdp;
  mutable std::mutex mMtx;
  Status mStatus;
  std::string mError;
  Phase mPhase = Phase::Idle;

  std::thread mThread;
  std::atomic<bool> mStop{false};
  std::atomic<bool> mConnectRequested{false};
  std::atomic<bool> mNewChatRequested{false};

  std::vector<Queued> mQueue;
  int mBaseline = -1;
  long long mSentAtMs = 0;
  long long mSettleSinceMs = 0;
  long long mLastBrowserTryMs = 0;
  long long mLastPollMs = 0;
  int mPort = 0;         // порт CDP, выбранный при запуске (0 = ещё не нашли)
  std::string mBrowserExe;
  std::string mUrl;

  ParsedOps mPendingReply;
  bool mReplyPending = false;
};
