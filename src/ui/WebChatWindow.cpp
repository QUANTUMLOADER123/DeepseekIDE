#include "ui/WebChatWindow.h"

#include <atomic>
#include <thread>

namespace webchat {

#ifdef DEEPSEEKIDE_WEBVIEW

#include <webview/webview.h>

namespace {
std::atomic<int> gOpenCount{0};
}

bool Available() { return true; }
const char* UnavailableReason() { return ""; }

void EnsureOpen(const std::string& url) {
  // Окно живёт в собственном потоке — webview::run() блокирующий.
  std::thread([url] {
    gOpenCount.fetch_add(1);
    try {
      webview::webview w(false, nullptr);
      w.set_title("DeepSeek — chat.deepseek.com");
      w.set_size(1180, 800, WEBVIEW_HINT_NONE);
      w.navigate(url);
      w.run();  // блокируется до закрытия окна
    } catch (...) {
      // WebView2 runtime не установлен и т.п. — просто закрываем.
    }
    gOpenCount.fetch_sub(1);
  }).detach();
}

#else

bool Available() { return false; }

const char* UnavailableReason() {
  return "Окно веб-чата не собрано: соберите с DEEPSEEKIDE_ENABLE_WEBVIEW=ON "
         "(Windows: WebView2 Runtime; Linux: webkit2gtk-4.1; macOS: WKWebView есть из коробки).";
}

void EnsureOpen(const std::string&) {}

#endif

}  // namespace webchat
