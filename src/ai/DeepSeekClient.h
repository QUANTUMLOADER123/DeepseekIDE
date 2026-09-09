#pragma once

#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Один вызов инструмента, пришедший от модели.
struct ToolCall {
  std::string id;
  std::string name;
  nlohmann::json args;
};

// Итог одного запроса к chat/completions.
struct ChatResult {
  bool ok = false;
  bool cancelled = false;
  std::string error;              // человекочитаемая ошибка (транспорт/API)
  std::string content;            // текст ассистента (может быть пустым при tool-only)
  std::string finishReason;
  std::vector<ToolCall> toolCalls;
  nlohmann::json rawToolCalls;    // для воспроизведения в истории (API это требует)
  int promptTokens = 0;
  int completionTokens = 0;
};

struct ChatOptions {
  std::string model = "deepseek-chat";
  float temperature = 0.3f;
  const nlohmann::json* tools = nullptr;  // может быть nullptr
};

// Клиент DeepSeek API (OpenAI-совместимый): https://api.deepseek.com
// Потоковый режим — Server-Sent Events с агрегацией tool_call-дельт.
class DeepSeekClient {
public:
  DeepSeekClient(std::string baseUrl, std::string apiKey);
  void SetCredentials(std::string baseUrl, std::string apiKey);
  bool HasKey() const { return !mApiKey.empty(); }

  // Потоковый чат. onToken вызывается на каждой порции текста (из рабочего потока!).
  ChatResult ChatStream(const nlohmann::json& messages, const ChatOptions& opts,
                        const std::function<void(const std::string& delta)>& onToken,
                        std::atomic<bool>* cancel);

  // Неблокирующий по токенам вариант (весь ответ целиком).
  ChatResult Chat(const nlohmann::json& messages, const ChatOptions& opts);

  // --- Вспомогательные функции вынесены в public для unit-тестов ---
  // Разбор одной SSE-строки "data: {...}". Возвращает true, если извлечён чанк.
  static bool ParseSseLine(const std::string& line, nlohmann::json& outChunk, bool& outDone);
  // Скармливает пришедшие байты, вызывает onChunk на каждый полный JSON-чанк.
  static void ConsumeSseBuffer(std::string& buf,
                               const std::function<void(const nlohmann::json&)>& onChunk);
  // Слияние дельты в результат (content + tool_calls + usage).
  void Aggregate(const nlohmann::json& chunk, ChatResult& acc) const;

private:
  std::string Endpoint() const;
  std::vector<std::string> MakeHeaders() const;
  nlohmann::json MakePayload(const nlohmann::json& messages, const ChatOptions& opts,
                             bool stream) const;
  void HandleApiError(long status, const std::string& body, ChatResult& res) const;

  std::string mBaseUrl;
  std::string mApiKey;
};
