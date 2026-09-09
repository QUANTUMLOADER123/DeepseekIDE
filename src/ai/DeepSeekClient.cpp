#include "ai/DeepSeekClient.h"

#include <map>

#include "net/HttpClient.h"

DeepSeekClient::DeepSeekClient(std::string baseUrl, std::string apiKey)
    : mBaseUrl(std::move(baseUrl)), mApiKey(std::move(apiKey)) {}

void DeepSeekClient::SetCredentials(std::string baseUrl, std::string apiKey) {
  mBaseUrl = std::move(baseUrl);
  mApiKey = std::move(apiKey);
}

std::string DeepSeekClient::Endpoint() const {
  std::string base = mBaseUrl.empty() ? "https://api.deepseek.com" : mBaseUrl;
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base + "/chat/completions";
}

std::vector<std::string> DeepSeekClient::MakeHeaders() const {
  return {"Content-Type: application/json", "Accept: application/json",
          "Authorization: Bearer " + mApiKey};
}

nlohmann::json DeepSeekClient::MakePayload(const nlohmann::json& messages, const ChatOptions& opts,
                                           bool stream) const {
  nlohmann::json payload;
  payload["model"] = opts.model.empty() ? "deepseek-chat" : opts.model;
  payload["messages"] = messages;
  payload["temperature"] = opts.temperature;
  payload["stream"] = stream;
  if (opts.tools && !opts.tools->empty()) {
    payload["tools"] = *opts.tools;
    payload["tool_choice"] = "auto";
  }
  return payload;
}

void DeepSeekClient::HandleApiError(long status, const std::string& body, ChatResult& res) const {
  res.ok = false;
  std::string detail;
  try {
    auto j = nlohmann::json::parse(body);
    detail = j["error"]["message"].get<std::string>();
  } catch (...) {
    detail = body.substr(0, 400);
  }
  res.error = "DeepSeek API: HTTP " + std::to_string(status);
  if (status == 401) res.error += " (неверный API-ключ — проверьте Настройки)";
  if (status == 402) res.error += " (недостаточно средств на балансе DeepSeek)";
  if (status == 429) res.error += " (превышен лимит запросов — подождите немного)";
  if (!detail.empty()) res.error += "\n" + detail;
}

void DeepSeekClient::Aggregate(const nlohmann::json& chunk, ChatResult& acc) const {
  if (chunk.contains("usage") && chunk["usage"].is_object()) {
    acc.promptTokens = chunk["usage"].value("prompt_tokens", acc.promptTokens);
    acc.completionTokens = chunk["usage"].value("completion_tokens", acc.completionTokens);
  }
  if (!chunk.contains("choices") || !chunk["choices"].is_array() || chunk["choices"].empty())
    return;
  const auto& choice = chunk["choices"][0];

  if (choice.contains("finish_reason") && choice["finish_reason"].is_string())
    acc.finishReason = choice["finish_reason"].get<std::string>();

  // delta (stream) или message (non-stream)
  const nlohmann::json* delta = nullptr;
  if (choice.contains("delta")) delta = &choice["delta"];
  else if (choice.contains("message")) delta = &choice["message"];
  if (!delta) return;

  if (delta->contains("content") && (*delta)["content"].is_string()) {
    // content сам event-loop-ом добавляется через onToken; здесь — финальный сбор.
  }
  if (delta->contains("tool_calls") && (*delta)["tool_calls"].is_array()) {
    for (const auto& tc : (*delta)["tool_calls"]) {
      int index = tc.value("index", 0);
      while (static_cast<int>(acc.rawToolCalls.size()) <= index)
        acc.rawToolCalls.push_back({{"id", ""},
                                    {"type", "function"},
                                    {"function", {{"name", ""}, {"arguments", ""}}}});
      auto& slot = acc.rawToolCalls[index];
      if (tc.contains("id") && tc["id"].is_string() && !tc["id"].get<std::string>().empty())
        slot["id"] = tc["id"];
      if (tc.contains("function") && tc["function"].is_object()) {
        const auto& fn = tc["function"];
        if (fn.contains("name") && fn["name"].is_string()) {
          std::string cur = slot["function"]["name"].get<std::string>();
          slot["function"]["name"] = cur + fn["name"].get<std::string>();
        }
        if (fn.contains("arguments") && fn["arguments"].is_string()) {
          std::string cur = slot["function"]["arguments"].get<std::string>();
          slot["function"]["arguments"] = cur + fn["arguments"].get<std::string>();
        }
      }
    }
  }
}

bool DeepSeekClient::ParseSseLine(const std::string& lineIn, nlohmann::json& outChunk,
                                  bool& outDone) {
  std::string line = lineIn;
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
  if (line.rfind("data:", 0) != 0) return false;
  std::string data = line.substr(5);
  size_t first = data.find_first_not_of(" \t");
  if (first != std::string::npos) data = data.substr(first);
  if (data == "[DONE]") {
    outDone = true;
    return false;
  }
  try {
    outChunk = nlohmann::json::parse(data);
    return true;
  } catch (...) {
    return false;  // поломанная строка — пропускаем
  }
}

void DeepSeekClient::ConsumeSseBuffer(
    std::string& buf, const std::function<void(const nlohmann::json&)>& onChunk) {
  size_t pos;
  while ((pos = buf.find('\n')) != std::string::npos) {
    std::string line = buf.substr(0, pos);
    buf.erase(0, pos + 1);
    bool done = false;
    nlohmann::json chunk;
    if (ParseSseLine(line, chunk, done)) onChunk(chunk);
  }
}

ChatResult DeepSeekClient::ChatStream(const nlohmann::json& messages, const ChatOptions& opts,
                                      const std::function<void(const std::string& delta)>& onToken,
                                      std::atomic<bool>* cancel) {
  ChatResult acc;
  const std::string body = MakePayload(messages, opts, true).dump();
  net::HttpResponse meta;

  std::string sseBuffer;
  bool transportOk = net::HttpClient::PostStream(
      Endpoint(), MakeHeaders(), body,
      [&](const char* data, size_t len) -> bool {
        sseBuffer.append(data, len);
        bool sawWork = false;
        ConsumeSseBuffer(sseBuffer, [&](const nlohmann::json& chunk) {
          sawWork = true;
          Aggregate(chunk, acc);
          // контентные дельты → наружу (для живого отображения в чате)
          if (chunk.contains("choices") && chunk["choices"].is_array() &&
              !chunk["choices"].empty()) {
            const auto& ch = chunk["choices"][0];
            if (ch.contains("delta") && ch["delta"].is_object() &&
                ch["delta"].contains("content") && ch["delta"]["content"].is_string()) {
              std::string d = ch["delta"]["content"].get<std::string>();
              acc.content += d;
              if (!d.empty() && onToken) onToken(d);
            }
          }
        });
        (void)sawWork;
        return true;
      },
      cancel, &meta, 600);

  // Последний кусок буфера без '\n' (если сервер не завершил переносом)
  if (!sseBuffer.empty()) {
    bool done = false;
    nlohmann::json chunk;
    if (ParseSseLine(sseBuffer, chunk, done)) {
      Aggregate(chunk, acc);
      if (chunk.contains("choices") && chunk["choices"].is_array() && !chunk["choices"].empty()) {
        const auto& ch = chunk["choices"][0];
        if (ch.contains("delta") && ch["delta"].is_object() && ch["delta"].contains("content") &&
            ch["delta"]["content"].is_string()) {
          std::string d = ch["delta"]["content"].get<std::string>();
          acc.content += d;
          if (!d.empty() && onToken) onToken(d);
        }
      }
    }
  }

  if (cancel && cancel->load()) {
    acc.cancelled = true;
    acc.error = "отменено пользователем";
    return acc;
  }
  if (!transportOk) {
    acc.ok = false;
    acc.error = "Сетевая ошибка: " + (meta.error.empty() ? "нет соединения" : meta.error);
    if (!meta.body.empty()) acc.error += "\n" + meta.body.substr(0, 300);
    return acc;
  }
  if (meta.status < 200 || meta.status >= 300) {
    // При ошибке тело — plain JSON (не SSE), оно осталось необработанным в буфере.
    HandleApiError(meta.status, sseBuffer, acc);
    return acc;
  }

  // Собираем распарсенные вызовы инструментов
  for (const auto& raw : acc.rawToolCalls) {
    ToolCall tc;
    tc.id = raw.value("id", "");
    tc.name = raw["function"].value("name", "");
    const std::string argsStr = raw["function"].value("arguments", "");
    try {
      tc.args = argsStr.empty() ? nlohmann::json::object() : nlohmann::json::parse(argsStr);
    } catch (...) {
      acc.ok = false;
      acc.error = "Модель вернула некорректные аргументы вызова " + tc.name;
      return acc;
    }
    if (!tc.name.empty()) acc.toolCalls.push_back(std::move(tc));
  }
  acc.ok = true;
  return acc;
}

ChatResult DeepSeekClient::Chat(const nlohmann::json& messages, const ChatOptions& opts) {
  ChatResult res;
  const std::string body = MakePayload(messages, opts, false).dump();
  auto http = net::HttpClient::Post(Endpoint(), MakeHeaders(), body, 180);
  if (!http.error.empty()) {
    res.error = "Сетевая ошибка: " + http.error;
    return res;
  }
  if (!http.Ok()) {
    HandleApiError(http.status, http.body, res);
    return res;
  }
  try {
    auto j = nlohmann::json::parse(http.body);
    res.promptTokens = j.value("usage", nlohmann::json::object()).value("prompt_tokens", 0);
    if (!j.contains("choices") || j["choices"].empty()) {
      res.error = "Пустой ответ API";
      return res;
    }
    const auto& msg = j["choices"][0]["message"];
    res.content = msg.value("content", "");
    res.finishReason = j["choices"][0].value("finish_reason", "");
    res.rawToolCalls = nlohmann::json::array();
    if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
      for (const auto& raw : msg["tool_calls"]) {
        ToolCall tc;
        tc.id = raw.value("id", "");
        tc.name = raw["function"].value("name", "");
        try {
          tc.args = nlohmann::json::parse(raw["function"].value("arguments", "{}"));
        } catch (...) {
          tc.args = nlohmann::json::object();
        }
        res.toolCalls.push_back(tc);
        res.rawToolCalls.push_back(raw);
      }
    }
    res.ok = true;
  } catch (const std::exception& e) {
    res.error = std::string("Не удалось разобрать ответ API: ") + e.what();
  }
  return res;
}
