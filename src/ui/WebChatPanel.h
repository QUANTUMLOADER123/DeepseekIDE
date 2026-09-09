#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Встроенная панель chat.deepseek.com с JS-мостом:
//  - Windows + WebView2: браузер живёт как дочернее окно внутри главного окна IDE,
//    панель получает прямоугольник каждый кадр (SetRect) и заполняет левую половину.
//  - Остальные платформы со webview: отдельное плавающее окно (SetRect игнорируется).
//  - Без webview: все методы — заглушки, Supported()==false.
//
// Мост со страницей: установщик внедряет window.__dside* функции и раз в ~0.5 с
// отдаёт состояние в C++ через bind("dsideState"). Вызовы JS — через dispatch() в
// UI-поток webview (сам webview живёт в собственном std::thread).
class WebChatPanel {
public:
  struct State {
    bool ok = false;          // мост установлен и присылает статус
    bool chatPresent = false; // поле ввода на странице обнаружено
    bool busy = false;        // страница «шевелится» (генерация/загрузка)
    int  messages = 0;        // количество блоков ds-markdown (ответы ассистента)
    std::string last;         // текст последнего ответа (может быть большим)
  };

  WebChatPanel();
  ~WebChatPanel();
  WebChatPanel(const WebChatPanel&) = delete;
  WebChatPanel& operator=(const WebChatPanel&) = delete;

  bool Supported() const;              // собрано ли с webview
  bool Running() const;
  const std::string& Url() const { return mUrl; }

  // parentHwnd — HWND главного окна (Windows) или nullptr (собственное окно).
  bool Attach(void* parentHwnd, const std::string& url);
  void Detach();                       // идемпотентно; вызывать при выходе

  // Координаты области чата в КЛИЕНТСКИХ координатах главного окна (Windows).
  void SetRect(int x, int y, int w, int h);
  void SetVisible(bool visible);

  // --- мост ---
  void SendPrompt(const std::string& text);  // вставить текст и нажать «Отправить»
  void FocusInput();                          // поставить фокус в поле ввода
  void NewChat();                             // нажать «New chat» на сайте

  State GetState() const;              // потокобезопасный слепок
  std::string LastError() const;

private:
  struct Impl;                          // платформенная часть, определена в .cpp

  void ThreadMain();
  void InstallerTick();                 // периодическая инъекция скрипта
  void OnStateJson(const char* json);   // из UI-потока webview
  void EvalLocked(const std::string& js);  // вызывать под mMtx — dispatch в поток webview

  mutable std::mutex mMtx;
  State mState;
  std::string mError;
  std::string mUrl;
  bool mVisible = true;

  std::unique_ptr<Impl> mImpl;
  std::thread mThread;
  bool mLaunched = false;
};
