#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace net {

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string error;  // транспортная ошибка (если есть)
  bool Ok() const { return error.empty() && status >= 200 && status < 300; }
};

// Минималистичный HTTP-клиент на libcurl. Потокобезопасен: каждый вызов
// создаёт свой curl-easy handle — вызывать можно из рабочих потоков агента.
class HttpClient {
public:
  // Простой POST, весь ответ в память.
  static HttpResponse Post(const std::string& url, const std::vector<std::string>& headers,
                           const std::string& body, long timeoutSec = 180);

  // Потоковый POST (SSE): сырые байты передаются в onData по мере поступления.
  // Возврат false из onData или установка *cancel обрывает передачу.
  static bool PostStream(const std::string& url, const std::vector<std::string>& headers,
                         const std::string& body,
                         const std::function<bool(const char* data, size_t len)>& onData,
                         std::atomic<bool>* cancel, HttpResponse* outMeta = nullptr,
                         long timeoutSec = 600);

  // GET (используется в самотестах и для проверки ключа).
  static HttpResponse Get(const std::string& url, const std::vector<std::string>& headers,
                          long timeoutSec = 30);

  static void GlobalInit();  // curl_global_init, вызывается один раз из main
};
}  // namespace net
