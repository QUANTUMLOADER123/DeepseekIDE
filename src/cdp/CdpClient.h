#pragma once

#include <functional>
#include <string>

#include "ws/WsClient.h"

// Тонкий клиент Chrome DevTools Protocol поверх WebSocket:
// только то, что нужно нашему веб-агенту — Runtime.evaluate.
class CdpClient {
public:
  CdpClient() = default;
  ~CdpClient() { Close(); }

  bool Connect(const std::string& wsDebuggerUrl, std::string& errorOut, int timeoutMs = 5000);
  bool Connected() const { return mWs.Connected(); }
  void Close();

  // Выполнить JS-выражение на странице (returnByValue) и вернуть значение.
  // ok==false → соединение сдохло или CDP ошибка (errorOut — причина).
  bool Evaluate(const std::string& expression, std::string& valueOut, std::string& errorOut,
                int timeoutMs = 8000);

private:
  WsClient mWs;
  int mNextId = 1;
};
