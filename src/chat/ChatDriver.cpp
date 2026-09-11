#include "chat/ChatDriver.h"

#include <chrono>
#include <sstream>

#include "ai/ToolRegistry.h"
#include "app/Platform.h"
#include "cdp/CdpClient.h"
#include "net/HttpClient.h"
#include "util/TextStitch.h"
#include "util/Utf8.h"

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
    if(last.length>60000){
      // Срез UTF-16 может попасть посреди суррогатной пары (эмодзи) —
      // lone surrogate ломает JSON-декодер на нашей стороне. Отступаем.
      last=last.substring(last.length-60000);
      var c0=last.charCodeAt(0);
      if(c0>=0xDC00&&c0<=0xDFFF)last=last.substring(1);
    }
    var now=Date.now();
    // DOM-сбор блоков deepseekide-ops: страница рендерит кодовый блок виджетом
    // с баннером «deepseekide-ops | Копировать | Скачать», и в innerText ЗАБОРА
    // (```) НЕ ОСТАЁТСЯ — текстовый парсер их в этом случае не увидит. Зато
    // текст кода живёт в <pre><code>, а кнопки — ВНЕ <code>.
    var opsBodies=[];
    if(n){
      try{
        var pres=blocks[n-1].querySelectorAll('pre');
        for(var pi=0;pi<pres.length&&opsBodies.length<20;pi++){
          var pre=pres[pi];
          var code=pre.querySelector('code');
          var ct=((code?code.textContent:pre.textContent)||'').replace(/\n+$/,'');
          if(!ct||(ct.indexOf('{')<0&&ct.indexOf('[')<0))continue;
          // Подпись языка — в баннере, т.е. в тексте предка БЕЗ текста кода.
          var host=pre,found=false;
          for(var up=0;up<5&&host;up++){
            var ht=host.textContent||'';
            if(ht.length>=ct.length&&ht.split(ct).join('').indexOf('deepseekide-ops')>=0){found=true;break;}
            host=host.parentElement;
          }
          if(found)opsBodies.push(ct);
        }
      }catch(e2){}
    }
    // Кнопка «Продолжить»: DeepSeek обрезал ответ лимитом. Докликиваем сами
    // и ФОРСИМ busy — иначе тишина после обрезка засчитается как конец ответа.
    var cont=null;
    var btns=document.querySelectorAll('div[role=button].ds-button');
    for(var bi=0;bi<btns.length;bi++){
      var bt=(btns[bi].innerText||'').replace(/\s+/g,' ').trim();
      if(bt==='Продолжить'||bt==='Continue'){cont=btns[bi];break;}
    }
    if(cont){
      cont.click();
      window.__dsideActivity=now;
      return JSON.stringify({ok:1,n:n,t:last,b:1,c:1,p:!!document.querySelector('textarea'),u:String(location.href),obs:opsBodies});
    }
    var busy=(now-window.__dsideActivity)<1300||(now-window.__dsideSentAt)<2500;
    return JSON.stringify({ok:1,n:n,t:last,b:busy?1:0,c:0,p:!!document.querySelector('textarea'),u:String(location.href),obs:opsBodies});
  }catch(e){return JSON.stringify({ok:0,e:String(e)});}
})()
)JS";

// Экранирование строки как JS-литерала. DumpJson (error_handler replace)
// nikогда не кидает type_error.316 даже на битом UTF-8 — подменит U+FFFD.
std::string JsVar(const std::string& s) { return utf8::DumpJson(nlohmann::json(s)); }

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
  function txt(el){return (el.innerText||'').replace(/\s+/g,' ').trim();}
  // Кнопка «Новый чат» у DeepSeek — div[tabindex] БЕЗ role=button, поэтому
  // старый селектор 'div[role=button]' её просто не видел. Ищем шире.
  var nodes=document.querySelectorAll('[role=button],a,button,[tabindex="0"]');
  for(var i=0;i<nodes.length;i++){
    var t=txt(nodes[i]);
    if(t==='New chat'||t==='Новый чат'){nodes[i].click();return 'ok';}
  }
  // Запасной путь: точный span с текстом → клик ближайшего кликабельного предка.
  var spans=document.querySelectorAll('span');
  for(var j=0;j<spans.length;j++){
    var t2=txt(spans[j]);
    if(t2==='New chat'||t2==='Новый чат'){
      var p=spans[j].closest('[role=button],[tabindex="0"],a,button')||spans[j].parentElement;
      if(p){p.click();return 'ok_via_span';}
    }
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
  // ВАЖНО: фазу обязательно возвращаем в Idle — иначе после первого ответа
  // приложение навсегда «ещё жду предыдущее» (блокировка всех отправок).
  if (mPhase == Phase::Ready || mPhase == Phase::TimedOut) mPhase = Phase::Idle;
  mStatus.stage = "online";
  mStatus.stageText = "чат готов";
  return true;
}

// ---------------------------------------------------------------------------

std::string ChatDriver::BuildPrompt(const std::string& task) {
  std::string tree = "(проект не открыт — дерево недоступно)";
  if (mCfg.tools) {
    ToolRunResult r = mCfg.tools->Execute("list_files", {{"max_entries", 350}});
    if (!r.output.empty()) tree = r.output;
  }
  // Дерево собиралось из имён файлов ОС (на Windows возможны битые UTF-8) —
  // чистим до того, как строка уедет в JsVar/CDP.
  tree = utf8::Sanitize(tree);

  std::ostringstream p;
  p << "You are DeepSeekIDE Agent — an autonomous senior software engineer running INSIDE the "
       "user's IDE on their own machine. You ACT on the project; you do not merely chat. "
       "Answer in Russian by default (the user is Russian-speaking) unless they write English.\n\n"

       "# HOW TO CHANGE FILES (mandatory format)\n"
       "Emit every change ONLY inside fenced blocks:\n"
       "```deepseekide-ops\n"
       "[{\"name\":\"write_file\",\"args\":{\"path\":\"src/main.py\",\"content\":\"full file text\"}}]\n"
       "```\n"
       "- Multiple blocks per reply are allowed; they execute top-to-bottom AUTOMATICALLY and "
       "immediately, with no user confirmation.\n"
       "- Inside a block: STRICT JSON ONLY — one object {\"name\": ..., \"args\": {...}} or an array "
       "of such objects. No comments, no prose, no trailing commas.\n"
       "- Escape newlines inside JSON strings as \\n. Triple backticks are fine INSIDE string "
       "values (they are data, not block delimiters).\n"
       "- Code shown OUTSIDE an ops block is never applied — it is merely chat text.\n\n"

       "# TOOLBOX (use freely — autonomy is expected)\n"
       "- write_file {path, content} — create / FULLY rewrite a file. Always complete files, "
       "never \"// rest unchanged\" placeholders.\n"
       "- edit_file {path, old_string, new_string, replace_all?} — replace an EXACT fragment "
       "(byte-for-byte, including indentation).\n"
       "- append_file {path, content} — append at end of file.\n"
       "- insert_lines {path, line, content} — insert BEFORE given 1-based line "
       "(line = lineCount+1 appends at end).\n"
       "- replace_lines {path, start_line, end_line, content} — replace line range "
       "(empty content deletes lines).\n"
       "- make_dir {path} — create directory.  delete_path {path, recursive?} — delete "
       "(folders need recursive:true).  copy_file {from, to}.  move_file {from, to}.\n"
       "- run_command {command, timeout_sec?} — run a shell command in the project root: build, "
       "test, install deps, run scripts, git… Its stdout/stderr IS SENT BACK TO YOU as the next "
       "message, so verify results and iterate until green. Requires the user's shell permission; "
       "if the command reports that shell is disabled, fall back to file edits and tell the user "
       "how to enable it in Settings.\n\n"

       "# GETTING CONTEXT (self-service, zero user involvement)\n"
       "Need an existing file's content? Put this on ITS OWN LINE (outside ops blocks):\n"
       "NEED FILE: <project-relative path>\n"
       "The IDE automatically replies with its numbered content — use the numbers for "
       "insert_lines/replace_lines.\n"
       "Need to find where something is defined/used? Put on its own line:\n"
       "NEED SEARCH: <substring>\n"
       "The IDE replies with file:line matches.\n\n"

       "# WORK STYLE (contract)\n"
       "- ACT, DON'T ASK. Ops apply instantly; pick sensible defaults and mention them instead of "
       "asking permission. Only ask when truly blocked.\n"
       "- When a task implies building/testing, finish with a run_command op and iterate on "
       "failures until it is green.\n"
       "- Stay strictly inside the project root shown below. Never touch system paths, never run "
       "destructive commands (no rm -rf /, no drive formatting, no wiping of the project itself).\n"
       "- After the ops blocks, add a short human summary (few lines) of what was done — in plain "
       "text, outside blocks.\n"
       "- Keep prose tight. Prefer one well-planned change over three sloppy ones.\n\n"

       "# PROJECT STRUCTURE\n"
       << tree << "\n\n"
        "# TASK\n"
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
  mLastContinueMs = 0;
  mBaseline = -1;  // узнаем на первом удачном опросе после отправки
  if (q.isTask) { mSentFileReqs.clear(); mSentSearchReqs.clear(); }
  mReplyFull.clear();
  TouchStatus([](Status& s){ s.stage = "busy"; s.stageText = "отправлено, жду ответ…"; });
  mCfg.log("agent", std::string(q.isTask ? "Задача: " : "Заметка: ") +
                        (q.text.size() > 200 ? utf8::Truncate(q.text, 200) + "…" : q.text));
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
      s << " ошибка: " << ver.error << "\n";
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
      if (mCdp->Evaluate(kNewChatJs, v, e, 3000)) {
        if (v.find("not_found") != std::string::npos)
          mCfg.log("chat", "NewChat: кнопка «Новый чат» на странице не найдена");
        else
          mCfg.log("chat", "Новый чат нажат (" + v + ")");
      } else {
        mCfg.log("chat", "NewChat: " + e);
      }
      // Семантика «Новый чат» — бросить текущий диалог: ждать ответа и держать
      // старые накопители больше нечего. Иначе при settle в сессию падал бы
      // текст из УЖЕ закрытого разговора.
      {
        std::lock_guard<std::mutex> lk(mMtx);
        mPhase = Phase::Idle;
        mQueue.clear();
        mBaseline = -1;
        mReplyFull.clear();
        mSentFileReqs.clear();
        mSentSearchReqs.clear();
        mReplyPending = false;
        mPendingReply = ParsedOps{};
        mStatus.stageText = "новый чат — готов";
      }
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
        std::lock_guard<std::mutex> lk(mMtx);
        // Одиночный кашель evaluate (страница тормозит на отрисовке, жёсткий
        // GC и т.п.) — НЕ повод рвать CDP и гонять полный ре-аттач. Рвём
        // только после 3 подряд идущих провалов — это уже реальный обрыв.
        if (++mPollFails >= 3) {
          mPollFails = 0;
          mCfg.log("chat", "poll: " + err + " (3 подряд — разрыв и переподключение)");
          if (mCdp) mCdp->Close();
        } else {
          mCfg.log("chat", "poll: " + err + " (попытка " + std::to_string(mPollFails) + "/3)");
        }
        return;
      }
      std::lock_guard<std::mutex> lk(mMtx);
      mPollFails = 0;
      mStatus.chatPresent = st.value("p", 0) != 0;
      mStatus.busy = st.value("b", 0) != 0;
      const int n = st.value("n", 0);
      const long long nowL = NowMs();
      // Окно ответа длиной ~60000 символов доклеиваем к полному тексту.
      // ВАЖНО: только когда появился НОВЫЙ markdown-блок (n > mBaseline),
      // иначе склеили бы хвост ответа из ПРЕДЫДУЩЕГО диалога и ExtractOps
      // вытащил бы чужие ops. Пока блока нет — окно игнорируем.
      const std::string winT = st.value("t", std::string{});
      if ((mPhase == Phase::WaitingSettle || mPhase == Phase::Sending) && mBaseline >= 0 &&
          n > mBaseline) {
        stitch::AppendWindow(mReplyFull, winT);
      }
      const bool contClicked = st.value("c", 0) != 0;
      if (contClicked) {
        // На странице видна кнопка «Продолжить» — скрипт её только что нажал.
        // Ответ ещё пишется: сдвигаем дедлайн тишины.
        mLastContinueMs = nowL;
        if (mPhase == Phase::Sending || mPhase == Phase::WaitingSettle) {
          mPhase = Phase::WaitingSettle;
          mSettleSinceMs = 0;
        }
      }

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
            // Тишина после генерации — ответ готов.
            // Берём ПОЛНЫЙ склеенный текст: при длинных ответах с авто-«Продолжить»
            // окно t хранит лишь последние ~60000 символов, и ops-блоки из начала
            // без склейки терялись навсегда.
            ParsedOps parsed;
            const std::string& raw = !mReplyFull.empty() ? mReplyFull : winT;
            // DOM-путь: код-блоки распознаны на странице по баннеру языка —
            // в innerText забора (```) уже нет, текстовый ExtractOps их не найдёт.
            // Если бы мы ДОБАВИЛИ их к fence-результату, при смешанном рендере
            // append_file мог бы примениться дважды — поэтому DOM-путь первичен,
            // текстовый — только запасной (obs нет: например, сырой стрим).
            const auto obs = st.value("obs", nlohmann::json::array());
            bool usedDom = false;
            for (const auto& b : obs) {
              if (!b.is_string()) continue;
              dside::ParseOpsBody(b.get<std::string>(), dside::ChatAllowedOps(),
                                  static_cast<int>(parsed.ops.size() + 1), parsed);
              usedDom = true;
            }
            if (usedDom) {
              if (parsed.text.empty()) parsed.text = raw;
            } else {
              dside::ExtractOps(raw, dside::ChatAllowedOps(), parsed);
            }
            {
              mPendingReply = std::move(parsed);
              mReplyPending = true;
            }
            if (mCfg.onSession) {
              std::string task;
              { task = mLastUserText; }
              mCfg.onSession(task, mPendingReply);
            }
            // АВТОПРИМЕНЕНИЕ: ни таблиц "принять/отклонить", ни кликов — правки
            // уходят в проект сразу, отчёт — заметкой модели.
            if (mCfg.onAutoApply && !mPendingReply.ops.empty()) {
              const std::string rep = mCfg.onAutoApply(mPendingReply.ops);
              if (!rep.empty()) {
                std::string nerr;
                if (!SendNote("SYSTEM: ops applied automatically by DeepSeekIDE:\n\n```\n" +
                                  rep + "\n```\n",
                              nerr))
                  mCfg.log("warn", "не смог отправить отчёт авто-применения: " + nerr);
              }
            }
            mPhase = Phase::Ready;
            mStatus.stage = "online";
            mStatus.stageText = "ответ получен";
            mCfg.log("agent", "Ответ зафиксирован: " +
                                  std::to_string(mPendingReply.ops.size()) + " операций, " +
                                  std::to_string(mPendingReply.errors.size()) + " предупреждений");
            // Модель могла попросить файлы («НУЖЕН ФАЙЛ: <путь>») — по контракту
            // промпта присылаем содержимое следующим сообщением. Раньше эту
            // просьбу никто не выполнял, и диалог с файлами зависал.
            if (mCfg.tools) {
              const auto wants = dside::FindFileRequests(raw);
              const auto searches = dside::FindSearchRequests(raw);
              if (!wants.empty() || !searches.empty()) {
                // Поштучный дедуп: отсылаем только то, чего модель ещё не видела.
                // Раньше дедуп был по сигнатуре всей пачки целиком — при пачке >3
                // файлов повторный запрос блокировался, и хвост терялся навсегда.
                std::vector<std::string> todoFiles, todoSearches;
                for (const auto& w : wants)
                  if (mSentFileReqs.insert(w).second) todoFiles.push_back(w);
                for (const auto& q : searches)
                  if (mSentSearchReqs.insert(q).second) todoSearches.push_back(q);
                if (todoFiles.empty() && todoSearches.empty()) {
                  mCfg.log("agent",
                           "Модель ждёт файлы/поиск, которые уже отправлены — не дублирую.");
                } else {
                  std::ostringstream note;
                  note << "SYSTEM: auto-reply from DeepSeekIDE.\n\n";
                  int sent = 0;
                  size_t fsent = 0;
                  for (const auto& w : todoFiles) {
                    if (++sent > 3) {
                      note << "(more files pending — repeat NEED FILE for them to continue)\n";
                      break;
                    }
                    note << "You requested file content of «" << w << "» (lines are numbered; "
                            "use those numbers with insert_lines/replace_lines).\n";
                    ToolRunResult fr = mCfg.tools->Execute("read_file", {{"path", w}});
                    std::string body = utf8::Sanitize(fr.output);
                    if (body.size() > 120000)
                      body = utf8::Truncate(body, 120000) +
                             "\n…(truncated — first 120000 chars shown)\n";
                    note << "FILE \"" << w << "\"" << (fr.ok ? "" : " — READ ERROR: ")
                         << "\n```\n" << body << "\n```\n\n";
                    ++fsent;
                  }
                  int ssent = 0;
                  for (const auto& q : todoSearches) {
                    if (++ssent > 2) {
                      note << "(more searches pending — repeat NEED SEARCH for them)\n";
                      break;
                    }
                    note << "You searched the project for «" << q << "». Matches (file:line: text):\n";
                    ToolRunResult sr = mCfg.tools->Execute("search_files", {{"query", q}});
                    std::string body = utf8::Sanitize(sr.output);
                    if (body.size() > 60000)
                      body = utf8::Truncate(body, 60000) + "\n…(truncated)\n";
                    note << "SEARCH \"" << q << "\"" << (sr.ok ? "" : " — ERROR: ")
                         << "\n```\n" << body << "\n```\n\n";
                  }
                  mQueue.push_back(Queued{false, note.str()});
                  mCfg.log("agent", "Модель попросила " + std::to_string(todoFiles.size()) +
                                    " файл(ов), " + std::to_string(todoSearches.size()) +
                                    " поиск(ов) — отправляю автоматически в чат.");
                }
              }
            }
            mBaseline = -1;
            return;
          }
          if (nowL - (mLastContinueMs > mSentAtMs ? mLastContinueMs : mSentAtMs) > 6 * 60 * 1000) {
            mPhase = Phase::TimedOut;
            mStatus.stage = "online";
            mStatus.stageText = "чат готов";
            mError = "чат не ответил за 6 минут — ожидание снято.";
          }
          break;
      }

      // Живой прогресс генерации (settle выше уже вышел по return со своим текстом).
      if (mPhase == Phase::WaitingSettle) {
        mStatus.stageText =
            std::string(contClicked ? "ответ обрезан лимитом — продолжаю генерацию… ("
                                    : "пишу ответ… (") +
            std::to_string(mReplyFull.size()) + " символов" +
            (mReplyFull.size() > 60000 ? ", окно склеено)" : ")");
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
}

