#include "ui/WebChatPanel.h"

#include <filesystem>

#include "app/Platform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
extern "C" int _putenv_s(const char*, const char*);  // CRT, без лишних инклюдов
#endif

#if defined(DEEPSEEKIDE_WEBVIEW)
#  if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <objbase.h>  // CoInitializeEx для COM-апартамента потока чата
#  endif
#  include <webview/webview.h>
#  define DSIDE_HAS_WV 1
#else
#  define DSIDE_HAS_WV 0
#endif

// ---------------------------------------------------------------------------
// JS-мост: селекторы chat.deepseek.com (стабильные классы ds-markdown /
// ds-button--primary / textarea; хеш-классы _xxxx не используем).
// ---------------------------------------------------------------------------

static const char* kInstallScript = R"JS(
(function(){
  if (window.__dsideInstalled) return 'installed';
  window.__dsideInstalled = 1;
  window.__dsideActivity = 0;
  window.__dsideSentAt = 0;
  function q(s){ return document.querySelector(s); }

  window.__dsideSendNow = function(text){
    var ta = q('textarea');
    if(!ta) return 'no_textarea';
    ta.focus(); ta.click();
    var proto = window.HTMLTextAreaElement && window.HTMLTextAreaElement.prototype;
    var desc = proto && Object.getOwnPropertyDescriptor(proto, 'value');
    if (desc && desc.set) desc.set.call(ta, text); else ta.value = text;
    ta.dispatchEvent(new Event('input', {bubbles:true}));
    ta.dispatchEvent(new Event('change', {bubbles:true}));
    window.__dsideSentAt = Date.now();
    window.__dsideActivity = Date.now();
    var tries = 0;
    var iv = setInterval(function(){
      var nodes = document.querySelectorAll('div[role=button]');
      for (var i = 0; i < nodes.length; i++){
        var c = nodes[i].className || '';
        if (typeof c === 'string' && c.indexOf('ds-button--primary') >= 0 &&
            c.indexOf('ds-button--disabled') < 0){
          nodes[i].click();
          clearInterval(iv);
          return;
        }
      }
      if (++tries > 40) clearInterval(iv);
    }, 100);
    return 'ok';
  };

  window.__dsideFocus = function(){
    var ta = q('textarea');
    if (ta){ ta.focus(); return 'ok'; }
    return 'no_textarea';
  };

  window.__dsideNewChat = function(){
    var nodes = document.querySelectorAll('a,button,div[role=button]');
    for (var i = 0; i < nodes.length; i++){
      var t = (nodes[i].innerText || '').replace(/\s+/g, ' ').trim();
      if (t === 'New chat' || t === 'Новый чат'){
        nodes[i].click();
        window.__dsideActivity = Date.now();
        return 'ok';
      }
    }
    return 'not_found';
  };

  var act = function(){ window.__dsideActivity = Date.now(); };
  var lastPush = 0;
  setInterval(function(){
    var root = document.body;
    if (!root) return;
    if (!window.__dsideObs){
      var mo = new MutationObserver(act);
      mo.observe(root, {childList:true, subtree:true, characterData:true});
      window.__dsideObs = 1;
    }
    var blocks = document.querySelectorAll('div.ds-markdown');
    var n = blocks.length;
    var last = n ? (blocks[n-1].innerText || '') : '';
    if (last.length > 60000) last = last.substring(last.length - 60000);
    var now = Date.now();
    var busy = (now - window.__dsideActivity) < 1300 || (now - window.__dsideSentAt) < 2500;
    var p = !!q('textarea');
    if (now - lastPush > 500){
      lastPush = now;
      var st = JSON.stringify({n:n, t:last, b:busy ? 1 : 0, p:p ? 1 : 0});
      if (window.dsideState){ try { window.dsideState(st); } catch(e){} }
    }
  }, 400);
  return 'installed_new';
})();
)JS";

// ---------------------------------------------------------------------------

namespace {

std::string JsVar(const std::string& s) { return nlohmann::json(s).dump(); }

std::string SendJs(const std::string& text) {
  return "window.__dsideSendNow ? String(window.__dsideSendNow(" + JsVar(text) +
         ")) : 'no_bridge';";
}

}  // namespace

// Хронологический boot-лог webchat-панели (то же boot.log, что и у приложения).
static void PanelBootStep(const std::string& step) {
  std::error_code ec;
  std::filesystem::create_directories(platform::ConfigDir(), ec);
  platform::AppendTextFile(platform::ConfigDir() / "boot.log",
                           "webchat: " + step + "\n");
}

#if DSIDE_HAS_WV && defined(_WIN32)
// WebView2 с GPU-композитингом на части машин (виртуалки, старые драйверы)
// рисует ПОЛНОСТЬЮ БЕЛОЕ окно при живом движке. Форсируем программный рендер
// через официальную переменную среды — если пользователь сам ничего не задал.
static void ForceSoftwareCompositing() {
  static bool done = false;
  if (done) return;
  done = true;
  if (std::getenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS") == nullptr)
    _putenv_s("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
              "--disable-gpu-compositing");
}
#endif

// ---------------------------------------------------------------------------
// Платформенная часть
// ---------------------------------------------------------------------------

struct WebChatPanel::Impl {
#if DSIDE_HAS_WV
  webview::webview* wv = nullptr;      // живёт в потоке панели
#endif
#if DSIDE_HAS_WV && defined(_WIN32)
  HWND child = nullptr;                // дочернее окно-контейнер браузера
  HWND parent = nullptr;
#endif
  bool shownOnce = false;              // окно-контейнер уже показано (Windows)
  bool threadDone = false;             // поток завершился (ставится из потока)
};

WebChatPanel::WebChatPanel() = default;

static bool sFirstStateLogged = false;

WebChatPanel::~WebChatPanel() { Detach(); }

bool WebChatPanel::Supported() const { return DSIDE_HAS_WV != 0; }

bool WebChatPanel::Running() const {
  std::lock_guard<std::mutex> lk(mMtx);
  return mLaunched && mImpl && !mImpl->threadDone;
}

std::string WebChatPanel::LastError() const {
  std::lock_guard<std::mutex> lk(mMtx);
  return mError;
}

#if DSIDE_HAS_WV && defined(_WIN32)
static const wchar_t* kChildClass = L"DeepSeekIDEWebChatChild";
static LRESULT CALLBACK ChildProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  return DefWindowProcW(h, msg, w, l);
}
static void EnsureChildClass() {
  static bool done = false;
  if (done) return;
  done = true;
  WNDCLASSW wc{};
  wc.lpfnWndProc = ChildProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kChildClass;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
}
#endif

bool WebChatPanel::Attach(void* parentHwnd, const std::string& url) {
  std::lock_guard<std::mutex> lk(mMtx);
  if (mLaunched) return true;
  if (!Supported()) {
    mError = "webview не собран (DEEPSEEKIDE_WEBVIEW=OFF)";
    return false;
  }
  mUrl = url;
  mImpl = std::make_unique<Impl>();
#if DSIDE_HAS_WV && defined(_WIN32)
  mImpl->parent = reinterpret_cast<HWND>(parentHwnd);
#endif
  mLaunched = true;
  mThread = std::thread([this] { ThreadMain(); });
  return true;
}

void WebChatPanel::Detach() {
  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (!mLaunched) return;
    mLaunched = false;  // больше никаких Eval из GUI
  }
#if DSIDE_HAS_WV
  webview::webview* w = nullptr;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (mImpl) w = mImpl->wv;
  }
  if (w) w->dispatch([w] { w->terminate(); });
#endif
  if (mThread.joinable()) mThread.join();
#if DSIDE_HAS_WV && defined(_WIN32)
  if (mImpl && mImpl->child) {
    DestroyWindow(mImpl->child);
    mImpl->child = nullptr;
  }
#endif
#if DSIDE_HAS_WV
  std::lock_guard<std::mutex> lk(mMtx);
  if (mImpl) mImpl->wv = nullptr;
#endif
}

#if DSIDE_HAS_WV && defined(_WIN32)
// WebView при EMBED-режиме (parent window) не инициализирует COM сам —
// конструктор Win32-бэкенда инициализирует COM только в ветке owns_window.
// Без CoInitializeEx(APARTMENTTHREADED) создание среды WebView2 молча фейлит
// (m_controller == nullptr → error_info{INVALID_STATE} с пустым сообщением).
struct ComApartment {
  HRESULT hr;
  bool mine = false;
  ComApartment() {
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    mine = SUCCEEDED(hr);  // S_OK или S_FALSE (повторно) — наш, надо CoUninitialize
  }
  ~ComApartment() { if (mine) CoUninitialize(); }
  bool usable() const { return mine || hr == RPC_E_CHANGED_MODE; }
};
#endif

void WebChatPanel::ThreadMain() {
  auto fail = [this](std::string msg) {
    std::lock_guard<std::mutex> lk(mMtx);
    mError = std::move(msg);
    if (mImpl) {
#if DSIDE_HAS_WV
      mImpl->wv = nullptr;
#endif
      mImpl->threadDone = true;
    }
  };

#if !DSIDE_HAS_WV
  fail("webview не собран (DEEPSEEKIDE_WEBVIEW=OFF)");
  return;
#else
  // Перебор профилей запуска WebView2: на части машин GPU-композитинг рисует
  // сплошное белое окно, на других антивирус/политика мешает дочерним процессам
  // (песочнице) — движок «жив», но страница мёртвая. Watchdog следит: если за
  // 12 секунд JS-мост не поднялся — пересоздаём движок со следующим профилем.
  const bool userArgs = std::getenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS") != nullptr;
  std::vector<std::string> profiles =
#  if defined(_WIN32)
      userArgs ? std::vector<std::string>{std::string()}
               : std::vector<std::string>{"--disable-gpu-compositing",
                                          "--no-sandbox --disable-gpu"};
#  else
      std::vector<std::string>{std::string()};
#  endif

#  if defined(_WIN32)
  PanelBootStep("thread start");
  if (userArgs) PanelBootStep("используются аргументы из WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS");
  ForceSoftwareCompositing();
  ComApartment com;
  if (!com.usable()) {
    char hbuf[24];
    std::snprintf(hbuf, sizeof(hbuf), "%08X", static_cast<unsigned>(com.hr));
    fail("COM-окружение недоступно (CoInitializeEx = 0x" + std::string(hbuf) + ")");
    return;
  }
  EnsureChildClass();
#  endif

  size_t attempt = 0;
  bool bridgeUp = false;
  for (; attempt < profiles.size() && !bridgeUp; ++attempt) {
    {
      std::lock_guard<std::mutex> lk(mMtx);
      if (!mLaunched) break;  // приложение закрывается
    }
#  if defined(_WIN32)
    if (!userArgs) {
      _putenv_s("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                profiles[attempt].empty() ? "" : profiles[attempt].c_str());
      PanelBootStep("попытка " + std::to_string(attempt + 1) + " из " +
                    std::to_string(profiles.size()) + ", args: " +
                    (profiles[attempt].empty() ? std::string("(пусто)") : profiles[attempt]));
    }
#  endif

    // Контейнер: на каждой попытке — новый (движок держит на него хендлы).
#if defined(_WIN32)
    if (mImpl->child) {
      DestroyWindow(mImpl->child);
      mImpl->child = nullptr;
      mImpl->shownOnce = false;
    }
    HWND child = CreateWindowExW(0, kChildClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0,
                                 400, 400, mImpl->parent, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    if (!child) {
      fail("Не удалось создать окно-контейнер для браузера (CreateWindowExW)");
      return;
    }
    mImpl->child = child;
    void* parentArg = child;
#else
    void* parentArg = nullptr;
#endif

    try {
      PanelBootStep("создаю controller WebView2…");
      webview::webview w(false, parentArg);  // может бросить exception — ловим
      {
        std::lock_guard<std::mutex> lk(mMtx);
        mImpl->wv = &w;
        mState = State{};  // состояние обнуляем под новую попытку
      }
      PanelBootStep("controller создан, захожу в run()");
      w.bind("dsideState", [this](const std::string& req) {
        OnStateJson(req.c_str());
        return std::string("ok");
      });
#  if !defined(_WIN32)
      w.set_title("DeepSeekIDE — chat.deepseek.com");
      w.set_size(760, 900, WEBVIEW_HINT_NONE);
#  endif
      w.navigate(mUrl);

      // Watchdog: инжектор моста + supervision попытки (тайм-аут 12 с).
      const long long startedAt =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count();
      std::atomic<bool> stopWd{false};
      auto* wp = &w;
      std::thread watchdog([this, &stopWd, startedAt, wp] {
        for (;;) {
          if (stopWd.load()) break;
          bool alive, ok;
          {
            std::lock_guard<std::mutex> lk(mMtx);
            alive = mLaunched;
            ok = mState.ok;
          }
          if (!alive) break;
          if (!ok) {
            InstallerTick();
            long long now =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            if (now - startedAt > 12000) {
              PanelBootStep("мост не поднялся за 12 с на этой попытке — пересоздаю");
              wp->dispatch([wp] { wp->terminate(); });
              break;
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        }
      });

      w.run();  // блокирует до terminate()

      stopWd.store(true);
      if (watchdog.joinable()) watchdog.join();
      {
        std::lock_guard<std::mutex> lk(mMtx);
        if (mImpl) mImpl->wv = nullptr;
        bridgeUp = mState.ok;
        if (!mLaunched) break;  // завершение приложения — не следующая попытка
      }
      if (bridgeUp) PanelBootStep("JS-мост поднялся");
      else PanelBootStep("движок завершился без живого моста");
    }
#if DSIDE_HAS_WV
    catch (const webview::exception& e) {
      std::string msg = "WebView2 (попытка " + std::to_string(attempt + 1) + "), код " +
                        std::to_string(static_cast<int>(e.error().code()));
      if (!e.error().message().empty()) msg += ": " + e.error().message();
      PanelBootStep(msg);
      {
        std::lock_guard<std::mutex> lk(mMtx);
        mError = msg;  // последний текст — покажем в Журнале позже
        if (!mLaunched) break;
        if (mImpl) mImpl->wv = nullptr;
      }
    }
#endif
    catch (const std::exception& e) {
      std::string msg = std::string("WebView2 (попытка ") + std::to_string(attempt + 1) +
                        "): " + e.what();
      PanelBootStep(msg);
      {
        std::lock_guard<std::mutex> lk(mMtx);
        mError = msg;
        if (!mLaunched) break;
        if (mImpl) mImpl->wv = nullptr;
      }
    } catch (...) {
      PanelBootStep("неизвестная ошибка при создании движка");
      {
        std::lock_guard<std::mutex> lk(mMtx);
        if (!mLaunched) break;
        if (mImpl) mImpl->wv = nullptr;
      }
    }
  }

  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (mImpl) {
      mImpl->wv = nullptr;
      mImpl->threadDone = true;
    }
  }

  if (!mLaunched) return;  // штатное завершение (detach)
  if (!bridgeUp) {
#  if defined(_WIN32)
    fail("Встроенный браузер не смог отрисовать chat.deepseek.com ни на одном "
         "профиле запуска (обычно это блокировка антивирусом/политикой — "
         "добавьте deepseekide.exe и msedgewebview2.exe в исключения, либо "
         "обновите драйвер видеокарты). Редактор IDE продолжит работать; "
         "подробности — в boot.log (папка %APPDATA%\\DeepSeekIDE).");
#  else
    fail("Встроенный браузер не смог загрузить chat.deepseek.com.");
#  endif
  }
#endif
}

void WebChatPanel::InstallerTick() {
#if DSIDE_HAS_WV
  webview::webview* w = nullptr;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (mImpl) w = mImpl->wv;
  }
  if (w) w->dispatch([w] { w->eval(kInstallScript); });
#else
  (void)this;
#endif
}

void WebChatPanel::OnStateJson(const char* json) {
  try {
    auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded()) return;
    State st;
    st.ok = true;
    st.messages = j.value("n", 0);
    st.busy = j.value("b", 0) != 0;
    st.chatPresent = j.value("p", 0) != 0;
    st.last = j.value("t", std::string{});
    std::lock_guard<std::mutex> lk(mMtx);
    mState = std::move(st);
    if (!sFirstStateLogged) {
      sFirstStateLogged = true;
      PanelBootStep("bridge state push received (JS жив)");
    }
  } catch (...) {
    // не роняем поток из-за битого JSON
  }
}

WebChatPanel::State WebChatPanel::GetState() const {
  std::lock_guard<std::mutex> lk(mMtx);
  return mState;
}

void WebChatPanel::SetRect(int x, int y, int w, int h) {
#if DSIDE_HAS_WV && defined(_WIN32)
  HWND child = nullptr;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (mImpl) child = mImpl->child;
  }
  if (child) {
    ::MoveWindow(child, x, y, w > 0 ? w : 1, h > 0 ? h : 1, TRUE);
    if (mVisible && !mImpl->shownOnce && w > 0 && h > 0) {
      mImpl->shownOnce = true;
      ShowWindow(child, SW_SHOW);
    }
  }
#else
  (void)x; (void)y; (void)w; (void)h;
#endif
}

void WebChatPanel::SetVisible(bool visible) {
  mVisible = visible;
#if DSIDE_HAS_WV && defined(_WIN32)
  HWND child = nullptr;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (mImpl) child = mImpl->child;
  }
  if (child && (!visible || mImpl->shownOnce))
    ShowWindow(child, visible ? SW_SHOW : SW_HIDE);
#else
  (void)visible;
#endif
}

void WebChatPanel::EvalLocked(const std::string& js) {
#if DSIDE_HAS_WV
  if (mImpl && mImpl->wv) {
    auto* w = mImpl->wv;
    w->dispatch([w, js] { w->eval(js); });
  }
#else
  (void)js;
#endif
}

void WebChatPanel::SendPrompt(const std::string& text) {
  std::lock_guard<std::mutex> lk(mMtx);
  if (!mLaunched || !mImpl) return;
  EvalLocked(SendJs(text));
}

void WebChatPanel::FocusInput() {
  std::lock_guard<std::mutex> lk(mMtx);
  if (!mLaunched || !mImpl) return;
  EvalLocked("window.__dsideFocus && window.__dsideFocus();");
}

void WebChatPanel::NewChat() {
  std::lock_guard<std::mutex> lk(mMtx);
  if (!mLaunched || !mImpl) return;
  EvalLocked("window.__dsideNewChat && window.__dsideNewChat();");
}
