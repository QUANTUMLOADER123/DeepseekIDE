#pragma once

#include <cstdint>
#include <string>

// Минимальный WebSocket-клиент (RFC 6455, текстовые фреймы) поверх голого TCP.
// Нужен для Chrome DevTools Protocol (localhost, без TLS).
// Кроссплатформенно: Winsock2 (Windows) / BSD sockets (Linux, macOS).
class WsClient {
public:
  WsClient() = default;
  ~WsClient();
  WsClient(const WsClient&) = delete;
  WsClient& operator=(const WsClient&) = delete;

  // ws://host:port/path — host: только 127.0.0.1/localhost (CDP локален).
  bool Connect(const std::string& wsUrl, std::string& errorOut, int timeoutMs = 5000);
  bool Connected() const { return mSock != kInvalid; }
  void Close();

  // Отправить текстовый фрейм (masking обязателен для клиента — делаем).
  bool SendText(const std::string& payload);
  // Получить ПОЛНОЕ текстовое сообщение (склейка continuation-фреймов,
  // ping → pong отвечаем сами). false — разрыв/таймаут.
  bool RecvText(std::string& out, int timeoutMs);

#ifdef _WIN32
  using SockT = uintptr_t;
  static constexpr SockT kInvalid = static_cast<SockT>(~0ull);
#else
  using SockT = int;
  static constexpr SockT kInvalid = -1;
#endif

private:
  static bool ParseWsUrl(const std::string& url, std::string& host, uint16_t& port,
                         std::string& path);
  bool RecvExact(void* buf, size_t n, int timeoutMs);
  bool SendAll(const void* buf, size_t n);
  bool SendFrame(uint8_t opcode, const std::string& payload);

  SockT mSock = kInvalid;
};
