#include "ws/WsClient.h"

#include <atomic>
#include <cstring>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace {

long long NowMs() {
#ifdef _WIN32
  return (long long)GetTickCount64();
#else
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

#ifdef _WIN32
bool EnsureWsa() {
  static std::atomic<int> state{0};
  if (state.load() == 1) return true;
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
  state.store(1);
  return true;
}
#endif

void CloseSock(WsClient::SockT s) {
#ifdef _WIN32
  closesocket(s);
#else
  ::close(s);
#endif
}

// 16 случайных байт → base64 для Sec-WebSocket-Key
std::string RandomWsKey() {
  static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint8_t rnd[16];
  for (auto& b : rnd) b = static_cast<uint8_t>(std::rand());
  std::string out;
  for (int i = 0; i < 16; i += 3) {
    uint32_t v = (uint32_t(rnd[i]) << 16) |
                 (i + 1 < 16 ? uint32_t(rnd[i + 1]) << 8 : 0) |
                 (i + 2 < 16 ? uint32_t(rnd[i + 2]) : 0);
    out.push_back(kB64[(v >> 18) & 63]);
    out.push_back(kB64[(v >> 12) & 63]);
    out.push_back(i + 1 < 16 ? kB64[(v >> 6) & 63] : '=');
    out.push_back(i + 2 < 16 ? kB64[v & 63] : '=');
  }
  return out;
}

}  // namespace

bool WsClient::ParseWsUrl(const std::string& url, std::string& host, uint16_t& port,
                          std::string& path) {
  if (url.rfind("ws://", 0) != 0) return false;
  std::string rest = url.substr(5);
  auto slash = rest.find('/');
  std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
  path = slash == std::string::npos ? "/" : rest.substr(slash);
  auto colon = authority.rfind(':');
  if (colon == std::string::npos) return false;
  host = authority.substr(0, colon);
  try {
    port = static_cast<uint16_t>(std::stoi(authority.substr(colon + 1)));
  } catch (...) {
    return false;
  }
  return !host.empty() && port != 0;
}

bool WsClient::Connect(const std::string& url, std::string& err, int timeoutMs) {
  std::string host, path;
  uint16_t port;
  if (!ParseWsUrl(url, host, port, path)) {
    err = "плохой ws:// URL: " + url;
    return false;
  }
#ifdef _WIN32
  if (!EnsureWsa()) {
    err = "WSAStartup failed";
    return false;
  }
#endif
  std::string numeric = host;
  if (host == "localhost") numeric = "127.0.0.1";

  SockT s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s == kInvalid) {
    err = "socket() failed";
    return false;
  }
  mSock = s;  // ВАЖНО: SendAll/SendFrame работают через mSock — назначаем СРАЗУ,
              // иначе handshake улетает на INVALID_SOCKET (баг "не удалось
              // отправить handshake" при живом браузере).
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_pton(AF_INET, numeric.c_str(), &sa.sin_addr) != 1) {
    err = "inet_pton failed для " + numeric;
    CloseSock(s);
    mSock = kInvalid;
    return false;
  }

  // Неблокирующий connect с select() — контролируемый таймаут
#ifdef _WIN32
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
#else
  int oldfl = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, oldfl | O_NONBLOCK);
#endif
  int rc = ::connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
  if (rc != 0) {
#ifdef _WIN32
    int e = WSAGetLastError();
    if (e != WSAEWOULDBLOCK && e != WSAEINPROGRESS) {
      err = "connect() failed (" + std::to_string(e) + ")";
      CloseSock(s);
      mSock = kInvalid;
      return false;
    }
#else
    if (errno != EINPROGRESS) {
      err = "connect() failed (" + std::to_string(errno) + ")";
      CloseSock(s);
      mSock = kInvalid;
      return false;
    }
#endif
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    rc = ::select(static_cast<int>(s + 1), nullptr, &wfds, nullptr, &tv);
    if (rc <= 0) {
      err = "connect timeout (" + std::to_string(timeoutMs) + " ms)";
      CloseSock(s);
      mSock = kInvalid;
      return false;
    }
    int soErr = 0;
#ifdef _WIN32
    int sl = sizeof(soErr);
#else
    socklen_t sl = sizeof(soErr);
#endif
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &sl);
    if (soErr != 0) {
      err = "connect refused (code " + std::to_string(soErr) + ")";
      CloseSock(s);
      mSock = kInvalid;
      return false;
    }
  }
#ifdef _WIN32
  nb = 0;
  ioctlsocket(s, FIONBIO, &nb);
#else
  fcntl(s, F_SETFL, oldfl & ~O_NONBLOCK);
#endif

  // HTTP Upgrade
  std::string req =
      "GET " + path + " HTTP/1.1\r\n"
      "Host: " + host + ":" + std::to_string(port) + "\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: " + RandomWsKey() + "\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Origin: http://" + host + "\r\n\r\n";
  bool sent = SendAll(req.data(), req.size());
  if (!sent) {
    err = "не удалось отправить handshake";
    CloseSock(s);
    mSock = kInvalid;
    return false;
  }
  // Ответ: читаем до \r\n\r\n (небольшой накопитель)
  std::string resp;
  resp.reserve(1024);
  long long endAt = NowMs() + timeoutMs;
  std::string head;
  while (head.find("\r\n\r\n") == std::string::npos) {
    timeval tv{0, 100000};
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    rc = ::select(static_cast<int>(s + 1), &rfds, nullptr, nullptr, &tv);
    if (NowMs() > endAt) break;
    if (rc <= 0) continue;
    char buf[1024];
    int n = ::recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;
    head.append(buf, n);
  }
  if (head.find(" 101") == std::string::npos || head.find("\r\n\r\n") == std::string::npos) {
    err = "WS handshake rejected: " + (head.size() > 80 ? head.substr(0, 80) : head);
    CloseSock(s);
    mSock = kInvalid;
    return false;
  }
  return true;
}

WsClient::~WsClient() { Close(); }

void WsClient::Close() {
  if (mSock == kInvalid) return;
  SendFrame(0x8, "");
#ifdef _WIN32
  shutdown(mSock, SD_BOTH);
#else
  shutdown(mSock, SHUT_RDWR);
#endif
  CloseSock(mSock);
  mSock = kInvalid;
}

bool WsClient::SendAll(const void* buf, size_t n) {
  const char* p = static_cast<const char*>(buf);
  while (n > 0) {
    int sent = ::send(mSock, p, static_cast<int>(n), 0);
    if (sent <= 0) return false;
    p += sent;
    n -= static_cast<size_t>(sent);
  }
  return true;
}

bool WsClient::SendFrame(uint8_t opcode, const std::string& payload) {
  if (mSock == kInvalid) return false;
  std::string frame;
  frame.reserve(payload.size() + 14);
  frame.push_back(static_cast<char>(0x80 | opcode));
  const uint64_t len = payload.size();
  if (len < 126) {
    frame.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(len)));
  } else if (len <= 0xFFFF) {
    frame.push_back(static_cast<char>(0x80 | 126));
    frame.push_back(static_cast<char>((len >> 8) & 0xFF));
    frame.push_back(static_cast<char>(len & 0xFF));
  } else {
    frame.push_back(static_cast<char>(0x80 | 127));
    for (int i = 7; i >= 0; --i) frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
  }
  // MASK (обязательна от клиента)
  uint8_t mask[4];
  for (auto& b : mask) b = static_cast<uint8_t>(std::rand());
  frame.append(reinterpret_cast<const char*>(mask), 4);
  for (size_t i = 0; i < payload.size(); ++i)
    frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
  return SendAll(frame.data(), frame.size());
}

bool WsClient::SendText(const std::string& payload) { return SendFrame(0x1, payload); }

bool WsClient::RecvExact(void* buf, size_t n, int timeoutMs) {
  char* p = static_cast<char*>(buf);
  long long endAt = NowMs() + timeoutMs;

  size_t got = 0;
  while (got < n) {
    timeval tv{0, 100000};
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(mSock, &rfds);
    int rc = ::select(static_cast<int>(mSock + 1), &rfds, nullptr, nullptr, &tv);
    if (NowMs() > endAt) return false;
    if (rc <= 0) continue;
    int r = ::recv(mSock, p + got, static_cast<int>(n - got), 0);
    if (r <= 0) return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

bool WsClient::RecvText(std::string& out, int timeoutMs) {
  out.clear();
  long long endAt = NowMs() + timeoutMs;

  for (;;) {
    // Заголовок фрейма
    uint8_t hdr[2];
    const long long left = std::max<long long>(1, endAt - NowMs());
    if (!RecvExact(hdr, 2, static_cast<int>(left))) return false;

    const bool fin = (hdr[0] & 0x80) != 0;
    const uint8_t opcode = hdr[0] & 0x0F;
    uint64_t len = hdr[1] & 0x7F;
    if (len == 126) {
      uint8_t ext[2];
      if (!RecvExact(ext, 2, 3000)) return false;
      len = (uint64_t(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
      uint8_t ext[8];
      if (!RecvExact(ext, 8, 3000)) return false;
      len = 0;
      for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
    }
    if (len > 32u * 1024u * 1024u) return false;  // защитный потолок

    std::string payload;
    payload.resize(static_cast<size_t>(len));
    if (len > 0 && !RecvExact(payload.data(), static_cast<size_t>(len), timeoutMs))
      return false;

    if (opcode == 0x9) {  // ping → pong
      SendFrame(0xA, payload);
      continue;
    }
    if (opcode == 0x8) return false;  // close
    if (opcode == 0x1 || opcode == 0x0) {
      out.append(payload);
      if (fin) return true;
      continue;  // continuation
    }
    // бинарь/прочее — пропускаем
  }
}
