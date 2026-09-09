#include "ui/WebChatPanel.h"

#include <filesystem>

#include "app/Platform.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
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
  try {
#if DSIDE_HAS_WV
    void* parentArg = nullptr;
#  if defined(_WIN32)
    PanelBootStep("thread start");
    ForceSoftwareCompositing();
    ComApartment com;
    if (!com.usable()) {
      char hbuf[24];
      std::snprintf(hbuf, sizeof(hbuf), "%08X", static_cast<unsigned>(com.hr));
      fail("COM-окружение недоступно (CoInitializeEx = 0x" + std::string(hbuf) + ")");
      return;
    }
    EnsureChildClass();
    // Дочернее окно-контейнер; браузер подчиняется его размерам.
    // Изначально НЕ показываем — ShowWindow делает первый успешный SetRect
    // (иначе пользователь видит белый прямоугольник при старте).
    HWND child = CreateWindowExW(0, kChildClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0,
                                 400, 400, mImpl->parent, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    if (!child) {
      fail("Не удалось создать окно-контейнер для браузера (CreateWindowExW)");
      return;
    }
    mImpl->child = child;
    parentArg = child;
#  endif

    PanelBootStep("creating WebView2 controller…");
    webview::webview w(false, parentArg);  // МОЖЕТ БРОСИТЬ exception — ловим ниже
    {
      std::lock_guard<std::mutex> lk(mMtx);
      mImpl->wv = &w;
    }
    PanelBootStep("controller created OK");
    w.bind("dsideState", [this](const std::string& req) {
      OnStateJson(req.c_str());
      return std::string("ok");
    });
#  if !defined(_WIN32)
    w.set_title("DeepSeekIDE — chat.deepseek.com");
    w.set_size(760, 900, WEBVIEW_HINT_NONE);
#  endif

    PanelBootStep("navigating to chat url");
    w.navigate(mUrl);

    // Инжектор: потоковый ms-цикл — idempotent install каждые 1.5 с.
    std::thread injector([this] {
      for (;;) {
        {
          std::lock_guard<std::mutex> lk(mMtx);
          if (!mLaunched) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        {
          std::lock_guard<std::mutex> lk(mMtx);
          if (!mLaunched) break;
        }
        InstallerTick();
      }
    });

    w.run();  // блокирует до terminate()

    if (injector.joinable()) injector.join();
    {
      std::lock_guard<std::mutex> lk(mMtx);
      if (mImpl) {
        mImpl->wv = nullptr;
        mImpl->threadDone = true;
      }
    }
#else
    fail("webview не собран (DEEPSEEKIDE_WEBVIEW=OFF)");
#endif
#if DSIDE_HAS_WV
  } catch (const webview::exception& e) {
    std::string msg = "Не удалось запустить встроенный браузер (webview код " +
                      std::to_string(static_cast<int>(e.error().code())) + ")";
    if (!e.error().message().empty()) msg += ": " + e.error().message();
#  if defined(_WIN32)
    msg += ". Если Microsoft Edge у вас обычно работает, а ошибка остаётся — "
           "WebView2 может блокироваться групповой политикой или антивирусом; "
           "попробуйте запуск от имени обычного пользователя без «песочницы».";
#  endif
    fail(msg);
#endif
  } catch (const std::exception& e) {
#if DSIDE_HAS_WV && defined(_WIN32)
    fail(std::string("Не удалось запустить встроенный браузер: ") + e.what() +
         ". Возможно, отсутствует WebView2 Runtime — скачайте бесплатный "
         "Evergreen Bootstrapper c сайта Microsoft (go.microsoft.com/fwlink/p/?LinkId=2124703). "
         "Редактор IDE продолжит работать без чата.");
#else
    fail(std::string("Не удалось запустить встроенный браузер: ") + e.what());
#endif
  } catch (...) {
    fail("Не удалось запустить встроенный браузер (неизвестная ошибка)");
  }
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
