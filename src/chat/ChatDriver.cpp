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
        mStatus.stage = "offline";
        mStatus.stageText = "браузер не найден";
        mStatus.error = err;
        mError = err;
        return false;
      }
    }
    mPort = platform::FindFreeTcpPort(19226, 19260);
    if (mPort == 0) {
      mStatus.error = "не удалось выбрать свободный порт CDP";
      return false;
    }
    std::string args =
        "--remote-debugging-port=" + std::to_string(mPort) + " --remote-allow-origins=* " +
        " --user-data-dir=\"" + platform::PathToStr(platform::ConfigDir() / "browser-profile") +
        "\" --no-first-run --no-default-browser-check --new-window \"" + mUrl + "\"";
    mCfg.log("chat", "Запускаю браузер: " + mBrowserExe);
    mStatus.stage = "launching";
    mStatus.stageText = "запускаю браузер…";
    if (!platform::LaunchDetached(mBrowserExe, args, err)) {
      mStatus.stage = "offline";
      mStatus.stageText = "не смог запустить браузер";
      mStatus.error = err;
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

  mStatus.browser = mBrowserExe;
  return true;
}

bool ChatDriver::AttachCdp(std::string& err) {
  mStatus.stage = "connecting";
  mStatus.stageText = "ищу вкладку chat.deepseek.com…";

  // Ищем page-target с нашим URL среди активных
  for (int attempt = 0; attempt < 20; ++attempt) {
    auto res = net::HttpClient::Get(
        "http://127.0.0.1:" + std::to_string(mPort) + "/json/list", {}, 1);
    if (res.Ok()) {
      auto arr = nlohmann::json::parse(res.body, nullptr, false);
      if (arr.is_array()) {
        for (const auto& t : arr) {
          const std::string type = t.value("type", std::string{});
          const std::string url = t.value("url", std::string{});
          if (type == "page" && url.rfind("https://chat.deepseek.com", 0) == 0) {
            const std::string ws = t.value("webSocketDebuggerUrl", std::string{});
            if (ws.empty()) break;
            mCdp = std::make_unique<CdpClient>();
            if (mCdp->Connect(ws, err, 4000)) {
              mCfg.log("chat", "CDP подключен к вкладке: " + url);
              mStatus.stage = "online";
              mStatus.stageText = "чат готов";
              mStatus.error.clear();
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
  err = "не нашёл вкладку chat.deepseek.com в отладочном браузере";
  mStatus.stage = "offline";
  mStatus.stageText = "нет вкладки чата";
  mStatus.error = err;
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
    mStatus.error = err;
    return false;
  }
  mPhase = Phase::Sending;
  mSentAtMs = NowMs();
  mSettleSinceMs = 0;
  mBaseline = -1;  // узнаем на первом удачном опросе после отправки
  mStatus.stage = "busy";
  mStatus.stageText = "отправлено, жду ответ…";
  mCfg.log("agent", std::string(q.isTask ? "Задача: " : "Заметка: ") +
                        (q.text.size() > 200 ? q.text.substr(0, 200) + "…" : q.text));
  return true;
}

// ---------------------------------------------------------------------------

void ChatDriver::ThreadMain() {
  mCfg.log("chat", "Драйвер чата запущен (порт CDP автовыбор)");
  while (!mStop.load()) {
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
      continue;
    }

    // 2) Периодика: запрос "новый чат"
    if (mNewChatRequested.exchange(false)) {
      std::string v, e;
      if (mCdp->Evaluate(kNewChatJs, v, e, 3000))
        mCfg.log("chat", "Новый чат нажат");
      else
        mCfg.log("chat", "NewChat: " + e);
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      continue;
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
        continue;
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
            mPhase = Phase::Ready;
            mStatus.stage = mStatus.chatPresent ? "online" : "online";
            mStatus.stageText = "ответ получен";
            mCfg.log("agent", "Ответ зафиксирован: " +
                                  std::to_string(mPendingReply.ops.size()) + " операций, " +
                                  std::to_string(mPendingReply.errors.size()) + " предупреждений");
            mBaseline = -1;
            continue;
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

  {
    std::lock_guard<std::mutex> lk(mMtx);
    mStatus.stage = "offline";
  }
  mCfg.log("chat", "Драйвер чата остановлен");
}
