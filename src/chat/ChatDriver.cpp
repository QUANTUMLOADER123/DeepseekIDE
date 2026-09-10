#include "chat/ChatDriver.h"

#include <chrono>
#include <sstream>

#include "ai/ToolRegistry.h"
#include "app/Platform.h"
#include "cdp/CdpClient.h"
#include "net/HttpClient.h"

namespace {

// -------------------------------------------------------------------- JS ---
// Скрипт состояния — исполняется каждый опрос через Runtime.evaluate.
// От наблюдателя на странице нужен только "пульс активности" для определения
// генерации (мутации DOM).
const char* kStateJs = R"JS(
(function(){
  try{
    if(typeof window.__dsideActivity==='undefined'){
      window.__dsideActivity=0;
      window.__dsideSentAt=0;
    }
    if(!window.__dsideObs){
      window.__dsideObs=true;
      var mo=new MutationObserver(function(){window.__dsideActivity=Date.now();});
      if(document.body)mo.observe(document.body,{childList:true,subtree:true,characterData:true});
    }
    var blocks=document.querySelectorAll('div.ds-markdown');
    var n=blocks.length;
    var last=n?(blocks[n-1].innerText||''):'';
    if(last.length>60000)last=last.substring(last.length-60000);
    var now=Date.now();
    var busy=(now-window.__dsideActivity)<1300||(now-window.__dsideSentAt)<2500;
    return JSON.stringify({ok:1,n:n,t:last,b:busy?1:0,p:!!document.querySelector('textarea'),u:String(location.href)});
  }catch(e){return JSON.stringify({ok:0,e:String(e)});}
})()
)JS";

std::string JsVar(const std::string& s) { return nlohmann::json(s).dump(); }

std::string SendJs(const std::string& text) {
  return std::string(
             "(function(){"
             "var text=")
          + JsVar(text) +
          ";"
          "var q=function(s){return document.querySelector(s);};"
          "var ta=q('textarea');"
          "if(!ta)return 'no_textarea';"
          "ta.focus();ta.click();"
          "var proto=window.HTMLTextAreaElement&&window.HTMLTextAreaElement.prototype;"
          "var desc=proto&&Object.getOwnPropertyDescriptor(proto,'value');"
          "if(desc&&desc.set)desc.set.call(ta,text);else ta.value=text;"
          "ta.dispatchEvent(new Event('input',{bubbles:true}));"
          "ta.dispatchEvent(new Event('change',{bubbles:true}));"
          "window.__dsideSentAt=Date.now();window.__dsideActivity=Date.now();"
          "var tries=0;"
          "var iv=setInterval(function(){"
          "  var nodes=document.querySelectorAll('div[role=button]');"
          "  for(var i=0;i<nodes.length;i++){"
          "    var c=nodes[i].className||'';"
          "    if(typeof c==='string'&&c.indexOf('ds-button--primary')>=0&&c.indexOf('ds-button--disabled')<0){"
          "      nodes[i].click();clearInterval(iv);return;"
          "    }"
          "  }"
          "  if(++tries>40)clearInterval(iv);"
          "},100);"
          "return 'ok';})()";
}

const char* kNewChatJs = R"JS(
(function(){
  var nodes=document.querySelectorAll('a,button,div[role=button]');
  for(var i=0;i<nodes.length;i++){
    var t=(nodes[i].innerText||'').replace(/\s+/g,' ').trim();
    if(t==='New chat'||t==='Новый чат'){nodes[i].click();return 'ok';}
  }
  return 'not_found';
})()
)JS";

}  // namespace

// ---------------------------------------------------------------------------

ChatDriver::ChatDriver(Cfg cfg) : mCfg(std::move(cfg)) {
  mUrl = mCfg.url;
  mStatus.stage = "offline";
  mStatus.stageText = "чат не подключен";
}

ChatDriver::~ChatDriver() { Stop(); }

void ChatDriver::Start() {
  bool expected = false;
  if (!mStop.compare_exchange_strong(expected, false)) return;
  mThread = std::thread([this] { ThreadMain(); });
}

void ChatDriver::Stop() {
  mStop.store(true);
  if (mThread.joinable()) mThread.join();
  std::lock_guard<std::mutex> lk(mMtx);
  if (mCdp) mCdp->Close();
}

long long ChatDriver::NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

ChatDriver::Status ChatDriver::GetStatus() const {
  std::lock_guard<std::mutex> lk(mMtx);
  return mStatus;
}

ChatDriver::Phase ChatDriver::GetPhase() const {
  std::lock_guard<std::mutex> lk(mMtx);
  return mPhase;
}

std::string ChatDriver::ConnectNow() {
  mConnectRequested.store(true);
  // Подождём аттач не дольше 20 секунд (пользовательский клик — интерактивно)
  for (int i = 0; i < 40; ++i) {
    {
      std::lock_guard<std::mutex> lk(mMtx);
      if (mStatus.stage == "online" || mStatus.stage == "busy") return "";
      if (!mStatus.error.empty() && mStatus.stage == "offline" && i > 4)
        return mStatus.error;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  return "не удалось подключиться за 20 секунд";
}

bool ChatDriver::SendTask(const std::string& task, std::string& err) {
  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (mPhase != Phase::Idle) {
      err = "ещё жду ответ на предыдущее — дождитесь или нажмите «Стоп»";
      return false;
    }
    if (mStatus.stage != "online" && mStatus.stage != "busy") {
      err = "чат не подключен — нажмите «Подключить чат»";
      return false;
    }
    mQueue.push_back(Queued{true, task});
    mError.clear();
  }
  return true;
}

bool ChatDriver::SendNote(const std::string& text, std::string& err) {
  {
    std::lock_guard<std::mutex> lk(mMtx);
    if (mPhase != Phase::Idle) {
      err = "ещё жду ответ на предыдущее — дождитесь или нажмите «Стоп»";
      return false;
    }
    if (mStatus.stage != "online" && mStatus.stage != "busy") {
      err = "чат не подключен";
      return false;
    }
    mQueue.push_back(Queued{false, text});
    mError.clear();
  }
  return true;
}

void ChatDriver::CancelWait() {
  std::lock_guard<std::mutex> lk(mMtx);
  mPhase = Phase::Idle;
  mQueue.clear();
  mStatus.stageText = "готов";
}

void ChatDriver::NewChat() { mNewChatRequested.store(true); }

bool ChatDriver::ReplyReady() const {
  std::lock_guard<std::mutex> lk(mMtx);
  return mReplyPending;
}

bool ChatDriver::TakeReply(ParsedOps& out) {
  std::lock_guard<std::mutex> lk(mMtx);
  if (!mReplyPending) return false;
  out = mPendingReply;
  mReplyPending = false;
  return true;
}

// ---------------------------------------------------------------------------

std::string ChatDriver::BuildPrompt(const std::string& task) {
  std::string tree = "(проект не открыт — дерево недоступно)";
  if (mCfg.tools) {
    ToolRunResult r = mCfg.tools->Execute("list_files", {{"max_entries", 350}});
    if (!r.output.empty()) tree = r.output;
  }

  std::ostringstream p;
  p << "Вы — агент программирования, встроенный в IDE DeepSeekIDE на моём компьютере. "
       "Отвечайте по-русски.\n\n"
       "ВАЖНЕЙШЕЕ ПРАВИЛО ФОРМАТА:\n"
       "Каждое изменение файлов оформляйте ТОЛЬКО блоком вида:\n"
       "```deepseekide-ops\n"
       "[{\"name\":\"write_file\",\"args\":{\"path\":\"src/main.cpp\",\"content\":\"полный текст "
       "файла\"}}]\n"
       "```\n"
       "Блоков может быть несколько — они выполняются сверху вниз. ВНУТРИ блока — только валидный "
       "JSON: один объект {\"name\": ..., \"args\": {...}} или массив таких объектов. Никакого текста, "
       "комментариев или пояснений внутри блока. Переводы строк в content — как \\n.\n\n"
       "ДОСТУПНЫЕ ОПЕРАЦИИ:\n"
       "- write_file {path, content} — создать или ПОЛНОСТЬЮ перезаписать файл\n"
       "- edit_file {path, old_string, new_string, replace_all?} — замена ТОЧНОГО фрагмента\n"
       "- append_file {path, content} — дописать в конец файла\n"
       "- insert_lines {path, line, content} — вставить текст ПЕРЕД строкой line (нумерация с 1; "
       "line = число строк + 1 — в конец файла)\n"
       "- replace_lines {path, start_line, end_line, content} — заменить строки start..end на новый "
       "текст (пустой content — удаление строк)\n"
       "- make_dir {path} — создать папку\n"
       "- delete_path {path, recursive?} — удалить файл; для папки — recursive:true\n"
       "- copy_file {from, to} — скопировать файл\n"
       "- move_file {from, to} — переместить/переименовать\n\n"
       "Если вам нужно СОДЕРЖИМОЕ существующего файла — напишите обычным текстом вне блоков: "
       "«НУЖЕН ФАЙЛ: <путь>», и я пришлю его следующим сообщением.\n"
       "Все пояснения — обычным текстом ВНЕ блоков deepseekide-ops.\n\n"
       "СТРУКТУРА ПРОЕКТА:\n"
       << tree << "\n\n"
        "ЗАДАЧА:\n"
        << task;
  return p.str();
}

// ---------------------------------------------------------------------------

bool ChatDriver::EnsureBrowserLocked(std::string& err) {
  // Уже есть живое подключение — ничего не делаем.
  if (mCdp && mCdp->Connected()) return true;

  const long long now = NowMs();
  if (now - mLastBrowserTryMs < 5000 && !mConnectRequested.load()) return false;
  mLastBrowserTryMs = now;

  // Порт: пробуем живой CDP (вдруг браузер уже запущен с прошлого раза)
  for (int port = 19226; port < 19240; ++port) {
    auto res = net::HttpClient::Get("http://127.0.0.1:" + std::to_string(port) + "/json/version",
                                    {}, 1);
    if (res.Ok()) {
      mPort = port;
      break;
    }
  }
  if (mPort == 0) {
    // Браузера с CDP нет — запускаем
    if (mBrowserExe.empty()) {
      if (!platform::FindChromiumBrowser(mBrowserExe, err)) {
        TouchStatus([&](Status& s) { s.stage="offline"; s.stageText="браузер не найден"; s.error = err; });
        mError = err;
        return false;
      }
    }
    mPort = platform::FindFreeTcpPort(19226, 19260);
    if (mPort == 0) {
      TouchStatus([](Status& s){ s.error = "не удалось выбрать свободный порт CDP"; });
      return false;
    }
    std::string args =
        "--remote-debugging-port=" + std::to_string(mPort) + " --remote-allow-origins=* " +
        " --user-data-dir=\"" + platform::PathToStr(platform::ConfigDir() / "browser-profile") +
        "\" --no-first-run --no-default-browser-check --new-window \"" + mUrl + "\"";
    mCfg.log("chat", "Запускаю браузер: " + mBrowserExe);
    TouchStatus([](Status& s){ s.stage = "launching"; s.stageText = "запускаю браузер…"; });
    if (!platform::LaunchDetached(mBrowserExe, args, err)) {
      TouchStatus([&](Status& s){ s.stage="offline"; s.stageText="не смог запустить браузер"; s.error = err; });
      mError = err;
      return false;
    }
    // Дадим ему подняться (poll /json/version до 15 с)
    for (int i = 0; i < 30; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      auto res = net::HttpClient::Get(
          "http://127.0.0.1:" + std::to_string(mPort) + "/json/version", {}, 1);
      if (res.Ok()) break;
      if (mStop.load()) return false;
    }
  }

  TouchStatus([&](Status& s){ s.browser = mBrowserExe; });
  return true;
}

// Основной путь подключения: browser-level WebSocket + Target.attachToTarget.
// He зависит от /json/list — а он ИНОГОДА ВРЁТ (пустой список при живых вкладках —
// наблюдали у пользователя: /json/new создаёт вкладку, а /json/list возвращает []).
bool ChatDriver::AttachViaBrowserWs(std::string& err) {
  const std::string base = "http://127.0.0.1:" + std::to_string(mPort);
  auto ver = net::HttpClient::Get(base + "/json/version", {}, 3);
  if (!ver.Ok()) {
    err = "/json/version недоступен (HTTP " + std::to_string(ver.status) + ", " + ver.error + ")";
    return false;
  }
  auto v = nlohmann::json::parse(ver.body, nullptr, false);
  const std::string bws = v.value("webSocketDebuggerUrl", std::string{});
  if (bws.empty()) {
    err = "у браузера нет browser-level WS (webSocketDebuggerUrl пуст)";
    return false;
  }
  const std::string browserName = v.value("Browser", std::string{});
  mCfg.log("chat", "browser-level WS найден, браузер: " + browserName);

  auto c = std::make_unique<CdpClient>();
  if (!c->Connect(bws, err, 5000, false)) {  // у browser-target нет Runtime-домена
    err = "browser WS не подключился: " + err;
    return false;
  }

  nlohmann::json r;
  if (!c->SendCmd("Target.getTargets", {}, r, err, 5000)) {
    err = "Target.getTargets: " + err;
    return false;
  }
  const auto targets = r.value("targetInfos", nlohmann::json::array());
  int pageCount = 0;
  std::string tid;
  for (const auto& t : targets) {
    if (t.value("type", std::string{}) != "page") continue;
    ++pageCount;
    if (t.value("url", std::string{}).find("deepseek.com") != std::string::npos)
      tid = t.value("targetId", std::string{});
  }
  mCfg.log("chat", "browser WS: page-таргетов " + std::to_string(pageCount) +
                       (tid.empty() ? ", deepseek среди них нет" : ", deepseek найден"));

  if (tid.empty()) {
    // deepseek-вкладки нет — создадим прямо через Target.createTarget
    if (!c->SendCmd("Target.createTarget",
                    {{"url", mUrl},
                     {"background", false},
                     {"newWindow", false}},
                    r, err, 5000)) {
      err = "Target.createTarget: " + err;
      return false;
    }
    tid = r.value("targetId", std::string{});
    if (tid.empty()) {
      err = "Target.createTarget не вернул targetId";
      return false;
    }
    mCfg.log("chat", "создал вкладку чата через Target.createTarget");
  }

  if (!c->SendCmd("Target.attachToTarget", {{"targetId", tid}, {"flatten", true}}, r, err,
                  5000)) {
    err = "Target.attachToTarget: " + err;
    return false;
  }
  const std::string sid = r.value("sessionId", std::string{});
  if (sid.empty()) {
    err = "attachToTarget: браузер не вернул sessionId (старый Chrome? скажите разработчику)";
    return false;
  }
  // Проверка: Runtime.evaluate в нашей сессии
  c->SetSession(sid);
  std::string probe;
  if (!c->Evaluate("location.href", probe, err, 5000)) {
    err = "сессия создана, но evaluate не работает: " + err;
    return false;
  }
  mCdp = std::move(c);
  mCfg.log("chat", "CDP attachToTarget ок, session=" + sid.substr(0, 8) +
                       "…, страница: " + probe.substr(0, 80));
  TouchStatus([](Status& s) { s.stage = "online"; s.stageText = "чат готов"; s.error.clear(); });
  return true;
}

bool ChatDriver::AttachCdp(std::string& err) {
  TouchStatus([](Status& s) { s.stage = "connecting"; s.stageText = "ищу вкладку chat.deepseek.com…"; });

  // Путь 1: browser-level (отказоустойчивый)
  {
    std::string e1;
    for (int attempt = 0; attempt < 40 && !mStop.load(); ++attempt) {
      e1.clear();
      if (AttachViaBrowserWs(e1)) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    mCfg.log("chat", "browser-WS путь: " + e1);
    err = e1 + "  |  fallback: ";
  }

  // Путь 2 (запасной): /json/list → page-level WS (классика).
  TouchStatus([](Status& s) { s.stageText = "перепроверяю список вкладок…"; });
  for (int attempt = 0; attempt < 40; ++attempt) {
    auto res = net::HttpClient::Get(
        "http://127.0.0.1:" + std::to_string(mPort) + "/json/list", {}, 1);
    if (res.Ok()) {
      auto arr = nlohmann::json::parse(res.body, nullptr, false);
      if (arr.is_array()) {
        for (const auto& t : arr) {
          const std::string type = t.value("type", std::string{});
          const std::string url = t.value("url", std::string{});
          // Гибкий матч: любая page-вкладка домена deepseek.com (м.б. редирект
          // Cloudflare/auth и т.п.) — её и берём под управление.
          if (type == "page" && url.find("deepseek.com") != std::string::npos) {
            const std::string ws = t.value("webSocketDebuggerUrl", std::string{});
            if (ws.empty()) break;
            mCdp = std::make_unique<CdpClient>();
            if (mCdp->Connect(ws, err, 4000)) {
              mCfg.log("chat", "CDP подключен к вкладке (json/list): " + url);
              TouchStatus([](Status& s) { s.stage = "online"; s.stageText = "чат готов"; s.error.clear(); });
              return true;
            }
            break;
          }
        }
      }
    }
    if (mStop.load()) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  err += "не нашёл вкладку chat.deepseek.com ни через browser-WS, ни через /json/list. "
         "Попробуйте кнопку «+ Вкладка» — она откроет её вручную.";
  TouchStatus([&](Status& s){ s.stage = "offline"; s.stageText = "нет вкладки чата"; s.error = err; });
  return false;
}

bool ChatDriver::PollState(nlohmann::json& out, std::string& err) {
  std::string val;
  if (!mCdp || !mCdp->Evaluate(kStateJs, val, err, 5000)) return false;
  out = nlohmann::json::parse(val, nullptr, false);
  if (out.is_discarded() || out.value("ok", 0) != 1) {
    err = "страница вернула неожиданность: " + val.substr(0, 120);
    return false;
  }
  return true;
}

bool ChatDriver::DoSendNow(const Queued& q, std::string& err) {
  std::string txt = q.isTask ? BuildPrompt(q.text) : q.text;
  std::string val;
  if (!mCdp || !mCdp->Evaluate(SendJs(txt), val, err, 6000)) return false;
  if (val.find("no_textarea") != std::string::npos) {
    err = "на странице нет поля ввода — войдите в учётку DeepSeek в окне браузера";
    TouchStatus([&](Status& s){ s.error = err; });
    return false;
  }
  mPhase = Phase::Sending;
  mSentAtMs = NowMs();
  mSettleSinceMs = 0;
  mBaseline = -1;  // узнаем на первом удачном опросе после отправки
  TouchStatus([](Status& s){ s.stage = "busy"; s.stageText = "отправлено, жду ответ…"; });
  mCfg.log("agent", std::string(q.isTask ? "Задача: " : "Заметка: ") +
                        (q.text.size() > 200 ? q.text.substr(0, 200) + "…" : q.text));
  mLastUserText = q.text;
  return true;
}


// Открыть вкладку chat.deepseek.com через DevTools HTTP (PUT /json/new).
std::string ChatDriver::OpenChatTab() {
  int port;
  std::string url;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    port = mPort;
    url = mUrl;
  }
  if (port == 0) {
    return "отладочный браузер ещё не запущен — сначала «Подключить чат»";
  }
  // минимальный urlencode для фиксированного URL
  auto enc = [](const std::string& s) {
    std::string o;
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char ch : s) {
      if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
          ch == '-' || ch == '_' || ch == '.' || ch == '~')
        o.push_back(static_cast<char>(ch));
      else {
        o.push_back('%');
        o.push_back(hex[(ch >> 4) & 15]);
        o.push_back(hex[ch & 15]);
      }
    }
    return o;
  };
  const std::string base = "http://127.0.0.1:" + std::to_string(port);
  auto r = net::HttpClient::Put(base + "/json/new?" + enc(url), {}, 5);
  if (!r.Ok()) {
    // старые Chrome принимали GET
    r = net::HttpClient::Get(base + "/json/new?" + enc(url), {}, 5);
  }
  if (r.Ok()) {
    mConnectRequested.store(true);
    mCfg.log("chat", "Вкладка чата создана через /json/new");
    return "";
  }
  // Запасной путь: создать вкладку через browser-level WS (Target.createTarget)
  {
    auto ver = net::HttpClient::Get(base + "/json/version", {}, 3);
    auto v = nlohmann::json::parse(ver.Ok() ? ver.body : std::string("null"), nullptr, false);
    const std::string bws = v.is_object() ? v.value("webSocketDebuggerUrl", std::string{}) : std::string{};
    if (!bws.empty()) {
      CdpClient c;
      std::string cerr;
      if (c.Connect(bws, cerr, 5000, false)) {
        nlohmann::json rr;
        if (c.SendCmd("Target.createTarget", {{"url", url}, {"background", false}}, rr, cerr, 5000) &&
            !rr.value("targetId", std::string{}).empty()) {
          mConnectRequested.store(true);
          mCfg.log("chat", "Вкладка чата создана через Target.createTarget");
          return "";
        }
      }
    }
  }
  return "не смог создать вкладку ни через /json/new (HTTP " + std::to_string(r.status) + " " +
         r.error + "), ни через Target.createTarget";
}

std::string ChatDriver::DebugInfo() {
  std::string browser, url, stage;
  int port;
  bool cdp;
  {
    std::lock_guard<std::mutex> lk(mMtx);
    browser = mBrowserExe.empty() ? mStatus.browser : mBrowserExe;
    url = mUrl;
    stage = mStatus.stage;
    port = mPort;
    cdp = mCdp && mCdp->Connected();
  }
  std::ostringstream s;
  s << "Браузер: " << (browser.empty() ? "(ещё не найден/не запущен)" : browser) << "\n";
  s << "URL чата: " << url << "\n";
  s << "Статус: " << stage << "\n";
  s << "CDP порт: " << (port ? std::to_string(port) : std::string("(не выбран)")) << "\n";
  s << "WebSocket CDP: " << (cdp ? "подключен" : "нет") << "\n";
  if (port == 0) {
    s << "\nПодсказка: нажмите «Подключить чат» — этап подключения сам найдёт браузер и запустит его.\n";
    return s.str();
  }
  {
    auto ver = net::HttpClient::Get("http://127.0.0.1:" + std::to_string(port) + "/json/version", {}, 3);
    s << "\n/json/version: HTTP " << ver.status;
    if (ver.Ok()) {
      auto v = nlohmann::json::parse(ver.body, nullptr, false);
      if (v.is_object()) {
        s << "\n  Browser: " << v.value("Browser", std::string{})
          << "\n  Protocol: " << v.value("Protocol-Version", std::string{})
          << "\n  browser-level WS: " << (v.value("webSocketDebuggerUrl", std::string{}).empty() ? "НЕТ" : "есть")
          << "\n";
      }
    } else {
      s << " ощибка: " << ver.error << "\n";
    }
  }
  s << "\nВкладки браузера (/json/list):\n";
  auto r = net::HttpClient::Get("http://127.0.0.1:" + std::to_string(port) + "/json/list", {}, 3);
  if (!r.Ok()) {
    s << "  [ошибка /json/list: HTTP " << r.status << " " << r.error << "]\n";
    return s.str();
  }
  auto arr = nlohmann::json::parse(r.body, nullptr, false);
  int cnt = 0;
  if (arr.is_array()) {
    for (const auto& t : arr) {
      if (t.value("type", std::string{}) != "page") continue;
      ++cnt;
      std::string tu = t.value("url", std::string{});
      std::string tt = t.value("title", std::string{});
      if (tt.size() > 60) tt = tt.substr(0, 57) + "…";
      s << "  " << cnt << ". " << tu << "\n     «" << tt << "»\n";
    }
  }
  if (cnt == 0) {
    s << "  (page-вкладок нет!) — список пуст при живом браузере;\n";
    s << "  attach всё равно пробуется через browser-WS, он от списка не зависит.\n";
    if (r.body.size() > 0 && r.body.size() < 400)
      s << "  сырой ответ: " + r.body + "\n";
  }
  return s.str();
}

// ---------------------------------------------------------------------------

void ChatDriver::TouchStatus(const std::function<void(Status&)>& fn) {
  std::lock_guard<std::mutex> lk(mMtx);
  fn(mStatus);
}

void ChatDriver::ThreadMain() {
  mCfg.log("chat", "Драйвер чата запущен (порт CDP автовыбор)");
  while (!mStop.load()) {
    try {
      ThreadBody();
    } catch (const std::exception& e) {
      // Любое исключение в потоке ранее роняло ВЕСЬ процесс — теперь просто лог
      // и пауза: UI покажет ошибку, драйвер продолжит жить.
      TouchStatus([&](Status& s) {
        s.error = std::string("внутренняя ошибка драйвера: ") + e.what();
        if (s.stage == "busy" || s.stage == "online") s.stage = "online";
      });
      {
        std::lock_guard<std::mutex> lk(mMtx);
        mError = std::string("внутренняя ошибка драйвера: ") + e.what();
      }
      mCfg.log("error", std::string("Чат-драйвер: ") + e.what());
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
  }

  {
    std::lock_guard<std::mutex> lk(mMtx);
    mStatus.stage = "offline";
  }
  mCfg.log("chat", "Драйвер чата остановлен");
}

// Одна итерация основного цикла.
void ChatDriver::ThreadBody() {
    // 1) Подключение/переподключение
    {
      std::lock_guard<std::mutex> lk(mMtx);
      if (mCdp && !mCdp->Connected()) {
        mCdp->Close();
        mCdp.reset();
        mStatus.stage = "offline";
        mStatus.stageText = "связь с вкладкой потеряна";
        mStatus.error = "вкладка чата закрыта? (автоповтор при следующем шаге)";
      }
    }

    bool connecting = false;
    {
      std::lock_guard<std::mutex> lk(mMtx);
      connecting = mCdp && mCdp->Connected();
    }
    if (!connecting) {
      // Ждем явный запрос пользователя? Нет — авто-попытка каждые 5 с: удобно.
      std::string err;
      if (EnsureBrowserLocked(err)) {
        if (!AttachCdp(err)) {
          mCfg.log("chat", "attach: " + err);
        }
      }
      mConnectRequested.store(false);
      if (!mStop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
      return;
    }

    // 2) Периодика: запрос "новый чат"
    if (mNewChatRequested.exchange(false)) {
      std::string v, e;
      if (mCdp->Evaluate(kNewChatJs, v, e, 3000))
        mCfg.log("chat", "Новый чат нажат");
      else
        mCfg.log("chat", "NewChat: " + e);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      return;
    }

    // 3) Очередь на отправку (только в Idle)
    Queued job;
    {
      std::lock_guard<std::mutex> lk(mMtx);
      if (mPhase != Phase::Idle || mQueue.empty()) {
      } else {
        job = mQueue.front();
        mQueue.erase(mQueue.begin());
      }
    }
    if (!job.text.empty()) {
      std::string err;
      if (!DoSendNow(job, err)) {
        mCfg.log("error", "Отправка: " + err);
        std::lock_guard<std::mutex> lk(mMtx);
        mError = err;
        mPhase = Phase::Idle;
      }
    }

    // 4) Опрос состояния страницы (не чаще раз в 700 мс)
    const long long now = NowMs();
    if (now - mLastPollMs >= 700) {
      mLastPollMs = now;
      nlohmann::json st;
      std::string err;
      if (!PollState(st, err)) {
        mCfg.log("chat", "poll: " + err);
        std::lock_guard<std::mutex> lk(mMtx);
        if (mCdp) mCdp->Close();
        return;
      }
      std::lock_guard<std::mutex> lk(mMtx);
      mStatus.chatPresent = st.value("p", 0) != 0;
      mStatus.busy = st.value("b", 0) != 0;
      const int n = st.value("n", 0);
      const long long nowL = NowMs();

      switch (mPhase) {
        case Phase::Idle:
        case Phase::Ready:
        case Phase::TimedOut:
          break;
        case Phase::Sending:
          if (mBaseline < 0) {
            mBaseline = n;
            mSettleSinceMs = 0;
          }
          if (n > mBaseline || mStatus.busy) {
            mPhase = Phase::WaitingSettle;
            mSettleSinceMs = 0;
          } else if (nowL - mSentAtMs > 15000) {
            // ни активности, ни нового ответа за 15 с — вероятно не залогинен/капча
            mPhase = Phase::TimedOut;
            mStatus.stage = "online";
            mStatus.stageText = "чат готов";
            mError = "нет реакции чата 15 с: войдите в учётку в окне браузера или решите капчу, "
                     "затем отправьте задачу снова.";
            mStatus.error = mError;
          }
          break;
        case Phase::WaitingSettle:
          if (mStatus.busy) {
            mSettleSinceMs = 0;
          } else if (mSettleSinceMs == 0) {
            mSettleSinceMs = nowL;
          }
          if (mSettleSinceMs != 0 && nowL - mSettleSinceMs > 1800) {
            // Тишина после генерации — ответ готов
            ParsedOps parsed;
            std::string raw = st.value("t", std::string{});
            dside::ExtractOps(raw, dside::MutationOps(), parsed);
            {
              mPendingReply = std::move(parsed);
              mReplyPending = true;
            }
            if (mCfg.onSession) {
              std::string task;
              { task = mLastUserText; }
              mCfg.onSession(task, mPendingReply);
            }
            mPhase = Phase::Ready;
            mStatus.stage = mStatus.chatPresent ? "online" : "online";
            mStatus.stageText = "ответ получен";
            mCfg.log("agent", "Ответ зафиксирован: " +
                                  std::to_string(mPendingReply.ops.size()) + " операций, " +
                                  std::to_string(mPendingReply.errors.size()) + " предупреждений");
            mBaseline = -1;
            return;
          }
          if (nowL - mSentAtMs > 6 * 60 * 1000) {
            mPhase = Phase::TimedOut;
            mStatus.stage = "online";
            mStatus.stageText = "чат готов";
            mError = "чат не ответил за 6 минут — ожидание снято.";
          }
          break;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
}

