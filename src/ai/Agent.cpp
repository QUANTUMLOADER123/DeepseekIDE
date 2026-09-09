#include "ai/Agent.h"

#include "ai/DeepSeekClient.h"
#include "ai/SystemPrompt.h"
#include "ai/ToolRegistry.h"
#include "app/Platform.h"
#include "core/Settings.h"
#include "core/SnapshotManager.h"

Agent::~Agent() {
  Cancel();
  mBusy.store(false);
  if (mThread.joinable()) mThread.join();
}

void Agent::Wire(Settings* settings, ToolRegistry* tools, SnapshotManager* snaps) {
  mSettings = settings;
  mTools = tools;
  mSnaps = snaps;
}

void Agent::Push(AgentEvent::Type type, std::string text, bool ok) {
  std::lock_guard<std::mutex> lk(mMtx);
  mEvents.push_back({type, std::move(text), ok});
  if (mEvents.size() > 4096) mEvents.pop_front();
}

std::vector<AgentEvent> Agent::DrainEvents() {
  std::vector<AgentEvent> out;
  std::lock_guard<std::mutex> lk(mMtx);
  out.reserve(mEvents.size());
  while (!mEvents.empty()) {
    out.push_back(std::move(mEvents.front()));
    mEvents.pop_front();
  }
  return out;
}

void Agent::ClearHistory() {
  std::lock_guard<std::mutex> lk(mMtx);
  mHistory.clear();
}

bool Agent::Ask(const std::string& userText) {
  if (mBusy.load()) return false;
  if (mThread.joinable()) mThread.join();
  mCancel.store(false);
  mBusy.store(true);
  mThread = std::thread(&Agent::ThreadMain, this, userText);
  return true;
}

void Agent::Cancel() {
  if (mBusy.load()) mCancel.store(true);
}

static std::string TrimFor(const std::string& s, size_t maxLen) {
  if (s.size() <= maxLen) return s;
  return s.substr(0, maxLen) + "\n… [обрезано для показа]";
}

void Agent::ThreadMain(std::string userText) {
  auto finish = [this] { mBusy.store(false); };

  if (!mSettings || mSettings->apiKey.empty()) {
    Push(AgentEvent::Type::Error,
         "Нет API-ключа DeepSeek. Откройте Настройки (Ctrl+,) и вставьте ключ "
         "с https://platform.deepseek.com — либо используйте Веб-чат chat.deepseek.com.");
    finish();
    return;
  }

  // Инструменты активны только при открытом проекте (корень снимков задан).
  const bool withTools = mTools != nullptr && mSnaps != nullptr && mSnaps->EnabledForProject();

  // --- Снимок на всё действие ---
  if (withTools && mSnaps && mSettings->autoSnapshot) {
    std::string title = userText;
    if (title.size() > 80) title = title.substr(0, 77) + "…";
    mSnaps->Begin(title);
  }

  // --- История ---
  {
    std::lock_guard<std::mutex> lk(mMtx);
    mHistory.push_back({{"role", "user"}, {"content", userText}});
  }

  DeepSeekClient client(mSettings->baseUrl, mSettings->apiKey);
  mLastModel = mSettings->model;

  const int maxSteps = mSettings->maxSteps > 0 ? mSettings->maxSteps : 32;
  bool done = false;

  for (int step = 0; step < maxSteps && !done; ++step) {
    if (mCancel.load()) break;

    // Собираем messages: system + история
    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "system"},
                        {"content", withTools ? prompts::kAgentSystemPrompt
                                              : prompts::kChatOnlySystemPrompt}});
    {
      std::lock_guard<std::mutex> lk(mMtx);
      for (const auto& m : mHistory) messages.push_back(m);
    }

    ChatOptions opts;
    opts.model = mSettings->model;
    opts.temperature = mSettings->temperature;
    const nlohmann::json* tools = nullptr;
    if (withTools) tools = &mTools->Schemas();
    opts.tools = tools;

    Push(AgentEvent::Type::Status,
         step == 0 ? "Отправка запроса в DeepSeek…" : "DeepSeek обрабатывает результаты…");

    std::atomic<bool>* cancelPtr = &mCancel;
    ChatResult res = client.ChatStream(
        messages, opts,
        [this](const std::string& delta) { Push(AgentEvent::Type::Token, delta); }, cancelPtr);

    if (mCancel.load() || res.cancelled) break;

    if (!res.ok) {
      Push(AgentEvent::Type::Error, res.error);
      done = true;
      break;
    }

    // Сохраняем ответ ассистента в историю (включая tool_calls в API-формате).
    {
      std::lock_guard<std::mutex> lk(mMtx);
      nlohmann::json am = {{"role", "assistant"}, {"content", res.content}};
      if (!res.rawToolCalls.empty()) am["tool_calls"] = res.rawToolCalls;
      mHistory.push_back(std::move(am));
    }

    if (res.toolCalls.empty()) {
      done = true;  // финальный ответ уже отстримлен по токенам
      break;
    }

    // Выполняем вызовы инструментов и возвращаем результаты модели
    for (const auto& tc : res.toolCalls) {
      if (mCancel.load()) break;
      Push(AgentEvent::Type::ToolCall, ToolRegistry::Describe(tc.name, tc.args));
      ToolRunResult tr = mTools->Execute(tc.name, tc.args);
      Push(AgentEvent::Type::ToolResult,
           (tr.ok ? "" : "[ошибка] ") + TrimFor(tr.output, 1200), tr.ok);

      std::lock_guard<std::mutex> lk(mMtx);
      mHistory.push_back({{"role", "tool"},
                          {"tool_call_id", tc.id},
                          {"name", tc.name},
                          {"content", TrimFor(tr.output, 24000)}});
    }
  }

  if (mCancel.load()) {
    Push(AgentEvent::Type::Cancelled, "Остановлено пользователем.");
    if (withTools && mSnaps && mSettings->autoSnapshot) mSnaps->Commit();
  } else {
    Push(AgentEvent::Type::Done, "");
    if (withTools && mSnaps && mSettings->autoSnapshot) mSnaps->Commit();
  }
  finish();
}

void Agent::SaveHistory(const std::filesystem::path& file) const {
  std::lock_guard<std::mutex> lk(mMtx);
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& m : mHistory) {
    const std::string role = m.value("role", "");
    if (role == "user" || role == "assistant") {
      // tool_calls не сохраняем — контекстная логика, при перезагрузке не нужна
      std::string content = m.value("content", "");
      if (!content.empty()) arr.push_back({{"role", role}, {"content", content}});
    }
    // Если у ассистента был пустой content (только tool_calls) — пропускаем.
  }
  platform::WriteTextFile(file, arr.dump(2));
}

std::vector<std::pair<std::string, std::string>> Agent::HistoryPairs() const {
  std::vector<std::pair<std::string, std::string>> out;
  std::lock_guard<std::mutex> lk(mMtx);
  for (const auto& m : mHistory) {
    std::string role = m.value("role", "");
    std::string content = m.value("content", "");
    if (role == "user" || role == "assistant") out.emplace_back(role, content);
  }
  return out;
}

void Agent::LoadHistory(const std::filesystem::path& file) {
  std::string text;
  if (!platform::ReadTextFile(file, text)) return;
  try {
    auto arr = nlohmann::json::parse(text);
    if (!arr.is_array()) return;
    std::lock_guard<std::mutex> lk(mMtx);
    mHistory.clear();
    for (const auto& m : arr) {
      std::string role = m.value("role", "");
      std::string content = m.value("content", "");
      if ((role == "user" || role == "assistant") && !content.empty())
        mHistory.push_back({{"role", role}, {"content", content}});
    }
  } catch (...) {
  }
}
