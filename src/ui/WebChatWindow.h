#pragma once

#include <string>

// Отдельное системное окно с chat.deepseek.com:
//   Windows — WebView2, Linux — WebKitGTK, macOS — WKWebView.
// Реализация собирается только при DEEPSEEKIDE_WEBVIEW=1.
namespace webchat {

bool Available();
const char* UnavailableReason();

// Открывает (или поднимает) окно с веб-чатом. Потокобезопасно.
void EnsureOpen(const std::string& url = "https://chat.deepseek.com/");

}  // namespace webchat
