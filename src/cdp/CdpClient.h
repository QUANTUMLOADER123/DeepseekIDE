#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "ws/WsClient.h"

// Тонкий клиент Chrome DevTools Protocol поверх WebSocket.
// Поддерживает и page-level подключение, и browser-level + sessionId
// (flatten-attach: Target.attachToTarget → Runtime.evaluate с sessionId).
class CdpClient {
public:
  CdpClient() = default;
  ~CdpClient() { Close(); }

  // probePage=true: после рукопожатия проверяет «1+1» (для page-level).
  // Для browser-level подключения ставьте false — там нет домена Runtime.
  bool Connect(const std::string& wsDebuggerUrl, std::string& errorOut, int timeoutMs = 5000,
               bool probePage = true);
  bool Connected() const { return mWs.Connected(); }
  void Close();

  // Привязка к attachToTarget-сессии (flatten). Пустая строка — default session.
  void SetSession(const std::string& sessionId) { mSession = sessionId; }
  const std::string& Session() const { return mSession; }

  // Произвольная CDP-команда; respOut = поле "result" ответа.
  bool SendCmd(const std::string& method, const nlohmann::json& params, nlohmann::json& respOut,
               std::string& err, int timeoutMs = 8000);

  // Выполнить JS-выражение на странице (returnByValue) и вернуть значение.
  bool Evaluate(const std::string& expression, std::string& valueOut, std::string& errorOut,
                int timeoutMs = 8000);

private:
  WsClient mWs;
  int mNextId = 1;
  std::string mSession;
};
